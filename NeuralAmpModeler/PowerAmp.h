#pragma once

// Push-pull pentode power amp for the Tube Lab, solved per (oversampled) sample.
//
//   in --Master--> phase inverter --+--Rpi--Cc--+--> grid 1 --> pentode 1 --+
//                   (long-tail pair) |           Rg (bias)                   |
//                       ^            +--Rpi--Cc--+--> grid 2 --> pentode 2 --+--> output transformer --> out
//                       |                        Rg (bias)                   |
//                       +------- negative feedback (Feedback, less of it up top: Presence) ---+
//
//   B+  <-- rectifier + supply resistance + reservoir cap (Sag) -- drawn on by both plates
//   Vg2 <-- screen resistor + filter cap from B+                 -- drawn on by both screens
//
// * Pentodes: Koren's model, Ip = (2/Kg1) * E1^Ex * atan(Vp/Kvb) with
//   E1 = (Vg2/Kp) * ln(1 + e^(Kp (1/mu + Vg1/Vg2))). It factors into a function of Vg2 alone, a
//   function of x = 1/mu + Vg1/Vg2 alone, and a function of Vp alone, so the two 1-D functions are
//   tabulated (value and slope) and the Vg2 factor is computed once per sample.
// * Class AB push-pull: each plate sees a quarter of the plate-to-plate load through the
//   transformer, Vp1 = B+ - Rl*D and Vp2 = B+ + Rl*D, where D = Ip1 - Ip2 is the difference current
//   that drives the speaker. Cold-ish fixed bias, so one tube cuts off on big swings (crossover).
// * Because the tubes draw more current the harder they're driven, B+ and the screens sag under
//   load: compression and "bloom" that recovers over tens to hundreds of milliseconds.
// * Phase inverter (1.20.0): a real long-tail pair -- two 12AX7s (the preamp's Dempwolf model)
//   with 82k / 100k plate loads, a 470 ohm bias resistor and a 10k tail, as in Marshall amps. It
//   has no memory of its own, so its exact transfer curve (one per output: the unequal loads make
//   the two sides differ, as on the real amp) is solved once and tabulated (PhaseInverterTable).
// * Power-tube grids (1.20.0): each is fed from its phase-inverter plate (output resistance Rpi)
//   through a coupling cap Cc, biased through the grid leak Rg, and draws real grid current when
//   driven above 0 V. That current charges Cc, so after a loud passage the grid sits colder until
//   Cc discharges through Rg -- power-amp blocking, from the circuit itself (it used to be an
//   envelope-follower approximation).
// * Negative feedback is part of the per-sample solve (implicit), not a one-sample-delayed loop, so
//   heavy feedback can't go unstable. Presence high-passes the fed-back signal so there's less
//   feedback -- more gain and bite -- up top.
// * Newton's method each sample on D and the two grid voltages together (the grid equations are
//   eliminated in closed form, so each iteration is a scalar step), warm-started from the last.
// * Level: small signals at unity at the reference Master, with the feedback's gain loss made up.
// * Speaker load (optional): instead of a resistor, the plates drive a real speaker's electrical
//   impedance through the transformer -- voice coil resistance, voice coil inductance with eddy
//   losses, and the cone's mechanical resonance as a parallel RLC -- so the voltage the power tubes
//   (high output impedance pentodes) put across it follows the impedance curve: a big bump at the
//   cone resonance (~95 Hz) and a rising top end. The feedback is taken from that voltage, so
//   Feedback flattens it, as on a real amp. The network is linear in the difference current, so it
//   folds into the same per-sample Newton solve (trapezoidal companion models).
// * Real Feel switches (1.33.0), each off by default (off is exactly the sound before them):
//   - Screen squish: a pentode's cathode current is split between plate and screen, and when the
//     plate swings down near or below the screen voltage the plate can't collect it all -- the
//     rest lands on the screen. So on big swings the screen current jumps, the screen supply sags,
//     and the tubes' gain drops with it: the spongy compression of a cranked power amp. (Without it,
//     the screens draw a fixed 12% of the plate current.)
//   - Transformer saturation: the output transformer's primary inductance (what rolls off the deep
//     bass) is iron, and iron saturates. The flux is the integral of the voltage, so it's big low
//     notes at volume that push it there: the inductance collapses on the peaks, the low end
//     compresses and grows harmonics. Solved per sample as a saturating inductor.
//   - Depth (1.33.1): 1x is the real parts below; more exaggerates all four together, up to 10x
//     (a screen supply k times softer, a transformer saturating at 1/k the flux, a coil heating k
//     times faster per watt, the preamp's and inverter's supplies moving k times the screens').
//     Negative feedback cancels much of these, as on a real amp, so heavy-Feedback settings need
//     more Depth to hear them.
//   - Voice coil heat: the speaker's voice coil resistance rises about 0.39% per degree, and a coil
//     warms by tens of degrees over a few seconds of loud playing. The tubes and the feedback see
//     that rising resistance; what you hear follows the coil's current (the cone's force), so
//     with feedback holding the voltage it compresses, and without it the bass resonance loosens.
// * Circuit+ (1.45.0), each off by default (off is exactly the sound before them):
//   - Cathode bias: instead of a fixed negative grid supply, the pair shares a cathode resistor
//     with a big bypass cap (as in AC30s, Deluxes, 18-watters). Its voltage is the bias, and it
//     follows the tubes' average current: dig in and the pair draws more on average (class AB
//     rectifies), the cathode rises, the tubes bias themselves colder -- less gain, more crossover
//     -- and it recovers over the cap's time constant (Rk x 100 uF, ~45 ms). Set so the idle
//     current is the same as the fixed bias's.
//   - Speaker feedback: a real amp takes its negative feedback from the speaker side of the output
//     transformer, so the transformer's bass and treble roll-offs are inside the loop (here they
//     used to come after it). Both are linear, so they fold into the same implicit per-sample
//     solve: the fed-back signal is a known linear function of this sample's output. (OT Iron's
//     saturation stays outside the loop.)
//   - Resonance: a low-pass (~150 Hz) part of the fed-back signal is taken away, up to 75%, as the
//     Resonance/Depth control on a Mesa or a 5150 does: less feedback in the lows, so the power
//     amp's low end -- and the speaker's resonance bump with Spkr Load -- comes back.
// * Bench (1.58.0, TubeBench.h): the Variac scales B+ (and so the screens), the fixed-bias supply and
//   the phase inverter's headroom; tube age and the heaters scale the tubes' perveance and put a soft
//   emission ceiling on their current; the bias pot sets the idle current (biasTrim). The output
//   level stays matched at the stock bias, so a cold bias sounds colder rather than being made up.
// * Quad (1.68.0): four power tubes, two in parallel on each side, as in a 100 W head -- into half the
//   plate-to-plate load (1.7k), from a supply with half the resistance and twice the filter caps, and
//   through a bigger output transformer (30 Hz corner, twice the flux before it saturates). Each tube
//   works as in the pair (the same voltages, the same share of the load), so small signals are as loud;
//   what changes is the supply and transformer staying out of the way longer, and a failed tube only
//   halves its side (that side then pushes half as hard: lopsided, fizzy) instead of silencing it.

#include <algorithm>
#include <cmath>

#include "TubeBench.h"
#include "TubeCircuit.h"

namespace tube
{
// The phase inverter's two outputs as functions of its differential input, from an exact solve of
// the long-tail pair. Input and outputs are normalized so the small-signal gain averages 1 per side
// (outputs are in volts of swing at each plate, sign-flipped so side 1 goes up with the input).
class PhaseInverterTable
{
public:
  static constexpr double kBPlus = 330.0; // The inverter's own supply node
  static constexpr double kRa1 = 82e3, kRa2 = 100e3; // Plate loads (input side, other side)
  static constexpr double kRk = 470.0, kRtail = 10e3;
  static constexpr int kSize = 8192;
  static constexpr double kRange = 600.0; // Normalized input volts covered (beyond: flat)

  static const PhaseInverterTable& Get()
  {
    static const PhaseInverterTable table; // Built once, thread-safely, then only read.
    return table;
  }

  // Side 1 (+) and side 2 (-) swing for normalized input u, and their slopes.
  void Lookup(double u, double& s1, double& d1, double& s2, double& d2) const
  {
    const double pos = (u + kRange) * ((kSize - 1) / (2.0 * kRange));
    if (pos <= 0.0 || pos >= kSize - 1)
    {
      const int i = pos <= 0.0 ? 0 : kSize - 1;
      s1 = mS1[i];
      s2 = mS2[i];
      d1 = d2 = 0.0;
      return;
    }
    const int i = static_cast<int>(pos);
    const double f = pos - i;
    s1 = mS1[i] + f * (mS1[i + 1] - mS1[i]);
    s2 = mS2[i] + f * (mS2[i + 1] - mS2[i]);
    const double perStep = (kSize - 1) / (2.0 * kRange);
    d1 = (mS1[i + 1] - mS1[i]) * perStep;
    d2 = (mS2[i + 1] - mS2[i]) * perStep;
  }

  // Per-side small-signal gains of the real pair (plate volts per grid volt), before normalizing.
  double gain1 = 0.0, gain2 = 0.0;
  // Plate output resistances (Ra || rp) at rest: what the power-tube grid circuits see.
  double rOut1 = 0.0, rOut2 = 0.0;

private:
  PhaseInverterTable()
  {
    // Plate currents with grid 1 at gridRef + v and grid 2 at gridRef. The grids get their DC bias
    // through 1M resistors from the junction of the bias resistor and the tail, but for signals
    // they're held still (grid 2 is bypassed to ground by a cap; grid 1 is driven through one), so
    // only the resting junction voltage is their reference -- except at rest itself (gridRef < 0),
    // where both grids sit at the junction.
    auto solve = [](double v, double& ia, double& ib, double gridRef) {
      constexpr double rkt = kRk + kRtail;
      const double dVgDi = gridRef >= 0.0 ? 0.0 : kRtail; // How the grids' reference moves with current
      for (int it = 0; it < 100; it++)
      {
        const double i = ia + ib;
        const double vk = rkt * i; // Cathodes: bias resistor + tail to ground
        const double vg = gridRef >= 0.0 ? gridRef : kRtail * i; // Grid 2 (and grid 1 at v = 0)
        const TriodeCurrents ta = TriodeExact(vg + v - vk, kBPlus - kRa1 * ia - vk);
        const TriodeCurrents tb = TriodeExact(vg - vk, kBPlus - kRa2 * ib - vk);
        const double ra = ia - (ta.ik - ta.ig), rb = ib - (tb.ik - tb.ig);
        // Exact Jacobian: plate current = cathode current - grid current, through Vgk and Vpk.
        const double gmA = ta.dikDvgk - ta.digDvgk, gpA = ta.dikDvpk;
        const double gmB = tb.dikDvgk - tb.digDvgk, gpB = tb.dikDvpk;
        const double dVgk = dVgDi - rkt; // d(Vgk)/d(either current), both sides
        const double j11 = 1.0 - (gmA * dVgk + gpA * (-kRa1 - rkt)), j12 = -(gmA * dVgk + gpA * (-rkt));
        const double j21 = -(gmB * dVgk + gpB * (-rkt)), j22 = 1.0 - (gmB * dVgk + gpB * (-kRa2 - rkt));
        const double det = j11 * j22 - j12 * j21;
        if (std::fabs(det) < 1e-30)
          break;
        double da = (ra * j22 - rb * j12) / det, db = (rb * j11 - ra * j21) / det;
        const double lim = 1e-4; // Damped: at most 0.1 mA per step
        da = std::clamp(da, -lim, lim);
        db = std::clamp(db, -lim, lim);
        ia = std::max(ia - da, 0.0);
        ib = std::max(ib - db, 0.0);
        if (std::fabs(da) < 1e-12 && std::fabs(db) < 1e-12)
          break;
      }
    };
    double ia0 = 0.5e-3, ib0 = 0.5e-3;
    solve(0.0, ia0, ib0, -1.0);
    const double gridRef = kRtail * (ia0 + ib0); // The junction's resting voltage
    // Small-signal gains and output resistances at rest.
    {
      const double dv = 1e-3;
      double ap = ia0, bp = ib0, am = ia0, bm = ib0;
      solve(dv, ap, bp, gridRef);
      solve(-dv, am, bm, gridRef);
      gain1 = kRa1 * (ap - am) / (2.0 * dv);
      gain2 = kRa2 * (bm - bp) / (2.0 * dv);
      const double i0 = ia0 + ib0, vk = (kRk + kRtail) * i0;
      const TriodeCurrents ta = TriodeExact(gridRef - vk, kBPlus - kRa1 * ia0 - vk);
      const TriodeCurrents tb = TriodeExact(gridRef - vk, kBPlus - kRa2 * ib0 - vk);
      const double rp1 = 1.0 / std::max(ta.dikDvpk, 1e-12), rp2 = 1.0 / std::max(tb.dikDvpk, 1e-12);
      rOut1 = kRa1 * rp1 / (kRa1 + rp1);
      rOut2 = kRa2 * rp2 / (kRa2 + rp2);
    }
    const double norm = 0.5 * (gain1 + gain2); // Normalized input u = v * norm
    // Sweep out from rest in both directions (each point warm-starts from the last).
    const int mid = (kSize - 1) / 2;
    for (int dir = -1; dir <= 1; dir += 2)
    {
      double ia = ia0, ib = ib0;
      for (int k = mid; k >= 0 && k < kSize; k += dir)
      {
        const double u = -kRange + 2.0 * kRange * k / (kSize - 1);
        solve(u / norm, ia, ib, gridRef);
        // Each output is its plate's swing flipped (a plate falls as its current rises): side 1
        // follows u, side 2 (whose current falls as side 1's rises) opposes it.
        mS1[k] = kRa1 * (ia - ia0);
        mS2[k] = kRa2 * (ib - ib0);
      }
    }
  }

  double mS1[kSize] = {}, mS2[kSize] = {};
};

struct PentodeModel
{
  double mu, ex, kg1, kg2, kp, kvb;
  double idleAmps; // Per-tube idle current the bias is set for
  double ratedWatts; // Maximum plate dissipation (the bench's bias readout)
};
// Koren's published constants.
constexpr PentodeModel kEL34 = {11.0, 1.35, 650.0, 4200.0, 60.0, 24.0, 0.035, 25.0};
constexpr PentodeModel k6L6 = {8.7, 1.35, 1460.0, 4500.0, 48.0, 12.0, 0.040, 30.0};

struct PowerAmpParams
{
  double master = 0.5; // 0..1: how hard the phase inverter drives the power tubes
  double sag = 0.5; // 0..1: stiff (solid-state rectifier) to saggy (tube rectifier, soft supply)
  double feedback = 0.5; // 0..1: negative feedback amount
  double presence = 0.5; // 0..1: how much less feedback (more bite) at high frequencies
  bool sixL6 = false; // false = EL34s, true = 6L6s
  bool speakerLoad = false; // Drive a real speaker's impedance instead of a resistor
  // Real Feel switches (1.33.0), see the top of this file.
  bool screenSquish = false;
  bool otSaturation = false;
  bool coilHeat = false; // Needs speakerLoad
  bool sharedSupply = false; // The phase inverter's headroom follows the screen supply
  double feelDepth = 1.0; // 1 = real parts .. 10 = exaggerated (see the top of this file)
  // Circuit+ (1.45.0, see the top of this file).
  bool cathodeBias = false;
  bool speakerFeedback = false;
  double resonance = 0.0; // 0..1
  // Bench (1.58.0): the bias pot, as the idle current relative to stock (set at 120 V, new tubes).
  double biasTrim = 1.0;
  int tubesPerSide = 1; // 1.68.0: 2 = a quad (see the top of this file)

  bool operator==(const PowerAmpParams& o) const
  {
    return master == o.master && sag == o.sag && feedback == o.feedback && presence == o.presence && sixL6 == o.sixL6
           && speakerLoad == o.speakerLoad && screenSquish == o.screenSquish && otSaturation == o.otSaturation
           && coilHeat == o.coilHeat && sharedSupply == o.sharedSupply && feelDepth == o.feelDepth
           && cathodeBias == o.cathodeBias && speakerFeedback == o.speakerFeedback && resonance == o.resonance
           && biasTrim == o.biasTrim && tubesPerSide == o.tubesPerSide;
  }
};

class PushPullPowerAmp
{
public:
  static constexpr double kBPlus = 450.0; // Plate supply at rest
  static constexpr double kRl = 850.0; // Per-plate load: 3.4k plate-to-plate / 4
  // Power-tube grid circuit: grid leak (to the bias supply) and grid current when above 0 V
  // (Ig = kGridK * v^1.5, smoothed at 0). Coupling cap: 22 nF (EL34, Marshall) or 100 nF (6L6,
  // Fender), so blocking recovers in ~5 ms or ~22 ms (Cc x Rg).
  static constexpr double kRg = 220e3;
  static constexpr double kGridK = 5e-4;
  static constexpr double kScreenFraction = 0.12; // Screen current as a share of plate current
  static constexpr double kReferenceMaster = 0.5; // Master position the output level is matched at
  // Shared supply: the preamp's and phase inverter's supply relative to rest, for the screen node at
  // `screenRatio` of its rest: they follow it Depth times as far (1x: exactly), never below 25%.
  static double SharedSupplyScale(double screenRatio, double depth)
  {
    return std::max(0.25, 1.0 + depth * (screenRatio - 1.0));
  }

  void Reset(double sampleRate, const PowerAmpParams& p)
  {
    mT = 1.0 / sampleRate;
    mParams = p;
    mSqDepth = mSqDepthMid = _Depth();
    _Update();
    mVB = kBPlus;
    mVg2 = mVg2Rest;
    mD = 0.0;
    mGrid1 = mGrid2 = mGridRef * mBench.mains;
    _HoldPredictor();
    mCc1.SetResting(-mGridRef * mBench.mains); // At rest: no current; the cap holds plate swing 0 minus the bias
    mCc2.SetResting(-mGridRef * mBench.mains);
    mCathodeV = mRestCathode;
    mPresHp = mPrevY = 0.0;
    mOtHp = mOtHpPrevIn = mOtLp = 0.0;
    mOtFlux = mOtSatOut = 0.0;
    mOtSat = mOtSatMid = _OtSatTarget();
    mOtMix = mParams.otSaturation ? 1.0 : 0.0;
    mCoilRise = 0.0;
    mRe = mRe0;
    mPiScale = 1.0;
    mFbProof = mFbProofMid = 1.0;
    mSquishMix = mParams.screenSquish ? 1.0 : 0.0;
    // Settle at rest: with a mismatched pair the idle difference current isn't exactly zero, so
    // start the transformer/presence filters from that resting value rather than stepping to it.
    // A few rounds: each settle-then-solve gets the resting current closer, since the solve sees the
    // network's resting voltage only once the network has been settled at that current.
    // While settling, the fed-back signal is just the output (at rest the presence high-pass passes
    // nothing), so the solve finds the same resting point the first real sample will.
    mSettling = true;
    for (int round = 0; round < 4; round++)
    {
      _SettleSpeaker();
      _HoldPredictor(); // Each round restarts from the last one's point, not a trend between them
      Process(0.0);
    }
    mSettling = false;
    _SettleSpeaker();
    _HoldPredictor();
    double zeq, u0;
    _LoadCoefficients(zeq, u0);
    const double yIdle = (zeq * mD + u0) / (mRl * mOutScale);
    mPresHp = mOtHp = mOtLp = 0.0;
    mPrevY = mOtHpPrevIn = yIdle;
    // The fed-back signal at rest: the plate's idle imbalance, or nothing through the transformer.
    mFbHp = mFbLp = 0.0;
    mFbHpPrevIn = yIdle;
    if (mParams.speakerFeedback)
      mPrevY = 0.0;
    mResLp = mFbRest = mPrevY;
    mResonance = mResMid = mResTarget;
    mOtSatOut = 0.0;
    mOtSat = mOtSatMid = _OtSatTarget();
    mOtFlux = _OtFluxFor(yIdle / mOtCornerW); // The primary carrying the idle imbalance, no voltage across it
    mCoilRise = 0.0;
    mRe = mRe0;
    mPiScale = 1.0;
    mFbProof = mFbProofMid = 1.0;
  }

  void SetParams(const PowerAmpParams& p)
  {
    if (p == mParams)
      return;
    const bool tubesChanged =
      p.sixL6 != mParams.sixL6 || p.cathodeBias != mParams.cathodeBias || p.tubesPerSide != mParams.tubesPerSide;
    const bool loadChanged = p.speakerLoad != mParams.speakerLoad;
    const bool otSatOn = p.otSaturation && !mParams.otSaturation;
    mParams = p;
    if (!mParams.screenSquish && mSquishMix <= 0.0)
      mSqDepth = mSqDepthMid = _Depth(); // Squish isn't running: nothing to glide
    _Update();
    // The saturating transformer starts from the plain high-pass's state -- the same inductor current
    // (the high-pass's input minus its output) and output -- and is crossfaded in (see Process).
    if (otSatOn && mOtMix <= 0.0)
    {
      mOtSat = mOtSatMid = _OtSatTarget();
      mOtFlux = _OtFluxFor((mOtHpPrevIn - mOtHp) / mOtCornerW);
      mOtSatOut = mOtHp;
    }
    if (tubesChanged)
    {
      mD = 0.0;
      mGrid1 = mGrid2 = mGridRef * mBench.mains;
      _HoldPredictor();
      mCc1.SetResting(-mGridRef * mBench.mains);
      mCc2.SetResting(-mGridRef * mBench.mains);
      mCathodeV = mRestCathode;
    }
    if (loadChanged)
      _SettleSpeaker();
  }

  // One sample. Input and output are in the Tube Lab's normalized units (small signals at unity at
  // the reference Master with no feedback; Master works like a real master volume). Built twice, with
  // and without the bench's emission ceiling (1.58.0), so stock tubes' Newton loop doesn't carry it.
  double Process(double in)
  {
    return (mBench.invCeiling > 0.0 || mSideN[0] != 1.0 || mSideN[1] != 1.0) ? _Process<true>(in) : _Process<false>(in);
  }

private:
  template <bool kCeiling>
  double _Process(double in)
  {
    const PentodeModel& m = _Model();
    // Everything the tubes see is relative to their cathode (0 V with fixed bias, so exactly as before).
    const double vk = mCathodeV;
    const double vg2 = std::max(mVg2 - vk, 1.0);
    const double a = _ScreenFactor(m, vg2) * mBench.perveance; // Vg2 factor, once per sample
    const double invVg2 = 1.0 / vg2;
    const double c0 = mSettling ? 0.0 : _PresenceConstant();
    const double fbGain = mSettling ? 1.0 : mFbGain;
    // The fed-back signal is z - presence HP(z) - resonance LP(z), where z is the plate swing y (or,
    // with Speaker feedback, y through the transformer's high- and low-pass). Every part is linear
    // and this sample's y enters each with a known weight, so it's fbA * y + fbB, solved implicitly.
    double zG = 1.0, zK = 0.0;
    if (mParams.speakerFeedback)
    {
      if (mSettling)
        zG = 0.0; // The transformer passes no DC: at rest nothing is fed back
      else
      {
        zG = mOtLpCoef * mOtHpCoef;
        zK = mOtLpCoef * mOtHpCoef * (mFbHp - mFbHpPrevIn) + (1.0 - mOtLpCoef) * mFbLp;
      }
    }
    // Resonance acts on the signal only, z's swing from its resting value (the plate's idle imbalance
    // with plate feedback): taking part of the DC feedback away would move the operating point, a
    // thump whenever the knob moved. And it glides (two one-poles, ~10 ms): a jump clicked.
    if (mResonance != mResTarget || mResMid != mResTarget)
    {
      mResMid += mResGlide * (mResTarget - mResMid);
      mResonance += mResGlide * (mResMid - mResonance);
      if (std::fabs(mResonance - mResTarget) < 1e-9 && std::fabs(mResMid - mResTarget) < 1e-9)
        mResonance = mResMid = mResTarget;
    }
    const double res = mSettling ? 0.0 : mResonance;
    const double fbA = (fbGain - res * mResCoef) * zG;
    const double fbB = (fbGain - res * mResCoef) * zK + c0 - res * ((1.0 - mResCoef) * mResLp - mFbRest);
    const double driveGain = _MasterGain();
    // Plate voltage swing u = zeq * D + u0 (a resistor kRl, or the speaker network's companion
    // model this sample); the output is that voltage, normalized.
    double zeq, u0;
    _LoadCoefficients(zeq, u0);
    const double yPerVolt = 1.0 / (mRl * mOutScale);

    // Newton on D (the difference current) and the two power-tube grid voltages G1, G2:
    //   F1 = D - (Ip1(G1, Vp1) - Ip2(G2, Vp2)) = 0, with Vp1 = B+ - swing, Vp2 = B+ + swing
    //   Fk = ik - (Gk - bias) / Rg - Ig(Gk) = 0 at each grid, where ik is the current arriving
    //        through Rpi and Cc from the phase inverter's output Sk(u)
    // and u, the inverter's input, includes this very sample's negative feedback. F2 and F3 each
    // involve only D and their own grid, so they're eliminated in closed form: one scalar step on D.
    //
    // Safeguarded Newton (1.33.0). With the grids solved exactly for each D (_SolveGrid), what's left
    // is one equation F(D) = D - (Ip1 - Ip2) that rises monotonically with D: more difference current
    // pulls one plate down and the other up, and the feedback turns the drive down. So once F has
    // been seen on both sides of 0 the root is bracketed, and any step that would leave the bracket
    // bisects it instead. Before, it was a plain Newton with a fixed +-50 mA step limit, which on
    // hard-driven settings (one tube cut off, the other's grid conducting) could bounce between two
    // points forever: Plexi Crunch's output was off the true solution by up to -23 dB re its peak.
    const PhaseInverterTable& pi = PhaseInverterTable::Get();
    const double h1 = 1.0 / (1.0 + mCc1.g * mRpi1), h2 = 1.0 / (1.0 + mCc2.g * mRpi2);
    // Newton's starting point (1.45.0): the last two samples' solutions extrapolated in a straight
    // line (see TriodeStageCircuit::Process).
    double d = 2.0 * mD - mDPrev, g1 = 2.0 * mGrid1 - mGrid1Prev, g2 = 2.0 * mGrid2 - mGrid2Prev;
    double lo = -1e30, hi = 1e30; // Bracket on D
    // The last evaluation and its slopes along the solution (per unit of D), kept for after the loop.
    double s1 = 0.0, s2 = 0.0, ip1 = 0.0, ip2 = 0.0;
    double dG1 = 0.0, dG2 = 0.0, dS1 = 0.0, dS2 = 0.0, dIp1 = 0.0, dIp2 = 0.0, dd = 0.0;
    bool converged = false;
    for (int it = 0; it < kNewtonIterations; it++)
    {
      const double swing = zeq * d + u0;
      const double y = swing * yPerVolt;
      const double u = driveGain * (in - mBeta * (fbA * y + fbB));
      const double dUdD = -driveGain * mBeta * fbA * zeq * yPerVolt;
      double ds1, ds2;
      _PiLookup(pi, u, s1, ds1, s2, ds2);
      const double a22 = _SolveGrid(g1, s1, mCc1, h1);
      const double a33 = _SolveGrid(g2, s2, mCc2, h2);
      const double a21 = h1 * mCc1.g * ds1 * dUdD;
      const double a31 = h2 * mCc2.g * ds2 * dUdD;

      double dIp1Dvg, dIp1Dvp, dIp2Dvg, dIp2Dvp;
      _Tube<kCeiling>(m, a, invVg2, g1 - vk, mVB - swing - vk, ip1, dIp1Dvg, dIp1Dvp);
      _Tube<kCeiling>(m, a * kTube2Match, invVg2, g2 - vk, mVB + swing - vk, ip2, dIp2Dvg, dIp2Dvp);
      if (kCeiling)
        _KillDead(ip1, dIp1Dvg, dIp1Dvp, ip2, dIp2Dvg, dIp2Dvp);
      const double f1 = d - (ip1 - ip2);
      const double a11 = 1.0 + zeq * (dIp1Dvp + dIp2Dvp), a12 = -dIp1Dvg, a13 = dIp2Dvg;
      if (f1 > 0.0)
        hi = std::min(hi, d);
      else
        lo = std::max(lo, d);

      // dF/dD with the grids following D (dG1/dD = -a21 / a22, dG2/dD = -a31 / a33).
      dG1 = -a21 / a22;
      dG2 = -a31 / a33;
      const double den = a11 + a12 * dG1 + a13 * dG2;
      double next = d + std::clamp(-f1 / std::max(den, 1e-12), -0.05, 0.05);
      if (f1 == 0.0)
        next = d;
      // The step always heads away from d's own end of the bracket (against F's sign), so if it
      // reaches the other end, both are known: bisect.
      else if ((next <= lo || next >= hi) && lo > -1e29 && hi < 1e29)
        next = 0.5 * (lo + hi);
      dd = next - d;
      if (std::fabs(dd) < kNewtonTolerance)
      {
        // Converged (1.45.0): d is within this last small step of the answer. Instead of solving
        // the phase inverter, both grids and both tubes once more at d + dd (as before: a fifth of
        // the power amp's time), move this evaluation along its own slopes; the error is ~dd^2.
        dS1 = ds1 * dUdD;
        dS2 = ds2 * dUdD;
        dIp1 = dIp1Dvg * dG1 - dIp1Dvp * zeq;
        dIp2 = dIp2Dvg * dG2 + dIp2Dvp * zeq;
        converged = true;
        break;
      }
      d = next;
    }
    if (converged)
    {
      d += dd;
      g1 += dG1 * dd;
      g2 += dG2 * dd;
      s1 += dS1 * dd;
      s2 += dS2 * dd;
      ip1 += dIp1 * dd;
      ip2 += dIp2 * dd;
    }
    const double swing = zeq * d + u0;
    mSwing = swing;
    const double y = swing * yPerVolt;
    if (!converged)
    {
      // Out of iterations (rare): the operating point at the final D, solved in full.
      const double u = driveGain * (in - mBeta * (fbA * y + fbB));
      double ds1, ds2;
      _PiLookup(pi, u, s1, ds1, s2, ds2);
      _SolveGrid(g1, s1, mCc1, h1);
      _SolveGrid(g2, s2, mCc2, h2);
      double t0, t1;
      _Tube<kCeiling>(m, a, invVg2, g1 - vk, mVB - swing - vk, ip1, t0, t1);
      _Tube<kCeiling>(m, a * kTube2Match, invVg2, g2 - vk, mVB + swing - vk, ip2, t0, t1);
      if (kCeiling)
      {
        double u0 = 0.0, u1 = 0.0, u2 = 0.0, u3 = 0.0;
        _KillDead(ip1, u0, u1, ip2, u2, u3);
      }
    }

    // The converged operating point: currents for the supplies, and the coupling caps' new charge.
    mDPrev = mD;
    mGrid1Prev = mGrid1;
    mGrid2Prev = mGrid2;
    mD = d;
    mGrid1 = g1;
    mGrid2 = g2;
    const double i1 = h1 * (mCc1.g * (s1 - g1) - mCc1.ieq);
    const double i2 = h2 * (mCc2.g * (s2 - g2) - mCc2.ieq);
    mCc1.Update(s1 - g1 - mRpi1 * i1);
    mCc2.Update(s2 - g2 - mRpi2 * i2);
    mIp1 = ip1;
    mIp2 = ip2;
    _UpdateSpeaker(d);
    // What goes on to the transformer. With a heated voice coil the tubes (and the feedback) see its
    // higher resistance, but the sound follows the coil's current, so the extra drop is taken back
    // out: the output is the voltage a cold coil would have with this current.
    double yOut = y;
    if (mParams.speakerLoad && (mCoilRise > 0.0 || mParams.coilHeat))
      yOut = _HeatVoiceCoil(d, y, yPerVolt);

    // The fed-back signal's filter states: the transformer's (Speaker feedback), presence, resonance.
    if (mParams.speakerFeedback)
    {
      mFbHp = mOtHpCoef * (mFbHp + y - mFbHpPrevIn);
      mFbHpPrevIn = y;
      mFbLp += mOtLpCoef * (mFbHp - mFbLp);
    }
    const double z = mParams.speakerFeedback ? mFbLp : y;
    mPresHp = mPresCoef * (mPresHp + z - mPrevY);
    mPrevY = z;
    mResLp += mResCoef * (z - mResLp);

    // Supplies: plates draw from B+ through the rectifier/supply resistance into the reservoir cap;
    // screens draw from B+ through the screen resistor into their own cap.
    const double iPlates = ip1 + ip2;
    double iCathode = 0.0; // Plates + screens: what the cathode resistor carries
    if (!mParams.screenSquish && mSquishMix <= 0.0)
    {
      const double iScreens = kScreenFraction * iPlates;
      mVB += mSupplyCoef * (mVSupply * mBench.mains - mRSupply * (iPlates + iScreens) - mVB);
      mVg2 += mScreenCoef * (mVB - mRScreen * iScreens - mVg2);
      iCathode = iPlates + iScreens;
    }
    else
    {
      // Squish: its screen current and softer supply, crossfaded in and out over kOtFadeS (an S-curve)
      // -- switching the whole screen circuit at once kinked the waveform, a click at high Depth.
      mSquishMix = std::clamp(mSquishMix + (mParams.screenSquish ? mOtFadeStep : -mOtFadeStep), 0.0, 1.0);
      // Depth glides (two one-poles): stepping the screen supply and its lift clicked when Depth was
      // turned on a clean, held note.
      const double depth = _Depth();
      if (mSqDepth != depth || mSqDepthMid != depth)
      {
        mSqDepthMid += mSqGlideCoef * (depth - mSqDepthMid);
        mSqDepth += mSqGlideCoef * (mSqDepthMid - mSqDepth);
        if (std::fabs(mSqDepth - depth) < 1e-9 && std::fabs(mSqDepthMid - depth) < 1e-9)
          mSqDepth = mSqDepthMid = depth;
        _SetSquishDepth(m);
      }
      const double w = mSquishMix * mSquishMix * mSquishMix * (mSquishMix * (6.0 * mSquishMix - 15.0) + 10.0);
      const double iStock = kScreenFraction * iPlates;
      // (Per tube, from its side's current shared by the side's live tubes, times those tubes.)
      const double iSquish =
        mSideN[0] == 1.0 && mSideN[1] == 1.0
          ? _ScreenCurrent(m, a, vg2, g1 - vk, mVB - swing - vk, ip1)
              + _ScreenCurrent(m, a * kTube2Match, vg2, g2 - vk, mVB + swing - vk, ip2)
          : (mSideN[0] > 0.0 ? mSideN[0] * _ScreenCurrent(m, a, vg2, g1 - vk, mVB - swing - vk, ip1 / mSideN[0]) : 0.0)
              + (mSideN[1] > 0.0
                   ? mSideN[1] * _ScreenCurrent(m, a * kTube2Match, vg2, g2 - vk, mVB + swing - vk, ip2 / mSideN[1])
                   : 0.0);
      const double iScreens = iStock + w * (iSquish - iStock);
      iCathode = iPlates + iScreens;
      mVB += mSupplyCoef * (mVSupply * mBench.mains - mRSupply * (iPlates + iScreens) - mVB);
      const double targetStock = mVB - mRScreen * iStock;
      const double targetSquish = mVB + mScreenLift * mBench.mains - mRSquish * iSquish;
      const double coef = mScreenCoef + w * (mSquishCoef - mScreenCoef);
      mVg2 += coef * (targetStock + w * (targetSquish - targetStock) - mVg2);
    }
    // Cathode bias: the bypass cap charges toward Rk x the cathode current (slow, so explicitly).
    if (mParams.cathodeBias)
      mCathodeV += mCathodeCoef * (mRkCath * iCathode - mCathodeV);
    // Shared supply: the phase inverter's node hangs off the screen node through its own decoupling
    // filter. Settles back to exactly 1 once switched off.
    if (mParams.sharedSupply || mPiScale != 1.0)
    {
      // (Relative to where the Variac puts the screens: that's not sag.)
      const double target =
        mParams.sharedSupply ? SharedSupplyScale(mVg2 / (mVg2Rest * mBench.mains), _Strength()) : 1.0;
      mPiScale += mPiCoef * (target - mPiScale);
      if (!mParams.sharedSupply && std::fabs(mPiScale - 1.0) < 1e-7)
        mPiScale = 1.0;
    }

    // Output transformer: primary inductance (bass roll-off) and leakage (treble roll-off).
    // The plain high-pass always runs. OT Iron's saturating primary runs alongside it while it's on
    // or fading, and is crossfaded over kOtFadeS when the switch flips: the two models' states
    // diverge while it's on (a saturated core's current has no linear equivalent), so handing over
    // instantly kinked the waveform -- an audible click at high Depth.
    double hp = mOtHpCoef * (mOtHp + yOut - mOtHpPrevIn);
    mOtHp = hp;
    if (mParams.otSaturation || mOtMix > 0.0)
    {
      mOtMix = std::clamp(mOtMix + (mParams.otSaturation ? mOtFadeStep : -mOtFadeStep), 0.0, 1.0);
      // Depth glides rather than stepping the core: two one-poles in series, so the glide also starts
      // smoothly (one alone changes slope instantly, which a 10x-to-1x jump made click).
      mOtSatMid += mOtSatCoef * (_OtSatTarget() - mOtSatMid);
      const double newSat = mOtSat + mOtSatCoef * (mOtSatMid - mOtSat);
      if (newSat != mOtSat)
      {
        // The core's current carries over as its saturation point moves (only the flux re-maps), so
        // the output doesn't jump; holding the flux instead jumped by the current's change.
        const double r = mOtFlux / mOtSat;
        const double current = mOtFlux * (1.0 + r * r * r * r);
        mOtSat = newSat;
        mOtFlux = _OtFluxFor(current, mOtFlux);
      }
      const double sat = _SaturatingPrimary(yOut);
      // A smooth S-curve (smootherstep), not a straight line: a linear fade between two differing
      // waveforms kinks where it starts and stops, which clicked faintly at high Depth.
      const double w = mOtMix * mOtMix * mOtMix * (mOtMix * (6.0 * mOtMix - 15.0) + 10.0);
      hp += w * (sat - hp);
    }
    mOtHpPrevIn = yOut;
    mOtLp += mOtLpCoef * (hp - mOtLp);
    // Make up the small-signal gain the feedback takes away (closed loop = 1 / (1 + loop gain) at
    // the reference Master), so Feedback changes the feel and tone, not the volume.
    const double out = mOtLp * (1.0 + mBeta);
    const bool proof = (mParams.screenSquish && (mBeta > 0.0 || mSqDepth > 1.0))
                       || (mParams.sharedSupply && mBeta > 0.0);
    if (!proof && mFbProof == 1.0)
      return out;
    // Glides (two one-poles, ~5 ms) so a switch flip or a Depth turn can't step the level.
    mFbProofMid += mFbProofCoef * ((proof ? _FeedbackProofGain(m) : 1.0) - mFbProofMid);
    mFbProof += mFbProofCoef * (mFbProofMid - mFbProof);
    if (!proof && std::fabs(mFbProof - 1.0) < 1e-9 && std::fabs(mFbProofMid - 1.0) < 1e-9)
      mFbProof = mFbProofMid = 1.0;
    return out * mFbProof;
  }

public:
  double RailVoltage() const { return mVB; }
  double ScreenVoltage() const { return mVg2; }
  // Debug probes (TubeProbe.h).
  double Grid1() const { return mGrid1; }
  double Grid2() const { return mGrid2; }
  double Plate1() const { return mVB - mSwing; }
  double Plate2() const { return mVB + mSwing; }
  double Current1() const { return mIp1; }
  double Current2() const { return mIp2; }
  double Bias() const { return mBias; }
  double CathodeVoltage() const { return mCathodeV; } // Cathode bias: the bias it has set itself (0 fixed)
  double IdleAmps() const { return _Model().idleAmps; }
  double RestingScreenVoltage() const { return mVg2Rest; }
  // Bench (1.58.0, TubeBench.h): the Variac, perveance and emission ceiling, a few times a
  // millisecond while they glide. Cheap: nothing is re-solved.
  void SetBench(const bench::State& s) { mBench = s; }
  // 1.62.0: a power tube that's failed (red-plated to death) passes no current: the amp runs on the
  // other one alone -- thin and fizzy, every other half-cycle crossing over -- or, with both dead, not
  // at all.
  // 1.68.0: four sockets; tube k is on side k % 2 (V9, V10, then the quad's V11, V12), and a quad's
  // failed tube leaves its side the other one.
  void SetTubeDead(int tube, bool dead)
  {
    mDead[std::clamp(tube, 0, 3)] = dead;
    _UpdateSides();
  }
  int TubesPerSide() const { return mParams.tubesPerSide; }
  double BenchMains() const { return mBench.mains; }
  // Each tube's plate dissipation this sample (W), its rating, and the grids' bias supply now.
  // (Each tube's own: a side's current is shared by its live tubes.)
  double Dissipation1() const { return mSideN[0] > 0.0 ? (Plate1() - mCathodeV) * mIp1 / mSideN[0] : 0.0; }
  double Dissipation2() const { return mSideN[1] > 0.0 ? (Plate2() - mCathodeV) * mIp2 / mSideN[1] : 0.0; }
  double RatedWatts() const { return _Model().ratedWatts; }
  double BiasSupply() const { return mGridRef * mBench.mains; }
  double RestingCathodeVoltage() const { return mRestCathode; }
  double FeedbackDB() const { return 20.0 * std::log10(1.0 + mBeta); }
  // Real Feel (1.33.0): the voice coil's temperature rise (K) and the transformer's flux relative to
  // where its inductance has halved.
  double CoilRiseK() const { return mCoilRise; }
  double Strength() const { return _Strength(); } // Real Feel Depth's strength multiplier
  double OtFluxRatio() const { return std::fabs(mOtFlux) / _OtSatTarget(); }

private:
  static constexpr int kNewtonIterations = 16; // Most samples take 1-3 (it exits once converged)
  // Newton stops once its step in D is below this (amps; D swings by tens of mA). With the step's
  // own slopes carried over (see Process) the error left is ~1e-12 A: 160 dB under the signal,
  // measured against a 200-iteration solve (1.45.0; it was 1e-8 plus a full re-solve after).
  static constexpr double kNewtonTolerance = 1e-6;
  static constexpr double kGridTolerance = 1e-5; // Volts, the grids' own Newton (was 1e-7)
  // Circuit+ (1.45.0): the cathode bypass cap, and Resonance's corner and the most it takes away.
  // Resonance is limited by stability: with Speaker feedback the loop also holds the transformer's
  // 40 Hz roll-off and the power grids' coupling caps (33 Hz with the EL34s' 22 nF), and taking a
  // low-pass part of the feedback away adds its own phase lead below its corner. At 100 Hz / 90% the
  // three passed 180 degrees with loop gain to spare: a 32 Hz motorboat at Feedback 10, Master 10
  // (EL34s, speaker load) -- as a real amp would. 150 Hz / 75% was stable in all 32 extreme
  // combinations tested (tubes, load, Speaker feedback, Presence, Sag) and still gives +3.5 dB at 70 Hz.
  static constexpr double kCathodeCapFarads = 100e-6;
  static constexpr double kResonanceHz = 150.0;
  static constexpr double kResonanceMax = 0.75;
  static constexpr int kGridIterations = 8;
  // Screen squish: the screen's own share of the cathode current (on top of what the plate can't
  // collect), set so the idle screen current stays near the fixed 12% used without it.
  static constexpr double kScreenBase = 0.08;
  static constexpr double kSquishDivert = 1.0; // Share of the uncollected current the screen takes
  // Squish also gives the screens a softer supply: the dropping resistor and filter cap of a
  // vintage-style screen node (the switch is the whole squish mechanism, not just the current).
  static constexpr double kSquishScreenOhms = 4700.0;
  static constexpr double kSquishScreenFarads = 22e-6;
  // Squish's gain pull beyond the real part: per Depth step above 1x, and its soft ceiling (0.8 Np =
  // 7 dB) so loud settings, whose screens already sag a lot, aren't crushed.
  static constexpr double kSquishEmphasis = 2.0;
  static constexpr double kSquishCapNepers = 0.8;
  // Shared supply: the phase inverter node's decoupling filter.
  static constexpr double kPiDecoupleTauS = 0.05;
  // Output transformer primary: the corner its inductance sets (as the plain high-pass), and the
  // flux, in volt-seconds of plate swing, where saturation has halved the inductance: a full 400 V
  // swing at ~106 Hz (a modestly sized guitar-amp transformer; hi-fi ones go far lower). Below
  // about half of it, the transformer is linear.
  static constexpr double kOtCornerHz = 40.0;
  static constexpr double kOtSatVoltSeconds = 0.6;
  static constexpr double kQuadOtCornerHz = 30.0, kQuadOtFlux = 2.0; // A quad's bigger transformer
  static constexpr double kOtFadeS = 0.02; // OT Iron's crossfade, and its saturation point's glide
  static constexpr double kDepthPower = 2.0; // Depth -> strength exponent (see _Strength)
  // Voice coil heating: copper's resistance rise per kelvin, the coil's thermal resistance to the
  // magnet (K per watt dissipated) and its time constant, and a ceiling (a coil past ~250 K over is
  // burning).
  static constexpr double kCoilAlpha = 0.0039;
  static constexpr double kCoilKPerWatt = 6.0;
  static constexpr double kCoilTauS = 3.0;
  static constexpr double kCoilMaxRiseK = 250.0;
  static constexpr double kScreenRest = 445.0;
  static constexpr double kBlockDepth = 0.6;
  static constexpr int kTableSize = 8192;

  const PentodeModel& _Model() const { return mParams.sixL6 ? k6L6 : kEL34; }

  // Newton's predictor restarts from the current point (no trend): after a reset or a tube swap.
  void _HoldPredictor()
  {
    mDPrev = mD;
    mGrid1Prev = mGrid1;
    mGrid2Prev = mGrid2;
  }

  // The plate current's screen factor (2/Kg1)(Vg2/Kp)^Ex for this sample's screen voltage. 1.45.0:
  // from a second-order expansion around the last exact value, which is re-anchored whenever Vg2
  // has drifted 1% from it. The screen supply moves slowly, so the pow() (6% of the Tube Lab's time
  // when it ran every sample) now runs rarely; the expansion's error is under 1e-7.
  double _ScreenFactor(const PentodeModel& m, double vg2)
  {
    const double r = vg2 / mScreenAnchorV - 1.0;
    if (!(std::fabs(r) < 0.01)) // (Also catches the anchor being invalidated: see _Update)
    {
      mScreenAnchorV = vg2;
      mScreenAnchorA = (2.0 / m.kg1) * std::pow(vg2 / m.kp, m.ex);
      return mScreenAnchorA;
    }
    return mScreenAnchorA * (1.0 + r * (m.ex + r * (0.5 * m.ex * (m.ex - 1.0))));
  }
  double _Depth() const { return std::clamp(mParams.feelDepth, 1.0, 10.0); }
  // How hard Depth pushes the parts: 1 at 1x (the real parts), rising faster than Depth so the top of
  // its range is audible at modest playing levels.
  double _Strength() const { return mStrength; } // Cached by _Update (pow per sample cost real CPU)

  // Feedback-proof (1.33.2): negative feedback restores most of the gain a sagging supply takes from
  // the power tubes, which is why Squish and Shared B+ were near-silent on high-Feedback settings.
  // This gives back the share it cancelled: the open-loop gain change -- the tubes' gain follows
  // (Vg2 / rest)^Ex, and the phase inverter's swing its supply -- raised to the feedback's share of
  // the loop, beta / (1 + beta). With no feedback it's exactly 1 (nothing was cancelled).
  // Squish (1.33.3) also pulls kSquishEmphasis x (Depth - 1) times harder on top, soft-capped at
  // kSquishCapNepers: at moderate levels the screens only sag ~10%, under 1 dB of gain, so the
  // pick's squash and bloom were hard to hear. Exactly the real part at 1x.
  double _FeedbackProofGain(const PentodeModel& m) const
  {
    double logOpen = 0.0, logSquish = 0.0;
    if (mParams.screenSquish)
      logSquish = m.ex * std::log(std::max(mVg2, 1.0) / mVg2Rest);
    if (mParams.sharedSupply)
      logOpen += std::log(mPiScale);
    const double share = mBeta / (1.0 + mBeta);
    const double squish = logSquish * (share + kSquishEmphasis * (mSqDepth - 1.0));
    return std::exp(logOpen * share + kSquishCapNepers * std::tanh(squish / kSquishCapNepers));
  }

  // Speaker equivalent circuit (an 8-ohm 12" guitar speaker), in ohms/henries/farads at the speaker,
  // scaled to each plate by the transformer: (Raa/4) / 8 ohms.
  static constexpr double kSpkRe = 6.4; // Voice coil DC resistance
  static constexpr double kSpkLe = 0.6e-3; // Voice coil inductance...
  static constexpr double kSpkR2 = 25.0; // ...with eddy-current losses in parallel (semi-inductance)
  static constexpr double kSpkFs = 95.0; // Cone resonance
  static constexpr double kSpkRm = 39.0; // Resonance peak above Re (~45 ohms total at fs)
  static constexpr double kSpkQms = 3.5;

  // u = zeq * D + u0 for this sample: the plate swing for a difference current D.
  void _LoadCoefficients(double& zeq, double& u0) const
  {
    if (!mParams.speakerLoad)
    {
      zeq = mRl;
      u0 = 0.0;
      return;
    }
    if (mSettling)
    {
      // At rest (DC) the inductors are shorts and the cap is open: just the voice coil resistance.
      zeq = mRe;
      u0 = 0.0;
      return;
    }
    zeq = mRe + 1.0 / mG2 + 1.0 / mG3;
    u0 = -mHL1 / mG2 + (mHC - mHL2) / mG3;
  }

  // Advance the speaker network's companion-model histories after the sample's D is known.
  void _UpdateSpeaker(double d)
  {
    if (!mParams.speakerLoad)
      return;
    const double v2 = (d - mHL1) / mG2; // Voice coil inductance || eddy loss
    const double v3 = (d - mHL2 + mHC) / mG3; // Mechanical resonance (L || C || R)
    const double iL1 = mGL1 * v2 + mHL1;
    mHL1 = iL1 + mGL1 * v2;
    const double iL2 = mGL2 * v3 + mHL2;
    mHL2 = iL2 + mGL2 * v3;
    const double iC = mGC * v3 - mHC;
    mHC = mGC * v3 + iC;
  }

  // Put the speaker network at rest for the current difference current (inductors carrying it,
  // no voltage across the reactive blocks).
  void _SettleSpeaker()
  {
    mHL1 = mHL2 = mD;
    mHC = 0.0;
  }

  // Master's 0..1 to phase-inverter input volts per normalized unit (quadratic, like a pot).
  double _MasterGain() const { return 2.0 + 78.0 * mParams.master * mParams.master; }

  // The constant part of the fed-back signal from the presence high-pass's state (see Process).
  double _PresenceConstant() const { return -mParams.presence * mPresCoef * (mPresHp - mPrevY); }

  // Plate current and slopes for one pentode (screen at vg2, given as invVg2 = 1 / vg2: 1.45.0 took
  // the divisions out of the Newton loop; factor a = (2/Kg1)(vg2/Kp)^Ex). The grid voltage is the
  // real one: its grid circuit (see Process) is what limits it above 0 V.
  template <bool kCeiling = false> // (The bench's emission ceiling: only in Process; the resting point is designed without it)
  void _Tube(const PentodeModel& m, double a, double invVg2, double vg1, double vp, double& ip, double& dIpDvg,
             double& dIpDvp) const
  {
    const double x = mInvMu + vg1 * invVg2;
    double f, df;
    _Read(mParams.sixL6 ? mF6L6 : mFEL34, mParams.sixL6 ? mDF6L6 : mDFEL34, kXMin, kXMax, x, f, df);
    const double vpc = std::max(vp, 0.0);
    double t, dt;
    _Read(mParams.sixL6 ? mT6L6 : mTEL34, mParams.sixL6 ? mDT6L6 : mDTEL34, 0.0, kVpMax, vpc, t, dt);
    ip = a * f * t;
    dIpDvg = a * df * t * invVg2;
    dIpDvp = vp > 0.0 ? a * f * dt : 0.0;
    if (kCeiling) // Bench: the cathode's emission ceiling (TubeBench.h)
    {
      double slope;
      ip = bench::Ceiling(ip, mBench.invCeiling, slope);
      dIpDvg *= slope;
      dIpDvp *= slope;
    }
  }

  // Each side's current and slopes: one tube's times the side's live tubes; a dead side passes nothing
  // (see SetTubeDead).
  void _KillDead(double& ip1, double& g1, double& p1, double& ip2, double& g2, double& p2) const
  {
    if (mSideN[0] == 0.0)
      ip1 = g1 = p1 = 0.0;
    else if (mSideN[0] != 1.0)
    {
      ip1 *= mSideN[0];
      g1 *= mSideN[0];
      p1 *= mSideN[0];
    }
    if (mSideN[1] == 0.0)
      ip2 = g2 = p2 = 0.0;
    else if (mSideN[1] != 1.0)
    {
      ip2 *= mSideN[1];
      g2 *= mSideN[1];
      p2 *= mSideN[1];
    }
  }
  // Live tubes per side from the dead flags (a pair: tubes 0, 1; a quad adds 2, 3).
  void _UpdateSides()
  {
    const int n = std::clamp(mParams.tubesPerSide, 1, 2);
    for (int side = 0; side < 2; side++)
    {
      int alive = 0;
      for (int j = 0; j < n; j++)
        alive += mDead[side + 2 * j] ? 0 : 1;
      mSideN[side] = alive;
    }
  }

  // Solves one power-tube grid's node equation for the phase inverter's output s, with D held:
  //   h (Cc.g (s - g) - Cc.ieq) - (g - bias) / Rg - Ig(g) = 0   (current in through Rpi and Cc = out)
  // The left side falls strictly with g and is concave (grid current is convex), so Newton converges
  // from any start, warm-started from g. Returns its slope at the solution.
  template <class CapT> // (Cap is declared further down)
  double _SolveGrid(double& g, double s, const CapT& cc, double h) const
  {
    double slope = -h * cc.g - mGridN / kRg; // (A quad: two grids per side, their leaks in parallel)
    // Usually the grid sits well below 0 V, where its current is negligible (< 1e-9 A at -2 V) and
    // the equation is linear: solve it directly, no Newton or transcendental calls.
    const double gridRef = mGridRef * mBench.mains; // The bias supply follows the Variac
    const double linear = (h * (cc.g * s - cc.ieq) + gridRef * (mGridN / kRg)) / -slope;
    if (linear - mCathodeV < -2.0)
    {
      g = linear;
      return slope;
    }
    for (int k = 0; k < kGridIterations; k++)
    {
      double ig, dig;
      _GridCurrent(g - mCathodeV, ig, dig);
      ig *= mGridN;
      dig *= mGridN;
      const double f = h * (cc.g * (s - g) - cc.ieq) - (g - gridRef) * mGridN / kRg - ig;
      slope = -h * cc.g - mGridN / kRg - dig;
      const double dg = -f / slope;
      g += dg;
      if (std::fabs(dg) < kGridTolerance)
        break;
    }
    return slope;
  }

  // The phase inverter's outputs at the current supply. A tube stage's swing scales with its supply,
  // while its small-signal gain barely moves: S(u) becomes k S(u / k), so it clips at k times the
  // level with the same slope (and the same slope's derivative).
  void _PiLookup(const PhaseInverterTable& pi, double u, double& s1, double& d1, double& s2, double& d2) const
  {
    const double k = mPiScale * mBench.mains; // Its own supply, and the Variac's
    if (k == 1.0)
    {
      pi.Lookup(u, s1, d1, s2, d2);
      return;
    }
    pi.Lookup(u / k, s1, d1, s2, d2);
    s1 *= k;
    s2 *= k;
  }

  // Screen squish: one tube's screen current. The cathode stream is about a F(x) pi/2 whatever the
  // plate voltage (it's set by the grid and screen); the plate collects the atan(Vp / Kvb) part of
  // it (Koren's knee), and what it doesn't collect goes to the screen.
  double _ScreenCurrent(const PentodeModel& m, double a, double vg2, double vg1, double vp, double ip) const
  {
    const double x = 1.0 / m.mu + vg1 / vg2;
    double f, df;
    _Read(mParams.sixL6 ? mF6L6 : mFEL34, mParams.sixL6 ? mDF6L6 : mDFEL34, kXMin, kXMax, x, f, df);
    // Depth also widens the knee (Kvb x sqrt(strength)) and takes more of what the plate misses (x the
    // same), so the screen starts taking current on the swings you actually play, not only when the
    // plates bottom out.
    const double knee = m.kvb * mSqrtStrength;
    return kScreenBase * ip + kSquishDivert * mSqrtStrength * a * f * (0.5 * 3.14159265358979 - std::atan(std::max(vp, 0.0) / knee));
  }

  // Transformer saturation: the primary inductance as a saturating inductor. Its current is
  // h(flux) = flux (1 + (flux / sat)^4) (so at flux = sat it draws double: half the inductance), and
  // it shunts the signal through the source like the plain high-pass does (same 40 Hz corner while
  // linear): out = in - w h(flux), with the flux the integral of out (trapezoidal). One equation in
  // the flux, rising monotonically, so Newton from the last sample's flux converges in a couple of
  // steps. Works in the output's normalized units; sat is converted from plate volt-seconds.
  double _SaturatingPrimary(double in)
  {
    const double sat = mOtSat;
    const double k = 0.5 * mT * mOtCornerW;
    const double rhs = mOtFlux + 0.5 * mT * (in + mOtSatOut);
    double l = mOtFlux;
    for (int i = 0; i < 6; i++)
    {
      const double r = l / sat, r4 = r * r * r * r;
      const double step = (l + k * l * (1.0 + r4) - rhs) / (1.0 + k * (1.0 + 5.0 * r4));
      l -= step;
      if (std::fabs(step) <= 1e-9 * (std::fabs(l) + sat)) // (1e-12 cost 20% more CPU for -300 dB)
        break;
    }
    mOtFlux = l;
    const double r = l / sat;
    mOtSatOut = in - mOtCornerW * l * (1.0 + r * r * r * r);
    return mOtSatOut;
  }

  // The saturation flux at the current Depth, in the output's normalized units.
  double _OtSatTarget() const { return mOtSatVs / mOtStrength / (mRl * mOutScale); }

  // The flux whose magnetizing current h(flux) is `current` (h is odd and rises monotonically, and
  // Newton from flux = current approaches from outside, never overshooting).
  double _OtFluxFor(double current) const { return _OtFluxFor(current, current); }
  double _OtFluxFor(double current, double start) const
  {
    const double sat = mOtSat;
    double l = start;
    for (int i = 0; i < 60; i++)
    {
      const double r = l / sat, r4 = r * r * r * r;
      const double step = (l * (1.0 + r4) - current) / (1.0 + 5.0 * r4);
      l -= step;
      if (std::fabs(step) <= 1e-13 * (std::fabs(l) + sat))
        break;
    }
    return l;
  }

  // Voice coil heating, per sample: the coil warms with the power in its resistance and cools
  // toward the magnet; its resistance follows. Returns the output with a cold coil's voltage drop
  // (see Process). Keeps cooling after the switch is turned off, so the load doesn't jump back.
  double _HeatVoiceCoil(double d, double y, double yPerVolt)
  {
    const double watts = mParams.coilHeat ? d * d * mRe : 0.0;
    mCoilRise += mCoilCoef * (kCoilKPerWatt * _Strength() * watts - mCoilRise);
    mCoilRise = std::min(mCoilRise, kCoilMaxRiseK);
    if (!mParams.coilHeat && mCoilRise < 1e-4)
      mCoilRise = 0.0;
    const double extra = mRe - mRe0;
    mRe = mRe0 * (1.0 + kCoilAlpha * mCoilRise); // Seen by the next sample's solve
    return y - extra * d * yPerVolt;
  }

  // Power-tube grid current: zero below 0 V, kGridK * v^1.5 above (softplus-smoothed at 0 so
  // Newton sees a continuous slope), and its derivative.
  static void _GridCurrent(double vg, double& ig, double& dig)
  {
    constexpr double c = 8.0; // Smoothing: ~1/8 V
    const double z = c * vg;
    const double sp = (z > 30.0 ? z : std::log1p(std::exp(z))) / c;
    const double sig = z > 30.0 ? 1.0 : 1.0 / (1.0 + std::exp(-z));
    const double r = std::sqrt(sp);
    ig = kGridK * sp * r;
    dig = kGridK * 1.5 * r * sig;
  }

  static void _Read(const double* v, const double* d, double lo, double hi, double x, double& out, double& dout)
  {
    double pos = (x - lo) * ((kTableSize - 1) / (hi - lo)); // (Constant once inlined: no division)
    if (pos <= 0.0)
    {
      out = v[0];
      dout = d[0];
      return;
    }
    if (pos >= kTableSize - 1)
    {
      // Past the end, extrapolate linearly from the last slope.
      const double xEnd = hi;
      out = v[kTableSize - 1] + d[kTableSize - 1] * (x - xEnd);
      dout = d[kTableSize - 1];
      return;
    }
    const int i = static_cast<int>(pos);
    const double fr = pos - i;
    out = v[i] + fr * (v[i + 1] - v[i]);
    dout = d[i] + fr * (d[i + 1] - d[i]);
  }

  static void _BuildTables(const PentodeModel& m, double* f, double* df, double* t, double* dt)
  {
    for (int i = 0; i < kTableSize; i++)
    {
      // F(x) = softplus(Kp x)^Ex, dF/dx = Ex softplus^(Ex-1) * logistic(Kp x) * Kp
      const double x = kXMin + (kXMax - kXMin) * i / (kTableSize - 1);
      const double z = m.kp * x;
      const double sp = z > 30.0 ? z : std::log1p(std::exp(z));
      const double sig = z > 30.0 ? 1.0 : std::exp(z) / (1.0 + std::exp(z));
      f[i] = sp > 0.0 ? std::pow(sp, m.ex) : 0.0;
      df[i] = sp > 0.0 ? m.ex * std::pow(sp, m.ex - 1.0) * sig * m.kp : 0.0;
      // T(Vp) = atan(Vp / Kvb)
      const double vp = kVpMax * i / (kTableSize - 1);
      t[i] = std::atan(vp / m.kvb);
      dt[i] = (1.0 / m.kvb) / (1.0 + (vp / m.kvb) * (vp / m.kvb));
    }
  }

  void _Update()
  {
    // Built once, thread-safely (C++11 static initialization), then only ever read.
    static const bool built = [] {
      _BuildTables(kEL34, mFEL34, mDFEL34, mTEL34, mDTEL34);
      _BuildTables(k6L6, mF6L6, mDF6L6, mT6L6, mDT6L6);
      return true;
    }();
    (void)built;
    const PentodeModel& m = _Model();
    // Quad (1.68.0): tubes per side, the load each side sees, the transformer (see the top of this file).
    const bool quad = mParams.tubesPerSide >= 2;
    mN = quad ? 2.0 : 1.0;
    mGridN = mN;
    mRl = quad ? kRl / 2.0 : kRl;
    mOtCornerHz = quad ? kQuadOtCornerHz : kOtCornerHz;
    mOtCornerW = 2.0 * 3.14159265358979 * mOtCornerHz;
    mOtSatVs = quad ? kOtSatVoltSeconds * kQuadOtFlux : kOtSatVoltSeconds;
    _UpdateSides();
    mScreenAnchorV = -1.0; // The tubes may have changed: _ScreenFactor recomputes exactly
    mInvMu = 1.0 / m.mu;
    // (The resting point is designed with new tubes at stock mains, as the bench sets the bias.)
    // Fixed bias for an idle current at rest (bisection; current rises with vg1), and with cathode
    // bias, the cathode voltage that gives it with the grids at 0 V (more cathode voltage, less
    // current).
    auto restFor = [&](double idleAmps, double& bias, double& cathode) {
      const double a = (2.0 / m.kg1) * std::pow(kScreenRest / m.kp, m.ex);
      double lo = -150.0, hi = 0.0;
      for (int i = 0; i < 60; i++)
      {
        const double mid = 0.5 * (lo + hi);
        double ip, d1, d2;
        _Tube(m, a, 1.0 / kScreenRest, mid, kBPlus, ip, d1, d2);
        if (ip < idleAmps)
          lo = mid;
        else
          hi = mid;
      }
      bias = 0.5 * (lo + hi);
      cathode = 0.0;
      if (mParams.cathodeBias)
      {
        double klo = 0.0, khi = 150.0;
        for (int i = 0; i < 60; i++)
        {
          const double mid = 0.5 * (klo + khi);
          const double ak = (2.0 / m.kg1) * std::pow((kScreenRest - mid) / m.kp, m.ex);
          double ip, d1, d2;
          _Tube(m, ak, 1.0 / (kScreenRest - mid), -mid, kBPlus - mid, ip, d1, d2);
          if (ip > idleAmps)
            klo = mid;
          else
            khi = mid;
        }
        cathode = 0.5 * (klo + khi);
      }
    };
    {
      // The output level is matched at the stock bias; the bias pot's own setting is applied after.
      restFor(m.idleAmps, mBias, mRestCathode);
      mGridRef = mParams.cathodeBias ? 0.0 : mBias; // Where the grid leaks return to
      const double restVgk = mGridRef - mRestCathode;
      const double aRest = (2.0 / m.kg1) * std::pow((kScreenRest - mRestCathode) / m.kp, m.ex);
      // Small-signal difference-current gain at idle: dD/du = 2 * gm (both tubes, push-pull),
      // reduced by the plate load (each tube's rp across Rl).
      double ip, gm, gp;
      _Tube(m, aRest, 1.0 / (kScreenRest - mRestCathode), restVgk, kBPlus - mRestCathode, ip, gm, gp);
      const double dDdu = quad ? 2.0 * mN * gm / (1.0 + 2.0 * mN * gp * mRl) : 2.0 * gm / (1.0 + 2.0 * gp * kRl);
      // Output scale: small signals come out at unity at the reference Master with no feedback.
      // In the midband the coupling cap is a short, so each grid gets the inverter's swing through
      // the divider Rg / (Rg + Rpi).
      const PhaseInverterTable& pi = PhaseInverterTable::Get();
      mRpi1 = pi.rOut1;
      mRpi2 = pi.rOut2;
      const double gridDivider = kRg / (kRg + 0.5 * (mRpi1 + mRpi2));
      const double refGain = 2.0 + 78.0 * kReferenceMaster * kReferenceMaster;
      mOutScale = dDdu * refGain * gridDivider;
      // Bench: the bias pot (exactly the stock resting point at 100%).
      if (mParams.biasTrim != 1.0)
      {
        restFor(m.idleAmps * std::clamp(mParams.biasTrim, 0.2, 3.0), mBias, mRestCathode);
        mGridRef = mParams.cathodeBias ? 0.0 : mBias;
      }
    }
    // Feedback: up to a loop gain of 2 (about 10 dB of feedback) at the reference Master.
    mBeta = 2.0 * mParams.feedback;
    // Presence: high-pass corner ~1.5 kHz on the fed-back signal; the fed-back signal is
    // y - presence * HP(y), so its gain on this sample's y is (1 - presence * coef).
    mPresCoef = std::exp(-2.0 * 3.14159265358979 * 1500.0 * mT);
    mFbGain = 1.0 - mParams.presence * mPresCoef;
    // Supply: rectifier + supply resistance from stiff (solid state, 20 ohm) to saggy (tube
    // rectifier and soft transformer, 600 ohm), into a 40 uF reservoir; screen resistor 500 ohm to
    // 2.2k into its own 40 uF cap.
    mStrength = std::pow(_Depth(), kDepthPower);
    mOtStrength = 2.0 * mStrength - 1.0; // 1 at 1x, 31 at 4x, 199 at 10x
    mRSupply = 20.0 + 580.0 * mParams.sag;
    mRScreen = 500.0 + 1700.0 * mParams.sag;
    if (quad) // A bigger supply: half the resistance (and twice the caps: the same time constants)
    {
      mRSupply /= mN;
      mRScreen /= mN;
    }
    // The supplies' exact resting point: both tubes' actual idle currents (including the pair's
    // mismatch) and the screen voltage they settle to, so B+ starts exactly at rest instead of
    // drifting there (which the speaker load's resonance turned into a small thump).
    {
      // The plain screen supply's resting point...
      const double rStock = mRScreen;
      double vg2r = kScreenRest, iTot = 0.0, iScr = 0.0, i1 = 0.0, i2 = 0.0, ar = 0.0;
      for (int k = 0; k < 6; k++)
      {
        const double vk = mRestCathode, vgk = mGridRef - vk;
        ar = (2.0 / m.kg1) * std::pow((vg2r - vk) / m.kp, m.ex);
        double x1, x2;
        _Tube(m, ar, 1.0 / (vg2r - vk), vgk, kBPlus - vk, i1, x1, x2);
        _Tube(m, ar * kTube2Match, 1.0 / (vg2r - vk), vgk, kBPlus - vk, i2, x1, x2);
        iTot = mN * (i1 + i2);
        iScr = kScreenFraction * iTot;
        vg2r = kBPlus - rStock * iScr;
      }
      mVg2Rest = vg2r;
      mRestA = ar;
      mRestI1 = i1;
      mRestI2 = i2;
      // The cathode resistor that holds the cathode there with these currents (and the screens').
      mRkCath = mRestCathode / std::max(mN * (i1 + i2) * (1.0 + kScreenFraction), 1e-6);
      mCathodeCoef = 1.0 - std::exp(-mT / std::max(mRkCath * kCathodeCapFarads, 1e-6));
    }
    // Resonance: a low-pass part of the fed-back signal taken away (see the top of this file).
    mResTarget = kResonanceMax * std::clamp(mParams.resonance, 0.0, 1.0);
    mResGlide = 1.0 - std::exp(-mT / 0.005);
    mResCoef = 1.0 - std::exp(-2.0 * 3.14159265358979 * kResonanceHz * mT);
    _SetSquishDepth(m); // Squish's parts (and B+'s resting supply) at its gliding Depth
    mSupplyCoef = 1.0 - std::exp(-mT / (mRSupply * 40e-6 * mN));
    mScreenCoef = 1.0 - std::exp(-mT / (mRScreen * 40e-6 * mN));
    mPiCoef = 1.0 - std::exp(-mT / kPiDecoupleTauS);
    // Grid coupling caps (value per tube type; a change keeps each cap's voltage, see Cap).
    const double cc = mParams.sixL6 ? 100e-9 : 22e-9;
    mCc1.Init(cc, mT);
    mCc2.Init(cc, mT);
    // Speaker network companion conductances (trapezoidal) at this sample rate.
    {
      const double s = mRl / 8.0; // The speaker as each plate sees it through the transformer
      const double w0 = 2.0 * 3.14159265358979 * kSpkFs;
      const double cm = kSpkQms / (kSpkRm * w0); // Parallel RLC: Q = R * sqrt(C/L), w0 = 1/sqrt(LC)
      const double lm = 1.0 / (w0 * w0 * cm);
      mRe0 = kSpkRe * s;
      mRe = mRe0 * (1.0 + kCoilAlpha * mCoilRise);
      mGL1 = mT / (2.0 * kSpkLe * s);
      mG2 = 1.0 / (kSpkR2 * s) + mGL1;
      mGL2 = mT / (2.0 * lm * s);
      mGC = 2.0 * (cm / s) / mT;
      mG3 = 1.0 / (kSpkRm * s) + mGL2 + mGC;
    }
    // Output transformer: ~40 Hz primary-inductance roll-off, ~12 kHz leakage roll-off.
    mOtHpCoef = std::exp(-2.0 * 3.14159265358979 * mOtCornerHz * mT);
    mCoilCoef = 1.0 - std::exp(-mT / kCoilTauS);
    mOtFadeStep = mT / kOtFadeS;
    mOtSatCoef = 1.0 - std::exp(-mT / (0.5 * kOtFadeS));
    mFbProofCoef = 1.0 - std::exp(-mT / 0.0025);
    mSqGlideCoef = 1.0 - std::exp(-mT / 0.03);
    mOtLpCoef = 1.0 - std::exp(-2.0 * 3.14159265358979 * 12000.0 * mT);
  }

  // Squish's Depth-dependent parts at its gliding Depth mSqDepth (see Process), and B+'s resting
  // supply, which depends on the screens' draw. Squish's screen supply idles where the plain one
  // does: its feed is lifted (as a designer picks the supply tap) so the screens sit where they did,
  // and only the squish itself moves them. (Without this, the bigger screen resistor just made
  // everything 0.3-1.3 dB quieter.) Its cap shrinks with Depth as its resistor grows, so the screens
  // recover just as fast.
  void _SetSquishDepth(const PentodeModel& m)
  {
    const double strength = std::pow(mSqDepth, kDepthPower);
    mSqrtStrength = std::sqrt(strength);
    mRSquish = (kSquishScreenOhms * strength + 1700.0 * mParams.sag) / mN;
    mSquishCoef = 1.0 - std::exp(-mT / (mRSquish * mN * kSquishScreenFarads / strength));
    const double vk = mRestCathode, vgk = mGridRef - vk;
    const double iSquish = mN * (_ScreenCurrent(m, mRestA, mVg2Rest - vk, vgk, kBPlus - vk, mRestI1)
                                 + _ScreenCurrent(m, mRestA * kTube2Match, mVg2Rest - vk, vgk, kBPlus - vk, mRestI2));
    mScreenLift = mVg2Rest - kBPlus + mRSquish * iSquish;
    const double iTot = mN * (mRestI1 + mRestI2);
    const double iScr = mParams.screenSquish ? iSquish : kScreenFraction * iTot;
    mVSupply = kBPlus + mRSupply * (iTot + iScr);
  }

  static constexpr double kXMin = -0.5, kXMax = 0.5; // 1/mu + Vg1/Vg2 range
  static constexpr double kVpMax = 1200.0;
  // Real power tubes are never perfectly matched; a few percent imbalance between the pair keeps
  // push-pull from cancelling the even harmonics completely (a perfect pair measured -134 dB).
  static constexpr double kTube2Match = 0.96;

  static inline double mFEL34[kTableSize], mDFEL34[kTableSize], mTEL34[kTableSize], mDTEL34[kTableSize];
  static inline double mF6L6[kTableSize], mDF6L6[kTableSize], mT6L6[kTableSize], mDT6L6[kTableSize];

  PowerAmpParams mParams;
  double mT = 1.0 / 96000.0;
  double mBias = -35.0, mOutScale = 1.0, mBeta = 0.0, mFbGain = 1.0, mPresCoef = 0.0;
  double mRSupply = 300.0, mRScreen = 1000.0, mVSupply = kBPlus, mSupplyCoef = 0.0, mScreenCoef = 0.0;
  // Power-tube grid circuits: the coupling caps (trapezoidal companions that keep their voltage
  // and current across a value change), the inverter's output resistances, the grid voltages.
  struct Cap
  {
    double g = 0.0, ieq = 0.0, v = 0.0, i = 0.0;
    void Init(double c, double t)
    {
      g = 2.0 * c / t;
      ieq = g * v + i;
    }
    void Update(double vn)
    {
      i = g * vn - ieq;
      v = vn;
      ieq = g * vn + i;
    }
    void SetResting(double vr)
    {
      v = vr;
      i = 0.0;
      ieq = g * vr;
    }
  };
  Cap mCc1, mCc2;
  double mRpi1 = 36e3, mRpi2 = 38e3, mGrid1 = -35.0, mGrid2 = -35.0;
  double mOtHpCoef = 1.0, mOtLpCoef = 1.0;
  double mVg2Rest = kScreenRest;
  bool mSettling = false; // Reset's settling solves (see Reset)
  double mVB = kBPlus, mVg2 = kScreenRest, mD = 0.0;
  double mDPrev = 0.0, mGrid1Prev = -35.0, mGrid2Prev = -35.0; // A sample earlier (Newton's predictor)
  double mScreenAnchorV = -1.0, mScreenAnchorA = 0.0; // _ScreenFactor's last exact value (V < 0: none)
  double mInvMu = 1.0 / 11.0; // 1 / the tubes' mu (see _Tube)
  // Cathode bias: the grids' bias reference (the fixed bias, or 0 V), the cathode node and its rest
  // voltage (0 with fixed bias), resistor and cap coefficient.
  double mGridRef = -35.0, mCathodeV = 0.0, mRestCathode = 0.0, mRkCath = 0.0, mCathodeCoef = 0.0;
  // The fed-back signal: the transformer's high- and low-pass on the plate swing (Speaker feedback),
  // and Resonance's amount, coefficient and low-pass state.
  double mFbHp = 0.0, mFbHpPrevIn = 0.0, mFbLp = 0.0;
  double mResonance = 0.0, mResCoef = 0.0, mResLp = 0.0;
  double mResTarget = 0.0, mResMid = 0.0, mResGlide = 1.0; // Its glide (see Process)
  double mFbRest = 0.0; // The fed-back signal at rest (Resonance leaves it alone)
  double mSwing = 0.0, mIp1 = 0.0, mIp2 = 0.0; // Last sample's plate swing and tube currents (probes)
  double mPresHp = 0.0, mPrevY = 0.0;
  // Speaker network: element values (scaled to a plate) and companion-model histories.
  double mRe = kRl, mGL1 = 0.0, mG2 = 1.0, mGL2 = 0.0, mGC = 0.0, mG3 = 1.0;
  double mHL1 = 0.0, mHL2 = 0.0, mHC = 0.0;
  double mOtHp = 0.0, mOtHpPrevIn = 0.0, mOtLp = 0.0;
  // Real Feel: the transformer primary's flux (normalized units), the voice coil's temperature rise
  // and its cold resistance (scaled to a plate, like mRe), and the coil's thermal coefficient.
  double mOtFlux = 0.0, mCoilRise = 0.0, mRe0 = kRl, mCoilCoef = 0.0;
  // OT Iron: the saturating primary's last output, its crossfade (0 = plain, 1 = saturating) and
  // step, and its saturation flux gliding toward the Depth's.
  double mOtSatOut = 0.0, mOtMix = 0.0, mOtFadeStep = 0.0, mOtSat = 1.0, mOtSatMid = 1.0, mOtSatCoef = 0.0;
  double mStrength = 1.0, mSqrtStrength = 1.0, mOtStrength = 1.0; // Depth's strengths (see _Update)
  double mFbProof = 1.0, mFbProofMid = 1.0, mFbProofCoef = 0.0; // Feedback-proof gain and its glide
  double mScreenLift = 0.0; // Squish: the screen feed's offset from B+ (see _Update)
  double mRSquish = 4700.0, mSquishCoef = 0.0, mSquishMix = 0.0; // Squish's screen supply and crossfade
  // Squish's gliding Depth (and its first stage) and glide coefficient; the supplies' resting point.
  double mSqDepth = 1.0, mSqDepthMid = 1.0, mSqGlideCoef = 0.0;
  double mRestA = 0.0, mRestI1 = 0.0, mRestI2 = 0.0;
  double mPiScale = 1.0, mPiCoef = 0.0; // Shared supply: the phase inverter's supply relative to rest
  bench::State mBench; // Variac, perveance and emission ceiling (see SetBench)
  bool mDead[4] = {false, false, false, false}; // 1.62.0: failed power tubes (see SetTubeDead)
  // 1.68.0: tubes per side (the quad), each side's live tubes, the load per side, the grids per side,
  // the output transformer's corner and saturation flux.
  double mN = 1.0, mSideN[2] = {1.0, 1.0}, mRl = kRl, mGridN = 1.0;
  double mOtCornerHz = kOtCornerHz, mOtCornerW = 2.0 * 3.14159265358979 * kOtCornerHz, mOtSatVs = kOtSatVoltSeconds;
};
} // namespace tube
