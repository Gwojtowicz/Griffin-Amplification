#pragma once

// Circuit-level model of one 12AX7 common-cathode gain stage, solved per sample.
//
// Instead of a static tube curve with dynamics bolted on, the whole stage is simulated as a
// circuit, so its feel comes out of the parts interacting the way they do in an amp:
//
//   Vin --Cin--+--Rstop--G          B+ (V_rail) --Ra-- P --Cc--+-- out
//              |         | \          ^                |       |
//             Rgl       Cgk  Cgp     supply R + reservoir     Rl (next stage's grid leak)
//              |         |     \     (sag)              |       |
//             gnd        K------+-- triode ------------+      gnd
//                        |
//                    Rk || Ck (cathode bias + bypass cap)
//                        |
//                       gnd
//
// * Tube: the Dempwolf & Zoelzer 12AX7 model (DAFx 2011), fitted to measured tubes, including
//   grid current -- which is what makes grid clamping and "blocking" (Cin charging on hard hits,
//   then recovering through Rgl) happen on their own.
// * Cathode Rk || Ck: gain depends on frequency below the bypass cap's corner, so a small cap
//   tightens the bass and a big one is full; with no cap the stage has local feedback (less gain,
//   cleaner, tighter).
// * Cgk/Cgp with the grid stopper: the Miller roll-off.
// * B+ fed through a supply resistance from a reservoir: sag.
// * Shared supply (1.33.0, a Real Feel switch): in a real amp the preamp's B+ is the far end of the
//   power amp's supply, through decoupling RC filters, so hard power-amp hits sag the preamp too.
//   TubeStage feeds the power amp's screen-node voltage (relative to rest) into SetSupplyTarget.
// * Bench (1.58.0, TubeBench.h): the Variac reaches the stage through SetSupplyTarget too; tube age
//   and the heaters scale its perveance and put a soft emission ceiling on its plate current
//   (SetBench). Rk stays the part the Bias knob picked, so a worn tube finds its own new bias.
//
// Solved with modified nodal analysis: capacitors as trapezoidal companion models, the triode as
// a nonlinear 3-terminal element, Newton-Raphson on the 5 unknown node voltages each sample,
// warm-started from the previous sample's solution. The supply node moves slowly (time constant
// of tenths of a second) so it's updated explicitly after each solve.

#include <algorithm>
#include <cmath>

#include "TubeBench.h"

namespace tube
{
// Dempwolf & Zoelzer, "A Physically-motivated Triode Model for Circuit Simulations" (DAFx 2011),
// 12AX7 parameters.
struct Dempwolf12AX7
{
  static constexpr double G = 2.242e-3;
  static constexpr double C = 3.40;
  static constexpr double mu = 103.2;
  static constexpr double gamma = 1.26;
  static constexpr double Gg = 6.177e-4;
  static constexpr double Cg = 9.901;
  static constexpr double xi = 1.314;
  static constexpr double Ig0 = 8.025e-8;
};

// 1.68.0: the triodes the chassis's extra sockets take. Dempwolf & Zoelzer fitted only the 12AX7, so
// the others keep its curve shape (C, gamma) and grid diode, with their own amplification factor mu
// and G set so the plate current matches the datasheet at one typical operating point (Va, Vg, Ia).
struct TriodeModel
{
  double G, C, mu, gamma, Gg, Cg, xi, Ig0;
};

enum TubeType
{
  k12AX7 = 0,
  k12AT7, // ECC81: mu 60, ~4x the 12AX7's current -- hotter, less gain, more headroom
  k12AU7, // ECC82: mu 17, low gain, a lot of headroom: the clean-boost / driver tube
  k5751, // mu 70: a 12AX7 with ~30% less gain, smoother breakup
  kNumTubeTypes
};

inline TriodeModel Model12AX7()
{
  using T = Dempwolf12AX7;
  return {T::G, T::C, T::mu, T::gamma, T::Gg, T::Cg, T::xi, T::Ig0};
}

// The 12AX7 model with another mu, and G fitted to Ia at (Va, Vg).
inline TriodeModel FitTriode(double mu, double va, double vg, double ia)
{
  TriodeModel m = Model12AX7();
  m.mu = mu;
  const double x = m.C * (va / mu + vg);
  const double s = (x > 30.0 ? x : std::log1p(std::exp(x))) / m.C;
  m.G = ia / std::pow(s, m.gamma);
  return m;
}

inline const TriodeModel& GetTriodeModel(int type)
{
  static const TriodeModel models[kNumTubeTypes] = {
    Model12AX7(),
    FitTriode(60.0, 250.0, -2.0, 10e-3), // RCA 12AT7: 250 V, -2 V, 10 mA
    FitTriode(17.0, 250.0, -8.5, 10.5e-3), // RCA 12AU7: 250 V, -8.5 V, 10.5 mA
    FitTriode(70.0, 250.0, -3.0, 1.0e-3)}; // GE 5751: 250 V, -3 V, 1.0 mA
  return models[std::clamp(type, 0, kNumTubeTypes - 1)];
}

struct TriodeCurrents
{
  double ik = 0.0; // Cathode current (plate + grid)
  double ig = 0.0; // Grid current
  double dikDvgk = 0.0, dikDvpk = 0.0, digDvgk = 0.0;
};

// Bench (TubeBench.h): a worn or under-heated tube's plate current -- perveance times the healthy
// one, under the emission ceiling. The grid current is left as it is.
inline void ApplyBench(TriodeCurrents& t, const bench::State& s)
{
  const double ip = s.perveance * (t.ik - t.ig);
  double slope = 1.0;
  const double limited = s.invCeiling > 0.0 ? bench::Ceiling(ip, s.invCeiling, slope) : ip;
  const double k = s.perveance * slope;
  t.ik = limited + t.ig;
  t.dikDvgk = k * (t.dikDvgk - t.digDvgk) + t.digDvgk;
  t.dikDvpk *= k;
}

inline double Softplus(double x) { return x > 30.0 ? x : std::log1p(std::exp(x)); }
inline double Sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

// (ln(1 + e^x) / c)^p for x = c * u, and its derivative with respect to u (= p (.)^(p-1) * the
// logistic of x; the 1/c and dx/du = c cancel). Shares the transcendental calls: one exp for both
// the softplus and the logistic, and one log + exp for the power.
inline void SoftplusPow(double x, double c, double p, double& value, double& deriv)
{
  double sp, sig;
  if (x > 30.0)
  {
    sp = x;
    sig = 1.0;
  }
  else
  {
    const double e = std::exp(x);
    sp = std::log1p(e);
    sig = e / (1.0 + e);
  }
  const double s = sp / c;
  if (s <= 0.0)
  {
    value = deriv = 0.0;
    return;
  }
  const double sPowM1 = std::exp((p - 1.0) * std::log(s));
  value = sPowM1 * s;
  deriv = p * sPowM1 * sig;
}

// Ik = G * (ln(1 + e^(C (Vpk/mu + Vgk))) / C)^gamma, Ig = Gg * (ln(1 + e^(Cg Vgk)) / Cg)^xi + Ig0.
// Exact (slow) version; Triode() below reads the same functions from lookup tables.
inline TriodeCurrents TriodeExact(const TriodeModel& T, double vgk, double vpk)
{
  TriodeCurrents r;
  double v, d;
  SoftplusPow(T.C * (vpk / T.mu + vgk), T.C, T.gamma, v, d);
  r.ik = T.G * v;
  r.dikDvgk = T.G * d;
  r.dikDvpk = r.dikDvgk / T.mu;
  const double xg = T.Cg * vgk;
  if (xg < -25.0) // Grid far below conduction: (e^xg / Cg)^xi is < 1e-14 A; only Ig0 is left.
  {
    r.ig = T.Ig0;
    r.digDvgk = 0.0;
  }
  else
  {
    SoftplusPow(xg, T.Cg, T.xi, v, d);
    r.ig = T.Gg * v + T.Ig0;
    r.digDvgk = T.Gg * d;
  }
  return r;
}
inline TriodeCurrents TriodeExact(double vgk, double vpk)
{
  using T = Dempwolf12AX7;
  static constexpr TriodeModel kModel = {T::G, T::C, T::mu, T::gamma, T::Gg, T::Cg, T::xi, T::Ig0};
  return TriodeExact(kModel, vgk, vpk);
}

// Lookup tables for the triode. In the Dempwolf model the cathode current depends on a single
// combined voltage u = Vpk/mu + Vgk, and the grid current on Vgk alone, so both are 1-D functions:
// tabulated once (value and derivative), read back with linear interpolation. This keeps the full
// per-sample circuit solve but takes the transcendental math out of the inner Newton loop.
class TriodeTables
{
public:
  static const TriodeTables& Get() { return For(k12AX7); }
  // 1.68.0: one set per tube type (TubeType), each built the first time it's asked for.
  static const TriodeTables& For(int type)
  {
    switch (std::clamp(type, 0, kNumTubeTypes - 1))
    {
      // Built once, thread-safe (C++11 static init), then read-only. Heap-allocated: 512 kB each.
      case k12AT7: { static const TriodeTables* t = new TriodeTables(GetTriodeModel(k12AT7)); return *t; }
      case k12AU7: { static const TriodeTables* t = new TriodeTables(GetTriodeModel(k12AU7)); return *t; }
      case k5751: { static const TriodeTables* t = new TriodeTables(GetTriodeModel(k5751)); return *t; }
      default: { static const TriodeTables* t = new TriodeTables(GetTriodeModel(k12AX7)); return *t; }
    }
  }

  const TriodeModel& Model() const { return mModel; }
  double InvMu() const { return mInvMu; }

  // Returns false outside the tabulated range (the caller then uses the exact model).
  bool Cathode(double u, double& ik, double& dik) const { return _Read(mIk, mDik, kUMin, kUMax, u, ik, dik); }
  bool Grid(double vgk, double& ig, double& dig) const
  {
    if (vgk < kGMin)
    {
      ig = mModel.Ig0;
      dig = 0.0;
      return true;
    }
    return _Read(mIg, mDig, kGMin, kGMax, vgk, ig, dig);
  }

private:
  static constexpr int kSize = 16384;
  static constexpr double kUMin = -8.0, kUMax = 8.0; // Below kUMin, Ik < 1e-17 A
  static constexpr double kGMin = -3.0, kGMax = 6.0; // Below kGMin, Ig is Ig0 to 1e-15 A

  explicit TriodeTables(const TriodeModel& T)
  : mModel(T)
  , mInvMu(1.0 / T.mu) // Multiplied, not divided, in the Newton loop
  {
    for (int i = 0; i < kSize; i++)
    {
      const double u = kUMin + (kUMax - kUMin) * i / (kSize - 1);
      SoftplusPow(T.C * u, T.C, T.gamma, mIk[i], mDik[i]);
      mIk[i] *= T.G;
      mDik[i] *= T.G;
      const double vg = kGMin + (kGMax - kGMin) * i / (kSize - 1);
      SoftplusPow(T.Cg * vg, T.Cg, T.xi, mIg[i], mDig[i]);
      mIg[i] = mIg[i] * T.Gg + T.Ig0;
      mDig[i] *= T.Gg;
    }
  }

  static bool _Read(const double* v, const double* d, double lo, double hi, double x, double& out, double& dout)
  {
    if (x < lo)
    {
      out = dout = 0.0;
      return true;
    }
    const double pos = (x - lo) * ((kSize - 1) / (hi - lo)); // (Constant once inlined: no division)
    if (pos >= kSize - 1)
      return false;
    const int i = static_cast<int>(pos);
    const double f = pos - i;
    out = v[i] + f * (v[i + 1] - v[i]);
    dout = d[i] + f * (d[i + 1] - d[i]);
    return true;
  }

  TriodeModel mModel;
  double mInvMu;
  double mIk[kSize], mDik[kSize], mIg[kSize], mDig[kSize];
};

inline TriodeCurrents Triode(const TriodeTables& t, double vgk, double vpk)
{
  TriodeCurrents r;
  double ik, dik, ig, dig;
  const double invMu = t.InvMu();
  if (!t.Cathode(vpk * invMu + vgk, ik, dik) || !t.Grid(vgk, ig, dig))
    return TriodeExact(t.Model(), vgk, vpk);
  r.ik = ik;
  r.dikDvgk = dik;
  r.dikDvpk = dik * invMu;
  r.ig = ig;
  r.digDvgk = dig;
  return r;
}

inline TriodeCurrents Triode(double vgk, double vpk) { return Triode(TriodeTables::Get(), vgk, vpk); }

// Cathode follower (1.45.0): the Marshall / 5150 stage between the last gain stage and the tone
// stack. A 12AX7 with its plate on a higher supply node than the gain stages (as in those amps) and a
// cathode resistor to ground, its grid wired straight (DC) to the gain stage's plate through a 100k
// stopper, its cathode driving the tone stack.
//
//   gain stage plate P --Rs-- G      B+ (kSupply)
//                              \      |
//                               triode
//                                     |
//                                     K ---- out, into the tone stack (kAcLoad, for signals)
//                                     |
//                                    Rk
//                                     |
//                                    gnd
//
// Two ways it clips, both lopsided, which is the "Marshall" crunch it's known for:
// * Cutoff, on big downward swings. A follower can only pull its cathode down as fast as Rk and its
//   load let the current fall to zero. The tone stack is a heavy AC load (its slope resistor and caps,
//   ~100k at mid frequencies), so when the plate swings down by more than about a third of the
//   cathode's voltage (~70 V) the tube cuts off and the bottom of the wave flattens.
// * Grid current, at the very top: when the gain stage cuts off and its plate rises toward its
//   supply, the follower can't keep its cathode that close under its own; its grid conducts and pulls
//   on the gain stage's plate through the stopper, rounding it over.
// The stack's load is taken as a resistance for the signal (its swing from the cathode's resting
// voltage), not its full frequency-dependent impedance.
//
// No memory of its own (no caps), so it's solved each sample for the gain stage's plate voltage:
// Newton on the grid and cathode voltages, warm-started from the last. The grid current it draws is a
// load on the plate node in the gain stage's own solve (with its slope, for Newton), so the coupling
// is implicit.
class CathodeFollower
{
public:
  // Marshalls use 100k with their gain stages' plates resting near 180 V; here they rest higher
  // (~215 V on the 250 V rail), and a 12AX7 with 100k would need its grid above its cathode just to
  // carry the resting current -- it would sit in grid conduction, a lossy divider. 220k on 350 V rests
  // it 0.85 V under its grid at ~1 mA, as theirs do. The stack's load: ~100k at the guitar's mids.
  static constexpr double kRk = 220e3, kRs = 100e3, kAcLoad = 100e3, kSupply = 350.0;

  // 1.68.0: the tube in it (a chassis socket can hold another type); Configure after a change.
  void SetTubeType(int type) { mTables = &TriodeTables::For(type); }

  // The resting point for the gain stage's resting plate voltage (the stack carries no current at
  // rest). Moves the state there too unless keepState (a knob being turned: the follower carries on
  // from where it is, only its reference moves).
  void Configure(double restPlateV, bool keepState)
  {
    double vg = restPlateV, vk = restPlateV + 1.5, j[4];
    for (int it = 0; it < 100; it++)
      if (_Step(restPlateV, vg, vk, 0.0, j) < 1e-12)
        break;
    mRestK = vk;
    if (!keepState)
    {
      mVg = vg;
      mVk = vk;
    }
  }

  // The grid current drawn from the plate at vp volts, and its slope; leaves the follower there.
  void Load(double vp, double& ig, double& dig)
  {
    // Newton converges quadratically: once a step is under 1 uV, what's left is ~1e-12 V. (Warm
    // started from the last solve, that's usually the first step.)
    double j[4];
    for (int it = 0; it < 20; it++)
      if (_Step(vp, mVg, mVk, 1.0 / kAcLoad, j) < 1e-6)
        break;
    // Slopes by implicit differentiation, J (dVk, dVg)/dVp = (0, 1), with the last step's Jacobian;
    // Ig = (Vp - Vg) / Rs.
    const double det = j[0] * j[3] - j[1] * j[2];
    const double dvg = j[0] / det;
    ig = (vp - mVg) * (1.0 / kRs);
    dig = (1.0 - dvg) * (1.0 / kRs);
  }

  // The cathode's swing from rest (the signal into the tone stack).
  double Output() const { return mVk - mRestK; }

private:
  // One Newton step on f1 = Vk / Rk + (Vk - rest) gl - Ik(Vg - Vk, B+ - Vk) (the cathode node: gl is
  // the stack's load, 0 at rest) and f2 = Vg - Vp + Rs Ig(Vg - Vk) (the stopper). Returns the larger
  // move (volts) and the Jacobian j = {f1 by Vk, f1 by Vg, f2 by Vk, f2 by Vg}.
  double _Step(double vp, double& vg, double& vk, double gl, double (&j)[4]) const
  {
    const TriodeCurrents t = Triode(*mTables, vg - vk, kSupply - vk);
    const double f1 = vk * (1.0 / kRk) + (vk - mRestK) * gl - t.ik;
    const double f2 = vg - vp + kRs * t.ig;
    const double j11 = 1.0 / kRk + gl + t.dikDvgk + t.dikDvpk, j12 = -t.dikDvgk;
    const double j21 = -kRs * t.digDvgk, j22 = 1.0 + kRs * t.digDvgk;
    j[0] = j11;
    j[1] = j12;
    j[2] = j21;
    j[3] = j22;
    const double det = j11 * j22 - j12 * j21;
    const double dk = std::clamp((f1 * j22 - j12 * f2) / det, -50.0, 50.0);
    const double dg = std::clamp((j11 * f2 - j21 * f1) / det, -50.0, 50.0);
    vk -= dk;
    vg -= dg;
    return std::max(std::fabs(dk), std::fabs(dg));
  }

  double mVg = 0.0, mVk = 0.0, mRestK = 0.0;
  const TriodeTables* mTables = &TriodeTables::Get();
};

struct CircuitParams
{
  double biasV = -1.5; // Resting grid-to-cathode voltage; Rk is chosen to give it
  double cinFarads = 22e-9; // Input coupling cap ("Bloom": how long blocking lasts)
  bool cathodeCap = true; // Cathode bypass cap fitted
  double ckFarads = 22e-6;
  double raOhms = 100e3; // Plate load
  bool sag = false;
  double rsOhms = 47e3; // Supply resistance feeding the plate node
  double sagTimeS = 0.5; // Supply R x reservoir C
  // Output resistance of whatever drives the input (0 = an ideal source, e.g. the guitar input
  // after Drive; a cascaded stage 2 sees stage 1's plate, Ra || rp). It sets how fast the input
  // coupling cap charges when the grid conducts -- i.e. interstage blocking.
  double sourceOhms = 0.0;
  // Cap across the plate resistor (0 = none). Real high-gain preamps fit these (often 470 pF on a
  // later stage) to roll off the fizz above a few kHz; with Ra || rp || load ~ 38k, 470 pF is a
  // corner around 9 kHz.
  double plateCapFarads = 0.0;
  // A cathode follower on the plate (1.45.0, see CathodeFollower): the stage's output is then
  // the follower's cathode instead of the coupling cap.
  bool cathodeFollower = false;
  // 1.68.0: the tube types (TubeType) of the stage and of its follower (the chassis sockets).
  int tubeType = k12AX7;
  int followerTube = k12AX7;

  bool operator==(const CircuitParams& o) const
  {
    return biasV == o.biasV && cinFarads == o.cinFarads && cathodeCap == o.cathodeCap && ckFarads == o.ckFarads
           && raOhms == o.raOhms && sag == o.sag && rsOhms == o.rsOhms && sagTimeS == o.sagTimeS
           && sourceOhms == o.sourceOhms && plateCapFarads == o.plateCapFarads
           && cathodeFollower == o.cathodeFollower && tubeType == o.tubeType && followerTube == o.followerTube;
  }
  bool operator!=(const CircuitParams& o) const { return !(*this == o); }
};

class TriodeStageCircuit
{
public:
  static constexpr double kBPlus = 250.0; // B+ at the plate node at rest
  static constexpr double kRgl = 1.0e6; // Grid leak
  static constexpr double kRstop = 68.0e3; // Grid stopper
  static constexpr double kCgk = 1.6e-12;
  static constexpr double kCgp = 1.7e-12;
  static constexpr double kCc = 22.0e-9; // Output coupling cap
  static constexpr double kRl = 1.0e6; // Next stage's grid leak (the load)
  static constexpr double kReferenceBiasV = -1.5; // Bias the output level is matched at
  static constexpr double kDecoupleTauS = 0.1; // Shared supply: the preamp's decoupling RC (e.g. 10k x 10 uF)

  // Rk that puts the stage at rest at `biasV` (grid-to-cathode) with plate load `raOhms`, plus the
  // resting plate current. Solved by bisection on the plate current (monotonic).
  static void SolveBias(double biasV, double raOhms, double& rkOut, double& ipOut,
                        const TriodeTables& tables = TriodeTables::Get())
  {
    const double vk = -biasV;
    const double ig = Triode(tables, biasV, 0.0).ig; // Grid current at a negative bias is ~Ig0 regardless of Vpk
    double lo = 0.0, hi = kBPlus / raOhms;
    for (int i = 0; i < 60; i++)
    {
      const double mid = 0.5 * (lo + hi);
      const double vpk = kBPlus - raOhms * mid - vk;
      const double ip = Triode(tables, biasV, vpk).ik - ig;
      if (mid - ip < 0.0)
        lo = mid;
      else
        hi = mid;
    }
    ipOut = 0.5 * (lo + hi);
    rkOut = vk / std::max(ipOut + ig, 1e-9);
  }

  // Static transfer (plate swing per grid volt, at unity small-signal gain) for the UI plot, with
  // the coupling and bypass caps frozen at rest. A pure function of its arguments, so it's safe to
  // call from the UI thread.
  static double StaticCurve(double vin, double biasV, double raOhms)
  {
    double rk = 0.0, ipq = 0.0;
    SolveBias(biasV, raOhms, rk, ipq);
    const double vk = -biasV;
    auto plateCurrent = [&](double vg) {
      double lo = 0.0, hi = kBPlus / raOhms;
      for (int i = 0; i < 50; i++)
      {
        const double mid = 0.5 * (lo + hi);
        const TriodeCurrents t = Triode(vg - vk, kBPlus - raOhms * mid - vk);
        if (mid - (t.ik - t.ig) < 0.0)
          lo = mid;
        else
          hi = mid;
      }
      return 0.5 * (lo + hi);
    };
    // Grid voltage after the stopper: vin - Rstop * Ig(vg - vk) = vg.
    double lo = std::min(vin, 0.0) - vk - 1.0, hi = vin;
    for (int i = 0; i < 50; i++)
    {
      const double mid = 0.5 * (lo + hi);
      if (vin - kRstop * Triode(mid - vk, 100.0).ig - mid > 0.0)
        lo = mid;
      else
        hi = mid;
    }
    const double vg = 0.5 * (lo + hi);
    return raOhms * (plateCurrent(vg) - ipq) / ReferenceGain();
  }

  void Reset(double sampleRate, const CircuitParams& p)
  {
    mT = 1.0 / sampleRate;
    mParams = p;
    mSupplyScale = 1.0;
    mSupplyShared = false;
    _UpdateComponents();
    _SolveDC();
  }

  // Applies new component values (a pot being turned). Node voltages and capacitor charges carry
  // over, so the circuit moves to its new state physically rather than jumping.
  void SetParams(const CircuitParams& p)
  {
    if (p == mParams)
      return;
    const bool capSwitched = p.cathodeCap != mParams.cathodeCap;
    const bool plateCapChanged = p.plateCapFarads != mParams.plateCapFarads;
    const bool followerOn = p.cathodeFollower && (!mParams.cathodeFollower || p.followerTube != mParams.followerTube);
    mParams = p;
    _UpdateComponents();
    if (followerOn)
    {
      // The follower comes in where the plate is now, its output's coupling high-pass where the
      // stage's own output (the coupling cap's) is, so the hand-over doesn't step.
      mCf.Configure(mVPRest, false);
      double ig, dig;
      mCf.Load(mV[kP], ig, dig);
      mCfHpX = mCf.Output();
      mCfHpY = mV[kO];
    }
    if (capSwitched)
      mCk.SetResting(mV[kK]); // Cap appears/disappears holding the cathode's current voltage.
    if (plateCapChanged)
      mCp.SetResting(mV[kP] - mVRail); // A swapped plate cap starts at the plate's current voltage.
  }

  // One sample: vin is the voltage at the input (volts). Returns the output plate swing, inverted
  // back and divided by the stage's mid-band small-signal gain (so small signals come out at
  // unity, like the LUT engine).
  double Process(double vin)
  {
    // Newton's starting point (1.45.0): the last two solutions extrapolated in a straight line,
    // instead of the last one. At the oversampled rate the node voltages move smoothly, so this
    // lands within a fraction of a millivolt and Newton usually finishes in one step.
    for (int r = 0; r < kNumNodes; r++)
    {
      const double v = mV[r];
      mV[r] += v - mVPrev[r];
      mVPrev[r] = v;
    }
    _Solve(vin, kMaxNewtonIterations);

    // Capacitor histories (trapezoidal). The input cap is in series with the source resistance:
    // recover its branch current, then the voltage across the cap itself.
    const double iIn = mCinGeq * (vin - mV[kA] - mCin.ieq * mCinR);
    mCin.Update((iIn + mCin.ieq) * mCinR);
    mCgk.Update(mV[kG] - mV[kK]);
    mCgp.Update(mV[kG] - mV[kP]);
    if (mParams.cathodeCap)
      mCk.Update(mV[kK]);
    mCc.Update(mV[kP] - mV[kO]);
    if (mParams.plateCapFarads > 0.0)
      mCp.Update(mV[kP] - mVRail);

    // Supply node: relaxes toward Vsupply - Rs * I with the Recovery time constant.
    if (mParams.sag)
    {
      const double iSupply = (mVRail - mV[kP]) / mParams.raOhms + mExtraLoad;
      mVRail += mSagCoef * (mVSupply * mSupplyScale - mParams.rsOhms * iSupply - mVRail);
    }
    else if (mSupplyShared)
      mVRail = kBPlus * mSupplyScale;
    if (mParams.cathodeFollower)
    {
      // The follower's cathode instead of the coupling cap. The tone stack's caps block its DC; the
      // same high-pass as the coupling cap into the next grid (Cc x Rl, ~7 Hz, bilinear like the
      // circuit's) keeps the low end -- and the switch between the two -- the same.
      const double x = mCf.Output();
      mCfHpY = mCfHpB * (x - mCfHpX) + mCfHpA * mCfHpY;
      mCfHpX = x;
      return -mCfHpY / mGain;
    }
    return -mV[kO] / mGain;
  }

  // Cascading (see TubeStage): stage 1 owns the supply; stage 2 is fed stage 1's rail each sample
  // and reports the current it draws back to stage 1 as extra load on the shared supply.
  void SetRail(double v) { mVRail = v; }
  double SupplyCurrent() const { return (mVRail - mV[kP]) / mParams.raOhms; }
  // Shared supply: where the upstream supply is now, relative to rest (1 = at rest), once per
  // sample. It reaches this stage through the decoupling filter. Sending 1 lets it settle back,
  // after which the stage runs exactly as an unshared one again.
  void SetSupplyTarget(double scale)
  {
    mSupplyScale += mDecoupleCoef * (scale - mSupplyScale);
    mSupplyShared = !(scale == 1.0 && std::fabs(mSupplyScale - 1.0) < 1e-7);
    if (!mSupplyShared)
    {
      mSupplyScale = 1.0;
      if (!mParams.sag)
        mVRail = kBPlus;
    }
  }
  bool SupplyShared() const { return mSupplyShared; }
  void SetExtraLoad(double amps) { mExtraLoad = amps; }
  void SetRestingExtraLoad(double amps)
  {
    mRestingExtraLoad = amps;
    mVSupply = kBPlus + (mParams.sag ? mParams.rsOhms * (mIpq + mRestingExtraLoad) : 0.0);
  }
  double RestingPlateCurrent() const { return mIpq; }
  // Plate voltage relative to its resting value: what the next stage's coupling cap passes on.
  double PlateSwing() const { return mV[kP] - mVPRest; }
  // 1.68.0: what drives the next stage: the plate swing, or its follower's cathode swing when it has one.
  double DriveSwing() const { return mParams.cathodeFollower ? mCf.Output() : PlateSwing(); }
  // Small-signal output resistance at the plate (Ra || rp): the next stage's source resistance.
  double OutputResistance() const { return mRout; }

  int LastIterations() const { return mLastIterations; }
  double PlateVoltage() const { return mV[kP]; }
  double CathodeVoltage() const { return mV[kK]; }
  double RailVoltage() const { return mVRail; }
  double Rk() const { return mRk; }
  double Gain() const { return mGain; }
  // Debug probes (TubeProbe.h).
  double GridCathodeVoltage() const { return mV[kG] - mV[kK]; }
  // The tube's own plate current (cathode minus grid current), not the current in Ra: in cutoff the
  // plate resistor still carries current into the output coupling cap and the next stage.
  double PlateCurrent() const
  {
    TriodeCurrents t = Triode(*mTables, mV[kG] - mV[kK], mV[kP] - mV[kK]);
    if (mBenchOn)
      ApplyBench(t, mBench);
    return t.ik - t.ig;
  }
  // Bench (TubeBench.h): perveance and the emission ceiling (mains comes in through SetSupplyTarget).
  void SetBench(const bench::State& s)
  {
    mBench = s;
    mBenchOn = s.perveance != 1.0 || s.invCeiling > 0.0;
  }
  // The input coupling cap's charge relative to rest. Its slow part (TubeStage low-passes it) is
  // how much colder than its set bias the grid sits after grid current on hard hits: blocking.
  double BlockingShift() const { return mCin.v - mCinRest; }
  double RestingPlateVoltage() const { return mVPRest; }
  // Frequency above which the cathode bypass cap gives full gain (0 with no cap).
  double CathodeCornerHz() const { return mCathodeCornerHz; }
  // Time constant of blocking recovery: the coupling cap discharging through the grid leak.
  double BlockRecoverSeconds() const { return mParams.cinFarads * (kRgl + mParams.sourceOhms); }

private:
  enum Node
  {
    kA = 0, // After the input coupling cap
    kG, // Grid
    kK, // Cathode
    kP, // Plate
    kO, // Output (after the output coupling cap)
    kNumNodes
  };
  static constexpr int kMaxNewtonIterations = 8;
  // Newton stops once a step moves no node more than this (volts). It converges quadratically, so
  // the error left after a step of 0.1 mV is around 1e-8 V: 160 dB under the signal measured against
  // a 200-iteration solve (1.45.0; it was 1e-5, which spent an extra iteration confirming).
  static constexpr double kNewtonTolerance = 1e-4;

  // Trapezoidal companion model: i = g * v - ieq, with g = 2C/T and ieq = g * v[n-1] + i[n-1].
  // The last voltage and current are kept so a value change (a cap selector, or the Bloom knob
  // setting the input cap) keeps the voltage across the cap and its current, as on a real circuit.
  // Before 1.19 only ieq was kept, so a new g rescaled the cap's voltage by old/new: switching the
  // cathode cap from 25 uF to 0.68 uF made a ~37x jump, a thump of over 4x full scale.
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
    void SetResting(double vr) // DC: no current, voltage vr
    {
      v = vr;
      i = 0.0;
      ieq = g * vr;
    }
  };

  void _UpdateComponents()
  {
    mTables = &TriodeTables::For(mParams.tubeType);
    mCf.SetTubeType(mParams.followerTube);
    double ipq = 0.0;
    SolveBias(mParams.biasV, mParams.raOhms, mRk, ipq, *mTables);
    mCin.Init(mParams.cinFarads, mT);
    mCgk.Init(kCgk, mT);
    mCgp.Init(kCgp, mT);
    mCk.Init(mParams.ckFarads, mT);
    mCc.Init(kCc, mT);
    mCp.Init(mParams.plateCapFarads, mT);
    mCinR = 1.0 / mCin.g;
    mCinGeq = 1.0 / (mParams.sourceOhms + mCinR);
    mIpq = ipq;
    mVPRest = kBPlus - mParams.raOhms * ipq;
    mCf.Configure(mVPRest, true); // The follower's resting point moves with the stage's
    {
      const double k = 2.0 * kCc * kRl / mT;
      mCfHpB = k / (1.0 + k);
      mCfHpA = (k - 1.0) / (k + 1.0);
    }
    {
      const TriodeCurrents t = Triode(*mTables, mParams.biasV, mVPRest + mParams.biasV);
      const double rp = 1.0 / std::max(t.dikDvpk, 1e-12);
      mRout = mParams.raOhms * rp / (mParams.raOhms + rp);
      // The cap bypasses Rk in parallel with the resistance looking into the cathode, (Ra + rp) /
      // (mu + 1); its full gain starts where its reactance falls below that parallel combination.
      const double rkIn = (mParams.raOhms + rp) / (mTables->Model().mu + 1.0);
      const double rPar = mRk * rkIn / (mRk + rkIn);
      mCathodeCornerHz = mParams.cathodeCap ? 1.0 / (2.0 * 3.14159265358979 * rPar * mParams.ckFarads) : 0.0;
    }
    mVSupply = kBPlus + (mParams.sag ? mParams.rsOhms * (ipq + mRestingExtraLoad) : 0.0);
    mSagCoef = 1.0 - std::exp(-mT / std::max(mParams.sagTimeS, 1e-3));
    mDecoupleCoef = 1.0 - std::exp(-mT / kDecoupleTauS);
    if (!mParams.sag)
      mVRail = kBPlus * mSupplyScale;

    // Output normalization: one fixed reference -- the mid-band gain of the default stage (Bias
    // -1.5 V, 100k plate, bypassed cathode). So Bias, Plate and Cathode Cap change the level the
    // way they do on a real amp (colder, a smaller plate resistor or no bypass cap = quieter)
    // instead of being normalized away, which also keeps the output bounded at the extremes.
    mGain = ReferenceGain();
    _BuildLinearPart();
  }

public:
  static double ReferenceGain()
  {
    static const double gain = [] {
      double rk = 0.0, ipq = 0.0;
      const double ra = CircuitParams().raOhms;
      SolveBias(kReferenceBiasV, ra, rk, ipq);
      const TriodeCurrents t = TriodeExact(kReferenceBiasV, kBPlus - ra * ipq + kReferenceBiasV);
      const double gm = t.dikDvgk - t.digDvgk;
      const double rp = 1.0 / std::max(t.dikDvpk, 1e-12);
      const double rload = ra * kRl / (ra + kRl);
      return gm * rp * rload / (rp + rload);
    }();
    return gain;
  }

private:

  // The conductance matrix's constant part (resistors and capacitor companion conductances); only
  // the capacitor history sources and the tube change per sample.
  void _BuildLinearPart()
  {
    for (auto& row : mGl)
      for (double& g : row)
        g = 0.0;
    auto stampR = [&](int a, int b, double g) {
      mGl[a][a] += g;
      if (b >= 0)
      {
        mGl[b][b] += g;
        mGl[a][b] -= g;
        mGl[b][a] -= g;
      }
    };
    mGl[kA][kA] += mCinGeq;
    stampR(kA, -1, 1.0 / kRgl);
    stampR(kA, kG, 1.0 / kRstop);
    stampR(kG, kK, mCgk.g);
    stampR(kG, kP, mCgp.g);
    stampR(kK, -1, 1.0 / mRk);
    if (mParams.cathodeCap)
      stampR(kK, -1, mCk.g);
    mGl[kP][kP] += 1.0 / mParams.raOhms;
    if (mParams.plateCapFarads > 0.0)
      mGl[kP][kP] += mCp.g; // Plate cap to the rail (an AC ground; its voltage is known)
    stampR(kP, kO, mCc.g);
    stampR(kO, -1, 1.0 / kRl);
    // The per-sample solve's reduced system (1.35.0, see _SolveReduced): A connects only to G (and
    // ground, and the source) and O only to P, so they're eliminated once here and each Newton step
    // solves just the tube's three nodes.
    const int n[3] = {kG, kK, kP};
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
        mS[r][c] = mGl[n[r]][n[c]];
    mS[0][0] -= mGl[kG][kA] * mGl[kA][kG] / mGl[kA][kA];
    mS[2][2] -= mGl[kP][kO] * mGl[kO][kP] / mGl[kO][kO];
    mInvAA = 1.0 / mGl[kA][kA];
    mInvOO = 1.0 / mGl[kO][kO];
    mRatioGA = mGl[kG][kA] * mInvAA;
    mRatioPO = mGl[kP][kO] * mInvOO;
  }

  // Resting operating point with the caps open (no signal), then the caps charged to match it.
  void _SolveDC()
  {
    double ipq = 0.0;
    SolveBias(mParams.biasV, mParams.raOhms, mRk, ipq, *mTables);
    mVRail = kBPlus;
    const double ig0 = mTables->Model().Ig0;
    mV[kA] = -ig0 * kRgl;
    mV[kG] = mV[kA] - ig0 * kRstop;
    mV[kK] = (ipq + ig0) * mRk;
    mV[kP] = kBPlus - mParams.raOhms * ipq;
    mV[kO] = 0.0;
    // Refine with Newton on the DC circuit (caps open).
    _Solve(0.0, 40, true);
    mCin.SetResting(0.0 - mV[kA]);
    mCinRest = mCin.v;
    mCgk.SetResting(mV[kG] - mV[kK]);
    mCgp.SetResting(mV[kG] - mV[kP]);
    mCk.SetResting(mV[kK]);
    mCc.SetResting(mV[kP] - mV[kO]);
    mCp.SetResting(mV[kP] - mVRail);
    for (int r = 0; r < kNumNodes; r++)
      mVPrev[r] = mV[r]; // At rest: nothing to extrapolate
    mCf.Configure(mV[kP], false);
    mCfHpX = mCfHpY = 0.0;
  }

  // Newton-Raphson on the node equations: sum of currents leaving each node = 0. Built twice, with
  // and without the bench (1.58.0), so a stock tube's Newton loop doesn't carry it (it cost ~3%).
  void _Solve(double vin, int maxIterations, bool dc = false)
  {
    if (mBenchOn)
      _SolveT<true>(vin, maxIterations, dc);
    else
      _SolveT<false>(vin, maxIterations, dc);
  }
  template <bool kBench>
  void _SolveT(double vin, int maxIterations, bool dc)
  {
    // Linear part (conductance matrix) and sources for this sample. The per-sample (non-DC) case
    // reuses the precomputed matrix and only builds the capacitor history sources.
    // The DC solve builds its own matrix; the per-sample one uses the precomputed mGl as it is.
    double glDC[kNumNodes][kNumNodes];
    double s[kNumNodes] = {};
    if (dc)
      for (auto& row : glDC)
        for (double& g : row)
          g = 0.0;
    double (*gl)[kNumNodes] = dc ? glDC : mGl;
    if (!dc)
    {
      s[kA] += mCinGeq * (vin - mCin.ieq * mCinR);
      s[kG] += mCgk.ieq + mCgp.ieq;
      s[kK] -= mCgk.ieq;
      if (mParams.cathodeCap)
        s[kK] += mCk.ieq;
      s[kP] += -mCgp.ieq + mCc.ieq + mVRail / mParams.raOhms;
      if (mParams.plateCapFarads > 0.0)
        s[kP] += mCp.ieq + mCp.g * mVRail;
      s[kO] -= mCc.ieq;
    }
    else
    {
    auto stampR = [&](int a, int b, double g) {
      gl[a][a] += g;
      if (b >= 0)
      {
        gl[b][b] += g;
        gl[a][b] -= g;
        gl[b][a] -= g;
      }
    };
    auto stampCap = [&](int a, int b, const Cap& c) {
      if (dc)
        return;
      stampR(a, b, c.g);
      s[a] += c.ieq;
      if (b >= 0)
        s[b] -= c.ieq;
    };
    // Input coupling cap from the (known) source to A: leaving A = g*VA - (g*Vin - ieq).
    if (!dc)
    {
      gl[kA][kA] += mCin.g;
      s[kA] += mCin.g * vin - mCin.ieq;
    }
    stampR(kA, -1, 1.0 / kRgl);
    stampR(kA, kG, 1.0 / kRstop);
    stampCap(kG, kK, mCgk);
    stampCap(kG, kP, mCgp);
    stampR(kK, -1, 1.0 / mRk);
    if (mParams.cathodeCap)
      stampCap(kK, -1, mCk);
    // Plate load to the (known) rail.
    gl[kP][kP] += 1.0 / mParams.raOhms;
    s[kP] += mVRail / mParams.raOhms;
    stampCap(kP, kO, mCc);
    stampR(kO, -1, 1.0 / kRl);
    }

    const TriodeTables& tables = *mTables;
    int it = 0;
    for (; it < maxIterations; it++)
    {
      const double vgk = mV[kG] - mV[kK];
      const double vpk = mV[kP] - mV[kK];
      TriodeCurrents t = Triode(tables, vgk, vpk);
      if (kBench)
        ApplyBench(t, mBench);
      const double ip = t.ik - t.ig;

      double f[kNumNodes];
      if (!dc)
      {
        // 1.53.1: only the entries _BuildLinearPart can make non-zero (13 of 25), in the same order
        // as the full product below. The skipped terms are exact zeros, so the sums are identical.
        f[kA] = -s[kA] + gl[kA][kA] * mV[kA] + gl[kA][kG] * mV[kG];
        f[kG] = -s[kG] + gl[kG][kA] * mV[kA] + gl[kG][kG] * mV[kG] + gl[kG][kK] * mV[kK] + gl[kG][kP] * mV[kP];
        f[kK] = -s[kK] + gl[kK][kG] * mV[kG] + gl[kK][kK] * mV[kK];
        f[kP] = -s[kP] + gl[kP][kG] * mV[kG] + gl[kP][kP] * mV[kP] + gl[kP][kO] * mV[kO];
        f[kO] = -s[kO] + gl[kO][kP] * mV[kP] + gl[kO][kO] * mV[kO];
      }
      else
        for (int r = 0; r < kNumNodes; r++)
        {
          f[r] = -s[r];
          for (int c = 0; c < kNumNodes; c++)
            f[r] += gl[r][c] * mV[c];
        }
      // Tube currents leaving each node: grid current leaves G, plate current leaves P, and both
      // arrive at K.
      f[kG] += t.ig;
      f[kK] -= t.ik;
      f[kP] += ip;
      // The cathode follower's grid current, drawn from the plate (see CathodeFollowerTable).
      double cfSlope = 0.0;
      if (mParams.cathodeFollower)
      {
        double ig;
        mCf.Load(mV[kP], ig, cfSlope);
        f[kP] += ig;
      }
      const double a = t.dikDvgk, b = t.dikDvpk, c = t.digDvgk;

      double dx[kNumNodes];
      if (dc)
      {
        double j[kNumNodes][kNumNodes];
        for (int r = 0; r < kNumNodes; r++)
          for (int cc = 0; cc < kNumNodes; cc++)
            j[r][cc] = gl[r][cc];
        j[kG][kG] += c;
        j[kG][kK] -= c;
        j[kK][kG] -= a;
        j[kK][kP] -= b;
        j[kK][kK] += a + b;
        j[kP][kG] += a - c;
        j[kP][kP] += b + cfSlope;
        j[kP][kK] += c - a - b;
        for (int r = 0; r < kNumNodes; r++)
          dx[r] = -f[r];
        _SolveLinear(j, dx);
      }
      else
        _SolveReduced(a, b, c, cfSlope, f, dx);

      // A loose sanity limit on each node's step. It used to be 2 V for the input, grid and cathode,
      // but a cascaded stage 2's input follows stage 1's plate, which can move tens of volts in one
      // sample: 8 iterations of 2 V couldn't catch up, so the solve stopped short of the answer
      // (1.33.0: Modern High Gain's output was off by up to -20 dB re its peak, a cranked stage 2 by
      // up to its whole peak). The Dempwolf model grows only polynomially past its knees, so plain
      // Newton steps are safe; measured against a 400-iteration solve, 8 now converge everywhere.
      double maxStep = 0.0;
      for (int r = 0; r < kNumNodes; r++)
      {
        const double limit = 100.0;
        dx[r] = std::clamp(dx[r], -limit, limit);
        mV[r] += dx[r];
        maxStep = std::max(maxStep, std::fabs(dx[r]));
      }
      if (maxStep < kNewtonTolerance)
      {
        it++;
        break;
      }
    }
    mLastIterations = it;
  }

  // The per-sample Newton step (1.35.0): J dx = -f with J the linear part plus the tube's
  // derivatives on G, K and P. A and O are linear and each tied to one tube node, so they're
  // eliminated (mS, built with mGl): a 3x3 system for G, K, P, then A and O back-substituted. The
  // same step as solving all five nodes (it was 40% of the Tube Lab's time), about 4x cheaper.
  void _SolveReduced(double a, double b, double c, double plateLoad, const double f[kNumNodes],
                     double dx[kNumNodes]) const
  {
    double m[3][3] = {{mS[0][0] + c, mS[0][1] - c, mS[0][2]},
                      {mS[1][0] - a, mS[1][1] + a + b, mS[1][2] - b},
                      {mS[2][0] + a - c, mS[2][1] + c - a - b, mS[2][2] + b + plateLoad}};
    double r[3] = {-f[kG] + mRatioGA * f[kA], -f[kK], -f[kP] + mRatioPO * f[kO]};
    double x[3];
    // Cramer's rule (one division); pivoted elimination below if it's near singular.
    const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    const double scale = std::fabs(m[0][0]) + std::fabs(m[1][1]) + std::fabs(m[2][2]);
    if (std::fabs(det) > 1e-9 * scale * scale * scale)
    {
      const double inv = 1.0 / det;
      const double c10 = m[0][2] * m[2][1] - m[0][1] * m[2][2];
      const double c11 = m[0][0] * m[2][2] - m[0][2] * m[2][0];
      const double c12 = m[0][1] * m[2][0] - m[0][0] * m[2][1];
      const double c20 = m[0][1] * m[1][2] - m[0][2] * m[1][1];
      const double c21 = m[0][2] * m[1][0] - m[0][0] * m[1][2];
      const double c22 = m[0][0] * m[1][1] - m[0][1] * m[1][0];
      x[0] = (c00 * r[0] + c10 * r[1] + c20 * r[2]) * inv;
      x[1] = (c01 * r[0] + c11 * r[1] + c21 * r[2]) * inv;
      x[2] = (c02 * r[0] + c12 * r[1] + c22 * r[2]) * inv;
      dx[kG] = x[0];
      dx[kK] = x[1];
      dx[kP] = x[2];
      dx[kA] = (-f[kA] - mGl[kA][kG] * x[0]) * mInvAA;
      dx[kO] = (-f[kO] - mGl[kO][kP] * x[2]) * mInvOO;
      return;
    }
    // 3x3 Gaussian elimination with partial pivoting.
    for (int col = 0; col < 3; col++)
    {
      int pivot = col;
      for (int row = col + 1; row < 3; row++)
        if (std::fabs(m[row][col]) > std::fabs(m[pivot][col]))
          pivot = row;
      if (pivot != col)
      {
        for (int k = 0; k < 3; k++)
          std::swap(m[col][k], m[pivot][k]);
        std::swap(r[col], r[pivot]);
      }
      const double d = m[col][col];
      if (std::fabs(d) < 1e-30)
        continue;
      for (int row = col + 1; row < 3; row++)
      {
        const double k = m[row][col] / d;
        for (int cc = col; cc < 3; cc++)
          m[row][cc] -= k * m[col][cc];
        r[row] -= k * r[col];
      }
    }
    for (int row = 2; row >= 0; row--)
    {
      double sum = r[row];
      for (int cc = row + 1; cc < 3; cc++)
        sum -= m[row][cc] * x[cc];
      x[row] = std::fabs(m[row][row]) > 1e-30 ? sum / m[row][row] : 0.0;
    }
    dx[kG] = x[0];
    dx[kK] = x[1];
    dx[kP] = x[2];
    dx[kA] = (-f[kA] - mGl[kA][kG] * x[0]) * mInvAA;
    dx[kO] = (-f[kO] - mGl[kO][kP] * x[2]) * mInvOO;
  }

  // Gaussian elimination with partial pivoting, in place (b becomes the solution).
  static void _SolveLinear(double a[kNumNodes][kNumNodes], double b[kNumNodes])
  {
    for (int col = 0; col < kNumNodes; col++)
    {
      int pivot = col;
      for (int r = col + 1; r < kNumNodes; r++)
        if (std::fabs(a[r][col]) > std::fabs(a[pivot][col]))
          pivot = r;
      if (pivot != col)
      {
        for (int c = 0; c < kNumNodes; c++)
          std::swap(a[col][c], a[pivot][c]);
        std::swap(b[col], b[pivot]);
      }
      const double d = a[col][col];
      if (std::fabs(d) < 1e-30)
        continue;
      for (int r = col + 1; r < kNumNodes; r++)
      {
        const double m = a[r][col] / d;
        if (m == 0.0)
          continue;
        for (int c = col; c < kNumNodes; c++)
          a[r][c] -= m * a[col][c];
        b[r] -= m * b[col];
      }
    }
    for (int r = kNumNodes - 1; r >= 0; r--)
    {
      double sum = b[r];
      for (int c = r + 1; c < kNumNodes; c++)
        sum -= a[r][c] * b[c];
      b[r] = std::fabs(a[r][r]) > 1e-30 ? sum / a[r][r] : 0.0;
    }
  }

  CircuitParams mParams;
  double mT = 1.0 / 96000.0;
  double mRk = 1500.0;
  double mGain = 60.0;
  double mGl[5][5] = {}; // Constant part of the conductance matrix (see _BuildLinearPart)
  double mS[3][3] = {}; // Its reduction onto G, K, P (see _SolveReduced)
  double mInvAA = 0.0, mInvOO = 0.0, mRatioGA = 0.0, mRatioPO = 0.0;
  double mV[kNumNodes] = {};
  double mVPrev[kNumNodes] = {}; // The solution a sample earlier (Newton's predictor, see Process)
  double mVRail = kBPlus, mVSupply = kBPlus, mSagCoef = 0.0;
  double mCinGeq = 0.0; // Input cap in series with the source resistance, as one conductance
  double mCinR = 0.0; // 1 / the input cap's companion conductance
  double mIpq = 0.0, mVPRest = kBPlus, mRout = 38e3;
  double mExtraLoad = 0.0, mRestingExtraLoad = 0.0; // Other stages on the same supply
  double mSupplyScale = 1.0, mDecoupleCoef = 0.0; // Shared supply (see SetSupplyTarget)
  bool mSupplyShared = false;
  Cap mCin, mCgk, mCgp, mCk, mCc, mCp; // mCp: plate cap (Ra bypass), plate to rail
  double mCinRest = 0.0; // Input cap voltage at rest (grid leakage through the grid leak)
  double mCathodeCornerHz = 0.0;
  CathodeFollower mCf; // Used when mParams.cathodeFollower
  double mCfHpX = 0.0, mCfHpY = 0.0, mCfHpA = 0.0, mCfHpB = 1.0; // Its output's coupling high-pass
  int mLastIterations = 0;
  bench::State mBench; // See SetBench
  bool mBenchOn = false;
  const TriodeTables* mTables = &TriodeTables::Get(); // The stage's tube (mParams.tubeType)
};
} // namespace tube
