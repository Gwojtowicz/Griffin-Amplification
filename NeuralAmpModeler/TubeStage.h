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
// 1.63.0: a hot setting (the second gain stage on) runs at 8x whatever the 8x switch says -- two cascaded
// clipping stages at 4x left -51 dB of aliasing on a high E, at Economy's 2x -40 dB (audible grit);
// at 8x -67 dB. Economy still halves it (to 4x).
constexpr int kMaxOversample = 8;
constexpr int kLatencySamples = 15;
constexpr int kMaxFIRTaps = kMaxOversample * kLatencySamples + 1;
inline int OversampleFor(double hostRate, bool economy = false, bool highQuality = false, bool hot = false)
{
  int factor = hostRate >= 176000.0 ? 1 : hostRate >= 88000.0 ? 2 : 4;
  if (highQuality || hot)
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

// 1.68.0: the chassis's extra preamp sockets, V5 and V6, cascaded after Stage 2 (or V1) in that order, before
// the cathode follower and the tone stack. Each holds a tube (TubeType) doing one job:
// * Gain: a common-cathode gain stage like V2, driven through its own Gain pot and coupling cap.
// * Follower: a cathode follower on the gain stage before it (its output then drives what's next).
// * Boost: a gain stage with its cathode unbypassed -- local feedback: about a third of the gain,
//   cleaner, tighter, more headroom (Cap is ignored).
// They hang off the preamp's supply with V1 and V2. Off, a socket is empty: no sound, no CPU.
constexpr int kNumSockets = 2;
enum SocketRole
{
  kSocketOff = 0,
  kSocketGain,
  kSocketFollower,
  kSocketBoost,
  kNumSocketRoles
};

struct SocketParams
{
  int role = kSocketOff;
  int tube = k12AX7;
  double gain = 0.5; // 0..1 pot position into it (audio taper)
  double biasV = -1.5;
  double cathodeCapUF = 0.68; // 0 = none
  double plateCapPF = 470.0;
  double coupleNF = 22.0;
  bool IsGain() const { return role == kSocketGain || role == kSocketBoost; }
};

// The bias each socket tube is usually run at in a preamp (what the chassis sets when you change type).
inline double TypicalBias(int tube)
{
  static const double v[kNumTubeTypes] = {-1.5, -1.5, -5.0, -2.0};
  return v[std::clamp(tube, 0, kNumTubeTypes - 1)];
}

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
  // Bench (1.58.0, TubeBench.h; circuit engine only): the Variac (wall volts, 120 stock), the power
  // tubes' bias pot (idle current relative to stock), and the preamp's and power tubes' age (0 new ..
  // 1 worn out).
  double mainsV = 120.0;
  double powerBias = 1.0;
  double preampAge = 0.0;
  double powerAge = 0.0;
  // 1.62.0: power tubes can fail: run past their rated plate dissipation (red-plating), they wear out
  // and die (see _UpdateTubeLife). Off: they never do.
  bool canFail = false;
  // 1.64.0: the AMP block's capture is the preamp (circuit engine): the input arriving here is already
  // the capture's output (the plugin runs it), and it goes straight to the tone stack (if one's on)
  // and the power amp, in place of the preamp tubes. Everything after the preamp is unchanged.
  bool capture = false;
  // 1.68.0: the chassis sockets V5, V6 (see SocketParams), and the power amp as a quad (four power tubes,
  // see PowerAmp.h).
  bool quad = false;
  SocketParams socket[kNumSockets];

  // The preamp's gain stages are numbered 0 (V1), 1 (V2) and 2 + k (socket k). The one a socket
  // follows: the last gain socket before it, else V2 if it's on, else V1.
  int GainBefore(int k) const
  {
    for (int j = k - 1; j >= 0; j--)
      if (socket[j].IsGain())
        return 2 + j;
    return stage2 ? 1 : 0;
  }
  int LastGain() const { return GainBefore(kNumSockets); }
  // The socket whose follower sits on gain stage `stage` (the first one, if two follow it), or -1.
  int FollowerSocketOn(int stage) const
  {
    for (int k = 0; k < kNumSockets; k++)
      if (socket[k].role == kSocketFollower && GainBefore(k) == stage)
        return k;
    return -1;
  }
  // Whether a cathode follower sits on gain stage `stage`: a follower socket after it, or the
  // Follower switch (V3) on the last one.
  bool Followed(int stage) const
  {
    return FollowerSocketOn(stage) >= 0 || (cathodeFollower && stage == LastGain());
  }
  int FollowerTube(int stage) const
  {
    const int k = FollowerSocketOn(stage);
    return k >= 0 ? socket[k].tube : k12AX7;
  }
  // Any cascaded stage after V1: the circuit then runs at the higher oversampling (OversampleFor).
  bool Hot() const
  {
    if (!circuit)
      return false;
    if (stage2)
      return true;
    for (const SocketParams& s : socket)
      if (s.IsGain())
        return true;
    return false;
  }

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
    a.biasTrim = powerBias;
    a.tubesPerSide = quad ? 2 : 1;
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
    c.cathodeFollower = Followed(0); // The Follower switch follows the last gain stage
    c.followerTube = FollowerTube(0);
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
    c.cathodeFollower = stage2 ? Followed(1) : cathodeFollower;
    c.followerTube = FollowerTube(1);
    return c;
  }

  // 1.68.0: socket k as a gain stage (Gain or Boost), driven from a source of `sourceOhms`. Like
  // stage 2, its supply comes from stage 1 (set per sample).
  CircuitParams ToSocket(int k, double sourceOhms) const
  {
    const SocketParams& s = socket[k];
    CircuitParams c = ToCircuit();
    c.biasV = s.biasV;
    c.cinFarads = s.coupleNF * 1e-9;
    c.cathodeCap = s.role == kSocketGain && s.cathodeCapUF > 0.0;
    c.ckFarads = (s.cathodeCapUF > 0.0 ? s.cathodeCapUF : 0.68) * 1e-6;
    c.plateCapFarads = s.plateCapPF * 1e-12;
    c.sag = false;
    c.sourceOhms = sourceOhms;
    c.tubeType = s.tube;
    c.cathodeFollower = Followed(2 + k);
    c.followerTube = FollowerTube(2 + k);
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
    mOversample = OversampleFor(sampleRate, mParams.economy, mParams.highQuality, mParams.Hot());
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
    // Bench: no glide from a reset (the circuits settle at rest where the Variac already is).
    mBenchMains = mBenchHeat = _BenchMainsTarget();
    mPopDecay = std::exp(-1.0 / (0.008 * sampleRate));
    mWarmStep = 1.0 / (2.0 * sampleRate);
    for (int k = 0; k < kNumPowerTubes; k++)
      mPower.SetTubeDead(k, mPowerDead[k]); // (A failed tube stays failed through a reset)
    mBenchAge[0] = mParams.preampAge;
    mBenchAge[1] = mParams.powerAge;
    mBenchAgeCoef = 1.0 - std::exp(-1.0 / (kBenchAgeGlideS * sampleRate));
    mBenchSupplyCoef = 1.0 - std::exp(-1.0 / (bench::kSupplyTauS * sampleRate));
    mBenchHeatCoef = 1.0 - std::exp(-1.0 / (bench::kHeaterTauS * sampleRate));
    _ApplyBench();
    mCircuit.Reset(os, mParams.ToCircuit());
    mCircuit2.Reset(os, mParams.ToCircuit2(mCircuit.OutputResistance()));
    _SetSockets(mParams, nullptr);
    mCircuit.SetRestingExtraLoad(_RestingExtraLoad(mParams));
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
    for (int k = 0; k < kNumSockets; k++)
      mTarget.socketGain[k] = TubeParams::PotToGain(p.socket[k].gain);
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
                              || (p.powerAmp && (p.sixL6 != mParams.sixL6 || p.quad != mParams.quad))
                              || (p.powerAmp && p.speakerLoad != mParams.speakerLoad) // 1.45.0: it stepped (-13 dB)
                              || p.cathodeFollower != mParams.cathodeFollower
                              || (p.powerAmp && (p.cathodeBias != mParams.cathodeBias
                                                 || p.speakerFeedback != mParams.speakerFeedback)))
                          || p.toneStack != mParams.toneStack || p.capture != mParams.capture
                          || (p.circuit && _SocketsFlip(p));
    if (engineFlip || partFlip)
    {
      mFadeFrom = !mParams.circuit ? FadeFrom::Classic : FadeFrom::Circuit;
      // The old circuit(s), still in their current state, finish the fade.
      mFadeCircuit = mCircuit;
      mFadeCircuit2 = mCircuit2;
      mFadeStage2 = mParams.stage2;
      for (int k = 0; k < kNumSockets; k++)
        if (mSocketMask & (1 << k))
          mFadeSocket[k] = mSocket[k];
      mFadeSocketMask = mSocketMask;
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
    // The sockets likewise (1.68.0): from rest when one starts working or gets a new tube.
    _SetSockets(p, engineFlip && p.circuit ? nullptr : &mParams);
    mCircuit.SetRestingExtraLoad(_RestingExtraLoad(p));
    if (!p.stage2 && mSocketMask == 0)
      mCircuit.SetExtraLoad(0.0);

    // The power amp starts from rest when switched in (or the circuit engine is, or the tubes are
    // swapped); otherwise its knobs change like pots.
    if (p.powerAmp && (!mParams.powerAmp || (engineFlip && p.circuit) || p.sixL6 != mParams.sixL6 || p.quad != mParams.quad
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
      _BenchGlide();
      const double dry = _DelayDry(x[s]);
      double wet = _Awake() ? mRipple.Process(_ProcessSample(x[s] * _WakeGain())) : dry;
      if (mPopEnv > 0.0 || mFailGain != 1.0)
        wet = _FailEffects(wet);
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
      if (mUiWanted)
      {
        // The chassis page's glow: each preamp tube's signal this block (V1 its grid drive, V2 its
        // plate, V3 and V4 the preamp's output).
        mLevelAcc[0] = std::max(mLevelAcc[0], std::fabs(static_cast<double>(x[s])) * mSmoothed.drive);
        mLevelAcc[2] = std::max(mLevelAcc[2], std::fabs(mLastPre));
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
    _UpdateTubeLife(numFrames);
    if (mUiWanted)
    {
      for (int i = 0; i < kNumStageLevels; i++)
      {
        mLevel[i] = mLevelAcc[i];
        mLevelAcc[i] = 0.0;
      }
      mLevel[3] = mLevel[2];
      // A follower socket shows the signal of the stage it follows.
      for (int k = 0; k < kNumSockets; k++)
        if (mParams.socket[k].role == kSocketFollower)
        {
          const int stage = mParams.GainBefore(k);
          mLevel[4 + k] = stage == 0 ? mLevel[2] : stage == 1 ? mLevel[1] : mLevel[4 + stage - 2];
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
    double socketGain[kNumSockets] = {0.25, 0.25}; // 1.68.0: each socket's Gain pot
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
    if (mSocketMask != 0)
      for (int k = 0; k < kNumSockets; k++)
        mSmoothed.socketGain[k] += mSmoothCoef * (mTarget.socketGain[k] - mSmoothed.socketGain[k]);
    else
      std::copy(std::begin(mTarget.socketGain), std::end(mTarget.socketGain), std::begin(mSmoothed.socketGain));
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
    _SetSockets(mParams, nullptr);
    mCircuit.SetRestingExtraLoad(_RestingExtraLoad(mParams));
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

    // Treble pre-emphasis into the grid (like a bright cap across the volume pot). With the capture as
    // the preamp (1.64.0), there's no grid here: the capture's output goes in as it is.
    double emphasized = x, touchGain = 1.0;
    if (!(mParams.capture && mParams.circuit))
    {
      mBrightLP += mBrightCoef * (x - mBrightLP);
      emphasized = x + 2.0 * mSmoothed.bright * (x - mBrightLP);
      touchGain = _TouchGain(x);
      if (touchGain != 1.0)
        emphasized *= touchGain;
    }

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
    const double y = mParams.circuit ? _Circuit(mCircuit, mCircuit2, mParams.stage2, mSocket, mSocketMask, mStack,
                                                mParams.toneStack, mPower, mParams.powerAmp, x)
                                     : _Stack(mStack, mParams.toneStack, _Classic(x));
    if (mProbing)
      _Probe(x, y);
    if (mLifeTracking)
    {
      // Each power tube's plate dissipation, summed for the block (see _UpdateTubeLife).
      mDissAcc[0] += mPower.Dissipation1();
      mDissAcc[1] += mPower.Dissipation2();
      mDissN++;
    }
    if (mUiWanted && mParams.stage2)
      mLevelAcc[1] = std::max(mLevelAcc[1], std::fabs(mCircuit2.PlateSwing()) * 0.01);
    if (mUiWanted && mSocketMask != 0 && mParams.circuit)
      for (int k = 0; k < kNumSockets; k++)
        if (mSocketMask & (1 << k))
          mLevelAcc[4 + k] = std::max(mLevelAcc[4 + k], std::fabs(mSocket[k].PlateSwing()) * 0.01);
    if (mFadeRemaining <= 0)
      return y;
    // Mid-crossfade after a switch flip: blend in from whatever was playing before.
    const double old = mFadeFrom == FadeFrom::Classic
                         ? _Stack(mFadeStack, mFadeStackType, _Classic(x))
                         : _Circuit(mFadeCircuit, mFadeCircuit2, mFadeStage2, mFadeSocket, mFadeSocketMask, mFadeStack,
                                    mFadeStackType, mFadePower, mFadePowerOn, x);
    const double g = static_cast<double>(mFadeRemaining--) / mFadeLength;
    return y + g * (old - y);
  }

  // Circuit engine: the whole stage is simulated, so bias (via Rk), blocking, the cathode bypass
  // cap, Miller roll-off and sag all come out of the circuit itself. The tone stack sits between
  // the preamp and the power amp, as in a real amp.
  double _Circuit(TriodeStageCircuit& stage1, TriodeStageCircuit& stage2, bool useStage2, TriodeStageCircuit* sockets,
                  int socketMask, tonestack::TMBStack& stack, int stackType, PushPullPowerAmp& power, bool usePower,
                  double x)
  {
    // (1.64.0: with the capture as the preamp, x is already its output.)
    const double pre =
      _Stack(stack, stackType, mParams.capture ? x : _Preamp(stage1, stage2, useStage2, sockets, socketMask, x));
    mLastPre = pre;
    const double mains = mBenchMains; // Bench: the Variac moves the preamp's supply too
    if (!usePower)
    {
      if (stage1.SupplyShared() || mains != 1.0)
        stage1.SetSupplyTarget(mains); // Power amp switched out: the preamp's supply settles back
      return pre;
    }
    const double out = power.Process(pre);
    // Shared supply: the preamp hangs off the power amp's screen node (as in most amps: B+, then the
    // screens, then the phase inverter and preamp nodes), so its rail follows that node's sag.
    if (mParams.sharedSupply || stage1.SupplyShared() || mains != 1.0)
      stage1.SetSupplyTarget(
        (mParams.sharedSupply
           ? PushPullPowerAmp::SharedSupplyScale(
               power.ScreenVoltage() / (power.RestingScreenVoltage() * power.BenchMains()), power.Strength())
           : 1.0)
        * mains);
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
  double _Preamp(TriodeStageCircuit& stage1, TriodeStageCircuit& stage2, bool useStage2, TriodeStageCircuit* sockets,
                 int socketMask, double x)
  {
    const double y1 = stage1.Process(x * mSmoothed.drive);
    if (!useStage2 && socketMask == 0)
      return y1 / std::sqrt(mSmoothed.drive);
    // Stage 2 and the gain sockets (1.68.0), in a cascade: each shares stage 1's B+ (and reports its
    // current draw back as extra supply load), and is driven by the stage before it -- its plate
    // swing, or its follower's cathode -- through its own gain pot. Every stage inverts; Process
    // re-inverts its own stage, so flip once more for each stage driving the next.
    const double rail = stage1.RailVoltage();
    const TriodeStageCircuit* last = &stage1;
    double y = y1, extra = 0.0;
    bool inverted = false;
    if (useStage2)
    {
      stage2.SetRail(rail);
      y = stage2.Process(mSmoothed.gain2 * last->DriveSwing());
      extra = stage2.SupplyCurrent();
      last = &stage2;
      inverted = true;
    }
    for (int k = 0; k < kNumSockets; k++)
      if (socketMask & (1 << k))
      {
        sockets[k].SetRail(rail);
        y = sockets[k].Process(mSmoothed.socketGain[k] * last->DriveSwing());
        extra += sockets[k].SupplyCurrent();
        last = &sockets[k];
        inverted = !inverted;
      }
    stage1.SetExtraLoad(extra);
    return (inverted ? -y : y) / std::sqrt(mSmoothed.drive);
  }

  // 1.68.0: the sockets' circuits for p (see SocketParams): each gain socket is set up after the stage
  // driving it (its source resistance is that stage's output). From rest when there's no `old` (a
  // reset), or one starts working or takes another tube; otherwise its parts change like pots.
  static constexpr double kFollowerOutOhms = 1e3; // A cathode follower's output: about 1 / gm
  TriodeStageCircuit& _GainStage(int i) { return i == 0 ? mCircuit : i == 1 ? mCircuit2 : mSocket[i - 2]; }
  void _SetSockets(const TubeParams& p, const TubeParams* old)
  {
    int mask = 0;
    for (int k = 0; k < kNumSockets; k++)
    {
      if (!p.socket[k].IsGain())
        continue;
      mask |= 1 << k;
      const int prev = p.GainBefore(k);
      const double source = p.Followed(prev) ? kFollowerOutOhms : _GainStage(prev).OutputResistance();
      const CircuitParams c = p.ToSocket(k, source);
      if (!old || !old->socket[k].IsGain() || old->socket[k].tube != p.socket[k].tube)
        mSocket[k].Reset(mOversampledRate, c);
      else
        mSocket[k].SetParams(c);
    }
    mSocketMask = mask;
  }
  // What stage 1's supply carries at rest besides stage 1: stage 2 and the gain sockets.
  double _RestingExtraLoad(const TubeParams& p) const
  {
    double amps = p.stage2 ? mCircuit2.RestingPlateCurrent() : 0.0;
    for (int k = 0; k < kNumSockets; k++)
      if (mSocketMask & (1 << k))
        amps += mSocket[k].RestingPlateCurrent();
    return amps;
  }
  // A socket change the sound should crossfade over (rather than step): one starting or stopping, a
  // new job or tube, a cathode bypass cap in or out.
  bool _SocketsFlip(const TubeParams& p) const
  {
    for (int k = 0; k < kNumSockets; k++)
    {
      const SocketParams &a = p.socket[k], &b = mParams.socket[k];
      if (a.role != b.role || (a.role != kSocketOff && a.tube != b.tube)
          || (a.role == kSocketGain && (a.cathodeCapUF > 0.0) != (b.cathodeCapUF > 0.0)))
        return true;
    }
    return false;
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
      st.paCurrentSum[0] += i1;
      st.paCurrentSum[1] += i2;
      st.paDissSum[0] += mPower.Dissipation1();
      st.paDissSum[1] += mPower.Dissipation2();
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
    for (int k = 0; k < kNumSockets; k++)
      if (mSocketMask & (1 << k))
        f[kProbeSocketPlate0 + k] = static_cast<float>(mSocket[k].PlateVoltage());
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
    // Bench.
    s.mainsV = mBenchMains * bench::kMainsNominal;
    s.mainsTargetV = _BenchMainsTarget() * bench::kMainsNominal;
    s.heaterV = mBenchHeat * bench::kHeaterNominal;
    s.cathodeK = bench::CathodeK(mBenchHeat);
    s.reserve[0] = bench::Reserve(mBenchHeat, mBenchAge[0]);
    s.reserve[1] = bench::Reserve(mBenchHeat, mBenchAge[1]);
    s.perveance[0] = bench::Perveance(mBenchAge[0]);
    s.perveance[1] = bench::Perveance(mBenchAge[1]);
    s.paBiasNow = mPower.BiasSupply();
    s.paCathodeRest = mPower.RestingCathodeVoltage();
    s.paRatedW = mPower.RatedWatts();
    s.paBiasTrim = mParams.powerBias;
    s.cathodeBias = mParams.cathodeBias;
    s.sixL6 = mParams.sixL6;
  }

public:
  // Power tube life (1.62.0) ========================================================================
  // Each power tube's life (1 new .. 0 failed), whether it's failed, and its plate dissipation as a
  // share of its rating (smoothed, ~0.3 s). Read by the plugin after Process, on the audio thread.
  // 1.68.0: four sockets (V9..V12); a pair uses the first two.
  static constexpr int kNumPowerTubes = 4;
  double PowerLife(int k) const { return mPowerLife[k]; }
  bool PowerDead(int k) const { return mPowerDead[k]; }
  double DissipationRatio(int k) const { return mDissRatio[k]; }
  // A new tube in socket k (it warms up over a couple of seconds).
  void ReplacePowerTube(int k)
  {
    mPowerLife[k] = 1.0;
    if (mPowerDead[k])
    {
      mPowerDead[k] = false;
      mPower.SetTubeDead(k, false);
      mFadePower.SetTubeDead(k, false);
      mFailGain = 0.0; // Warming up: fades back in (see _FailEffects)
    }
  }
  // The other channel's stage follows this one's tubes (only channel 0 wears them).
  void SyncTubesFrom(const TubeStage& o)
  {
    for (int k = 0; k < kNumPowerTubes; k++)
    {
      mPowerLife[k] = o.mPowerLife[k];
      mDissRatio[k] = o.mDissRatio[k];
      if (o.mPowerDead[k] && !mPowerDead[k])
        _FailPowerTube(k);
      else if (!o.mPowerDead[k] && mPowerDead[k])
        ReplacePowerTube(k);
    }
  }
  void SetWearMaster(bool master) { mWearMaster = master; }
  // The chassis page wants the glow levels (and the dissipation readings).
  void SetUiWanted(bool wanted) { mUiWanted = wanted; }
  // Each preamp tube's signal level last block (V1 grid volts, V2 plate swing / 100 V, V3/V4 the
  // preamp's output in full scale, V5, V6 (the sockets, 1.68.0) plate swing / 100 V), for the chassis
  // page's glow.
  static constexpr int kNumStageLevels = 4 + kNumSockets;
  double StageLevel(int i) const { return mLevel[std::clamp(i, 0, kNumStageLevels - 1)]; }
  double StageRk(int i) const { return i == 0 ? mCircuit.Rk() : mCircuit2.Rk(); }
  // 1.68.0: a gain socket's cathode resistor (what its Bias picked), 0 if it isn't a gain stage.
  double SocketRk(int k) const { return (mSocketMask & (1 << k)) ? mSocket[k].Rk() : 0.0; }

private:
  // Red plating wears a power tube out: per second, 0.02 x (excess / 10%)^2 of its life, where excess
  // is how far its average plate dissipation is over its rating -- at 110% about a minute, at 130% a
  // few seconds -- and up to 2.5x faster with the heaters over-run by the Variac (135 V). Under its
  // rating a tube doesn't wear at all. Accelerated a lot (a real tube takes minutes to hours of red
  // plating), so you can hear it happen. A failed tube goes open (see PowerAmp::SetTubeDead).
  void _UpdateTubeLife(size_t numFrames)
  {
    // Measured while tubes can fail or the chassis page shows the readings; worn only if they can fail.
    const bool tracking = (mParams.canFail || mUiWanted) && mParams.circuit && mParams.powerAmp;
    if (!tracking && !mLifeTracking)
      return;
    mLifeTracking = tracking;
    if (mDissN > 0)
    {
      const double rated = std::max(1.0, mPower.RatedWatts());
      const double dt = static_cast<double>(numFrames) / mSampleRate;
      const double coef = 1.0 - std::exp(-dt / 0.3);
      // 1.68.0: tube k is on side k % 2 (a quad's V11, V12 share V9's and V10's sides); each wears at its
      // own rate (no two tubes are alike: the quad's second pair a little faster and slower).
      const int tubes = mParams.quad ? kNumPowerTubes : 2;
      for (int k = 0; k < tubes; k++)
      {
        const double ratio = mPowerDead[k] ? 0.0 : mDissAcc[k % 2] / mDissN / rated;
        mDissRatio[k] += coef * (ratio - mDissRatio[k]);
        if (!mWearMaster || mPowerDead[k] || !tracking || !mParams.canFail)
          continue;
        const double excess = mDissRatio[k] - 1.0;
        if (excess <= 0.0)
          continue;
        const double heat = 1.0 + 12.0 * std::max(0.0, mBenchHeat - 1.0);
        static constexpr double kWearSpread[kNumPowerTubes] = {1.0, 1.0, 1.15, 0.88};
        mPowerLife[k] -= dt * 0.02 * (excess / 0.1) * (excess / 0.1) * heat * kWearSpread[k];
        if (mPowerLife[k] <= 0.0)
        {
          mPowerLife[k] = 0.0;
          _FailPowerTube(k);
        }
      }
      mDissAcc[0] = mDissAcc[1] = 0.0;
      mDissN = 0;
    }
  }

  void _FailPowerTube(int k)
  {
    mPowerDead[k] = true;
    mPowerLife[k] = 0.0;
    mPower.SetTubeDead(k, true);
    mFadePower.SetTubeDead(k, true);
    mPopEnv = 1.0; // The bang as it goes (see _FailEffects)
  }

  // The failure's pop (a sharp crack, then a burst of noise dying away in ~30 ms) and the warm-up
  // after a replacement (the output fading back in over ~2 s).
  double _FailEffects(double wet)
  {
    if (mPopEnv > 0.0)
    {
      mPopRng = mPopRng * 1664525u + 1013904223u;
      const double noise = (static_cast<double>(mPopRng >> 8) / 8388608.0) - 1.0;
      wet += mPopEnv * (0.6 * noise + (mPopEnv > 0.999 ? 0.8 : 0.0));
      mPopEnv *= mPopDecay;
      if (mPopEnv < 1e-4)
        mPopEnv = 0.0;
    }
    if (mFailGain != 1.0)
    {
      mFailGain = std::min(1.0, mFailGain + mWarmStep);
      wet *= mFailGain * mFailGain * (3.0 - 2.0 * mFailGain);
    }
    return wet;
  }

  // Bench (TubeBench.h) =============================================================================
  // The Variac's setting as a ratio of 120 V.
  double _BenchMainsTarget() const { return std::clamp(mParams.mainsV, 60.0, 160.0) / bench::kMainsNominal; }

  // Per host sample: the supplies follow the Variac within a fraction of a second, the cathodes'
  // temperature over seconds, and the age knobs glide (a jump clicked); the circuits get the new
  // state every kBenchUpdateSamples while anything moves. Settled at stock, it's skipped entirely.
  void _BenchGlide()
  {
    const double target = _BenchMainsTarget();
    const double ages[2] = {mParams.preampAge, mParams.powerAge};
    auto glide = [](double& v, double to, double coef, double snap) {
      v += coef * (to - v);
      if (std::fabs(v - to) < snap)
        v = to;
    };
    const bool settled =
      mBenchMains == target && mBenchHeat == target && mBenchAge[0] == ages[0] && mBenchAge[1] == ages[1];
    if (settled && !mBenchDirty)
      return;
    if (!settled)
    {
      glide(mBenchMains, target, mBenchSupplyCoef, 1e-7);
      glide(mBenchHeat, target, mBenchHeatCoef, 1e-6);
      glide(mBenchAge[0], ages[0], mBenchAgeCoef, 1e-6);
      glide(mBenchAge[1], ages[1], mBenchAgeCoef, 1e-6);
    }
    const bool nowSettled =
      mBenchMains == target && mBenchHeat == target && mBenchAge[0] == ages[0] && mBenchAge[1] == ages[1];
    if (--mBenchCount > 0 && !mBenchDirty && !nowSettled)
      return;
    mBenchCount = kBenchUpdateSamples;
    _ApplyBench();
  }

  // The bench state for each section (stock: exactly none).
  void _ApplyBench()
  {
    mBenchDirty = false;
    auto stateFor = [this](double age, double peakAmps) {
      bench::State s;
      s.mains = mBenchMains;
      if (mBenchHeat == 1.0 && age <= 0.0)
        return s;
      s.perveance = bench::Perveance(age);
      s.invCeiling = 1.0 / (bench::Reserve(mBenchHeat, age) * peakAmps);
      return s;
    };
    const bench::State pre = stateFor(mBenchAge[0], kPreampPeakAmps);
    const bench::State power = stateFor(mBenchAge[1], kPowerPeakAmps);
    mCircuit.SetBench(pre);
    mCircuit2.SetBench(pre);
    mPower.SetBench(power);
    mFadeCircuit.SetBench(pre);
    mFadeCircuit2.SetBench(pre);
    mFadePower.SetBench(power);
    for (int k = 0; k < kNumSockets; k++)
    {
      mSocket[k].SetBench(pre);
      mFadeSocket[k].SetBench(pre);
    }
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
    if (mEconomyStage == 0
        && OversampleFor(mSampleRate, mParams.economy, mParams.highQuality, mParams.Hot()) != mOversample)
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
  TriodeStageCircuit mSocket[kNumSockets]; // 1.68.0: the chassis sockets (gain ones in mSocketMask)
  int mSocketMask = 0;
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
  TriodeStageCircuit mFadeSocket[kNumSockets];
  int mFadeSocketMask = 0;
  PushPullPowerAmp mFadePower;
  bool mFadePowerOn = false;
  tonestack::TMBStack mFadeStack;
  int mFadeStackType = 0;
  int mFadeLength = 1, mFadeRemaining = 0;

  std::vector<iplug::sample> mBuffer;
  iplug::sample* mBufPtr[1] = {nullptr};

  // Bench (see _BenchGlide): the supplies' and the cathodes' share of the Variac (ratios of 120 V),
  // their glides, and the most current each section's circuit asks of its tubes (what the emission
  // reserve is measured against: a 12AX7 on a 100k plate load peaks under 2 mA, B+ / Ra being 2.5 mA,
  // and the power tubes here peak around 0.1 A with a hard strum; measured with the debug probes).
  static constexpr int kBenchUpdateSamples = 16;
  static constexpr double kPreampPeakAmps = 2e-3, kPowerPeakAmps = 0.12;
  static constexpr double kBenchAgeGlideS = 0.15;
  double mBenchMains = 1.0, mBenchHeat = 1.0, mBenchSupplyCoef = 1.0, mBenchHeatCoef = 1.0;
  double mBenchAge[2] = {0.0, 0.0}, mBenchAgeCoef = 1.0; // Preamp, power tubes (gliding)
  int mBenchCount = 0;
  bool mBenchDirty = false;

  // Power tube life (see _UpdateTubeLife) and the chassis page's glow levels.
  double mPowerLife[kNumPowerTubes] = {1.0, 1.0, 1.0, 1.0};
  bool mPowerDead[kNumPowerTubes] = {false, false, false, false};
  double mDissAcc[2] = {0.0, 0.0}, mDissRatio[kNumPowerTubes] = {}; // (Accumulated per side, the ratio per tube)
  int mDissN = 0;
  bool mLifeTracking = false, mWearMaster = true, mUiWanted = false;
  double mPopEnv = 0.0, mPopDecay = 0.999, mFailGain = 1.0, mWarmStep = 1e-5;
  uint32_t mPopRng = 22222u;
  double mLevel[kNumStageLevels] = {}, mLevelAcc[kNumStageLevels] = {};

  // Debug probes (see SetProbe). Not owned.
  TubeProbe* mProbe = nullptr;
  bool mProbing = false, mProbeFramePhase = true;
  double mBlockLP[2] = {0.0, 0.0}, mBlockCoef = 0.0; // Coupling caps' slow charge (blocking)
  double mLastPre = 0.0; // Preamp (after the tone stack) output of the last oversampled sample
};
} // namespace tube
