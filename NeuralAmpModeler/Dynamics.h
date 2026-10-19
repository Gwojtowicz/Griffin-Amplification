#pragma once

// Dynamics blocks added in 1.52.0:
//   Gate       -- a full noise gate: Threshold with a separate close point (Hysteresis), Attack, Hold,
//                 Release, Range, a high-pass on what it listens to, and a Key switch. With Key on it
//                 listens to the clean guitar input (after Master In) while gating wherever it sits,
//                 the trick that keeps a gate solid after a high-gain amp: the amp's own hiss and
//                 the sustain it adds never hold the gate open, and the clean pick decides.
//   StudioComp -- a studio compressor: Threshold, Ratio (20 = limiting), Attack, Release, Knee,
//                 Makeup (or Auto), Mix (parallel compression), a high-pass on its detector, and three
//                 characters: Clean (feed-forward VCA, peak detection), Opto (RMS detection, a release
//                 that slows the longer it's been squeezing, like an LA-2A) and FET (fast, with a
//                 little grit that grows with the gain reduction, like an 1176).
// Same call convention as Pedals.h: Process(...) returns the block's own output buffers. Both are
// stereo-linked: one detector on the louder channel, one gain for every channel. Both publish meter
// values for their popups (atomics, written once per block, read by the UI).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "Pedals.h"

namespace dynamics
{
using sample = iplug::sample;

inline double ToDB(double amp) { return 20.0 * std::log10(std::max(amp, 1e-12)); }
inline double FromDB(double dB) { return std::exp(dB * 0.11512925464970229); }
inline double TimeCoef(double seconds, double sr) { return std::exp(-1.0 / (std::max(seconds, 1e-6) * sr)); }
// Meters: the audio thread keeps the highest value since the UI last read it (the UI swaps in a reset
// value), so a short peak between two UI frames still shows.
inline void StoreMax(std::atomic<float>& a, float v)
{
  if (v > a.load(std::memory_order_relaxed))
    a.store(v, std::memory_order_relaxed);
}

// A 2nd-order Butterworth high-pass for a detector (transposed direct form II). Off at 20 Hz or below.
struct DetectorHpf
{
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  bool on = false;
  double freq = -1.0, sr = -1.0;
  void Set(double hz, double sampleRate)
  {
    if (hz == freq && sampleRate == sr)
      return;
    freq = hz;
    sr = sampleRate;
    on = hz > 20.5;
    if (!on)
      return;
    const double w = 2.0 * pedals::kPedalsPi * std::min(hz, 0.45 * sampleRate) / sampleRate;
    const double c = std::cos(w), al = std::sin(w) / (2.0 * 0.7071067811865476), a0 = 1.0 + al;
    b0 = (1.0 + c) / 2.0 / a0;
    b1 = -(1.0 + c) / a0;
    b2 = (1.0 + c) / 2.0 / a0;
    a1 = -2.0 * c / a0;
    a2 = (1.0 - al) / a0;
  }
  double Process(double x)
  {
    if (!on)
      return x;
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

class BufferedBlock
{
protected:
  void _EnsureSize(int numChannels, int numFrames)
  {
    if ((int)mOutputs.size() != numChannels)
      mOutputs.resize(numChannels);
    for (auto& ch : mOutputs)
      if ((int)ch.size() < numFrames)
        ch.resize(numFrames);
    if ((int)mPointers.size() != numChannels)
      mPointers.resize(numChannels);
    for (int c = 0; c < numChannels; c++)
      mPointers[c] = mOutputs[c].data();
  }
  std::vector<std::vector<sample>> mOutputs;
  std::vector<sample*> mPointers;
};

struct GateParams
{
  double thresholdDB = -60.0; // opens above this
  double hysteresisDB = 6.0; // closes this far under the threshold
  double attackMs = 0.5;
  double holdMs = 60.0;
  double releaseMs = 150.0; // time to fall the whole Range
  double rangeDB = -80.0; // how far it turns down when closed (-90 = silence)
  bool keyInput = true; // listen to the clean input, not the block's own input
  double keyHpfHz = 20.0; // high-pass on what it listens to (20 = off)
};

class Gate : public BufferedBlock
{
public:
  void SetSampleRate(double sr) { mSampleRate = sr; }
  void SetParams(const GateParams& p) { mParams = p; }

  // key / keyChannels: the clean input (used when Key is on and it's given), else the block's input.
  sample** Process(sample** inputs, int numChannels, int numFrames, sample** key, int keyChannels)
  {
    _EnsureSize(numChannels, numFrames);
    const GateParams& p = mParams;
    const bool useKey = p.keyInput && key != nullptr && keyChannels > 0;
    sample** det = useKey ? key : inputs;
    const int detChannels = std::min(useKey ? keyChannels : numChannels, kMaxDetChannels);
    for (int c = 0; c < detChannels; c++)
      mHpf[c].Set(p.keyHpfHz, mSampleRate);
    // The detector: a peak follower that rises in 0.3 ms (fast enough for a pick, but a lone noise
    // spike doesn't trip it) and falls over 10 ms, so it doesn't drop out between the peaks of a low
    // note (82 Hz is a 12 ms cycle); Hold covers the rest.
    const double envRise = TimeCoef(0.0003, mSampleRate);
    const double envFall = TimeCoef(0.010, mSampleRate);
    const double openDB = p.thresholdDB, closeDB = p.thresholdDB - std::max(0.0, p.hysteresisDB);
    const int holdSamples = (int)(p.holdMs * 0.001 * mSampleRate);
    const double floorDB = p.rangeDB <= -89.9 ? -200.0 : std::min(0.0, p.rangeDB);
    const double floorGain = p.rangeDB <= -89.9 ? 0.0 : FromDB(floorDB);
    // Opening: a linear ramp in amplitude over Attack (no click). Closing: a straight line in dB from
    // 0 down to the floor over Release (smooth to the ear), then exactly the floor.
    const double openStep = 1.0 / (std::max(0.05, p.attackMs) * 0.001 * mSampleRate);
    const double closeStepDB = std::max(1.0, -floorDB) / (std::max(1.0, p.releaseMs) * 0.001 * mSampleRate);
    double keyPeak = 0.0, minGain = 1.0;
    for (int s = 0; s < numFrames; s++)
    {
      double x = 0.0;
      for (int c = 0; c < detChannels; c++)
        x = std::max(x, std::fabs(mHpf[c].Process(det[c][s])));
      mEnv = x > mEnv ? envRise * mEnv + (1.0 - envRise) * x : mEnv * envFall;
      keyPeak = std::max(keyPeak, mEnv);
      const double envDB = ToDB(mEnv);
      if (envDB > openDB)
      {
        mOpen = true;
        mHold = holdSamples;
      }
      else if (mOpen && envDB < closeDB)
      {
        if (mHold > 0)
          mHold--;
        else
          mOpen = false;
      }
      else if (mOpen)
        mHold = holdSamples; // between the two points: stay open, keep the hold topped up
      if (mOpen)
      {
        if (mGain < 1.0)
        {
          // Climbing from wherever the release had got to.
          mGain = std::min(1.0, std::max(mGain, floorGain) + openStep);
          mGainDB = ToDB(mGain);
        }
      }
      else if (mGain < floorGain)
      {
        // Below the floor (it starts fully closed, or Range was raised): straight to it.
        mGain = floorGain;
        mGainDB = floorDB;
      }
      else if (mGain > floorGain)
      {
        mGainDB -= closeStepDB;
        if (mGainDB <= floorDB)
        {
          mGainDB = floorDB;
          mGain = floorGain;
        }
        else
          mGain = FromDB(mGainDB);
      }
      minGain = std::min(minGain, mGain);
      for (int c = 0; c < numChannels; c++)
        mOutputs[c][s] = static_cast<sample>(inputs[c][s] * mGain);
    }
    StoreMax(meterKeyDB, (float)ToDB(keyPeak));
    StoreMax(meterClosedDB, (float)-std::max(-90.0, ToDB(minGain)));
    meterOpen.store(mOpen, std::memory_order_relaxed);
    return mPointers.data();
  }

  void Reset()
  {
    mEnv = 0.0;
    mOpen = false;
    mHold = 0;
    mGain = 0.0;
    mGainDB = -200.0;
    for (auto& h : mHpf)
      h = DetectorHpf();
  }

  // For the popup: the key's peak (dBFS), how far the gate turned down (dB, 0 open .. -Range closed),
  // and whether it's open.
  std::atomic<float> meterKeyDB{-120.f}, meterClosedDB{0.f};
  std::atomic<bool> meterOpen{false};

private:
  static constexpr int kMaxDetChannels = 2;
  double mSampleRate = 48000.0;
  GateParams mParams;
  DetectorHpf mHpf[kMaxDetChannels];
  double mEnv = 0.0;
  bool mOpen = false;
  int mHold = 0;
  double mGain = 0.0, mGainDB = -200.0; // starts closed: no burst of noise on load
};

struct CompParams
{
  double thresholdDB = -24.0;
  double ratio = 4.0; // 20 and up = limiting (infinite ratio)
  double attackMs = 5.0;
  double releaseMs = 120.0;
  double kneeDB = 6.0;
  double makeupDB = 0.0;
  bool autoMakeup = true; // adds back what a -12 dBFS peak loses (on top of Makeup)
  double mix = 1.0; // 0..1, dry/compressed
  int style = 0; // 0 Clean, 1 Opto, 2 FET
  double scHpfHz = 20.0; // high-pass on the detector (20 = off)
};

class StudioComp : public BufferedBlock
{
public:
  enum Style
  {
    kClean = 0,
    kOpto,
    kFET
  };
  static constexpr double kAutoMakeupRefDB = -12.0;

  void SetSampleRate(double sr) { mSampleRate = sr; }
  void SetParams(const CompParams& p)
  {
    mParams = p;
    mMakeup.Set(p.makeupDB + (p.autoMakeup ? -GainComputer(kAutoMakeupRefDB, p) : 0.0));
    mMix.Set(std::clamp(p.mix, 0.0, 1.0));
  }

  // The static curve: gain change (dB, <= 0) for a level x (dB).
  static double GainComputer(double xDB, const CompParams& p)
  {
    const double slope = p.ratio >= 19.99 ? -1.0 : 1.0 / std::max(1.0, p.ratio) - 1.0;
    const double knee = std::max(0.0, p.style == kOpto ? std::max(p.kneeDB, 6.0) : p.kneeDB);
    const double over = xDB - p.thresholdDB;
    if (2.0 * over <= -knee)
      return 0.0;
    if (knee > 0.0 && 2.0 * std::fabs(over) < knee)
    {
      const double t = over + knee * 0.5;
      return slope * t * t / (2.0 * knee);
    }
    return slope * over;
  }

  sample** Process(sample** inputs, int numChannels, int numFrames)
  {
    _EnsureSize(numChannels, numFrames);
    const CompParams& p = mParams;
    const int detChannels = std::min(numChannels, kMaxDetChannels);
    for (int c = 0; c < detChannels; c++)
      mHpf[c].Set(p.scHpfHz, mSampleRate);
    const double atk = TimeCoef(std::max(0.02, p.attackMs) * 0.001, mSampleRate);
    const double rmsCoef = TimeCoef(0.010, mSampleRate);
    const double memUp = 1.0 - TimeCoef(2.0, mSampleRate), memDown = 1.0 - TimeCoef(5.0, mSampleRate);
    const double coef = pedals::GlideCoef(pedals::kKnobGlideS, mSampleRate);
    const double* makeup = mMakeup.Fill(mGlideBuf[0], numFrames, coef);
    const double* mix = mMix.Fill(mGlideBuf[1], numFrames, coef);
    double relBase = TimeCoef(std::max(5.0, p.releaseMs) * 0.001, mSampleRate);
    double maxGR = 0.0, inPeak = 0.0, outPeak = 0.0;
    for (int s = 0; s < numFrames; s++)
    {
      double x = 0.0, x2 = 0.0;
      for (int c = 0; c < detChannels; c++)
      {
        const double v = mHpf[c].Process(inputs[c][s]);
        x = std::max(x, std::fabs(v));
        x2 = std::max(x2, v * v);
      }
      double levelDB;
      if (p.style == kOpto)
      {
        // RMS over ~10 ms, +3 dB so a steady sine reads its peak.
        mMeanSquare = rmsCoef * mMeanSquare + (1.0 - rmsCoef) * x2;
        levelDB = 10.0 * std::log10(std::max(mMeanSquare, 1e-24)) + 3.0103;
      }
      else
        levelDB = ToDB(x);
      const double target = GainComputer(levelDB, p);
      // Smooth the gain change: Attack when it's reducing more, Release when it's letting go.
      double rel = relBase;
      if (p.style == kOpto)
      {
        // The longer and harder it's been squeezing, the slower it lets go (up to 5x Release),
        // like an optical cell's memory.
        mMemory += (-mGR > mMemory ? memUp : memDown) * (-mGR - mMemory);
        const double slow = 1.0 + 4.0 * std::clamp(mMemory / 10.0, 0.0, 1.0);
        rel = TimeCoef(std::max(5.0, p.releaseMs) * 0.001 * slow, mSampleRate);
      }
      mGR = target < mGR ? atk * mGR + (1.0 - atk) * target : rel * mGR + (1.0 - rel) * target;
      maxGR = std::max(maxGR, -mGR);
      const double g = FromDB(mGR + makeup[s]);
      // FET: a little grit as it compresses (a soft clip whose drive grows with the reduction, up to
      // 1.6x at 20 dB; clean at low levels, a dB or two of extra squash on full-scale peaks).
      const double drive = p.style == kFET ? 1.0 + 0.6 * std::clamp(-mGR / 20.0, 0.0, 1.0) : 1.0;
      for (int c = 0; c < numChannels; c++)
      {
        const double in = inputs[c][s];
        double wet = in * g;
        if (drive > 1.0)
          wet = std::tanh(wet * drive) / drive;
        const double out = in + mix[s] * (wet - in);
        inPeak = std::max(inPeak, std::fabs(in));
        outPeak = std::max(outPeak, std::fabs(out));
        mOutputs[c][s] = static_cast<sample>(out);
      }
    }
    StoreMax(meterGR, (float)maxGR);
    StoreMax(meterInDB, (float)ToDB(inPeak));
    StoreMax(meterOutDB, (float)ToDB(outPeak));
    return mPointers.data();
  }

  void Reset()
  {
    mGR = 0.0;
    mMeanSquare = 0.0;
    mMemory = 0.0;
    for (auto& h : mHpf)
      h = DetectorHpf();
  }

  // For the popup: the most gain reduction this block (dB, positive), input and output peaks (dBFS).
  std::atomic<float> meterGR{0.f}, meterInDB{-120.f}, meterOutDB{-120.f};

private:
  static constexpr int kMaxDetChannels = 2;
  double mSampleRate = 48000.0;
  CompParams mParams;
  DetectorHpf mHpf[kMaxDetChannels];
  pedals::Glide mMakeup, mMix;
  std::vector<double> mGlideBuf[2];
  double mGR = 0.0; // smoothed gain change, dB (<= 0)
  double mMeanSquare = 0.0;
  double mMemory = 0.0; // Opto: how hard it's been squeezing lately (dB)
};

} // namespace dynamics
