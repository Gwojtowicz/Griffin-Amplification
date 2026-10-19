#pragma once

// Live spectrum (1.46.0): what a point in the chain sounds like right now, for the PRO EQ block, the
// Output EQ page and the spectrum strip on the main window.
//
// * Tap (audio thread -> UI): a ring of the last 16384 samples (mono: the channels averaged). The
//   audio thread only writes samples and then publishes the write position (release); the UI reads
//   the newest window behind that position (acquire). No locks, nothing allocated on the audio
//   thread. A window the writer is overwriting while the UI reads it can tear for a frame -- it's a
//   picture, not audio, and 16384 samples is 4x the window, so that needs a UI stall of ~250 ms.
// * Analyzer (UI thread): an 8192-point FFT (5.9 Hz bins at 48 kHz) with a 4-term Blackman-Harris
//   window (sidelobes -92 dB), two taps at once in one complex FFT (one real signal in the real part,
//   the other in the imaginary part, separated by symmetry). Read out on a log frequency axis,
//   1/48 octave from 20 Hz: each point is the power average of the FFT bins in its band (so a sine
//   reads its true level, in dBFS for a full-scale sine), interpolated where bins are sparser than
//   points. Ballistics: rises at once, falls at 30 dB/s (smooth enough to read, fast enough to see
//   a pick), plus a peak hold that falls 6 dB/s after 1 s.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <cmath>
#include <complex>
#include <vector>


namespace spectrum
{
// What one EQ view shows (1.47.0), kept by the plugin so it survives a relayout: the analyzer on/off,
// frozen, which spectra, and the selected band (-1: none). onAnalyzerChanged (1.54.0): the plugin
// remembers the ANALYZER switch in ui.ini.
struct EqViewState
{
  bool analyzer = true, frozen = false, showA = true, showB = true;
  int selected = -1;
  std::function<void()> onAnalyzerChanged;
};

// In-place radix-2 complex FFT (forward, unscaled) -- the same as FastImpulseResponse.h's, kept here
// so this header stands alone.
class FFT
{
public:
  void Init(int n)
  {
    mN = n;
    mTwiddle.resize(n / 2);
    for (int k = 0; k < n / 2; k++)
      mTwiddle[k] = std::polar(1.0, -2.0 * 3.14159265358979323846 * k / n);
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

class Tap
{
public:
  static constexpr int kSize = 16384; // A power of two

  void SetSampleRate(double sr) { mSampleRate.store(sr, std::memory_order_relaxed); }
  double SampleRate() const { return mSampleRate.load(std::memory_order_relaxed); }

  // Audio thread. numChannels (1 or 2) x numFrames; the channels are averaged.
  template <class T>
  void Push(T** in, int numChannels, int numFrames)
  {
    // Only while a view on screen wants it (see KeepAlive): otherwise the audio thread does nothing.
    const int64_t alive = mAlive.load(std::memory_order_relaxed);
    if (alive <= 0)
      return;
    mAlive.store(alive - numFrames, std::memory_order_relaxed);
    uint32_t w = mWrite.load(std::memory_order_relaxed);
    const float scale = numChannels >= 2 ? 0.5f : 1.0f;
    for (int s = 0; s < numFrames; s++)
    {
      float v = (float)in[0][s];
      if (numChannels >= 2)
        v += (float)in[1][s];
      mBuf[w & (kSize - 1)] = v * scale;
      w++;
    }
    mWrite.store(w, std::memory_order_release);
  }

  // UI thread: the newest n samples (n <= kSize / 2), oldest first. Returns false before any audio.
  bool Read(float* out, int n) const
  {
    const uint32_t w = mWrite.load(std::memory_order_acquire);
    if (w == mLastSeenEmpty)
      return false;
    for (int i = 0; i < n; i++)
      out[i] = mBuf[(w - (uint32_t)n + (uint32_t)i) & (kSize - 1)];
    return true;
  }
  uint32_t WritePosition() const { return mWrite.load(std::memory_order_acquire); }

  // 1.47.0: only taps someone is looking at are fed. A view showing a live spectrum calls KeepAlive
  // every frame; half a second after the last call (the view hidden, its analyzer off or frozen) the
  // audio thread stops writing.
  void KeepAlive() { mAlive.store((int64_t)(0.5 * SampleRate()), std::memory_order_relaxed); }
  bool Enabled() const { return mAlive.load(std::memory_order_relaxed) > 0; }

private:
  float mBuf[kSize] = {};
  std::atomic<uint32_t> mWrite{0};
  std::atomic<double> mSampleRate{48000.0};
  std::atomic<int64_t> mAlive{0};
  const uint32_t mLastSeenEmpty = 0;
};

// 1.50.0: multi-resolution. One long FFT resolves the lows but averages ~170 ms, so the whole
// display lagged. Now four FFT sizes each cover a frequency range: 8192 below ~120 Hz, 4096 up to
// ~400 Hz, 2048 up to ~1.4 kHz, 1024 above. Each looks only at the newest N samples, so the mids
// and highs show a pick attack in 20-40 ms while the lows keep their detail. Neighbouring sizes
// cross-fade over half an octave around each split, so there's no seam. Measured: a 3 kHz note
// shows (within 6 dB) after 10 ms instead of 73 ms; 80 Hz still takes 87 ms (resolving it needs the
// long window). Two taps cost 0.6 ms per update (0.46 before).
class Analyzer
{
public:
  static constexpr int kNumRes = 4;
  static constexpr int kSizes[kNumRes] = {8192, 4096, 2048, 1024};
  static constexpr double kSplitHz[kNumRes - 1] = {120.0, 400.0, 1400.0};
  static constexpr int kFFTSize = 8192; // the longest
  static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
  static constexpr int kPointsPerOctave = 48;
  static constexpr double kFloorDB = -120.0;

  Analyzer()
  {
    const double kPi = 3.14159265358979323846;
    for (int r = 0; r < kNumRes; r++)
    {
      const int n = kSizes[r];
      mFFT[r].Init(n);
      mWindow[r].resize(n);
      double sum = 0.0;
      for (int k = 0; k < n; k++)
      {
        const double x = 2.0 * kPi * k / n;
        mWindow[r][k] = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
        sum += mWindow[r][k];
      }
      // A full-scale sine's bin power is (A sum / 2)^2: normalize so it reads 0 dBFS at every size.
      // (Shorter FFTs have wider bins, so hiss in the highs reads a few dB higher than it did with
      // 8192 alone; tones, which are what you judge an EQ by, read the same.)
      mNorm[r] = 4.0 / (sum * sum);
      mBins[r].resize(n);
      mPower[r][0].resize(n / 2 + 1);
      mPower[r][1].resize(n / 2 + 1);
    }
    mNumPoints = (int)std::ceil(std::log2(kMaxHz / kMinHz) * kPointsPerOctave) + 1;
    for (auto* v : {&mLevel[0], &mLevel[1], &mPeak[0], &mPeak[1]})
      v->assign(mNumPoints, kFloorDB);
    mHold[0].assign(mNumPoints, 0.0);
    mHold[1].assign(mNumPoints, 0.0);
    mSamples[0].resize(kFFTSize);
    mSamples[1].resize(kFFTSize);
    // Which size(s) each point reads: res r0, blended toward r0 + 1 by mBlend.
    mRes.resize(mNumPoints);
    mBlend.resize(mNumPoints);
    for (int i = 0; i < mNumPoints; i++)
    {
      const double oct = std::log2(PointHz(i));
      double pos = 0.0;
      for (double split : kSplitHz)
        pos += std::clamp((oct - std::log2(split)) / 0.5 + 0.5, 0.0, 1.0);
      mRes[i] = std::min((int)pos, kNumRes - 1);
      mBlend[i] = mRes[i] == kNumRes - 1 ? 0.0 : pos - mRes[i];
    }
  }

  int NumPoints() const { return mNumPoints; }
  static double PointHz(int i) { return kMinHz * std::pow(2.0, (double)i / kPointsPerOctave); }
  // Level and peak hold (dB) of tap 0 or 1 at point i.
  double Level(int tap, int i) const { return mLevel[tap][i]; }
  double Peak(int tap, int i) const { return mPeak[tap][i]; }
  bool HasData(int tap) const { return mHasData[tap]; }

  // Analyze the newest samples of up to two taps (either may be null); dt = seconds since the last
  // call (ballistics). About 2 ms for all four sizes, so fine for every UI frame.
  void Update(const Tap* a, const Tap* b, double dt)
  {
    const Tap* taps[2] = {a, b};
    bool got[2] = {false, false};
    for (int t = 0; t < 2; t++)
      if (taps[t])
      {
        got[t] = taps[t]->Read(mSamples[t].data(), kFFTSize);
        if (got[t] && taps[t]->WritePosition() == mLastWrite[t])
          got[t] = mHasData[t]; // Nothing new (audio stopped): keep falling from what's there
        mLastWrite[t] = taps[t]->WritePosition();
      }
    if (got[0] || got[1])
      for (int r = 0; r < kNumRes; r++)
      {
        const int n = kSizes[r], off = kFFTSize - n; // the newest n samples
        auto& bins = mBins[r];
        for (int k = 0; k < n; k++)
          bins[k] = {got[0] ? mWindow[r][k] * mSamples[0][off + k] : 0.0,
                     got[1] ? mWindow[r][k] * mSamples[1][off + k] : 0.0};
        mFFT[r].Forward(bins.data());
        // Two real signals in one FFT: X = (Z[k] + conj Z[N-k]) / 2, Y = (Z[k] - conj Z[N-k]) / 2i.
        for (int k = 0; k <= n / 2; k++)
        {
          const std::complex<double> zk = bins[k], zn = std::conj(bins[(n - k) & (n - 1)]);
          const std::complex<double> x = 0.5 * (zk + zn), y = std::complex<double>(0.0, -0.5) * (zk - zn);
          mPower[r][0][k] = std::norm(x) * mNorm[r];
          mPower[r][1][k] = std::norm(y) * mNorm[r];
        }
      }
    const Tap* any = taps[0] ? taps[0] : taps[1];
    const double sr = any ? std::max(1.0, any->SampleRate()) : 48000.0;
    const double fall = 30.0 * dt, peakFall = 6.0 * dt;
    for (int t = 0; t < 2; t++)
    {
      if (!taps[t])
        continue;
      for (int i = 0; i < mNumPoints; i++)
      {
        double db = kFloorDB;
        if (got[t])
        {
          const double hz = PointHz(i);
          const int r = mRes[i];
          const double f = mBlend[i];
          db = _PointDB(r, t, hz, sr);
          if (f > 0.0)
            db += f * (_PointDB(r + 1, t, hz, sr) - db);
        }
        double& level = mLevel[t][i];
        level = db >= level ? db : std::max(db, level - fall);
        double& peak = mPeak[t][i];
        if (level >= peak)
        {
          peak = level;
          mHold[t][i] = 1.0;
        }
        else if ((mHold[t][i] -= dt) <= 0.0)
          peak = std::max(level, peak - peakFall);
      }
      mHasData[t] = mHasData[t] || got[t];
    }
  }

private:
  // The level (dB) at hz from FFT size r.
  double _PointDB(int r, int t, double hz, double sr) const
  {
    const int n = kSizes[r];
    const auto& power = mPower[r][t];
    const double binHz = sr / n;
    const double lo = hz * std::pow(2.0, -0.5 / kPointsPerOctave), hi = hz * std::pow(2.0, 0.5 / kPointsPerOctave);
    const int k0 = (int)std::ceil(lo / binHz), k1 = (int)std::floor(hi / binHz);
    double p;
    if (k1 >= k0)
    {
      // Bins inside the band: the strongest, so a tone between points isn't averaged away.
      p = 0.0;
      for (int k = std::max(k0, 1); k <= std::min(k1, n / 2); k++)
        p = std::max(p, power[k]);
    }
    else
    {
      // Fewer bins than points: interpolate between the two nearest bins.
      const double pos = std::clamp(hz / binHz, 1.0, (double)n / 2 - 1);
      const int k = (int)pos;
      const double f = pos - k;
      p = power[k] + f * (power[k + 1] - power[k]);
    }
    return std::max(kFloorDB, 10.0 * std::log10(p + 1e-30));
  }

  FFT mFFT[kNumRes];
  std::vector<double> mWindow[kNumRes];
  double mNorm[kNumRes] = {};
  std::vector<std::complex<double>> mBins[kNumRes];
  std::vector<double> mPower[kNumRes][2];
  int mNumPoints = 0;
  std::vector<int> mRes;
  std::vector<double> mBlend;
  std::vector<double> mLevel[2], mPeak[2], mHold[2];
  std::vector<float> mSamples[2];
  uint32_t mLastWrite[2] = {0, 0};
  bool mHasData[2] = {false, false};
};
} // namespace spectrum
