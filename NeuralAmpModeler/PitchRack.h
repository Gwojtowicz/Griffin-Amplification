#pragma once

// PITCH RACK (1.76.0): a dual-voice studio pitch shifter, the kind of late-80s rack processor that put
// the wide, shimmering detune and the intelligent harmonies on a generation of guitar records.
// Modelled on how those units worked rather than copied from one:
//   - Two voices, A (left) and B (right), each a pitch shifter followed by its own delay and
//     feedback. Feedback goes back into the voice's shifter, so every repeat is shifted again: a
//     fifth climbs in fifths, an octave in octaves (the cascading "staircase").
//   - The shifter is the splicing kind those units used: two read taps sweep through a short
//     memory at the new speed and crossfade (sin^2 windows sum to one). Like their de-glitching,
//     each new splice is placed where the waveform best lines up with what's playing (normalised
//     correlation over one guitar period), so the splices don't beat against the note.
//   - The wet path has the bandwidth of the converters of the time (15 kHz, steep), and Hi Cut below that.
// Modes:
//   Micro    -- voice A up by Fine cents, B down by the same, after short delays: the classic stereo
//               detune thickener (about 9 cents and 10-20 ms).
//   Dual     -- each voice shifted by its own semitones (+-2 octaves), Fine spreads them apart.
//   Diatonic -- each voice a scale interval (a 3rd, a 5th ...) above or below the note you play, in
//               Key and Scale: it tracks your pitch, so the harmony stays in the key.
//   Reverse  -- each voice plays its last window (Delay A / Delay B long) backwards, shifted;
//               with Feedback, the reversed, pitched repeats cascade ("crystal" echoes).
// Same call convention as the other pedals; the output is always stereo.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "MorePedals.h"

namespace pedals
{

struct PitchRackParams
{
  int mode = 0; // Micro / Dual / Diatonic / Reverse
  double semiA = 7.0, semiB = 12.0; // Dual / Reverse: semitones, -24..24
  int diaA = 9, diaB = 11; // Diatonic: 0..14 = -7..+7 scale steps (7 = unison; +2 a 3rd, +4 a 5th)
  int key = 0; // C .. B
  int scale = 0; // Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Harmonic Minor
  double fineCents = 9.0; // 0..50
  double delayAms = 12.0, delayBms = 20.0; // 0..1500
  double feedback01 = 0.0;
  double mix01 = 0.5; // 0.5 = dry and wet both full
  double width01 = 1.0; // 1 = A hard left, B hard right
  double hiCutHz = 15000.0;
  int splice = 0; // 1.78.0: 0 Smart (aligned splices), 1 Vintage (fixed, shorter, unaligned: the older units' warble)
};

// A note tracker for the Diatonic mode: YIN on a band-limited copy decimated to about 12 kHz, every
// 4 ms. A new note is taken only when it's read twice in a row; between notes (or on chords it
// can't read) it holds the last one.
class RackPitchTracker
{
public:
  void Reset(double sr)
  {
    mDecim = std::max(1, (int)std::lround(sr / 12000.0));
    mDsr = sr / mDecim;
    mLp1.LowPass(sr, 1000.0, 0.5412);
    mLp2.LowPass(sr, 1000.0, 1.3066);
    mLp1.Reset();
    mLp2.Reset();
    mRing.assign(kRing, 0.0);
    mWrite = mCount = mHop = 0;
    mNote = -1;
    mCand = -1;
    mCandHits = 0;
    mTauMax = std::min((int)(mDsr / 70.0), kRing / 3);
    mTauMin = std::max(2, (int)(mDsr / 1100.0));
    mWin = std::min((int)(mDsr * 0.027), kRing - mTauMax - 1);
    mA.assign(mWin + mTauMax + 1, 0.0);
    mD.assign(mTauMax + 2, 0.0);
  }
  void Push(double x)
  {
    const double y = mLp2.Process(mLp1.Process(x));
    if (++mCount < mDecim)
      return;
    mCount = 0;
    mRing[mWrite] = y;
    mWrite = (mWrite + 1) & (kRing - 1);
    if (++mHop >= std::max(1, (int)(mDsr * 0.004)))
    {
      mHop = 0;
      _Analyze();
    }
  }
  int Note() const { return mNote; } // MIDI note number, or -1 before the first

private:
  static constexpr int kRing = 1024;
  void _Analyze()
  {
    const int n = mWin + mTauMax;
    for (int j = 0; j < n; j++)
      mA[j] = mRing[(mWrite - n + j + kRing) & (kRing - 1)];
    double energy = 0.0;
    for (int j = 0; j < mWin; j++)
      energy += mA[j] * mA[j];
    if (energy / mWin < 1e-8) // under about -80 dBFS: nothing to read
      return;
    double running = 0.0;
    mD[0] = 1.0;
    int found = -1;
    for (int tau = 1; tau <= mTauMax; tau++)
    {
      double d = 0.0;
      for (int j = 0; j < mWin; j++)
      {
        const double e = mA[j] - mA[j + tau];
        d += e * e;
      }
      running += d;
      mD[tau] = running > 0.0 ? d * tau / running : 1.0;
      if (found < 0 && tau > mTauMin + 1 && mD[tau - 1] < 0.15 && mD[tau - 1] <= mD[tau])
        found = tau - 1;
    }
    if (found < 0)
      return;
    double t = found;
    if (found > 1 && found < mTauMax)
    {
      const double a = mD[found - 1], b = mD[found], c = mD[found + 1];
      const double den = a - 2.0 * b + c;
      if (std::fabs(den) > 1e-12)
        t += std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }
    const double f = mDsr / t;
    const int note = (int)std::lround(69.0 + 12.0 * std::log2(f / 440.0));
    if (note == mCand)
      mCandHits++;
    else
    {
      mCand = note;
      mCandHits = 1;
    }
    if (mCandHits >= 2)
      mNote = note;
  }
  Biquad mLp1, mLp2;
  std::vector<double> mRing, mA, mD;
  double mDsr = 12000.0;
  int mDecim = 4, mWrite = 0, mCount = 0, mHop = 0, mWin = 320, mTauMax = 171, mTauMin = 11;
  int mNote = -1, mCand = -1, mCandHits = 0;
};

class PitchRack
{
public:
  enum Mode
  {
    kMicro = 0,
    kDual,
    kDiatonic,
    kReverse
  };
  static constexpr int kNumScales = 7;

  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    for (auto& v : mVoice)
    {
      v.in.Resize((int)(4.2 * sr) + 64); // Reverse: a 1 s window read back at up to 4x
      v.out.Resize((int)(1.6 * sr) + 16);
      v.fb = 0.0;
      v.semi = mWantSemi[&v - mVoice.data()];
      v.started = false;
      for (auto& f : v.lp)
        f.Reset();
    }
    mTracker.Reset(sr);
    mHiCutSet = -1.0;
    _ResetTaps();
    mModeGain = 1.0;
  }

  void SetParams(const PitchRackParams& p)
  {
    mParams = p;
    mWantMode = std::clamp(p.mode, 0, 3);
    mWantSplice = std::clamp(p.splice, 0, 1);
    mFeedback.Set(std::clamp(p.feedback01, 0.0, 1.0) * 0.9);
    mMix.Set(std::clamp(p.mix01, 0.0, 1.0));
    mWidth.Set(std::clamp(p.width01, 0.0, 1.0));
    mDelay[0].Set(std::clamp(p.delayAms, 0.0, 1500.0));
    mDelay[1].Set(std::clamp(p.delayBms, 0.0, 1500.0));
  }

  // Semitones a Diatonic voice shifts the note `note` by: `steps` scale degrees up (or down) from the
  // note in Key/Scale. A note outside the scale is moved with the scale note under it (so a passing
  // note gets a parallel harmony).
  static double DiatonicSemis(int note, int key, int scale, int steps)
  {
    static const int kScales[kNumScales][7] = {
      {0, 2, 4, 5, 7, 9, 11}, // Major
      {0, 2, 3, 5, 7, 8, 10}, // Minor
      {0, 2, 3, 5, 7, 9, 10}, // Dorian
      {0, 1, 3, 5, 7, 8, 10}, // Phrygian
      {0, 2, 4, 6, 7, 9, 11}, // Lydian
      {0, 2, 4, 5, 7, 9, 10}, // Mixolydian
      {0, 2, 3, 5, 7, 8, 11}, // Harmonic minor
    };
    const int* s = kScales[std::clamp(scale, 0, kNumScales - 1)];
    const int pc = (((note - key) % 12) + 12) % 12;
    int deg = 0;
    for (int d = 0; d < 7; d++)
      if (s[d] <= pc)
        deg = d;
    const int t = deg + steps;
    const int oct = t >= 0 ? t / 7 : -((-t + 6) / 7);
    const int td = t - 7 * oct;
    return (double)(s[td] + 12 * oct - s[deg]);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(2, numFrames);
    const double sr = mSampleRate;
    const double coef = GlideCoef(kKnobGlideS, sr);
    const double* fb = mFeedback.Fill(mG[0], numFrames, coef);
    const double* mix = mMix.Fill(mG[1], numFrames, coef);
    const double* width = mWidth.Fill(mG[2], numFrames, coef);
    // Delay times glide slower, like turning a delay knob on the rack: a little pitch bend, no click.
    const double* delA = mDelay[0].Fill(mG[3], numFrames, GlideCoef(0.12, sr));
    const double* delB = mDelay[1].Fill(mG[4], numFrames, GlideCoef(0.12, sr));
    const double hiCut = std::clamp(mParams.hiCutHz, 1000.0, 15000.0);
    if (hiCut != mHiCutSet)
    {
      // The converters' 15 kHz top end (4th-order), with Hi Cut under it.
      for (auto& v : mVoice)
      {
        v.lp[0].LowPass(sr, hiCut, 0.5412);
        v.lp[1].LowPass(sr, hiCut, 1.3066);
      }
      mHiCutSet = hiCut;
    }
    const double modeStep = 1.0 / (0.012 * sr);
    const double semiCoef = GlideCoef(0.012, sr), diaCoef = GlideCoef(0.006, sr);
    for (int s = 0; s < numFrames; s++)
    {
      // A mode (or Splice) change: the wet fades out (12 ms), the taps start over, and it fades back in.
      if (mWantMode != mMode || mWantSplice != mSplice)
      {
        mModeGain -= modeStep;
        if (mModeGain <= 0.0)
        {
          mModeGain = 0.0;
          mMode = mWantMode;
          mSplice = mWantSplice;
          _ResetTaps();
        }
      }
      else
        mModeGain = std::min(1.0, mModeGain + modeStep);
      const double xL = inputs[0][s], xR = numChannels > 1 ? inputs[1][s] : xL;
      const double mono = 0.5 * (xL + xR);
      // Each voice's shift, in semitones.
      double want[2];
      const double fine = std::clamp(mParams.fineCents, 0.0, 50.0) / 100.0;
      switch (mMode)
      {
        case kMicro:
          want[0] = fine;
          want[1] = -fine;
          break;
        case kDiatonic:
        {
          mTracker.Push(mono);
          const int note = mTracker.Note() >= 0 ? mTracker.Note() : 60 + mParams.key;
          want[0] = DiatonicSemis(note, mParams.key, mParams.scale, mParams.diaA - 7) + fine;
          want[1] = DiatonicSemis(note, mParams.key, mParams.scale, mParams.diaB - 7) - fine;
          break;
        }
        default:
          want[0] = std::clamp(mParams.semiA, -24.0, 24.0) + fine;
          want[1] = std::clamp(mParams.semiB, -24.0, 24.0) - fine;
          break;
      }
      const bool reverse = mMode == kReverse;
      double wet[2];
      for (int k = 0; k < 2; k++)
      {
        Voice& v = mVoice[k];
        mWantSemi[k] = want[k];
        if (!v.started)
        {
          v.semi = want[k];
          v.started = true;
        }
        v.semi += (mMode == kDiatonic ? diaCoef : semiCoef) * (want[k] - v.semi);
        const double ratio = std::exp2(v.semi / 12.0);
        const double delayMs = k == 0 ? delA[s] : delB[s];
        v.in.Push(mono + fb[s] * v.fb);
        double grainN;
        if (reverse)
          grainN = std::max(0.06, delayMs * 0.001) * sr; // the reverse window
        else if (mSplice == 1)
          grainN = 0.035 * sr; // Vintage: the short fixed splice of the earliest shifters
        else
          grainN = (mMode == kMicro ? 0.08 : 0.05) * sr;
        // (The mode fade goes on before the delay, so the delay doesn't replay the restart.)
        double y = _Shift(v, ratio, reverse, grainN) * mModeGain;
        y = v.lp[1].Process(v.lp[0].Process(y));
        v.out.Push(y);
        const double delayed = reverse ? y : v.out.Read(std::max(1.0, delayMs * 0.001 * sr));
        v.fb = delayed;
        wet[k] = delayed;
      }
      // A left, B right (equal power), Width brings them in to the middle.
      const double w = width[s];
      const double aL = std::cos((1.0 - w) * 0.25 * kPedalsPi), aR = std::sin((1.0 - w) * 0.25 * kPedalsPi);
      const double wetG = std::min(1.0, 2.0 * mix[s]), dryG = std::min(1.0, 2.0 * (1.0 - mix[s]));
      mBuf.out[0][s] = static_cast<sample>(dryG * xL + wetG * (aL * wet[0] + aR * wet[1]));
      mBuf.out[1][s] = static_cast<sample>(dryG * xR + wetG * (aR * wet[0] + aL * wet[1]));
    }
    return mBuf.ptrs.data();
  }

private:
  struct Tap
  {
    double d = 2.0; // samples back
    double phase = 0.0; // 0..1 through its grain
  };
  struct Voice
  {
    HermiteDelay in, out;
    std::array<Tap, 2> tap;
    Biquad lp[2];
    double fb = 0.0, semi = 0.0;
    bool started = false;
  };

  void _ResetTaps()
  {
    for (auto& v : mVoice)
    {
      v.tap[0] = {2.0, 0.0};
      v.tap[1] = {2.0, 0.5};
    }
  }

  // The splicing shifter. Each tap moves through the memory at the voice's speed (ratio) and is
  // heard through a sin^2 window over its grain; the two are half a grain apart, so their windows sum
  // to one. When a tap's grain ends (its window is at zero) it jumps back, to where the waveform best
  // matches the other tap, which is at the top of its window right then.
  double _Shift(Voice& v, double ratio, bool reverse, double grainN)
  {
    if (!reverse && mSplice == 1)
    {
      // Vintage: the earliest shifters' delay-line form. Each tap sweeps through a fixed window at the new
      // speed and wraps around; its crossfade follows where it is in the window, not a clock. Small
      // detunes wrap only every few seconds, so the two taps' comb filter sweeps slowly: their warble.
      // (Tap 0 sweeps; tap 1 is always half a window away, so their windows sum to one.)
      double x0 = std::fmod(v.tap[0].d - 2.0 + (1.0 - ratio), grainN);
      if (x0 < 0.0)
        x0 += grainN;
      double y = 0.0;
      for (int i = 0; i < 2; i++)
      {
        const double x = i == 0 ? x0 : std::fmod(x0 + 0.5 * grainN, grainN);
        v.tap[i].d = 2.0 + x;
        const double g = std::sin(kPedalsPi * x / grainN);
        y += g * g * v.in.Read(2.0 + x);
      }
      return y;
    }
    const double inc = 1.0 / grainN;
    double y = 0.0;
    for (int i = 0; i < 2; i++)
    {
      Tap& t = v.tap[i];
      t.phase += inc;
      t.d += reverse ? 1.0 + ratio : 1.0 - ratio;
      if (t.phase >= 1.0)
      {
        t.phase -= 1.0;
        _Reseat(v, i, ratio, reverse, grainN);
      }
      const double g = std::sin(kPedalsPi * t.phase);
      y += g * g * v.in.Read(t.d);
    }
    return y;
  }

  void _Reseat(Voice& v, int i, double ratio, bool reverse, double grainN)
  {
    Tap& t = v.tap[i];
    if (reverse)
    {
      t.d = 2.0; // Starts at the newest sound and reads back through the window
      return;
    }
    // Going up, the tap catches up (ratio - 1) samples a sample: start that far back.
    const double base = 2.0 + (ratio > 1.0 ? (ratio - 1.0) * grainN : 0.0);
    const Tap& o = v.tap[1 - i];
    const int span = (int)(0.0125 * mSampleRate); // one period of the lowest guitar note
    const int len = (int)(0.008 * mSampleRate);
    const double frac = o.d - std::floor(o.d);
    const int od = (int)std::floor(o.d);
    const int b0 = (int)std::floor(base);
    const auto& buf = v.in.buf;
    const int pos = v.in.pos, mask = v.in.mask;
    double best = -1e300;
    int bestK = 0;
    for (int k = 0; k <= span; k += 2)
    {
      const int cd = b0 + k;
      double c = 0.0, e = 0.0;
      for (int j = 0; j < len; j += 2)
      {
        const double a = buf[(pos - od - j) & mask], b = buf[(pos - cd - j) & mask];
        c += a * b;
        e += b * b;
      }
      const double score = c / std::sqrt(e + 1e-20);
      if (score > best)
      {
        best = score;
        bestK = k;
      }
    }
    t.d = b0 + bestK + frac;
  }

  double mSampleRate = 48000.0;
  PitchRackParams mParams;
  int mMode = kMicro, mWantMode = kMicro, mSplice = 0, mWantSplice = 0;
  double mModeGain = 1.0, mHiCutSet = -1.0;
  double mWantSemi[2] = {0.0, 0.0};
  std::array<Voice, 2> mVoice;
  RackPitchTracker mTracker;
  Glide mFeedback, mMix, mWidth, mDelay[2];
  std::vector<double> mG[5];
  PedalBuffers mBuf;
};

// PITCH RACK presets (1.77.0; five more and Splice in 1.78.0): the studio rack sounds of the 80s, voiced for a distorted guitar (put PITCH
// RACK after the AMP and CAB, where the rack sat). Each sets every knob but On; Key and Scale only where
// `setsKey` (the Diatonic ones, in E minor, the key a lot of those records are in), so the others leave your
// key alone. Diatonic intervals: 7 Unison, 9 +3rd, 11 +5th, 14 +Oct (see PitchRackParams::diaA).
struct PitchRackPreset
{
  const char* name;
  const char* description;
  PitchRackParams p;
  bool setsKey;
};
inline PitchRackParams RackPreset(int mode, double semiA, double semiB, int diaA, int diaB, double fine, double delA,
                                  double delB, double fb01, double mix01, double width01, double hiCut, int key = 0,
                                  int scale = 0, int splice = 0)
{
  PitchRackParams p;
  p.mode = mode;
  p.semiA = semiA;
  p.semiB = semiB;
  p.diaA = diaA;
  p.diaB = diaB;
  p.fineCents = fine;
  p.delayAms = delA;
  p.delayBms = delB;
  p.feedback01 = fb01;
  p.mix01 = mix01;
  p.width01 = width01;
  p.hiCutHz = hiCut;
  p.key = key;
  p.scale = scale;
  p.splice = splice;
  return p;
}
// clang-format off
static const PitchRackPreset kPitchRackPresets[] = {
  // Doubling: the session player's trick of a second "take" from the rack. Short, different delays on
  // each side so the two voices never line up, a few cents apart, under the dry.
  {"Nashville Double", "Session-style doubling: two slightly detuned takes, 9 and 16 ms, either side of you.",
   RackPreset(0, 0, 0, 7, 7, 7.0, 9, 16, 0.0, 0.38, 1.0, 15000), false},
  {"Tight Thickener", "Barely-there width for rhythm: 5 cents, 5 and 8 ms. Big without sounding like an effect.",
   RackPreset(0, 0, 0, 7, 7, 5.0, 5, 8, 0.0, 0.30, 1.0, 15000), false},
  {"80s Wall Of Rhythm", "The huge stereo distorted rhythm: 10 cents, 14 and 23 ms, top end rolled off the voices.",
   RackPreset(0, 0, 0, 7, 7, 10.0, 14, 23, 0.0, 0.45, 1.0, 9000), false},
  {"Detuned Lead Echo", "The 80s solo rig: detuned stereo repeats at 380 and 510 ms that sit behind the lead.",
   RackPreset(0, 0, 0, 7, 7, 9.0, 380, 510, 0.30, 0.40, 1.0, 7000), false},
  {"Chorus Detune", "A lush chorus spread with no LFO wobble: 16 cents, 22 and 31 ms. Great on cleans too.",
   RackPreset(0, 0, 0, 7, 7, 16.0, 22, 31, 0.0, 0.50, 1.0, 10000), false},
  // Fixed intervals.
  {"Octave Up Lead", "An octave above the lead on both sides, a few cents apart: the synth-like solo voice.",
   RackPreset(1, 12, 12, 7, 7, 6.0, 0, 10, 0.0, 0.35, 0.8, 9000), false},
  {"Octave Down Thick", "An octave below, both sides: a baritone weight under riffs.",
   RackPreset(1, -12, -12, 7, 7, 5.0, 0, 12, 0.0, 0.35, 0.6, 6000), false},
  {"Fifths And Octaves", "A fifth left, an octave right: power chords from single notes.",
   RackPreset(1, 7, 12, 7, 7, 0.0, 0, 0, 0.0, 0.35, 1.0, 9000), false},
  // Diatonic: the twin-guitar harmonies.
  {"Twin Harmony 3rds", "A harmony a third up in E minor on the left, a detuned double on the right. Set Key/Scale to your song.",
   RackPreset(2, 0, 0, 9, 7, 6.0, 12, 20, 0.0, 0.45, 1.0, 12000, 4, 1), true},
  {"Harmony 3rds And 5ths", "Three-part lead: a third up left, a fifth up right, in E minor. Set Key/Scale to your song.",
   RackPreset(2, 0, 0, 9, 11, 0.0, 0, 8, 0.0, 0.40, 1.0, 12000, 4, 1), true},
  // Feedback through the shifter.
  {"Pitch Staircase", "Repeats that climb a fourth (left) and a fifth (right) every time around.",
   RackPreset(1, 5, 7, 7, 7, 0.0, 300, 450, 0.60, 0.40, 1.0, 8000), false},
  {"Crystal Echoes", "Reversed windows an octave and a fifth up, cascading: the shimmering crystal sound.",
   RackPreset(3, 12, 7, 7, 7, 0.0, 350, 520, 0.50, 0.40, 1.0, 9000), false},
  {"Reverse Lead", "Your lead played backwards, slightly detuned, behind you.",
   RackPreset(3, 0, 0, 7, 7, 8.0, 450, 600, 0.30, 0.45, 1.0, 10000), false},
  // 1.78.0.
  {"Chorus Slap", "A chorused slapback: both sides detuned 11 cents at 110 and 130 ms, one soft repeat.",
   RackPreset(0, 0, 0, 7, 7, 11.0, 110, 130, 0.15, 0.42, 1.0, 8000), false},
  {"Cool Presence", "Air on top: a quiet octave up, a few cents apart, tucked right under you. Brightens without EQ.",
   RackPreset(1, 12, 12, 7, 7, 4.0, 0, 3, 0.0, 0.18, 0.7, 15000), false},
  {"Crystal Echoes 2", "Both sides an octave up, reversed in long windows, cascading higher and darker.",
   RackPreset(3, 12, 12, 7, 7, 5.0, 600, 800, 0.55, 0.38, 1.0, 7000), false},
  {"Guitar Room", "A small-room ambience from short, slightly detuned reflections. Add SKY VERB (Room) for a real room.",
   RackPreset(1, 0, 0, 7, 7, 3.0, 23, 31, 0.45, 0.30, 1.0, 6000), false},
  {"Dual Vintage Shifters", "Two of the earliest shifters, one each side, 12 cents apart: Vintage splices, warble and all.",
   RackPreset(1, 0, 0, 7, 7, 12.0, 0, 15, 0.0, 0.45, 1.0, 9000, 0, 0, 1), false},
};
// clang-format on
constexpr int kNumPitchRackPresets = (int)(sizeof(kPitchRackPresets) / sizeof(kPitchRackPresets[0]));

} // namespace pedals
