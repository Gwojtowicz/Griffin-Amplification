#pragma once

// 60 Hz mains (1.39.0, grind 1.40.0): taking it out, and putting it in.
//
// HumFilter: the Hum Filter switch (AMP popup, under Bass). Narrow notches at 60 Hz and its
// harmonics up to 480 Hz on the input, before everything else (the gate included), for the hum a
// guitar's pickups and cables pick up from the mains. Each notch is 2.5 Hz wide (-3 dB), so notes
// next to a harmonic lose very little: B2 (123.5 Hz) is 3.5 Hz from 120 Hz and drops about 0.4 dB.
//
// Ripple: the Tube Lab's 60 Hz switch, Amount and Mix knobs (REAL FEEL row). A real amp's B+ comes
// from a full-wave rectifier into a reservoir cap, so it carries a 120 Hz ripple, bigger the more
// current the tubes draw. Modelled on the Tube Lab's output (1.40.0; 1.39.0 was a gentle gain wobble):
//   - Grind: a supply-starved stage whose clipping headroom drops with Amount and wobbles at 120 Hz,
//     more while you play hard, so sustained notes sputter and grind. Level-matched to your peaks.
//   - Hum: a faint, constant buzz (the ripple's shape plus a little 60 Hz from heaters and ground).
// Mix blends both with the untouched Tube Lab signal (in parallel; no delay between them). A
// behavioural model, not a power supply simulation. On/off, Amount and Mix glide (20 ms), so nothing
// clicks; off is bit-identical to having no ripple at all.

#include <algorithm>
#include <cmath>

namespace mains
{
constexpr double kMainsHz = 60.0;
constexpr double kPi = 3.14159265358979323846;

class HumFilter
{
public:
  void Reset(double sampleRate)
  {
    mNumNotches = 0;
    for (int k = 1; k <= kMaxHarmonics && kMainsHz * k < 0.45 * sampleRate; k++)
    {
      const double f0 = kMainsHz * k;
      const double w0 = 2.0 * kPi * f0 / sampleRate;
      const double alpha = std::sin(w0) / (2.0 * (f0 / kBandwidthHz));
      const double a0 = 1.0 + alpha, c = -2.0 * std::cos(w0);
      Notch& n = mNotches[mNumNotches++];
      n.b0 = n.b2 = 1.0 / a0;
      n.b1 = n.a1 = c / a0;
      n.a2 = (1.0 - alpha) / a0;
    }
    _Clear();
    mFadeStep = 1.0 / std::max(1.0, kFadeSeconds * sampleRate);
    mMix = mOn ? 1.0 : 0.0;
  }

  void SetOn(bool on) { mOn = on; }

  // In place, on the mono input.
  void Process(double* x, size_t numFrames)
  {
    if (!mOn && mMix == 0.0)
      return;
    for (size_t s = 0; s < numFrames; s++)
    {
      double y = x[s];
      for (int i = 0; i < mNumNotches; i++)
      {
        Notch& n = mNotches[i];
        const double out = n.b0 * y + n.z1;
        n.z1 = n.b1 * y - n.a1 * out + n.z2;
        n.z2 = n.b2 * y - n.a2 * out;
        y = out;
      }
      mMix = std::clamp(mMix + (mOn ? mFadeStep : -mFadeStep), 0.0, 1.0);
      const double m = mMix, w = m * m * m * (m * (6.0 * m - 15.0) + 10.0); // smootherstep
      x[s] += w * (y - x[s]);
    }
    if (!mOn && mMix == 0.0)
      _Clear(); // Faded out: the next switch-on starts from rest
  }

private:
  struct Notch
  {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
  };
  void _Clear()
  {
    for (auto& n : mNotches)
      n.z1 = n.z2 = 0.0;
  }

  static constexpr int kMaxHarmonics = 8; // 60 .. 480 Hz
  static constexpr double kBandwidthHz = 2.5;
  static constexpr double kFadeSeconds = 0.02;
  Notch mNotches[kMaxHarmonics];
  int mNumNotches = 0;
  bool mOn = false;
  double mMix = 0.0, mFadeStep = 0.0;
};

class Ripple
{
public:
  void Reset(double sampleRate)
  {
    mPhaseInc = kMainsHz / sampleRate;
    mPhase = 0.0;
    mEnv = 0.0;
    mRef = kRefStart;
    mRelease = std::exp(-1.0 / (kEnvReleaseSeconds * sampleRate));
    mRefDecay = std::pow(10.0, -kRefReleaseDBPerSecond / (20.0 * sampleRate));
    mGlide = 1.0 - std::exp(-1.0 / (kGlideSeconds * sampleRate));
    mAmount = mTarget;
    mMix = mMixTarget;
  }

  // on: the 60 Hz switch; amount 0..10; mix 0..1 (the grind's dry/wet).
  void SetTarget(bool on, double amount, double mix = 1.0)
  {
    mTarget = on ? std::clamp(amount, 0.0, 10.0) / 10.0 : 0.0;
    mMixTarget = std::clamp(mix, 0.0, 1.0);
  }

  // One sample of the Tube Lab's wet (amp) signal in, with the supply's grind and hum.
  double Process(double wet)
  {
    if (mTarget == 0.0 && mAmount == 0.0)
      return wet;
    mAmount += mGlide * (mTarget - mAmount);
    mMix += mGlide * (mMixTarget - mMix);
    if (mTarget == 0.0 && mAmount < 1e-9)
      mAmount = 0.0; // Fully off again: back to the untouched path
    mPhase += mPhaseInc;
    if (mPhase >= 1.0)
      mPhase -= 1.0;
    const double s = std::sin(2.0 * kPi * mPhase);
    // Full-wave rectified mains, zero mean, scaled to peak-to-peak 2 (-1.27 .. +0.73): the reservoir
    // cap's 120 Hz ripple, sagging (negative) between the rectifier's charging peaks.
    const double r = 2.0 * (std::fabs(s) - 2.0 / kPi);
    // The load: the signal's envelope against your recent loudest (as REAL FEEL's full power).
    const double a = std::fabs(wet);
    mEnv = a > mEnv ? a : mEnv * mRelease;
    mRef = std::max(std::max(mEnv, mRef * mRefDecay), kRefFloor);
    const double load = std::min(mEnv / mRef, 1.0);

    // The grind: a supply-starved stage. Its headroom drops 1.8 dB per Amount step (half your loudest
    // at 10; 4x over it at 0), and the ripple moves it at 120 Hz -- most under load, when the reservoir cap
    // drains furthest between peaks -- so held notes clip harder twice per mains cycle and sputter.
    // The clip is asymmetric (a shifted tanh, still 0 at 0), like a starved tube. Made up so a peak at
    // your loudest comes out at the same peak.
    // Above about 6 a steeper top end (A^4) takes it much further: 1.41.0, for a heavier Amount 10.
    const double top = mAmount * mAmount * mAmount * mAmount;
    const double head0 = mRef * kHeadAtZero * std::pow(10.0, -(kStarveDB * mAmount + kTopStarveDB * top) / 20.0);
    const double wobble = (kHeadRipple * mAmount + kTopRipple * top) * (kIdleShare + (1.0 - kIdleShare) * load);
    const double head = head0 * std::max(kMinHead, 1.0 + wobble * r);
    const double offset = std::tanh(kAsym);
    // (The negative side clips later, so it sets the make-up.)
    // Matched at the wobble's highest headroom (full load, the ripple's peak), so no peak comes out hotter.
    const double headHi = head0 * (1.0 + (kHeadRipple * mAmount + kTopRipple * top) * kRippleMax);
    const double makeup = mRef / (headHi * std::max(std::tanh(mRef / headHi + kAsym) - offset, offset - std::tanh(kAsym - mRef / headHi)));
    const double grind = makeup * head * (std::tanh(wet / head + kAsym) - offset);
    const double hum = mAmount * kHumAtFull * (0.5 * r + kHeater60 * s);
    // Mix fades the grind and hum in; below Amount 1 they fade with Amount too, so switching off
    // lands on the untouched signal without a step.
    const double w = mMix * std::min(1.0, mAmount * 10.0);
    return wet + w * (grind - wet + hum);
  }

private:
  static constexpr double kHeadAtZero = 4.0; // Headroom over your loudest at Amount 0 (nearly clean)
  static constexpr double kStarveDB = 18.0; // Amount 10 takes it 18 dB lower (half your loudest)
  static constexpr double kHeadRipple = 0.45; // Headroom swing per Amount (linear part)
  static constexpr double kTopStarveDB = 12.0; // The top end: Amount 10 is 30 dB under 4x your loudest
  static constexpr double kTopRipple = 0.4; // ... and swings -85% (floored) .. +62% at full load
  static constexpr double kMinHead = 0.1;
  static constexpr double kRippleMax = 2.0 * (1.0 - 2.0 / kPi); // r's peak (0.73)
  static constexpr double kIdleShare = 0.3; // Ripple with no load, relative to full
  static constexpr double kAsym = 0.4; // tanh shift: the stage clips one side first
  static constexpr double kHumAtFull = 0.0024; // Hum peak about -56 dBFS at Amount 10, Mix 100%
  static constexpr double kHeater60 = 0.15; // A little 60 Hz from the heaters and ground
  static constexpr double kEnvReleaseSeconds = 0.05;
  static constexpr double kRefStart = 0.1; // -20 dBFS until you play
  static constexpr double kRefFloor = 0.01; // -40 dBFS
  static constexpr double kRefReleaseDBPerSecond = 0.1; // 6 dB a minute
  static constexpr double kGlideSeconds = 0.02;

  double mPhase = 0.0, mPhaseInc = kMainsHz / 48000.0;
  double mEnv = 0.0, mRef = kRefStart, mRelease = 0.0, mRefDecay = 1.0;
  double mTarget = 0.0, mAmount = 0.0, mGlide = 1.0;
  double mMixTarget = 1.0, mMix = 1.0;
};
} // namespace mains
