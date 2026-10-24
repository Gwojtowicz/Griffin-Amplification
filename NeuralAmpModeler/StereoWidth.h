#pragma once

// Output Width and Widen (1.58.0, the OUTPUT page): mid/side on the final stereo output, after the
// Output EQ and before Master Out and the limiter. It replaces 1.55.0's Mono switch (Width 0%).
//
// * Width scales the side signal (L - R) / 2: 0% is mono (both outputs get (L + R) / 2), 100% is the
//   signal as it is, 200% doubles the difference between the sides.
// * Widen makes stereo out of what's in the middle -- for a mono guitar chain, everything. A copy of
//   the mid signal, delayed kDelayS and high-passed at kHighPassHz, is added to the side: the left
//   output gets mid + copy, the right mid - copy. Each side then has its own comb of peaks and dips
//   (complementary ones), which the ear hears as width, while the mono sum is exactly the mid --
//   nothing cancels when it's played through one speaker, unlike a Haas delay. The bass stays in the
//   middle (the high-pass). Width scales the widened side too, so Width 0% is still mono.
// Both glide over ~10 ms. At 100% / 0% and settled it's skipped: the output is untouched.

#include <algorithm>
#include <cmath>
#include <vector>

class StereoWidth
{
public:
  static constexpr double kDelayS = 0.011;
  static constexpr double kHighPassHz = 250.0;
  static constexpr double kWidenGain = 0.7; // The copy's level at Widen 100%

  void Reset(double sampleRate)
  {
    mDelay.assign(std::max(1, static_cast<int>(std::lround(kDelayS * sampleRate))), 0.0);
    mPos = 0;
    mHpX = mHpY = 0.0;
    mHpCoef = std::exp(-2.0 * 3.14159265358979 * kHighPassHz / sampleRate);
    mGlide = 1.0 - std::exp(-1.0 / (0.01 * sampleRate / 4.0)); // Settles in ~10 ms
    mWidth = mWidthTarget;
    mWiden = mWidenTarget;
  }

  // Width 0..2 (1 = as is), Widen 0..1.
  void SetTargets(double width, double widen)
  {
    mWidthTarget = std::clamp(width, 0.0, 2.0);
    mWidenTarget = std::clamp(widen, 0.0, 1.0);
  }

  bool Active() const { return !(mWidth == 1.0 && mWiden == 0.0 && mWidthTarget == 1.0 && mWidenTarget == 0.0); }

  void Process(double* l, double* r, int numFrames)
  {
    if (!Active())
      return;
    if (mWiden == 0.0 && mWidenTarget > 0.0)
    {
      // Widen starting from off: its delay line and filter hold old audio.
      std::fill(mDelay.begin(), mDelay.end(), 0.0);
      mHpX = mHpY = 0.0;
    }
    const int n = static_cast<int>(mDelay.size());
    for (int s = 0; s < numFrames; s++)
    {
      mWidth += mGlide * (mWidthTarget - mWidth);
      mWiden += mGlide * (mWidenTarget - mWiden);
      if (std::fabs(mWidth - mWidthTarget) < 1e-7)
        mWidth = mWidthTarget;
      if (std::fabs(mWiden - mWidenTarget) < 1e-7)
        mWiden = mWidenTarget;
      const double mid = 0.5 * (l[s] + r[s]), side = 0.5 * (l[s] - r[s]);
      double copy = 0.0;
      if (mWiden > 0.0)
      {
        const double delayed = mDelay[mPos];
        mDelay[mPos] = mid;
        mPos = mPos + 1 == n ? 0 : mPos + 1;
        mHpY = mHpCoef * (mHpY + delayed - mHpX);
        mHpX = delayed;
        copy = kWidenGain * mWiden * mHpY;
      }
      const double newSide = mWidth * (side + copy);
      l[s] = mid + newSide;
      r[s] = mid - newSide;
    }
  }

  // Stereo correlation of the last block's output (+1 mono .. 0 unrelated .. -1 out of phase), for
  // the OUTPUT page's meter (whether or not Width/Widen are doing anything).
  static double Correlation(const double* l, const double* r, int numFrames)
  {
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int s = 0; s < numFrames; s++)
    {
      lr += l[s] * r[s];
      ll += l[s] * l[s];
      rr += r[s] * r[s];
    }
    const double d = std::sqrt(ll * rr);
    return d > 1e-12 ? lr / d : 1.0;
  }

private:
  std::vector<double> mDelay = std::vector<double>(1, 0.0);
  int mPos = 0;
  double mHpX = 0.0, mHpY = 0.0, mHpCoef = 0.0, mGlide = 1.0;
  double mWidth = 1.0, mWiden = 0.0, mWidthTarget = 1.0, mWidenTarget = 0.0;
};
