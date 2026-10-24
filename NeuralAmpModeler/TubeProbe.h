#pragma once

// Tube Lab debug probes (1.32.0): live node voltages and currents from the circuit simulation, for
// the debug page's oscilloscope, circuit map, "feel" readout and CSV log.
//
// The audio thread (TubeStage, channel 0 only) writes one frame per host sample into a ring buffer
// and keeps running statistics; the UI thread reads both. Nothing is captured unless the debug page
// is open or the CSV log is on (Enabled()), so it costs nothing otherwise.
//
// * Frames: a single-writer ring with an atomic write count. The reader copies frames well behind
//   the writer (a window of a few thousand out of kRingFrames), so the writer never laps it mid-copy.
// * Statistics: accumulated per oversampled sample on the audio thread. The UI asks for them
//   (RequestStats), the audio thread hands its accumulation over at the end of the next block and
//   starts a fresh one, and the UI picks it up (TakeStats). One handoff at a time, so the two sides
//   never touch the same ProbeStats at once.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace tube
{
enum ProbeChannel
{
  kProbeInput = 0, // Signal arriving at V1's input coupling cap, after Drive (V)
  kProbeV1Vgk, // V1 grid-to-cathode (V): bias plus signal; grid current above about -0.2 V
  kProbeV1Plate, // V1 plate (V)
  kProbeV1Cathode, // V1 cathode (V)
  kProbeV1Current, // V1 plate current (mA)
  kProbePreRail, // Preamp B+ at the plate resistor (V)
  kProbeV1Block, // How much colder V1 sits than its set bias, from the input cap's charge (V)
  kProbeV2Vgk, // V2 grid-to-cathode (V)
  kProbeV2Plate, // V2 plate (V)
  kProbeV2Block, // V2's extra cold bias from the interstage cap (V)
  kProbeStackOut, // Preamp out, after the tone stack (normalized: 1 = full scale)
  kProbePAGrid1, // Power tube 1 control grid (V)
  kProbePAGrid2, // Power tube 2 control grid (V)
  kProbePAPlate1, // Power tube 1 plate (V)
  kProbePAPlate2, // Power tube 2 plate (V)
  kProbePACurrent1, // Power tube 1 plate current (mA)
  kProbePACurrent2, // Power tube 2 plate current (mA)
  kProbePARail, // Power amp B+ (V)
  kProbePAScreen, // Power tube screens (V)
  kProbeOutput, // Tube Lab output (normalized)
  // 1.68.0: the chassis sockets V5 and V6's plates (V), while they're gain stages. After the others, so
  // saved scope channels keep their meaning; the circuit map doesn't show them (the scope does).
  kProbeSocketPlate0,
  kNumProbes = kProbeSocketPlate0 + 2
};

struct ProbeInfo
{
  const char* name; // Short, for the scope's channel menu and the circuit map
  const char* unit;
};
inline const ProbeInfo& GetProbeInfo(int channel)
{
  static const ProbeInfo kInfo[kNumProbes] = {
    {"Input (after Drive)", "V"}, {"V1 grid-cathode", "V"}, {"V1 plate", "V"},       {"V1 cathode", "V"},
    {"V1 plate current", "mA"},   {"Preamp B+", "V"},       {"V1 blocking", "V"},    {"V2 grid-cathode", "V"},
    {"V2 plate", "V"},            {"V2 blocking", "V"},     {"Tone stack out", "FS"}, {"Power grid 1", "V"},
    {"Power grid 2", "V"},        {"Power plate 1", "V"},   {"Power plate 2", "V"},  {"Power tube 1 current", "mA"},
    {"Power tube 2 current", "mA"}, {"Power B+", "V"},      {"Screens", "V"},        {"Output", "FS"},
    {"V5 plate (socket)", "V"},   {"V6 plate (socket)", "V"}};
  return kInfo[std::clamp(channel, 0, kNumProbes - 1)];
}

// Grid-to-cathode voltage above which a 12AX7 draws real grid current (about 1 uA in the Dempwolf
// model, vs. 0.08 uA leakage): the grid has started to clamp the signal.
constexpr double kGridConductVgk = -0.2;
// A tube passing under this fraction of its idle plate current is counted as cut off.
constexpr double kCutoffFraction = 0.05;

struct ProbeStats
{
  // Per host block.
  int64_t hostSamples = 0;
  double cpuSeconds = 0.0; // Time spent in the Tube Lab's Process
  double audioSeconds = 0.0; // Audio those calls produced
  // Per oversampled sample.
  int64_t samples = 0;
  double inPeak = 0.0; // Input to the Tube Lab (normalized, before Drive)
  double gridDrivePeak = 0.0; // Same after Drive (V)
  double outPeak = 0.0, outSumSq = 0.0;
  int64_t outSamples = 0;
  // Preamp triodes [V1, V2].
  double vgkMin[2] = {1e9, 1e9}, vgkMax[2] = {-1e9, -1e9};
  double plateMin[2] = {1e9, 1e9}, plateMax[2] = {-1e9, -1e9};
  double blockMax[2] = {0.0, 0.0};
  int64_t gridConducting[2] = {0, 0}, cutoff[2] = {0, 0};
  double railMin = 1e9, railMax = -1e9;
  int newtonMax = 0;
  int64_t newtonSum = 0;
  // Power amp.
  int64_t paSamples = 0;
  double paGridMax = -1e9, paGridMin = 1e9;
  int64_t paGridConducting = 0, paCutoff = 0;
  double paCurrentMax = 0.0;
  double paRailMin = 1e9, paScreenMin = 1e9;
  double paCurrentSum[2] = {0.0, 0.0}, paDissSum[2] = {0.0, 0.0}; // Per power tube: A and W, summed

  // Operating points at rest (copied in at the handoff, so the UI can explain them).
  bool circuit = true, stage2 = false, powerAmp = false;
  int oversample = 1;
  double sampleRate = 48000.0;
  double rk[2] = {0.0, 0.0}, idleMA[2] = {0.0, 0.0}, plateRest[2] = {0.0, 0.0}, rail = 250.0;
  double cathodeCornerHz[2] = {0.0, 0.0}; // Where the bypass cap's full gain starts (0 = no cap)
  double blockRecoverMs[2] = {0.0, 0.0}; // Input / interstage cap x grid leak
  double paBias = 0.0, paIdleMA = 0.0, paRailRest = 450.0, paScreenRest = 445.0, feedbackDB = 0.0;
  // Bench (1.58.0, TubeBench.h): the Variac now and where it's going, the heaters and cathodes, each
  // section's emission reserve and perveance ([preamp, power]), the power tubes' bias supply now (V;
  // 0 with cathode bias), resting cathode voltage (cathode bias), rated watts and bias pot.
  double mainsV = 120.0, mainsTargetV = 120.0, heaterV = 6.3, cathodeK = 1050.0;
  double reserve[2] = {8.0, 8.0}, perveance[2] = {1.0, 1.0};
  double paBiasNow = 0.0, paCathodeRest = 0.0, paRatedW = 25.0, paBiasTrim = 1.0;
  bool cathodeBias = false, sixL6 = false;

  void ResetCounters()
  {
    const ProbeStats fresh;
    hostSamples = fresh.hostSamples;
    cpuSeconds = audioSeconds = 0.0;
    samples = 0;
    inPeak = gridDrivePeak = outPeak = outSumSq = 0.0;
    outSamples = 0;
    for (int s = 0; s < 2; s++)
    {
      vgkMin[s] = fresh.vgkMin[s];
      vgkMax[s] = fresh.vgkMax[s];
      plateMin[s] = fresh.plateMin[s];
      plateMax[s] = fresh.plateMax[s];
      blockMax[s] = 0.0;
      gridConducting[s] = cutoff[s] = 0;
    }
    railMin = fresh.railMin;
    railMax = fresh.railMax;
    newtonMax = 0;
    newtonSum = 0;
    paSamples = 0;
    paGridMax = fresh.paGridMax;
    paGridMin = fresh.paGridMin;
    paGridConducting = paCutoff = 0;
    paCurrentMax = 0.0;
    paRailMin = paScreenMin = 1e9;
    for (int t = 0; t < 2; t++)
      paCurrentSum[t] = paDissSum[t] = 0.0;
  }

  double Percent(int64_t count, int64_t of) const { return of > 0 ? 100.0 * count / of : 0.0; }

  // Takes the operating-point fields from `o` (for showing an older window's counters against the
  // current settings).
  void CopyRestingFrom(const ProbeStats& o)
  {
    circuit = o.circuit;
    stage2 = o.stage2;
    powerAmp = o.powerAmp;
    oversample = o.oversample;
    sampleRate = o.sampleRate;
    for (int s = 0; s < 2; s++)
    {
      rk[s] = o.rk[s];
      idleMA[s] = o.idleMA[s];
      plateRest[s] = o.plateRest[s];
      cathodeCornerHz[s] = o.cathodeCornerHz[s];
      blockRecoverMs[s] = o.blockRecoverMs[s];
    }
    rail = o.rail;
    paBias = o.paBias;
    paIdleMA = o.paIdleMA;
    paRailRest = o.paRailRest;
    paScreenRest = o.paScreenRest;
    feedbackDB = o.feedbackDB;
    mainsV = o.mainsV;
    mainsTargetV = o.mainsTargetV;
    heaterV = o.heaterV;
    cathodeK = o.cathodeK;
    for (int t = 0; t < 2; t++)
    {
      reserve[t] = o.reserve[t];
      perveance[t] = o.perveance[t];
    }
    paBiasNow = o.paBiasNow;
    paCathodeRest = o.paCathodeRest;
    paRatedW = o.paRatedW;
    paBiasTrim = o.paBiasTrim;
    cathodeBias = o.cathodeBias;
    sixL6 = o.sixL6;
  }
};

class TubeProbe
{
public:
  static constexpr int kRingFrames = 32768; // ~0.68 s at 48 kHz

  TubeProbe() { mRing.assign(static_cast<size_t>(kRingFrames) * kNumProbes, 0.f); }

  // UI thread: capture on/off (the debug page is open, or the CSV log is on).
  void SetEnabled(bool on) { mEnabled.store(on, std::memory_order_relaxed); }
  bool Enabled() const { return mEnabled.load(std::memory_order_relaxed); }

  // Audio thread ==================================================================================
  // The frame being filled for the current host sample; Push() publishes it.
  float* Frame() { return mFrame; }
  void Push()
  {
    const int64_t n = mWritten.load(std::memory_order_relaxed);
    std::copy(mFrame, mFrame + kNumProbes, &mRing[static_cast<size_t>(n % kRingFrames) * kNumProbes]);
    mWritten.store(n + 1, std::memory_order_release);
  }
  ProbeStats& Accum() { return mAccum; }
  // End of a block: hand the accumulation over if the UI asked for it. `atRest` fills in the
  // operating-point fields of the handed-over copy.
  template <typename F>
  void EndBlock(F&& atRest)
  {
    if (mState.load(std::memory_order_acquire) != kRequested)
      return;
    mSnapshot = mAccum;
    atRest(mSnapshot);
    mAccum.ResetCounters();
    mState.store(kReady, std::memory_order_release);
  }

  // UI thread =====================================================================================
  int64_t Written() const { return mWritten.load(std::memory_order_acquire); }
  // Copies the newest `count` frames (oldest first) into out[count * kNumProbes]. Returns how many
  // were available (fewer right after capture starts).
  int ReadLatest(int count, std::vector<float>& out) const
  {
    const int64_t end = Written();
    count = static_cast<int>(std::min<int64_t>({(int64_t)count, end, (int64_t)kRingFrames / 2}));
    out.resize(static_cast<size_t>(count) * kNumProbes);
    for (int i = 0; i < count; i++)
    {
      const int64_t n = end - count + i;
      std::copy_n(&mRing[static_cast<size_t>(n % kRingFrames) * kNumProbes], kNumProbes,
                  &out[static_cast<size_t>(i) * kNumProbes]);
    }
    return count;
  }
  // Copies the frames written since `cursor` (at most the newest kRingFrames / 2, if it fell
  // behind) into out, and moves the cursor to the end. Returns how many.
  int ReadSince(int64_t& cursor, std::vector<float>& out) const
  {
    const int64_t end = Written();
    const int64_t start = std::max(cursor, end - kRingFrames / 2);
    const int count = static_cast<int>(std::max<int64_t>(0, end - start));
    out.resize(static_cast<size_t>(count) * kNumProbes);
    for (int i = 0; i < count; i++)
      std::copy_n(&mRing[static_cast<size_t>((start + i) % kRingFrames) * kNumProbes], kNumProbes,
                  &out[static_cast<size_t>(i) * kNumProbes]);
    cursor = end;
    return count;
  }
  void RequestStats()
  {
    int expected = kIdle;
    mState.compare_exchange_strong(expected, kRequested, std::memory_order_acq_rel);
  }
  bool TakeStats(ProbeStats& out)
  {
    if (mState.load(std::memory_order_acquire) != kReady)
      return false;
    out = mSnapshot;
    mState.store(kIdle, std::memory_order_release);
    return true;
  }

private:
  enum
  {
    kIdle = 0,
    kRequested,
    kReady
  };
  std::atomic<bool> mEnabled{false};
  std::vector<float> mRing;
  std::atomic<int64_t> mWritten{0};
  float mFrame[kNumProbes] = {};
  ProbeStats mAccum, mSnapshot;
  std::atomic<int> mState{kIdle};
};
} // namespace tube
