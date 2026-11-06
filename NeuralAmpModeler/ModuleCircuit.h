#pragma once

// Preamp modules (1.72.0): whole preamps, stage by stage, from their schematics, for the MODULAR chassis's
// bays (PreampModules.h has the circuits, TubeStage runs the one in the active bay).
//
// A module is a chain of triode gain stages and then an output network (usually the tone stack). Each
// stage is its own small circuit, solved per sample like TubeCircuit.h's TriodeStageCircuit, but built
// from a netlist instead of a fixed layout:
//
//   previous stage --Rsrc-- S --(its network: coupling caps, pots, bright caps, dividers,       B+
//   (its Thevenin swing)        a tone stack between stages, a grid stopper)-- G               |
//                                                                               \             Ra || Cp
//                                                                    Cgk  Cgp    triode ------ P --(any of the
//                                                                                    |              stage's own
//                                                                                    K -- Rk || Ck  output parts)
//
// * The network is linear: resistors, capacitors (trapezoidal companions, as everywhere in the Tube Lab)
//   and pots (two resistors that move with a knob, or a rheostat). Everything the schematic draws between
//   one plate and the next grid goes in, so a gain pot's bright cap, a 470k || 470p divider or a plate-
//   driven tone stack load and shape the signal the way the real parts do.
// * The triode is TubeCircuit.h's (the Dempwolf model, from the same lookup tables), with grid current:
//   blocking on the coupling caps, and the grid conduction that a divider's resistance limits, happen on
//   their own.
// * Solving: the linear nodes are eliminated once whenever a knob moves (a Schur complement), so each
//   sample's Newton iterations solve only the tube's three nodes (G, K, P), as TriodeStageCircuit does;
//   the linear nodes are back-substituted after.
// * Stage to stage: the next stage is driven by this stage's output swing (its plate, or a node after its
//   own output parts) through this stage's small-signal output resistance -- a Thevenin source. The
//   plate itself is loaded by what the stage's own netlist hangs on it, not by the next stage's network
//   (each stage is solved in turn, not all at once): the next network's loading is in its source
//   resistance instead.
// * A stage can end in a cathode follower (DC-coupled to its plate, as in the British amps and the
//   modern high-gain ones): ModFollower, TubeCircuit.h's CathodeFollower with the schematic's parts.
// * Each stage's B+ is the schematic's node voltage for it (scaled by the Variac and a shared supply).
//
// Plain C++ (no iPlug2), so it can be tested on its own.

#include <algorithm>
#include <cmath>

#include "PowerAmp.h" // PowerTube
#include "TubeCircuit.h"

namespace tube
{
namespace module
{
constexpr int kMaxLinear = 12; // Linear nodes in one stage's network (S and N1..N11)
constexpr int kMaxElems = 24;
constexpr int kMaxStages = 6;

// Netlist nodes. S is where the source (the previous stage, or the guitar) connects through its
// resistance; G, K, P are the stage's tube.
enum Node : int
{
  GND = -1,
  S = 0,
  N1,
  N2,
  N3,
  N4,
  N5,
  N6,
  N7,
  N8,
  N9,
  N10,
  N11,
  G = 100,
  K,
  P
};

enum ElemType
{
  kRes = 0,
  kCap,
  kPotTop, // A pot's track from its top end to the wiper: R x (1 - taper(position))
  kPotBottom, // From the wiper to its bottom end: R x taper(position)
  kRheo // A pot wired as a variable resistor: R x taper(position)
};

// The knobs a module's pots can follow (a bay's Gain, Gain 2, Bass, Mid, Treble).
enum Knob
{
  kNoKnob = -1,
  kKnobGain = 0,
  kKnobGain2,
  kKnobBass,
  kKnobMid,
  kKnobTreble,
  kNumKnobs
};

enum Taper
{
  kLinear = 0,
  kAudio // Log: ~10% of the track at the middle (tonestack::AudioTaper)
};

struct Elem
{
  int type = kRes;
  int a = GND, b = GND;
  double value = 0.0; // Ohms or farads
  int knob = kNoKnob;
  int taper = kLinear;
};

struct StageDef
{
  int tube = k12AX7;
  double railV = 300.0; // B+ at the top of Ra
  double raOhms = 100e3;
  double plateCapF = 0.0; // Across Ra (to the rail)
  double rkOhms = 1500.0;
  double ckF = 0.0; // Across Rk (0 = unbypassed; other cathode parts can be elements on K)
  int numElems = 0;
  Elem elems[kMaxElems];
  int out = P; // The node whose swing drives the next stage
  // A cathode follower on the plate (DC-coupled through Rs), driving what's next from its cathode.
  bool follower = false;
  double cfRkOhms = 100e3, cfRsOhms = 1e3, cfRailV = 320.0, cfLoadOhms = 100e3;

  StageDef& R(int a, int b, double ohms) { return _Add({kRes, a, b, ohms}); }
  StageDef& C(int a, int b, double farads) { return _Add({kCap, a, b, farads}); }
  // A pot: top end, wiper, bottom end.
  StageDef& Pot(int top, int wiper, int bottom, double ohms, int knob, int taper)
  {
    _Add({kPotTop, top, wiper, ohms, knob, taper});
    return _Add({kPotBottom, wiper, bottom, ohms, knob, taper});
  }
  StageDef& Rheo(int a, int b, double ohms, int knob, int taper) { return _Add({kRheo, a, b, ohms, knob, taper}); }
  // The tone stack of most amps (tonestack::TMBStack's circuit) from `in`, its output on the treble pot's
  // wiper `o`; nodes a, b, c, d are its own (top of the treble pot, top of the bass pot, the slope
  // resistor's far end, top of the mid pot).
  StageDef& Stack(int in, int o, int a, int b, int c, int d, double c1, double c2, double c3, double rTreble,
                  double rBass, double rMid, double rSlope, int trebleTaper = kLinear)
  {
    C(in, a, c1);
    Pot(a, o, b, rTreble, kKnobTreble, trebleTaper);
    Rheo(b, d, rBass, kKnobBass, kAudio);
    Rheo(d, GND, rMid, kKnobMid, kLinear);
    R(in, c, rSlope);
    C(c, b, c2);
    return C(c, d, c3);
  }
  StageDef& Follower(double rk, double rail, double load, double rs = 1e3)
  {
    follower = true;
    cfRkOhms = rk;
    cfRailV = rail;
    cfLoadOhms = load;
    cfRsOhms = rs;
    return *this;
  }

private:
  StageDef& _Add(const Elem& e)
  {
    if (numElems < kMaxElems)
      elems[numElems++] = e;
    return *this;
  }
};

inline double KnobTaper(double position01, int taper)
{
  const double p = std::clamp(position01, 0.0, 1.0);
  return taper == kAudio ? (std::exp(4.6 * p) - 1.0) / (std::exp(4.6) - 1.0) : p;
}

// A pot end can't be exactly 0 ohms in a nodal solve; a few ohms is a real pot's end resistance.
constexpr double kMinOhms = 10.0;

inline double ElemOhms(const Elem& e, const double* knobs)
{
  if (e.type == kRes)
    return e.value;
  const double t = KnobTaper(e.knob >= 0 && knobs != nullptr ? knobs[e.knob] : 0.5, e.taper);
  const double r = e.type == kPotTop ? (1.0 - t) * e.value : t * e.value;
  return std::max(r, kMinOhms);
}

// Numbers a netlist's linear nodes: S is 0, then each N it uses, in order. Returns how many.
inline int MapNodes(const StageDef& d, int (&map)[kMaxLinear])
{
  bool used[kMaxLinear] = {};
  used[0] = true;
  for (int e = 0; e < d.numElems; e++)
    for (int n : {d.elems[e].a, d.elems[e].b})
      if (n >= 0 && n < kMaxLinear)
        used[n] = true;
  if (d.out >= 0 && d.out < kMaxLinear)
    used[d.out] = true;
  int m = 0;
  for (int n = 0; n < kMaxLinear; n++)
    map[n] = used[n] ? m++ : 0;
  return m;
}

// TubeCircuit.h's cathode follower with a schematic's parts. It has no memory of its own (no caps), so
// what it does is a function of the plate voltage driving it alone: solved once over the plate's whole
// range into a table (grid current, its slope, the cathode's swing) and read back each Newton step,
// instead of a Newton solve of its own inside every one of the stage's.
class ModFollower
{
public:
  void SetParts(int tube, double rk, double rs, double rail, double load)
  {
    mTables = &TriodeTables::For(tube);
    mRk = rk;
    mRs = std::max(rs, 1.0);
    mRail = rail;
    mGl = 1.0 / std::max(load, 1.0);
  }
  double Rail() const { return mRail; }
  void SetRail(double v) { mRail = v; }

  // Its resting point for the plate at restPlateV (the load carries no current at rest).
  void Rest(double restPlateV)
  {
    double vg = restPlateV, vk = restPlateV + 1.5, j[4];
    for (int it = 0; it < 200; it++)
      if (_Step(restPlateV, vg, vk, 0.0, j) < 1e-12)
        break;
    mRestK = vk;
    mVg = vg;
    mVk = vk;
    const TriodeCurrents t = Triode(*mTables, vg - vk, mRail - vk);
    const double gm = t.dikDvgk + t.dikDvpk;
    mRout = 1.0 / (1.0 / mRk + std::max(gm, 1e-9)); // Rk || 1/gm
  }

  // The grid current drawn from the plate at vp and its slope, by Newton (for the resting-point solve).
  void LoadExact(double vp, double& ig, double& dig)
  {
    double j[4];
    for (int it = 0; it < 50; it++)
      if (_Step(vp, mVg, mVk, mGl, j) < 1e-9)
        break;
    const double det = j[0] * j[3] - j[1] * j[2];
    ig = (vp - mVg) / mRs;
    dig = (1.0 - j[0] / det) / mRs;
    mOut = mVk - mRestK;
  }

  // The table, over plate voltages 0 .. vMax.
  void BuildTable(double vMax)
  {
    mV0 = 0.0;
    mDv = std::max(vMax, 10.0) / (kTable - 1);
    const double vg0 = mVg, vk0 = mVk;
    mVg = 0.0;
    mVk = 1.0;
    for (int i = 0; i < kTable; i++)
      LoadExact(mV0 + i * mDv, mIg[i], mDig[i]), mOutT[i] = mOut;
    mVg = vg0;
    mVk = vk0;
    mInvDv = 1.0 / mDv;
  }

  void Load(double vp, double& ig, double& dig)
  {
    double pos = (vp - mV0) * mInvDv;
    pos = std::clamp(pos, 0.0, kTable - 1.001);
    const int i = static_cast<int>(pos);
    const double f = pos - i;
    ig = mIg[i] + f * (mIg[i + 1] - mIg[i]);
    dig = mDig[i] + f * (mDig[i + 1] - mDig[i]);
    mOut = mOutT[i] + f * (mOutT[i + 1] - mOutT[i]);
  }
  double Output() const { return mOut; }
  double OutputOhms() const { return mRout; }

private:
  static constexpr int kTable = 2048;

  double _Step(double vp, double& vg, double& vk, double gl, double (&j)[4]) const
  {
    const TriodeCurrents t = Triode(*mTables, vg - vk, mRail - vk);
    const double f1 = vk / mRk + (vk - mRestK) * gl - t.ik;
    const double f2 = vg - vp + mRs * t.ig;
    const double j11 = 1.0 / mRk + gl + t.dikDvgk + t.dikDvpk, j12 = -t.dikDvgk;
    const double j21 = -mRs * t.digDvgk, j22 = 1.0 + mRs * t.digDvgk;
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

  const TriodeTables* mTables = &TriodeTables::Get();
  double mRk = 100e3, mRs = 1e3, mRail = 320.0, mGl = 1e-5;
  double mVg = 0.0, mVk = 0.0, mRestK = 0.0, mRout = 600.0, mOut = 0.0;
  double mV0 = 0.0, mDv = 0.1, mInvDv = 10.0;
  double mIg[kTable] = {}, mDig[kTable] = {}, mOutT[kTable] = {};
};

// Trapezoidal companion (the same as TriodeStageCircuit's): i = g v - ieq.
struct ModCap
{
  int a = GND, b = GND; // Matrix indices (-1 ground, -2 the rail)
  double c = 0.0, g = 0.0, ieq = 0.0, v = 0.0, i = 0.0;
  void Init(double t)
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

// Small dense helpers.
template <int N>
inline bool InvertMatrix(const double (&a)[N][N], double (&inv)[N][N], int n)
{
  double m[N][2 * N];
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
    {
      m[i][j] = a[i][j];
      m[i][n + j] = i == j ? 1.0 : 0.0;
    }
  for (int col = 0; col < n; col++)
  {
    int piv = col;
    for (int row = col + 1; row < n; row++)
      if (std::fabs(m[row][col]) > std::fabs(m[piv][col]))
        piv = row;
    if (std::fabs(m[piv][col]) < 1e-300)
      return false;
    if (piv != col)
      for (int k = 0; k < 2 * n; k++)
        std::swap(m[col][k], m[piv][k]);
    const double d = 1.0 / m[col][col];
    for (int k = 0; k < 2 * n; k++)
      m[col][k] *= d;
    for (int row = 0; row < n; row++)
    {
      if (row == col || m[row][col] == 0.0)
        continue;
      const double f = m[row][col];
      for (int k = 0; k < 2 * n; k++)
        m[row][k] -= f * m[col][k];
    }
  }
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      inv[i][j] = m[i][n + j];
  return true;
}

// Solves a (n x n) x = b in place (b becomes x), partial pivoting.
template <int N>
inline void SolveDense(double (&a)[N][N], double (&b)[N], int n)
{
  for (int col = 0; col < n; col++)
  {
    int piv = col;
    for (int r = col + 1; r < n; r++)
      if (std::fabs(a[r][col]) > std::fabs(a[piv][col]))
        piv = r;
    if (piv != col)
    {
      for (int c = 0; c < n; c++)
        std::swap(a[col][c], a[piv][c]);
      std::swap(b[col], b[piv]);
    }
    const double d = a[col][col];
    if (std::fabs(d) < 1e-300)
      continue;
    for (int r = col + 1; r < n; r++)
    {
      const double f = a[r][col] / d;
      if (f == 0.0)
        continue;
      for (int c = col; c < n; c++)
        a[r][c] -= f * a[col][c];
      b[r] -= f * b[col];
    }
  }
  for (int r = n - 1; r >= 0; r--)
  {
    double s = b[r];
    for (int c = r + 1; c < n; c++)
      s -= a[r][c] * b[c];
    b[r] = std::fabs(a[r][r]) > 1e-300 ? s / a[r][r] : 0.0;
  }
}

// J dx = -f for a 3x3 J: Cramer's rule, pivoted elimination if it's near singular.
inline void Solve3(double (&j)[3][3], const double (&f)[3], double (&dx)[3])
{
  const double c00 = j[1][1] * j[2][2] - j[1][2] * j[2][1];
  const double c01 = j[1][2] * j[2][0] - j[1][0] * j[2][2];
  const double c02 = j[1][0] * j[2][1] - j[1][1] * j[2][0];
  const double det = j[0][0] * c00 + j[0][1] * c01 + j[0][2] * c02;
  // Near singular, relative to the diagonal's product (the grid, cathode and plate rows' conductances are
  // orders of magnitude apart, so a sum-based scale would call most good matrices singular).
  if (std::fabs(det) > 1e-12 * std::fabs(j[0][0] * j[1][1] * j[2][2]))
  {
    const double inv = -1.0 / det;
    const double c10 = j[0][2] * j[2][1] - j[0][1] * j[2][2];
    const double c11 = j[0][0] * j[2][2] - j[0][2] * j[2][0];
    const double c12 = j[0][1] * j[2][0] - j[0][0] * j[2][1];
    const double c20 = j[0][1] * j[1][2] - j[0][2] * j[1][1];
    const double c21 = j[0][2] * j[1][0] - j[0][0] * j[1][2];
    const double c22 = j[0][0] * j[1][1] - j[0][1] * j[1][0];
    dx[0] = (c00 * f[0] + c10 * f[1] + c20 * f[2]) * inv;
    dx[1] = (c01 * f[0] + c11 * f[1] + c21 * f[2]) * inv;
    dx[2] = (c02 * f[0] + c12 * f[1] + c22 * f[2]) * inv;
    return;
  }
  double b[3] = {-f[0], -f[1], -f[2]};
  SolveDense<3>(j, b, 3);
  dx[0] = b[0];
  dx[1] = b[1];
  dx[2] = b[2];
}

// One stage of a module (see the top of the file).
class ModStage
{
public:
  static constexpr int kMaxN = kMaxLinear + 3;
  static constexpr double kCgk = TriodeStageCircuit::kCgk, kCgp = TriodeStageCircuit::kCgp;

  // From rest (every cap charged to its resting voltage). rsrc: the source's resistance into S.
  void Reset(const StageDef& d, double sampleRate, double rsrcOhms, const double* knobs)
  {
    mDef = d;
    mT = 1.0 / sampleRate;
    mRsrc = std::max(rsrcOhms, 1.0);
    mTables = &TriodeTables::For(d.tube);
    mRailScale = 1.0;
    // Linear nodes: S, then every N the netlist uses, numbered in order (unused names take no row).
    mM = MapNodes(d, mMap);
    mN = mM + 3;
    mOut = _Index(d.out);
    // Caps: the netlist's, then the built-in ones.
    mNumCaps = 0;
    for (int e = 0; e < d.numElems; e++)
      if (d.elems[e].type == kCap)
        _AddCap(_Index(d.elems[e].a), _Index(d.elems[e].b), d.elems[e].value);
    _AddCap(mM + 0, mM + 1, kCgk);
    _AddCap(mM + 0, mM + 2, kCgp);
    if (d.ckF > 0.0)
      _AddCap(mM + 1, -1, d.ckF);
    if (d.plateCapF > 0.0)
      _AddCap(mM + 2, -2, d.plateCapF);
    for (int c = 0; c < mNumCaps; c++)
      mCaps[c].Init(mT);
    _SetKnobs(knobs, true);
    _SolveDC();
    _ComputeOutputOhms();
  }

  // A knob moved: new resistances, the caps keep their charge (like turning a real pot).
  void SetKnobs(const double* knobs) { _SetKnobs(knobs, false); }
  bool UsesKnob(int k) const
  {
    for (int e = 0; e < mDef.numElems; e++)
      if (mDef.elems[e].type != kRes && mDef.elems[e].type != kCap && mDef.elems[e].knob == k)
        return true;
    return false;
  }

  // The Variac (and a shared supply) on this stage's B+. The follower's table is rebuilt when its rail
  // has moved by more than 0.2% (a Variac glide rebuilds it a few dozen times; ~0.1 ms each).
  void SetRailScale(double s)
  {
    mRailScale = s;
    if (mDef.follower && std::fabs(mDef.cfRailV * s - mCf.Rail()) > 0.002 * mDef.cfRailV)
    {
      mCf.SetRail(mDef.cfRailV * s);
      mCf.Rest(mVRest[mM + 2]);
      mCf.BuildTable(_PlateMax());
    }
  }

  // One sample: vs is the source's swing (volts). Returns this stage's output swing.
  double Process(double vs)
  {
    const int m = mM, nu = mNu;
    // The sources: the input, each cap's history, the rail (see _BuildMaps).
    double u[kMaxNu];
    u[0] = vs;
    for (int c = 0; c < mNumCaps; c++)
      u[1 + c] = mCaps[c].ieq;
    u[nu - 1] = mDef.railV * mRailScale;
    // Reduced right-hand side for G, K, P.
    double r[3];
    for (int k = 0; k < 3; k++)
    {
      double acc = 0.0;
      for (int j = 0; j < nu; j++)
        acc += mMr[k][j] * u[j];
      r[k] = acc;
    }
    // Newton on G, K, P from a straight-line prediction.
    double* vn = mV + m;
    for (int k = 0; k < 3; k++)
    {
      const double v = vn[k];
      vn[k] += v - mVPrev[k];
      mVPrev[k] = v;
    }
    int it = 0;
    for (; it < kMaxIterations; it++)
    {
      const TriodeCurrents t = Triode(*mTables, vn[0] - vn[1], vn[2] - vn[1]);
      const double ip = t.ik - t.ig;
      double f[3];
      for (int k = 0; k < 3; k++)
        f[k] = mSr[k][0] * vn[0] + mSr[k][1] * vn[1] + mSr[k][2] * vn[2] - r[k];
      f[0] += t.ig;
      f[1] -= t.ik;
      f[2] += ip;
      double cfSlope = 0.0;
      if (mDef.follower)
      {
        double ig;
        mCf.Load(vn[2], ig, cfSlope);
        f[2] += ig;
      }
      const double a = t.dikDvgk, bb = t.dikDvpk, c = t.digDvgk;
      double j[3][3] = {{mSr[0][0] + c, mSr[0][1] - c, mSr[0][2]},
                        {mSr[1][0] - a, mSr[1][1] + a + bb, mSr[1][2] - bb},
                        {mSr[2][0] + a - c, mSr[2][1] + c - a - bb, mSr[2][2] + bb + cfSlope}};
      double dx[3];
      Solve3(j, f, dx);
      double step = 0.0;
      for (int k = 0; k < 3; k++)
      {
        const double d = std::clamp(dx[k], -100.0, 100.0);
        vn[k] += d;
        step = std::max(step, std::fabs(d));
      }
      if (step < kTolerance)
      {
        it++;
        break;
      }
    }
    mLastIterations = it;
    // The caps' new voltages, and the output node, straight from the sources and G, K, P.
    for (int c = 0; c < mNumCaps; c++)
    {
      const double* row = mMc[c];
      double acc = mDc[c][0] * vn[0] + mDc[c][1] * vn[1] + mDc[c][2] * vn[2];
      for (int j = 0; j < nu; j++)
        acc += row[j] * u[j];
      mCaps[c].Update(acc);
    }
    if (mOut < m)
    {
      double acc = mDo[0] * vn[0] + mDo[1] * vn[1] + mDo[2] * vn[2];
      for (int j = 0; j < nu; j++)
        acc += mMo[j] * u[j];
      mV[mOut] = acc;
    }
    return Output();
  }

  double Output() const
  {
    if (mDef.follower && mOut == mM + 2)
      return mCf.Output();
    return mV[mOut] - mVRest[mOut];
  }
  // The small-signal resistance the next stage sees (Thevenin).
  double OutputOhms() const { return mDef.follower && mOut == mM + 2 ? mCf.OutputOhms() : mRout; }
  double PlateSwing() const { return mV[mM + 2] - mVRest[mM + 2]; }
  double PlateVoltage() const { return mV[mM + 2]; }
  double RestingPlateVoltage() const { return mVRest[mM + 2]; }
  double CathodeVoltage() const { return mV[mM + 1]; }
  double GridCathodeVoltage() const { return mV[mM] - mV[mM + 1]; }
  double SupplyCurrent() const { return (mDef.railV * mRailScale - mV[mM + 2]) / mDef.raOhms; }
  int LastIterations() const { return mLastIterations; }
  bool HasFollower() const { return mDef.follower; }
  const StageDef& Def() const { return mDef; }

private:
  static constexpr int kMaxIterations = 10;
  // Newton stops once a step moves G, K, P by under 10 mV. Converging quadratically from the predictor, the
  // output is then within -82 dB (re its RMS) of a solve to 1e-7 V for every module, in ~1.1 iterations a
  // sample instead of ~1.95 at TriodeStageCircuit's 1e-4 V (measured, scratchpad mod/tol.cpp).
  static constexpr double kTolerance = 1e-2;

  int _Index(int node) const
  {
    if (node == GND)
      return -1;
    if (node >= G)
      return mM + (node - G);
    return mMap[std::clamp(node, 0, kMaxLinear - 1)];
  }

  void _AddCap(int a, int b, double c)
  {
    if (mNumCaps >= kMaxCaps || c <= 0.0)
      return;
    if (a < 0 && b >= 0)
      std::swap(a, b);
    if (a < 0)
      return;
    ModCap& cap = mCaps[mNumCaps++];
    cap = ModCap{};
    cap.a = a;
    cap.b = b;
    cap.c = c;
  }

  // The conductance matrix's constant part (resistors and cap conductances) for these knobs. dc: caps open.
  void _Stamp(double (&a)[kMaxN][kMaxN], const double* knobs, bool dc) const
  {
    for (int i = 0; i < mN; i++)
      for (int j = 0; j < mN; j++)
        a[i][j] = 0.0;
    auto stamp = [&](int p, int q, double g) {
      if (p >= 0)
        a[p][p] += g;
      if (q >= 0)
        a[q][q] += g;
      if (p >= 0 && q >= 0)
      {
        a[p][q] -= g;
        a[q][p] -= g;
      }
    };
    stamp(0, -1, 1.0 / mRsrc);
    for (int e = 0; e < mDef.numElems; e++)
    {
      const Elem& el = mDef.elems[e];
      if (el.type == kCap)
        continue;
      stamp(_Index(el.a), _Index(el.b), 1.0 / ElemOhms(el, knobs));
    }
    if (!dc)
      for (int c = 0; c < mNumCaps; c++)
        stamp(mCaps[c].a, mCaps[c].b == -2 ? -1 : mCaps[c].b, mCaps[c].g);
    stamp(mM + 1, -1, 1.0 / mDef.rkOhms);
    stamp(mM + 2, -1, 1.0 / mDef.raOhms);
    if (dc)
      for (int i = 0; i < mN; i++)
        a[i][i] += 1e-12; // Nodes only caps reach (none in practice) stay defined
  }

  void _SetKnobs(const double* knobs, bool force)
  {
    bool changed = force;
    for (int k = 0; k < kNumKnobs; k++)
    {
      const double v = knobs != nullptr ? knobs[k] : 0.5;
      if (v != mKnobs[k] && UsesKnob(k))
        changed = true;
      mKnobs[k] = v;
    }
    if (!changed)
      return;
    double a[kMaxN][kMaxN];
    _Stamp(a, mKnobs, false);
    const int m = mM;
    double all[kMaxLinear][kMaxLinear];
    for (int i = 0; i < m; i++)
      for (int j = 0; j < m; j++)
        all[i][j] = a[i][j];
    InvertMatrix<kMaxLinear>(all, mW, m);
    // X = W A_LN, Tr = A_NL W, Sr = A_NN - A_NL X.
    for (int i = 0; i < m; i++)
      for (int k = 0; k < 3; k++)
      {
        double acc = 0.0;
        for (int j = 0; j < m; j++)
          acc += mW[i][j] * a[j][m + k];
        mX[i][k] = acc;
      }
    for (int k = 0; k < 3; k++)
      for (int j = 0; j < m; j++)
      {
        double acc = 0.0;
        for (int i = 0; i < m; i++)
          acc += a[m + k][i] * mW[i][j];
        mTr[k][j] = acc;
      }
    for (int k = 0; k < 3; k++)
      for (int l = 0; l < 3; l++)
      {
        double acc = a[m + k][m + l];
        for (int i = 0; i < m; i++)
          acc -= a[m + k][i] * mX[i][l];
        mSr[k][l] = acc;
      }
    _BuildMaps();
  }

  // Per sample, the right-hand side is linear in the sources u = (the input, each cap's history current,
  // the rail voltage): b = Bu u. So the reduced right-hand side is Mr u (Mr = BuN - Tr BuL), and every
  // linear node is (W BuL) u - X vN: each cap's voltage and the output are one row over u plus three
  // terms in G, K, P. Built with the rest when a knob moves; it saves solving for every node each sample.
  void _BuildMaps()
  {
    const int m = mM, n = mN;
    mNu = mNumCaps + 2;
    const int nu = mNu;
    double bu[kMaxN][kMaxNu] = {};
    bu[0][0] = 1.0 / mRsrc;
    for (int c = 0; c < mNumCaps; c++)
    {
      const ModCap& cap = mCaps[c];
      bu[cap.a][1 + c] += 1.0;
      if (cap.b == -2)
        bu[cap.a][nu - 1] += cap.g; // A cap to the rail: i = g (va - rail) - ieq
      else if (cap.b >= 0)
        bu[cap.b][1 + c] -= 1.0;
    }
    bu[m + 2][nu - 1] += 1.0 / mDef.raOhms;
    for (int k = 0; k < 3; k++)
      for (int j = 0; j < nu; j++)
      {
        double acc = bu[m + k][j];
        for (int i = 0; i < m; i++)
          acc -= mTr[k][i] * bu[i][j];
        mMr[k][j] = acc;
      }
    double vl[kMaxLinear][kMaxNu];
    for (int i = 0; i < m; i++)
      for (int j = 0; j < nu; j++)
      {
        double acc = 0.0;
        for (int l = 0; l < m; l++)
          acc += mW[i][l] * bu[l][j];
        vl[i][j] = acc;
      }
    // A node's voltage as (row over u, row over G K P), with sign s added into (mu, mn).
    auto addNode = [&](int idx, double sgn, double* mu, double* mn) {
      if (idx == -1)
        return;
      if (idx == -2)
      {
        mu[nu - 1] += sgn;
        return;
      }
      if (idx < m)
      {
        for (int j = 0; j < nu; j++)
          mu[j] += sgn * vl[idx][j];
        for (int k = 0; k < 3; k++)
          mn[k] -= sgn * mX[idx][k];
        return;
      }
      mn[idx - m] += sgn;
    };
    for (int c = 0; c < mNumCaps; c++)
    {
      for (int j = 0; j < kMaxNu; j++)
        mMc[c][j] = 0.0;
      mDc[c][0] = mDc[c][1] = mDc[c][2] = 0.0;
      addNode(mCaps[c].a, 1.0, mMc[c], mDc[c]);
      addNode(mCaps[c].b, -1.0, mMc[c], mDc[c]);
    }
    for (int j = 0; j < kMaxNu; j++)
      mMo[j] = 0.0;
    mDo[0] = mDo[1] = mDo[2] = 0.0;
    if (mOut < m)
      addNode(mOut, 1.0, mMo, mDo);
    (void)n;
  }

  // The resting point (caps open, no signal), by Newton on the whole stage; then the caps charged to it.
  void _SolveDC()
  {
    const double rail = mDef.railV * mRailScale;
    if (mDef.follower)
      mCf.SetParts(mDef.tube, mDef.cfRkOhms, mDef.cfRsOhms, mDef.cfRailV * mRailScale, mDef.cfLoadOhms);
    double v[kMaxN] = {};
    // A starting guess: ~1 mA through Ra, the cathode a volt or two up.
    v[mM + 1] = 1.5;
    v[mM + 2] = std::max(rail - mDef.raOhms * 1e-3, rail * 0.5);
    for (int it = 0; it < 200; it++)
    {
      double a[kMaxN][kMaxN];
      _Stamp(a, mKnobs, true);
      double rhs[kMaxN] = {};
      rhs[mM + 2] += rail / mDef.raOhms;
      // f = A v - rhs + tube currents; J = A + tube derivatives.
      double f[kMaxN];
      for (int i = 0; i < mN; i++)
      {
        double acc = -rhs[i];
        for (int j = 0; j < mN; j++)
          acc += a[i][j] * v[j];
        f[i] = acc;
      }
      const int g = mM, k = mM + 1, p = mM + 2;
      const TriodeCurrents t = Triode(*mTables, v[g] - v[k], v[p] - v[k]);
      f[g] += t.ig;
      f[k] -= t.ik;
      f[p] += t.ik - t.ig;
      double cfSlope = 0.0;
      if (mDef.follower)
      {
        mCf.Rest(v[p]);
        double ig;
        mCf.LoadExact(v[p], ig, cfSlope);
        f[p] += ig;
      }
      const double da = t.dikDvgk, db = t.dikDvpk, dc = t.digDvgk;
      a[g][g] += dc;
      a[g][k] -= dc;
      a[k][g] -= da;
      a[k][p] -= db;
      a[k][k] += da + db;
      a[p][g] += da - dc;
      a[p][p] += db + cfSlope;
      a[p][k] += dc - da - db;
      double dx[kMaxN];
      for (int i = 0; i < mN; i++)
        dx[i] = -f[i];
      SolveDense<kMaxN>(a, dx, mN);
      double step = 0.0;
      for (int i = 0; i < mN; i++)
      {
        const double d = std::clamp(dx[i], -20.0, 20.0);
        v[i] += d;
        step = std::max(step, std::fabs(d));
      }
      if (step < 1e-10)
        break;
    }
    for (int i = 0; i < mN; i++)
      mV[i] = mVRest[i] = v[i];
    for (int k = 0; k < 3; k++)
      mVPrev[k] = v[mM + k];
    for (int c = 0; c < mNumCaps; c++)
    {
      ModCap& cap = mCaps[c];
      const double vb = cap.b == -2 ? rail : cap.b >= 0 ? v[cap.b] : 0.0;
      cap.SetResting(v[cap.a] - vb);
    }
    if (mDef.follower)
    {
      mCf.Rest(v[mM + 2]);
      mCf.BuildTable(_PlateMax());
    }
  }

  // The highest the plate can go: its rail (a stage in cutoff), with room for a Variac at its top.
  double _PlateMax() const { return 1.2 * mDef.railV * std::max(mRailScale, 1.0); }

  // The output node's small-signal resistance at 1 kHz (caps as their conductance there), the tube
  // linearized at rest: inject 1 A, read the voltage.
  void _ComputeOutputOhms()
  {
    double a[kMaxN][kMaxN];
    _Stamp(a, mKnobs, true);
    const double w = 2.0 * 3.14159265358979 * 1000.0;
    auto stamp = [&](int p, int q, double g) {
      if (p >= 0)
        a[p][p] += g;
      if (q >= 0)
        a[q][q] += g;
      if (p >= 0 && q >= 0)
      {
        a[p][q] -= g;
        a[q][p] -= g;
      }
    };
    for (int c = 0; c < mNumCaps; c++)
      stamp(mCaps[c].a, mCaps[c].b == -2 ? -1 : mCaps[c].b, w * mCaps[c].c);
    const int g = mM, k = mM + 1, p = mM + 2;
    const TriodeCurrents t = Triode(*mTables, mVRest[g] - mVRest[k], mVRest[p] - mVRest[k]);
    const double da = t.dikDvgk, db = t.dikDvpk, dc = t.digDvgk;
    a[g][g] += dc;
    a[g][k] -= dc;
    a[k][g] -= da;
    a[k][p] -= db;
    a[k][k] += da + db;
    a[p][g] += da - dc;
    a[p][p] += db;
    a[p][k] += dc - da - db;
    double rhs[kMaxN] = {};
    rhs[mOut] = 1.0;
    SolveDense<kMaxN>(a, rhs, mN);
    mRout = std::clamp(rhs[mOut], 100.0, 10e6);
  }

  static constexpr int kMaxCaps = kMaxElems + 4;
  static constexpr int kMaxNu = kMaxCaps + 2;
  StageDef mDef;
  const TriodeTables* mTables = &TriodeTables::Get();
  double mT = 1.0 / 192000.0, mRsrc = 1.0, mRailScale = 1.0, mRout = 50e3;
  int mM = 1, mN = 4, mOut = 3, mNumCaps = 0, mLastIterations = 0;
  int mMap[kMaxLinear] = {};
  ModCap mCaps[kMaxCaps];
  double mKnobs[kNumKnobs] = {-1.0, -1.0, -1.0, -1.0, -1.0};
  double mW[kMaxLinear][kMaxLinear] = {};
  double mX[kMaxLinear][3] = {};
  double mTr[3][kMaxLinear] = {};
  double mSr[3][3] = {};
  int mNu = 2;
  double mMr[3][kMaxNu] = {};
  double mMc[kMaxCaps][kMaxNu] = {}, mDc[kMaxCaps][3] = {};
  double mMo[kMaxNu] = {}, mDo[3] = {};
  double mV[kMaxN] = {}, mVRest[kMaxN] = {}, mVPrev[3] = {};
  ModFollower mCf;
};

// The output network after the last stage (a tone stack, a master pot, the next stage's load): linear,
// solved exactly each sample (the inverse is rebuilt when a knob moves).
class ModOutput
{
public:
  void Reset(const StageDef& d, double sampleRate, double rsrcOhms, const double* knobs)
  {
    mDef = d;
    mT = 1.0 / sampleRate;
    mRsrc = std::max(rsrcOhms, 1.0);
    mM = MapNodes(d, mMap);
    mOut = d.out >= 0 && d.out < kMaxLinear ? mMap[d.out] : 0;
    mNumCaps = 0;
    for (int e = 0; e < d.numElems; e++)
      if (d.elems[e].type == kCap && mNumCaps < kMaxCaps)
      {
        int a = d.elems[e].a, b = d.elems[e].b;
        if (a < 0)
          std::swap(a, b);
        if (a < 0 || a >= G || b >= G)
          continue;
        a = mMap[a];
        b = b >= 0 ? mMap[b] : -1;
        ModCap& cap = mCaps[mNumCaps++];
        cap = ModCap{};
        cap.a = a;
        cap.b = b;
        cap.c = d.elems[e].value;
        cap.Init(mT);
      }
    for (double& k : mKnobs)
      k = -1.0;
    mBuilt = false;
    SetKnobs(knobs);
    for (double& v : mV)
      v = 0.0;
  }

  bool UsesKnob(int k) const
  {
    for (int e = 0; e < mDef.numElems; e++)
      if (mDef.elems[e].type != kRes && mDef.elems[e].type != kCap && mDef.elems[e].knob == k)
        return true;
    return false;
  }

  void SetKnobs(const double* knobs)
  {
    bool changed = false;
    for (int k = 0; k < kNumKnobs; k++)
    {
      const double v = knobs != nullptr ? knobs[k] : 0.5;
      if (v != mKnobs[k] && UsesKnob(k))
        changed = true;
      mKnobs[k] = v;
    }
    if (!changed && mBuilt)
      return;
    double a[kMaxLinear][kMaxLinear] = {};
    auto stamp = [&](int p, int q, double g) {
      if (p >= 0)
        a[p][p] += g;
      if (q >= 0)
        a[q][q] += g;
      if (p >= 0 && q >= 0)
      {
        a[p][q] -= g;
        a[q][p] -= g;
      }
    };
    stamp(0, -1, 1.0 / mRsrc);
    for (int e = 0; e < mDef.numElems; e++)
    {
      const Elem& el = mDef.elems[e];
      if (el.type == kCap || el.a >= G || el.b >= G)
        continue;
      stamp(el.a >= 0 ? mMap[el.a] : -1, el.b >= 0 ? mMap[el.b] : -1, 1.0 / ElemOhms(el, mKnobs));
    }
    for (int c = 0; c < mNumCaps; c++)
      stamp(mCaps[c].a, mCaps[c].b, mCaps[c].g);
    for (int i = 0; i < mM; i++)
      a[i][i] += 1e-12;
    InvertMatrix<kMaxLinear>(a, mInv, mM);
    mBuilt = true;
  }

  double Process(double vs)
  {
    double b[kMaxLinear] = {};
    b[0] = vs / mRsrc;
    for (int c = 0; c < mNumCaps; c++)
    {
      b[mCaps[c].a] += mCaps[c].ieq;
      if (mCaps[c].b >= 0)
        b[mCaps[c].b] -= mCaps[c].ieq;
    }
    for (int i = 0; i < mM; i++)
    {
      double acc = 0.0;
      for (int j = 0; j < mM; j++)
        acc += mInv[i][j] * b[j];
      mV[i] = acc;
    }
    for (int c = 0; c < mNumCaps; c++)
      mCaps[c].Update(mV[mCaps[c].a] - (mCaps[c].b >= 0 ? mV[mCaps[c].b] : 0.0));
    return mV[mOut];
  }

private:
  static constexpr int kMaxCaps = kMaxElems;
  StageDef mDef;
  double mT = 1.0 / 192000.0, mRsrc = 1.0;
  int mM = 1, mOut = 0, mNumCaps = 0;
  int mMap[kMaxLinear] = {};
  bool mBuilt = false;
  ModCap mCaps[kMaxCaps];
  double mKnobs[kNumKnobs] = {-1.0, -1.0, -1.0, -1.0, -1.0};
  double mInv[kMaxLinear][kMaxLinear] = {};
  double mV[kMaxLinear] = {};
};

// What a module brings with it besides its preamp: the power amp of the amp it comes from.
struct PowerDef
{
  bool sixL6 = false;
  bool quad = true;
  double feedback = 0.5, presence = 0.5, master = 0.5, sag = 0.5; // 0..1, as TubeParams
  // 1.75.0: the amp's own tubes and iron where the Tube Lab's switches don't have them (0: as they are).
  int tube = kPowerStock;
  double bPlus = 0.0, raa = 0.0; // Plate supply; plate-to-plate load
  bool cathodeBias = false;
  const char* Name() const
  {
    return tube == kPower6V6 ? "6V6s" : tube == kPowerEL84 ? "EL84s" : sixL6 ? "6L6s" : "EL34s";
  }
  template <typename Params>
  void ApplyTo(Params& p) const
  {
    p.powerTube = tube;
    p.bPlus = bPlus;
    p.raa = raa;
    p.cathodeBias = p.cathodeBias || cathodeBias;
  }
};

struct ModuleDef
{
  const char* name = "";
  const char* family = ""; // BRIT, HG, CAL: the card's colour
  const char* description = "";
  int numStages = 0;
  StageDef stage[kMaxStages];
  StageDef output; // The output network (only its linear parts; its source is S)
  double sourceOhms = 100.0; // The guitar into the first stage's S
  const char* knobLabel[kNumKnobs] = {"Gain", nullptr, "Bass", "Middle", "Treble"};
  double knobDefault[kNumKnobs] = {0.5, 0.5, 0.5, 0.5, 0.5};
  PowerDef power;
  // Whether it needs TubeStage's higher oversampling (8x at 44.1/48 kHz) to keep its aliasing down: the
  // high-gain ones do (at 4x, a high E aliased at -34 to -51 dB re its harmonics; at 8x -44 to -62). The
  // others alias under -53 dB at 4x (as the Tube Lab's single stage does), and run there for half the CPU.
  bool needs8x = true;
  // Output trim, measured, not a part of the circuit: each module at its default knobs, with its own power
  // amp and a speaker load, comes out of the Tube Lab within 0.2 dB of the Tube Lab's own preamp on the same
  // settings (a two-stage, hot setting at Drive 20 dB), so A/B flips and swaps don't jump in level.
  double levelDB = 0.0;
  // 1.75.0: level after the power amp. levelDB is before it, so it changes how hard the amp is driven: the
  // low-gain modules leave that as their circuits have it and are matched here instead.
  double outputDB = 0.0;
  bool UsesKnob(int k) const { return knobLabel[k] != nullptr; }
};

// A module running: its stages in a chain, then the output network.
class ModuleChain
{
public:
  // From rest. knobs: kNumKnobs values 0..1.
  void Reset(const ModuleDef& def, double sampleRate, const double* knobs)
  {
    mDef = &def;
    mNumStages = std::clamp(def.numStages, 0, kMaxStages);
    double rsrc = def.sourceOhms;
    for (int s = 0; s < mNumStages; s++)
    {
      mStage[s].Reset(def.stage[s], sampleRate, rsrc, knobs);
      rsrc = mStage[s].OutputOhms();
    }
    mOutput.Reset(def.output, sampleRate, rsrc, knobs);
    mScale = std::pow(10.0, def.levelDB / 20.0) / kVoltsRef;
    mRailScale = 1.0;
  }

  void SetKnobs(const double* knobs)
  {
    for (int s = 0; s < mNumStages; s++)
      mStage[s].SetKnobs(knobs);
    mOutput.SetKnobs(knobs);
  }

  void SetRailScale(double s)
  {
    if (s == mRailScale)
      return;
    mRailScale = s;
    for (int i = 0; i < mNumStages; i++)
      mStage[i].SetRailScale(s);
  }

  // vin: volts at the input jack. Returns the preamp's output in the Tube Lab's preamp units (what the
  // power amp takes).
  double Process(double vin)
  {
    double x = vin;
    for (int s = 0; s < mNumStages; s++)
      x = mStage[s].Process(x);
    return mOutput.Process(x) * mScale;
  }

  int NumStages() const { return mNumStages; }
  const ModStage& Stage(int s) const { return mStage[s]; }
  const ModuleDef* Def() const { return mDef; }
  double SupplyCurrent() const
  {
    double i = 0.0;
    for (int s = 0; s < mNumStages; s++)
      i += mStage[s].SupplyCurrent();
    return i;
  }

  // Volts out of the output network for one unit into the power amp.
  static constexpr double kVoltsRef = 20.0;

private:
  const ModuleDef* mDef = nullptr;
  int mNumStages = 0;
  ModStage mStage[kMaxStages];
  ModOutput mOutput;
  double mScale = 1.0, mRailScale = 1.0;
};
} // namespace module
} // namespace tube
