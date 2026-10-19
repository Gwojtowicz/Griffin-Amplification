#pragma once

// A small autocorrelation-based chromatic tuner. Operates on the raw (pre-gate, pre-amp) mono
// input signal so it reflects the actual string pitch, not the processed amp tone.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

class Tuner
{
public:
  void SetSampleRate(double sampleRate)
  {
    // The pitch search costs ~(rate)^2, so it runs on a copy decimated to ~22-24 kHz (plenty for
    // fundamentals up to 1 kHz): at 192 kHz the full-rate search was ~16x the 48 kHz cost, about a
    // full CPU core in bursts. The decimation is a simple average of each group of samples.
    mDecimation = std::max(1, (int)std::lround(sampleRate / 24000.0));
    mSampleRate = sampleRate / mDecimation;
    // ~60 ms is enough for a couple of periods of a low E (82Hz) while staying responsive.
    mBufferSize = (int)(mSampleRate * 0.06);
    mBuffer.assign(mBufferSize, 0.0);
    // Preallocated so the audio thread never allocates.
    mWork.assign(mBufferSize, 0.0);
    mNsdf.assign(mBufferSize, 0.0);
    mMinLag = std::max(2, (int)(mSampleRate / kMaxFreq));
    mMaxLag = std::min(mBufferSize - 2, (int)(mSampleRate / kMinFreq));
    mWritePos = 0;
    mAcc = 0.0;
    mAccCount = 0;
    mFilled = false;
    mSearchLag = -1;
  }

  // The pitch search only runs while someone is looking (the tuner page is open), 1.52.2. Set from
  // the UI thread. The level readout and noise floor keep running either way: they're cheap.
  void SetActive(bool active) { mActive.store(active); }

  // Feed mono samples in from ProcessBlock. Each full window gets a level check at once; the pitch
  // search over it is spread across the next half window of blocks (1.52.2). Done in one go, it
  // was a burst of ~0.5M multiply-adds every 60 ms: about 3x a whole block's average DSP cost, in
  // a single block, often enough to crackle at small buffers.
  void Process(iplug::sample** inputs, int numFrames)
  {
    if (mBuffer.empty())
      return;
    int produced = 0;
    for (int s = 0; s < numFrames; s++)
    {
      mAcc += inputs[0][s];
      if (++mAccCount < mDecimation)
        continue;
      mBuffer[mWritePos] = mAcc / mDecimation;
      mAcc = 0.0;
      mAccCount = 0;
      produced++;
      mWritePos++;
      if (mWritePos >= mBufferSize)
      {
        mWritePos = 0;
        mFilled = true;
        _WindowFilled();
      }
    }
    if (mSearchLag >= 0)
    {
      // Lags per decimated sample so the search ends halfway to the next window.
      const int numLags = mMaxLag - mMinLag + 1;
      const int budget = std::max(1, (2 * produced * numLags + mBufferSize - 1) / mBufferSize);
      _Search(budget);
    }
  }

  // Hz, or 0.0 if no confident pitch was detected (silence / noise). Safe to read from the UI thread.
  double GetFrequencyHz() const { return mDetectedFreq.load(); }
  // RMS of the last analysis window (linear), for the "Input: -xx dB" readout. UI-thread safe.
  double GetInputRMS() const { return mInputRMS.load(); }

private:
  void _WindowFilled()
  {
    double rms = 0.0;
    for (double v : mBuffer)
      rms += v * v;
    rms = std::sqrt(rms / mBufferSize);
    mInputRMS.store(rms);

    // Level gate, relative to the input's own noise floor (1.29.1). The fixed -50 dBFS gate it
    // replaces ignored a normally played guitar on a modest interface level. The floor follows the
    // quietest windows down at once and creeps up slowly (~0.3 dB/s, 1.29.2; slower than a ringing
    // note decays, so the floor can't catch up with the note and gate it out); hum near the floor
    // stays gated out even though it's periodic.
    mNoiseFloor = std::max(1e-5, rms < mNoiseFloor ? rms : mNoiseFloor * 1.002);
    if (!mActive.load())
    {
      mSearchLag = -1;
      mDetectedFreq.store(0.0);
      return;
    }
    if (mSearchLag >= 0)
      _Search(mMaxLag - mMinLag + 1); // Unfinished (can't happen with the budget above): finish it
    if (rms < std::max(kMinRMS, 4.0 * mNoiseFloor)) // 12 dB over the floor: a note mixed with hum misreads
    {
      mDetectedFreq.store(0.0);
      return;
    }
    std::copy(mBuffer.begin(), mBuffer.end(), mWork.begin());
    mSearchLag = mMinLag;
    mBestValue = -1.0;
    mBestLag = -1;
  }

  // Normalized autocorrelation (NSDF-style) over the next `count` candidate lags of mWork; picks the
  // pitch once the last lag is done.
  void _Search(int count)
  {
    std::vector<double>& nsdf = mNsdf; // Preallocated in SetSampleRate (size >= maxLag + 1)
    const int end = std::min(mMaxLag, mSearchLag + count - 1);
    for (int lag = mSearchLag; lag <= end; lag++)
    {
      double num = 0.0, denom = 0.0;
      const int n = mBufferSize - lag;
      for (int i = 0; i < n; i++)
      {
        num += mWork[i] * mWork[i + lag];
        denom += mWork[i] * mWork[i] + mWork[i + lag] * mWork[i + lag];
      }
      const double value = denom > 1e-12 ? (2.0 * num / denom) : 0.0;
      nsdf[lag] = value;
      if (value > mBestValue)
      {
        mBestValue = value;
        mBestLag = lag;
      }
    }
    mSearchLag = end + 1;
    if (mSearchLag > mMaxLag)
    {
      mSearchLag = -1;
      _Pick();
    }
  }

  void _Pick()
  {
    const std::vector<double>& nsdf = mNsdf;
    const int minLag = mMinLag, maxLag = mMaxLag;
    int bestLag = mBestLag;
    double bestValue = mBestValue;

    // A signal that repeats every period also repeats every 2, 3... periods, and at a coarse
    // (decimated) lag resolution the true peak can score a hair under a multiple -- an octave
    // error. So take the first local peak within 90% of the best (McLeod's pitch method), not the
    // global best.
    if (bestLag > minLag && bestValue >= 0.5)
    {
      const double threshold = 0.9 * bestValue;
      for (int lag = minLag + 1; lag < maxLag; lag++)
      {
        if (nsdf[lag] >= threshold && nsdf[lag] >= nsdf[lag - 1] && nsdf[lag] >= nsdf[lag + 1])
        {
          bestLag = lag;
          bestValue = nsdf[lag];
          break;
        }
      }
    }

    if (bestLag <= minLag || bestLag >= maxLag || bestValue < 0.5)
    {
      mDetectedFreq.store(0.0);
      return;
    }

    // Parabolic interpolation around the peak for sub-sample lag precision.
    const double y0 = nsdf[bestLag - 1], y1 = nsdf[bestLag], y2 = nsdf[bestLag + 1];
    const double denom = (y0 - 2.0 * y1 + y2);
    const double shift = denom != 0.0 ? 0.5 * (y0 - y2) / denom : 0.0;
    const double refinedLag = bestLag + std::clamp(shift, -1.0, 1.0);

    mDetectedFreq.store(mSampleRate / refinedLag);
  }

  static constexpr double kMinFreq = 55.0; // below low E, for margin
  static constexpr double kMaxFreq = 1000.0;
  double mSampleRate = 24000.0; // The decimated rate the search runs at
  int mDecimation = 2;
  double mAcc = 0.0;
  int mAccCount = 0;
  int mBufferSize = 1440;
  std::vector<double> mBuffer, mWork, mNsdf;
  int mWritePos = 0;
  bool mFilled = false;
  int mMinLag = 2, mMaxLag = 2;
  int mSearchLag = -1; // Next lag to search in mWork, or -1 when no search is running
  int mBestLag = -1;
  double mBestValue = -1.0;
  std::atomic<bool> mActive{false};
  std::atomic<double> mDetectedFreq{0.0};
  std::atomic<double> mInputRMS{0.0};
  static constexpr double kMinRMS = 3e-5; // -90 dBFS: below this, nothing is worth reading
  double mNoiseFloor = 1.0; // drops to the first window's level at once
};
