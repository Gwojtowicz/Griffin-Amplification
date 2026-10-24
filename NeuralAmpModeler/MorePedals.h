#pragma once

// Pedals added in 1.66.0, modelled on how classic pedals behave (not copies of their circuits):
//   TrackGate    -- a three-model noise gate: Tracking (in the spirit of the ISP Decimator: its release
//                   follows how fast the note is dying, so it never chops a sustain), Classic (after the
//                   Boss NS-2: threshold and decay, a fast hard gate) and Smart (after the MXR Smart
//                   Gate: Hiss gates only the top end, Mid the mids and up, Full everything). Plus
//                   Hold, Range, a downward-expander Ratio and Key input.
//   DigitalDelay -- after the Boss DD-3 / DD-8: clean repeats with the DD-3's ~7 kHz bandwidth,
//                   Standard, Mod (chorused repeats), Warm (darker, softly saturating) or Reverse.
//   AnalogDelay  -- after the MXR Carbon Copy: bucket-brigade repeats that get darker the longer the
//                   delay (the BBD's clock drops), soft-saturating regen that self-oscillates past
//                   noon-and-a-bit, and the Mod switch's slow wobble (Rate, Width).
//   TapeEcho     -- after the Boss RE-20 / Roland Space Echo: three playback heads (1:2:3 spacing),
//                   tape speed (Rate), Intensity, wow and flutter, tape saturation, and a spring.
//   SpringVerb   -- after a '63 Fender Reverb / Boss FRV-1: two dispersive springs (a chain of
//                   allpasses in a feedback loop gives the "drip" chirp), Dwell drives the spring's
//                   transducer (more level, more grit, and the boing when you dig in), Tone, Mix,
//                   Decay.
// Same call convention as Pedals.h / NewPedals.h: Process(inputs, numChannels, numFrames) returns
// the pedal's own output buffers; the knobs glide (pedals::Glide).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "NewPedals.h"

namespace pedals
{

// A power-of-two delay line, read at fractional positions with 4-point Hermite interpolation.
struct HermiteDelay
{
  std::vector<double> buf = std::vector<double>(8, 0.0);
  int mask = 7, pos = 0;
  void Resize(int minLength)
  {
    int n = 8;
    while (n < minLength)
      n <<= 1;
    buf.assign(n, 0.0);
    mask = n - 1;
    pos = 0;
  }
  void Clear() { std::fill(buf.begin(), buf.end(), 0.0); }
  void Push(double x)
  {
    pos = (pos + 1) & mask;
    buf[pos] = x;
  }
  // `samples` back from the newest (>= 1).
  double Read(double samples) const
  {
    samples = std::clamp(samples, 1.0, (double)mask - 3.0);
    const double rp = (double)pos - samples;
    const double fl = std::floor(rp);
    const int i1 = (int)fl;
    const double t = rp - fl;
    const double y0 = buf[(i1 - 1) & mask], y1 = buf[i1 & mask], y2 = buf[(i1 + 1) & mask], y3 = buf[(i1 + 2) & mask];
    const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
  }
};

inline double OnePoleCoef(double hz, double sr) { return 1.0 - std::exp(-2.0 * kPedalsPi * std::min(hz, 0.45 * sr) / sr); }

// ============================================================================================
// TrackGate.
class TrackGate
{
public:
  enum Model
  {
    kTracking = 0,
    kClassic,
    kSmart
  };

  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    Reset();
  }
  void Reset()
  {
    mEnv = 0.0;
    mGainDB = 0.0;
    mHold = 0;
    mOpen = true;
    mPrevEnvDB = -120.0;
    mSlope = 0.0;
    for (auto& c : mSplit)
      c = {};
  }

  // threshDB -90..-20; release01 (Classic: decay 5 ms..1.5 s; Tracking/Smart: how much longer than
  // the note's own decay it waits); holdMs; rangeDB (how far down when closed, -90..0); ratio
  // (downward expander 1.5..20; 20 is a gate); noise (Smart: 0 Hiss, 1 Mid, 2 Full).
  void SetParams(int model, double threshDB, double release01, double holdMs, double rangeDB, double ratio, int noise)
  {
    mModel = std::clamp(model, 0, 2);
    mThreshDB = threshDB;
    mRelease01 = std::clamp(release01, 0.0, 1.0);
    mHoldMs = std::clamp(holdMs, 0.0, 1000.0);
    mRangeDB = std::clamp(rangeDB, -90.0, 0.0);
    mRatio = std::clamp(ratio, 1.2, 20.0);
    mNoise = std::clamp(noise, 0, 2);
  }

  // key: a detector signal (e.g. the clean input), or null for the pedal's own input.
  sample** Process(sample** inputs, int numChannels, int numFrames, sample* const* key = nullptr, int keyChannels = 0)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    // Detector: peak with a fast attack and a ~10 ms decay, in dB; its slope (dB/s) is the note's decay rate.
    const double envRelease = std::exp(-1.0 / (0.010 * sr));
    const double attackMs = mModel == kClassic ? 1.0 : mModel == kSmart ? 0.5 : 0.3;
    const double attackC = 1.0 - std::exp(-1.0 / (attackMs * 0.001 * sr));
    const double slopeC = 1.0 - std::exp(-1.0 / (0.05 * sr));
    const double hyst = 4.0; // dB under the threshold before it closes (no chatter)
    const int holdN = (int)(mHoldMs * 0.001 * sr);
    // Crossovers for Smart: low < 250 Hz < mid < 3 kHz < high (complementary one-poles: bands sum flat).
    const double cLo = OnePoleCoef(250.0, sr), cHi = OnePoleCoef(3000.0, sr);
    for (int s = 0; s < numFrames; s++)
    {
      double peak = 0.0;
      if (key && keyChannels > 0)
        for (int c = 0; c < keyChannels; c++)
          peak = std::max(peak, std::fabs((double)key[c][s]));
      else
        for (int c = 0; c < numChannels; c++)
          peak = std::max(peak, std::fabs((double)inputs[c][s]));
      mEnv = peak > mEnv ? peak : mEnv * envRelease;
      const double envDB = 20.0 * std::log10(mEnv + 1e-12);
      mSlope += slopeC * ((envDB - mPrevEnvDB) * sr - mSlope);
      mPrevEnvDB = envDB;
      // Open / closed, with hysteresis and hold.
      if (envDB > mThreshDB)
      {
        mOpen = true;
        mHold = holdN;
      }
      else if (envDB < mThreshDB - hyst)
      {
        if (mHold > 0)
          mHold--;
        else
          mOpen = false;
      }
      // Target gain: 0 dB open; closed, a downward expander under the threshold, floored at Range.
      double target = 0.0;
      if (!mOpen)
        target = std::max(mRangeDB, std::min(0.0, (envDB - mThreshDB) * (mRatio - 1.0)));
      // Release time: Classic is the knob (5 ms..1.5 s). Tracking follows the note: a fast-dying note
      // closes fast, a slowly dying one slowly (25 ms .. 2 s from the decay rate), stretched by the knob.
      double releaseS;
      if (mModel == kClassic)
        releaseS = 0.005 * std::pow(300.0, mRelease01);
      else
      {
        const double rate = std::max(1.0, -mSlope); // dB/s the note is falling at
        releaseS = std::clamp(40.0 / rate, 0.025, 2.0) * (0.5 + 2.5 * mRelease01);
      }
      const double releaseC = 1.0 - std::exp(-1.0 / (releaseS * sr));
      mGainDB += (target > mGainDB ? attackC : releaseC) * (target - mGainDB);
      const double g = std::pow(10.0, mGainDB / 20.0);
      for (int c = 0; c < numChannels; c++)
      {
        const double x = inputs[c][s];
        double y;
        if (mModel != kSmart || mNoise == 2)
          y = x * g;
        else
        {
          // Smart: only the noisy part is gated -- Hiss: above 3 kHz; Mid: above 250 Hz.
          Split& sp = mSplit[std::min(c, 1)];
          sp.lo += cLo * (x - sp.lo);
          sp.loMid += cHi * (x - sp.loMid);
          const double low = sp.lo, high = x - sp.loMid, mid = sp.loMid - sp.lo;
          y = mNoise == 0 ? low + mid + g * high : low + g * (mid + high);
        }
        mBuf.out[c][s] = static_cast<sample>(y);
      }
    }
    return mBuf.ptrs.data();
  }

  bool IsOpen() const { return mOpen; }
  double GainDB() const { return mGainDB; }

private:
  struct Split
  {
    double lo = 0.0, loMid = 0.0;
  };
  double mSampleRate = 48000.0;
  int mModel = kTracking, mNoise = 2;
  double mThreshDB = -60.0, mRelease01 = 0.4, mHoldMs = 20.0, mRangeDB = -90.0, mRatio = 8.0;
  double mEnv = 0.0, mGainDB = 0.0, mPrevEnvDB = -120.0, mSlope = 0.0;
  int mHold = 0;
  bool mOpen = true;
  std::array<Split, 2> mSplit;
  PedalBuffers mBuf;
};

// ============================================================================================
// DigitalDelay.
class DigitalDelay
{
public:
  enum Mode
  {
    kStandard = 0,
    kMod,
    kWarm,
    kReverse
  };

  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    for (auto& d : mLine)
      d.Resize((int)(3.7 * sr) + 16); // Reverse reads up to 3x the time back
    for (auto& l : mLp)
      l = 0.0;
    mPhase = 0.0;
    mRevPos = 0.0;
    mXfade = 1.0;
    mCurMs = mWantMs;
    mOldMs = mWantMs;
  }

  // timeMs 20..1200, feedback01, level01 (the repeats' level; the dry signal is unchanged).
  void SetParams(int mode, double timeMs, double feedback01, double level01)
  {
    mMode = std::clamp(mode, 0, 3);
    mWantMs = std::clamp(timeMs, 20.0, 1200.0);
    mFeedback.Set(std::clamp(feedback01, 0.0, 1.0) * 0.98);
    mLevel.Set(std::clamp(level01, 0.0, 1.0) * 1.2);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    const double coef = GlideCoef(kKnobGlideS, sr);
    const double* fb = mFeedback.Fill(mG[0], numFrames, coef);
    const double* level = mLevel.Fill(mG[1], numFrames, coef);
    const double bwC = OnePoleCoef(mMode == kWarm ? 2800.0 : 7000.0, sr);
    const double xfStep = 1.0 / (0.03 * sr);
    for (int s = 0; s < numFrames; s++)
    {
      // A new time crossfades from the old read point over 30 ms (no pitch sweep).
      if (mXfade >= 1.0 && std::fabs(mWantMs - mCurMs) > 0.01)
      {
        mOldMs = mCurMs;
        mCurMs = mWantMs;
        mXfade = 0.0;
      }
      mXfade = std::min(1.0, mXfade + xfStep);
      mPhase += 0.6 / sr;
      if (mPhase >= 1.0)
        mPhase -= 1.0;
      // Reverse: read backwards through each window of the delay time, with a short fade at the ends.
      double revEnv = 1.0, revOffset = 0.0;
      const double windowN = mCurMs * 0.001 * sr;
      if (mMode == kReverse)
      {
        mRevPos += 1.0;
        if (mRevPos >= windowN)
          mRevPos = 0.0;
        revOffset = 2.0 * mRevPos; // Going back twice as fast as time moves: plays the window backwards
        const double edge = std::min(mRevPos, windowN - mRevPos) / std::max(1.0, 0.01 * sr);
        revEnv = std::min(1.0, edge);
      }
      for (int c = 0; c < numChannels; c++)
      {
        HermiteDelay& line = mLine[c];
        const double mod = mMode == kMod ? (0.0015 * sr) * std::sin(2.0 * kPedalsPi * (mPhase + 0.25 * c)) : 0.0;
        auto at = [&](double ms) { return line.Read(ms * 0.001 * sr + mod + revOffset); };
        double wet = mXfade < 1.0 ? at(mCurMs) * mXfade + at(mOldMs) * (1.0 - mXfade) : at(mCurMs);
        mLp[c] += bwC * (wet - mLp[c]);
        wet = mLp[c] * revEnv;
        const double x = inputs[c][s];
        double feed = x + fb[s] * wet;
        if (mMode == kWarm)
          feed = std::tanh(feed * 1.2) / 1.2;
        // (Reverse plays one reversed repeat of each window; feeding it back would turn it forwards again.)
        line.Push(mMode == kReverse ? x : feed);
        mBuf.out[c][s] = static_cast<sample>(x + level[s] * wet);
      }
    }
    return mBuf.ptrs.data();
  }

private:
  double mSampleRate = 48000.0;
  int mMode = kStandard;
  double mWantMs = 350.0, mCurMs = 350.0, mOldMs = 350.0, mXfade = 1.0, mPhase = 0.0, mRevPos = 0.0;
  std::array<HermiteDelay, 2> mLine;
  double mLp[2] = {0.0, 0.0};
  Glide mFeedback, mLevel;
  std::vector<double> mG[2];
  PedalBuffers mBuf;
};

// ============================================================================================
// AnalogDelay.
class AnalogDelay
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    for (auto& d : mLine)
      d.Resize((int)(0.7 * sr) + 16);
    for (auto& st : mLp)
      st = {0.0, 0.0};
    mPhase = 0.0;
  }

  // timeMs 20..600; regen01 (past ~0.85 it builds up and self-oscillates); mix01; mod; modRateHz; modWidth01.
  void SetParams(double timeMs, double regen01, double mix01, bool mod, double modRateHz, double modWidth01)
  {
    mTime.Set(std::clamp(timeMs, 20.0, 600.0));
    mRegen.Set(std::clamp(regen01, 0.0, 1.0) * 1.12);
    mMix.Set(std::clamp(mix01, 0.0, 1.0));
    mMod = mod;
    mModRate = std::clamp(modRateHz, 0.05, 10.0);
    mModWidth = std::clamp(modWidth01, 0.0, 1.0);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    const double coef = GlideCoef(0.08, sr); // A slow glide on time: the BBD's pitch swoop
    const double* timeMs = mTime.Fill(mG[0], numFrames, coef);
    const double* regen = mRegen.Fill(mG[1], numFrames, GlideCoef(kKnobGlideS, sr));
    const double* mix = mMix.Fill(mG[2], numFrames, GlideCoef(kKnobGlideS, sr));
    for (int s = 0; s < numFrames; s++)
    {
      mPhase += mModRate / sr;
      if (mPhase >= 1.0)
        mPhase -= 1.0;
      // The BBD's bandwidth falls as its clock slows for longer delays: ~6 kHz at 20 ms, ~1.8 kHz at 600 ms.
      const double bw = std::clamp(2400.0 * std::pow(300.0 / timeMs[s], 0.45), 1500.0, 6500.0);
      const double c1 = OnePoleCoef(bw, sr);
      for (int c = 0; c < numChannels; c++)
      {
        const double wob = mMod ? mModWidth * 0.004 * timeMs[s] * std::sin(2.0 * kPedalsPi * (mPhase + 0.3 * c)) : 0.0;
        const double d = (timeMs[s] + wob) * 0.001 * sr;
        double wet = mLine[c].Read(d);
        // Two poles: the reconstruction filter's roll-off.
        mLp[c][0] += c1 * (wet - mLp[c][0]);
        mLp[c][1] += c1 * (mLp[c][0] - mLp[c][1]);
        wet = mLp[c][1];
        const double x = inputs[c][s];
        // Soft saturation in the loop (the compander and BBD headroom): repeats stay musical, and
        // high Regen builds into oscillation instead of exploding.
        mLine[c].Push(std::tanh(1.4 * (x + regen[s] * wet)) / 1.4);
        // Mix is the repeats' level (as on the Carbon Copy): the dry signal is unchanged.
        mBuf.out[c][s] = static_cast<sample>(x + wet * mix[s] * 1.2);
      }
    }
    return mBuf.ptrs.data();
  }

private:
  double mSampleRate = 48000.0, mPhase = 0.0, mModRate = 0.6, mModWidth = 0.4;
  bool mMod = false;
  std::array<HermiteDelay, 2> mLine;
  std::array<std::array<double, 2>, 2> mLp = {};
  Glide mTime, mRegen, mMix;
  std::vector<double> mG[3];
  PedalBuffers mBuf;
};

// ============================================================================================
// A spring tank: a chain of first-order allpasses inside a feedback delay loop. The allpasses delay
// low frequencies more than high ones (dispersion), so each pass round the loop comes back as a
// falling "chirp" -- the drip. Two springs of different lengths per side.
class Spring
{
public:
  static constexpr int kStages = 24;
  void Setup(double sr, double loopMs, double apCoef)
  {
    mSr = sr;
    mLine.Resize((int)(0.08 * sr) + 8);
    mLoopN = loopMs * 0.001 * sr;
    mA = apCoef;
    for (auto& s : mX1)
      s = 0.0;
    for (auto& s : mY1)
      s = 0.0;
    mLp = mHp = mHpX = 0.0;
  }
  void Clear()
  {
    mLine.Clear();
    for (auto& s : mX1)
      s = 0.0;
    for (auto& s : mY1)
      s = 0.0;
    mLp = mHp = mHpX = 0.0;
  }
  // One sample: in -> out; decayGain per loop, lpC (the spring's top-end loss), the loop's modulation.
  double Process(double in, double decayGain, double lpC, double hpC, double mod)
  {
    double v = mLine.Read(mLoopN + mod);
    // Dispersion.
    for (int k = 0; k < kStages; k++)
    {
      const double y = -mA * v + mX1[k] + mA * mY1[k];
      mX1[k] = v;
      mY1[k] = y;
      v = y;
    }
    mLp += lpC * (v - mLp);
    // A gentle high-pass in the loop: the transducers don't pass deep bass.
    mHp = hpC * (mHp + mLp - mHpX);
    mHpX = mLp;
    mLine.Push(in + decayGain * mHp);
    return mHp;
  }

private:
  double mSr = 48000.0, mLoopN = 1500.0, mA = 0.65;
  HermiteDelay mLine;
  double mX1[kStages] = {}, mY1[kStages] = {};
  double mLp = 0.0, mHp = 0.0, mHpX = 0.0;
};

// ============================================================================================
// SpringVerb.
class SpringVerb
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    const double loops[4] = {33.0, 41.0, 37.0, 45.5};
    const double ap[4] = {0.62, 0.66, 0.64, 0.68};
    for (int i = 0; i < 4; i++)
      mSpring[i].Setup(sr, loops[i], ap[i]);
    mPhase = 0.0;
    mDriveLp = {0.0, 0.0};
    mDriveC = OnePoleCoef(5000.0, sr);
  }
  void Clear()
  {
    for (auto& s : mSpring)
      s.Clear();
  }

  // dwell01 (the drive into the spring: level and grit), tone01 (dark .. bright), mix01, decay01.
  void SetParams(double dwell01, double tone01, double mix01, double decay01)
  {
    mDwell.Set(std::clamp(dwell01, 0.0, 1.0));
    mTone = std::clamp(tone01, 0.0, 1.0);
    mMix.Set(std::clamp(mix01, 0.0, 1.0));
    mDecay = std::clamp(decay01, 0.0, 1.0);
  }

  // The wet signal alone for one sample of channel c (for the TapeEcho's built-in spring).
  double Wet(int c, double x, double dwell, double lpC, double hpC, double decayGain)
  {
    // The driver: a tube pushing a transducer -- level up with Dwell, then it saturates. Its top end
    // is rolled off (the spring can't pass much over 5 kHz).
    double drive = x * (0.6 + 6.0 * dwell * dwell);
    mDriveLp[c] += mDriveC * (drive - mDriveLp[c]);
    drive = std::tanh(mDriveLp[c]);
    const double mod = 3.0 * std::sin(2.0 * kPedalsPi * (mPhase + 0.37 * c)); // The springs wobble a little
    const double a = mSpring[2 * c].Process(drive, decayGain, lpC, hpC, mod);
    const double b = mSpring[2 * c + 1].Process(drive, decayGain * 0.97, lpC, hpC, -mod);
    return 0.6 * (a + b);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    const double coef = GlideCoef(kKnobGlideS, sr);
    const double* dwell = mDwell.Fill(mG[0], numFrames, coef);
    const double* mix = mMix.Fill(mG[1], numFrames, coef);
    // Decay: the loop gain from about 1 s to 4 s of RT60 at the ~37 ms loop.
    const double rt60 = 1.0 + 3.0 * mDecay;
    const double decayGain = std::pow(10.0, -3.0 * 0.037 / rt60);
    const double lpC = OnePoleCoef(2500.0 + 4500.0 * mTone, sr);
    const double hpC = std::exp(-2.0 * kPedalsPi * 180.0 / sr);
    for (int s = 0; s < numFrames; s++)
    {
      mPhase += 0.9 / sr;
      if (mPhase >= 1.0)
        mPhase -= 1.0;
      for (int c = 0; c < numChannels; c++)
      {
        const double x = inputs[c][s];
        const double wet = Wet(std::min(c, 1), x, dwell[s], lpC, hpC, decayGain);
        mBuf.out[c][s] = static_cast<sample>(x * (1.0 - 0.4 * mix[s]) + 1.2 * mix[s] * wet);
      }
    }
    return mBuf.ptrs.data();
  }

  void AdvancePhase(double hz) { mPhase = std::fmod(mPhase + hz / mSampleRate, 1.0); }

private:
  double mSampleRate = 48000.0, mPhase = 0.0, mTone = 0.5, mDecay = 0.5, mDriveC = 0.5;
  std::array<Spring, 4> mSpring;
  std::array<double, 2> mDriveLp = {0.0, 0.0};
  Glide mDwell, mMix;
  std::vector<double> mG[2];
  PedalBuffers mBuf;
};

// ============================================================================================
// TapeEcho.
class TapeEcho
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    for (auto& d : mTape)
      d.Resize((int)(1.4 * sr) + 16);
    mSpring.SetSampleRate(sr);
    for (auto& st : mHead)
      st = {0.0, 0.0};
    mWow = mFlutter = mWow1 = mFlutter1 = 0.0;
  }

  // heads 0..6: 1, 2, 3, 1+2, 2+3, 1+2+3, 1+3. rate01 (tape speed: slow = long, dark), intensity01
  // (feedback), echo01 (echo level), reverb01 (spring level), wow01, drive01 (tape saturation).
  void SetParams(int heads, double rate01, double intensity01, double echo01, double reverb01, double wow01,
                 double drive01)
  {
    mHeads = std::clamp(heads, 0, 6);
    mRate.Set(std::clamp(rate01, 0.0, 1.0));
    mIntensity.Set(std::clamp(intensity01, 0.0, 1.0) * 1.05);
    mEcho.Set(std::clamp(echo01, 0.0, 1.0));
    mReverb.Set(std::clamp(reverb01, 0.0, 1.0));
    mWowAmt = std::clamp(wow01, 0.0, 1.0);
    mDrive = std::clamp(drive01, 0.0, 1.0);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    const double* rate = mRate.Fill(mG[0], numFrames, GlideCoef(0.25, sr)); // Tape speed changes slowly (the swoop)
    const double* intensity = mIntensity.Fill(mG[1], numFrames, GlideCoef(kKnobGlideS, sr));
    const double* echo = mEcho.Fill(mG[2], numFrames, GlideCoef(kKnobGlideS, sr));
    const double* reverb = mReverb.Fill(mG[3], numFrames, GlideCoef(kKnobGlideS, sr));
    static const bool kOn[7][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}};
    const bool* on = kOn[mHeads];
    int numOn = 0;
    for (int h = 0; h < 3; h++)
      numOn += on[h] ? 1 : 0;
    const double headGain = 1.0 / std::sqrt((double)std::max(1, numOn));
    const double springLp = OnePoleCoef(4500.0, sr), springHp = std::exp(-2.0 * kPedalsPi * 180.0 / sr);
    const double springDecay = std::pow(10.0, -3.0 * 0.037 / 2.0);
    const double wowC = 1.0 - std::exp(-2.0 * kPedalsPi * 0.8 / sr), flutC = 1.0 - std::exp(-2.0 * kPedalsPi * 12.0 / sr);
    // Two one-poles in a row on uniform noise (std 0.577): the result's std is 0.577 sqrt(c / 4); these scale it to 1.
    const double wowNorm = 1.0 / (0.577 * std::sqrt(wowC / 4.0)), flutNorm = 1.0 / (0.577 * std::sqrt(flutC / 4.0));
    for (int s = 0; s < numFrames; s++)
    {
      // Head 1 at 70..300 ms with the speed (heads 2 and 3 at 2x and 3x). Slower tape is also darker.
      const double head1Ms = 300.0 - 230.0 * rate[s];
      const double tapeBw = 3200.0 + 4500.0 * rate[s];
      const double bwC = OnePoleCoef(tapeBw, sr);
      // Wow (slow) and flutter (fast), from smoothed noise: the capstan and the tape's own wobble.
      mRng = mRng * 1664525u + 1013904223u;
      const double n = ((mRng >> 8) / 16777216.0) * 2.0 - 1.0;
      mWow1 += wowC * (n - mWow1);
      mWow += wowC * (mWow1 - mWow);
      mFlutter1 += flutC * (n - mFlutter1);
      mFlutter += flutC * (mFlutter1 - mFlutter);
      // In ms: at full Wow, about 1.5 ms of drift at 0.8 Hz (~8 cents) and 0.08 ms of flutter at 12 Hz.
      const double wobble = mWowAmt * (1.5 * mWow * wowNorm + 0.08 * mFlutter * flutNorm);
      mSpring.AdvancePhase(0.9);
      for (int c = 0; c < numChannels; c++)
      {
        const double x = inputs[c][s];
        double echoSum = 0.0;
        for (int h = 0; h < 3; h++)
          if (on[h])
          {
            const double ms = head1Ms * (h + 1) + wobble * (1.0 + 0.3 * h) * (c == 1 ? 1.1 : 1.0);
            echoSum += mTape[c].Read(ms * 0.001 * sr);
          }
        echoSum *= headGain;
        auto& lp = mHead[c];
        lp[0] += bwC * (echoSum - lp[0]);
        lp[1] += bwC * (lp[0] - lp[1]);
        const double played = lp[1];
        // Record: the input plus the feedback, onto saturating tape.
        const double rec = x + intensity[s] * played;
        const double k = 1.0 + 3.0 * mDrive;
        mTape[c].Push(std::tanh(k * rec) / k);
        const double springIn = x + 0.5 * played;
        const double spr = mSpring.Wet(std::min(c, 1), springIn, 0.35, springLp, springHp, springDecay);
        mBuf.out[c][s] = static_cast<sample>(x + echo[s] * 1.2 * played + reverb[s] * spr);
      }
    }
    return mBuf.ptrs.data();
  }

private:
  double mSampleRate = 48000.0;
  int mHeads = 0;
  double mWowAmt = 0.3, mDrive = 0.3, mWow = 0.0, mFlutter = 0.0, mWow1 = 0.0, mFlutter1 = 0.0;
  uint32_t mRng = 31337u;
  std::array<HermiteDelay, 2> mTape;
  std::array<std::array<double, 2>, 2> mHead = {};
  SpringVerb mSpring;
  Glide mRate, mIntensity, mEcho, mReverb;
  std::vector<double> mG[4];
  PedalBuffers mBuf;
};

} // namespace pedals
