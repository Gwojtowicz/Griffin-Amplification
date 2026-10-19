#pragma once

// The passive Bass / Mid / Treble tone stack of Fender and Marshall amps (1.20.0), simulated from
// its real parts -- the circuit analyzed in D. T. Yeh and J. O. Smith, "Discretization of the '59
// Fender Bassman Tone Stack" (DAFx 2006). In an amp it sits between the preamp and the power amp,
// so the knobs change what drives the power tubes, and they interact (the famous mid scoop moves
// as you turn them) because they're one network, not three separate filters.
//
//   in --C1-- A                     A = top of the Treble pot, O = its wiper (the output)
//             |
//          Treble pot (t)
//             |
//             O ----------- out (into the next stage, Rload)
//             |
//          Treble pot (1-t)
//             |
//   in --Rslope-- C --C2-- B        B = bottom of the Treble pot = top of the Bass pot
//                 |        |
//                 |     Bass pot (rheostat, l * Rbass)
//                 |        |
//                 +--C3--- D        D = top of the Mid pot
//                          |
//                       Mid pot (rheostat, m * Rmid)
//                          |
//                         gnd
//
// Linear, so it's solved exactly each sample: the capacitors are trapezoidal companions (the same
// method as TubeCircuit.h, equivalent to the bilinear transform), and the 5x5 nodal matrix is
// inverted only when a knob moves. A knob turn changes the resistors but not the capacitors'
// charge, so it doesn't click -- like turning a real pot.
//
// Plain C++ (no iPlug2), so it can be tested on its own.

#include <algorithm>
#include <cmath>
#include <complex>

namespace tonestack
{
struct Components
{
  double c1, c2, c3; // Treble, bass, mid caps (farads)
  double rTreble, rBass, rMid, rSlope; // Pots (full value) and the slope resistor (ohms)
  bool trebleAudioTaper; // Treble pot: audio (log) taper, or linear
};

// Standard values (as in Duncan's Tone Stack Calculator and the Yeh & Smith paper). Fender used
// audio-taper treble pots; Marshall's treble pot is linear, so its noon is brighter.
constexpr Components kFender = {250e-12, 20e-9, 20e-9, 250e3, 1e6, 25e3, 56e3, true}; // '59 Bassman 5F6-A
constexpr Components kMarshall = {470e-12, 22e-9, 22e-9, 220e3, 1e6, 25e3, 33e3, false}; // JCM800 / Plexi
constexpr double kLoadOhms = 1e6; // The next stage's grid leak

// Pot tapers: Treble and Bass are audio (log) pots -- about 10% of the resistance at the middle of
// the rotation -- and Mid is linear.
inline double AudioTaper(double position01)
{
  const double p = std::clamp(position01, 0.0, 1.0);
  return (std::exp(4.6 * p) - 1.0) / (std::exp(4.6) - 1.0);
}

class TMBStack
{
public:
  // knobs are 0..1 (noon = 0.5).
  void Reset(double sampleRate, const Components& parts, double bass, double mid, double treble)
  {
    mParts = parts;
    mT = 1.0 / sampleRate;
    mC1 = Cap{};
    mC2 = Cap{};
    mC3 = Cap{};
    mC1.Init(parts.c1, mT);
    mC2.Init(parts.c2, mT);
    mC3.Init(parts.c3, mT);
    mBass = mMid = mTreble = -1.0;
    SetKnobs(bass, mid, treble);
    mMakeup = 1.0 / std::abs(Response(1000.0, 0.5, 0.5, 0.5));
  }

  void SetKnobs(double bass, double mid, double treble)
  {
    if (bass == mBass && mid == mMid && treble == mTreble)
      return;
    mBass = bass;
    mMid = mid;
    mTreble = treble;
    double g[kN][kN] = {};
    _Stamp(g, _Resistors(bass, mid, treble), mC1.g, mC2.g, mC3.g);
    _Invert(g, mInv);
  }

  // One sample. Returns the stack's output times the makeup gain: the stack loses a lot of level
  // (a real amp makes it up in the next stage), so the output is scaled so that a 1 kHz tone at
  // noon on all three knobs comes out at the level it went in. Off noon, the level changes as on
  // the real amp.
  double Process(double vin)
  {
    // Sources: the input through C1 (into A) and through Rslope (into C), and the cap histories.
    // A cap from node p to node q (v = Vp - Vq) carries i = g*v - ieq from p to q, so its history
    // term goes on the right-hand side as +ieq at p and -ieq at q. (Only the fixed slope resistor is
    // needed here: before 1.45.0 every sample re-derived all the pot resistances, two exp() calls.)
    double s[kN] = {};
    s[kA] += mC1.g * vin - mC1.ieq; // C1: from the input (p) to A (q)
    s[kC] += vin / mParts.rSlope;
    s[kC] += mC2.ieq + mC3.ieq; // C2 and C3: from C (p) to B and D (q)
    s[kB] -= mC2.ieq;
    s[kD] -= mC3.ieq;
    double v[kN];
    for (int i = 0; i < kN; i++)
    {
      double acc = 0.0;
      for (int j = 0; j < kN; j++)
        acc += mInv[i][j] * s[j];
      v[i] = acc;
    }
    mC1.Update(vin - v[kA]);
    mC2.Update(v[kC] - v[kB]);
    mC3.Update(v[kC] - v[kD]);
    return v[kO] * mMakeup;
  }

  double MakeupGain() const { return mMakeup; }

  // The exact frequency response of the same circuit (no discretization): for checking the
  // per-sample simulation, and for plots.
  std::complex<double> Response(double hz, double bass, double mid, double treble) const
  {
    using cd = std::complex<double>;
    const cd jw(0.0, 2.0 * 3.141592653589793 * hz);
    const Resistors r = _Resistors(bass, mid, treble);
    cd g[kN][kN] = {};
    auto stamp = [&](int a, int b, cd y) {
      if (a >= 0)
        g[a][a] += y;
      if (b >= 0)
        g[b][b] += y;
      if (a >= 0 && b >= 0)
      {
        g[a][b] -= y;
        g[b][a] -= y;
      }
    };
    stamp(kA, kO, 1.0 / r.trebleTop);
    stamp(kO, kB, 1.0 / r.trebleBottom);
    stamp(kB, kD, 1.0 / r.bass);
    stamp(kD, -1, 1.0 / r.mid);
    stamp(kO, -1, 1.0 / kLoadOhms);
    stamp(kC, kB, jw * mParts.c2);
    stamp(kC, kD, jw * mParts.c3);
    // Elements to the input (a known 1 V source): their admittance on the diagonal, the source on
    // the right-hand side.
    g[kA][kA] += jw * mParts.c1;
    g[kC][kC] += 1.0 / r.slope;
    cd s[kN] = {};
    s[kA] = jw * mParts.c1;
    s[kC] = 1.0 / r.slope;
    // Gaussian elimination.
    for (int col = 0; col < kN; col++)
    {
      int piv = col;
      for (int row = col + 1; row < kN; row++)
        if (std::abs(g[row][col]) > std::abs(g[piv][col]))
          piv = row;
      for (int k = 0; k < kN; k++)
        std::swap(g[col][k], g[piv][k]);
      std::swap(s[col], s[piv]);
      for (int row = col + 1; row < kN; row++)
      {
        const cd f = g[row][col] / g[col][col];
        for (int k = col; k < kN; k++)
          g[row][k] -= f * g[col][k];
        s[row] -= f * s[col];
      }
    }
    cd v[kN];
    for (int row = kN - 1; row >= 0; row--)
    {
      cd acc = s[row];
      for (int k = row + 1; k < kN; k++)
        acc -= g[row][k] * v[k];
      v[row] = acc / g[row][row];
    }
    return v[kO];
  }

private:
  enum Node
  {
    kA = 0,
    kO,
    kB,
    kC,
    kD,
    kN
  };
  // A pot end can't be exactly 0 ohms in a nodal solve; a few ohms is a real pot's end resistance.
  static constexpr double kMinOhms = 10.0;

  struct Resistors
  {
    double trebleTop, trebleBottom, bass, mid, slope;
  };
  Resistors _Resistors(double bass, double mid, double treble) const
  {
    const double t = mParts.trebleAudioTaper ? AudioTaper(treble) : std::clamp(treble, 0.0, 1.0);
    Resistors r;
    r.trebleTop = std::max((1.0 - t) * mParts.rTreble, kMinOhms);
    r.trebleBottom = std::max(t * mParts.rTreble, kMinOhms);
    r.bass = std::max(AudioTaper(bass) * mParts.rBass, kMinOhms);
    r.mid = std::max(std::clamp(mid, 0.0, 1.0) * mParts.rMid, kMinOhms);
    r.slope = mParts.rSlope;
    return r;
  }

  void _Stamp(double (&g)[kN][kN], const Resistors& r, double g1, double g2, double g3) const
  {
    auto stamp = [&](int a, int b, double y) {
      if (a >= 0)
        g[a][a] += y;
      if (b >= 0)
        g[b][b] += y;
      if (a >= 0 && b >= 0)
      {
        g[a][b] -= y;
        g[b][a] -= y;
      }
    };
    stamp(kA, kO, 1.0 / r.trebleTop);
    stamp(kO, kB, 1.0 / r.trebleBottom);
    stamp(kB, kD, 1.0 / r.bass);
    stamp(kD, -1, 1.0 / r.mid);
    stamp(kO, -1, 1.0 / kLoadOhms);
    stamp(kC, kB, g2);
    stamp(kC, kD, g3);
    g[kA][kA] += g1; // C1 to the input
    g[kC][kC] += 1.0 / r.slope; // Rslope to the input
  }

  static void _Invert(double (&a)[kN][kN], double (&inv)[kN][kN])
  {
    double m[kN][2 * kN];
    for (int i = 0; i < kN; i++)
      for (int j = 0; j < kN; j++)
      {
        m[i][j] = a[i][j];
        m[i][kN + j] = i == j ? 1.0 : 0.0;
      }
    for (int col = 0; col < kN; col++)
    {
      int piv = col;
      for (int row = col + 1; row < kN; row++)
        if (std::fabs(m[row][col]) > std::fabs(m[piv][col]))
          piv = row;
      for (int k = 0; k < 2 * kN; k++)
        std::swap(m[col][k], m[piv][k]);
      const double d = m[col][col];
      for (int k = 0; k < 2 * kN; k++)
        m[col][k] /= d;
      for (int row = 0; row < kN; row++)
      {
        if (row == col)
          continue;
        const double f = m[row][col];
        for (int k = 0; k < 2 * kN; k++)
          m[row][k] -= f * m[col][k];
      }
    }
    for (int i = 0; i < kN; i++)
      for (int j = 0; j < kN; j++)
        inv[i][j] = m[i][kN + j];
  }

  // Trapezoidal companion: i = g * v - ieq (current through the cap, in the direction of v).
  struct Cap
  {
    double g = 0.0, ieq = 0.0;
    void Init(double c, double t) { g = 2.0 * c / t; }
    void Update(double v)
    {
      const double i = g * v - ieq;
      ieq = g * v + i;
    }
  };

  Components mParts = kFender;
  double mT = 1.0 / 48000.0;
  Cap mC1, mC2, mC3;
  double mBass = -1.0, mMid = -1.0, mTreble = -1.0;
  double mInv[kN][kN] = {};
  double mMakeup = 1.0;
};
} // namespace tonestack
