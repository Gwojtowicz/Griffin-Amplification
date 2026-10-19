#pragma once

// Simple, self-contained stompbox-style DSP modules for the pedalboard feature.
// Mono in, mono out (matches the plugin's internal mono signal path), matching the
// "sample** in, sample** out, numFrames" call convention used elsewhere in ProcessBlock.

#include <cmath>
#include <vector>
#include <algorithm>

namespace pedals
{

using sample = iplug::sample;
constexpr double kPedalsPi = 3.14159265358979323846;

// 1.42.0: a knob's value as the DSP uses it, gliding to where the knob is set instead of jumping
// there (a jump in a gain, a mix or a delay time clicked). The first value it's given is taken at
// once, and once it arrives it's exactly the knob's value.
constexpr double kKnobGlideS = 0.02;
inline double GlideCoef(double seconds, double sampleRate) { return 1.0 - std::exp(-1.0 / (seconds * sampleRate)); }
struct Glide
{
  double target = 0.0, value = 0.0;
  bool started = false;
  void Set(double t)
  {
    target = t;
    if (!started)
    {
      value = t;
      started = true;
    }
  }
  // The next n values, into buf.
  const double* Fill(std::vector<double>& buf, int n, double coef)
  {
    if ((int)buf.size() < n)
      buf.resize(n);
    for (int s = 0; s < n; s++)
    {
      if (value != target)
      {
        value += coef * (target - value);
        if (std::fabs(target - value) <= 1e-9 * std::max(1.0, std::fabs(target)))
          value = target;
      }
      buf[s] = value;
    }
    return buf.data();
  }
};

// A simple envelope-follower + gain-reduction compressor ("Sustain" = amount of compression,
// "Level" = output makeup gain in dB).
//
// 1.42.0: Sustain works the same however hard you play, and doesn't turn the pedal down. The
// threshold used to be fixed (-6 to -40 dBFS), with no make-up: at a typical guitar level Sustain 5
// did nothing, and higher settings only made the pedal quieter (12 dB at Sustain 10 for picks peaking
// at -20 dBFS). Now the threshold sits Sustain's depth (6 to 40 dB) under your recent loudest picks
// (a peak follower: up at once, down 1 dB/s, floored at -50 dBFS), and the make-up gives that much
// back, so your loudest picks come out where they went in and everything under them -- the note
// ringing out -- comes up: about +5 dB of sustain at Sustain 5-10, peaks within ~3 dB. The attack is
// 0.3 ms (it was 3 ms, which let picks spike 15-20 dB over with the make-up on).
class Compressor
{
public:
  void SetSampleRate(double sr) { mSampleRate = sr; }

  void SetParams(double sustain01, double levelDB, double mix01 = 1.0)
  {
    // Higher sustain -> a lower threshold (more of the signal gets compressed) and a higher ratio.
    const double depth = 6.0 + sustain01 * 34.0; // 6 dB (light) .. 40 dB (heavy) under your loudest
    const double ratio = 2.0 + sustain01 * 8.0; // 2:1 up to 10:1
    mDepth.Set(depth);
    mSlope.Set(1.0 - 1.0 / ratio);
    mLevelDB.Set(levelDB);
    mMix.Set(mix01);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    const double attackCoeff = std::exp(-1.0 / (kAttackS * mSampleRate));
    const double releaseCoeff = std::exp(-1.0 / (0.15 * mSampleRate)); // ~150ms release
    const double refFall = kRefFallDBPerS / mSampleRate;
    const double coef = GlideCoef(kKnobGlideS, mSampleRate);
    const double* depth = mDepth.Fill(mGlideBuf[0], numFrames, coef);
    const double* slope = mSlope.Fill(mGlideBuf[1], numFrames, coef);
    const double* levelDB = mLevelDB.Fill(mGlideBuf[2], numFrames, coef);
    const double* mix = mMix.Fill(mGlideBuf[3], numFrames, coef);
    // Stereo-linked (1.36.1): one detector on the louder channel, one gain for both. It used to run
    // each channel's whole block through the same envelope in turn, so in stereo the envelope (and
    // the gain) jumped between the channels' levels at every block boundary -- a zipper at the
    // buffer rate with panned sources -- and the image wobbled. Mono is unchanged.
    for (int s = 0; s < numFrames; s++)
    {
      double rectified = 0.0;
      for (int c = 0; c < numChannels; c++)
        rectified = std::max(rectified, std::abs(static_cast<double>(inputs[c][s])));
      mEnvelope = rectified > mEnvelope ? attackCoeff * mEnvelope + (1.0 - attackCoeff) * rectified
                                        : releaseCoeff * mEnvelope + (1.0 - releaseCoeff) * rectified;
      const double envDB = 20.0 * std::log10(std::max(mEnvelope, 1e-9));
      mRefDB = std::max(envDB, mRefDB - refFall);
      const double threshold = std::max(mRefDB, kRefFloorDB) - depth[s];
      double gainReductionDB = 0.0;
      if (envDB > threshold)
        gainReductionDB = (threshold - envDB) * slope[s];
      // Make-up: the reduction at the reference, given back (see the top of the class).
      const double gain = std::pow(10.0, (gainReductionDB + depth[s] * slope[s] + levelDB[s]) / 20.0);
      for (int c = 0; c < numChannels; c++)
      {
        const double in = inputs[c][s];
        const double wet = in * gain;
        mOutputs[c][s] = static_cast<sample>(in * (1.0 - mix[s]) + wet * mix[s]);
      }
    }
    return _GetPointers();
  }

private:
  void _EnsureSize(int numChannels, int numFrames)
  {
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  static constexpr double kAttackS = 0.0003;
  static constexpr double kRefFallDBPerS = 1.0;
  static constexpr double kRefFloorDB = -50.0;
  double mSampleRate = 48000.0;
  Glide mDepth, mSlope, mLevelDB, mMix;
  std::vector<double> mGlideBuf[4];
  double mEnvelope = 0.0;
  double mRefDB = kRefFloorDB; // Your recent loudest (envelope, dBFS)
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

// Pre-gain + soft (tanh) clipping + a simple tone tilt + output level.
class Overdrive
{
public:
  void SetSampleRate(double sr) { mSampleRate = sr; }

  void SetParams(double drive01, double tone01, double levelDB, double mix01 = 1.0)
  {
    mDriveGain.Set(1.0 + drive01 * 39.0); // 1x - 40x
    mTone.Set(tone01); // 0 = dark, 1 = bright
    mLevelGain.Set(std::pow(10.0, levelDB / 20.0));
    mMix.Set(mix01);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    if ((int)mLpState.size() != numChannels)
      mLpState.assign(numChannels, 0.0);
    // The knobs glide (1.42.0): their values for each sample of this block.
    const double coef = GlideCoef(kKnobGlideS, mSampleRate);
    const double* drive = mDriveGain.Fill(mGlideBuf[0], numFrames, coef);
    const double* tone = mTone.Fill(mGlideBuf[1], numFrames, coef);
    const double* level = mLevelGain.Fill(mGlideBuf[2], numFrames, coef);
    const double* mix = mMix.Fill(mGlideBuf[3], numFrames, coef);
    if ((int)mAlphaBuf.size() < numFrames)
      mAlphaBuf.resize(numFrames);
    for (int s = 0; s < numFrames; s++)
    {
      // One-pole lowpass for the "dark" component of the tone control.
      if (s == 0 || tone[s] != tone[s - 1])
      {
        const double toneFreq = 800.0 + tone[s] * 4200.0; // 800Hz (dark) .. 5kHz (bright)
        mAlphaBuf[s] = std::exp(-2.0 * kPedalsPi * toneFreq / mSampleRate);
      }
      else
        mAlphaBuf[s] = mAlphaBuf[s - 1];
    }

    for (int c = 0; c < numChannels; c++)
    {
      for (int s = 0; s < numFrames; s++)
      {
        const double in = inputs[c][s];
        const double lpAlpha = mAlphaBuf[s];
        const double driven = std::tanh(in * drive[s]) / std::tanh(drive[s] > 1.0 ? drive[s] : 1.0);
        mLpState[c] = lpAlpha * mLpState[c] + (1.0 - lpAlpha) * driven;
        // Blend between the filtered (dark) and raw driven (bright) signal per the Tone knob.
        const double toned = mLpState[c] * (1.0 - tone[s]) + driven * tone[s];
        const double wet = toned * level[s];
        mOutputs[c][s] = static_cast<sample>(in * (1.0 - mix[s]) + wet * mix[s]);
      }
    }
    return _GetPointers();
  }

private:
  void _EnsureSize(int numChannels, int numFrames)
  {
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  double mSampleRate = 48000.0;
  Glide mDriveGain, mTone, mLevelGain, mMix;
  std::vector<double> mGlideBuf[4], mAlphaBuf;
  std::vector<double> mLpState;
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

// Classic LFO-modulated short delay line, blended with the dry signal per the Mix knob.
class Chorus
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    const int maxDelaySamples = (int)(0.035 * sr) + 4; // up to 35ms
    mBufferSize = maxDelaySamples;
  }

  void SetParams(double rateHz, double depth01, double mix01 = 0.5)
  {
    mRateHz = rateHz;
    mDepth.Set(depth01);
    mMix.Set(mix01);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    const double baseDelayMs = 12.0;
    // Depth and Mix glide (1.42.0).
    const double coef = GlideCoef(kKnobGlideS, mSampleRate);
    const double* depth = mDepth.Fill(mGlideBuf[0], numFrames, coef);
    const double* mix = mMix.Fill(mGlideBuf[1], numFrames, coef);
    for (int c = 0; c < numChannels; c++)
    {
      auto& buf = mBuffer[c];
      int& writePos = mWritePos[c];
      double& phase = mPhase[c];
      for (int s = 0; s < numFrames; s++)
      {
        buf[writePos] = inputs[c][s];

        const double depthMs = depth[s] * 8.0;
        const double delayMs = baseDelayMs + depthMs * std::sin(phase);
        const double delaySamples = delayMs * 0.001 * mSampleRate;
        double readPos = (double)writePos - delaySamples;
        while (readPos < 0)
          readPos += mBufferSize;
        const int readIdx0 = (int)readPos % mBufferSize;
        const int readIdx1 = (readIdx0 + 1) % mBufferSize;
        const double frac = readPos - std::floor(readPos);
        const double delayed = buf[readIdx0] * (1.0 - frac) + buf[readIdx1] * frac;

        mOutputs[c][s] = static_cast<sample>(inputs[c][s] * (1.0 - mix[s]) + delayed * mix[s]);

        writePos = (writePos + 1) % mBufferSize;
        phase += 2.0 * kPedalsPi * mRateHz / mSampleRate;
        if (phase > 2.0 * kPedalsPi)
          phase -= 2.0 * kPedalsPi;
      }
    }
    return _GetPointers();
  }

private:
  void _EnsureSize(int numChannels, int numFrames)
  {
    // Reallocate if the channel count changed, OR if SetSampleRate changed mBufferSize since the
    // last allocation (e.g. the audio engine reset with a different rate) -- otherwise the delay
    // line's read/write indices (computed mod the NEW mBufferSize) could run past the end of the
    // OLD (still-allocated-at-the-old-size) buffer.
    const bool wrongBufferSize = !mBuffer.empty() && (int)mBuffer[0].size() != mBufferSize;
    if ((int)mBuffer.size() != numChannels || wrongBufferSize)
    {
      mBuffer.assign(numChannels, std::vector<sample>(mBufferSize, 0.0));
      mWritePos.assign(numChannels, 0);
      // Offset the right channel's LFO phase by 90 degrees so a stereo call actually produces two
      // different signals (real width), not two identical (dual-mono) ones.
      mPhase.assign(numChannels, 0.0);
      if (numChannels > 1)
        mPhase[1] = kPedalsPi * 0.5;
    }
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  double mSampleRate = 48000.0;
  int mBufferSize = 1024;
  double mRateHz = 0.8;
  Glide mDepth, mMix;
  std::vector<double> mGlideBuf[2];
  std::vector<std::vector<sample>> mBuffer;
  std::vector<int> mWritePos;
  std::vector<double> mPhase;
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

// Standard feedback delay line with a wet/dry mix.
class Delay
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    mBufferSize = (int)(2.5 * sr) + 4; // up to 2.5s
  }

  void SetParams(double timeMs, double feedback01, double mix01)
  {
    mTimeTarget = timeMs;
    if (!mTimeStarted)
    {
      mTimeNow = mTimeNext = timeMs; // The first Time is taken at once
      mTimeStarted = true;
    }
    mFeedback.Set(feedback01 * 0.92); // cap feedback short of runaway self-oscillation
    mMix.Set(mix01);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    // Feedback and Mix glide (1.42.0). A new Time crossfades (kTimeFadeS) from the repeats at the old
    // time to the repeats at the new one, like a digital delay: jumping the read point clicked, and
    // gliding it swept the repeats' pitch (a zip on a big change).
    const double coef = GlideCoef(kKnobGlideS, mSampleRate);
    const double* feedback = mFeedback.Fill(mGlideBuf[1], numFrames, coef);
    const double* mix = mMix.Fill(mGlideBuf[2], numFrames, coef);
    for (auto* b : {&mTimeA, &mTimeB, &mTimeFade})
      if ((int)b->size() < numFrames)
        b->resize(numFrames);
    const double fadeStep = 1.0 / (kTimeFadeS * mSampleRate);
    for (int s = 0; s < numFrames; s++)
    {
      if (mFadePos <= 0.0 && mTimeNow != mTimeTarget)
      {
        mTimeNext = mTimeTarget; // A crossfade to wherever Time is now (a later turn queues behind it)
        mFadePos = fadeStep;
      }
      else if (mFadePos > 0.0)
        mFadePos += fadeStep;
      if (mFadePos >= 1.0)
      {
        mTimeNow = mTimeNext;
        mFadePos = 0.0;
      }
      mTimeA[s] = mTimeNow;
      mTimeB[s] = mTimeNext;
      mTimeFade[s] = mFadePos * mFadePos * (3.0 - 2.0 * mFadePos);
    }
    auto read = [this](const std::vector<sample>& buf, int writePos, double timeMs, int c) {
      // Right channel gets a slightly longer delay (a classic stereo-spread trick) so a stereo
      // call produces real width instead of two identical (dual-mono) taps.
      const double delaySamples = (c == 1)
                                    ? std::clamp(timeMs * 1.08 * 0.001 * mSampleRate, 1.0, (double)(mBufferSize - 2))
                                    : std::clamp(timeMs * 0.001 * mSampleRate, 1.0, (double)(mBufferSize - 2));
      double readPos = (double)writePos - delaySamples;
      while (readPos < 0)
        readPos += mBufferSize;
      const int readIdx0 = (int)readPos % mBufferSize;
      const int readIdx1 = (readIdx0 + 1) % mBufferSize;
      const double frac = readPos - std::floor(readPos);
      return buf[readIdx0] * (1.0 - frac) + buf[readIdx1] * frac;
    };
    for (int c = 0; c < numChannels; c++)
    {
      auto& buf = mBuffer[c];
      int& writePos = mWritePos[c];
      for (int s = 0; s < numFrames; s++)
      {
        double delayed = read(buf, writePos, mTimeA[s], c);
        if (mTimeFade[s] > 0.0)
          delayed += mTimeFade[s] * (read(buf, writePos, mTimeB[s], c) - delayed);

        buf[writePos] = inputs[c][s] + delayed * feedback[s];
        mOutputs[c][s] = static_cast<sample>(inputs[c][s] * (1.0 - mix[s]) + delayed * mix[s]);

        writePos = (writePos + 1) % mBufferSize;
      }
    }
    return _GetPointers();
  }

private:
  void _EnsureSize(int numChannels, int numFrames)
  {
    // See the matching comment in Chorus::_EnsureSize -- must also catch mBufferSize changing
    // (e.g. a sample-rate change) even when the channel count didn't.
    const bool wrongBufferSize = !mBuffer.empty() && (int)mBuffer[0].size() != mBufferSize;
    if ((int)mBuffer.size() != numChannels || wrongBufferSize)
    {
      mBuffer.assign(numChannels, std::vector<sample>(mBufferSize, 0.0));
      mWritePos.assign(numChannels, 0);
    }
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  double mSampleRate = 48000.0;
  int mBufferSize = 96000;
  static constexpr double kTimeFadeS = 0.03;
  double mTimeTarget = 350.0, mTimeNow = 350.0, mTimeNext = 350.0, mFadePos = 0.0;
  bool mTimeStarted = false;
  std::vector<double> mTimeA, mTimeB, mTimeFade;
  Glide mFeedback, mMix;
  std::vector<double> mGlideBuf[3];
  std::vector<std::vector<sample>> mBuffer;
  std::vector<int> mWritePos;
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

// Simple Schroeder/Freeverb-style reverb: 4 parallel comb filters (each with a damping lowpass in
// its feedback path) summed and fed through 2 series allpass filters for diffusion. "Size"
// controls decay length (comb feedback amount); "Mix" blends dry/wet. Comb tunings are offset
// slightly between channels (classic Freeverb trick) for stereo width.
class Reverb
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    static const double kCombMsL[kNumCombs] = {29.7, 37.1, 41.1, 43.7};
    static const double kCombMsR[kNumCombs] = {30.3, 37.8, 41.9, 44.5};
    static const double kAllpassMs[kNumAllpass] = {5.0, 1.7};
    for (int i = 0; i < kNumCombs; i++)
    {
      mCombSizeL[i] = (int)(kCombMsL[i] * 0.001 * sr) + 1;
      mCombSizeR[i] = (int)(kCombMsR[i] * 0.001 * sr) + 1;
    }
    for (int i = 0; i < kNumAllpass; i++)
      mAllpassSize[i] = (int)(kAllpassMs[i] * 0.001 * sr) + 1;
    mNeedsReinit = true;
  }

  void SetParams(double size01, double mix01)
  {
    mFeedback.Set(0.7 + size01 * 0.28); // 0.70 (small/short) .. 0.98 (large/long decay)
    mMix.Set(mix01);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    // Size and Mix glide (1.42.0).
    const double coef = GlideCoef(kKnobGlideS, mSampleRate);
    const double* feedback = mFeedback.Fill(mGlideBuf[0], numFrames, coef);
    const double* mix = mMix.Fill(mGlideBuf[1], numFrames, coef);
    for (int c = 0; c < numChannels; c++)
    {
      const int* combSizes = (c == 1) ? mCombSizeR : mCombSizeL;
      for (int s = 0; s < numFrames; s++)
      {
        const double in = inputs[c][s];
        double combSum = 0.0;
        for (int i = 0; i < kNumCombs; i++)
        {
          auto& buf = mCombBuffer[c][i];
          int& pos = mCombPos[c][i];
          double& damp = mCombDamp[c][i];
          const double delayed = buf[pos];
          damp = delayed * (1.0 - mDamping) + damp * mDamping; // one-pole lowpass in the feedback path
          buf[pos] = in + damp * feedback[s];
          pos = (pos + 1) % combSizes[i];
          combSum += delayed;
        }
        double allpassSignal = combSum * 0.25; // average the 4 combs
        for (int i = 0; i < kNumAllpass; i++)
        {
          auto& buf = mAllpassBuffer[c][i];
          int& pos = mAllpassPos[c][i];
          const double bufOut = buf[pos];
          const double apOut = -allpassSignal * 0.5 + bufOut;
          buf[pos] = allpassSignal + bufOut * 0.5;
          pos = (pos + 1) % mAllpassSize[i];
          allpassSignal = apOut;
        }
        mOutputs[c][s] = static_cast<sample>(in * (1.0 - mix[s]) + allpassSignal * mix[s]);
      }
    }
    return _GetPointers();
  }

private:
  static constexpr int kNumCombs = 4;
  static constexpr int kNumAllpass = 2;

  void _EnsureSize(int numChannels, int numFrames)
  {
    if ((int)mCombBuffer.size() != numChannels || mNeedsReinit)
    {
      mCombBuffer.assign(numChannels, std::vector<std::vector<sample>>(kNumCombs));
      mCombPos.assign(numChannels, std::vector<int>(kNumCombs, 0));
      mCombDamp.assign(numChannels, std::vector<double>(kNumCombs, 0.0));
      mAllpassBuffer.assign(numChannels, std::vector<std::vector<sample>>(kNumAllpass));
      mAllpassPos.assign(numChannels, std::vector<int>(kNumAllpass, 0));
      for (int c = 0; c < numChannels; c++)
      {
        const int* combSizes = (c == 1) ? mCombSizeR : mCombSizeL;
        for (int i = 0; i < kNumCombs; i++)
          mCombBuffer[c][i].assign(combSizes[i], 0.0);
        for (int i = 0; i < kNumAllpass; i++)
          mAllpassBuffer[c][i].assign(mAllpassSize[i], 0.0);
      }
      mNeedsReinit = false;
    }
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  double mSampleRate = 48000.0;
  Glide mFeedback, mMix;
  std::vector<double> mGlideBuf[2];
  double mDamping = 0.2;
  bool mNeedsReinit = true;
  int mCombSizeL[kNumCombs] = {0, 0, 0, 0};
  int mCombSizeR[kNumCombs] = {0, 0, 0, 0};
  int mAllpassSize[kNumAllpass] = {0, 0};
  std::vector<std::vector<std::vector<sample>>> mCombBuffer; // [channel][comb][sample]
  std::vector<std::vector<int>> mCombPos; // [channel][comb]
  std::vector<std::vector<double>> mCombDamp; // [channel][comb]
  std::vector<std::vector<std::vector<sample>>> mAllpassBuffer; // [channel][allpass][sample]
  std::vector<std::vector<int>> mAllpassPos; // [channel][allpass]
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

// Simple 3-band EQ built from cascaded one-pole filters (no biquads, matching the one-pole tone
// filter already used in Overdrive): a one-pole lowpass carves off the Low band, a second one-pole
// lowpass applied to what's left of that carves the Mid band out of it, and whatever remains is the
// High band. Since low + mid + high reconstructs the original signal, each band's gain is applied
// as a simple multiplier on that band before summing -- unity on all three is a transparent bypass.
class EQ
{
public:
  void SetSampleRate(double sr) { mSampleRate = sr; }

  void SetParams(double lowGainDB, double midGainDB, double highGainDB, double levelDB)
  {
    mLowGain.Set(std::pow(10.0, lowGainDB / 20.0));
    mMidGain.Set(std::pow(10.0, midGainDB / 20.0));
    mHighGain.Set(std::pow(10.0, highGainDB / 20.0));
    mLevelGain.Set(std::pow(10.0, levelDB / 20.0));
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    // The gains glide (1.42.0).
    const double coef = GlideCoef(kKnobGlideS, mSampleRate);
    const double* lowGain = mLowGain.Fill(mGlideBuf[0], numFrames, coef);
    const double* midGain = mMidGain.Fill(mGlideBuf[1], numFrames, coef);
    const double* highGain = mHighGain.Fill(mGlideBuf[2], numFrames, coef);
    const double* levelGain = mLevelGain.Fill(mGlideBuf[3], numFrames, coef);
    constexpr double kLowHz = 400.0;
    constexpr double kMidHz = 2500.0;
    const double lowAlpha = std::exp(-2.0 * kPedalsPi * kLowHz / mSampleRate);
    const double midAlpha = std::exp(-2.0 * kPedalsPi * kMidHz / mSampleRate);

    for (int c = 0; c < numChannels; c++)
    {
      double& lpLowState = mLpLowState[c];
      double& lpMidState = mLpMidState[c];
      for (int s = 0; s < numFrames; s++)
      {
        const double in = inputs[c][s];
        lpLowState = lowAlpha * lpLowState + (1.0 - lowAlpha) * in;
        const double low = lpLowState;
        const double aboveLow = in - low;
        lpMidState = midAlpha * lpMidState + (1.0 - midAlpha) * aboveLow;
        const double mid = lpMidState;
        const double high = aboveLow - mid;
        const double out = (low * lowGain[s] + mid * midGain[s] + high * highGain[s]) * levelGain[s];
        mOutputs[c][s] = static_cast<sample>(out);
      }
    }
    return _GetPointers();
  }

private:
  void _EnsureSize(int numChannels, int numFrames)
  {
    if ((int)mLpLowState.size() != numChannels)
      mLpLowState.assign(numChannels, 0.0);
    if ((int)mLpMidState.size() != numChannels)
      mLpMidState.assign(numChannels, 0.0);
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  double mSampleRate = 48000.0;
  Glide mLowGain, mMidGain, mHighGain, mLevelGain;
  std::vector<double> mGlideBuf[4];
  std::vector<double> mLpLowState;
  std::vector<double> mLpMidState;
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

// Classic amplitude-modulation tremolo: an LFO smoothly scales the signal's gain between
// (1 - depth) and 1. The second channel's LFO phase is offset 90 degrees (same trick as Chorus)
// so a stereo call doesn't come out dual-mono.
class Tremolo
{
public:
  void SetSampleRate(double sr) { mSampleRate = sr; }

  void SetParams(double rateHz, double depth01, double mix01 = 1.0)
  {
    mRateHz = rateHz;
    mDepth.Set(depth01);
    mMix.Set(mix01);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    // Depth and Mix glide (1.42.0).
    const double coef = GlideCoef(kKnobGlideS, mSampleRate);
    const double* depth = mDepth.Fill(mGlideBuf[0], numFrames, coef);
    const double* mix = mMix.Fill(mGlideBuf[1], numFrames, coef);
    for (int c = 0; c < numChannels; c++)
    {
      double& phase = mPhase[c];
      for (int s = 0; s < numFrames; s++)
      {
        const double in = inputs[c][s];
        const double lfo = 0.5 * (1.0 - std::cos(phase)); // 0..1, starts at 0
        const double gain = 1.0 - depth[s] * lfo;
        const double wet = in * gain;
        mOutputs[c][s] = static_cast<sample>(in * (1.0 - mix[s]) + wet * mix[s]);

        phase += 2.0 * kPedalsPi * mRateHz / mSampleRate;
        if (phase > 2.0 * kPedalsPi)
          phase -= 2.0 * kPedalsPi;
      }
    }
    return _GetPointers();
  }

private:
  void _EnsureSize(int numChannels, int numFrames)
  {
    if ((int)mPhase.size() != numChannels)
    {
      mPhase.assign(numChannels, 0.0);
      if (numChannels > 1)
        mPhase[1] = kPedalsPi * 0.5;
    }
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  double mSampleRate = 48000.0;
  double mRateHz = 5.0;
  Glide mDepth, mMix;
  std::vector<double> mGlideBuf[2];
  std::vector<double> mPhase;
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

// Classic flanger: a very short delay (0.25 ms up to ~7 ms) swept by a triangle LFO and mixed with
// the dry signal, so the comb-filter notches sweep up and down ("jet" whoosh). The sweep is
// exponential in delay time, so the notches move evenly in pitch, like a bucket-brigade flanger.
// Regen feeds the delayed signal back into the line: positive sharpens the peaks (metallic),
// negative gives the hollow, through-a-tube sound. The right channel's LFO runs 90 degrees ahead.
class Flanger
{
public:
  void SetSampleRate(double sr)
  {
    mSampleRate = sr;
    int size = 1;
    while (size < (int)(kMaxDelayMs * 0.001 * sr) + 8)
      size <<= 1;
    mBufferSize = size;
  }

  // depth01: how far the delay sweeps; regen: -1..1 (scaled to +/-0.9 inside); mix01: 0.5 = the
  // deepest notches.
  void SetParams(double rateHz, double depth01, double regen, double mix01)
  {
    mRateHz = rateHz;
    mDepthTarget = std::clamp(depth01, 0.0, 1.0);
    mRegenTarget = 0.9 * std::clamp(regen, -1.0, 1.0);
    mMixTarget = std::clamp(mix01, 0.0, 1.0);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    const int mask = mBufferSize - 1;
    // Knob changes glide over ~10 ms instead of jumping (a jump in delay time clicks).
    const double smooth = 1.0 - std::exp(-1.0 / (0.010 * mSampleRate));
    const double phaseInc = mRateHz / mSampleRate;
    for (int c = 0; c < numChannels; c++)
    {
      auto& buf = mBuffer[c];
      int& writePos = mWritePos[c];
      double& phase = mPhase[c];
      double depth = mDepth, regen = mRegen, mix = mMix;
      for (int s = 0; s < numFrames; s++)
      {
        depth += smooth * (mDepthTarget - depth);
        regen += smooth * (mRegenTarget - regen);
        mix += smooth * (mMixTarget - mix);

        const double tri = phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase; // 0..1..0
        const double maxMs = kMinDelayMs + depth * (kMaxDelayMs - kMinDelayMs);
        const double delayMs = kMinDelayMs * std::pow(maxMs / kMinDelayMs, tri);
        const double readPos = (double)writePos - delayMs * 0.001 * mSampleRate;
        const double fl = std::floor(readPos);
        const int i1 = (int)fl;
        const double t = readPos - fl;
        // 4-point Hermite interpolation: linear interpolation dulls the highs and makes the sweep
        // itself audible as a wobble in brightness at these short delays.
        const double y0 = buf[(i1 - 1) & mask], y1 = buf[i1 & mask], y2 = buf[(i1 + 1) & mask],
                     y3 = buf[(i1 + 2) & mask];
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        const double delayed = ((c3 * t + c2) * t + c1) * t + y1;

        const double in = inputs[c][s];
        // tanh keeps hard-driven regen from running away, and is transparent at normal levels.
        buf[writePos] = static_cast<sample>(in + regen * std::tanh(delayed));
        // Regen piles up level at the peaks; take some back so turning it up isn't a volume boost.
        const double wetGain = 1.0 - 0.5 * std::fabs(regen);
        // Makeup: the notches remove level (about 4 dB at an even mix), so switching the pedal on
        // would sound like a volume drop. Averaged over the sweep, dry and wet add in power; the
        // wet line's power is wetGain^2 / (1 - regen^2) (the regen's echoes).
        const double wetPower = wetGain * wetGain / (1.0 - regen * regen);
        const double makeup = 1.0 / std::sqrt((1.0 - mix) * (1.0 - mix) + mix * mix * wetPower);
        mOutputs[c][s] = static_cast<sample>((in * (1.0 - mix) + delayed * wetGain * mix) * makeup);

        writePos = (writePos + 1) & mask;
        phase += phaseInc;
        if (phase >= 1.0)
          phase -= 1.0;
      }
      if (c == numChannels - 1)
      {
        mDepth = depth;
        mRegen = regen;
        mMix = mix;
      }
    }
    return _GetPointers();
  }

private:
  static constexpr double kMinDelayMs = 0.25;
  static constexpr double kMaxDelayMs = 7.0;

  void _EnsureSize(int numChannels, int numFrames)
  {
    // As in Chorus: reallocate when the sample rate changed the line's size, too.
    const bool wrongBufferSize = !mBuffer.empty() && (int)mBuffer[0].size() != mBufferSize;
    if ((int)mBuffer.size() != numChannels || wrongBufferSize)
    {
      mBuffer.assign(numChannels, std::vector<sample>(mBufferSize, 0.0));
      mWritePos.assign(numChannels, 0);
      mPhase.assign(numChannels, 0.0);
      if (numChannels > 1)
        mPhase[1] = 0.25;
    }
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  double mSampleRate = 48000.0;
  int mBufferSize = 512;
  double mRateHz = 0.25;
  double mDepthTarget = 0.7, mRegenTarget = 0.45, mMixTarget = 0.5;
  double mDepth = 0.7, mRegen = 0.45, mMix = 0.5; // smoothed
  std::vector<std::vector<sample>> mBuffer;
  std::vector<int> mWritePos;
  std::vector<double> mPhase; // 0..1
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

// Classic 4-stage phaser (Phase 90 / Small Stone style): four first-order allpass filters share
// one swept break frequency; mixed with the dry signal, they cancel it at two frequencies, and
// those two notches sweep up and down. The sweep is exponential in frequency (even in pitch),
// from 150 Hz up to ~4 kHz at full Depth, with a sine LFO. Regen feeds the last stage back into
// the first: positive sharpens the peaks between the notches (the "vocal" sound), negative makes
// it softer and hollower. The right channel's LFO runs 90 degrees ahead.
class Phaser
{
public:
  void SetSampleRate(double sr) { mSampleRate = sr; }

  // depth01: how far the notches sweep; regen: -1..1 (scaled to +/-0.7 inside); mix01: 0.5 = the
  // deepest notches.
  void SetParams(double rateHz, double depth01, double regen, double mix01)
  {
    mRateHz = rateHz;
    mDepthTarget = std::clamp(depth01, 0.0, 1.0);
    mRegenTarget = 0.7 * std::clamp(regen, -1.0, 1.0);
    mMixTarget = std::clamp(mix01, 0.0, 1.0);
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    // Knob changes glide over ~10 ms instead of jumping.
    const double smooth = 1.0 - std::exp(-1.0 / (0.010 * mSampleRate));
    const double phaseInc = 2.0 * kPedalsPi * mRateHz / mSampleRate;
    // Keep the top of the sweep well under Nyquist at low sample rates.
    const double maxHz = std::min(kMinHz * std::pow(2.0, kMaxOctaves), 0.35 * mSampleRate);
    const double maxOctaves = std::log2(maxHz / kMinHz);
    for (int c = 0; c < numChannels; c++)
    {
      State& st = mState[c];
      double depth = mDepth, regen = mRegen, mix = mMix;
      for (int s = 0; s < numFrames; s++)
      {
        depth += smooth * (mDepthTarget - depth);
        regen += smooth * (mRegenTarget - regen);
        mix += smooth * (mMixTarget - mix);

        const double lfo = 0.5 - 0.5 * std::cos(st.phase); // 0..1, starts at the bottom
        const double fc = kMinHz * std::exp2(depth * maxOctaves * lfo);
        const double t = std::tan(kPedalsPi * fc / mSampleRate);
        const double a = (t - 1.0) / (t + 1.0);

        const double in = inputs[c][s];
        // tanh keeps hard-driven regen from running away, and is transparent at normal levels.
        double x = in + regen * std::tanh(st.last);
        for (int k = 0; k < kStages; k++)
        {
          // First-order allpass: y = a*x + x[n-1] - a*y[n-1].
          const double y = a * x + st.x1[k] - a * st.y1[k];
          st.x1[k] = x;
          st.y1[k] = y;
          x = y;
        }
        st.last = x;
        // Makeup: with an even mix the two notches cost about 2-3 dB, so switching the pedal on
        // would sound like a volume drop. Same power estimate as the Flanger's (dry and wet add in
        // power; regen raises the wet's power by 1 / (1 - regen^2)).
        const double wetPower = 1.0 / (1.0 - regen * regen);
        const double makeup = 1.0 / std::sqrt((1.0 - mix) * (1.0 - mix) + mix * mix * wetPower);
        mOutputs[c][s] = static_cast<sample>((in * (1.0 - mix) + x * mix) * makeup);

        st.phase += phaseInc;
        if (st.phase > 2.0 * kPedalsPi)
          st.phase -= 2.0 * kPedalsPi;
      }
      if (c == numChannels - 1)
      {
        mDepth = depth;
        mRegen = regen;
        mMix = mix;
      }
    }
    return _GetPointers();
  }

private:
  static constexpr int kStages = 4;
  static constexpr double kMinHz = 150.0;
  static constexpr double kMaxOctaves = 4.75; // 150 Hz -> ~4 kHz

  struct State
  {
    double x1[kStages] = {};
    double y1[kStages] = {};
    double last = 0.0;
    double phase = 0.0;
  };

  void _EnsureSize(int numChannels, int numFrames)
  {
    if ((int)mState.size() != numChannels)
    {
      mState.assign(numChannels, State());
      if (numChannels > 1)
        mState[1].phase = kPedalsPi * 0.5;
    }
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() != numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
  }
  sample** _GetPointers()
  {
    for (size_t c = 0; c < mOutputs.size(); c++)
      mPointers[c] = mOutputs[c].data();
    return mPointers.data();
  }

  double mSampleRate = 48000.0;
  double mRateHz = 0.5;
  double mDepthTarget = 0.7, mRegenTarget = 0.21, mMixTarget = 0.5;
  double mDepth = 0.7, mRegen = 0.21, mMix = 0.5; // smoothed
  std::vector<State> mState;
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

} // namespace pedals
