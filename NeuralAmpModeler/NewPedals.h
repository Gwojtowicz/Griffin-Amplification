#pragma once

// Pedals added in 1.44.0, modelled on how classic pedals behave (not copies of their circuits):
//   SkyVerb        -- a multi-mode studio reverb in the spirit of a Strymon BigSky: Hall, Plate, Room,
//                     Shimmer, Cloud and Bloom, with Decay, Pre-delay, Tone, Mod, Mix and Freeze.
//   FlangerDoubler -- after the MXR 126 Flanger/Doubler: a swept short delay (flanger) or a 15-70 ms
//                     wandering delay (doubler: a second take of the same part), Manual, Width,
//                     Speed, Regen, Mix.
//   Wah            -- a Dunlop Cry Baby, a Vox V847, or an envelope (auto) wah: a swept resonant
//                     filter. Its Position is meant for an expression pedal (MIDI learn).
//   PrecisionDrive -- after the Horizon Devices Precision Drive: a tight overdrive/boost for high
//                     gain amps, with Attack (how much low end goes into the clipping) and a gate.
//   ApexPreamp     -- after the Horizon Devices Apex Preamp: a three-stage high-gain preamp with
//                     a three-band EQ and Presence.
// Same call convention as Pedals.h: Process(inputs, numChannels, numFrames) returns the pedal's
// own output buffers; the knobs glide (pedals::Glide).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Pedals.h"

namespace pedals
{

// RBJ biquad, transposed direct form II, in double.
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
  void Reset() { z1 = z2 = 0.0; }
  void _Set(double nb0, double nb1, double nb2, double na0, double na1, double na2)
  {
    b0 = nb0 / na0;
    b1 = nb1 / na0;
    b2 = nb2 / na0;
    a1 = na1 / na0;
    a2 = na2 / na0;
  }
  void HighPass(double sr, double f, double q = 0.7071)
  {
    const double w = 2.0 * kPedalsPi * std::min(f, 0.45 * sr) / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
    _Set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
  }
  void LowPass(double sr, double f, double q = 0.7071)
  {
    const double w = 2.0 * kPedalsPi * std::min(f, 0.45 * sr) / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
    _Set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
  }
  void Peak(double sr, double f, double q, double dB)
  {
    const double A = std::pow(10.0, dB / 40.0), w = 2.0 * kPedalsPi * std::min(f, 0.45 * sr) / sr;
    const double c = std::cos(w), al = std::sin(w) / (2.0 * q);
    _Set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
  }
  void Shelf(double sr, double f, double dB, bool high)
  {
    const double A = std::pow(10.0, dB / 40.0), w = 2.0 * kPedalsPi * std::min(f, 0.45 * sr) / sr;
    const double c = std::cos(w), s = std::sin(w), al = s / 2.0 * std::sqrt(2.0), sa = 2.0 * std::sqrt(A) * al;
    if (high)
      _Set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
           (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    else
      _Set(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
           (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
  }
};

// tanh with first-order antiderivative anti-aliasing (much less aliasing from hard drive, no
// oversampling): y = (F(x) - F(x1)) / (x - x1), F = log(cosh).
struct AdaaTanh
{
  double x1 = 0.0;
  // Start at rest at x0 (a biased stage's resting input), so the first sample isn't a step.
  void Reset(double x0 = 0.0) { x1 = x0; }
  static double F(double x)
  {
    const double a = std::fabs(x);
    return a > 20.0 ? a - 0.6931471805599453 : std::log(std::cosh(x));
  }
  double Process(double x)
  {
    const double d = x - x1;
    const double y = std::fabs(d) < 1e-5 ? std::tanh(0.5 * (x + x1)) : (F(x) - F(x1)) / d;
    x1 = x;
    return y;
  }
};

// Output buffers, the same way every pedal in Pedals.h keeps them.
struct PedalBuffers
{
  std::vector<std::vector<sample>> out;
  std::vector<sample*> ptrs;
  void Ensure(int numChannels, int numFrames)
  {
    if ((int)out.size() != numChannels)
      out.resize(numChannels);
    for (auto& ch : out)
      if ((int)ch.size() < numFrames)
        ch.resize(numFrames);
    if ((int)ptrs.size() != numChannels)
      ptrs.resize(numChannels);
    for (int c = 0; c < numChannels; c++)
      ptrs[c] = out[c].data();
  }
};

// ============================================================================================
// SkyVerb: an 8-line feedback delay network (Householder mixing, per-line damping and slow
// modulation) after a pre-delay and input diffusers. Modes change the line lengths, diffusion and
// modulation; Shimmer feeds an octave-up pitch shift of the tank back into it; Bloom swells the
// reverb's input in after each note; Freeze holds the tank (no input, no loss).
class SkyVerb
{
public:
  enum Mode
  {
    kHall = 0,
    kPlate,
    kRoom,
    kShimmer,
    kCloud,
    kBloom,
    kNumModes
  };

  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    const int maxLine = (int)std::ceil(0.16 * 1.03 * sr) + 8;
    for (auto& l : mLines)
      l.assign(maxLine, 0.0);
    mPre.assign((int)std::ceil(0.52 * sr) + 4, 0.0);
    for (auto& d : mDiff)
      d.assign((int)std::ceil(0.03 * sr) + 4, 0.0);
    mShift.assign((int)std::ceil(0.12 * sr) + 4, 0.0);
    mLinePos = mPrePos = mShiftPos = 0;
    mDiffPos.fill(0);
    mDamp.fill(0.0);
    mPhase.fill(0.0);
    for (int i = 0; i < kN; i++)
      mPhase[i] = 0.137 * i;
    mShiftPhase = 0.0;
    mShiftLP = 0.0;
    mBloomSlow = mBloomFast = 0.0;
    mOutHP[0].HighPass(sr, 40.0);
    mOutHP[1].HighPass(sr, 40.0);
    mOutHP[0].Reset();
    mOutHP[1].Reset();
    mModeGain = 1.0;
    mCurMode = mWantMode;
  }

  // mode: Mode; decay01, tone01, mod01, mix01: 0..1; predelayMs: 0..500.
  void SetParams(int mode, double decay01, double predelayMs, double tone01, double mod01, double mix01, bool freeze)
  {
    mWantMode = std::clamp(mode, 0, kNumModes - 1);
    mDecay.Set(std::clamp(decay01, 0.0, 1.0));
    mPreTarget = std::clamp(predelayMs, 0.0, 500.0);
    if (!mPreStarted)
    {
      mPreNow = mPreNext = mPreTarget;
      mPreStarted = true;
    }
    mTone.Set(std::clamp(tone01, 0.0, 1.0));
    mMod.Set(std::clamp(mod01, 0.0, 1.0));
    mMix.Set(std::clamp(mix01, 0.0, 1.0));
    mFreeze.Set(freeze ? 1.0 : 0.0);
  }

  // Decay knob (0..1) to RT60 in seconds: 0.3 s .. 40 s, exponential.
  static double DecaySeconds(double decay01) { return 0.3 * std::pow(40.0 / 0.3, decay01); }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    const double coef = GlideCoef(kKnobGlideS, sr);
    const double* decay = mDecay.Fill(mG[0], numFrames, coef);
    const double* tone = mTone.Fill(mG[2], numFrames, coef);
    const double* mod = mMod.Fill(mG[3], numFrames, coef);
    const double* mix = mMix.Fill(mG[4], numFrames, coef);
    const double* freeze = mFreeze.Fill(mG[5], numFrames, GlideCoef(0.03, sr));
    const int lineSize = (int)mLines[0].size();
    const int preSize = (int)mPre.size();
    const double modeFade = 1.0 / (0.015 * sr);

    for (int s = 0; s < numFrames; s++)
    {
      // A new mode: fade the wet out, switch at silence, fade back in (the line lengths jump).
      if (mWantMode != mCurMode)
      {
        mModeGain -= modeFade;
        if (mModeGain <= 0.0)
        {
          mModeGain = 0.0;
          mCurMode = mWantMode;
        }
      }
      else if (mModeGain < 1.0)
        mModeGain = std::min(1.0, mModeGain + modeFade);
      const ModeSpec& m = kModes[mCurMode];

      double inMono = 0.0;
      for (int c = 0; c < numChannels; c++)
        inMono += inputs[c][s];
      inMono /= numChannels;

      // Bloom: the reverb's input swells in after each note (a slow envelope over a fast one).
      double feed = inMono * (1.0 - freeze[s]);
      if (mCurMode == kBloom)
      {
        const double a = std::fabs(inMono);
        const double fastA = 1.0 - std::exp(-1.0 / (0.005 * sr)), fastR = 1.0 - std::exp(-1.0 / (0.25 * sr));
        mBloomFast += (a > mBloomFast ? fastA : fastR) * (a - mBloomFast);
        const double slowC = 1.0 - std::exp(-1.0 / ((0.5 + 1.5 * decay[s]) * sr));
        mBloomSlow += slowC * (a - mBloomSlow);
        feed *= std::min(1.0, mBloomSlow / (mBloomFast + 1e-9));
      }

      // Pre-delay. A new time crossfades (30 ms) from the old read point to the new one: gliding it
      // swept the pitch (a zip on a big change).
      mPre[mPrePos] = feed;
      if (mPreFade <= 0.0 && mPreNow != mPreTarget)
      {
        mPreNext = mPreTarget;
        mPreFade = 1e-9;
      }
      auto readPre = [&](double ms) {
        double rp = mPrePos - std::clamp(ms * 0.001 * sr, 0.0, (double)preSize - 2.0);
        if (rp < 0)
          rp += preSize;
        const int ri = (int)rp;
        const double rf = rp - ri;
        return mPre[ri] * (1.0 - rf) + mPre[(ri + 1) % preSize] * rf;
      };
      double x = readPre(mPreNow);
      if (mPreFade > 0.0)
      {
        mPreFade = std::min(1.0, mPreFade + 1.0 / (0.03 * sr));
        const double g = mPreFade * mPreFade * (3.0 - 2.0 * mPreFade);
        x += g * (readPre(mPreNext) - x);
        if (mPreFade >= 1.0)
        {
          mPreNow = mPreNext;
          mPreFade = 0.0;
        }
      }
      mPrePos = (mPrePos + 1) % preSize;

      // Input diffusion: four allpasses.
      for (int k = 0; k < 4; k++)
      {
        auto& d = mDiff[k];
        const int len = std::max(1, (int)(m.diffMs[k] * 0.001 * sr));
        int& pos = mDiffPos[k];
        int rd = pos - len;
        if (rd < 0)
          rd += (int)d.size();
        const double v = d[rd];
        const double g = m.diffG;
        const double w = x + g * v;
        d[pos] = w;
        x = v - g * w;
        pos = (pos + 1) % (int)d.size();
      }

      // Shimmer: the tank's octave-up, fed back in (the classic rising shimmer).
      if (mCurMode == kShimmer)
        x += mShiftOut * (0.15 + 0.45 * mod[s]);

      // Tank. Loss per pass from RT60 and the damping, updated every 32 samples (they glide).
      if ((s & 31) == 0 || mCoefMode != mCurMode)
      {
        // (x1.2: the damping takes energy too; measured RT60s came out ~18% short without it.)
        const double rt60 = 1.2 * DecaySeconds(decay[s]);
        for (int i = 0; i < kN; i++)
          mLineGain[i] = std::pow(10.0, -3.0 * (m.lineMs[i] * 0.001) / rt60);
        const double fc = 1500.0 * std::pow(16000.0 / 1500.0, tone[s]) * m.dampScale;
        mDampA = std::exp(-2.0 * kPedalsPi * std::min(fc, 0.45 * sr) / sr);
        mCoefMode = mCurMode;
      }
      const double dampA = mDampA;
      const double modDepth = (mCurMode == kShimmer ? 0.3 : mod[s]) * m.modDepth;
      double read[kN];
      double sum = 0.0;
      for (int i = 0; i < kN; i++)
      {
        mPhase[i] += (m.modHz * (0.7 + 0.09 * i)) / sr;
        if (mPhase[i] >= 1.0)
          mPhase[i] -= 1.0;
        double len = m.lineMs[i] * 0.001 * sr * (1.0 + modDepth * (1.0 - freeze[s]) * std::sin(2.0 * kPedalsPi * mPhase[i]));
        // Frozen: whole-sample lengths, so the loop doesn't lose top end to interpolation each pass.
        if (freeze[s] > 0.999)
          len = std::round(len);
        double p = mLinePos - std::clamp(len, 2.0, (double)lineSize - 3.0);
        if (p < 0)
          p += lineSize;
        const int i0 = (int)p;
        const double f = p - i0;
        const auto& line = mLines[i];
        double v = line[i0] * (1.0 - f) + line[(i0 + 1) % lineSize] * f;
        // No loss and no damping while frozen.
        const double g = mLineGain[i];
        const double gain = g + (1.0 - g) * freeze[s];
        const double damp = dampA * (1.0 - freeze[s]);
        mDamp[i] = v * (1.0 - damp) + mDamp[i] * damp;
        read[i] = mDamp[i] * gain;
        sum += read[i];
      }
      const double householder = 2.0 / kN * sum;
      for (int i = 0; i < kN; i++)
        mLines[i][mLinePos] = read[i] - householder + x * (i % 2 ? -0.35 : 0.35);
      mLinePos = (mLinePos + 1) % lineSize;

      double wetL = (read[0] - read[2] + read[4] - read[6]) * 0.6;
      double wetR = (read[1] - read[3] + read[5] - read[7]) * 0.6;

      // Shimmer's pitch shifter: two taps sweeping through a short delay at double speed, crossfaded.
      if (mCurMode == kShimmer)
      {
        const int shSize = (int)mShift.size();
        mShift[mShiftPos] = 0.5 * (wetL + wetR);
        const double win = 0.05 * sr;
        mShiftPhase += 1.0 / win; // the read point moves at 2x: +1 octave
        if (mShiftPhase >= 1.0)
          mShiftPhase -= 1.0;
        double y = 0.0;
        for (int t = 0; t < 2; t++)
        {
          const double ph = std::fmod(mShiftPhase + 0.5 * t, 1.0);
          double p = mShiftPos - (1.0 - ph) * win - 2.0;
          if (p < 0)
            p += shSize;
          const int i0 = (int)p;
          const double f = p - i0;
          const double v = mShift[i0] * (1.0 - f) + mShift[(i0 + 1) % shSize] * f;
          y += v * std::sin(kPedalsPi * ph) * std::sin(kPedalsPi * ph);
        }
        mShiftPos = (mShiftPos + 1) % shSize;
        mShiftLP += 0.35 * (y - mShiftLP);
        mShiftOut = mShiftLP;
      }
      else
        mShiftOut = 0.0;

      wetL = mOutHP[0].Process(wetL) * mModeGain;
      wetR = mOutHP[1].Process(wetR) * mModeGain;
      for (int c = 0; c < numChannels; c++)
      {
        const double wet = numChannels == 1 ? 0.5 * (wetL + wetR) : (c == 0 ? wetL : wetR);
        mBuf.out[c][s] = static_cast<sample>(inputs[c][s] * (1.0 - mix[s]) + wet * mix[s]);
      }
    }
    return mBuf.ptrs.data();
  }

private:
  static constexpr int kN = 8;
  struct ModeSpec
  {
    double lineMs[kN];
    double diffMs[4];
    double diffG;
    double dampScale; // Damping cutoff relative to Tone
    double modHz, modDepth; // Line-length modulation at Mod 10
  };
  static constexpr ModeSpec kModes[kNumModes] = {
    // Hall: big, smooth, gently moving.
    {{29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.3, 79.1}, {4.8, 3.6, 12.7, 9.3}, 0.7, 1.0, 0.6, 0.004},
    // Plate: dense and bright, builds instantly.
    {{17.1, 19.7, 23.3, 27.1, 29.9, 33.7, 37.1, 41.3}, {4.8, 3.6, 12.7, 9.3}, 0.78, 1.3, 0.9, 0.002},
    // Room: short lines, small space.
    {{7.1, 9.3, 11.9, 13.7, 15.1, 17.3, 19.9, 23.1}, {2.1, 1.7, 5.3, 3.9}, 0.6, 0.9, 0.5, 0.003},
    // Shimmer: the Hall, with an octave-up fed back (Mod sets how much).
    {{29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.3, 79.1}, {4.8, 3.6, 12.7, 9.3}, 0.7, 1.1, 0.5, 0.003},
    // Cloud: huge and diffuse, slow wide modulation.
    {{61.0, 73.0, 89.0, 101.0, 113.0, 127.0, 139.0, 151.0}, {9.1, 7.3, 23.1, 17.9}, 0.8, 0.8, 0.3, 0.008},
    // Bloom: the Hall, whose input swells in.
    {{29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.3, 79.1}, {4.8, 3.6, 12.7, 9.3}, 0.75, 0.9, 0.5, 0.004},
  };

  double mSampleRate = 48000.0;
  std::array<std::vector<double>, kN> mLines;
  std::vector<double> mPre, mShift;
  std::array<std::vector<double>, 4> mDiff;
  std::array<int, 4> mDiffPos{};
  std::array<double, kN> mDamp{}, mPhase{};
  int mLinePos = 0, mPrePos = 0, mShiftPos = 0;
  double mShiftPhase = 0.0, mShiftLP = 0.0, mShiftOut = 0.0;
  double mBloomSlow = 0.0, mBloomFast = 0.0;
  Biquad mOutHP[2];
  int mWantMode = kHall, mCurMode = kHall;
  double mModeGain = 1.0;
  std::array<double, kN> mLineGain{};
  double mDampA = 0.5;
  int mCoefMode = -1;
  Glide mDecay, mTone, mMod, mMix, mFreeze;
  double mPreTarget = 20.0, mPreNow = 20.0, mPreNext = 20.0, mPreFade = 0.0;
  bool mPreStarted = false;
  std::vector<double> mG[6];
  PedalBuffers mBuf;
};

// ============================================================================================
// FlangerDoubler (MXR 126-style). Flanger: a 0.3-10 ms delay swept by a triangle LFO (exponential
// in time, like a BBD); Doubler: a 15-70 ms delay that wanders (a slow sine plus smoothed noise),
// so it sounds like the part played twice. Regen feeds the delayed signal back (soft-limited).
class FlangerDoubler
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    int size = 1;
    while (size < (int)(0.09 * sr) + 8)
      size <<= 1;
    for (auto& b : mBuffer)
      b.assign(size, 0.0);
    mWritePos = 0;
    mPhase = 0.0;
    mNoise[0] = mNoise[1] = 0.0;
    mModeGain = 1.0;
    mCurDoubler = mWantDoubler;
  }

  // doubler: the mode; manual01, width01, regen (-1..1), mix01; speedHz.
  void SetParams(bool doubler, double manual01, double width01, double speedHz, double regen, double mix01)
  {
    mWantDoubler = doubler;
    mManual.Set(std::clamp(manual01, 0.0, 1.0));
    mWidth.Set(std::clamp(width01, 0.0, 1.0));
    mSpeed = std::clamp(speedHz, 0.01, 10.0);
    mRegen.Set(0.9 * std::clamp(regen, -1.0, 1.0));
    mMix.Set(std::clamp(mix01, 0.0, 1.0));
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    const double coef = GlideCoef(kKnobGlideS, sr);
    const double* manual = mManual.Fill(mG[0], numFrames, coef);
    const double* width = mWidth.Fill(mG[1], numFrames, coef);
    const double* regen = mRegen.Fill(mG[2], numFrames, coef);
    const double* mix = mMix.Fill(mG[3], numFrames, coef);
    const int size = (int)mBuffer[0].size(), mask = size - 1;
    const double modeFade = 1.0 / (0.015 * sr);
    const double noiseC = 1.0 - std::exp(-2.0 * kPedalsPi * 1.5 / sr);
    for (int s = 0; s < numFrames; s++)
    {
      if (mWantDoubler != mCurDoubler)
      {
        mModeGain -= modeFade;
        if (mModeGain <= 0.0)
        {
          mModeGain = 0.0;
          mCurDoubler = mWantDoubler;
        }
      }
      else if (mModeGain < 1.0)
        mModeGain = std::min(1.0, mModeGain + modeFade);
      mPhase += mSpeed / sr;
      if (mPhase >= 1.0)
        mPhase -= 1.0;
      for (int c = 0; c < numChannels; c++)
      {
        // The right channel's sweep runs a quarter cycle ahead (stereo movement).
        const double ph = std::fmod(mPhase + (c == 1 ? 0.25 : 0.0), 1.0);
        double delayMs;
        if (!mCurDoubler)
        {
          const double tri = ph < 0.5 ? 2.0 * ph : 2.0 - 2.0 * ph;
          const double center = 0.3 + manual[s] * 4.0;
          delayMs = std::min(10.0, center * std::pow(2.0, width[s] * 3.0 * tri));
        }
        else
        {
          // Smoothed noise (a different stream per channel) plus a slow sine: a wandering take.
          mRng = mRng * 1664525u + 1013904223u;
          const double n = ((mRng >> 8) / 16777216.0) * 2.0 - 1.0;
          mNoise[c] += noiseC * (n - mNoise[c]);
          const double wander = 0.6 * std::sin(2.0 * kPedalsPi * ph) + 3.0 * mNoise[c];
          const double center = (15.0 + manual[s] * 55.0) * (c == 1 ? 1.13 : 1.0);
          delayMs = center * (1.0 + width[s] * 0.06 * wander);
        }
        const double readPos = mWritePos - std::clamp(delayMs * 0.001 * sr, 1.0, (double)size - 4.0);
        const double fl = std::floor(readPos);
        const int i1 = (int)fl;
        const double t = readPos - fl;
        const auto& buf = mBuffer[c];
        const double y0 = buf[(i1 - 1) & mask], y1 = buf[i1 & mask], y2 = buf[(i1 + 1) & mask], y3 = buf[(i1 + 2) & mask];
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        const double delayed = ((c3 * t + c2) * t + c1) * t + y1;
        const double in = inputs[c][s];
        const double fb = mCurDoubler ? 0.5 * regen[s] : regen[s];
        mBuffer[c][mWritePos] = in + fb * std::tanh(delayed);
        const double wet = delayed * (1.0 - 0.4 * std::fabs(fb)) * mModeGain;
        mBuf.out[c][s] = static_cast<sample>(in * (1.0 - 0.5 * mix[s]) + wet * mix[s]);
      }
      mWritePos = (mWritePos + 1) & mask;
    }
    return mBuf.ptrs.data();
  }

private:
  double mSampleRate = 48000.0;
  std::array<std::vector<double>, 2> mBuffer;
  int mWritePos = 0;
  double mPhase = 0.0, mSpeed = 0.5;
  double mNoise[2] = {0.0, 0.0};
  uint32_t mRng = 22222u;
  bool mWantDoubler = false, mCurDoubler = false;
  double mModeGain = 1.0;
  Glide mManual, mWidth, mRegen, mMix;
  std::vector<double> mG[4];
  PedalBuffers mBuf;
};

// ============================================================================================
// Wah: a swept resonant filter (a state-variable filter: band-pass with some low-pass, like the
// wah's inductor circuit). Cry Baby: 350 Hz - 2.2 kHz, sharp peak. V847: 450 Hz - 1.6 kHz, rounder.
// Auto: the position follows your picking (an envelope against your recent loudest).
class Wah
{
public:
  enum Model
  {
    kCryBaby = 0,
    kV847,
    kAuto,
    kNumModels
  };

  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    for (auto& st : mState)
      st = {};
    mEnv = 0.0;
    mRefDB = -50.0;
    mAutoPos = 0.0;
    for (auto& h : mHP)
    {
      h.HighPass(sr, 80.0);
      h.Reset();
    }
  }

  // model: Model; position01 (heel 0 .. toe 1); q01: resonance (0.5 = the pedal's own); sens01:
  // auto-wah sensitivity; levelDB: output.
  void SetParams(int model, double position01, double q01, double sens01, double levelDB)
  {
    mModel = std::clamp(model, 0, kNumModels - 1);
    mPosition.Set(std::clamp(position01, 0.0, 1.0));
    mQ.Set(std::clamp(q01, 0.0, 1.0));
    mSens = std::clamp(sens01, 0.0, 1.0);
    mLevel.Set(std::pow(10.0, std::clamp(levelDB, -24.0, 24.0) / 20.0));
  }

  // The peak frequency at a position, for each model (Auto sweeps the Cry Baby's range).
  static double PeakHz(int model, double pos)
  {
    const double lo = model == kV847 ? 450.0 : 350.0, hi = model == kV847 ? 1600.0 : 2200.0;
    return lo * std::pow(hi / lo, std::clamp(pos, 0.0, 1.0));
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    const double* position = mPosition.Fill(mG[0], numFrames, GlideCoef(0.012, sr));
    const double* q = mQ.Fill(mG[1], numFrames, GlideCoef(kKnobGlideS, sr));
    const double* level = mLevel.Fill(mG[2], numFrames, GlideCoef(kKnobGlideS, sr));
    const double baseQ = mModel == kV847 ? 3.5 : 5.5;
    const double lpMix = mModel == kV847 ? 0.4 : 0.25;
    const double attack = 1.0 - std::exp(-1.0 / (0.004 * sr)), release = 1.0 - std::exp(-1.0 / (0.12 * sr));
    const double posUp = 1.0 - std::exp(-1.0 / (0.015 * sr)), posDown = 1.0 - std::exp(-1.0 / (0.15 * sr));
    const double refFall = 1.0 / sr;
    for (int s = 0; s < numFrames; s++)
    {
      double pos = position[s];
      if (mModel == kAuto)
      {
        // Envelope against your recent loudest (up at once, down 1 dB/s): the same at any level.
        double a = 0.0;
        for (int c = 0; c < numChannels; c++)
          a = std::max(a, std::fabs((double)inputs[c][s]));
        mEnv += (a > mEnv ? attack : release) * (a - mEnv);
        const double envDB = 20.0 * std::log10(std::max(mEnv, 1e-9));
        mRefDB = std::max(envDB, mRefDB - refFall);
        // Sensitivity: how far under your loudest the sweep starts (6 .. 36 dB).
        const double span = 6.0 + 30.0 * mSens;
        const double target = std::clamp((envDB - (std::max(mRefDB, -50.0) - span)) / span, 0.0, 1.0);
        mAutoPos += (target > mAutoPos ? posUp : posDown) * (target - mAutoPos);
        // Position sets where the sweep starts (heel) in Auto.
        pos = position[s] * 0.5 + (1.0 - position[s] * 0.5) * mAutoPos;
      }
      const double fc = PeakHz(mModel == kAuto ? kCryBaby : mModel, pos);
      const double Q = baseQ * std::pow(2.0, (q[s] - 0.5) * 2.0);
      // TPT state-variable filter (Zavalishin).
      const double g = std::tan(kPedalsPi * std::min(fc, 0.45 * sr) / sr), k = 1.0 / Q;
      const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
      for (int c = 0; c < numChannels; c++)
      {
        State& st = mState[c];
        const double x = mHP[c].Process(inputs[c][s]);
        const double v3 = x - st.ic2;
        const double v1 = a1 * st.ic1 + a2 * v3;
        const double v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
        st.ic1 = 2.0 * v1 - st.ic1;
        st.ic2 = 2.0 * v2 - st.ic2;
        // Band-pass (peak gain Q at fc) with some low-pass under it; scaled so the sweep's peak sits
        // around +10 dB over the dry signal, like the real pedals.
        const double y = (v1 + lpMix * v2) * (1.8 / std::sqrt(Q));
        mBuf.out[c][s] = static_cast<sample>(y * level[s]);
      }
    }
    return mBuf.ptrs.data();
  }

private:
  struct State
  {
    double ic1 = 0.0, ic2 = 0.0;
  };
  double mSampleRate = 48000.0;
  std::array<State, 2> mState{};
  Biquad mHP[2];
  int mModel = kCryBaby;
  double mSens = 0.5;
  double mEnv = 0.0, mRefDB = -50.0, mAutoPos = 0.0;
  Glide mPosition, mQ, mLevel;
  std::vector<double> mG[3];
  PedalBuffers mBuf;
};

// ============================================================================================
// PrecisionDrive: Attack sets a high-pass into the clipping (60 Hz .. 500 Hz: how much low end gets
// clipped -- tight for metal) with a little mid push; Drive the gain into an asymmetric soft clip;
// a low-pass after (Bright opens it); Volume; and a noise gate keyed from the pedal's input.
class PrecisionDrive
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    for (int c = 0; c < 2; c++)
    {
      mPreHP[c].Reset();
      mMid[c].Reset();
      mPostLP[c].Reset();
      mPostLP2[c].Reset();
      mDcHP[c].HighPass(sr, 20.0);
      mDcHP[c].Reset();
      mClip[c].Reset(kBias);
    }
    mGateEnv = 0.0;
    mGateGain = 1.0;
    mGateHold = 0;
    mLastAttack = mLastBright = -1.0;
  }

  // volumeDB; drive01, attack01; gate01 (0 = off, then a threshold -90 .. -30 dBFS); bright.
  void SetParams(double volumeDB, double drive01, double attack01, double gate01, bool bright)
  {
    mVolume.Set(std::pow(10.0, std::clamp(volumeDB, -40.0, 20.0) / 20.0));
    mDrive.Set(std::clamp(drive01, 0.0, 1.0));
    mAttack = std::clamp(attack01, 0.0, 1.0);
    mGate = std::clamp(gate01, 0.0, 1.0);
    mBright = bright ? 1.0 : 0.0;
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    if (mAttack != mLastAttack || mBright != mLastBright)
    {
      for (int c = 0; c < 2; c++)
      {
        mPreHP[c].HighPass(sr, 60.0 * std::pow(500.0 / 60.0, mAttack));
        mMid[c].Peak(sr, 750.0, 0.8, 3.0 + 3.0 * mAttack);
        mPostLP[c].LowPass(sr, mBright > 0.5 ? 8000.0 : 4800.0);
        mPostLP2[c].LowPass(sr, mBright > 0.5 ? 11000.0 : 6500.0);
      }
      mLastAttack = mAttack;
      mLastBright = mBright;
    }
    const double* drive = mDrive.Fill(mG[0], numFrames, GlideCoef(kKnobGlideS, sr));
    const double* volume = mVolume.Fill(mG[1], numFrames, GlideCoef(kKnobGlideS, sr));
    const double thrDB = -90.0 + 60.0 * mGate;
    const double envA = 1.0 - std::exp(-1.0 / (0.001 * sr)), envR = 1.0 - std::exp(-1.0 / (0.05 * sr));
    const double open = 1.0 - std::exp(-1.0 / (0.001 * sr)), close = 1.0 - std::exp(-1.0 / (0.06 * sr));
    const int holdLen = (int)(0.03 * sr);
    for (int s = 0; s < numFrames; s++)
    {
      // Gate, keyed from the input (before the gain).
      double a = 0.0;
      for (int c = 0; c < numChannels; c++)
        a = std::max(a, std::fabs((double)inputs[c][s]));
      mGateEnv += (a > mGateEnv ? envA : envR) * (a - mGateEnv);
      double gateTarget = 1.0;
      if (mGate > 0.0)
      {
        const double envDB = 20.0 * std::log10(std::max(mGateEnv, 1e-9));
        if (envDB > thrDB)
          mGateHold = holdLen;
        else if (mGateHold > 0)
          mGateHold--;
        gateTarget = (envDB > thrDB - 6.0 && mGateHold > 0) ? 1.0 : 0.0;
      }
      mGateGain += (gateTarget > mGateGain ? open : close) * (gateTarget - mGateGain);
      const double gain = std::pow(10.0, (6.0 + 36.0 * drive[s]) / 20.0);
      for (int c = 0; c < numChannels; c++)
      {
        double x = mMid[c].Process(mPreHP[c].Process(inputs[c][s]));
        // Asymmetric soft clip: biased tanh (the bias's DC is removed after).
        x = mClip[c].Process(x * gain + kBias) - std::tanh(kBias);
        x = mDcHP[c].Process(x);
        x = mPostLP2[c].Process(mPostLP[c].Process(x));
        mBuf.out[c][s] = static_cast<sample>(x * 0.5 * volume[s] * mGateGain);
      }
    }
    return mBuf.ptrs.data();
  }

private:
  static constexpr double kBias = 0.15;
  double mSampleRate = 48000.0;
  Biquad mPreHP[2], mMid[2], mPostLP[2], mPostLP2[2], mDcHP[2];
  AdaaTanh mClip[2];
  double mAttack = 0.5, mGate = 0.0, mBright = 0.0, mLastAttack = -1.0, mLastBright = -1.0;
  double mGateEnv = 0.0, mGateGain = 1.0;
  int mGateHold = 0;
  Glide mDrive, mVolume;
  std::vector<double> mG[2];
  PedalBuffers mBuf;
};

// ============================================================================================
// ApexPreamp: a tightening high-pass, three cascaded gain stages (biased soft clips with filtering
// between them, like a modern high-gain preamp), then Bass / Mid / Treble, Presence and Volume.
// Bright adds top end before the gain.
class ApexPreamp
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    for (int c = 0; c < 2; c++)
    {
      mInHP[c].HighPass(sr, 90.0);
      mS1HP[c].HighPass(sr, 160.0);
      mS1LP[c].LowPass(sr, 9000.0);
      mS2HP[c].HighPass(sr, 90.0);
      mS2LP[c].LowPass(sr, 6500.0);
      mOutLP[c].LowPass(sr, 9500.0);
      mDc[c].HighPass(sr, 20.0);
      for (auto* b : {&mInHP[c], &mS1HP[c], &mS1LP[c], &mS2HP[c], &mS2LP[c], &mOutLP[c], &mDc[c], &mBrightF[c],
                      &mBass[c], &mMidF[c], &mTreble[c], &mPresence[c]})
        b->Reset();
      // Each stage at rest at its own bias (see Process).
      mClip[c][0].Reset(0.2);
      mClip[c][1].Reset(-0.15);
      mClip[c][2].Reset(0.1);
    }
    mLast = {-1, -1, -1, -1, -1};
  }

  // gain01, bass01, mid01, treble01, presence01; volumeDB; bright.
  void SetParams(double gain01, double bass01, double mid01, double treble01, double presence01, double volumeDB,
                 bool bright)
  {
    mGain.Set(std::clamp(gain01, 0.0, 1.0));
    mEq = {std::clamp(bass01, 0.0, 1.0), std::clamp(mid01, 0.0, 1.0), std::clamp(treble01, 0.0, 1.0),
           std::clamp(presence01, 0.0, 1.0), bright ? 1.0 : 0.0};
    mVolume.Set(std::pow(10.0, std::clamp(volumeDB, -40.0, 20.0) / 20.0));
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    mBuf.Ensure(numChannels, numFrames);
    const double sr = mSampleRate;
    if (mEq != mLast)
    {
      // (EQ knobs re-set the filters per block; their changes are small steps, like turning a pot.)
      for (int c = 0; c < 2; c++)
      {
        mBrightF[c].Shelf(sr, 1800.0, mEq[4] * 6.0, true);
        mBass[c].Shelf(sr, 120.0, -12.0 + 24.0 * mEq[0], false);
        mMidF[c].Peak(sr, 700.0, 0.8, -12.0 + 18.0 * mEq[1]);
        mTreble[c].Shelf(sr, 2800.0, -12.0 + 24.0 * mEq[2], true);
        mPresence[c].Shelf(sr, 5000.0, 9.0 * mEq[3], true);
      }
      mLast = mEq;
    }
    const double* gain = mGain.Fill(mG[0], numFrames, GlideCoef(kKnobGlideS, sr));
    const double* volume = mVolume.Fill(mG[1], numFrames, GlideCoef(kKnobGlideS, sr));
    for (int s = 0; s < numFrames; s++)
    {
      const double g1 = std::pow(10.0, (8.0 + 22.0 * gain[s]) / 20.0);
      const double g2 = std::pow(10.0, (6.0 + 20.0 * gain[s]) / 20.0);
      for (int c = 0; c < numChannels; c++)
      {
        double x = mBrightF[c].Process(mInHP[c].Process(inputs[c][s]));
        x = mClip[c][0].Process(x * g1 + 0.2) - std::tanh(0.2);
        x = mS1LP[c].Process(mS1HP[c].Process(x));
        x = mClip[c][1].Process(x * g2 - 0.15) + std::tanh(0.15);
        x = mS2LP[c].Process(mS2HP[c].Process(x));
        x = mClip[c][2].Process(x * 3.0 + 0.1) - std::tanh(0.1);
        x = mDc[c].Process(x);
        x = mPresence[c].Process(mTreble[c].Process(mMidF[c].Process(mBass[c].Process(x))));
        x = mOutLP[c].Process(x);
        mBuf.out[c][s] = static_cast<sample>(x * 0.35 * volume[s]);
      }
    }
    return mBuf.ptrs.data();
  }

private:
  double mSampleRate = 48000.0;
  Biquad mInHP[2], mS1HP[2], mS1LP[2], mS2HP[2], mS2LP[2], mOutLP[2], mDc[2], mBrightF[2], mBass[2], mMidF[2],
    mTreble[2], mPresence[2];
  AdaaTanh mClip[2][3];
  std::array<double, 5> mEq{0.5, 0.5, 0.5, 0.0, 0.0}, mLast{-1, -1, -1, -1, -1};
  Glide mGain, mVolume;
  std::vector<double> mG[2];
  PedalBuffers mBuf;
};

} // namespace pedals
