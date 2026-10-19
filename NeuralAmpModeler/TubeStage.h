#pragma once

// Tube preamp stage for the "Tube Lab" window: one 12AX7 common-cathode gain stage, with two engines.
//
// * Circuit (default): the whole stage simulated as a circuit, per sample -- see TubeCircuit.h.
//   Input coupling cap and grid leak, grid stopper, Miller capacitances, the Dempwolf 12AX7 model
//   with grid current, cathode Rk || bypass cap, plate load from a B+ node with real supply
//   resistance. Bias sets Rk, Bloom sets the input coupling cap (how long blocking lasts), and
//   the cathode cap, plate load and supply are knobs. The feel (bass tightening, Miller softening,
//   blocking, bias shift) comes out of the parts interacting rather than being scripted.
//   Optionally a second, cascaded stage (Stage 2): stage 1's plate swing goes through a Gain 2 pot
//   and the interstage coupling cap into stage 2, which sees stage 1's real output resistance (so
//   its grid conduction charges that cap at the right rate -- interstage blocking) and shares
//   stage 1's B+ supply.
//   Optionally followed by a push-pull power amp (PowerAmp.h): phase inverter, EL34 or 6L6 pair,
//   output transformer, negative feedback with presence, and a sagging B+ and screen supply.
// * Real Feel switches (1.33.0), all off by default: screen squish, transformer saturation and
//   voice coil heat (PowerAmp.h), and the preamp's B+ shared with the power amp's (TubeCircuit.h).
// * Classic: the 1.8 engine. The Koren 12AX7 solved once into a lookup table, plus a "Bloom" bias
//   shift that imitates grid conduction charging the coupling cap.
//
// Both run oversampled (windowed-sinc FIR up/down) to keep the harmonics they create from aliasing
// back down as fizz and ringing. The factor follows the host rate so the stages always run at
// about 176-192 kHz: 4x at 44.1/48 kHz, 2x at 88.2/96 kHz, none at 176.4/192 kHz -- the same sound
// and the same CPU at every host rate. That costs kLatencySamples of latency, which the plugin
// reports to the host while the stage is in the chain.
//
// Output level: half of the Drive gain (in dB) is compensated after the tube, so +24 dB of Drive
// is +12 dB at small signals -- turning Drive up pushes whatever follows (the NAM amp) harder, as a
// tube preamp/boost in front of an amp should. Classic also compensates Bias; Circuit levels
// everything against one reference stage, so Bias, Plate and the cathode cap change the level the
// way they do on a real amp.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "IPlugConstants.h"
#include "MainsHum.h"
#include "TubeCircuit.h"
#include "PowerAmp.h"
#include "ToneStackCircuit.h"
#include "TubeProbe.h"

namespace tube
{
// Koren 12AX7 model constants.
constexpr double kMu = 100.0;
constexpr double kEx = 1.4;
constexpr double kKg1 = 1060.0;
constexpr double kKp = 600.0;
constexpr double kKvb = 300.0;

// Stage circuit.
constexpr double kBPlus = 250.0; // Supply volts
constexpr double kRPlate = 100000.0; // Plate load ohms
constexpr double kRGridStopper = 33000.0; // Source impedance seen by the grid when it conducts
constexpr double kGridCurrentK = 0.0002; // Ig = K * Vg^1.5 for Vg > 0 (amps)

// Lookup table domain, in volts at the grid (before grid-current limiting), relative to cathode.
constexpr double kGridMin = -12.0;
constexpr double kGridMax = 12.0;
constexpr int kLUTSize = 4096;

// 1.0 digital full scale == this many volts at the grid, before Drive.
constexpr double kVoltsPerUnit = 1.0;

// Oversampling. Cascaded saturating stages make harmonics far past the host's Nyquist, which fold
// back down as audible aliasing unless the stages run at a high rate. The factor is picked per host
// rate (OversampleFor) to land at ~176-192 kHz, and the FIR length with it so the latency stays a
// whole number of host samples: each filter delays (taps-1)/2 oversampled samples, so up + down is
// taps-1 == factor * kLatencySamples (61 taps at 4x, 31 at 2x; no filters, no latency, at 1x).
// Economy (1.35.0) halves the factor (~88-96 kHz) for about half the CPU; 8x (1.45.0) doubles it
// (~352-384 kHz) for twice the CPU, for the hottest settings: a hard-clipping Stage 2 makes harmonics
// past the 4x rate's Nyquist that fold into the band (a 2.5 kHz note at Drive 30 / Gain 2 9: aliasing
// -38 dB at 4x, -52 dB at 8x). Both on cancel out.
constexpr int kMaxOversample = 8;
constexpr int kLatencySamples = 15;
constexpr int kMaxFIRTaps = kMaxOversample * kLatencySamples + 1;
inline int OversampleFor(double hostRate, bool economy = false, bool highQuality = false)
{
  int factor = hostRate >= 176000.0 ? 1 : hostRate >= 88000.0 ? 2 : 4;
  if (highQuality)
    factor *= 2;
  return economy ? std::max(1, factor / 2) : factor;
}

constexpr double kPi = 3.14159265358979323846;

inline double DBToAmp(double dB) { return std::pow(10.0, dB / 20.0); }

// Koren plate current (amps) for grid volts vg and plate volts vp.
inline double PlateCurrent(double vg, double vp)
{
  if (vp <= 0.0)
    return 0.0;
  const double arg = kKp * (1.0 / kMu + vg / std::sqrt(kKvb + vp * vp));
  // log1p(exp(x)) without overflowing for large x.
  const double softplus = arg > 30.0 ? arg : std::log1p(std::exp(arg));
  const double e1 = vp / kKp * softplus;
  return e1 > 0.0 ? 2.0 * std::pow(e1, kEx) / kKg1 : 0.0;
}

// Plate volts for a given grid voltage, solved on the load line B+ - Vp - Ra*Ip(vg, Vp) = 0.
// The left side is strictly decreasing in Vp, so bisection always converges.
inline double SolvePlateVoltage(double vg)
{
  double lo = 0.0, hi = kBPlus;
  for (int i = 0; i < 60; i++)
  {
    const double mid = 0.5 * (lo + hi);
    if (kBPlus - mid - kRPlate * PlateCurrent(vg, mid) > 0.0)
      lo = mid;
    else
      hi = mid;
  }
  return 0.5 * (lo + hi);
}

// Voltage that actually reaches the grid when `vin` is applied through the grid stopper: for
// positive grids, grid current drops voltage across the stopper (vin - vg = Rs * Ig(vg)).
inline double LimitGridVoltage(double vin)
{
  if (vin <= 0.0)
    return vin;
  double lo = 0.0, hi = vin;
  for (int i = 0; i < 60; i++)
  {
    const double mid = 0.5 * (lo + hi);
    if (vin - mid - kRGridStopper * kGridCurrentK * std::pow(mid, 1.5) > 0.0)
      lo = mid;
    else
      hi = mid;
  }
  return 0.5 * (lo + hi);
}

// Kaiser-windowed-sinc lowpass a little under the host Nyquist (the up- and downsampling filter).
//
// 1.45.0: it was a Blackman window cut off exactly at the host Nyquist, which is only -6 dB there
// and -21 dB at 28 kHz: on the oversampled side, everything the tubes made between 24 and ~32 kHz
// folded back down into 16-24 kHz at nearly full level (measured: harmonics of a 6 kHz note
// aliased at -28 dB re the signal). Kaiser (beta 7) with the cutoff at 0.87 x Nyquist, the same
// length (so the same latency and CPU), rejects 71 dB from 28 kHz up, so nothing folds below 20 kHz
// unattenuated. The price is the very top: flat to 12 kHz, -0.5 dB at 16 kHz, -2.7 dB at 18 kHz
// through both filters (was -0.02 / -0.3), far above where a guitar speaker has rolled off.
class HalfbandFIR
{
public:
  HalfbandFIR() { Configure(kMaxOversample); }

  static constexpr double kCutoffFraction = 0.87; // Of the host Nyquist
  static constexpr double kKaiserBeta = 7.0;

  // Sets the filter up for an oversampling factor (2 to 8): kLatencySamples * factor + 1 taps.
  void Configure(int factor)
  {
    factor = std::clamp(factor, 2, kMaxOversample);
    mFactor = factor;
    mTaps = factor * kLatencySamples + 1;
    const int center = (mTaps - 1) / 2;
    const double fc = kCutoffFraction * 0.5 / factor; // Cycles per oversampled sample
    double sum = 0.0;
    for (int n = 0; n < mTaps; n++)
    {
      const double k = n - center;
      const double sinc = k == 0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * k) / (kPi * k);
      const double r = 2.0 * n / (mTaps - 1) - 1.0;
      const double window = _BesselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / _BesselI0(kKaiserBeta);
      mCoefs[n] = sinc * window;
      sum += mCoefs[n];
    }
    for (int n = 0; n < mTaps; n++)
      mCoefs[n] /= sum;
    Reset();
  }

  void Reset()
  {
    std::fill(std::begin(mHistory), std::end(mHistory), 0.0);
    mPos = 0;
  }

  void Push(double x)
  {
    mPos = (mPos == 0 ? mTaps : mPos) - 1;
    // Written twice so Output() can read mTaps contiguous samples without wrapping.
    mHistory[mPos] = mHistory[mPos + mTaps] = x;
  }

  double Output() const
  {
    double y = 0.0;
    const double* h = mHistory + mPos;
    for (int n = 0; n < mTaps; n++)
      y += mCoefs[n] * h[n];
    return y;
  }

  // Output() for a zero-stuffed input (1.35.0): `phase` samples since the last non-zero push, so
  // only every mFactor-th history sample from n = phase on can be non-zero. The same terms in the
  // same order as Output() -- the zeros it skips add nothing -- for a quarter of the work at 4x.
  double OutputZeroStuffed(int phase) const
  {
    double y = 0.0;
    const double* h = mHistory + mPos;
    for (int n = phase; n < mTaps; n += mFactor)
      y += mCoefs[n] * h[n];
    return y;
  }

private:
  // The modified Bessel function I0 (Kaiser's window), by its power series.
  static double _BesselI0(double x)
  {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60 && term > 1e-17 * sum; k++)
    {
      const double h = 0.5 * x / k;
      term *= h * h;
      sum += term;
    }
    return sum;
  }

  int mFactor = kMaxOversample;
  double mCoefs[kMaxFIRTaps] = {};
  double mHistory[2 * kMaxFIRTaps] = {};
  int mTaps = kMaxFIRTaps;
  int mPos = 0;
};

struct TubeParams
{
  double driveDB = 12.0; // Gain into the grid
  double biasV = -1.5; // Operating point (grid-to-cathode volts at rest)
  double bloom = 0.5; // 0..1. Classic: how strongly grid conduction shifts the bias. Circuit: the
                      // input coupling cap (2.2 nF..220 nF, log), i.e. how long blocking lasts.
  double bright = 0.3; // 0..1, treble pre-emphasis into the grid
  double mix = 1.0; // 0..1, dry/wet
  double outputDB = 0.0;

  // Engine: the circuit model (TubeCircuit.h) or the Classic LUT stage.
  bool circuit = true;
  // Circuit-engine parts (ignored by Classic).
  bool cathodeCap = true; // Cathode bypass cap fitted
  double cathodeCapUF = 22.0;
  double plateKOhm = 100.0; // Plate load resistor
  bool sag = false;
  double sagSupplyKOhm = 47.0; // Supply resistance feeding the plate node
  double sagTimeS = 0.5; // Supply R x reservoir cap
  // Second, cascaded stage (Circuit engine only). Plate load and supply are shared with stage 1.
  bool stage2 = false;
  double gain2 = 0.5; // 0..1 position of the pot between the stages (audio taper)
  double bias2V = -2.0; // A slightly colder second stage, as in many high-gain designs
  bool cathodeCap2 = true;
  double cathodeCap2UF = 0.68; // Small bypass cap: the classic tight second stage
  double coupleNF = 22.0; // Interstage coupling cap
  // Caps across the plate resistors (0 = none): none on stage 1, the classic 470 pF on stage 2.
  double plateCapPF = 0.0;
  double plateCap2PF = 470.0;

  // Power amp (Circuit engine only), after the preamp stage(s).
  bool powerAmp = false;
  double master = 0.5; // 0..1
  double powerSag = 0.5; // 0..1: stiff to saggy supply
  double feedback = 0.5; // 0..1
  double presence = 0.5; // 0..1
  bool sixL6 = false; // EL34s (false) or 6L6s (true)
  bool speakerLoad = false; // Power tubes drive a real speaker's impedance (see PowerAmp.h)

  // Tone stack (1.20.0, ToneStackCircuit.h) between the preamp and the power amp: 0 off,
  // 1 Fender, 2 Marshall. Knobs 0..1 (noon = 0.5).
  int toneStack = 0;
  double bass = 0.5, mid = 0.5, treble = 0.5;

  // Real Feel switches (1.33.0). All but sharedSupply live in the power amp; all need it on.
  bool screenSquish = false;
  bool sharedSupply = false; // The preamp's B+ follows the power amp's (screen node) supply
  bool otSaturation = false;
  bool coilHeat = false; // Also needs speakerLoad
  double feelDepth = 4.0; // 1x = real parts .. 10x exaggerated (1.33.1, see PowerAmp.h)
  bool economy = false; // 1.35.0: half the oversampling, for about half the CPU (see OversampleFor)
  bool highQuality = false; // 1.45.0: twice the oversampling (8x at 44.1/48 kHz), twice the CPU
  // Circuit+ (1.45.0): a cathode follower after the last gain stage (TubeCircuit.h), and the power
  // amp's cathode bias, feedback from the speaker side of the transformer and Resonance (PowerAmp.h).
  bool cathodeFollower = false;
  bool cathodeBias = false;
  bool speakerFeedback = false;
  double resonance = 0.0; // 0..1
  // Touch (1.38.0), 0..10: picks softer than your recent hardest also get less drive, so they clean
  // up while hard picks keep the full sound (see TubeStage::_TouchGain). 0 = off.
  double touch = 0.0;
  // 60 Hz (1.39.0, MainsHum.h): the supply's 120 Hz ripple and hum on the output; Amount 0..10.
  bool ripple = false;
  double rippleAmount = 5.0;
  double rippleMix = 1.0; // 1.40.0: the grind's dry/wet, 0..1
  static const tonestack::Components& StackParts(int type)
  {
    return type == 2 ? tonestack::kMarshall : tonestack::kFender;
  }

  PowerAmpParams ToPowerAmp() const
  {
    PowerAmpParams a;
    a.master = master;
    a.sag = powerSag;
    a.feedback = feedback;
    a.presence = presence;
    a.sixL6 = sixL6;
    a.speakerLoad = speakerLoad;
    a.screenSquish = screenSquish;
    a.otSaturation = otSaturation;
    a.coilHeat = coilHeat;
    a.sharedSupply = sharedSupply;
    a.feelDepth = feelDepth;
    a.cathodeBias = cathodeBias;
    a.speakerFeedback = speakerFeedback;
    a.resonance = resonance;
    return a;
  }

  // Gain 2's pot position to the voltage ratio it passes (an audio-taper pot is roughly squared).
  static double PotToGain(double position) { return position * position; }

  // Bloom's 0..1 mapped to the input coupling cap: 2.2 nF at 0, 22 nF (a common value) at 0.5,
  // 220 nF at 1.
  static double BloomToCinFarads(double bloom) { return 2.2e-9 * std::pow(100.0, std::clamp(bloom, 0.0, 1.0)); }

  CircuitParams ToCircuit() const
  {
    CircuitParams c;
    c.biasV = biasV;
    c.cinFarads = BloomToCinFarads(bloom);
    c.cathodeCap = cathodeCap;
    c.ckFarads = cathodeCapUF * 1e-6;
    c.raOhms = plateKOhm * 1e3;
    c.sag = sag;
    c.rsOhms = sagSupplyKOhm * 1e3;
    c.sagTimeS = sagTimeS;
    c.plateCapFarads = plateCapPF * 1e-12;
    c.cathodeFollower = cathodeFollower && !stage2; // It follows the last gain stage
    return c;
  }

  // Stage 2, driven from a source with stage 1's output resistance. Its supply comes from stage 1
  // (set per sample), so its own sag is off.
  CircuitParams ToCircuit2(double sourceOhms) const
  {
    CircuitParams c = ToCircuit();
    c.biasV = bias2V;
    c.cinFarads = coupleNF * 1e-9;
    c.cathodeCap = cathodeCap2;
    c.ckFarads = cathodeCap2UF * 1e-6;
    c.plateCapFarads = plateCap2PF * 1e-12;
    c.sag = false;
    c.sourceOhms = sourceOhms;
    c.cathodeFollower = cathodeFollower;
    return c;
  }
};

class TubeStage
{
public:
  TubeStage() { _BuildLUT(); }

  void Reset(double sampleRate, int maxBlockSize)
  {
    mSampleRate = sampleRate;
    mBuffer.assign(static_cast<size_t>(std::max(maxBlockSize, 1)), 0.0);
    mEconomyStage = 0;
    mEconomyCount = 0;
    _Configure();
    // 1.42.0: the circuit starts from rest -- caps uncharged -- and settles in the first ~50 ms,
    // which came out as a thump (-28 dBFS from the Tube Lab, -13 dBFS after a high-gain amp) at
    // start-up, on a sample-rate change, or dragging the Tube Lab into the chain. Hold the output
    // silent while it settles, then fade it in.
    mWarmupPos = 0;
    mWarmupMute = static_cast<int>(kWarmupMuteS * sampleRate);
    mWarmupTotal = mWarmupMute + std::max(1, static_cast<int>(kWarmupFadeS * sampleRate));
  }

  void SetParams(const TubeParams& p)
  {
    // (Economy changes the oversampling, which restarts everything at the new rate: Process fades the
    // output out first and back in after; see _EconomyGain.)
    _SetParams(p);
  }

private:
  // Everything Reset does past the buffer: the oversampling for this host rate (see
  // OversampleFor), and every filter and circuit at it, from rest.
  void _Configure()
  {
    const double sampleRate = mSampleRate;
    mOversample = OversampleFor(sampleRate, mParams.economy, mParams.highQuality);
    mLatency = mOversample > 1 ? kLatencySamples : 0;
    if (mOversample > 1)
    {
      mUp.Configure(mOversample);
      mDown.Configure(mOversample);
    }
    std::fill(std::begin(mDryDelay), std::end(mDryDelay), 0.0);
    mDryPos = 0;
    mBiasShift = 0.0;
    mBrightLP = 0.0;
    mPlateLP = 0.0;
    mDCx = mDCy = 0.0;

    const double os = mOversample * sampleRate;
    mOversampledRate = os;
    mBrightCoef = 1.0 - std::exp(-2.0 * kPi * 1000.0 / sampleRate);
    mPlateCoef = 1.0 - std::exp(-2.0 * kPi * 9000.0 / os); // Miller-capacitance rolloff (Classic)
    mDCCoef = std::exp(-2.0 * kPi * 15.0 / sampleRate); // Output coupling cap
    // Grid conduction charges the coupling cap fast and it bleeds back off slowly.
    mBloomAttack = 1.0 - std::exp(-1.0 / (0.002 * os));
    mBloomRelease = std::exp(-1.0 / (0.15 * os));
    mSmoothCoef = 1.0 - std::exp(-1.0 / (0.02 * sampleRate));
    mSmoothed = mTarget;
    std::fill(std::begin(mTouchDelay), std::end(mTouchDelay), 1.0);
    mTouchPos = 0;
    mTouchEnv = 0.0;
    mTouchHold = 0;
    mTouchRef = kTouchRefStart;
    mTouchTarget = mTouchGain = 1.0;
    mTouchHoldSamples = static_cast<int>(kTouchHoldSeconds * sampleRate);
    mTouchRelease = std::exp(-1.0 / (kTouchReleaseSeconds * sampleRate));
    mTouchRefDecay = DBToAmp(-kTouchRefDecayDBPerSecond / sampleRate);
    mRipple.Reset(sampleRate);
    mTouchGainCoef = 1.0 - std::exp(-1.0 / (kTouchGlideSeconds * sampleRate));
    mCircuit.Reset(os, mParams.ToCircuit());
    mCircuit2.Reset(os, mParams.ToCircuit2(mCircuit.OutputResistance()));
    mCircuit.SetRestingExtraLoad(mParams.stage2 ? mCircuit2.RestingPlateCurrent() : 0.0);
    mCircuit.SetExtraLoad(0.0);
    mPower.Reset(os, mParams.ToPowerAmp());
    mStack.Reset(os, TubeParams::StackParts(mParams.toneStack), mSmoothed.bass, mSmoothed.mid, mSmoothed.treble);
    mFadeLength = std::max(1, static_cast<int>(kFadeSeconds * os));
    mAsleep = false; // From rest; the next sample decides (see _Awake)
    mWakeLength = std::max(1, static_cast<int>(kWakeSeconds * sampleRate));
    mWakePos = mWakeLength;
    mPrerollLeft = 0;
    mBlockLP[0] = mBlockLP[1] = 0.0;
    mBlockCoef = 1.0 - std::exp(-2.0 * kPi * 10.0 / os);
    mFadeRemaining = 0;
  }

  void _SetParams(const TubeParams& p)
  {
    mTarget.drive = DBToAmp(p.driveDB) * kVoltsPerUnit;
    mTarget.bias = p.biasV;
    mTarget.bright = p.bright;
    mTarget.mix = p.mix;
    mTarget.output = DBToAmp(p.outputDB);
    mBloom = p.bloom;
    mTarget.gain2 = TubeParams::PotToGain(p.gain2);
    mTarget.bass = p.bass;
    mTarget.mid = p.mid;
    mTarget.treble = p.treble;
    mTarget.touch = p.touch * kTouchExponentPerUnit;
    mRipple.SetTarget(p.ripple, p.rippleAmount, p.rippleMix);

    // Switch flips (engine, cathode caps, sag, stage 2) crossfade over kFadeSeconds from the old sound to the
    // new one, instead of stepping (which clicked). Knob turns don't need it: the circuit carries
    // its state over like a real pot being turned.
    const bool engineFlip = p.circuit != mParams.circuit;
    const bool partFlip = p.circuit
                          && (p.cathodeCap != mParams.cathodeCap || p.sag != mParams.sag || p.stage2 != mParams.stage2
                              || (p.stage2 && p.cathodeCap2 != mParams.cathodeCap2) || p.powerAmp != mParams.powerAmp
                              || (p.powerAmp && p.sixL6 != mParams.sixL6)
                              || (p.powerAmp && p.speakerLoad != mParams.speakerLoad) // 1.45.0: it stepped (-13 dB)
                              || p.cathodeFollower != mParams.cathodeFollower
                              || (p.powerAmp && (p.cathodeBias != mParams.cathodeBias
                                                 || p.speakerFeedback != mParams.speakerFeedback)))
                          || p.toneStack != mParams.toneStack;
    if (engineFlip || partFlip)
    {
      mFadeFrom = !mParams.circuit ? FadeFrom::Classic : FadeFrom::Circuit;
      // The old circuit(s), still in their current state, finish the fade.
      mFadeCircuit = mCircuit;
      mFadeCircuit2 = mCircuit2;
      mFadeStage2 = mParams.stage2;
      mFadePower = mPower;
      mFadePowerOn = mParams.powerAmp;
      mFadeStack = mStack;
      mFadeStackType = mParams.toneStack;
      mFadeRemaining = mFadeLength;
    }
    if (engineFlip && p.circuit)
      mCircuit.Reset(mOversampledRate, p.ToCircuit()); // Start the circuit from rest.
    else
      mCircuit.SetParams(p.ToCircuit()); // Like turning a pot: the circuit carries its state over.

    // Stage 2 starts from rest when it's switched in (or the circuit engine is); otherwise its
    // parts change like pots, same as stage 1.
    const CircuitParams c2 = p.ToCircuit2(mCircuit.OutputResistance());
    if (p.stage2 && (!mParams.stage2 || (engineFlip && p.circuit)))
      mCircuit2.Reset(mOversampledRate, c2);
    else
      mCircuit2.SetParams(c2);
    mCircuit.SetRestingExtraLoad(p.stage2 ? mCircuit2.RestingPlateCurrent() : 0.0);
    if (!p.stage2)
      mCircuit.SetExtraLoad(0.0);

    // The power amp starts from rest when switched in (or the circuit engine is, or the tubes are
    // swapped); otherwise its knobs change like pots.
    if (p.powerAmp && (!mParams.powerAmp || (engineFlip && p.circuit) || p.sixL6 != mParams.sixL6
                       || p.cathodeBias != mParams.cathodeBias || p.speakerFeedback != mParams.speakerFeedback))
      mPower.Reset(mOversampledRate, p.ToPowerAmp());
    else
      mPower.SetParams(p.ToPowerAmp());
    // A different tone stack starts from rest (crossfaded in, above).
    if (p.toneStack != mParams.toneStack)
      mStack.Reset(mOversampledRate, TubeParams::StackParts(p.toneStack), mSmoothed.bass, mSmoothed.mid,
                   mSmoothed.treble);
    mParams = p;
  }

public:
  // Same buffer-returning convention as the dsp::DSP blocks in the chain (mono in, mono out).
  iplug::sample** Process(iplug::sample** in, size_t numChannels, size_t numFrames)
  {
    if (mBuffer.size() < numFrames)
      mBuffer.resize(numFrames);
    // Debug probes (TubeProbe.h): only while the debug page or CSV log wants them. A fresh capture
    // starts its statistics from zero rather than from whatever was left the last time.
    const bool probing = mProbe != nullptr && mProbe->Enabled();
    if (probing && !mProbing)
      mProbe->Accum().ResetCounters();
    mProbing = probing;
    const auto startTime = probing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

    const iplug::sample* x = in[0];
    for (size_t s = 0; s < numFrames; s++)
    {
      const double economyGain = _EconomyGain();
      _SmoothParams();
      const double dry = _DelayDry(x[s]);
      const double wet = _Awake() ? mRipple.Process(_ProcessSample(x[s] * _WakeGain())) : dry;
      if (mPrerollLeft > 0)
      {
        mPrerollLeft--;
        mSmoothed.mix = 0.0; // Woken, but not heard until it has settled (see _Awake)
      }
      mBuffer[s] =
        static_cast<iplug::sample>(economyGain * mSmoothed.output * (dry + mSmoothed.mix * (wet - dry)));
      if (mWarmupPos < mWarmupTotal)
      {
        // Settling after a reset (see Reset): silent, then a smoothstep in.
        const int pos = mWarmupPos++;
        const double t = pos < mWarmupMute ? 0.0 : (double)(pos - mWarmupMute + 1) / (mWarmupTotal - mWarmupMute);
        mBuffer[s] = static_cast<iplug::sample>(mBuffer[s] * t * t * (3.0 - 2.0 * t));
      }
      if (probing)
      {
        ProbeStats& st = mProbe->Accum();
        const double out = mBuffer[s];
        st.inPeak = std::max(st.inPeak, std::fabs(static_cast<double>(x[s])));
        st.outPeak = std::max(st.outPeak, std::fabs(out));
        st.outSumSq += out * out;
        st.outSamples++;
        mProbe->Frame()[kProbeOutput] = static_cast<float>(out);
        mProbe->Push();
      }
    }
    if (probing)
    {
      ProbeStats& st = mProbe->Accum();
      st.hostSamples += static_cast<int64_t>(numFrames);
      st.audioSeconds += numFrames / mSampleRate;
      st.cpuSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
      mProbe->EndBlock([this](ProbeStats& rest) { _FillRestingStats(rest); });
    }
    mBufPtr[0] = mBuffer.data();
    return mBufPtr;
  }

  // Debug probes: only one stage (channel 0) is given one.
  void SetProbe(TubeProbe* probe) { mProbe = probe; }

  int GetLatency() const { return mLatency; }
  int GetOversampling() const { return mOversample; }

  // Static transfer curve (no bloom/filters/caps moving), for the UI plot. Classic: unity
  // small-signal gain. Circuit: the plate swing at rest, at the circuit's reference gain, through
  // stage 2 as well if it's on. Only reads immutable tables, so it's safe to call from the UI
  // thread while audio runs.
  double TransferCurve(double x, double driveDB, double biasV, bool circuit = false, double plateOhms = 100e3,
                       bool stage2 = false, double gain2 = 0.5, double bias2V = -2.0) const
  {
    const double drive = DBToAmp(driveDB) * kVoltsPerUnit;
    if (!circuit)
      return _Shape(x * drive + biasV, biasV) / drive;
    const double y1 = TriodeStageCircuit::StaticCurve(x * drive, biasV, plateOhms);
    if (!stage2)
      return y1 / drive;
    // Stage 1's plate swing (inverted) through the pot into stage 2, inverted back once more.
    const double swing1 = -y1 * TriodeStageCircuit::ReferenceGain();
    return -TriodeStageCircuit::StaticCurve(TubeParams::PotToGain(gain2) * swing1, bias2V, plateOhms) / drive;
  }

private:
  struct Smoothed
  {
    double drive = 1.0, bias = -1.5, bright = 0.0, mix = 1.0, output = 1.0, gain2 = 0.25;
    double bass = 0.5, mid = 0.5, treble = 0.5; // Tone stack knobs glide like turned pots
    double touch = 0.0; // Touch's expansion exponent (see _TouchGain)
  };

  void _BuildLUT()
  {
    mLUT.resize(kLUTSize);
    mGridLUT.resize(kLUTSize);
    for (int i = 0; i < kLUTSize; i++)
    {
      const double vin = kGridMin + (kGridMax - kGridMin) * i / (kLUTSize - 1);
      mGridLUT[i] = LimitGridVoltage(vin);
      mLUT[i] = SolvePlateVoltage(mGridLUT[i]);
    }
  }

  // Linear interpolation into a table over kGridMin..kGridMax, clamped at the ends.
  static double _Lookup(const std::vector<double>& table, double vin)
  {
    const double pos = (vin - kGridMin) * (kLUTSize - 1) / (kGridMax - kGridMin);
    if (pos <= 0.0)
      return table.front();
    if (pos >= kLUTSize - 1)
      return table.back();
    const int i = static_cast<int>(pos);
    const double frac = pos - i;
    return table[i] + frac * (table[i + 1] - table[i]);
  }

  double _Plate(double vin) const { return _Lookup(mLUT, vin); }

  // Small-signal (inverting) plate gain of the LUT stage at `bias`.
  double _LutGain(double bias) const
  {
    const double delta = 0.05;
    return std::max((_Plate(bias - delta) - _Plate(bias + delta)) / (2.0 * delta), 1e-6);
  }

  // Plate swing around the operating point at `bias`, sign-flipped (the stage inverts) and divided
  // by the small-signal gain there, so the result is in grid volts at unity small-signal gain.
  double _Shape(double vin, double bias) const { return (_Plate(bias) - _Plate(vin)) / _LutGain(bias); }

  void _SmoothParams()
  {
    mSmoothed.drive += mSmoothCoef * (mTarget.drive - mSmoothed.drive);
    mSmoothed.bias += mSmoothCoef * (mTarget.bias - mSmoothed.bias);
    mSmoothed.bright += mSmoothCoef * (mTarget.bright - mSmoothed.bright);
    mSmoothed.mix += mSmoothCoef * (mTarget.mix - mSmoothed.mix);
    mSmoothed.output += mSmoothCoef * (mTarget.output - mSmoothed.output);
    mSmoothed.gain2 += mSmoothCoef * (mTarget.gain2 - mSmoothed.gain2);
    mSmoothed.bass += mSmoothCoef * (mTarget.bass - mSmoothed.bass);
    mSmoothed.mid += mSmoothCoef * (mTarget.mid - mSmoothed.mid);
    mSmoothed.treble += mSmoothCoef * (mTarget.treble - mSmoothed.treble);
    mSmoothed.touch += mSmoothCoef * (mTarget.touch - mSmoothed.touch);
    if (mTarget.touch == 0.0 && mSmoothed.touch < 1e-9)
      mSmoothed.touch = 0.0; // Fully off again: back to the exact Touch-off path
  }

  // Touch's make-up gain, delayed like the dry signal so it lines up with the drive change it undoes.
  double _DelayTouch(double gain)
  {
    if (mLatency == 0)
      return gain;
    const double out = mTouchDelay[mTouchPos];
    mTouchDelay[mTouchPos] = gain;
    mTouchPos = (mTouchPos + 1) % mLatency;
    return out;
  }

  // Touch: a level-dependent drive. The input's envelope (instant attack, held over a low E's period,
  // then a 150 ms release) is compared with a reference that follows your hardest recent picks
  // (instantly up, down by 0.3 dB/s). Drive is scaled by (envelope / reference)^exponent, so a pick
  // 6 dB under your hardest gets 6 x exponent dB less drive and the tubes clip less, as if you had
  // picked softer into a less saturated amp. All of it is made up after the tubes (see _DelayTouch),
  // so where the circuit is clean the level is unchanged: Touch changes how dirty a note is, not how
  // loud it is, and a fading note keeps its natural decay (half make-up shortened clean tails). Not
  // a part of a real amp: it moves the circuit's clean-to-dirty change into your picking range.
  // Returns the gain into the tubes.
  double _TouchGain(double x)
  {
    const double ax = std::fabs(x);
    if (ax >= mTouchEnv)
    {
      mTouchEnv = ax;
      mTouchHold = mTouchHoldSamples;
    }
    else if (mTouchHold > 0)
      mTouchHold--;
    else
      mTouchEnv *= mTouchRelease;
    mTouchRef = std::max(std::max(mTouchEnv, mTouchRef * mTouchRefDecay), kTouchRefFloor);
    if (mSmoothed.touch == 0.0)
      mTouchTarget = 1.0;
    else if (++mTouchCount >= kTouchUpdateSamples) // The pow every 8 samples: the glide below smooths it
    {
      mTouchCount = 0;
      mTouchTarget = std::max(kTouchMinGain, std::pow(std::max(mTouchEnv, 1e-9) / mTouchRef, mSmoothed.touch));
    }
    mTouchGain += mTouchGainCoef * (mTouchTarget - mTouchGain);
    if (mTouchTarget == 1.0 && std::fabs(1.0 - mTouchGain) < 1e-9)
      mTouchGain = 1.0;
    return mTouchGain;
  }

  // Sleep (1.45.0). Switched off, the Tube Lab is the dry signal (its Mix glides to 0) but the whole
  // oversampled circuit kept running unheard: about 18% of a core on this rig, as much as when it's
  // on. Once Mix has glided all the way down, the circuit stops; the dry path (and so the latency)
  // carries on. Switched back on, it restarts from rest -- every cap charged to its resting voltage,
  // as an amp that's been idling -- with its input faded in over kWakeSeconds (it may wake mid-note),
  // and runs unheard for kPrerollSeconds before Mix glides up as before: a rested circuit hit by a
  // note settles over a few cycles (the coupling caps charge: blocking), and heard, that settling
  // was a click 10-12 dB over the old always-running switch-on. With the pre-roll it measures the
  // same as before; the cost is that switching on takes effect 50 ms later (off is still instant).
  bool _Awake()
  {
    if (mAsleep)
    {
      if (mTarget.mix == 0.0)
        return false;
      _Wake();
      return true;
    }
    if (mTarget.mix == 0.0 && mSmoothed.mix < kSleepMix)
    {
      mSmoothed.mix = 0.0;
      mAsleep = true;
      return false;
    }
    return true;
  }

  void _Wake()
  {
    mAsleep = false;
    mCircuit.Reset(mOversampledRate, mParams.ToCircuit());
    mCircuit2.Reset(mOversampledRate, mParams.ToCircuit2(mCircuit.OutputResistance()));
    mCircuit.SetRestingExtraLoad(mParams.stage2 ? mCircuit2.RestingPlateCurrent() : 0.0);
    mCircuit.SetExtraLoad(0.0);
    mPower.Reset(mOversampledRate, mParams.ToPowerAmp());
    mStack.Reset(mOversampledRate, TubeParams::StackParts(mParams.toneStack), mSmoothed.bass, mSmoothed.mid,
                 mSmoothed.treble);
    if (mOversample > 1)
    {
      mUp.Reset();
      mDown.Reset();
    }
    mFadeRemaining = 0;
    mBiasShift = mBrightLP = mPlateLP = 0.0;
    mDCx = mDCy = 0.0;
    mTouchEnv = 0.0;
    mTouchHold = 0;
    mTouchTarget = mTouchGain = 1.0;
    std::fill(std::begin(mTouchDelay), std::end(mTouchDelay), 1.0);
    mWakePos = 0;
    mPrerollLeft = static_cast<int>(kPrerollSeconds * mSampleRate);
  }

  // The circuit's input fade after waking (a smoothstep), then 1.
  double _WakeGain()
  {
    if (mWakePos >= mWakeLength)
      return 1.0;
    const double t = static_cast<double>(++mWakePos) / mWakeLength;
    return t * t * (3.0 - 2.0 * t);
  }

  // Delays the dry signal to line up with the oversampled wet path.
  double _DelayDry(double x)
  {
    if (mLatency == 0)
      return x;
    const double out = mDryDelay[mDryPos];
    mDryDelay[mDryPos] = x;
    mDryPos = (mDryPos + 1) % mLatency;
    return out;
  }

  double _ProcessSample(double x)
  {
    // Tone stack knobs: re-solved only while they're still gliding (a matrix inverse each time).
    _UpdateStackKnobs(mStack);
    if (mFadeRemaining > 0)
      _UpdateStackKnobs(mFadeStack);

    // Treble pre-emphasis into the grid (like a bright cap across the volume pot).
    mBrightLP += mBrightCoef * (x - mBrightLP);
    double emphasized = x + 2.0 * mSmoothed.bright * (x - mBrightLP);
    const double touchGain = _TouchGain(x);
    if (touchGain != 1.0)
      emphasized *= touchGain;

    // Upsample by zero-stuffing (which divides the level by kOversample, hence the gain), process
    // each oversampled sample, and decimate on the first phase -- so the total delay is a whole
    // kLatencySamples at the host rate, lined up with the delayed dry signal.
    double y = 0.0;
    mProbeFramePhase = true;
    if (mOversample == 1)
      y = _ProcessOversampled(emphasized); // The host rate is already high enough.
    else
      for (int phase = 0; phase < mOversample; phase++)
      {
        mProbeFramePhase = phase == 0; // The scope gets one frame per host sample
        mUp.Push(phase == 0 ? emphasized : 0.0);
        mDown.Push(_ProcessOversampled(mOversample * mUp.OutputZeroStuffed(phase)));
        if (phase == 0)
          y = mDown.Output();
      }

    // Output coupling cap: removes the DC the asymmetric clipping produces.
    const double dcOut = y - mDCx + mDCCoef * mDCy;
    mDCx = y;
    mDCy = dcOut;
    const double makeup = _DelayTouch(touchGain);
    return makeup != 1.0 ? dcOut / makeup : dcOut;
  }

  double _ProcessOversampled(double x)
  {
    const double y = mParams.circuit ? _Circuit(mCircuit, mCircuit2, mParams.stage2, mStack, mParams.toneStack,
                                                mPower, mParams.powerAmp, x)
                                     : _Stack(mStack, mParams.toneStack, _Classic(x));
    if (mProbing)
      _Probe(x, y);
    if (mFadeRemaining <= 0)
      return y;
    // Mid-crossfade after a switch flip: blend in from whatever was playing before.
    const double old = mFadeFrom == FadeFrom::Classic
                         ? _Stack(mFadeStack, mFadeStackType, _Classic(x))
                         : _Circuit(mFadeCircuit, mFadeCircuit2, mFadeStage2, mFadeStack, mFadeStackType, mFadePower,
                                    mFadePowerOn, x);
    const double g = static_cast<double>(mFadeRemaining--) / mFadeLength;
    return y + g * (old - y);
  }

  // Circuit engine: the whole stage is simulated, so bias (via Rk), blocking, the cathode bypass
  // cap, Miller roll-off and sag all come out of the circuit itself. The tone stack sits between
  // the preamp and the power amp, as in a real amp.
  double _Circuit(TriodeStageCircuit& stage1, TriodeStageCircuit& stage2, bool useStage2, tonestack::TMBStack& stack,
                  int stackType, PushPullPowerAmp& power, bool usePower, double x)
  {
    const double pre = _Stack(stack, stackType, _Preamp(stage1, stage2, useStage2, x));
    mLastPre = pre;
    if (!usePower)
    {
      if (stage1.SupplyShared())
        stage1.SetSupplyTarget(1.0); // Power amp switched out: the preamp's supply settles back
      return pre;
    }
    const double out = power.Process(pre);
    // Shared supply: the preamp hangs off the power amp's screen node (as in most amps: B+, then the
    // screens, then the phase inverter and preamp nodes), so its rail follows that node's sag.
    if (mParams.sharedSupply || stage1.SupplyShared())
      stage1.SetSupplyTarget(mParams.sharedSupply ? PushPullPowerAmp::SharedSupplyScale(
                                                      power.ScreenVoltage() / power.RestingScreenVoltage(),
                                                      power.Strength())
                                                  : 1.0);
    return out;
  }

  static double _Stack(tonestack::TMBStack& stack, int stackType, double x)
  {
    return stackType > 0 ? stack.Process(x) : x;
  }

  void _UpdateStackKnobs(tonestack::TMBStack& stack)
  {
    stack.SetKnobs(_Quantize(mSmoothed.bass), _Quantize(mSmoothed.mid), _Quantize(mSmoothed.treble));
  }
  // Knob glides re-solve the stack only when they've moved a visible amount (1/1000 of a turn).
  static double _Quantize(double knob) { return std::round(knob * 1000.0) / 1000.0; }

  // The preamp stage(s).
  double _Preamp(TriodeStageCircuit& stage1, TriodeStageCircuit& stage2, bool useStage2, double x)
  {
    const double y1 = stage1.Process(x * mSmoothed.drive);
    if (!useStage2)
      return y1 / std::sqrt(mSmoothed.drive);
    // Stage 2: shares stage 1's B+ (and reports its current draw back as extra supply load), and
    // is driven by stage 1's plate swing through the Gain 2 pot. Both stages invert; Process
    // re-inverts its own stage, so flip once more for stage 1's.
    stage2.SetRail(stage1.RailVoltage());
    const double y2 = stage2.Process(mSmoothed.gain2 * stage1.PlateSwing());
    stage1.SetExtraLoad(stage2.SupplyCurrent());
    return -y2 / std::sqrt(mSmoothed.drive);
  }

  // Classic engine: the Koren LUT plus the Bloom bias shift.
  double _Classic(double x)
  {
    const double bias = mSmoothed.bias - mBiasShift;
    const double vin = x * mSmoothed.drive + bias;

    // Bloom: the voltage the grid stopper drops while the grid conducts charges the coupling cap,
    // pushing the bias colder (fast), and it then bleeds back off (slow). A peak follower on that
    // voltage, scaled by the Bloom knob.
    const double conduction = vin - _Lookup(mGridLUT, vin);
    const double target = std::min(mBloom * kBloomDepth * conduction, kMaxBiasShift);
    if (target > mBiasShift)
      mBiasShift += mBloomAttack * (target - mBiasShift);
    else
      mBiasShift *= mBloomRelease;

    // Normalize against the nominal operating point (not the shifted one) so the bloom is heard
    // as the real level/asymmetry change it causes.
    const double y = _Shape(vin, mSmoothed.bias) / std::sqrt(mSmoothed.drive);
    mPlateLP += mPlateCoef * (y - mPlateLP);
    return mPlateLP;
  }

  // Debug probes, per oversampled sample of the main (not crossfading-out) path: statistics every
  // sample, a scope frame on the first phase.
  void _Probe(double x, double y)
  {
    ProbeStats& st = mProbe->Accum();
    float* f = mProbe->Frame();
    const bool frame = mProbeFramePhase;
    st.samples++;
    const double grid = x * mSmoothed.drive;
    st.gridDrivePeak = std::max(st.gridDrivePeak, std::fabs(grid));
    if (frame)
    {
      std::fill(f, f + kNumProbes, 0.f);
      f[kProbeInput] = static_cast<float>(grid);
    }
    if (!mParams.circuit)
    {
      if (frame)
        f[kProbeStackOut] = static_cast<float>(y);
      return;
    }
    _ProbeStage(st, 0, mCircuit);
    if (mParams.stage2)
      _ProbeStage(st, 1, mCircuit2);
    st.railMin = std::min(st.railMin, mCircuit.RailVoltage());
    st.railMax = std::max(st.railMax, mCircuit.RailVoltage());
    st.newtonMax = std::max(st.newtonMax, mCircuit.LastIterations());
    st.newtonSum += mCircuit.LastIterations();
    if (mParams.powerAmp)
    {
      const double g1 = mPower.Grid1(), g2 = mPower.Grid2(), i1 = mPower.Current1(), i2 = mPower.Current2();
      st.paSamples++;
      st.paGridMax = std::max(st.paGridMax, std::max(g1, g2));
      st.paGridMin = std::min(st.paGridMin, std::min(g1, g2));
      if (std::max(g1, g2) > 0.0)
        st.paGridConducting++;
      if (std::min(i1, i2) < kCutoffFraction * mPower.IdleAmps())
        st.paCutoff++;
      st.paCurrentMax = std::max(st.paCurrentMax, std::max(i1, i2));
      st.paRailMin = std::min(st.paRailMin, mPower.RailVoltage());
      st.paScreenMin = std::min(st.paScreenMin, mPower.ScreenVoltage());
    }
    if (!frame)
      return;
    f[kProbeV1Vgk] = static_cast<float>(mCircuit.GridCathodeVoltage());
    f[kProbeV1Plate] = static_cast<float>(mCircuit.PlateVoltage());
    f[kProbeV1Cathode] = static_cast<float>(mCircuit.CathodeVoltage());
    f[kProbeV1Current] = static_cast<float>(1e3 * mCircuit.PlateCurrent());
    f[kProbePreRail] = static_cast<float>(mCircuit.RailVoltage());
    f[kProbeV1Block] = static_cast<float>(mBlockLP[0]);
    if (mParams.stage2)
    {
      f[kProbeV2Vgk] = static_cast<float>(mCircuit2.GridCathodeVoltage());
      f[kProbeV2Plate] = static_cast<float>(mCircuit2.PlateVoltage());
      f[kProbeV2Block] = static_cast<float>(mBlockLP[1]);
    }
    f[kProbeStackOut] = static_cast<float>(mLastPre);
    if (mParams.powerAmp)
    {
      f[kProbePAGrid1] = static_cast<float>(mPower.Grid1());
      f[kProbePAGrid2] = static_cast<float>(mPower.Grid2());
      f[kProbePAPlate1] = static_cast<float>(mPower.Plate1());
      f[kProbePAPlate2] = static_cast<float>(mPower.Plate2());
      f[kProbePACurrent1] = static_cast<float>(1e3 * mPower.Current1());
      f[kProbePACurrent2] = static_cast<float>(1e3 * mPower.Current2());
      f[kProbePARail] = static_cast<float>(mPower.RailVoltage());
      f[kProbePAScreen] = static_cast<float>(mPower.ScreenVoltage());
    }
  }

  void _ProbeStage(ProbeStats& st, int i, const TriodeStageCircuit& c)
  {
    const double vgk = c.GridCathodeVoltage(), vp = c.PlateVoltage();
    st.vgkMin[i] = std::min(st.vgkMin[i], vgk);
    st.vgkMax[i] = std::max(st.vgkMax[i], vgk);
    st.plateMin[i] = std::min(st.plateMin[i], vp);
    st.plateMax[i] = std::max(st.plateMax[i], vp);
    // Blocking is the slow part of the coupling cap's charge; the cap also carries some of the
    // signal itself (more so at low frequencies), which a 10 Hz low-pass leaves out.
    mBlockLP[i] += mBlockCoef * (c.BlockingShift() - mBlockLP[i]);
    st.blockMax[i] = std::max(st.blockMax[i], mBlockLP[i]);
    if (vgk > kGridConductVgk)
      st.gridConducting[i]++;
    if (c.PlateCurrent() < kCutoffFraction * c.RestingPlateCurrent())
      st.cutoff[i]++;
  }

  // The circuit's resting operating points, for the debug page to explain.
  void _FillRestingStats(ProbeStats& s) const
  {
    s.circuit = mParams.circuit;
    s.stage2 = mParams.stage2;
    s.powerAmp = mParams.powerAmp;
    s.oversample = mOversample;
    s.sampleRate = mSampleRate;
    const TriodeStageCircuit* stages[2] = {&mCircuit, &mCircuit2};
    for (int i = 0; i < 2; i++)
    {
      s.rk[i] = stages[i]->Rk();
      s.idleMA[i] = 1e3 * stages[i]->RestingPlateCurrent();
      s.plateRest[i] = stages[i]->RestingPlateVoltage();
      s.cathodeCornerHz[i] = stages[i]->CathodeCornerHz();
      s.blockRecoverMs[i] = 1e3 * stages[i]->BlockRecoverSeconds();
    }
    s.rail = TriodeStageCircuit::kBPlus;
    s.paBias = mPower.Bias();
    s.paIdleMA = 1e3 * mPower.IdleAmps();
    s.paRailRest = PushPullPowerAmp::kBPlus;
    s.paScreenRest = mPower.RestingScreenVoltage();
    s.feedbackDB = mPower.FeedbackDB();
  }

  // How far (fraction of the conduction voltage) and how far at most (volts) bloom shifts the bias.
  static constexpr double kBloomDepth = 0.5;
  static constexpr double kMaxBiasShift = 4.0;

  // Plate volts, indexed by grid volts at the stopper input.
  std::vector<double> mLUT;
  // Grid volts actually reached (after grid-current limiting), same indexing.
  std::vector<double> mGridLUT;
  HalfbandFIR mUp, mDown;
  double mDryDelay[kLatencySamples] = {};
  int mDryPos = 0;
  int mOversample = kMaxOversample, mLatency = kLatencySamples;
  // Economy switch (see SetParams): an S-curve fade out, the restart at the new oversampling, a
  // short silence while the restarted circuits settle, and an S-curve fade back in. (A 5 ms linear
  // fade with no settling popped audibly.)
  static constexpr double kEconomyFadeSeconds = 0.015, kEconomySettleSeconds = 0.01;
  int mEconomyStage = 0, mEconomyCount = 0; // 0 idle, 1 fading out, 2 settling, 3 fading in
  // Warm-up after Reset (1.42.0): silent for kWarmupMuteS while the circuit settles, then faded in.
  static constexpr double kWarmupMuteS = 0.06, kWarmupFadeS = 0.04;
  int mWarmupPos = 0, mWarmupMute = 0, mWarmupTotal = 0;
  // Sleep (see _Awake): Mix below this (-140 dB) counts as all the way down.
  static constexpr double kSleepMix = 1e-7, kWakeSeconds = 0.02, kPrerollSeconds = 0.05;
  bool mAsleep = false;
  int mWakePos = 0, mWakeLength = 1, mPrerollLeft = 0;

  double _EconomyGain()
  {
    // Flipped (even back again mid-fade): start the switch once the last one has finished.
    if (mEconomyStage == 0 && OversampleFor(mSampleRate, mParams.economy, mParams.highQuality) != mOversample)
    {
      mEconomyStage = 1;
      mEconomyCount = 0;
    }
    if (mEconomyStage == 0)
      return 1.0;
    const int fade = std::max(1, static_cast<int>(kEconomyFadeSeconds * mSampleRate));
    const int settle = std::max(1, static_cast<int>(kEconomySettleSeconds * mSampleRate));
    auto smooth = [](double x) { return x * x * x * (x * (6.0 * x - 15.0) + 10.0); };
    double g = 1.0;
    if (mEconomyStage == 1)
    {
      g = smooth(1.0 - static_cast<double>(++mEconomyCount) / fade);
      if (mEconomyCount >= fade)
      {
        _Configure(); // The new oversampling (mParams already has it)
        mEconomyStage = 2;
        mEconomyCount = 0;
      }
    }
    else if (mEconomyStage == 2)
    {
      g = 0.0;
      if (++mEconomyCount >= settle)
      {
        mEconomyStage = 3;
        mEconomyCount = 0;
      }
    }
    else
    {
      g = smooth(static_cast<double>(++mEconomyCount) / fade);
      if (mEconomyCount >= fade)
        mEconomyStage = 0;
    }
    return g;
  }

  double mSampleRate = 48000.0, mOversampledRate = 96000.0;
  double mBrightCoef = 0.0, mBrightLP = 0.0;
  double mPlateCoef = 1.0, mPlateLP = 0.0;
  double mDCCoef = 0.999, mDCx = 0.0, mDCy = 0.0;
  double mBloom = 0.5, mBloomAttack = 0.0, mBloomRelease = 0.0, mBiasShift = 0.0;
  double mSmoothCoef = 1.0;
  Smoothed mTarget, mSmoothed;

  // Touch (see _TouchGain).
  static constexpr double kTouchExponentPerUnit = 0.3; // Knob 10 = 3: 6 dB softer, 18 dB less drive
  static constexpr double kTouchHoldSeconds = 0.015; // Longer than a low E's period (12 ms)
  static constexpr double kTouchReleaseSeconds = 0.15;
  static constexpr double kTouchRefDecayDBPerSecond = 0.3;
  static constexpr double kTouchGlideSeconds = 0.002;
  static constexpr int kTouchUpdateSamples = 8;
  static constexpr double kTouchRefStart = 0.1; // -20 dBFS, about a hard pick at the Tube Lab here
  static constexpr double kTouchRefFloor = 0.01; // -40 dBFS: quieter playing never becomes "hard"
  static constexpr double kTouchMinGain = 0.0158; // -36 dB
  double mTouchEnv = 0.0, mTouchRef = kTouchRefStart, mTouchTarget = 1.0, mTouchGain = 1.0;
  double mTouchRelease = 0.0, mTouchRefDecay = 1.0, mTouchGainCoef = 1.0;
  int mTouchHold = 0, mTouchHoldSamples = 0, mTouchCount = 0;
  double mTouchDelay[kLatencySamples] = {};
  int mTouchPos = 0;

  mains::Ripple mRipple; // 60 Hz (see TubeParams::ripple)

  TubeParams mParams;
  TriodeStageCircuit mCircuit, mCircuit2;
  PushPullPowerAmp mPower;
  tonestack::TMBStack mStack;

  // Crossfade after a switch flip (see SetParams).
  static constexpr double kFadeSeconds = 0.01;
  enum class FadeFrom
  {
    Classic,
    Circuit
  };
  FadeFrom mFadeFrom = FadeFrom::Classic;
  TriodeStageCircuit mFadeCircuit, mFadeCircuit2;
  bool mFadeStage2 = false;
  PushPullPowerAmp mFadePower;
  bool mFadePowerOn = false;
  tonestack::TMBStack mFadeStack;
  int mFadeStackType = 0;
  int mFadeLength = 1, mFadeRemaining = 0;

  std::vector<iplug::sample> mBuffer;
  iplug::sample* mBufPtr[1] = {nullptr};

  // Debug probes (see SetProbe). Not owned.
  TubeProbe* mProbe = nullptr;
  bool mProbing = false, mProbeFramePhase = true;
  double mBlockLP[2] = {0.0, 0.0}, mBlockCoef = 0.0; // Coupling caps' slow charge (blocking)
  double mLastPre = 0.0; // Preamp (after the tone stack) output of the last oversampled sample
};
} // namespace tube
