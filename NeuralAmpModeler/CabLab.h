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

#include <algorithm>
#include <cmath>
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
constexpr int kNumSpeakers = 6;
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
constexpr int kNumMics = 6;
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
  };
  return mics[std::clamp(i, 0, kNumMics - 1)];
}

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
    mRoomIn.Resize(static_cast<int>(0.12 * mFs) + 8);
    for (int i = 0; i < 4; i++)
    {
      mFdn[i].Resize(static_cast<int>(0.15 * mFs) + 8);
      mFdnLp[i] = 0.0;
    }
    mHasParams = false;
    SetParams(mParams);
  }

  void SetParams(const CabLabParams& p)
  {
    const bool speakerChanged = !mHasParams || p.speaker != mParams.speaker;
    const bool micAChanged = !mHasParams || !(p.micA == mParams.micA);
    const bool micBChanged = !mHasParams || !(p.micB == mParams.micB);
    const bool roomChanged = !mHasParams || p.roomSize != mParams.roomSize;
    mParams = p;
    mHasParams = true;
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
  void Process(const double* in, double* outL, double* outR, size_t n, const double* irSpeaker = nullptr)
  {
    const bool useIR = mParams.useIR && irSpeaker != nullptr;
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

  Biquad mRoomMic, mRoomMicLp;
  DelayLine mRoomIn;
  double mPredelay = 0.0;
  double mTapL[kNumTaps] = {}, mTapR[kNumTaps] = {}, mTapGain[kNumTaps] = {};
  DelayLine mFdn[4];
  double mFdnDelay[4] = {}, mFdnGain[4] = {}, mFdnLp[4] = {}, mDampCoef = 0.5;
  double mRoomGain = 0.1, mOutGain = 1.0;
};
} // namespace cab
