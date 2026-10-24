#pragma once

// Output limiter (1.37.0): a stereo-linked lookahead peak limiter on the plugin's output, before the
// safety soft clip. Guitar tones put out sparse sub-millisecond pick transients several dB above
// everything else; turning the master up to make the rest louder pushed those into the soft clip.
// This turns just those down, smoothly, so the output never exceeds kCeiling:
//   - The gain each sample needs (ceiling / peak, or 1) is held at its lowest for the lookahead
//     window, released over kReleaseS, then averaged over the window -- so the gain glides down
//     ahead of a peak and reaches what that peak needs exactly when it plays (the audio is delayed
//     by the window less one sample). Every output sample is provably within the ceiling.
//   - Below the ceiling the gain is exactly 1: quieter material comes out untouched, only delayed.
// 1.58.0: the ceiling (-12 .. -1 dBFS) and release (5 .. 500 ms) are knobs on the OUTPUT page, and
// it reports the most gain reduction in each block for that page's meter.

#include <algorithm>
#include <cmath>
#include <vector>

class OutputLimiter
{
public:
  static constexpr double kCeiling = 0.8912509381337456; // -1 dBFS: under the soft clip's 0.9 knee (the most it goes to)
  static constexpr double kLookaheadS = 0.00067; // ~32 samples at 48 kHz
  static constexpr double kReleaseS = 0.03; // 100 ms pumped audibly after each pick; 30 ms barely moves the sustain

  // The ceiling (dBFS, at most -1) and release (ms). Cheap when nothing changed.
  void SetParams(double ceilingDB, double releaseMs)
  {
    if (ceilingDB != mCeilingDB)
    {
      mCeilingDB = ceilingDB;
      mCeiling = std::min(kCeiling, std::pow(10.0, ceilingDB / 20.0));
    }
    if (releaseMs != mReleaseMs)
    {
      mReleaseMs = releaseMs;
      mReleaseCoef = 1.0 - std::exp(-1.0 / (std::max(releaseMs, 1.0) * 0.001 * mSampleRate));
    }
  }

  void Reset(double sampleRate)
  {
    mSampleRate = sampleRate;
    mWindow = std::max(2, static_cast<int>(std::lround(kLookaheadS * sampleRate)));
    mReq.assign(mWindow, 1.0);
    mBox.assign(mWindow, 1.0);
    mDelayL.assign(mWindow, 0.0);
    mDelayR.assign(mWindow, 0.0);
    mSum = mWindow;
    mRelease = 1.0;
    mPos = 0;
    mReleaseCoef = 1.0 - std::exp(-1.0 / (std::max(mReleaseMs, 1.0) * 0.001 * sampleRate));
  }

  int Latency() const { return mWindow - 1; }

  // In place, both channels (pass the same pointer twice for mono).
  void Process(double* l, double* r, int numFrames)
  {
    double minGain = 1.0;
    for (int s = 0; s < numFrames; s++)
    {
      const double peak = std::max(std::fabs(l[s]), std::fabs(r[s]));
      mReq[mPos] = peak > mCeiling ? mCeiling / peak : 1.0;
      double held = 1.0;
      for (double g : mReq)
        held = std::min(held, g);
      mRelease = held < mRelease ? held : mRelease + (held - mRelease) * mReleaseCoef;
      mSum += mRelease - mBox[mPos];
      mBox[mPos] = mRelease;
      const double gain = std::min(1.0, mSum / mWindow);
      minGain = std::min(minGain, gain);
      // The sample written mWindow - 1 samples ago, which this gain was built for.
      const int oldest = mPos + 1 == mWindow ? 0 : mPos + 1;
      const double dl = mDelayL[oldest], dr = mDelayR[oldest];
      mDelayL[mPos] = l[s];
      mDelayR[mPos] = r[s];
      l[s] = dl * gain;
      if (r != l)
        r[s] = dr * gain;
      mPos = oldest;
    }
    // Re-sum the window now and then so the running sum can't drift.
    mSum = 0.0;
    for (double g : mBox)
      mSum += g;
    mBlockMinGain = minGain;
  }

  // The most gain reduction in the last block (dB, <= 0).
  double BlockGainReductionDB() const { return 20.0 * std::log10(std::max(1e-9, mBlockMinGain)); }

  // How far the gain is down right now (dB, <= 0), for a meter or a test.
  double GainReductionDB() const { return 20.0 * std::log10(std::max(1e-9, std::min(1.0, mSum / mWindow))); }

private:
  int mWindow = 32, mPos = 0;
  std::vector<double> mReq, mBox, mDelayL, mDelayR;
  double mSum = 32.0, mRelease = 1.0, mReleaseCoef = 0.0;
  double mSampleRate = 48000.0, mCeiling = kCeiling, mCeilingDB = -1.0, mReleaseMs = kReleaseS * 1000.0;
  double mBlockMinGain = 1.0;
};
