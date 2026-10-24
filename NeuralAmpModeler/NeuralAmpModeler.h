#pragma once

#include <atomic>
#include <climits>
#include <chrono>

#include "../AudioDSPTools/dsp/ImpulseResponse.h"
#include "FastImpulseResponse.h" // The cab IRs are made as these (1.35.0): FFT convolution
#include "OutputLimiter.h"
#include "StereoWidth.h"
#include "../AudioDSPTools/dsp/NoiseGate.h"
#include "../AudioDSPTools/dsp/dsp.h"
#include "../AudioDSPTools/dsp/wav.h"
#include "../AudioDSPTools/dsp/ResamplingContainer/ResamplingContainer.h"
#include "../NeuralAmpModelerCore/NAM/dsp.h"
#include "../NeuralAmpModelerCore/NAM/slimmable.h"

#include "Colors.h"
#include "ToneStack.h"
#include "TubeStage.h"
#include "TubeDebug.h"
#include "CabLab.h"
#include "ChainGrid.h"

#include "IPlug_include_in_plug_hdr.h"
#include <future>
#include "PackScanner.h" // 1.70.0 PACK PICKER

// TEMP drag-debug log, defined in NeuralAmpModelerControls.h.
inline void NAMDragLog(const char* fmt, ...);
#include "ISender.h"

#include "Pedals.h"
#include "NewPedals.h"
#include "ProEQ.h"
#include "Dynamics.h"
#include "MorePedals.h"
#include "Spectrum.h"
#include "RealFeel.h"
#include "MainsHum.h"
#include "Tuner.h"


const int kNumPresets = 1;
// The plugin is mono inside
constexpr size_t kNumChannelsInternal = 1;

// NC channels; must match the NAMMeterControl<NC> it feeds.
template <int NC = 1>
class NAMSender : public iplug::IPeakAvgSender<NC>
{
public:
  NAMSender()
  // RMS over 5 ms, 1 ms attack, 100 ms decay (the meter adds its own smooth glide on top, so a
  // longer decay here would just make it lag), 500 ms peak hold.
  : iplug::IPeakAvgSender<NC>(-90.0, true, 5.0f, 1.0f, 100.0f, 500.0f)
  {
  }
};

// Extra pedal copies (1.19.0, see chaingrid::kFirstExtraPedal): each has Active, Engaged, then
// its type's knobs, in this order, starting at kExtraPedalParams0. Knob counts per type (Comp, OD,
// Chorus, Delay, Reverb, EQ, Tremolo) -- checked against kPedals in the .cpp.
constexpr int kMultiPedalKnobs[chaingrid::kNumMultiPedalTypes] = {3, 4, 3, 3, 2, 4, 3};
constexpr int ExtraPedalParamOffset(int extraIdx)
{
  int offset = 0;
  for (int i = 0; i < extraIdx; i++)
    offset += 2 + kMultiPedalKnobs[i / chaingrid::kExtraCopiesPerType];
  return offset;
}
constexpr int kNumExtraPedalParams = ExtraPedalParamOffset(chaingrid::kNumExtraPedals);
// The grid ids that had their GridCell/GridIn params laid out in the middle of the params (ids
// 0..31, less Input). Each later id's grid params were added at the end of the params with its
// block (kGridCell32, kGridCell33, ...), so adding a block never moves any param's index (a DAW
// automates params by index). See GridCellParam.
constexpr int kNumGridSlots32 = 31;

enum EParams
{
  // These need to be the first ones because I use their indices to place
  // their rects in the GUI.
  kInputLevel = 0,
  kNoiseGateThreshold,
  kToneBass,
  kToneMid,
  kToneTreble,
  kOutputLevel,
  // The rest is fine though.
  kNoiseGateActive,
  kEQActive,
  kIRToggle,
  // Input calibration
  kCalibrateInput,
  kInputCalibrationLevel,
  kOutputMode,
  kSlim,
  kABSlot,
  // Post-amp tone shaping. Bipolar: 0 = no effect, +/- adds or subtracts that frequency band.
  kDensity,
  kDensityActive,
  kPresence, // Labeled "Boost" in the GUI
  kPresenceActive,
  // AMP and CAB are draggable blocks too, each with their own Slot -- see kAmpSlotMin/Max and the
  // pedal Slot comment below for how everything's ordering interacts.
  kAmpSlot,
  kCabSlot,
  // FX Loop Send/Return: a real hardware/DAW send-return pair via an extra stereo aux bus (see
  // PLUG_CHANNEL_IO) -- Send writes the chain's current signal to the aux output (for the DAW to
  // route out to external gear or another plugin); Return replaces the chain's signal with
  // whatever comes back on the aux input. Two separate draggable blocks, same Slot mechanism as
  // everything else, so where you place them in the chain is where the loop actually taps in/out.
  kLoopSendSlot,
  kLoopReturnSlot,
  // Pedalboard. Each pedal has a Slot param, a free-form chain-order position relative to AMP's own
  // (also draggable) Slot: <= AMP's slot is before the amp model (mono), > AMP's slot is in the
  // effects loop after the tone stack (stereo; CAB's own Slot places it among these the same way).
  // Pedals are sorted by Slot value within each side to determine processing order, so dragging a
  // pedal to a new Slot value both moves it Pre<->Loop and reorders it relative to the others.
  //
  // "Active" = is this pedal in the chain at all (chip in the rack vs. the tray) -- set only by
  // dragging, never by the on/off switch or MIDI/automation.
  // "Engaged" = the actual real-time on/off (bypass) switch -- mappable to MIDI/a DAW automation
  // lane. Flipping it does NOT move the pedal; a bypassed-but-in-chain pedal just stays put in its
  // slot and reads as "off" (so a MIDI footswitch doesn't make the chip jump to the tray and back).
  kCompressorActive,
  kCompressorEngaged,
  kCompressorSlot,
  kCompressorSustain,
  kCompressorLevel,
  kCompressorMix,
  kOverdriveActive,
  kOverdriveEngaged,
  kOverdriveSlot,
  kOverdriveDrive,
  kOverdriveTone,
  kOverdriveLevel,
  kOverdriveMix,
  kChorusActive,
  kChorusEngaged,
  kChorusSlot,
  kChorusRate,
  kChorusDepth,
  kChorusMix,
  kDelayActive,
  kDelayEngaged,
  kDelaySlot,
  kDelayTime,
  kDelayFeedback,
  kDelayMix,
  kReverbActive,
  kReverbEngaged,
  kReverbSlot,
  kReverbSize,
  kReverbMix,
  kEqPedalActive,
  kEqPedalEngaged,
  kEqPedalSlot,
  kEqPedalLow,
  kEqPedalMid,
  kEqPedalHigh,
  kEqPedalLevel,
  kTremoloActive,
  kTremoloEngaged,
  kTremoloSlot,
  kTremoloRate,
  kTremoloDepth,
  kTremoloMix,
  // NAM-file pedals: downloaded captures (e.g. Tone3000 stompbox/pedal .nam files) rather than a
  // coded algorithm. Mono-only (matches the amp model's own constraint), so these are restricted
  // to pre-amp slots only -- see kNamPedalSlotMin/Max below.
  kNamPedal0Active,
  kNamPedal0Engaged,
  kNamPedal0Slot,
  kNamPedal0Level,
  kNamPedal0Mix,
  kNamPedal1Active,
  kNamPedal1Engaged,
  kNamPedal1Slot,
  kNamPedal1Level,
  kNamPedal1Mix,
  // Tube Lab: a 12AX7 tube stage (TubeStage.h), a built-in pedal like the ones above (Active /
  // Engaged / Slot, any position, pre-amp or loop). These come last, after every param that's
  // saved positionally -- see kNumPositionalParams.
  kTubeActive,
  kTubeEngaged,
  kTubeSlot,
  kTubeDrive,
  kTubeBias,
  kTubeBloom,
  kTubeBright,
  kTubeMix,
  kTubeOutput,
  // Tube Lab engine and circuit parts (1.11.0): the Circuit engine simulates the whole stage; these
  // are its components. Classic (Circuit off) is the 1.8 lookup-table engine and ignores them.
  kTubeCircuit,
  kTubeCathodeCap, // Stepped: None or one of the stock bypass cap values (see kTubeCathodeCapUF)
  kTubePlate,
  kTubePlateCap, // Stepped: None or a cap across the plate resistor (see kTubePlateCapPF)
  kTubeSagOn,
  kTubeSagSupply,
  kTubeSagTime,
  // Tube Lab second, cascaded stage (1.12.0; Circuit engine only).
  kTubeStage2,
  kTubeGain2,
  kTubeBias2,
  kTubeCap2, // Stepped, like kTubeCathodeCap
  kTubePlateCap2, // Stepped, like kTubePlateCap
  kTubeCouple, // Stepped interstage coupling cap (see kTubeCoupleNF)
  // Tube Lab power amp (1.13.0; Circuit engine only).
  kTubePower,
  kTubeMaster,
  kTubePowerSag,
  kTubeFeedback,
  kTubePresence,
  kTube6L6,
  kTubeSpeakerLoad, // 1.15.0: power amp drives a real speaker's impedance
  // CAB LAB (1.15.0): speaker cab (modeled or IR), two close mics and a room, stereo out.
  kCabLabActive,
  kCabLabEngaged,
  kCabLabSlot,
  kCabSource, // Off: modeled speaker; on: the CAB LAB's own IR
  kCabSpeaker,
  kMicAType,
  kMicAPos,
  kMicADist,
  kMicALevel,
  kMicAPan,
  kMicBOn,
  kMicBType,
  kMicBPos,
  kMicBDist,
  kMicBLevel,
  kMicBPan,
  kMicBInvert,
  kRoomLevel,
  kRoomSize,
  kRoomWidth,
  kCabOutput,
  // Signal-chain grid (1.17.0, see ChainGrid.h). These replace the Slot params above for ordering:
  // each block's cell, each block's input cables, and Output's. kChainGridV1 is set once a state has
  // a grid; states saved before 1.17 lack it, and their Slot order is laid out on the grid on load.
  kChainGridV1,
  kGridCell0, // one per grid id 0..31 except Input (see GridCellParam); named by id
  kGridIn0 = kGridCell0 + kNumGridSlots32,
  kGridOut = kGridIn0 + kNumGridSlots32,
  // AMP block on/off (1.19.1): off passes the signal straight through the block (model, gate gain
  // and tone stack all skipped). MIDI/automation-mappable like a pedal's Engaged.
  kAmpEngaged,
  // Tube Lab tone stack (1.20.0, ToneStackCircuit.h): Off / Fender / Marshall, and its knobs.
  kTubeStack,
  kTubeBass,
  kTubeMid,
  kTubeTreble,
  kExtraPedalParams0,
  // FX loop on/off and mix (1.23.0): off skips the loop (nothing is sent; the Return passes the
  // chain straight through); Mix blends the Return with the signal arriving at it (100% = the
  // Return replaces it, as before). After the extra pedal params so their indices don't move.
  kLoopEngaged = kExtraPedalParams0 + kNumExtraPedalParams,
  kLoopMix,
  // Flanger pedal (1.27.0, grid id chaingrid::kFlanger). No Slot param: it never existed before
  // the chain grid, so there's no legacy order to migrate.
  kFlangerActive,
  kFlangerEngaged,
  kFlangerRate,
  kFlangerDepth,
  kFlangerRegen,
  kFlangerMix,
  // Phaser pedal (1.28.0, grid id chaingrid::kPhaser), laid out like the Flanger's.
  kPhaserActive,
  kPhaserEngaged,
  kPhaserRate,
  kPhaserDepth,
  kPhaserRegen,
  kPhaserMix,
  // Tube Lab "Real Feel" switches (1.33.0, see PowerAmp.h and TubeCircuit.h): power-tube screen
  // squish, the preamp on the power amp's supply, output transformer saturation, voice coil heating.
  // Off by default and in every preset, so older sounds are unchanged.
  kTubeScreenSquish,
  kTubeSharedSupply,
  kTubeOTSaturation,
  kTubeCoilHeat,
  kTubeFeelDepth, // 1.33.1: how strongly the Real Feel switches act (1x = real parts .. 10x)
  // REAL FEEL pedal (1.34.0, grid id chaingrid::kRealFeel, RealFeel.h): in the chain or not. It has
  // no other params of its own: its On switch is the Tube Lab's (kTubeEngaged, a tandem pedal) and
  // its controls are the Tube Lab's Real Feel switches and Depth.
  kRealFeelActive,
  // REAL FEEL's grid params (see kNumGridSlots32), named GridCell32 / GridIn32 like the others.
  kGridCell32,
  kGridIn32,
  // Tube Lab Economy (1.35.0): half the oversampling for about half the CPU (TubeStage.h).
  kTubeEconomy,
  // CAB 2 and CAB 3 (1.36.0, grid ids chaingrid::kCab2 / kCab3): extra cab IR blocks, each with its
  // own IR (loaded by path, see mExtraCabIR), Level and Pan, for running several cabs in parallel or
  // in series.
  kCab2Active,
  kCab2Engaged,
  kCab2Level,
  kCab2Pan,
  kCab3Active,
  kCab3Engaged,
  kCab3Level,
  kCab3Pan,
  kGridCell33,
  kGridIn33,
  kGridCell34,
  kGridIn34,
  // Output limiter (1.37.0, OutputLimiter.h): a lookahead peak limiter at -1 dBFS before the
  // safety soft clip, so the master can go up without the pick transients clipping.
  kOutputLimiter,
  // Tube Lab Touch (1.38.0, TubeStage.h): softer picks also get less drive, so they clean up.
  kTubeTouch,
  // 60 Hz (1.39.0, MainsHum.h): the input's Hum Filter (60 Hz and harmonics notched out), and the
  // Tube Lab's supply ripple + hum switch and its Amount.
  kHumFilter,
  kTubeRipple,
  kTubeRippleAmount,
  // REAL FEEL pedal dry/wet (1.39.0).
  kRealFeelMix,
  // The Tube Lab 60 Hz grind's own dry/wet (1.40.0).
  kTubeRippleMix,
  // 1.44.0 pedals (NewPedals.h; grid ids chaingrid::kSkyVerb .. kApexPreamp), and their grid params.
  kSkyActive,
  kSkyEngaged,
  kSkyMode, // Hall / Plate / Room / Shimmer / Cloud / Bloom
  kSkyDecay,
  kSkyPredelay,
  kSkyTone,
  kSkyMod,
  kSkyMix,
  kSkyFreeze,
  kFDActive,
  kFDEngaged,
  kFDDoubler, // Off: flanger; on: doubler
  kFDManual,
  kFDWidth,
  kFDSpeed,
  kFDRegen,
  kFDMix,
  kWahActive,
  kWahEngaged,
  kWahModel, // Cry Baby / V847 / Auto
  kWahPosition,
  kWahQ,
  kWahSens,
  kWahLevel,
  kPDActive,
  kPDEngaged,
  kPDVolume,
  kPDDrive,
  kPDAttack,
  kPDGate,
  kPDBright,
  kApexActive,
  kApexEngaged,
  kApexGain,
  kApexBass,
  kApexMid,
  kApexTreble,
  kApexPresence,
  kApexVolume,
  kApexBright,
  kGridCell35,
  kGridIn35,
  kGridCell36,
  kGridIn36,
  kGridCell37,
  kGridIn37,
  kGridCell38,
  kGridIn38,
  kGridCell39,
  kGridIn39,
  // Tube Lab Circuit+ (1.45.0): a cathode follower before the tone stack (TubeCircuit.h), the power
  // amp's cathode bias, feedback from the speaker and Resonance (PowerAmp.h); and 8x oversampling.
  kTubeCathodeFollower,
  kTubeCathodeBias,
  kTubeSpeakerFeedback,
  kTubeResonance,
  kTubeHighQuality,
  // PRO EQ (1.46.0, ProEQ.h): the block (grid id chaingrid::kProEq) and its grid params, then the
  // Output EQ at the end of the chain. Each EQ's bands are 8 x {On, Freq, Gain, Q} (see EqBandParam).
  kProEqActive,
  kProEqEngaged,
  kProEqOutput,
  kProEqBandFirst,
  kProEqBandLast = kProEqBandFirst + 8 * 4 - 1,
  kGridCell40,
  kGridIn40,
  kOutEqOn,
  kOutEqOutput,
  kOutEqBandFirst,
  kOutEqBandLast = kOutEqBandFirst + 8 * 4 - 1,
  // 1.47.0: each EQ's input Trim and each band's Type and Slope (see EqBandExtParam).
  kProEqTrim,
  kOutEqTrim,
  kProEqBandExtFirst,
  kProEqBandExtLast = kProEqBandExtFirst + 8 * 2 - 1,
  kOutEqBandExtFirst,
  kOutEqBandExtLast = kOutEqBandExtFirst + 8 * 2 - 1,
  // GATE and PRO COMP (1.52.0, Dynamics.h): the blocks (grid ids chaingrid::kGate / kStudioComp), then
  // their grid params.
  kGateActive,
  kGateEngaged,
  kGateThreshold,
  kGateHysteresis,
  kGateAttack,
  kGateHold,
  kGateRelease,
  kGateRange,
  kGateKey,
  kGateKeyHpf,
  kSCompActive,
  kSCompEngaged,
  kSCompThreshold,
  kSCompRatio,
  kSCompAttack,
  kSCompRelease,
  kSCompKnee,
  kSCompMakeup,
  kSCompAutoMakeup,
  kSCompMix,
  kSCompStyle,
  kSCompScHpf,
  kGridCell41,
  kGridIn41,
  kGridCell42,
  kGridIn42,
  kOutputMono, // 1.55.0: both outputs carry the L+R mix. 1.58.0: replaced by Width (loads as Width 0%)
  // Tube Lab bench (1.58.0, TubeBench.h; on the debug page): the Variac (V), the power tubes' bias pot
  // (% of stock idle current) and the preamp's and power tubes' age (%).
  kTubeVariac,
  kTubeBiasPot,
  kTubePreampAge,
  kTubePowerAge,
  // OUTPUT page (1.58.0): the limiter's Ceiling (dBFS) and Release (ms), and the output's stereo
  // Width and Widen (StereoWidth.h).
  kLimiterCeiling,
  kLimiterRelease,
  kOutputWidth,
  kOutputWiden,
  // SPEAKER LAB (1.59.0, CabLab.h): CAB LAB's 3D placement mode, the cab's format, and four mics of
  // {On, Type, X, Y, Z, Level, Pan, Invert} each (see LabMicParam).
  kCabLab3D,
  kCabFormat,
  kLabMicFirst,
  kLabMicLast = kLabMicFirst + 4 * 8 - 1,
  // Dual cabs (1.61.0): Single / Dual, cab 2's format, speaker and source (its own IR, see
  // mCabLabIR2), and which cab each mic is on.
  kCabDual,
  kCab2Format,
  kCab2Speaker,
  kCab2SourceIR,
  kLabMicCabFirst,
  kLabMicCabLast = kLabMicCabFirst + 3,
  // 1.62.0: the Tube Lab's power tubes can fail (red-plating wears them out; TubeStage.h).
  kTubeCanFail,
  // 1.64.0: the AMP block's capture as the Tube Lab's preamp (the AMP block then steps aside).
  kTubeCapture,
  // 1.66.0 pedals (MorePedals.h; grid ids chaingrid::kTrackGate .. kSpringVerb), then their grid params.
  kTGActive,
  kTGEngaged,
  kTGModel, // Tracking / Classic / Smart
  kTGThreshold,
  kTGRelease,
  kTGHold,
  kTGRange,
  kTGRatio,
  kTGNoise, // Smart: Hiss / Mid / Full
  kTGKey,
  kDigiActive,
  kDigiEngaged,
  kDigiMode, // Standard / Mod / Warm / Reverse
  kDigiTime,
  kDigiFeedback,
  kDigiLevel,
  kAnaActive,
  kAnaEngaged,
  kAnaTime,
  kAnaRegen,
  kAnaMix,
  kAnaMod,
  kAnaRate,
  kAnaWidth,
  kTapeActive,
  kTapeEngaged,
  kTapeHeads,
  kTapeRate,
  kTapeIntensity,
  kTapeEcho,
  kTapeReverb,
  kTapeWow,
  kTapeDrive,
  kSprActive,
  kSprEngaged,
  kSprDwell,
  kSprTone,
  kSprMix,
  kSprDecay,
  kGridCell43,
  kGridIn43,
  kGridCell44,
  kGridIn44,
  kGridCell45,
  kGridIn45,
  kGridCell46,
  kGridIn46,
  kGridCell47,
  kGridIn47,
  // TUBE LAB 2 (1.67.0, grid id chaingrid::kTubeLab2): its Active (on the grid), then a copy of each
  // of the Tube Lab's sound params, in kTubeLabParams' order (see LabParam), then its grid params.
  kLab2Active,
  kLab2First,
  kLab2Last = kLab2First + 52 - 1,
  kGridCell48,
  kGridIn48,
  // 1.68.0: the chassis: its two preamp sockets V5, V6 (TubeStage.h SocketParams), kSocketFields each (see
  // SocketParam), and the quad switch (four power tubes); TUBE LAB's, then TUBE LAB 2's copies in the same
  // order (see LabParam).
  kTubeSockFirst,
  kTubeSockLast = kTubeSockFirst + 2 * 7 - 1,
  kTubeQuad,
  kLab2SockFirst,
  kLab2SockLast = kLab2SockFirst + 2 * 7 - 1,
  kLab2Quad,
  // 1.71.0: tap tempo. The tempo, then each delay's note value for it (Off = taps leave it alone; see
  // _PushTempoToDelays): DELAY, DELAY 2, DELAY 3, DIGI DELAY, ANALOG DLY, TAPE ECHO.
  kTempoBPM,
  kDelayTapDiv,
  kDelay2TapDiv,
  kDelay3TapDiv,
  kDigiTapDiv,
  kAnaTapDiv,
  kTapeTapDiv,
  kNumParams
};

// A chassis socket's param (1.68.0): socket k (0, 1 = V5, V6), field one of these (TUBE LAB's; LabParam
// gives TUBE LAB 2's).
enum SocketField
{
  kSockRole = 0, // Off / Gain / Follower / Boost
  kSockTube, // 12AX7 / 12AT7 / 12AU7 / 5751
  kSockGain,
  kSockBias,
  kSockCap,
  kSockPlateCap,
  kSockCouple,
  kSocketFields
};
constexpr int SocketParam(int k, int field) { return kTubeSockFirst + k * kSocketFields + field; }
static_assert(kTubeSockLast - kTubeSockFirst + 1 == 2 * kSocketFields, "2 sockets x kSocketFields");
// The chassis's own params, TUBE LAB's and TUBE LAB 2's (each block the same length, in the same order).
constexpr int kTubeChassisFirst = kTubeSockFirst, kTubeChassisLast = kTubeQuad;
constexpr int kLab2ChassisFirst = kLab2SockFirst, kLab2ChassisLast = kLab2Quad;
static_assert(kLab2ChassisLast - kLab2ChassisFirst == kTubeChassisLast - kTubeChassisFirst, "same layout");

// An EQ band's param: first = kProEqBandFirst or kOutEqBandFirst, field 0 On, 1 Freq, 2 Gain, 3 Q.
constexpr int EqBandParam(int first, int band, int field) { return first + band * 4 + field; }
// A band's Type (field 0) or Slope (field 1) param: first = kProEqBandExtFirst or kOutEqBandExtFirst.
constexpr int EqBandExtParam(int first, int band, int field) { return first + band * 2 + field; }

// A grid block's cell and input-cable params, by block id (not Input).
constexpr int kLateGridCell[] = {kGridCell32, kGridCell33, kGridCell34, kGridCell35,
                                 kGridCell36, kGridCell37, kGridCell38, kGridCell39, kGridCell40,
                                 kGridCell41, kGridCell42, kGridCell43, kGridCell44, kGridCell45,
                                 kGridCell46, kGridCell47, kGridCell48};
constexpr int kLateGridIn[] = {kGridIn32, kGridIn33, kGridIn34, kGridIn35, kGridIn36,
                               kGridIn37, kGridIn38, kGridIn39, kGridIn40, kGridIn41, kGridIn42,
                               kGridIn43, kGridIn44, kGridIn45, kGridIn46, kGridIn47, kGridIn48};
// The 1.44.0 pedals' params, first to last (older states load them at their defaults).
constexpr int kNewPedalParamsFirst = kSkyActive, kNewPedalParamsLast = kApexBright;
// The 1.52.0 GATE and PRO COMP params, first to last (older states load them at their defaults).
constexpr int kDynamicsParamsFirst = kGateActive, kDynamicsParamsLast = kSCompScHpf;
// The 1.66.0 pedals' params, first to last (older states load them at their defaults).
constexpr int kMorePedalParamsFirst = kTGActive, kMorePedalParamsLast = kSprDecay;
// The 1.71.0 tap tempo params, first to last (older states load them at their defaults).
constexpr int kTempoParamsFirst = kTempoBPM, kTempoParamsLast = kTapeTapDiv;
// 1.67.0: every Tube Lab param TUBE LAB 2 has its own copy of (all but Active and the old Slot). LAB 2's
// copy of kTubeLabParams[i] is kLab2First + i.
constexpr int kTubeLabParams[] = {kTubeEngaged, kTubeDrive, kTubeBias, kTubeBloom, kTubeBright,
                                  kTubeMix, kTubeOutput, kTubeCircuit, kTubeCathodeCap, kTubePlate,
                                  kTubePlateCap, kTubeSagOn, kTubeSagSupply, kTubeSagTime, kTubeStage2,
                                  kTubeGain2, kTubeBias2, kTubeCap2, kTubePlateCap2, kTubeCouple,
                                  kTubePower, kTubeMaster, kTubePowerSag, kTubeFeedback, kTubePresence,
                                  kTube6L6, kTubeSpeakerLoad, kTubeStack, kTubeBass, kTubeMid,
                                  kTubeTreble, kTubeScreenSquish, kTubeSharedSupply, kTubeOTSaturation, kTubeCoilHeat,
                                  kTubeFeelDepth, kTubeEconomy, kTubeTouch, kTubeRipple, kTubeRippleAmount,
                                  kTubeRippleMix, kTubeCathodeFollower, kTubeCathodeBias, kTubeSpeakerFeedback, kTubeResonance,
                                  kTubeHighQuality, kTubeVariac, kTubeBiasPot, kTubePreampAge, kTubePowerAge,
                                  kTubeCanFail, kTubeCapture};
constexpr int kNumLabParams = (int)(sizeof(kTubeLabParams) / sizeof(int));
static_assert(kNumLabParams == kLab2Last - kLab2First + 1, "TUBE LAB 2 copies every kTubeLabParams entry");
// A Tube Lab param of lab 0 (TUBE LAB) or lab 1 (TUBE LAB 2). Any other param is its own.
constexpr int LabParam(int lab, int p)
{
  if (lab == 0)
    return p;
  if (p >= kTubeChassisFirst && p <= kTubeChassisLast) // 1.68.0: the chassis's
    return kLab2ChassisFirst + (p - kTubeChassisFirst);
  for (int i = 0; i < kNumLabParams; i++)
    if (kTubeLabParams[i] == p)
      return kLab2First + i;
  return p;
}
// TUBE LAB's param that a TUBE LAB 2 param copies, or -1.
constexpr int Lab2Base(int p)
{
  if (p >= kLab2ChassisFirst && p <= kLab2ChassisLast)
    return kTubeChassisFirst + (p - kLab2ChassisFirst);
  return p >= kLab2First && p <= kLab2Last ? kTubeLabParams[p - kLab2First] : -1;
}
static_assert(sizeof(kLateGridCell) / sizeof(int) == chaingrid::kNumParamSlots - kNumGridSlots32,
              "every id past 31 needs its grid params");
constexpr int GridCellParam(int id)
{
  const int slot = chaingrid::ParamSlot(id);
  return slot < kNumGridSlots32 ? kGridCell0 + slot : kLateGridCell[slot - kNumGridSlots32];
}
constexpr int GridInParam(int id)
{
  const int slot = chaingrid::ParamSlot(id);
  return slot < kNumGridSlots32 ? kGridIn0 + slot : kLateGridIn[slot - kNumGridSlots32];
}
// The extra cab blocks, as indices 0, 1 into mExtraCabIR etc.
constexpr int kNumExtraCabs = 2;
constexpr int ExtraCabIndex(int id) { return id == chaingrid::kCab2 ? 0 : id == chaingrid::kCab3 ? 1 : -1; }
static_assert(GridCellParam(chaingrid::kPhaser) == kGridIn0 - 1, "ids up to 31 keep their grid params");

// SerializeState writes params [0, kNumPositionalParams) positionally (as every 1.x state on disk
// already has them), and every param after that by name in its own "###ExtraParamsV1###" section,
// so states saved before a param existed still line up (and older builds just ignore the section).
constexpr int kNumPositionalParams = kTubeActive;

// The Tube Lab's Circuit+ controls (1.45.0; presets and old states turn them off).
constexpr int kTubeCircuitPlusParams[] = {kTubeCathodeFollower, kTubeCathodeBias, kTubeSpeakerFeedback,
                                          kTubeResonance};

// The SPEAKER LAB page's shared state (SpeakerLabControls.h): reading the params, moving a mic, the
// selection and the 3D camera.
namespace speakerlab
{
struct Model
{
  std::function<cab::CabLabParams()> params;
  std::function<void(int mic, double x, double y, double z)> setMic; // metres
  std::function<void(int mic)> select;
  std::function<void(int cab)> selectCab; // 1.61.0
  int* selected = nullptr;
  int* selectedCab = nullptr; // 1.61.0
  float* yaw = nullptr;
  float* pitch = nullptr;
  std::function<void()> redrawViews; // After an orbit (no param changes)
};
} // namespace speakerlab

// A SPEAKER LAB mic's param: mic 0..3, field 0 On, 1 Type, 2 X, 3 Y, 4 Z (cm), 5 Level, 6 Pan, 7 Invert.
constexpr int kNumLabMicFields = 8;
constexpr int LabMicParam(int mic, int field) { return kLabMicFirst + mic * kNumLabMicFields + field; }

// The Tube Lab bench (1.58.0): older states load it at stock, and (since 1.63.0) so do Tube Lab presets:
// they were voiced on a stock bench.
constexpr int kTubeBenchParams[] = {kTubeVariac, kTubeBiasPot, kTubePreampAge, kTubePowerAge};
// The OUTPUT page's 1.58.0 controls (older states load them at their defaults).
constexpr int kOutputPageParams[] = {kLimiterCeiling, kLimiterRelease, kOutputWidth, kOutputWiden};

// The Tube Lab's Real Feel switches (presets and old states turn them off).
constexpr int kTubeRealFeelParams[] = {kTubeScreenSquish, kTubeSharedSupply, kTubeOTSaturation, kTubeCoilHeat,
                                       kTubeRipple};

// 20 total chain blocks: room for AMP, CAB, and every pedal to be freely repositioned. This is
// just an integer range and grid layout math -- there's no real technical ceiling on slot count;
// grow it further whenever more room is actually needed.
constexpr int kPedalSlotMin = -9;
constexpr int kPedalSlotMax = 10;
constexpr int kNumChainSlots = kPedalSlotMax - kPedalSlotMin + 1;
// The rack wraps into this many rows once a single row would make every chip too cramped to
// read/hit reliably -- 20 slots in one row was the trigger for this.
constexpr int kChainRows = 2;
constexpr int kSlotsPerChainRow = (kNumChainSlots + kChainRows - 1) / kChainRows;
// Built-in pedals' (and CAB's) own Slot params stop one short of kPedalSlotMax -- not because
// anything's still reserved there (AMP and CAB both became freely draggable, so nothing is pinned
// to a fixed slot anymore), but to keep the same conservative param range that was already in use,
// since nothing forces revisiting it now that the reservation it was protecting is gone.
constexpr int kMaxDraggablePedalSlot = kPedalSlotMax - 1;
// AMP is draggable too, but only within [0, kMaxDraggablePedalSlot] -- never negative. This
// guarantees AMP's own slot is always strictly greater than any NAM-capture pedal's slot (which
// tops out at kNamPedalSlotMax = -1, see below), so the mono/stereo split in ProcessBlock (drawn at
// "<= AMP's slot" vs. "> AMP's slot") never accidentally strands a NAM pedal -- which is hard-coded
// mono internally -- on the stereo side, regardless of where AMP gets dragged to.
constexpr int kAmpSlotMin = 0;
constexpr int kAmpSlotMax = kMaxDraggablePedalSlot;
// _SelectBlock's blockId sentinel for "the CAB block's popup is open" (pedals use 0..kNumPedals-1,
// -1 means no popup open).
constexpr int kCabBlockId = -2;
// _SelectBlock's blockId sentinel for "the AMP block's popup is open". Was -3 as a bare literal
// on the AMP chip's own construction (chip IDs just need to be distinct from pedal indices and
// kCabBlockId); named now that it also drives the popup, same as kCabBlockId does for CAB.
constexpr int kAmpBlockId = -3;
// _SelectBlock's blockId sentinel for "the FX loop's popup is open" (either LOOP chip opens it;
// -4 and -5 are those chips' own ids).
constexpr int kLoopBlockId = -6;
// The Amp popup is user-resizable (see mAmpPopupWidth/Height, NAMResizeGripControl) -- these are
// just the floor so a drag can't shrink it down to something the knobs can't fit in. The ceiling
// is always "whatever's left of the current window", computed fresh each time since the window
// itself can be resized independently (host corner-resizer).
constexpr float kAmpPopupMinWidth = 360.f;
constexpr float kAmpPopupMinHeight = 200.f;
// NAM pedals are mono-only, so they're restricted to the pre-amp side of the chain -- and since
// kNamPedalSlotMax (-1) is always < kAmpSlotMin (0), they can never end up on the wrong side of
// AMP's own (now-draggable) position.
constexpr int kNamPedalSlotMin = kPedalSlotMin;
constexpr int kNamPedalSlotMax = -1;
constexpr int kNumBuiltInPedals = 9;
// The Tube Lab and CAB LAB are the last built-in pedals; the NAM-file pedals follow at
// kNumBuiltInPedals + 0/1.
constexpr int kTubePedalIdx = 7;
constexpr int kCabLabPedalIdx = 8;
constexpr int kNumNamPedals = 2;
constexpr int kNumPedals = kNumBuiltInPedals + kNumNamPedals;
// DSP-only sentinels identifying CAB and the FX Loop Send/Return blocks within ProcessBlock's
// unified pedalIdx dispatch (applyPedal) -- distinct from kCabBlockId, which is a UI-only sentinel
// for _SelectBlock's popup selection (Loop Send/Return have no popup, so need no such sentinel).
constexpr int kCabPedalIdx = kNumPedals;
constexpr int kLoopSendPedalIdx = kNumPedals + 1;
constexpr int kLoopReturnPedalIdx = kNumPedals + 2;
// The chain grid's block ids line up with the pedal indices and the DSP sentinels above.
static_assert(chaingrid::kNumPedalBlocks == kNumPedals, "grid block ids must match kPedals");
static_assert(chaingrid::kCab == kCabPedalIdx && chaingrid::kLoopSend == kLoopSendPedalIdx
                && chaingrid::kLoopReturn == kLoopReturnPedalIdx,
              "grid block ids must match the DSP sentinels");
// Loop Send/Return read/write the aux I/O bus (see PLUG_CHANNEL_IO) at whatever index the main
// bus's channel count ends at -- i.e. right after the main stereo pair.
constexpr int kLoopBusChannelOffset = 2;

const int numKnobs = 6;

enum ECtrlTags
{
  kCtrlTagModelFileBrowser = 0,
  kCtrlTagIRFileBrowser,
  kCtrlTagInputMeter,
  kCtrlTagOutputMeter,
  kCtrlTagInputMeterLabel,
  kCtrlTagOutputMeterLabel,
  kCtrlTagSettingsBox,
  kCtrlTagOutputMode,
  kCtrlTagCalibrateInput,
  kCtrlTagInputCalibrationLevel,
  kCtrlTagSlimmableIcon,
  kCtrlTagSlimOverlayBackdrop,
  kCtrlTagSlimKnob,
  kCtrlTagTunerBox,
  kCtrlTagTunerDisplay,
  kCtrlTagPedalboardBox,
  kCtrlTagIRFileBrowserR,
  kCtrlTagTubeCurve,
  kCtrlTagCabLabIRName,
  kCtrlTagTubeDebug,
  kCtrlTagOutEqPage, // 1.46.0: the Output EQ page
  kCtrlTagOutputPage, // 1.58.0: the OUTPUT page and its meters
  kCtrlTagSpeakerLab, // 1.59.0: the SPEAKER LAB page
  kCtrlTagPedalDrawer, // 1.60.0: the PEDALS drawer's panel
  kCtrlTagCabLabIR2Name, // 1.61.0: CAB LAB cab 2's IR name (SPEAKER LAB page)
  kCtrlTagCabLabIR1Name, // ...and cab 1's, on the same page
  kCtrlTagTubeChassis, // 1.62.0: the PREAMP CHASSIS page
  kCtrlTagLab2CaptureName, // 1.67.0: TUBE LAB 2's capture file name
  kCtrlTagLimiterGR,
  kCtrlTagOutCorrelation,
  kCtrlTagCab2IRName, // 1.36.0: CAB 2 / CAB 3's loaded IR names
  kCtrlTagCab3IRName,
  kCtrlTagTuneButton, // 1.43.0: MIDI-learnable (the tuner page)
  kCtrlTagMidiLearn, // 1.43.0: the MIDI learn overlay
  kCtrlTagTapButton, // 1.71.0: MIDI-learnable (tap tempo)
  kNumCtrlTags
};

enum EMsgTags
{
  // These tags are used from UI -> DSP
  kMsgTagClearModel = 0,
  kMsgTagClearIR,
  kMsgTagClearIR_R,
  kMsgTagHighlightColor,
  // The following tags are from DSP -> UI
  kMsgTagLoadFailed,
  kMsgTagLoadedModel,
  kMsgTagLoadedIR,
  kMsgTagClearedDisplay,
  kMsgTagTunerUpdate,
  kNumMsgTags
};

// Number of A/B amp slots
constexpr int kNumABSlots = 2;

// Get the sample rate of a NAM model.
// Sometimes, the model doesn't know its own sample rate; this wrapper guesses 48k based on the way that most
// people have used NAM in the past.
double GetNAMSampleRate(const std::unique_ptr<nam::DSP>& model)
{
  // Some models are from when we didn't have sample rate in the model.
  // For those, this wraps with the assumption that they're 48k models, which is probably true.
  const double assumedSampleRate = 48000.0;
  const double reportedEncapsulatedSampleRate = model->GetExpectedSampleRate();
  const double encapsulatedSampleRate =
    reportedEncapsulatedSampleRate <= 0.0 ? assumedSampleRate : reportedEncapsulatedSampleRate;
  return encapsulatedSampleRate;
};

class ResamplingNAM : public nam::DSP
{
public:
  // Resampling wrapper around the NAM models
  ResamplingNAM(std::unique_ptr<nam::DSP> encapsulated, const double expected_sample_rate)
  : nam::DSP(encapsulated->NumInputChannels(), encapsulated->NumOutputChannels(), expected_sample_rate)
  , mEncapsulated(std::move(encapsulated))
  , mResampler(GetNAMSampleRate(mEncapsulated))
  {
    // Assign the encapsulated object's processing function  to this object's member so that the resampler can use it:
    auto ProcessBlockFunc = [&](NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames) {
      mEncapsulated->process(input, output, numFrames);
    };
    mBlockProcessFunc = ProcessBlockFunc;

    // Get the other information from the encapsulated NAM so that we can tell the outside world about what we're
    // holding.
    if (mEncapsulated->HasLoudness())
    {
      SetLoudness(mEncapsulated->GetLoudness());
    }
    if (mEncapsulated->HasInputLevel())
    {
      SetInputLevel(mEncapsulated->GetInputLevel());
    }
    if (mEncapsulated->HasOutputLevel())
    {
      SetOutputLevel(mEncapsulated->GetOutputLevel());
    }

    // NOTE: prewarm samples doesn't mean anything--we can prewarm the encapsulated model as it likes and be good to
    // go.
    // _prewarm_samples = 0;

    // And be ready
    int maxBlockSize = 2048; // Conservative
    Reset(expected_sample_rate, maxBlockSize);
  };

  ~ResamplingNAM() = default;

  void prewarm() override { mEncapsulated->prewarm(); };

  void process(NAM_SAMPLE** input, NAM_SAMPLE** output, const int num_frames) override
  {
    if (num_frames > mMaxExternalBlockSize)
      // We can afford to be careful
      throw std::runtime_error("More frames were provided than the max expected!");

    if (!NeedToResample())
    {
      mEncapsulated->process(input, output, num_frames);
    }
    else
    {
      mResampler.ProcessBlock(input, output, num_frames, mBlockProcessFunc);
    }
  };

  int GetLatency() const { return NeedToResample() ? mResampler.GetLatency() : 0; };

  void Reset(const double sampleRate, const int maxBlockSize) override
  {
    mExpectedSampleRate = sampleRate;
    mMaxExternalBlockSize = maxBlockSize;
    mResampler.Reset(sampleRate, maxBlockSize);

    // Allocations in the encapsulated model (HACK)
    // Stolen some code from the resampler; it'd be nice to have these exposed as methods? :)
    const double mUpRatio = sampleRate / GetEncapsulatedSampleRate();
    const auto maxEncapsulatedBlockSize = static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) / mUpRatio));
    mEncapsulated->ResetAndPrewarm(sampleRate, maxEncapsulatedBlockSize);
  };

  // So that we can let the world know if we're resampling (useful for debugging)
  double GetEncapsulatedSampleRate() const { return GetNAMSampleRate(mEncapsulated); };

  nam::SlimmableModel* GetSlimmableModel() { return dynamic_cast<nam::SlimmableModel*>(mEncapsulated.get()); }
  const nam::SlimmableModel* GetSlimmableModel() const
  {
    return dynamic_cast<const nam::SlimmableModel*>(mEncapsulated.get());
  }

private:
  bool NeedToResample() const { return GetExpectedSampleRate() != GetEncapsulatedSampleRate(); };
  // The encapsulated NAM
  std::unique_ptr<nam::DSP> mEncapsulated;

  // The resampling wrapper
  dsp::ResamplingContainer<NAM_SAMPLE, 1, 12> mResampler;

  // Used to check that we don't get too large a block to process.
  int mMaxExternalBlockSize = 0;

  // This function is defined to conform to the interface expected by the iPlug2 resampler.
  std::function<void(NAM_SAMPLE**, NAM_SAMPLE**, int)> mBlockProcessFunc;
};

class NeuralAmpModeler final : public iplug::Plugin
{
public:
  NeuralAmpModeler(const iplug::InstanceInfo& info);
  ~NeuralAmpModeler();

  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const iplug::IMidiMsg& msg) override;
  void OnReset() override;
  void OnIdle() override;

  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;
  void OnUIOpen() override;
  void LayoutUI(iplug::igraphics::IGraphics* pGraphics) override;
  // The standalone window was resized from outside the UI (maximize, restore, dragging its frame):
  // follow it, at the current scale. iPlug2's default ignores this, which left the UI at its old
  // size inside a maximized window with a white strip beside it. (Resize relayouts, since the
  // corner resizer is attached with layoutOnResize; needsPlatformResize = false avoids feedback.)
  void OnParentWindowResize(int width, int height) override
  {
    if (auto* pGraphics = GetUI())
    {
      const float scale = pGraphics->GetDrawScale();
      NAMDragLog("parent resize %d x %d (ui %d x %d, draw %.3f, screen %.3f)", width, height, pGraphics->Width(),
                 pGraphics->Height(), scale, pGraphics->GetScreenScale());
      pGraphics->Resize(static_cast<int>(width / scale), static_cast<int>(height / scale), scale, false);
    }
  }
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override { return true; }

  void OnParamChange(int paramIdx) override;
  void OnParamChangeUI(int paramIdx, iplug::EParamSource source) override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

  // The loaded .nam file's name for a NAM pedal slot (0 or 1), or nullptr if none is loaded.
  // Used so the pedalboard/Quick FX chip can actually show which capture is in that slot, instead
  // of just a generic "NAM 1"/"NAM 2" label regardless of what's loaded.
  const char* GetNamPedalFileName(int slot) const
  {
    if (slot < 0 || slot >= kNumNamPedals || mNamPedalPath[slot].GetLength() == 0)
      return nullptr;
    return mNamPedalPath[slot].get_filepart();
  }

private:
#ifdef APP_API
  // Standalone-only persistence: a DAW build gets its state saved/restored by the host's project
  // file, so none of this applies there -- but the bare standalone .exe has no host to do that for
  // it, and previously just lost every knob/pedal/EQ setting on close. Autosave (throttled, so it
  // doesn't touch disk on every single knob tick) plus explicit named Save/Load Preset both reuse
  // the same SerializeState/UnserializeState this plugin already uses for DAW project save/load.
  void _GetAutosavePath(WDL_String& path) const;
  void _GetPresetsDir(WDL_String& path) const;
  void _SaveStateToFile(const char* path) const;
  bool _LoadStateFromFile(const char* path);
  // Called every OnIdle tick; actually writes to disk only if mStateDirty and enough idle ticks
  // have passed since the last write.
  void _AutosaveIfDirty();

  bool mStateDirty = true; // true at launch so a fresh install/first run gets an initial autosave.
  int mIdleTicksSinceDirty = 0;
#endif

  // Allocates mInputPointers and mOutputPointers
  void _AllocateIOPointers(const size_t nChans);
  // Moves DSP modules from staging area to the main area.
  // Also deletes DSP modules that are flagged for removal.
  // Exists so that we don't try to use a DSP module that's only
  // partially-instantiated.
  void _ApplyDSPStaging();
  // Deallocates mInputPointers and mOutputPointers
  void _DeallocateIOPointers();
  // Fallback that just copies inputs to outputs if mDSP doesn't hold a model.
  void _FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels, const size_t numFrames);
  // Sizes based on mInputArray
  size_t _GetBufferNumChannels() const;
  size_t _GetBufferNumFrames() const;
  void _InitToneStack();
  // Loads a NAM model and stores it to mStagedNAM
  // Returns an empty string on success, or an error message on failure.
  std::string _StageModel(const WDL_String& dspFile);
  // Loads an IR and stores it to mStagedIR.
  // Return status code so that error messages can be relayed if
  // it wasn't successful.
  dsp::wav::LoadReturnCode _StageIR(const WDL_String& irPath);
  // Same, for the optional right-channel IR (stereo cab). If this is never loaded, the right
  // channel just reuses the (left) mIR -- i.e. a mono cab, the existing default behavior.
  dsp::wav::LoadReturnCode _StageIR_R(const WDL_String& irPath);

  bool _HaveModel() const { return this->mModel != nullptr; };
  // Prepare the input & output buffers
  void _PrepareBuffers(const size_t numChannels, const size_t numFrames);
  // Manage pointers
  void _PrepareIOPointers(const size_t nChans);
  // Copy the input buffer to the object, applying input level.
  // :param nChansIn: In from external
  // :param nChansOut: Out to the internal of the DSP routine
  void _ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn, const size_t nChansOut);
  // Copy the output to the output buffer, applying output level.
  // :param nChansIn: In from internal
  // :param nChansOut: Out to external
  void _ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames, const size_t nChansIn,
                      const size_t nChansOut);
  // Resetting for models and IRs, called by OnReset
  void _ResetModelAndIR(const double sampleRate, const int maxBlockSize);

  void _SetInputGain();
  void _SetOutputGain();
  void _ApplySlimParamToLoadedNAMs();
  // Swaps the live model/IR with whatever is stashed in the given A/B slot (0 or 1).
  // Safe to call from the audio thread (mirrors the existing staging pattern: moves unique_ptrs, no locks).
  void _SwitchABSlot(int newSlot);

  // NAM-file pedals ============================================
  // Loads a downloaded .nam capture into a pedal slot (mirrors _StageModel, but per-slot).
  // Returns an empty string on success, or an error message on failure.
  std::string _StageNamPedal(int slot, const WDL_String& modelPath);
  // Flags a NAM pedal slot's model for removal at a safe time (mirrors kMsgTagClearModel).
  void _RemoveNamPedal(int slot);
  // Moves staged NAM pedal models into place / removes flagged ones. Called from _ApplyDSPStaging.
  void _ApplyNamPedalDSPStaging();
  // Resets/prewarms all loaded (or staged) NAM pedal models. Called from OnReset.
  void _ResetNamPedals(double sampleRate, int maxBlockSize);
  // Sum of added latency from all loaded NAM pedal models.
  int _NamPedalLatency() const;

  // See: Unserialization.cpp
  void _UnserializeApplyConfig(nlohmann::json& config);
  // 0.7.9 and later
  int _UnserializeStateWithKnownVersion(const iplug::IByteChunk& chunk, int startPos);
  // Hopefully 0.7.3-0.7.8, but no gurantees
  int _UnserializeStateWithUnknownVersion(const iplug::IByteChunk& chunk, int startPos);

  // Update all controls that depend on a model
  void _UpdateControlsFromModel();
  // Tells the model and IR pickers what's loaded (after the editor opens, and after every relayout).
  void _SendLoadedFileNamesToUI();

  // Make sure that the latency is reported correctly.
  void _UpdateLatency();

  // Update level meters
  // Called within ProcessBlock().
  // Assume _ProcessInput() and _ProcessOutput() were run immediately before.
  void _UpdateMeters(iplug::sample** inputPointer, iplug::sample** outputPointer, const size_t nFrames,
                     const size_t nChansIn, const size_t nChansOut);

  // Member data

  // Input arrays to NAM
  std::vector<std::vector<iplug::sample>> mInputArray;
  // Output from NAM
  std::vector<std::vector<iplug::sample>> mOutputArray;
  // Pointer versions
  iplug::sample** mInputPointers = nullptr;
  iplug::sample** mOutputPointers = nullptr;

  // Stereo working buffers: the chain is mono through the amp model + tone stack (NAM models are
  // mono-only), then splits into true stereo here for the effects-loop pedals, cab IR, and
  // post-amp tone shaping, so Chorus/Delay width and a dual-mic stereo IR can actually be heard.
  std::vector<iplug::sample> mStereoBufferL;
  std::vector<iplug::sample> mStereoBufferR;
  iplug::sample* mStereoChannels[2] = {nullptr, nullptr};

  // Input and output gain
  double mInputGain = 1.0;
  double mOutputGain = 1.0;

  // MIDI mappings (1.43.0: MIDI learn; the fixed Rig Kontrol 3 map of 1.29.0 is now just the
  // default mappings). Each CC drives a list of targets: a param, the tuner page, or the volume
  // pedal. ProcessMidiMsg (audio thread) only counts messages per CC and keeps the last value (and
  // drives the volume pedal); OnIdle applies them (_ApplyMidiMappings) on the main thread, where
  // params and the GUI can be touched. Saved for the whole app in midimap.txt (see _MidiMapPath).
  //  - A switch target (a bool param, or a two-way one like A/B) toggles on every message: a latching
  //    footswitch sends 127, then 0. Several switches on one CC toggle as a group: if any of them
  //    (in the chain) is on, all go off, else all come on -- so they can't get out of step.
  //  - Anything else follows the CC's value, 0..127 across its range.
  static constexpr int kMidiTargetTuner = -2, kMidiTargetVolume = -3, kMidiTargetTap = -4;
  static constexpr int kMidiNoTarget = INT_MIN;
  std::vector<int> mMidiMap[128]; // UI thread
  std::atomic<bool> mMidiCCIsVolume[128] = {}; // The audio thread's copy of the volume targets
  std::atomic<bool> mMidiCCIsTap[128] = {}; // ...and of the tap tempo targets
  std::atomic<int> mMidiCCCount[128] = {};
  std::atomic<int> mMidiCCValue[128] = {};
  std::atomic<int> mMidiLastCC{-1}, mMidiLastValue{0}, mMidiSeq{0};
  int mMidiSeqSeen = 0; // UI thread: the last message MIDI learn has looked at
  bool mMidiLearnOn = false;
  int mMidiLearnTarget = kMidiNoTarget;
  // Expression pedal = volume pedal on the output. Not a param (never saved): starts at full.
  std::atomic<double> mExprVolumeTarget = 1.0;
  double mExprVolume = 1.0; // smoothed toward the target per sample, audio thread only
  void _ApplyMidiMappings();
  void _SetDefaultMidiMap();
  void _LoadMidiMap();
  void _SaveMidiMap() const;
  std::string _MidiMapPath() const;
  void _SyncMidiVolumeFlags();
  // Tap tempo (1.71.0). A MIDI tap is timed when it arrives, on the audio thread (steady clock, in s),
  // and queued here; OnIdle hands the queue to _TempoTap. Taps more than 2 s apart start over.
  static constexpr int kMidiTapQueue = 16;
  std::atomic<double> mMidiTapTime[kMidiTapQueue] = {};
  std::atomic<int> mMidiTapWrite{0};
  int mMidiTapRead = 0; // UI thread
  double mTapTimes[5] = {}; // The last taps, oldest first (UI thread)
  int mNumTaps = 0;
  double mBeatAnchor = -1.0; // When a beat fell (the last tap), for the BPM light; < 0 = none yet
  bool mTempoPushPending = false; // The BPM was dragged or typed: _PushTempoToDelays in OnIdle
  void _TempoTap(double t);
  void _PushTempoToDelays();
  double _BeatPhase() const;
  void _SetMidiLearn(bool on);
  void _MidiLearnAssign(int target, int cc, bool add);
  bool mMidiLearnAdd = false; // The selection was Shift-clicked: add it to the CC instead of replacing
  void _MidiClearTarget(int target);
  int _MidiCCOf(int target) const;
  std::string _MidiTargetName(int target) const;
  bool _MidiIsToggle(int target) const;

  // Noise gates
  dsp::noise_gate::Trigger mNoiseGateTrigger;
  dsp::noise_gate::Gain mNoiseGateGain;
  // The model actually being used:
  std::unique_ptr<ResamplingNAM> mModel;
  // 1.64.0: the capture run inside the Tube Lab (kTubeCapture): its mono input and output, and the
  // input gain's ramp (Drive, re 12 dB).
  std::vector<iplug::sample> mCapIn, mCapOut;
  iplug::sample* mCapInPtr[1] = {nullptr};
  iplug::sample* mCapOutPtr[1] = {nullptr};
  double mCapGain = 1.0;
  bool _TubeTakesCapture() const; // The Tube Lab is running the AMP block's capture (so the AMP block doesn't)
  // And the IR (left channel, or the only channel if no R-channel IR is loaded)
  std::unique_ptr<dsp::ImpulseResponse> mIR;
  // Optional right-channel IR for a stereo (dual-mic/cab) cab sim. If null, the right channel
  // just reuses mIR (mono cab -- the original, still-default behavior).
  std::unique_ptr<dsp::ImpulseResponse> mIR_R;
  // Manages switching what DSP is being used.
  std::unique_ptr<ResamplingNAM> mStagedModel;
  std::unique_ptr<dsp::ImpulseResponse> mStagedIR;
  std::unique_ptr<dsp::ImpulseResponse> mStagedIR_R;
  // 1.42.0: a stereo IR file loaded as the cab IR stages its right channel here, alongside
  // mStagedIR (its left); without an explicit right-channel IR it becomes mIR_R.
  std::unique_ptr<dsp::ImpulseResponse> mStagedIRPairR;
  // mIR_R holds the cab IR file's own right channel (not a mirror of mIR, nor an explicit R IR).
  bool mIRStereoPair = false;
  // Flags to take away the modules at a safe time.
  std::atomic<bool> mShouldRemoveModel = false;
  std::atomic<bool> mShouldRemoveIR = false;
  std::atomic<bool> mShouldRemoveIR_R = false;

  std::atomic<bool> mNewModelLoadedInDSP = false;
  std::atomic<bool> mModelCleared = false;

  // Tone stack modules
  std::unique_ptr<dsp::tone_stack::AbstractToneStack> mToneStack;

  // Post-IR filters
  recursive_linear_filter::HighPass mHighPass;
  //  recursive_linear_filter::LowPass mLowPass;

  // Post-amp tone shaping: low-end density and high-end boost.
  recursive_linear_filter::LowShelf mDensityFilter;
  recursive_linear_filter::HighShelf mPresenceFilter;

  // Path to model's config.json or model.nam
  WDL_String mNAMPath;
  // Path to IR (.wav file)
  WDL_String mIRPath;
  // Path to the optional right-channel IR. Stashed per A/B slot like mIR (1.42.0; it used to be
  // shared, so switching slots left the right channel on the other slot's cab) and saved with the
  // state in a ###CabIRRightV1### section.
  WDL_String mIRPath_R;
  // True once the user has explicitly loaded their own right-channel IR. Until then, mIR_R is
  // kept auto-mirrored from mIR (its own independent instance/state, same audio content) so the
  // right channel still gets correct, independently-stateful mono-cab processing by default --
  // ImpulseResponse's convolution history is a single stateful ring buffer per instance, so the
  // same instance can't safely be called twice per block for two different channels.
  bool mIRRExplicit = false;

  // A/B slots: whichever slot ISN'T active stashes its model/IR/paths here.
  // (mModel/mIR/mNAMPath/mIRPath above always hold the currently-active slot's state.)
  int mActiveSlot = 0;
  std::unique_ptr<ResamplingNAM> mSlotModel[kNumABSlots];
  std::unique_ptr<dsp::ImpulseResponse> mSlotIR[kNumABSlots];
  WDL_String mSlotNAMPath[kNumABSlots];
  WDL_String mSlotIRPath[kNumABSlots];
  std::atomic<bool> mSlotSwitched = false;
  // 1.42.0: the right-channel cab IR goes with its slot too.
  std::unique_ptr<dsp::ImpulseResponse> mSlotIR_R[kNumABSlots];
  WDL_String mSlotIRPath_R[kNumABSlots];
  bool mSlotIRRExplicit[kNumABSlots] = {false, false};
  bool mSlotIRStereoPair[kNumABSlots] = {false, false};
  // 1.42.0: A/B switches on the audio thread. OnParamChange only sets the target; ProcessBlock fades
  // the output out (kABFadeOutS), swaps the slots at silence, clears the incoming cab IRs'
  // convolution history (what they last heard, possibly minutes ago) and fades back in (kABFadeInS).
  // Switching instantly clicked, and it ran on whichever thread changed the param, under the audio.
  std::atomic<int> mABSlotTarget{0};
  // 0 none, 5 pre-roll, 1 fading out, 3 silent (swap at block end), 2 fading in, 4 done at block end.
  // Pre-roll: the incoming slot's model runs on the AMP block's input (unheard) for kABPrerollS
  // first, so it comes in already playing the note rather than hearing it start mid-ring.
  int mABFadeStage = 0;
  int mABFadeCount = 0;
  double mABGain = 1.0;
  static constexpr double kABFadeOutS = 0.008;
  static constexpr double kABFadeInS = 0.015;
  static constexpr double kABPrerollS = 0.1;
  std::vector<iplug::sample> mABPrerollBuf[2];
  iplug::sample* mABPrerollIn[1] = {};
  iplug::sample* mABPrerollOut[1] = {};
  bool _ABPrerolling() const { return mABFadeStage == 5 || mABFadeStage == 1 || mABFadeStage == 3; }
  // Loading a state: ABSlot names the slot the state's model and IR belong to. The audio thread
  // relabels the live slot to it (no swap, no fade) and puts the state's other slot in place.
  bool mInUnserialize = false;
  std::atomic<int> mABRelabel{-1};
  std::unique_ptr<ResamplingNAM> mStagedSlotModel;
  std::unique_ptr<dsp::ImpulseResponse> mStagedSlotIR, mStagedSlotIR_R;
  WDL_String mStagedSlotNAMPath, mStagedSlotIRPath, mStagedSlotIRPath_R;
  bool mStagedSlotIRRExplicit = false, mStagedSlotIRStereoPair = false;
  int mStagedSlotIndex = 0;
  std::atomic<bool> mSlotStaged = false;
  // The A/B fade is applied to the AMP and CAB blocks' outputs (mABGainBuf), so everything after
  // them -- a reverb's tail included -- sees the old amp fade out and the new one fade in.
  bool _ABBeginBlock(size_t numFrames);
  void _ABEndBlock(size_t numFrames);
  std::vector<double> mABGainBuf;
  // Loads a state's inactive A/B slot (UI thread) into the mStagedSlot* members.
  void _StageInactiveSlot(int slot, const WDL_String& namPath, const WDL_String& irPath, const WDL_String& irPathR);

  WDL_String mHighLightColor{PluginColors::NAM_THEMECOLOR.ToColorCode()};

  std::unordered_map<std::string, double> mNAMParams = {{"Input", 0.0}, {"Output", 0.0}};

  NAMSender<1> mInputSender; // the plugin is mono in
  NAMSender<2> mOutputSender; // stereo out: L and R meters

  // Pedalboard: each can independently be routed Pre (before the amp) or Loop (effects loop,
  // after the tone stack, before the cab IR) via its own Position param.
  pedals::Compressor mCompressor;
  pedals::Overdrive mOverdrive;
  pedals::Chorus mChorus;
  pedals::Delay mDelay;
  pedals::Reverb mReverb;
  pedals::EQ mEqPedal;
  pedals::Tremolo mTremolo;
  // The extra copies of each (see chaingrid::kFirstExtraPedal).
  pedals::Compressor mCompressorCopy[chaingrid::kExtraCopiesPerType];
  pedals::Overdrive mOverdriveCopy[chaingrid::kExtraCopiesPerType];
  pedals::Chorus mChorusCopy[chaingrid::kExtraCopiesPerType];
  pedals::Delay mDelayCopy[chaingrid::kExtraCopiesPerType];
  pedals::Reverb mReverbCopy[chaingrid::kExtraCopiesPerType];
  pedals::EQ mEqPedalCopy[chaingrid::kExtraCopiesPerType];
  pedals::Tremolo mTremoloCopy[chaingrid::kExtraCopiesPerType];
  pedals::Flanger mFlanger;
  // 1.44.0 (NewPedals.h).
  pedals::SkyVerb mSkyVerb;
  pedals::FlangerDoubler mFlangerDoubler;
  pedals::Wah mWah;
  pedals::PrecisionDrive mPrecisionDrive;
  pedals::ApexPreamp mApexPreamp;
  // PRO EQ (1.46.0, ProEQ.h): the block and the Output EQ, and the spectrum taps the UI reads
  // (Spectrum.h): each EQ's input and output (the Output EQ's "output" is the final output).
  proeq::EQ mProEq, mOutEq;
  spectrum::Tap mProEqTapIn, mProEqTapOut, mOutTapIn, mOutTapOut;
  // GATE and PRO COMP (1.52.0, Dynamics.h), and the GATE's key: the clean input (after Master In),
  // copied at the start of each block while Key is on (blocks may process the input in place).
  dynamics::Gate mGate;
  dynamics::StudioComp mStudioComp;
  std::vector<iplug::sample> mGateKeyBuf[2];
  iplug::sample* mGateKeyPtrs[2] = {nullptr, nullptr};
  int mGateKeyChannels = 0;
  // Their popups: two rows of controls and a meter.
  static bool _IsDynamicsPedal(int id) { return id == chaingrid::kGate || id == chaingrid::kStudioComp; }
  iplug::igraphics::IRECT _ComputeDynamicsPopupCardArea(const iplug::igraphics::IRECT& winBounds) const;
  // Their popups are wider than a plain pedal's (more controls).
  static bool _IsWidePedal(int id)
  {
    return (id >= chaingrid::kSkyVerb && id <= chaingrid::kApexPreamp)
           || (id >= chaingrid::kTrackGate && id <= chaingrid::kSpringVerb);
  }
  // 1.66.0 pedals (MorePedals.h).
  pedals::TrackGate mTrackGate;
  pedals::DigitalDelay mDigiDelay;
  pedals::AnalogDelay mAnalogDelay;
  pedals::TapeEcho mTapeEcho;
  pedals::SpringVerb mSpringVerb;
  iplug::igraphics::IRECT _ComputeWidePopupCardArea(const iplug::igraphics::IRECT& winBounds) const;
  pedals::Phaser mPhaser;
  // REAL FEEL pedal (chaingrid::kRealFeel): its DSP and output buffers, and the Shared B+ gain last
  // applied to the AMP block's input (ramped from there to the pedal's latest each block).
  realfeel::RealFeel mRealFeel;
  mains::HumFilter mHumFilter; // The input's Hum Filter (kHumFilter)
  std::vector<iplug::sample> mRealFeelBuf[2];
  iplug::sample* mRealFeelPtrs[2] = {};
  double mAmpFeedGain = 1.0;
  bool _RealFeelPedalPlaced() const;

  // NAM-file pedals: like mModel, but per pedal slot, loaded from a downloaded .nam capture
  // (e.g. Tone3000). Pre-amp only (mono) -- see kNamPedalSlotMin/Max. An unloaded or bypassed slot
  // costs nothing extra in ProcessBlock, so DSP resources only scale up as pedals are actually used.
  std::unique_ptr<ResamplingNAM> mNamPedalModel[kNumNamPedals];
  std::unique_ptr<ResamplingNAM> mStagedNamPedalModel[kNumNamPedals];
  WDL_String mNamPedalPath[kNumNamPedals];
  std::atomic<bool> mShouldRemoveNamPedal[kNumNamPedals] = {};
  // Dedicated scratch buffers, one per slot -- deliberately NOT mOutputPointers (which the amp
  // model writes fresh every block): a NAM pedal that's last in the pre-amp chain would otherwise
  // hand the amp model the same buffer for both its input and output, aliasing a stateful inference
  // pass against itself.
  std::vector<iplug::sample> mNamPedalBuffer[kNumNamPedals];
  double mNamPedalLevelSm[kNumNamPedals] = {-1.0, -1.0}, mNamPedalMixSm[kNumNamPedals] = {0.0, 0.0}; // (< 0: not started)
  iplug::sample* mNamPedalBufferPtr[kNumNamPedals][1] = {};

  // Tube Lab: one stage per channel, so it works on the mono pre-amp side and the stereo loop side.
  // While it's in the chain, its 15-sample oversampling latency is always reported; the on/off
  // (Engaged) switch crossfades to the equally-delayed dry signal instead of changing the latency,
  // so footswitching it doesn't make the host re-compensate mid-song.
  tube::TubeStage mTubeStage[2];
  iplug::sample* mTubeChannels[2] = {nullptr, nullptr};
  // Last "Tube Lab is in the chain" state ProcessBlock saw, so latency is updated on change.
  bool mTubeWasInChain = false;
  // Every Tube Lab sound param, as the stage takes them (also read by the debug page and log), of
  // lab 0 (TUBE LAB) or lab 1 (TUBE LAB 2).
  tube::TubeParams _ReadTubeParams(int lab = 0) const;
  // TUBE LAB 2 (1.67.0): its stages, output, capture (its own .nam, staged like the NAM pedals') and
  // capture input gain.
  tube::TubeStage mTubeStage2[2];
  iplug::sample* mTubeChannels2[2] = {nullptr, nullptr};
  bool mTube2WasInChain = false;
  double mCapGain2 = 1.0;
  std::unique_ptr<ResamplingNAM> mLab2Model, mStagedLab2Model;
  WDL_String mLab2ModelPath;
  std::atomic<bool> mShouldRemoveLab2Model{false};
  std::string _StageLab2Model(const WDL_String& path); // "" or the error
  // The Tube Lab popup, debug page and chassis page edit one lab at a time: mUiLab, the one whose
  // popup was opened last. Every control bound to a kTubeLabParams param is re-bound to that lab's
  // copy (mLabRetarget: {control, TUBE LAB's param}); mLabOnly[lab] show only for that lab. The audio
  // thread feeds the probe and the chassis readings from mUiLabAudio's stages.
  int mUiLab = 0;
  std::atomic<int> mUiLabAudio{0};
  std::vector<std::pair<iplug::igraphics::IControl*, int>> mLabRetarget;
  std::vector<iplug::igraphics::IControl*> mLabOnly[2];
  void _SetUiLab(int lab);
  // 1.67.1: CAB LAB opens into the SPEAKER LAB (3D on); its CAB LAB button goes to the classic card (3D off).
  void _ShowSpeakerLab(bool show);
  int _TP(int base) const { return LabParam(mUiLab, base); }
  tube::TubeStage* _UiStages() { return mUiLab == 0 ? mTubeStage : mTubeStage2; }
  bool _TubeLabSelected() const { return mSelectedBlock == kTubePedalIdx || mSelectedBlock == chaingrid::kTubeLab2; }
  // An EQ's settings from its params (EqBandParam); disabled = every band off, output 0 dB.
  proeq::Params _ReadEqParams(int firstBandParam, int firstExtParam, int trimParam, int outputParam, bool enabled) const;
  // The EQ views' analyzer/freeze/selection (1.47.0): PRO EQ's popup, the Output EQ page, the strip.
  spectrum::EqViewState mProEqView, mOutEqView, mStripView;

  // Tube Lab debug page and CSV log (1.32.0, TubeProbe.h / TubeDebug.h / TubeDebugControls.h).
  // The probe sits on mTubeStage[0]; it only captures while the page is open or the log is on.
  // Statistics are requested every kTubeStatsMs and arrive at the end of the next audio block.
  static constexpr int kTubeStatsMs = 250;
  tube::TubeProbe mTubeProbe;
  TubeScopeSettings mTubeScopeSettings;
  tube::debug::TubeDebugLog mTubeLog;
  bool mTubeDebugVisible = false; // Kept across relayouts
  bool mTubeProbesAlways = false; // 1.58.0: ALWAYS ON (ui.ini tube_probes_always)
  // 1.62.0: the PREAMP CHASSIS page: its readings (written on the audio thread after the Tube Lab, read
  // in OnIdle) and its requests (a power tube to replace: bit k).
  std::atomic<bool> mChassisOpen{false};
  // Glow levels: V1..V4, then the sockets V5..V8 (1.68.0; TubeStage::StageLevel).
  // Power tubes: V9..V12 (1.68.0: four, for the quad).
  std::atomic<float> mUiStageLevel[tube::TubeStage::kNumStageLevels] = {};
  std::atomic<float> mUiPowerLife[4] = {1.f, 1.f, 1.f, 1.f}, mUiDissRatio[4] = {};
  std::atomic<bool> mUiPowerDead[4] = {false, false, false, false};
  std::atomic<int> mReplacePowerReq{0}, mReplacePowerReq2{0}; // TUBE LAB's, TUBE LAB 2's
  void _UpdateChassis(); // OnIdle
  // 1.68.0: the chassis's links to the other pages (chassis::Link).
  void _ChassisGo(int link);
  int mChassisSelected = 0; // chassis::Item
  // The side panel's controls, each with the chassis::Item it belongs to (OnIdle shows the selected one's).
  std::vector<std::pair<int, iplug::igraphics::IControl*>> mChassisFields;
  float mChassisYaw = -0.35f, mChassisPitch = 0.7f; // 1.68.0: the 3D view's camera, kept across relayouts
  double mChassisSignature = 0.0; // What the page last drew (see _UpdateChassis)
  std::chrono::steady_clock::time_point mChassisLastDraw{};
  std::chrono::steady_clock::time_point mTubeStatsNextRequest{}, mTubeStatsLastArrived{}, mTubeLastSignalTime{};
  std::chrono::steady_clock::time_point mTubeLogStart{};
  double mTubeLogLastRowS = -1e9;
  tube::ProbeStats mTubeLastSignalStats;
  bool mTubeHaveSignalStats = false;
  std::vector<float> mTubeLiveFrames;
  // Called every OnIdle tick: feeds the page and the log.
  void _UpdateTubeDebug();
  void _SetTubeLogging(bool on);
  std::string _TubeLogDir() const;
  // The blocks the signal goes through, left to right ("TUBE LAB > AMP > CAB"), for the log.
  std::string _ChainDescription() const;

  // CAB LAB: the cab/mic/room model, plus its own (optional) speaker IR. The IR loads on the UI
  // thread into the staged slot and is swapped in by the audio thread (see _ApplyDSPStaging).
  cab::CabLab mCabLab;
  std::unique_ptr<dsp::ImpulseResponse> mCabLabIR, mStagedCabLabIR;
  // CAB 2 / CAB 3 (1.36.0): their IRs (staged from the UI thread like CAB LAB's; an empty path clears
  // one), paths, output buffers, and the Level/Pan gains last applied (ramped from each block).
  std::unique_ptr<dsp::ImpulseResponse> mExtraCabIR[kNumExtraCabs], mStagedExtraCabIR[kNumExtraCabs];
  std::atomic<bool> mExtraCabIRStaged[kNumExtraCabs] = {false, false};
  WDL_String mExtraCabIRPath[kNumExtraCabs];
  std::vector<iplug::sample> mExtraCabMono[kNumExtraCabs], mExtraCabOut[kNumExtraCabs][2];
  iplug::sample* mExtraCabPtrs[kNumExtraCabs][2] = {};
  double mExtraCabGain[kNumExtraCabs][2] = {{1.0, 1.0}, {1.0, 1.0}};
  bool _StageExtraCabIR(int cab, const WDL_String& path);
  std::atomic<bool> mCabLabIRStaged = false;
  WDL_String mCabLabIRPath;
  // 1.61.0: CAB LAB's second cab's IR (dual cabs), staged like the first.
  std::unique_ptr<dsp::ImpulseResponse> mCabLabIR2, mStagedCabLabIR2;
  std::atomic<bool> mCabLabIR2Staged = false;
  WDL_String mCabLabIR2Path;
  bool _StageCabLabIR2(const WDL_String& path); // Empty path: none
  std::vector<double> mCabMono, mCabL, mCabR;
  iplug::sample* mCabChannels[2] = {nullptr, nullptr};
  // Loads an IR for the CAB LAB (UI thread). Returns true on success.
  bool _StageCabLabIR(const WDL_String& path);

  // Signal-chain grid ============================================
  // The grid as stored in params. Pedals that aren't Active read as off the grid.
  chaingrid::Grid _ReadChainGrid() const;
  // UI thread: sends every grid param that differs from `grid`.
  void _WriteChainGrid(const chaingrid::Grid& grid);
  // Lays out a pre-1.17 state's Slot order on the grid (sets the params directly, not via the UI).
  void _MigrateLegacyChain();
  // Clears the grid params, so a state loaded without them gets migrated.
  void _ResetChainGridParams();
  // Sizes the grid's mix and delay buffers for blocks of up to numFrames.
  void _PrepareGridBuffers(size_t numFrames);
  // Per block (and Output, at index kNumIds): the mixed input it processes.
  std::vector<iplug::sample> mGridMix[chaingrid::kNumIds + 1][2];
  iplug::sample* mGridMixPtrs[chaingrid::kNumIds + 1][2] = {};
  // 1.42.0: a block's On switch crossfades (kBlockFadeS, smoothstep) between its input and its
  // output instead of switching it in or out between two samples, which clicked. mBlockFade is
  // where each block is in its fade (0 = out of the path, 1 = in it); mBlockFadeOut holds a
  // fading block's output and mBlockFadeDry a copy of its input.
  static constexpr double kBlockFadeS = 0.02;
  double mBlockFade[chaingrid::kNumIds] = {};
  bool mBlockFadeInit = false; // The first block after a reset starts every fade where it's going
  std::vector<iplug::sample> mBlockFadeOut[chaingrid::kNumIds][2];
  iplug::sample* mBlockFadeOutPtrs[chaingrid::kNumIds][2] = {};
  std::vector<iplug::sample> mBlockFadeDry[2];
  std::vector<double> mBlockFadeGain;
  // Switched off but still ringing out (a delay's repeats, a reverb's or cab's tail): keeps
  // processing on silence until its output is, or for at most kMaxTailS.
  bool mBlockTail[chaingrid::kNumIds] = {};
  int mBlockTailSamples[chaingrid::kNumIds] = {};
  static constexpr double kMaxTailS = 20.0;
  // Density / Boost on/off (the same crossfade), and Master In/Out as last applied (ramped per block).
  double mDensityFade = 1.0, mBoostFade = 1.0;
  std::vector<iplug::sample> mShelfDry[2];
  iplug::sample* mShelfPtrs[2] = {};
  double mInputGainApplied = -1.0, mOutputGainApplied = -1.0;
  // Per source (blocks, then Input): a short history of its output, so parallel paths with
  // different latencies line up where they merge.
  static constexpr int kGridRingSize = 8192;
  std::vector<iplug::sample> mGridRing[chaingrid::kNumIds][2];
  int mGridRingPos = 0;
  // Latency of the path into Output, as reported to the host.
  int mGridLatency = 0;
  // The FX Loop Return's input, copied before anything writes the (possibly shared) output buffers.
  std::vector<iplug::sample> mLoopReturnBuf[2];
  iplug::sample* mLoopReturnPtrs[2] = {};
  // kLoopMix (0..1) as last applied, for ramping it across a block.
  float mLoopMixSmoothed = 1.f;
  // The FX loop's popup (On switch + Mix knob), opened from either LOOP chip.
  std::vector<iplug::igraphics::IControl*> mLoopDetailControls;

  // Output clip warning (1.19.2): the audio thread flags any output sample past full scale (checked
  // before the standalone's safety soft clip); OnIdle then shows "CLIP" in red under the OUT meter
  // for kClipHoldMs. mClipShown is what the label currently says (reset on every relayout).
  static constexpr int kClipHoldMs = 1500;
  std::atomic<bool> mOutputClipped = false;
  // Output limiter (see kOutputLimiter): its DSP, whether it's in the path (it follows the switch
  // through a short fade out and back in, since switching moves the output by its lookahead), and
  // the stereo scratch it works on.
  OutputLimiter mLimiter;
  bool mLimiterOn = false;
  int mLimiterFadeStage = 0, mLimiterFadeCount = 0; // 0 idle, 1 fading out, 3 silent to block end, 2 fading in
  std::vector<double> mLimiterBuf[2];
  // Output Width / Widen (kOutputWidth, kOutputWiden; StereoWidth.h).
  StereoWidth mStereoWidth;
  // For the OUTPUT page's meters, only while it's open (mOutputPageOpen, set from OnIdle): the most
  // limiter gain reduction since the page last looked (dB, <= 0; the UI swaps in 0) and the output's
  // stereo correlation.
  std::atomic<bool> mOutputPageOpen{false};
  std::atomic<float> mLimiterGRShown{0.f}, mOutCorrelation{1.f};
  int _TotalLatency() const { return mGridLatency + (mLimiterOn ? mLimiter.Latency() : 0); }
  std::chrono::steady_clock::time_point mClipShownUntil{};
  bool mClipShown = false;
  void _UpdateClipIndicator();
  void _UpdateOutputPage(); // 1.58.0: the OUTPUT page's meters (OnIdle)
  // SPEAKER LAB (1.59.0): its params as the DSP sees them, the page's refresh after a change, a mic's
  // placement from a drag, and the selected mic (its controls are the ones showing) and 3D camera.
  cab::CabLabParams _ReadCabLabParams() const;
  void _SpeakerLabChanged();
  void _SetLabMicPosition(int mic, double x, double y, double z);
  void _RemapLabMicsForFormat(int cab, int oldFormat, int newFormat, int onlyMic = -1);
  // 1.60.0: the PEDALS drawer (over the grid) is open.
  bool mPedalDrawerOpen = false;
  void _SetPedalDrawerOpen(bool open);
  int mLabSelectedMic = 0;
  int mLabSelectedCab = 0; // 1.61.0: the cab the FRONT / SIDE views and the cab row show
  int mLabFormat2Shown = -1;
  std::vector<iplug::igraphics::IControl*> mLabCabControls[2]; // Each cab's row on the page
  speakerlab::Model mLabModel;
  float mLabYaw = -0.55f, mLabPitch = 0.32f;
  int mLabFormatShown = -1; // The format the mics were last placed for (see OnParamChangeUI)
  std::vector<iplug::igraphics::IControl*> mLabMicControls[4]; // Each mic's controls on the page

  // Tuner: analyzes the raw input signal, independent of the rest of the chain.
  Tuner mTuner;

  // Whether the right-side AMP NEST / Quick FX panel is shown. Toggled from the main view; when
  // false, the main content area widens to reclaim the space.
  bool mShowLibraryPanel = true;
  // Set by the toggle button, consumed on the next OnIdle tick (see .cpp for why it's deferred).
  bool mLibraryPanelRelayoutPending = false;
  // UI preferences (1.48.0), kept in ui.ini next to the autosave (shared by the app and the plug-in):
  // the colour theme (palette 0-5, dark or light) and whether the spectrum strip is shown.
  int mThemeIndex = 0;
  bool mThemeLight = false;
  bool mShowSpecStrip = true;
  bool mSpecStripExpanded = false; // the strip's taller size (its arrow button)
  iplug::igraphics::IRECT mSpecStripRect; // where the strip is, from the last layout
  // The strip's controls, with their rects at its current and small size (the big cards shrink it).
  struct SpecStripCtl
  {
    iplug::igraphics::IControl* control;
    iplug::igraphics::IRECT full, shrunk;
  };
  std::vector<SpecStripCtl> mSpecStripCtls;
  iplug::igraphics::IRECT mSpecStripSmallRect;
  bool mSpecFloating = false; // 1.50.0: the spectrum in its own movable window
  iplug::igraphics::IRECT mSpecFloatRect; // that window, in UI units (empty: the default spot)
  void _SetSpecFloating(bool floating);
  void _SetSpecStripExpanded(bool expanded);
  iplug::igraphics::IRECT _ComputePedalPopupCardArea(const iplug::igraphics::IRECT& winBounds) const;
  iplug::igraphics::IRECT _LiftAboveStrip(const iplug::igraphics::IRECT& card,
                                          const iplug::igraphics::IRECT& winBounds) const;
  bool mReopenSettingsAfterRelayout = false;
  void _LoadUiPrefs();
  void _SaveUiPrefs() const;
  void _SetTheme(int theme, bool light);
  void _SetSpecStripShown(bool show);
  // Scroll position (in px) of the AMP NEST panel's pedal picker list -- the panel is a fixed
  // height, and the pedal list (drag one out into the rack to activate it) can be taller than
  // that, so this is scrolled via a pair of arrow buttons rather than trying to fit everything.
  float mLibraryScrollY = 0.f;

  // Main screen IS the signal chain/routing view: the AMP block (always-visible controls below
  // it) plus pedal chips, always visible (no separate hidden "FX" page). AMP and CAB are draggable
  // (their own Slot params, see kAmpSlotMin/Max), same drag mechanic as pedals; only CAB and pedals
  // open a popup on click (AMP's controls are the always-visible ones below instead, so clicking it
  // does nothing beyond dragging). Only one popup is ever open at a time.
  // Non-owning pointers into the IGraphics control stack, rebuilt every relayout (see mLayoutFunc).
  // -1 = none (hint shown), 0..kNumPedals-1 = that pedal's popup is open, kCabBlockId = the fixed
  // CAB (IR/cabinet) block's popup is open, kAmpBlockId = the amp's own (bigger) popup is open.
  int mSelectedBlock = -1;
  // Indexed by grid id (only pedal ids are used: see chaingrid::IsPedal).
  iplug::igraphics::IControl* mPedalChips[chaingrid::kNumIds] = {};
  std::vector<iplug::igraphics::IControl*> mPedalDetailControls[chaingrid::kNumIds];
  // The Tube Lab's tabs (1.46.0): which is open (kept across relayouts), and each page control's tab.
  int mTubeTab = 0;
  std::vector<std::pair<iplug::igraphics::IControl*, int>> mTubeTabControls;
  // AMP: draggable AND clickable -- opens a popup (its own bigger card, not the shared
  // pedal/cab one) holding every one of the amp's own controls.
  iplug::igraphics::IControl* mAmpChip = nullptr;
  std::vector<iplug::igraphics::IControl*> mAmpDetailControls;
  // Amp popup size, persisted across relayouts (0 = not yet set -- computed to a wide, short,
  // amp-head-like default the first time mLayoutFunc runs) and user-adjustable via the resize
  // grip in its corner. Re-clamped against the current window every time it's used, since the
  // window itself can be resized independently of this.
  float mAmpPopupWidth = 0.f;
  float mAmpPopupHeight = 0.f;
  // The meters' normal (main-page) positions, captured in mLayoutFunc so _SelectBlock can move
  // them into the Amp popup (next to Input/Output) while it's open and put them back when it
  // closes -- "carrying" the single existing meter controls rather than trying to mirror a second
  // instance, since IGraphics only supports one control per ctrlTag (GetControlWithTag returns a
  // single match), so a second meter sharing the tag would never actually receive level updates.
  iplug::igraphics::IRECT mInputMeterHomeRect;
  iplug::igraphics::IRECT mOutputMeterHomeRect;
  iplug::igraphics::IRECT mInputMeterLabelHomeRect;
  iplug::igraphics::IRECT mOutputMeterLabelHomeRect;
  // CAB: draggable AND clickable -- it opens a popup (reusing the same card/backdrop/title/close
  // chrome as a pedal) holding the IR/cab-loading controls that used to sit in an always-visible
  // row under the amp's own knobs.
  iplug::igraphics::IControl* mCabButton = nullptr;
  std::vector<iplug::igraphics::IControl*> mCabDetailControls;
  // Loop Send/Return: draggable, no popup (nothing to adjust -- they're pure taps into the aux
  // bus), same non-clickable-but-draggable pattern as AMP.
  iplug::igraphics::IControl* mLoopSendChip = nullptr;
  iplug::igraphics::IControl* mLoopReturnChip = nullptr;
  iplug::igraphics::IControl* mBlockHintLabel = nullptr;
  // Bottom-left latency readout, and the values it last showed (see OnIdle).
  iplug::igraphics::IControl* mLatencyLabel = nullptr;
  int mShownBlockSize = -1, mShownLatency = -1;
  double mShownSampleRate = -1.;
  // Standalone only: the audio device's buffer size: what the running stream got (1.51.0), else the
  // app's settings.ini (0 = unknown). mRequestedBufferSize is the settings.ini value (what was asked for;
  // a driver may only run the size set in its own panel).
  int mDeviceBufferSize = 0;
  int mRequestedBufferSize = 0;
  int mDeviceBufferPollTicks = 1000; // > threshold, so it's read on the first idle tick
  // Standalone only (1.31.0): a buffer size picked on the Settings page, applied on the next idle
  // tick (outside the click handler) by restarting the app's audio. 0 = nothing pending.
  int mPendingBufferSize = 0;
  // Standalone only (1.51.0): Settings page DRIVER PANEL / DEVICE clicks, run on the next idle tick.
  bool mPendingDriverPanel = false;
  // Idle ticks until DRIVER PANEL tries once more if nothing came up (Focusrite's driver only starts
  // its helper app on the first call). 0 = no retry pending.
  int mDriverPanelRetryTicks = 0;
  bool mPendingDevicePrefs = false;
  // The AMP NEST library panel's show/hide toggle -- hidden while any block's popup is open (see
  // _SelectBlock) so it can't float on top of a popup and trigger a relayout out from under it.
  iplug::igraphics::IControl* mLibraryToggleButton = nullptr;
  iplug::igraphics::IControl* mBlockPopupBackdrop = nullptr;
  iplug::igraphics::IControl* mBlockPopupCardBg = nullptr;
  iplug::igraphics::IControl* mBlockPopupCloseButton = nullptr;
  iplug::igraphics::IControl* mBlockPopupTitleLabel = nullptr;
  void _SelectBlock(int blockId);
  // Tube Lab presets (TubePresets.h): sets every Tube Lab sound param, as if turned in the UI.
  void _ApplyTubePreset(int index);
  iplug::igraphics::IPopupMenu mTubePresetMenu;
  // 1.70.0 PACK PICKER (PackScanner.h, PackPickerControl.h): switch between the captures of one
  // unzipped pack folder from the AMP block. Everything here is UI-thread state except the atomics,
  // which the audio thread reads in _SetOutputGain / _ApplyDSPStaging.
  griffinpack::Pack mPack;
  std::string mPackGroup, mPackKey, mPackSize; // The capture picked in the pack ("" = none)
  bool mPackAutoIR = true; // Pair the pack's IR with amp-only captures
  std::string mPackIRChoice; // The pack IR to pair ("" = the pack's default)
  std::string mPackIRSetPath; // The cab IR the pack itself last loaded ("" = none): the only one it replaces
  bool mPackHasRef = false; // Level match: the loudness of the first capture picked in this pack
  double mPackRefLoudness = 0.0;
  bool mPackStaging = false; // _StageModel is staging a pack capture (UI thread)
  void* mLastStagedModelPtr = nullptr; // The model _StageModel last staged (identity only, never dereferenced)
  std::atomic<void*> mPackModelPtr{nullptr}; // The pack capture's model: only it gets the level trim
  std::atomic<double> mPackTrimDB{0.0}; // Its trim (Raw output mode only; the other modes already level)
  std::atomic<bool> mPackGainDirty{false}; // Recompute the output gain on the audio thread
  iplug::igraphics::IPopupMenu mPackMenu;
  std::vector<std::pair<const void*, std::function<void()>>> mPackMenuTargets; // {menu item, what it does}
  void _PackOnMenu(iplug::igraphics::IPopupMenu* pMenu);
  // 1.70.3: .zip packs, unpacked on a worker thread into PackCache (PackZip.cpp).
  struct PackZipJob { std::string zip, dir, err, log; bool reused = false; double ms = 0.0; };
  std::future<PackZipJob> mPackZipFuture;
  std::string mPackUnpacking; // The zip being unpacked ("" = none): the dropdown says so
  std::string mPackZipPath; // The zip the open pack came from ("" = a plain folder): what presets save
  std::string mPackOpeningZip; // Set while _PackOpenFolder opens a zip's cache folder
  void _PackPollZip(); // UI thread (OnIdle): picks up a finished unpack
  void _PackOpenFolder(const std::string& folder);
  void _PackSelect(const griffinpack::Capture* c);
  std::string _PackIRPath() const;
  std::string _PackLabel(int which) const; // 0 PACK, 1 capture, 2 size, 3 IR
  void _PackShowMenu(int which, iplug::igraphics::IControl* pCaller);
  // Greys out the Tube Lab's circuit-row controls that don't currently apply: all of them in the
  // Classic engine, and the cap/supply knobs while their section switch is off.
  void _RefreshTubeLabControls(iplug::igraphics::IGraphics* pGraphics);
  // Shows the open tab's page of Tube Lab controls (only while the Tube Lab's popup is open).
  void _ApplyTubeTab();
  // Greys out CAB LAB controls that don't apply (speaker selector with an IR; Mic B's with it off).
  void _RefreshCabLabControls(iplug::igraphics::IGraphics* pGraphics);
  // Shared by mLayoutFunc (initial placement) and _SelectBlock (per-selection resize) so the two
  // can never drift out of sync with each other -- previously this formula was duplicated in both
  // places by hand.
  iplug::igraphics::IRECT _ComputeAmpPopupCardArea(const iplug::igraphics::IRECT& winBounds) const;
  // Same idea for the Tube Lab, whose popup is bigger than a regular pedal's (6 knobs + a curve).
  iplug::igraphics::IRECT _ComputeTubePopupCardArea(const iplug::igraphics::IRECT& winBounds) const;
};
