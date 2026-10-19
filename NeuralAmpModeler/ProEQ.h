#pragma once

// PRO EQ (1.46.0; band types, slopes and Trim 1.47.0): an 8-band parametric EQ, used twice -- as the
// PRO EQ block on the grid and as the Output EQ at the end of the chain (its own page and the spectrum
// strip on the main window). The UI draws its response from ResponseDB(), the same biquads the audio
// runs, so the curve you see is the curve you get.
//
// Each band has On, Type, Slope, Freq, Gain (cut/boost) and Q. Types: Bell, Low shelf, High shelf,
// Low cut, High cut, Notch; any band can be any type (they default, left to right, to a low cut, a
// low shelf, four bells, a high shelf and a high cut). Slope is for the cuts: 12, 24, 36 or 48 dB/oct
// (1 to 4 Butterworth sections; Q is the resonance of the sharpest one, 0.707 = plain Butterworth).
// A notch is a 40 dB-deep bell (its width is Q; Gain doesn't apply).
//
// Each section is an RBJ biquad (bilinear, prewarped at its frequency). Knob moves glide (one-pole on
// log frequency, gain and log Q, ~20 ms) and the coefficients follow every kCoefEvery samples, so
// dragging a node doesn't zipper or click. A band switched off fades to neutral (no gain, or a cut
// slid off the end of the range) before it's bypassed; a change of type or slope does the same, then
// fades the new shape in. Trim (before the bands) and Output (after) glide too.
//
// Plain C++ (no iPlug2), so it can be tested on its own.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace proeq
{
constexpr int kNumBands = 8;
constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
constexpr double kMaxGainDB = 18.0;
constexpr double kMinQ = 0.1, kMaxQ = 18.0;
constexpr double kNotchDepthDB = 40.0;
constexpr int kMaxSections = 4; // A 48 dB/oct cut
constexpr int kNumSlopes = 4;

// The order of the Type parameter's list.
enum class Type
{
  Bell = 0,
  LowShelf,
  HighShelf,
  LowCut,
  HighCut,
  Notch
};
constexpr int kNumTypes = 6;

inline const char* TypeName(Type t)
{
  switch (t)
  {
    case Type::LowShelf: return "Low shelf";
    case Type::HighShelf: return "High shelf";
    case Type::LowCut: return "Low cut";
    case Type::HighCut: return "High cut";
    case Type::Notch: return "Notch";
    default: return "Bell";
  }
}
inline bool HasGain(Type t) { return t == Type::Bell || t == Type::LowShelf || t == Type::HighShelf; }
inline bool IsCut(Type t) { return t == Type::LowCut || t == Type::HighCut; }
inline int SlopeDBPerOct(int slope) { return 12 * (std::clamp(slope, 0, kNumSlopes - 1) + 1); }

// The type each band starts as (and loads as from a state saved before types existed).
inline Type DefaultType(int band)
{
  switch (band)
  {
    case 0: return Type::LowCut;
    case 1: return Type::LowShelf;
    case kNumBands - 2: return Type::HighShelf;
    case kNumBands - 1: return Type::HighCut;
    default: return Type::Bell;
  }
}

struct Band
{
  bool on = false;
  Type type = Type::Bell;
  int slope = 0; // 0..3 = 12..48 dB/oct (cuts)
  double freq = 1000.0, gainDB = 0.0, q = 0.707;
  double amount = 1.0; // Internal (gliding): 1 = the full effect, 0 = neutral
};

// Defaults: everything off and flat, the bands spread where guitar EQ moves usually happen.
inline Band DefaultBand(int band)
{
  static const double kFreqs[kNumBands] = {80.0, 120.0, 250.0, 700.0, 1800.0, 4000.0, 6000.0, 9000.0};
  Band b;
  b.type = DefaultType(band);
  b.freq = kFreqs[band];
  b.q = b.type == Type::Bell ? 1.0 : 0.707;
  return b;
}

struct Params
{
  std::array<Band, kNumBands> bands;
  double trimDB = 0.0, outputDB = 0.0;
  Params()
  {
    for (int i = 0; i < kNumBands; i++)
      bands[i] = DefaultBand(i);
  }
};

struct Coefs
{
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; // a0 normalized to 1
};

enum class Kind
{
  Peak,
  LowShelf,
  HighShelf,
  HighPass,
  LowPass
};

// RBJ cookbook biquad.
inline Coefs Biquad(Kind kind, double freq, double gainDB, double q, double sampleRate)
{
  const double kPi = 3.14159265358979323846;
  freq = std::clamp(freq, 1.0, 0.49 * sampleRate);
  q = std::clamp(q, 0.05, 100.0);
  const double w = 2.0 * kPi * freq / sampleRate, cw = std::cos(w), sw = std::sin(w);
  const double alpha = sw / (2.0 * q);
  const double A = std::pow(10.0, gainDB / 40.0);
  double b0, b1, b2, a0, a1, a2;
  switch (kind)
  {
    case Kind::HighPass:
      b0 = (1.0 + cw) * 0.5;
      b1 = -(1.0 + cw);
      b2 = b0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cw;
      a2 = 1.0 - alpha;
      break;
    case Kind::LowPass:
      b0 = (1.0 - cw) * 0.5;
      b1 = 1.0 - cw;
      b2 = b0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cw;
      a2 = 1.0 - alpha;
      break;
    case Kind::LowShelf:
    case Kind::HighShelf:
    {
      const double sA = 2.0 * std::sqrt(A) * alpha;
      const double s = kind == Kind::LowShelf ? 1.0 : -1.0;
      b0 = A * ((A + 1.0) - s * (A - 1.0) * cw + sA);
      b1 = s * 2.0 * A * ((A - 1.0) - s * (A + 1.0) * cw);
      b2 = A * ((A + 1.0) - s * (A - 1.0) * cw - sA);
      a0 = (A + 1.0) + s * (A - 1.0) * cw + sA;
      a1 = -s * 2.0 * ((A - 1.0) + s * (A + 1.0) * cw);
      a2 = (A + 1.0) + s * (A - 1.0) * cw - sA;
      break;
    }
    default: // Peak
      b0 = 1.0 + alpha * A;
      b1 = -2.0 * cw;
      b2 = 1.0 - alpha * A;
      a0 = 1.0 + alpha / A;
      a1 = -2.0 * cw;
      a2 = 1.0 - alpha / A;
      break;
  }
  Coefs c;
  c.b0 = b0 / a0;
  c.b1 = b1 / a0;
  c.b2 = b2 / a0;
  c.a1 = a1 / a0;
  c.a2 = a2 / a0;
  return c;
}

struct Sections
{
  int n = 1;
  Coefs c[kMaxSections];
};

// A band as cascaded biquads.
inline Sections Design(const Band& b, double sampleRate)
{
  Sections s;
  const double q = std::clamp(b.q, kMinQ, kMaxQ);
  switch (b.type)
  {
    case Type::LowCut:
    case Type::HighCut:
    {
      // Butterworth of order 2n: sections with Q_k = 1 / (2 cos((2k - 1) pi / 4n)). The sharpest
      // (last) one carries the resonance: its Q scaled by Q / 0.707.
      s.n = std::clamp(b.slope, 0, kNumSlopes - 1) + 1;
      const double kPi = 3.14159265358979323846;
      for (int k = 0; k < s.n; k++)
      {
        double qk = 1.0 / (2.0 * std::cos((2.0 * k + 1.0) * kPi / (4.0 * s.n)));
        if (k == s.n - 1)
          qk *= q / 0.70710678118654752;
        s.c[k] = Biquad(b.type == Type::LowCut ? Kind::HighPass : Kind::LowPass, b.freq, 0.0, qk, sampleRate);
      }
      break;
    }
    case Type::LowShelf: s.c[0] = Biquad(Kind::LowShelf, b.freq, b.gainDB * b.amount, q, sampleRate); break;
    case Type::HighShelf: s.c[0] = Biquad(Kind::HighShelf, b.freq, b.gainDB * b.amount, q, sampleRate); break;
    case Type::Notch: s.c[0] = Biquad(Kind::Peak, b.freq, -kNotchDepthDB * b.amount, q, sampleRate); break;
    default: s.c[0] = Biquad(Kind::Peak, b.freq, b.gainDB * b.amount, q, sampleRate); break;
  }
  return s;
}

// |H| of one biquad at hz.
inline double Magnitude(const Coefs& c, double hz, double sampleRate)
{
  const double w = 2.0 * 3.14159265358979323846 * hz / sampleRate;
  const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
  return std::abs((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
}

// Where a band is "off" for gliding: no effect (amount 0), or a cut slid out of the way.
inline Band Neutral(const Band& b)
{
  Band n = b;
  if (b.type == Type::LowCut)
    n.freq = 5.0;
  else if (b.type == Type::HighCut)
    n.freq = 40000.0;
  else
    n.amount = 0.0;
  return n;
}

inline double BandResponseDB(const Band& b, double hz, double sampleRate)
{
  const Sections s = Design(b, sampleRate);
  double mag = 1.0;
  for (int k = 0; k < s.n; k++)
    mag *= Magnitude(s.c[k], hz, sampleRate);
  return 20.0 * std::log10(std::max(mag, 1e-12));
}

// The response of a set of bands, in dB, at hz (Trim and Output included).
inline double ResponseDB(const Params& p, double hz, double sampleRate)
{
  double db = p.trimDB + p.outputDB;
  for (int i = 0; i < kNumBands; i++)
    if (p.bands[i].on)
      db += BandResponseDB(p.bands[i], hz, sampleRate);
  return db;
}

class EQ
{
public:
  static constexpr int kMaxChannels = 2;
  static constexpr int kCoefEvery = 16;

  void Reset(double sampleRate)
  {
    mSampleRate = sampleRate;
    mGlide = 1.0 - std::exp(-kCoefEvery / (0.02 * sampleRate));
    for (int i = 0; i < kNumBands; i++)
    {
      mState[i] = _Live(mTarget.bands[i]);
      mLive[i] = mTarget.bands[i].on;
      mSec[i] = Design(mState[i], sampleRate);
    }
    mTrimGain = mTrimTarget = std::pow(10.0, mTarget.trimDB / 20.0);
    mOutGain = mOutTarget = std::pow(10.0, mTarget.outputDB / 20.0);
    for (auto& ch : mZ)
      for (auto& band : ch)
        for (auto& z : band)
          z[0] = z[1] = 0.0;
    mCount = 0;
  }

  void SetParams(const Params& p)
  {
    mTarget = p;
    for (auto& b : mTarget.bands)
      b.amount = 1.0;
    mTrimTarget = std::pow(10.0, p.trimDB / 20.0);
    mOutTarget = std::pow(10.0, p.outputDB / 20.0);
  }

  // Is anything in the signal path (a band on or still gliding off, or a gain away from unity)?
  bool Active() const
  {
    for (int i = 0; i < kNumBands; i++)
      if (mLive[i] || mTarget.bands[i].on) // (A band just switched on goes live in Process)
        return true;
    return mOutGain != 1.0 || mOutTarget != 1.0 || mTrimGain != 1.0 || mTrimTarget != 1.0;
  }

  // In place, numChannels (1 or 2) x numFrames.
  template <class T>
  void Process(T** io, int numChannels, int numFrames)
  {
    numChannels = std::min(numChannels, kMaxChannels);
    for (int s = 0; s < numFrames; s++)
    {
      if (mCount-- <= 0)
      {
        mCount = kCoefEvery - 1;
        _Glide();
      }
      mOutGain += 0.002 * (mOutTarget - mOutGain);
      if (std::fabs(mOutGain - mOutTarget) < 1e-9)
        mOutGain = mOutTarget;
      mTrimGain += 0.002 * (mTrimTarget - mTrimGain);
      if (std::fabs(mTrimGain - mTrimTarget) < 1e-9)
        mTrimGain = mTrimTarget;
      for (int c = 0; c < numChannels; c++)
      {
        double x = io[c][s] * mTrimGain;
        for (int i = 0; i < kNumBands; i++)
        {
          if (!mLive[i])
            continue;
          for (int k = 0; k < mSec[i].n; k++)
          {
            const Coefs& q = mSec[i].c[k];
            double* z = mZ[c][i][k];
            // Transposed direct form II.
            const double y = q.b0 * x + z[0];
            z[0] = q.b1 * x - q.a1 * y + z[1];
            z[1] = q.b2 * x - q.a2 * y;
            x = y;
          }
        }
        io[c][s] = static_cast<T>(x * mOutGain);
      }
    }
  }

private:
  // Where a band glides to: its setting if on, else neutral.
  static Band _Live(const Band& b) { return b.on ? b : Neutral(b); }
  static bool _Near(const Band& s, const Band& t)
  {
    return std::fabs(std::log(s.freq / t.freq)) < 1e-4 && std::fabs(s.gainDB - t.gainDB) < 1e-3
           && std::fabs(std::log(s.q / t.q)) < 1e-4 && std::fabs(s.amount - t.amount) < 1e-4;
  }
  void _ClearBand(int i)
  {
    for (auto& ch : mZ)
      for (auto& z : ch[i])
        z[0] = z[1] = 0.0;
  }

  void _Glide()
  {
    for (int i = 0; i < kNumBands; i++)
    {
      const Band& target = mTarget.bands[i];
      Band& s = mState[i];
      if (!mLive[i])
      {
        if (!target.on)
        {
          s = Neutral(target); // Off and settled: follow the knobs silently
          continue;
        }
        // Switched on: start from neutral, in its own type, and glide in.
        s = Neutral(target);
        _ClearBand(i);
        mLive[i] = true;
      }
      // A new type or slope: glide to neutral in the old shape first, then swap and glide in.
      const bool reshape = s.type != target.type || s.slope != target.slope;
      const Band goal = reshape ? Neutral(s) : _Live(target);
      if (_Near(s, goal))
      {
        if (reshape)
        {
          s = Neutral(target);
          _ClearBand(i);
        }
        else
        {
          s = goal;
          if (!target.on)
          {
            mLive[i] = false; // Faded all the way out: bypass
            _ClearBand(i);
            continue;
          }
        }
      }
      else
      {
        s.freq = std::exp(std::log(s.freq) + mGlide * (std::log(goal.freq) - std::log(s.freq)));
        s.gainDB += mGlide * (goal.gainDB - s.gainDB);
        s.q = std::exp(std::log(s.q) + mGlide * (std::log(goal.q) - std::log(s.q)));
        s.amount += mGlide * (goal.amount - s.amount);
      }
      mSec[i] = Design(s, mSampleRate);
    }
  }

  double mSampleRate = 48000.0, mGlide = 0.1;
  Params mTarget;
  Band mState[kNumBands];
  bool mLive[kNumBands] = {};
  Sections mSec[kNumBands];
  double mZ[kMaxChannels][kNumBands][kMaxSections][2] = {};
  double mTrimGain = 1.0, mTrimTarget = 1.0, mOutGain = 1.0, mOutTarget = 1.0;
  int mCount = 0;
};
} // namespace proeq
