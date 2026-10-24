#pragma once

// CAB LAB: speaker cabinet, close mics and room, mono in -> stereo out.
//
//   in --> speaker (a modeled speaker/cab, or an IR) --+--> Mic A: type EQ, cone position, proximity
//                                                      |          effect, distance (delay + level) --> pan --+
//                                                      +--> Mic B: the same, own settings (+ polarity) --> pan --+--> L/R
//                                                      +--> room: predelay, early reflections, diffuse
//                                                                 tail, through a stereo room-mic pair -------+
//
// * Speakers: 6 modeled speaker/cab combinations, each a filter set shaped after the manufacturer's
//   published frequency-response chart (cab resonance and low end, the speaker's mid/presence
//   peaks, and its high-frequency roll-off). These are approximations of the spec charts, not
//   measurements. Or the speaker can be an IR (a measured cab, which already includes whatever mic
//   recorded it).
// * Mics: 6 types, each with its published frequency response (approximated with filters), polar
//   pattern and proximity effect: SM57, SM7B, Beta 57A, R-121-style ribbon, MD421, U87-style
//   condenser.
// * Physics of placement: distance is a real delay (d / 343 m/s), so two mics at different
//   distances comb-filter against each other the way real ones do; level falls off with distance;
//   the proximity effect's bass boost grows as the mic gets closer (more for figure-8, less for
//   cardioid); cone position goes from the bright centre (dust cap) to the darker edge, and a
//   farther mic hears more of the whole cone, so position matters less.
// * Room: a stereo pair of room mics hearing early reflections and a diffuse tail whose size,
//   delay and decay follow Room Size; Width sets how wide the room pair is.
//
// SPEAKER LAB (1.59.0): CAB LAB's 3D placement mode (lab3D; older states keep the classic mics above,
// unchanged). The cab has a format -- 4x12, 2x12, 1x12, 4x10 -- with its cones where they really sit
// on the baffle, and up to four mics, each placed in X/Y (across the baffle, cm from its centre) and
// Z (cm out from the grille cloth). Each mic hears every cone separately:
// * Distance: from the cone's dust cap (recessed a little behind the grille) to the mic, a real delay
//   (time of flight) and a level that falls off with it like a vibrating piston's: flat-ish within
//   the cone's own radius (the near field), then 6 dB per doubling (sqrt(d^2 + a^2) - d, the on-axis
//   pressure of a piston of radius a). So a close mic hears its own cone ~15 dB over the next one. A mic between two cones hears both at slightly
//   different times, so they comb-filter against each other the way real ones do; far back, all of
//   a 4x12's cones arrive together.
// * Where it points on the cone: one number, s = r / sqrt(r^2 + (z + a)^2) (r = sideways offset from
//   that cone's centre, a = the cone's radius), from 0 on the dust cap to ~0.7 at the edge up close,
//   and the sine of the off-axis angle far away. It sets a treble shelf: the bright cap, the darker
//   edge, the cone's beaming off axis. (Up close it gives the classic Position's range: +2 to -9 dB.)
// * The mic's own polar pattern, aimed straight at the baffle: its sensitivity A + (1 - A) cos(angle)
//   (cardioid A = 0.5, supercardioid 0.37, hypercardioid 0.25, figure-8 0) and its off-axis treble loss.
// * Proximity effect: from the nearest cone, as in the classic mode.
// * The cones aren't a perfectly matched set: each has a slightly different level and time (a few
//   tens of microseconds), as real speakers do.
// * Dual cabs (1.61.0): a second cab beside the first, with its own format, speaker voicing and source
//   (the model or its own IR). The same signal feeds both; each mic is on one of them (its Cab), placed
//   in that cab's coordinates. Single, every mic is on cab 1.
// Placement moves glide (recomputed every kLabTickSamples and ramped in between), so dragging a mic
// doesn't zipper or click. Cost: one delay read and one one-pole filter per mic per cone, plus each
// mic's EQ (as in the classic mode); mics that are off cost nothing.

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace cab
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kSpeedOfSound = 343.0;

// RBJ cookbook biquad, transposed direct form II.
struct Biquad
{
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

  double Process(double x)
  {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void Clear() { z1 = z2 = 0.0; }

  static Biquad Make(double b0, double b1, double b2, double a0, double a1, double a2)
  {
    Biquad q;
    q.b0 = b0 / a0;
    q.b1 = b1 / a0;
    q.b2 = b2 / a0;
    q.a1 = a1 / a0;
    q.a2 = a2 / a0;
    return q;
  }
  static Biquad Peak(double fs, double f, double q, double db)
  {
    const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * f / fs, al = std::sin(w) / (2 * q);
    return Make(1 + al * A, -2 * std::cos(w), 1 - al * A, 1 + al / A, -2 * std::cos(w), 1 - al / A);
  }
  static Biquad LowShelf(double fs, double f, double q, double db)
  {
    const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
    const double s = 2 * std::sqrt(A) * al;
    return Make(A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
                (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
  }
  static Biquad HighShelf(double fs, double f, double q, double db)
  {
    const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
    const double s = 2 * std::sqrt(A) * al;
    return Make(A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
                (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
  }
  static Biquad LowPass(double fs, double f, double q)
  {
    const double w = 2 * kPi * std::min(f, 0.45 * fs) / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
    return Make((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
  }
  static Biquad HighPass(double fs, double f, double q)
  {
    const double w = 2 * kPi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
    return Make((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
  }
};

// A filter spec: type, frequency, Q, gain.
struct Band
{
  enum Type
  {
    kPeak,
    kLowShelf,
    kHighShelf,
    kLowPass,
    kHighPass,
    kNone
  } type = kNone;
  double f = 1000, q = 0.707, db = 0;

  Biquad Build(double fs) const
  {
    switch (type)
    {
      case kPeak: return Biquad::Peak(fs, f, q, db);
      case kLowShelf: return Biquad::LowShelf(fs, f, q, db);
      case kHighShelf: return Biquad::HighShelf(fs, f, q, db);
      case kLowPass: return Biquad::LowPass(fs, f, q);
      case kHighPass: return Biquad::HighPass(fs, f, q);
      default: return Biquad();
    }
  }
};
constexpr int kMaxBands = 7;

// ---------------------------------------------------------------------------------------------
// Speakers (shaped after published response charts). Closed-back 4x12s have a firm low-end
// resonance bump; open-back combos lose low end to rear-wave cancellation.
struct SpeakerModel
{
  const char* name;
  Band bands[kMaxBands];
};
constexpr int kNumSpeakers = 7;
inline const SpeakerModel& GetSpeaker(int i)
{
  using B = Band;
  static const SpeakerModel speakers[kNumSpeakers] = {
    {"4x12 V30", // Aggressive upper mids, steep roll-off past 5 kHz
     {{B::kHighPass, 75, 0.9, 0},
      {B::kPeak, 105, 1.2, 3},
      {B::kPeak, 600, 1.0, -3},
      {B::kPeak, 2600, 1.4, 6},
      {B::kPeak, 4500, 2.0, 3},
      {B::kLowPass, 5500, 0.8, 0},
      {B::kLowPass, 7000, 0.6, 0}}},
    {"4x12 Greenback", // Warm mid hump, smoother top
     {{B::kHighPass, 70, 0.9, 0},
      {B::kPeak, 100, 1.2, 3},
      {B::kPeak, 1400, 0.9, 4},
      {B::kPeak, 3200, 1.5, 3},
      {B::kLowPass, 4800, 0.75, 0},
      {B::kLowPass, 6000, 0.6, 0},
      {}}},
    {"4x12 G12H-30", // Tight, big low end; balanced mids
     {{B::kHighPass, 65, 0.9, 0},
      {B::kPeak, 90, 1.3, 4},
      {B::kPeak, 1800, 1.0, 2},
      {B::kPeak, 3000, 1.4, 3},
      {B::kLowPass, 5000, 0.8, 0},
      {B::kLowPass, 6500, 0.6, 0},
      {}}},
    {"2x12 Alnico Blue", // Open back: lighter lows; chimey, extended top
     {{B::kHighPass, 90, 0.7, 0},
      {B::kPeak, 400, 1.0, -2},
      {B::kPeak, 1200, 1.0, 3},
      {B::kPeak, 3300, 1.4, 5},
      {B::kLowPass, 6500, 0.8, 0},
      {B::kLowPass, 8000, 0.6, 0},
      {}}},
    {"1x12 Jensen P12", // Open back: scooped mids, sparkly top
     {{B::kHighPass, 95, 0.7, 0},
      {B::kPeak, 700, 0.8, -3},
      {B::kPeak, 2800, 1.2, 4},
      {B::kPeak, 5000, 2.0, 2},
      {B::kLowPass, 6000, 0.8, 0},
      {B::kLowPass, 8000, 0.6, 0},
      {}}},
    {"1x12 EVM12L", // Closed back, flat and extended: hi-fi
     {{B::kHighPass, 70, 0.8, 0},
      {B::kPeak, 2500, 1.0, 1.5},
      {B::kLowPass, 6500, 0.7, 0},
      {B::kLowPass, 9000, 0.6, 0},
      {},
      {},
      {}}},
    {"4x10 Jensen P10R", // 1.59.0. Tweed Bassman: lean lows, round mids, a quick, glassy top
     {{B::kHighPass, 85, 0.7, 0},
      {B::kPeak, 900, 0.9, 2},
      {B::kPeak, 2500, 1.3, 4},
      {B::kLowPass, 5500, 0.8, 0},
      {B::kLowPass, 7500, 0.6, 0},
      {},
      {}}},
  };
  return speakers[std::clamp(i, 0, kNumSpeakers - 1)];
}

// ---------------------------------------------------------------------------------------------
// Mics (shaped after published response charts, far-field / on-axis), with polar pattern for the
// proximity effect: 1.0 cardioid, 1.15 supercardioid, 1.5 figure-8.
struct MicModel
{
  const char* name;
  double proximity;
  Band bands[kMaxBands];
};
constexpr int kNumMics = 10; // The classic mode's selectors offer the first 6
inline const MicModel& GetMic(int i)
{
  using B = Band;
  static const MicModel mics[kNumMics] = {
    {"SM57", 1.0, // Presence peak, bass roll-off, top falls past 10 kHz
     {{B::kHighPass, 90, 0.7, 0},
      {B::kPeak, 2500, 1.0, 2},
      {B::kPeak, 5500, 1.3, 4},
      {B::kPeak, 8500, 2.0, -2},
      {B::kLowPass, 13000, 0.7, 0},
      {},
      {}}},
    {"SM7B", 1.0, // Smooth, full, gentle presence
     {{B::kHighPass, 50, 0.7, 0},
      {B::kPeak, 4500, 0.9, 2},
      {B::kLowPass, 11000, 0.6, 0},
      {},
      {},
      {},
      {}}},
    {"Beta 57A", 1.15, // Supercardioid: tighter lows, brighter, more extended top
     {{B::kHighPass, 110, 0.8, 0},
      {B::kPeak, 3000, 1.0, 2},
      {B::kPeak, 6000, 1.4, 5},
      {B::kLowPass, 16000, 0.7, 0},
      {},
      {},
      {}}},
    {"R-121 Ribbon", 1.5, // Figure-8 ribbon: big lows, dark, smooth top
     {{B::kHighPass, 30, 0.7, 0},
      {B::kLowShelf, 150, 0.7, 1},
      {B::kHighShelf, 6000, 0.7, -4},
      {B::kLowPass, 12000, 0.6, 0},
      {},
      {},
      {}}},
    {"MD421", 1.0, // Punchy lows, slight mid scoop, presence lift
     {{B::kHighPass, 60, 0.7, 0},
      {B::kPeak, 500, 0.9, -2},
      {B::kPeak, 4500, 1.0, 4},
      {B::kLowPass, 14000, 0.7, 0},
      {},
      {},
      {}}},
    {"U87 Condenser", 1.0, // Open and detailed: flat with a broad high lift
     {{B::kHighPass, 30, 0.7, 0},
      {B::kPeak, 4000, 0.8, 1},
      {B::kPeak, 10000, 0.8, 2},
      {B::kLowPass, 18000, 0.7, 0},
      {},
      {},
      {}}},
    // 1.59.0 (SPEAKER LAB).
    {"e609", 1.15, // Flat-faced supercardioid made to hang over the grille: mid-forward, fat presence
     {{B::kHighPass, 80, 0.7, 0},
      {B::kPeak, 900, 1.0, 1.5},
      {B::kPeak, 4000, 1.0, 4},
      {B::kLowPass, 14000, 0.7, 0},
      {},
      {},
      {}}},
    {"e906", 1.15, // Supercardioid with a gentle presence lift, a tighter low end than an SM57
     {{B::kHighPass, 70, 0.7, 0},
      {B::kPeak, 300, 1.0, -1},
      {B::kPeak, 4500, 0.9, 3},
      {B::kLowPass, 15000, 0.7, 0},
      {},
      {},
      {}}},
    {"M160 Ribbon", 1.3, // Hypercardioid ribbon: a ribbon's smooth body, but tighter and brighter than a 121
     {{B::kHighPass, 40, 0.7, 0},
      {B::kPeak, 2000, 0.9, 1},
      {B::kHighShelf, 8000, 0.7, -2},
      {B::kLowPass, 15000, 0.6, 0},
      {},
      {},
      {}}},
    {"C414 Condenser", 1.0, // Large-diaphragm condenser: full, with an airy lift up top
     {{B::kHighPass, 30, 0.7, 0},
      {B::kPeak, 6000, 0.9, 1.5},
      {B::kPeak, 12000, 0.8, 3},
      {B::kLowPass, 19000, 0.7, 0},
      {},
      {},
      {}}},
  };
  return mics[std::clamp(i, 0, kNumMics - 1)];
}

// Each mic's polar pattern (sensitivity A + (1 - A) cos(angle)) and its extra treble loss 90 degrees
// off axis (dynamics lose more than condensers; ribbons little). By GetMic index.
struct MicPolar
{
  double a, offAxisDB;
  const char* pattern;
};
inline const MicPolar& GetMicPolar(int i)
{
  static const MicPolar polar[kNumMics] = {
    {0.5, -6.0, "cardioid"},      {0.5, -4.0, "cardioid"},      {0.37, -6.0, "supercardioid"},
    {0.0, -2.0, "figure-8"},      {0.5, -5.0, "cardioid"},      {0.5, -3.0, "cardioid"},
    {0.37, -5.0, "supercardioid"}, {0.37, -5.0, "supercardioid"}, {0.25, -3.0, "hypercardioid"},
    {0.5, -3.0, "cardioid"}};
  return polar[std::clamp(i, 0, kNumMics - 1)];
}

// ---------------------------------------------------------------------------------------------
// SPEAKER LAB cab formats: where the cones sit on the baffle (m, from its centre; x right, y up),
// the cones' radiating radius, how far the dust caps sit behind the grille cloth, the cab's front
// size (for drawing), whether it's open-backed, and a mild cabinet character on top of the speaker
// voicing (a closed back's low resonance, an open back's lighter low end).
struct CabFormat
{
  const char* name;
  int numCones;
  double cx[4], cy[4];
  double coneRadius, recess;
  double width, height, depth;
  bool openBack;
  Band character[2];
};
constexpr int kNumCabFormats = 4;
inline const CabFormat& GetCabFormat(int i)
{
  using B = Band;
  static const CabFormat formats[kNumCabFormats] = {
    {"4x12", 4, {-0.165, 0.165, -0.165, 0.165}, {0.165, 0.165, -0.165, -0.165}, 0.12, 0.025, 0.76, 0.76, 0.36, false,
     {{B::kPeak, 100, 1.2, 1.0}, {}}},
    {"2x12", 2, {-0.17, 0.17, 0, 0}, {0, 0, 0, 0}, 0.12, 0.025, 0.70, 0.48, 0.26, true,
     {{B::kLowShelf, 150, 0.7, -1.0}, {}}},
    {"1x12", 1, {0, 0, 0, 0}, {0, 0, 0, 0}, 0.12, 0.025, 0.50, 0.45, 0.26, true,
     {{B::kLowShelf, 160, 0.7, -1.5}, {}}},
    {"4x10", 4, {-0.13, 0.13, -0.13, 0.13}, {0.13, 0.13, -0.13, -0.13}, 0.10, 0.02, 0.60, 0.60, 0.26, true,
     {{B::kLowShelf, 140, 0.7, -1.0}, {}}},
  };
  return formats[std::clamp(i, 0, kNumCabFormats - 1)];
}

// The cones' small differences from one another (level, and time in seconds).
constexpr double kConeGain[4] = {1.0, 0.96, 1.03, 0.98};
constexpr double kConeTime[4] = {0.0, 30e-6, -20e-6, 15e-6};
constexpr double kConeTimeBase = 30e-6; // (so no cone's time goes negative)

// A SPEAKER LAB mic.
struct LabMic
{
  bool on = false;
  int type = 0;
  double x = 0.0, y = 0.0, z = 0.025; // m (x, y on the baffle from its centre; z out from the grille)
  double levelDB = 0.0;
  double pan = 0.0; // -1 left .. +1 right
  bool invert = false;
  int cab = 0; // 1.61.0: which cab it's on (0 or 1; with a single cab, always 0)
  bool operator==(const LabMic& o) const
  {
    return on == o.on && type == o.type && x == o.x && y == o.y && z == o.z && levelDB == o.levelDB && pan == o.pan
           && invert == o.invert && cab == o.cab;
  }
};
constexpr int kNumLabMics = 4;

// What a mic hears of one cone (see the top of this file): delay (s), level, and the treble shelf's
// gain (linear), plus the geometry behind them for the page's readouts.
struct ConePath
{
  double delayS = 0.0, gain = 0.0, shelf = 1.0;
  double distanceM = 0.0, offAxisDeg = 0.0, s = 0.0;
};
inline ConePath ComputeConePath(const LabMic& m, const CabFormat& fmt, int cone)
{
  const double dx = m.x - fmt.cx[cone], dy = m.y - fmt.cy[cone];
  const double r = std::sqrt(dx * dx + dy * dy);
  const double zz = std::max(m.z, 0.0) + fmt.recess;
  const double d = std::sqrt(r * r + zz * zz);
  const double s = r / std::sqrt(r * r + (zz + fmt.coneRadius) * (zz + fmt.coneRadius));
  const double cosPhi = zz / std::max(d, 1e-6);
  const MicPolar& polar = GetMicPolar(m.type);
  ConePath p;
  p.distanceM = d;
  p.s = s;
  p.offAxisDeg = std::acos(std::clamp(cosPhi, -1.0, 1.0)) * 180.0 / kPi;
  p.delayS = d / kSpeedOfSound + kConeTimeBase + kConeTime[cone];
  const double sens = std::max(0.0, polar.a + (1.0 - polar.a) * cosPhi);
  // Normalized to 1 at 5 cm on the cone's axis (a mic 2.5 cm off the grille), for any cone size.
  const double a = fmt.coneRadius;
  const double piston = (std::sqrt(d * d + a * a) - d) / (std::sqrt(0.0025 + a * a) - 0.05);
  p.gain = kConeGain[cone] * sens * piston;
  const double shelfDB = 2.0 - 15.5 * s + polar.offAxisDB * (1.0 - cosPhi);
  p.shelf = std::pow(10.0, shelfDB / 20.0);
  return p;
}
// The cone a mic is closest to, and the proximity effect's boost from it (dB; as the classic mode).
inline double LabProximityDB(const LabMic& m, const CabFormat& fmt, int* nearest = nullptr)
{
  double best = 1e9;
  for (int k = 0; k < fmt.numCones; k++)
  {
    const double dx = m.x - fmt.cx[k], dy = m.y - fmt.cy[k];
    const double d = std::sqrt(dx * dx + dy * dy + m.z * m.z);
    if (d < best)
    {
      best = d;
      if (nearest)
        *nearest = k;
    }
  }
  const double d = std::clamp(best, 0.005, 1.5);
  return std::min(18.0, 20.0 * std::log10(1.0 + 0.054 / d) * GetMic(m.type).proximity);
}
constexpr double kLabShelfHz = 2500.0; // The cone's treble shelf (a one-pole split)

// A power-of-two delay line with linear-interpolated reads (the SPEAKER LAB's: every mic reads every
// cone's arrival from this one line).
class LabDelay
{
public:
  void Resize(int minLength)
  {
    int n = 64;
    while (n < minLength)
      n <<= 1;
    mBuf.assign(n, 0.0);
    mMask = n - 1;
    mPos = 0;
  }
  void Clear() { std::fill(mBuf.begin(), mBuf.end(), 0.0); }
  void Push(double x)
  {
    mPos = (mPos + 1) & mMask;
    mBuf[mPos] = x;
  }
  double Read(double samples) const
  {
    samples = std::clamp(samples, 0.0, static_cast<double>(mMask - 1));
    const int i = static_cast<int>(samples);
    const double f = samples - i;
    const double a = mBuf[(mPos - i) & mMask], b = mBuf[(mPos - i - 1) & mMask];
    return a + f * (b - a);
  }

private:
  std::vector<double> mBuf = std::vector<double>(64, 0.0);
  int mMask = 63, mPos = 0;
};

struct MicParams
{
  bool on = true;
  int type = 0;
  double position = 0.3; // 0 = dust cap (bright) .. 1 = cone edge (dark)
  double distanceM = 0.025; // Grille to cone
  double levelDB = 0.0;
  double pan = 0.0; // -1 left .. +1 right
  bool invert = false; // Polarity flip

  bool operator==(const MicParams& o) const
  {
    return on == o.on && type == o.type && position == o.position && distanceM == o.distanceM
           && levelDB == o.levelDB && pan == o.pan && invert == o.invert;
  }
};

struct CabLabParams
{
  bool useIR = false; // Speaker from an IR (provided by the caller) instead of the model
  int speaker = 1; // 4x12 Greenback
  // SPEAKER LAB (1.59.0): 3D placement instead of the classic Mic A / Mic B, the cab's format, and
  // its four mics. Defaults: an SM57 just off the dust cap of the top-left 4x12 cone and an R-121 a
  // little farther out on the same cone (the classic defaults' sound, placed).
  bool lab3D = false;
  int format = 0;
  LabMic lab[kNumLabMics] = {{true, 0, -0.165 + 0.035, 0.165, 0.025, 0.0, -0.3, false, 0},
                             {true, 3, -0.165 + 0.06, 0.165, 0.05, -2.0, 0.3, false, 0},
                             {false, 4, -0.17 + 0.03, 0.0, 0.03, 0.0, 0.0, false, 1},
                             {false, 5, 0.0, 0.0, 0.6, -6.0, 0.0, false, 0}};
  // Dual cabs (1.61.0): cab 2's format, speaker voicing and source. Defaults: an open-back 2x12 with
  // Alnico Blues beside the 4x12.
  bool dual = false;
  int format2 = 1;
  int speaker2 = 3;
  bool useIR2 = false;
  // The cab a mic is on, as the audio uses it.
  int MicCab(int m) const { return dual ? std::clamp(lab[m].cab, 0, 1) : 0; }
  int CabFormatOf(int cab) const { return cab == 0 ? format : format2; }
  MicParams micA{true, 0, 0.3, 0.025, 0.0, -0.3, false}; // SM57, close, slightly off the cap
  MicParams micB{true, 3, 0.5, 0.05, -2.0, 0.3, false}; // Ribbon, a little farther back
  double roomLevelDB = -18.0;
  double roomSize = 0.4; // 0..1
  double width = 0.8; // 0..1
  double outputDB = 0.0;
};

// Fractional delay line (linear interpolation).
class DelayLine
{
public:
  void Resize(int n)
  {
    mBuf.assign(static_cast<size_t>(std::max(n, 4)), 0.0);
    mPos = 0;
  }
  void Clear() { std::fill(mBuf.begin(), mBuf.end(), 0.0); }
  void Push(double x)
  {
    mPos = (mPos + 1) % mBuf.size();
    mBuf[mPos] = x;
  }
  // Delayed by `samples` (fractional), 0 = the sample just pushed.
  double Read(double samples) const
  {
    samples = std::clamp(samples, 0.0, static_cast<double>(mBuf.size() - 2));
    const int i = static_cast<int>(samples);
    const double f = samples - i;
    const size_t n = mBuf.size();
    const double a = mBuf[(mPos + n - i) % n], b = mBuf[(mPos + n - i - 1) % n];
    return a + f * (b - a);
  }

private:
  std::vector<double> mBuf;
  size_t mPos = 0;
};

class CabLab
{
public:
  void Reset(double sampleRate, int maxBlockSize)
  {
    mFs = sampleRate;
    mMaxBlock = std::max(maxBlockSize, 1);
    mSpeakerBuf.assign(mMaxBlock, 0.0);
    // Mic delays up to 1 m (+ margin); room predelay + early reflections up to ~80 ms.
    for (auto& m : mMics)
    {
      m.delay.Resize(static_cast<int>(0.004 * mFs) + 8);
      m.delay.Clear();
    }
    // SPEAKER LAB: up to 1.5 m out and 0.5 m across, plus the cones' recess and time differences.
    for (auto& d : mLabDelays)
    {
      d.Resize(static_cast<int>(0.0055 * mFs) + 8);
      d.Clear();
    }
    mLabTick = 0;
    mLabFresh = true;
    for (auto& m : mLab)
    {
      for (auto& p : m.paths)
        p.lp = 0.0;
      for (auto& b : m.filters)
        b.Clear();
      m.prox.Clear();
    }
    mRoomIn.Resize(static_cast<int>(0.12 * mFs) + 8);
    for (int i = 0; i < 4; i++)
    {
      mFdn[i].Resize(static_cast<int>(0.15 * mFs) + 8);
      mFdnLp[i] = 0.0;
    }
    mLabShelfCoef = 1.0 - std::exp(-2.0 * kPi * kLabShelfHz / mFs);
    mHasParams = false;
    SetParams(mParams);
  }

  void SetParams(const CabLabParams& p)
  {
    const bool speakerChanged = !mHasParams || p.speaker != mParams.speaker;
    const bool micAChanged = !mHasParams || !(p.micA == mParams.micA);
    const bool micBChanged = !mHasParams || !(p.micB == mParams.micB);
    const bool roomChanged = !mHasParams || p.roomSize != mParams.roomSize;
    const bool formatChanged = !mHasParams || p.format != mParams.format;
    const bool speaker2Changed = !mHasParams || p.speaker2 != mParams.speaker2;
    const bool format2Changed = !mHasParams || p.format2 != mParams.format2;
    if (p.dual && (!mHasParams || !mParams.dual))
    {
      // Cab 2 coming in: its delay line wasn't being fed.
      mLabDelays[1].Clear();
      for (auto& b : mSpeaker2)
        b.Clear();
    }
    if (p.lab3D && (!mHasParams || !mParams.lab3D))
    {
      // Into the SPEAKER LAB from the classic mics: its delay line wasn't being fed.
      for (auto& d : mLabDelays)
        d.Clear();
      mLabFresh = true;
      mLabTick = 0;
      for (auto& m : mLab)
        m.active = false;
    }
    bool labTypeChanged[kNumLabMics];
    for (int m = 0; m < kNumLabMics; m++)
      labTypeChanged[m] = !mHasParams || p.lab[m].type != mParams.lab[m].type;
    mParams = p;
    mHasParams = true;
    for (int m = 0; m < kNumLabMics; m++)
      if (labTypeChanged[m])
        _BuildLabMicEQ(m);
    if (formatChanged || format2Changed)
      _BuildCabCharacter();
    if (speaker2Changed)
    {
      const SpeakerModel& sp = GetSpeaker(mParams.speaker2);
      mNumSpeaker2Bands = 0;
      for (const Band& b : sp.bands)
        if (b.type != Band::kNone)
        {
          mSpeaker2[mNumSpeaker2Bands] = _KeepState(mSpeaker2[mNumSpeaker2Bands], b.Build(mFs));
          mNumSpeaker2Bands++;
        }
    }
    if (speakerChanged)
      _BuildSpeaker();
    if (micAChanged)
      _BuildMic(mMics[0], p.micA);
    if (micBChanged)
      _BuildMic(mMics[1], p.micB);
    if (roomChanged)
      _BuildRoom();
    mRoomGain = std::pow(10.0, p.roomLevelDB / 20.0);
    mOutGain = std::pow(10.0, p.outputDB / 20.0);
  }

  // Mono in, stereo out. `irSpeaker`, if given and useIR is on, is the speaker signal from an IR
  // the caller already ran `in` through; otherwise the modeled speaker is used.
  void Process(const double* in, double* outL, double* outR, size_t n, const double* irSpeaker = nullptr,
               const double* irSpeaker2 = nullptr)
  {
    const bool useIR = mParams.useIR && irSpeaker != nullptr;
    // Cab 2 (dual, 3D only): its own IR, or its own modeled speaker.
    const bool cab2 = mParams.lab3D && mParams.dual;
    const bool useIR2 = mParams.useIR2 && irSpeaker2 != nullptr;
    for (size_t s = 0; s < n; s++)
    {
      // Speaker.
      double spk;
      if (useIR)
        spk = irSpeaker[s];
      else
      {
        spk = in[s];
        for (int b = 0; b < mNumSpeakerBands; b++)
          spk = mSpeaker[b].Process(spk);
      }

      // Close mics.
      double l = 0.0, r = 0.0;
      if (mParams.lab3D)
      {
        double spk2 = 0.0;
        if (cab2)
        {
          if (useIR2)
            spk2 = irSpeaker2[s];
          else
          {
            spk2 = in[s];
            for (int b = 0; b < mNumSpeaker2Bands; b++)
              spk2 = mSpeaker2[b].Process(spk2);
          }
        }
        _ProcessLab(spk, spk2, cab2, l, r);
      }
      else
      for (int m = 0; m < 2; m++)
      {
        Mic& mic = mMics[m];
        mic.delay.Push(spk);
        if (!mic.on)
          continue;
        double x = mic.delay.Read(mic.delaySamples);
        for (int b = 0; b < mic.numBands; b++)
          x = mic.filters[b].Process(x);
        x *= mic.gain;
        l += x * mic.panL;
        r += x * mic.panR;
      }

      // Room: predelay + early reflections + a 4-line feedback delay network.
      mRoomIn.Push(mRoomMic.Process(mRoomMicLp.Process(spk)));
      double erL = 0.0, erR = 0.0;
      for (int t = 0; t < kNumTaps; t++)
      {
        erL += mTapGain[t] * mRoomIn.Read(mTapL[t]);
        erR += mTapGain[t] * mRoomIn.Read(mTapR[t]);
      }
      const double pre = mRoomIn.Read(mPredelay);
      double outs[4];
      for (int i = 0; i < 4; i++)
        outs[i] = mFdn[i].Read(mFdnDelay[i]);
      // Hadamard mix (energy preserving), damping, decay, re-inject.
      const double h[4] = {0.5 * (outs[0] + outs[1] + outs[2] + outs[3]), 0.5 * (outs[0] - outs[1] + outs[2] - outs[3]),
                           0.5 * (outs[0] + outs[1] - outs[2] - outs[3]), 0.5 * (outs[0] - outs[1] - outs[2] + outs[3])};
      for (int i = 0; i < 4; i++)
      {
        mFdnLp[i] += mDampCoef * (h[i] - mFdnLp[i]);
        mFdn[i].Push(pre * 0.5 + mFdnGain[i] * mFdnLp[i]);
      }
      double roomL = erL + 0.7 * (outs[0] + outs[2]);
      double roomR = erR + 0.7 * (outs[1] + outs[3]);
      // Width: mid/side.
      const double mid = 0.5 * (roomL + roomR), side = 0.5 * (roomL - roomR) * mParams.width;
      roomL = (mid + side) * mRoomGain;
      roomR = (mid - side) * mRoomGain;

      outL[s] = (l + roomL) * mOutGain;
      outR[s] = (r + roomR) * mOutGain;
    }
  }

  // SPEAKER LAB: the frequency response (dB) of the close mics' mono sum (or one mic's, mic >= 0, or one
  // cab's mics', cab >= 0) at
  // `freqs`, from the same geometry and filters the audio uses -- for the page's plot. Not the room.
  // Called on the UI thread only when something changed; reads nothing the audio thread writes.
  static void LabResponse(const CabLabParams& p, double fs, const double* freqs, int numFreqs, double* outDB, int mic = -1,
                          int cab = -1)
  {
    using C = std::complex<double>;
    auto biquadAt = [](const Biquad& q, const C& zi) { // zi = z^-1
      return (q.b0 + q.b1 * zi + q.b2 * zi * zi) / (1.0 + q.a1 * zi + q.a2 * zi * zi);
    };
    // Each cab's speaker (model, or flat for an IR, which isn't known here) and character.
    std::vector<Biquad> speaker[2];
    for (int c = 0; c < 2; c++)
    {
      if (!(c == 0 ? p.useIR : p.useIR2))
        for (const Band& b : GetSpeaker(c == 0 ? p.speaker : p.speaker2).bands)
          if (b.type != Band::kNone)
            speaker[c].push_back(b.Build(fs));
      for (const Band& b : GetCabFormat(p.CabFormatOf(c)).character)
        if (b.type != Band::kNone)
          speaker[c].push_back(b.Build(fs));
    }
    const double shelfC = 1.0 - std::exp(-2.0 * kPi * kLabShelfHz / fs);
    for (int i = 0; i < numFreqs; i++)
    {
      const double w = 2.0 * kPi * std::min(freqs[i], 0.49 * fs) / fs;
      const C zi = std::polar(1.0, -w);
      C spkAt[2] = {1.0, 1.0};
      for (int c = 0; c < 2; c++)
        for (const Biquad& q : speaker[c])
          spkAt[c] *= biquadAt(q, zi);
      const C lp = shelfC / (1.0 - (1.0 - shelfC) * zi); // The one-pole low-pass
      C sum = 0.0;
      for (int m = 0; m < kNumLabMics; m++)
      {
        const LabMic& lm = p.lab[m];
        if (!lm.on || (mic >= 0 && m != mic) || (cab >= 0 && p.MicCab(m) != cab))
          continue;
        const int micCab = p.MicCab(m);
        const CabFormat& fmt = GetCabFormat(p.CabFormatOf(micCab));
        C h = 0.0;
        for (int k = 0; k < fmt.numCones; k++)
        {
          const ConePath cp = ComputeConePath(lm, fmt, k);
          h += cp.gain * (lp + cp.shelf * (1.0 - lp)) * std::polar(1.0, -w * cp.delayS * fs);
        }
        for (const Band& b : GetMic(lm.type).bands)
          if (b.type != Band::kNone)
            h *= biquadAt(b.Build(fs), zi);
        h *= biquadAt(Biquad::LowShelf(fs, 120.0, 0.55, LabProximityDB(lm, fmt)), zi);
        h *= (lm.invert ? -1.0 : 1.0) * std::pow(10.0, lm.levelDB / 20.0);
        sum += h * spkAt[micCab];
      }
      outDB[i] = 20.0 * std::log10(std::max(1e-9, std::abs(sum)));
    }
  }

  // The modeled speaker alone (for tests / plots).
  double ProcessSpeakerOnly(double x)
  {
    for (int b = 0; b < mNumSpeakerBands; b++)
      x = mSpeaker[b].Process(x);
    return x;
  }

private:
  struct Mic
  {
    bool on = true;
    DelayLine delay;
    double delaySamples = 0.0;
    Biquad filters[kMaxBands + 2];
    int numBands = 0;
    double gain = 1.0, panL = 0.707, panR = 0.707;
  };

  // SPEAKER LAB ==================================================================================
  struct LabPath
  {
    double delay = 0.0, gain = 0.0, shelf = 1.0; // Now (samples, linear, linear)
    double dDelay = 0.0, dGain = 0.0, dShelf = 0.0; // Per-sample ramps toward the next tick's targets
    double lp = 0.0; // The shelf's one-pole state
  };
  struct LabMicState
  {
    LabPath paths[4];
    Biquad filters[kMaxBands];
    int numBands = 0;
    Biquad prox;
    double proxDB = 0.0;
    double gain = 0.0, panL = 0.707, panR = 0.707, dGain = 0.0, dPanL = 0.0, dPanR = 0.0;
    bool active = false; // On, or still fading out
    int cab = 0; // Which cab's delay line it reads (as of the last tick)
  };
  static constexpr int kLabTickSamples = 32;

  void _BuildLabMicEQ(int m)
  {
    LabMicState& st = mLab[m];
    st.numBands = 0;
    for (const Band& b : GetMic(mParams.lab[m].type).bands)
      if (b.type != Band::kNone)
      {
        st.filters[st.numBands] = _KeepState(st.filters[st.numBands], b.Build(mFs));
        st.numBands++;
      }
  }

  void _BuildCabCharacter()
  {
    for (int cab = 0; cab < 2; cab++)
    {
      mNumCharBands[cab] = 0;
      for (const Band& b : GetCabFormat(mParams.CabFormatOf(cab)).character)
        if (b.type != Band::kNone)
        {
          mCharacter[cab][mNumCharBands[cab]] = _KeepState(mCharacter[cab][mNumCharBands[cab]], b.Build(mFs));
          mNumCharBands[cab]++;
        }
    }
  }

  // Every kLabTickSamples: each mic's targets from the geometry, ramped to over the next tick (the
  // first tick after a reset starts there instead).
  void _LabTick()
  {
    const double ramp = 1.0 / kLabTickSamples;
    for (int m = 0; m < kNumLabMics; m++)
    {
      const LabMic& lm = mParams.lab[m];
      LabMicState& st = mLab[m];
      const int cab = mParams.MicCab(m);
      const CabFormat& fmt = GetCabFormat(mParams.CabFormatOf(cab));
      st.cab = cab;
      const double targetGain = lm.on ? (lm.invert ? -1.0 : 1.0) * std::pow(10.0, lm.levelDB / 20.0) : 0.0;
      if (!lm.on && !st.active)
        continue;
      if (!st.active || mLabFresh)
      {
        // Switched on (or reset): its paths start where they belong, its level fades in.
        for (int k = 0; k < 4; k++)
        {
          const ConePath cp = k < fmt.numCones ? ComputeConePath(lm, fmt, k) : ConePath();
          st.paths[k].delay = cp.delayS * mFs;
          st.paths[k].gain = cp.gain;
          st.paths[k].shelf = cp.shelf;
        }
        st.gain = mLabFresh ? targetGain : 0.0;
        st.active = true;
      }
      for (int k = 0; k < 4; k++)
      {
        LabPath& p = st.paths[k];
        const ConePath cp = k < fmt.numCones ? ComputeConePath(lm, fmt, k) : ConePath();
        p.dDelay = (cp.delayS * mFs - p.delay) * ramp;
        p.dGain = (cp.gain - p.gain) * ramp;
        p.dShelf = (cp.shelf - p.shelf) * ramp;
      }
      // Proximity: a low shelf, gliding toward its target (rebuilt only while it moves).
      const double proxTarget = LabProximityDB(lm, fmt);
      if (std::fabs(proxTarget - st.proxDB) > 1e-4 || mLabFresh)
      {
        st.proxDB = mLabFresh ? proxTarget : st.proxDB + 0.3 * (proxTarget - st.proxDB);
        st.prox = _KeepState(st.prox, Biquad::LowShelf(mFs, 120.0, 0.55, st.proxDB));
      }
      const double a = (std::clamp(lm.pan, -1.0, 1.0) + 1.0) * 0.25 * kPi;
      st.dGain = (targetGain - st.gain) * ramp;
      st.dPanL = (std::cos(a) - st.panL) * ramp;
      st.dPanR = (std::sin(a) - st.panR) * ramp;
      if (!lm.on && std::fabs(st.gain) < 1e-6)
        st.active = false; // Faded out
    }
    mLabFresh = false;
  }

  static Biquad _KeepState(const Biquad& from, Biquad to)
  {
    to.z1 = from.z1;
    to.z2 = from.z2;
    return to;
  }

  void _ProcessLab(double spk, double spk2, bool cab2, double& l, double& r)
  {
    for (int b = 0; b < mNumCharBands[0]; b++)
      spk = mCharacter[0][b].Process(spk);
    mLabDelays[0].Push(spk);
    if (cab2)
    {
      for (int b = 0; b < mNumCharBands[1]; b++)
        spk2 = mCharacter[1][b].Process(spk2);
      mLabDelays[1].Push(spk2);
    }
    if (mLabTick-- <= 0)
    {
      mLabTick = kLabTickSamples - 1;
      _LabTick();
    }
    for (int m = 0; m < kNumLabMics; m++)
    {
      LabMicState& st = mLab[m];
      if (!st.active)
        continue;
      const LabDelay& delay = mLabDelays[st.cab];
      const int numCones = GetCabFormat(mParams.CabFormatOf(st.cab)).numCones;
      double x = 0.0;
      for (int k = 0; k < numCones; k++)
      {
        LabPath& p = st.paths[k];
        p.delay += p.dDelay;
        p.gain += p.dGain;
        p.shelf += p.dShelf;
        const double v = delay.Read(p.delay);
        p.lp += mLabShelfCoef * (v - p.lp);
        x += p.gain * (p.lp + p.shelf * (v - p.lp));
      }
      for (int b = 0; b < st.numBands; b++)
        x = st.filters[b].Process(x);
      x = st.prox.Process(x);
      st.gain += st.dGain;
      st.panL += st.dPanL;
      st.panR += st.dPanR;
      x *= st.gain;
      l += x * st.panL;
      r += x * st.panR;
    }
  }

  void _BuildSpeaker()
  {
    const SpeakerModel& sp = GetSpeaker(mParams.speaker);
    mNumSpeakerBands = 0;
    for (const Band& b : sp.bands)
      if (b.type != Band::kNone)
        mSpeaker[mNumSpeakerBands++] = b.Build(mFs);
  }

  void _BuildMic(Mic& mic, const MicParams& p)
  {
    const MicModel& model = GetMic(p.type);
    mic.on = p.on;
    mic.numBands = 0;
    for (const Band& b : model.bands)
      if (b.type != Band::kNone)
        mic.filters[mic.numBands++] = b.Build(mFs);
    const double d = std::clamp(p.distanceM, 0.005, 1.0);
    // Proximity effect: bass boost growing as the mic gets closer -- about +10 dB at 100 Hz at
    // 2.5 cm for a cardioid, +5 dB at 7.5 cm, ~1 dB at 30 cm (SM57 chart) -- scaled by the pattern.
    const double proxDB = std::min(18.0, 20.0 * std::log10(1.0 + 0.054 / d) * model.proximity);
    mic.filters[mic.numBands++] = Biquad::LowShelf(mFs, 120.0, 0.55, proxDB);
    // Cone position: the dust cap is brightest, the edge darkest. A farther mic hears more of the
    // whole cone, so the position matters less with distance.
    const double pos = p.position + (0.5 - p.position) * std::min(1.0, d / 0.5);
    mic.filters[mic.numBands++] = Biquad::HighShelf(mFs, 2500.0, 0.6, 2.0 - 11.0 * pos);
    // Distance: real propagation delay, and level falling off (a 12" cone isn't a point source up
    // close, so the fall-off is referenced to ~15 cm).
    mic.delaySamples = d / kSpeedOfSound * mFs;
    const double distanceGain = 0.15 / (d + 0.15);
    mic.gain = (p.invert ? -1.0 : 1.0) * std::pow(10.0, p.levelDB / 20.0) * distanceGain / (0.15 / 0.175);
    // Equal-power pan.
    const double a = (std::clamp(p.pan, -1.0, 1.0) + 1.0) * 0.25 * kPi;
    mic.panL = std::cos(a);
    mic.panR = std::sin(a);
  }

  void _BuildRoom()
  {
    const double size = std::clamp(mParams.roomSize, 0.0, 1.0);
    const double scale = 0.4 + 1.6 * size; // Room dimensions relative to a mid-size room
    // Room mics: a pair of condensers a few metres back -- some low end lost, top softened by air
    // and distance.
    mRoomMic = Biquad::HighPass(mFs, 60.0, 0.7);
    mRoomMicLp = Biquad::LowPass(mFs, 9000.0 - 3000.0 * size, 0.6);
    mPredelay = (0.004 + 0.016 * size) * mFs;
    // Early reflections: floor, walls, ceiling -- different paths to each room mic.
    static const double tapMs[kNumTaps][2] = {{5.1, 6.3}, {8.7, 7.4}, {11.9, 13.6}, {16.2, 14.1}, {21.5, 23.8}, {27.3, 25.6}};
    for (int t = 0; t < kNumTaps; t++)
    {
      mTapL[t] = mPredelay + tapMs[t][0] * 0.001 * scale * mFs;
      mTapR[t] = mPredelay + tapMs[t][1] * 0.001 * scale * mFs;
      mTapGain[t] = 0.55 * std::pow(0.78, t);
    }
    // Diffuse tail: RT60 from 0.25 s (small) to 1.5 s (large); bigger rooms are darker.
    const double rt60 = 0.25 + 1.25 * size;
    static const double baseMs[4] = {29.7, 37.1, 41.3, 53.9};
    for (int i = 0; i < 4; i++)
    {
      mFdnDelay[i] = baseMs[i] * 0.001 * scale * mFs;
      mFdnGain[i] = std::pow(10.0, -3.0 * (mFdnDelay[i] / mFs) / rt60);
    }
    mDampCoef = 1.0 - std::exp(-2.0 * kPi * (7000.0 - 3500.0 * size) / mFs);
  }

  static constexpr int kNumTaps = 6;

  CabLabParams mParams;
  bool mHasParams = false;
  double mFs = 48000.0;
  int mMaxBlock = 512;
  std::vector<double> mSpeakerBuf;

  Biquad mSpeaker[kMaxBands];
  int mNumSpeakerBands = 0;
  Mic mMics[2];

  // SPEAKER LAB (see the top of this file).
  LabDelay mLabDelays[2]; // Each cab's speaker signal (cab 2's only fed when dual)
  LabMicState mLab[kNumLabMics];
  Biquad mCharacter[2][2];
  int mNumCharBands[2] = {0, 0}, mLabTick = 0;
  Biquad mSpeaker2[kMaxBands]; // Cab 2's modeled speaker
  int mNumSpeaker2Bands = 0;
  bool mLabFresh = true;
  double mLabShelfCoef = 0.3;

  Biquad mRoomMic, mRoomMicLp;
  DelayLine mRoomIn;
  double mPredelay = 0.0;
  double mTapL[kNumTaps] = {}, mTapR[kNumTaps] = {}, mTapGain[kNumTaps] = {};
  DelayLine mFdn[4];
  double mFdnDelay[4] = {}, mFdnGain[4] = {}, mFdnLp[4] = {}, mDampCoef = 0.5;
  double mRoomGain = 0.1, mOutGain = 1.0;
};
} // namespace cab
