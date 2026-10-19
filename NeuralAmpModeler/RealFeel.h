#pragma once

// REAL FEEL pedal (1.34.0): the Tube Lab's Real Feel switches as a block you place after the AMP
// block, so a captured amp's output gets the power section's feel instead of having it flattened
// by the capture (a high-gain capture's input saturation squashes any sag that happens before it).
//
// It's a behavioural model of a push-pull power section driven by the finished amp signal, not a
// circuit: the signal's level stands in for the power tubes' current draw.
//   - Full power is the loudest you've played lately: a peak follower that jumps to every new
//     peak and lets go 6 dB a minute (never below -40 dBFS), so it works at whatever level the AMP
//     block puts out, and harder picks sag more than softer ones, as on a real amp.
//   - Squish: the screen supply sags with the draw (rising steeply near full power, where a real
//     pentode's screen takes the current its plate can't): a hard pick drains it in ~20 ms and it
//     recovers over ~150 ms, and the power section's gain follows it, so the pick is squashed and
//     blooms back. Above 1x the steady part of the sag is taken back out (as the Tube Lab's Squish
//     keeps the screens' resting voltage), so it's feel rather than a quieter amp. Soft-capped at 7 dB.
//   - Shared B+: the preamp hangs off the same supply (through its own 50 ms decoupling), so the
//     sag also turns down the signal into the AMP block (PreampGain, applied by the plugin).
//   - OT Iron: the output transformer's primary saturates on big low notes (its magnetizing current
//     rises as flux^5 past the saturation flux), which takes low end out of the loudest notes. Only
//     the saturating part is modelled: the AMP block's capture already has the linear transformer.
//   - Coil Heat: the speaker coil warms with the power over a few seconds (3 s); its resistance rises
//     (copper, +0.39%/K), so the speaker draws less current and gets quieter, except around the cone
//     resonance (~100 Hz), where the motional impedance dominates -- so the low end bumps up relatively.
//   - Depth (1x real .. 10x) scales all four, as on the Tube Lab.
//   - Mix (1.39.0) blends the pedal's output with what came into it (no latency, so they line up),
//     and Shared B+'s turn-down of the AMP block's input by the same amount.
// Every switch crossfades (an S-curve over 20 ms) and Depth glides, so nothing clicks.

#include <algorithm>
#include <cmath>
#include <vector>

namespace realfeel
{
struct Params
{
  bool engaged = true; // Off: everything fades out (the pedal follows the Tube Lab's On switch)
  bool squish = false, sharedSupply = false, otIron = false, coilHeat = false;
  double depth = 4.0; // 1x .. 10x
  double mix = 1.0; // 0..1, dry/wet
};

class RealFeel
{
public:
  void Reset(double sampleRate)
  {
    mT = 1.0 / sampleRate;
    mRef = kRefStart;
    mSag = mSagSlow = mSagPre = 0.0;
    mCoilRise = 0.0;
    mPreGain = 1.0;
    for (auto& c : mCh)
      c = Channel();
    mMix[0] = mParams.engaged && mParams.squish ? 1.0 : 0.0;
    mMix[1] = mParams.engaged && mParams.sharedSupply ? 1.0 : 0.0;
    mMix[2] = mParams.engaged && mParams.otIron ? 1.0 : 0.0;
    mMix[3] = mParams.engaged && mParams.coilHeat ? 1.0 : 0.0;
    mDepth = mDepthMid = _DepthTarget();
    mWet = std::clamp(mParams.mix, 0.0, 1.0);
    const double kPi = 3.14159265358979;
    mFadeStep = mT / kFadeS;
    mRefDecay = std::pow(10.0, -kRefReleaseDBPerS * mT / 20.0);
    mSagCoef = 1.0 - std::exp(-mT / kSagTauS);
    mSagAttackCoef = 1.0 - std::exp(-mT / kSagAttackS);
    mSlowCoef = 1.0 - std::exp(-mT / kSlowTauS);
    mPreCoef = 1.0 - std::exp(-mT / kPreTauS);
    mDepthCoef = 1.0 - std::exp(-mT / kDepthGlideS);
    mCoilCoef = 1.0 - std::exp(-mT / kCoilTauS);
    mOtW = 2.0 * kPi * kOtCornerHz;
    mOtLeak = 2.0 * kPi * kOtLeakHz * mT;
    // Cone resonance band: an RBJ band-pass (0 dB peak).
    const double w0 = 2.0 * kPi * kResHz * mT, alpha = std::sin(w0) / (2.0 * kResQ), a0 = 1.0 + alpha;
    mBp[0] = alpha / a0;
    mBp[1] = 0.0;
    mBp[2] = -alpha / a0;
    mBp[3] = -2.0 * std::cos(w0) / a0;
    mBp[4] = (1.0 - alpha) / a0;
  }

  void SetParams(const Params& p) { mParams = p; }

  bool Active() const
  {
    return (mParams.engaged && (mParams.squish || mParams.sharedSupply || mParams.otIron || mParams.coilHeat))
           || mMix[0] > 0.0 || mMix[1] > 0.0 || mMix[2] > 0.0 || mMix[3] > 0.0;
  }

  // Shared B+: the gain for the signal into the AMP block (1 = none), as of the last sample.
  double PreampGain() const { return mPreGain; }

  // Processes nc (1 or 2) channels of nf samples into out (which may be in).
  void Process(const double* const* in, double** out, int nc, int nf)
  {
    if (!Active())
    {
      for (int c = 0; c < nc; c++)
        if (out[c] != in[c])
          std::copy(in[c], in[c] + nf, out[c]);
      mPreGain = 1.0;
      return;
    }
    const bool want[4] = {mParams.engaged && mParams.squish, mParams.engaged && mParams.sharedSupply,
                          mParams.engaged && mParams.otIron, mParams.engaged && mParams.coilHeat};
    for (int s = 0; s < nf; s++)
    {
      double w[4];
      for (int k = 0; k < 4; k++)
      {
        mMix[k] = std::clamp(mMix[k] + (want[k] ? mFadeStep : -mFadeStep), 0.0, 1.0);
        const double m = mMix[k];
        w[k] = m * m * m * (m * (6.0 * m - 15.0) + 10.0); // smootherstep
      }
      mDepthMid += mDepthCoef * (_DepthTarget() - mDepthMid);
      mDepth += mDepthCoef * (mDepthMid - mDepth);
      const double strength = mDepth * mDepth;
      const double wetTarget = std::clamp(mParams.mix, 0.0, 1.0);
      mWet += mDepthCoef * (wetTarget - mWet);
      if (std::fabs(wetTarget - mWet) < 1e-9)
        mWet = wetTarget; // Exactly 100% again: the output is exactly the pedal's

      // The draw: the louder channel, relative to full power.
      double peak = 0.0, power = 0.0;
      for (int c = 0; c < nc; c++)
      {
        peak = std::max(peak, std::fabs(in[c][s]));
        power += in[c][s] * in[c][s];
      }
      power /= nc;
      mRef = std::max(std::max(peak, mRef * mRefDecay), kRefFloor);
      const double d = std::min(peak / mRef, 1.0);

      // Screen supply sag (0 = at rest): the screens' draw rises steeply near full power.
      const double draw = d * (kScreenBase + (1.0 - kScreenBase) * d * d);
      const double sagTarget = kSagDepth * draw;
      mSag += (sagTarget > mSag ? mSagAttackCoef : mSagCoef) * (sagTarget - mSag);
      mSagSlow += mSlowCoef * (mSag - mSagSlow);
      mSagPre += mPreCoef * (mSag - mSagPre);
      double gain = 1.0;
      if (w[0] > 0.0)
      {
        // The power tubes' gain follows the screen voltage (^1.35, Koren's Ex), Depth^2 times harder.
        // Above 1x the steady part of the sag is taken back out (over ~1.5 s, by 4x fully), as the
        // Tube Lab's Squish keeps the screens' resting voltage: what's left is the feel -- the pick
        // squashed, then blooming back -- not just a quieter amp.
        const double above1x = std::min(1.0, (mDepth - 1.0) / 3.0);
        const double steady = above1x * mSagSlow;
        const double emphasis = strength * (1.0 + kSquishEmphasis * above1x);
        const double x = kEx * emphasis * std::log(1.0 - std::clamp(mSag - steady, 0.0, 0.9));
        gain = std::exp(w[0] * kCapNepers * std::tanh(x / kCapNepers));
      }
      mPreGain = 1.0 + mWet * w[1] * (std::max(kPreFloor, 1.0 - strength * kPreDepth * mSagPre) - 1.0);

      // Coil heat: temperature rise from the power, Depth^2 times faster per watt.
      mCoilRise += mCoilCoef * (std::min(kCoilMaxRiseK, kCoilKPerPower * strength * power / (mRef * mRef)) - mCoilRise);
      const double coilGain = 1.0 + w[3] * (1.0 / (1.0 + kCoilAlpha * mCoilRise) - 1.0);

      // OT saturation flux: 1x saturates only for full power below kOtSatHz; k x Depth moves that up.
      const double sat = mRef / (2.0 * 3.14159265358979 * kOtSatHz * (2.0 * strength - 1.0));
      for (int c = 0; c < nc; c++)
      {
        Channel& ch = mCh[c];
        double y = in[c][s];
        if (w[2] > 0.0)
          y += w[2] * (_SaturatingPrimary(ch, y, sat) - y);
        else
          ch.otFlux = ch.otPrevOut = 0.0;
        if (w[3] > 0.0)
        {
          const double bp = mBp[0] * y + ch.bpZ1;
          ch.bpZ1 = mBp[1] * y - mBp[3] * bp + ch.bpZ2;
          ch.bpZ2 = mBp[2] * y - mBp[4] * bp;
          y = coilGain * y + (1.0 - coilGain) * bp;
        }
        const double dry = in[c][s];
        out[c][s] = mWet == 1.0 ? y * gain : dry + mWet * (y * gain - dry);
      }
    }
  }

  // Probes for tests.
  double Sag() const { return mSag; }
  double Reference() const { return mRef; }
  double CoilRiseK() const { return mCoilRise; }

private:
  struct Channel
  {
    double otFlux = 0.0, otPrevOut = 0.0, bpZ1 = 0.0, bpZ2 = 0.0;
  };

  // The saturating part of the primary's magnetizing current shunts the signal:
  // out = in - w * flux * (flux / sat)^4, with the flux the (slightly leaky) integral of out,
  // trapezoidal. One monotone equation in the flux; Newton from the linear solution converges from
  // outside in a few steps.
  double _SaturatingPrimary(Channel& ch, double in, double sat)
  {
    const double k = 0.5 * mT;
    const double a = ch.otFlux * (1.0 - mOtLeak) + k * ch.otPrevOut + k * in;
    double l = a;
    for (int i = 0; i < 8; i++)
    {
      const double r = l / sat, r4 = r * r * r * r;
      const double f = l - a + k * mOtW * l * r4;
      const double step = f / (1.0 + 5.0 * k * mOtW * r4);
      l -= step;
      if (std::fabs(step) <= 1e-12 * (std::fabs(l) + sat))
        break;
    }
    const double r = l / sat;
    const double out = in - mOtW * l * r * r * r * r;
    ch.otFlux = l;
    ch.otPrevOut = out;
    return out;
  }

  double _DepthTarget() const { return std::clamp(mParams.depth, 1.0, 10.0); }

  static constexpr double kFadeS = 0.02;
  static constexpr double kDepthGlideS = 0.03;
  static constexpr double kRefStart = 0.1; // -20 dBFS until you play
  static constexpr double kRefFloor = 0.01; // -40 dBFS
  static constexpr double kRefReleaseDBPerS = 0.1; // 6 dB a minute
  // Squish: the screen supply's time constant, its sag at full power (1x), the screens' share of
  // the draw at low levels, the power tubes' Ex, and the gain cap (0.8 Np = 7 dB).
  static constexpr double kSagTauS = 0.15; // Recovery (the bloom)
  static constexpr double kSagAttackS = 0.02; // A hard pick drains the screen cap fast
  static constexpr double kSlowTauS = 1.5; // The steady sag taken back out above 1x
  static constexpr double kSagDepth = 0.12;
  static constexpr double kScreenBase = 0.3;
  static constexpr double kEx = 1.35;
  static constexpr double kCapNepers = 0.8;
  static constexpr double kSquishEmphasis = 5.0; // Extra pull on what's left, by 4x
  // Shared B+: the preamp's decoupling filter, and how far its supply moves per Depth.
  static constexpr double kPreTauS = 0.05;
  static constexpr double kPreDepth = 1.2;
  static constexpr double kPreFloor = 0.5; // -6 dB
  // OT Iron: the transformer's corner (as the Tube Lab's), the leak that keeps the flux centred, and
  // the frequency below which full power saturates it at 1x.
  static constexpr double kOtCornerHz = 40.0;
  static constexpr double kOtLeakHz = 5.0;
  static constexpr double kOtSatHz = 30.0;
  // Coil heat: copper's tempco, K per unit of mean-square power relative to full power (1x), time
  // constant, ceiling; the cone resonance band.
  static constexpr double kCoilAlpha = 0.0039;
  static constexpr double kCoilKPerPower = 40.0;
  static constexpr double kCoilTauS = 3.0;
  static constexpr double kCoilMaxRiseK = 250.0;
  static constexpr double kResHz = 100.0;
  static constexpr double kResQ = 2.0;

  Params mParams;
  double mT = 1.0 / 48000.0;
  double mRef = kRefStart, mRefDecay = 1.0;
  double mSagAttackCoef = 0.0;
  double mSag = 0.0, mSagSlow = 0.0, mSlowCoef = 0.0, mSagPre = 0.0, mSagCoef = 0.0, mPreCoef = 0.0, mPreGain = 1.0;
  double mCoilRise = 0.0, mCoilCoef = 0.0;
  double mMix[4] = {0.0, 0.0, 0.0, 0.0}, mFadeStep = 0.0;
  double mDepth = 4.0, mDepthMid = 4.0, mDepthCoef = 0.0;
  double mWet = 1.0; // Mix, gliding
  double mOtW = 0.0, mOtLeak = 0.0;
  double mBp[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
  Channel mCh[2];
};
} // namespace realfeel
