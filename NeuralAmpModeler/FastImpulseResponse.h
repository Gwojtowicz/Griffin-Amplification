#pragma once

// FastImpulseResponse (1.35.0): the cab IR -- dsp::ImpulseResponse, with exactly its weights (the
// same resampling, -18 dB gain and 8192-tap limit) -- convolved by FFT instead of a dot product per
// sample. A full 8192-tap IR cost ~5% of a core per channel that way (8192 multiply-adds a sample);
// this is several times cheaper and adds no latency:
//   - The IR is cut into kPartition-sample pieces. The first is convolved directly, sample by
//     sample, so each output sample still depends on the input up to and including itself.
//   - The rest go through a uniformly partitioned overlap-save convolution: each time kPartition
//     input samples have arrived, their spectrum (FFT of the last 2 x kPartition samples) joins a
//     delay line of spectra, and the next kPartition samples' share of every later piece is one
//     multiply-add per spectrum bin per piece, then one inverse FFT.
// The arithmetic is double precision; the original's float dot product differs from it by ~1e-7.
// 1.52.2: the direct part and the spectrum multiply-adds run in SSE2 (2 doubles at a time, the
// spectra stored as separate real/imaginary arrays), about 2.5x faster. Only the double-precision
// summation order changed (~1e-16).

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

#include <emmintrin.h>

#include "../AudioDSPTools/dsp/ImpulseResponse.h"
#include "../AudioDSPTools/dsp/Resample.h"

namespace fastir
{
// In-place radix-2 complex FFT of a fixed power-of-two size.
class FFT
{
public:
  void Init(int n)
  {
    mN = n;
    mTwiddle.resize(n / 2);
    const double kPi = 3.14159265358979323846;
    for (int k = 0; k < n / 2; k++)
      mTwiddle[k] = std::polar(1.0, -2.0 * kPi * k / n);
    mBitRev.resize(n);
    int bits = 0;
    while ((1 << bits) < n)
      bits++;
    for (int i = 0; i < n; i++)
    {
      int r = 0;
      for (int b = 0; b < bits; b++)
        r |= ((i >> b) & 1) << (bits - 1 - b);
      mBitRev[i] = r;
    }
  }

  // Forward (e^-i), unscaled. Inverse: conjugate in, conjugate out, scale by 1/n (done by the caller).
  void Forward(std::complex<double>* x) const
  {
    for (int i = 0; i < mN; i++)
      if (i < mBitRev[i])
        std::swap(x[i], x[mBitRev[i]]);
    for (int len = 2; len <= mN; len <<= 1)
    {
      const int half = len >> 1, step = mN / len;
      for (int start = 0; start < mN; start += len)
        for (int k = 0; k < half; k++)
        {
          const std::complex<double> t = mTwiddle[k * step] * x[start + k + half];
          x[start + k + half] = x[start + k] - t;
          x[start + k] += t;
        }
    }
  }

private:
  int mN = 0;
  std::vector<std::complex<double>> mTwiddle;
  std::vector<int> mBitRev;
};

// Zero-latency convolution of a mono signal with a fixed IR (see the top of the file).
class Convolver
{
public:
  static constexpr int kPartition = 128;
  static constexpr int kFFTSize = 2 * kPartition;
  static constexpr int kBins = kPartition + 1; // Real input: bins 0..P carry everything
  static constexpr int kBinsPadded = kBins + 1; // Even, for 2-wide SSE2; the extra bin stays zero

  void Init(const std::vector<double>& h)
  {
    const int n = (int)h.size();
    mHeadReversed.assign(kPartition, 0.0);
    for (int j = 0; j < kPartition && j < n; j++)
      mHeadReversed[kPartition - 1 - j] = h[j];
    mNumTail = n > kPartition ? (n - kPartition + kPartition - 1) / kPartition : 0;
    mFFT.Init(kFFTSize);
    mTailRe.assign((size_t)mNumTail * kBinsPadded, 0.0);
    mTailIm.assign((size_t)mNumTail * kBinsPadded, 0.0);
    std::vector<std::complex<double>> buf(kFFTSize);
    for (int k = 0; k < mNumTail; k++)
    {
      std::fill(buf.begin(), buf.end(), std::complex<double>(0.0, 0.0));
      for (int j = 0; j < kPartition; j++)
      {
        const int tap = (k + 1) * kPartition + j;
        if (tap < n)
          buf[j] = h[tap];
      }
      mFFT.Forward(buf.data());
      for (int b = 0; b < kBins; b++)
      {
        mTailRe[(size_t)k * kBinsPadded + b] = buf[b].real();
        mTailIm[(size_t)k * kBinsPadded + b] = buf[b].imag();
      }
    }
    mInputRe.assign((size_t)std::max(mNumTail, 1) * kBinsPadded, 0.0);
    mInputIm.assign((size_t)std::max(mNumTail, 1) * kBinsPadded, 0.0);
    mWork.assign(kFFTSize, {0.0, 0.0});
    mAccRe.assign(kBinsPadded, 0.0);
    mAccIm.assign(kBinsPadded, 0.0);
    mBuf.assign(kFFTSize, 0.0);
    mTailOut.assign(kPartition, 0.0);
    mNewest = 0;
    mPos = 0;
  }

  // Forgets the input heard so far (as after Init), without reallocating: safe on the audio thread.
  void Clear()
  {
    std::fill(mInputRe.begin(), mInputRe.end(), 0.0);
    std::fill(mInputIm.begin(), mInputIm.end(), 0.0);
    std::fill(mBuf.begin(), mBuf.end(), 0.0);
    std::fill(mTailOut.begin(), mTailOut.end(), 0.0);
    mNewest = 0;
    mPos = 0;
  }

  double Process(double x)
  {
    // mBuf: the previous partition's input, then this one's so far.
    mBuf[kPartition + mPos] = x;
    const double* in = mBuf.data() + mPos + 1; // x[n - (P-1)] .. x[n]
    // Direct part: 4 independent 2-wide sums, so the adds don't wait on each other.
    const double* h = mHeadReversed.data();
    __m128d s0 = _mm_setzero_pd(), s1 = _mm_setzero_pd(), s2 = _mm_setzero_pd(), s3 = _mm_setzero_pd();
    for (int m = 0; m < kPartition; m += 8)
    {
      s0 = _mm_add_pd(s0, _mm_mul_pd(_mm_loadu_pd(h + m), _mm_loadu_pd(in + m)));
      s1 = _mm_add_pd(s1, _mm_mul_pd(_mm_loadu_pd(h + m + 2), _mm_loadu_pd(in + m + 2)));
      s2 = _mm_add_pd(s2, _mm_mul_pd(_mm_loadu_pd(h + m + 4), _mm_loadu_pd(in + m + 4)));
      s3 = _mm_add_pd(s3, _mm_mul_pd(_mm_loadu_pd(h + m + 6), _mm_loadu_pd(in + m + 6)));
    }
    const __m128d sum = _mm_add_pd(_mm_add_pd(s0, s1), _mm_add_pd(s2, s3));
    const double y = mTailOut[mPos] + (_mm_cvtsd_f64(sum) + _mm_cvtsd_f64(_mm_unpackhi_pd(sum, sum)));
    if (++mPos == kPartition)
    {
      _CompletePartition();
      mPos = 0;
    }
    return y;
  }

private:
  // A partition of input is complete: add its spectrum to the delay line and work out every later
  // piece's contribution to the next partition of output.
  void _CompletePartition()
  {
    if (mNumTail > 0)
    {
      for (int i = 0; i < kFFTSize; i++)
        mWork[i] = mBuf[i];
      mFFT.Forward(mWork.data());
      mNewest = (mNewest + 1) % mNumTail;
      {
        double* xr = mInputRe.data() + (size_t)mNewest * kBinsPadded;
        double* xi = mInputIm.data() + (size_t)mNewest * kBinsPadded;
        for (int b = 0; b < kBins; b++)
        {
          xr[b] = mWork[b].real();
          xi[b] = mWork[b].imag();
        }
      }
      // Output partition n+1 gets piece k (k = 1..K) of the IR applied to input partition n+1-k:
      // the newest spectrum for piece 1, the one before it for piece 2, and so on.
      std::fill(mAccRe.begin(), mAccRe.end(), 0.0);
      std::fill(mAccIm.begin(), mAccIm.end(), 0.0);
      double* ar = mAccRe.data();
      double* ai = mAccIm.data();
      for (int k = 0; k < mNumTail; k++)
      {
        const int slot = (mNewest - k + mNumTail) % mNumTail;
        const double* Xr = mInputRe.data() + (size_t)slot * kBinsPadded;
        const double* Xi = mInputIm.data() + (size_t)slot * kBinsPadded;
        const double* Hr = mTailRe.data() + (size_t)k * kBinsPadded;
        const double* Hi = mTailIm.data() + (size_t)k * kBinsPadded;
        for (int b = 0; b < kBinsPadded; b += 2)
        {
          const __m128d xr = _mm_loadu_pd(Xr + b), xi = _mm_loadu_pd(Xi + b);
          const __m128d hr = _mm_loadu_pd(Hr + b), hi = _mm_loadu_pd(Hi + b);
          _mm_storeu_pd(ar + b, _mm_add_pd(_mm_loadu_pd(ar + b), _mm_sub_pd(_mm_mul_pd(xr, hr), _mm_mul_pd(xi, hi))));
          _mm_storeu_pd(ai + b, _mm_add_pd(_mm_loadu_pd(ai + b), _mm_add_pd(_mm_mul_pd(xr, hi), _mm_mul_pd(xi, hr))));
        }
      }
      // Inverse FFT of the (Hermitian) sum: conjugate, forward, conjugate, scale.
      for (int b = 0; b < kBins; b++)
        mWork[b] = {ar[b], -ai[b]};
      for (int b = kBins; b < kFFTSize; b++)
        mWork[b] = {ar[kFFTSize - b], ai[kFFTSize - b]}; // conj(conj(...)) of the mirrored bin
      mFFT.Forward(mWork.data());
      const double scale = 1.0 / kFFTSize;
      // Overlap-save: the second half is the linear convolution.
      for (int i = 0; i < kPartition; i++)
        mTailOut[i] = mWork[kPartition + i].real() * scale;
    }
    std::memmove(mBuf.data(), mBuf.data() + kPartition, sizeof(double) * kPartition);
  }

  FFT mFFT;
  std::vector<double> mHeadReversed;
  int mNumTail = 0;
  std::vector<std::complex<double>> mWork;
  std::vector<double> mTailRe, mTailIm, mInputRe, mInputIm, mAccRe, mAccIm; // kBinsPadded per spectrum
  std::vector<double> mBuf, mTailOut;
  int mNewest = 0, mPos = 0;
};
} // namespace fastir

class FastImpulseResponse : public dsp::ImpulseResponse
{
public:
  FastImpulseResponse(const char* fileName, const double sampleRate)
  : dsp::ImpulseResponse(fileName, sampleRate)
  {
    _Build(sampleRate);
  }
  FastImpulseResponse(const IRData& irData, const double sampleRate)
  : dsp::ImpulseResponse(irData, sampleRate)
  {
    _Build(sampleRate);
  }

  // As dsp::ImpulseResponse::Process: convolves channel 0 and copies it to every channel.
  double** Process(double** inputs, const size_t numChannels, const size_t numFrames) override
  {
    _PrepareBuffers(numChannels, numFrames);
    for (size_t i = 0; i < numFrames; i++)
      mOutputs[0][i] = mConvolver.Process(inputs[0][i]);
    for (size_t c = 1; c < numChannels; c++)
      for (size_t i = 0; i < numFrames; i++)
        mOutputs[c][i] = mOutputs[0][i];
    return _GetPointers();
  }

  // 1.42.0: silence in the convolution's history (an A/B slot coming back in).
  void ClearHistory() { mConvolver.Clear(); }

private:
  // The weights exactly as dsp::ImpulseResponse::_SetWeights makes them (float arithmetic included).
  void _Build(const double sampleRate)
  {
    std::vector<double> h;
    if (GetWavState() == dsp::wav::LoadReturnCode::SUCCESS)
    {
      const IRData data = GetData();
      std::vector<float> resampled;
      if (data.mRawAudioSampleRate == sampleRate)
        resampled = data.mRawAudio;
      else
      {
        std::vector<float> padded(data.mRawAudio.size() + 2, 0.0f);
        std::memcpy(padded.data() + 1, data.mRawAudio.data(), sizeof(float) * data.mRawAudio.size());
        dsp::ResampleCubic<float>(padded, data.mRawAudioSampleRate, sampleRate, 0.0, resampled);
      }
      const size_t irLength = std::min(resampled.size(), kMaxLength);
      const float gain = pow(10, -18 * 0.05) * 48000 / sampleRate;
      h.resize(irLength);
      for (size_t i = 0; i < irLength; i++)
        h[i] = (double)(float)(gain * resampled[i]);
    }
    mConvolver.Init(h);
  }

  static constexpr size_t kMaxLength = 8192; // As dsp::ImpulseResponse::mMaxLength
  fastir::Convolver mConvolver;
};
