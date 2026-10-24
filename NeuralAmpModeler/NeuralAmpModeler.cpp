#include <algorithm> // std::clamp, std::min
#include <chrono>
#include <cmath> // pow
#include <filesystem>
#include <cstdlib> // std::getenv (the MIDI map's folder)
#include <fstream>
#include <sstream>
#include <iostream>
#include <tuple>
#include <typeinfo>
#include <utility>

#include "Colors.h"
#include "../NeuralAmpModelerCore/NAM/activations.h"
#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
// clang-format off
// These includes need to happen in this order or else the latter won't know
// a bunch of stuff.
#include "NeuralAmpModeler.h"
#include "TubePresets.h"
#include "IPlug_include_in_plug_src.h"
// clang-format on
#include "architecture.hpp"

#include "NeuralAmpModelerControls.h"
#include "TubeDebugControls.h"
#include "OutputPageControls.h"
#include "SpeakerLabControls.h"
#include "LibraryControls.h"
#include "PackPickerControl.h" // 1.70.0 PACK PICKER
static void _PackLog(const std::string& text); // 1.70.1 (defined with the PACK PICKER code)
#include "TubeChassisControls.h"
#include "ParamHelp.h"
#include "EQControls.h"
#include "IPlugPaths.h"
#if defined(APP_API) && defined(OS_WIN)
  #include "IPlugAPP_host.h" // Settings page buffer size (IPlugAPPHost::SetBufferSize)
#endif

using namespace iplug;

// The amp/IR library folders live in "<user profile>\Desktop\Griffin Amplification\".
#include <cstdlib>
static std::string GriffinLibraryDir()
{
  std::string home = ".";
#ifdef _WIN32
  char* buf = nullptr;
  size_t len = 0;
  if (_dupenv_s(&buf, &len, "USERPROFILE") == 0 && buf != nullptr)
  {
    home = buf;
    free(buf);
  }
  return home + "\\Desktop\\Griffin Amplification\\";
#else
  if (const char* h = std::getenv("HOME"))
    home = h;
  return home + "/Desktop/Griffin Amplification/";
#endif
}
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;

// Styles. Built from the theme colours (Colors.h), so they're rebuilt by _RebuildStyles at the start
// of every layout -- a theme change (1.48.0) then shows up everywhere once the UI is rebuilt.
// 1.57.0: settings, presets, the autosave and the MIDI map moved from %LOCALAPPDATA%\NeuralAmpModeler to
// %LOCALAPPDATA%\<BUNDLE_NAME>, so this app no longer shares a folder with the plug-in it was forked from.
// On the first run the old folder is copied across, before the app reads settings.ini (a static
// initializer runs before the app's main). Only a folder this app wrote is copied (it has ui.ini, which
// only this app makes), and the old one is left in place.
static const bool gSettingsFolderMigrated = []() {
  namespace fs = std::filesystem;
  const char* base = std::getenv("LOCALAPPDATA");
  if (base == nullptr)
    return false;
  std::error_code ec;
  const fs::path from = fs::u8path(base) / "NeuralAmpModeler", to = fs::u8path(base) / BUNDLE_NAME;
  if (fs::exists(to, ec) || !fs::exists(from / "ui.ini", ec))
    return false;
  fs::copy(from, to, fs::copy_options::recursive, ec);
  return !ec;
}();

static IVColorSpec _MakeColorSpec()
{
  return IVColorSpec{
    DEFAULT_BGCOLOR, // Background
    PluginColors::NAM_THEMECOLOR, // Foreground
    PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), // Pressed
    PluginColors::NAM_THEMECOLOR.WithOpacity(0.4f), // Frame
    PluginColors::MOUSEOVER, // Highlight
    DEFAULT_SHCOLOR, // Shadow
    PluginColors::NAM_THEMECOLOR, // Extra 1
    COLOR_RED, // Extra 2 --> color for clipping in meters
    PluginColors::NAM_THEMECOLOR.WithContrast(0.1f), // Extra 3
  };
}
IVColorSpec colorSpec = _MakeColorSpec();

static IVStyle _MakeStyle()
{
  return IVStyle{true, // Show label
            true, // Show value
            colorSpec,
            {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Middle, PluginColors::NAM_THEMEFONTCOLOR}, // Knob label text5
            {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Bottom, PluginColors::NAM_THEMEFONTCOLOR}, // Knob value text
            DEFAULT_HIDE_CURSOR,
            DEFAULT_DRAW_FRAME,
            false,
            DEFAULT_EMBOSS,
            0.2f,
            2.f,
            DEFAULT_SHADOW_OFFSET,
            0.8f, // widgetFrac: shrink the knob/switch graphic within its cell so labels/values get breathing room
            DEFAULT_WIDGET_ANGLE};
}
IVStyle style = _MakeStyle();

static IVStyle _MakeTitleStyle()
{
  return DEFAULT_STYLE.WithValueText(IText(30, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular"))
    .WithDrawFrame(false)
    .WithShadowOffset(PluginColors::NAM_LIGHT ? 0.f : 2.f);
}
IVStyle titleStyle = _MakeTitleStyle();

static IVStyle _MakeRadioButtonStyle()
{
  return style
    .WithColor(EVColor::kON, PluginColors::NAM_THEMECOLOR) // Pressed buttons and their labels
    .WithColor(EVColor::kOFF, PluginColors::NAM_THEMECOLOR.WithOpacity(0.1f)) // Unpressed buttons
    .WithColor(EVColor::kX1, PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f)); // Unpressed buttons' labels
}
IVStyle radioButtonStyle = _MakeRadioButtonStyle();

static void _RebuildStyles()
{
  colorSpec = _MakeColorSpec();
  style = _MakeStyle();
  titleStyle = _MakeTitleStyle();
  radioButtonStyle = _MakeRadioButtonStyle();
}

// Describes each of the kNumPedals blocks in the routing rack -- file scope (not just local to the
// layout function) since both the layout (building the controls) and NeuralAmpModeler::_SelectBlock
// (naming the popup title) need it.
struct PedalDef
{
  const char* name;
  int activeParam; // in-chain (position); NOT the on/off switch
  int engagedParam; // real-time on/off (bypass), MIDI/automation-mappable; doesn't move the chip
  int slotParam;
  struct
  {
    const char* label;
    int paramIdx;
  } knobs[4];
};
// Tube Lab stepped capacitor selectors: 6 stock values each, like swapping parts on a real amp.
// Index 0 of the cathode and plate caps is "None" (no cap fitted).
static const double kTubeCathodeCapUF[6] = {0.0, 0.68, 1.0, 2.2, 10.0, 25.0};
static const double kTubePlateCapPF[6] = {0.0, 47.0, 100.0, 220.0, 470.0, 1000.0};
static const double kTubeCoupleNF[6] = {2.2, 4.7, 10.0, 22.0, 47.0, 100.0};

static const PedalDef kPedals[kNumPedals] = {
  {"COMP", kCompressorActive, kCompressorEngaged, kCompressorSlot,
   {{"Sustain", kCompressorSustain}, {"Level", kCompressorLevel}, {"Mix", kCompressorMix}, {nullptr, -1}}},
  {"OD", kOverdriveActive, kOverdriveEngaged, kOverdriveSlot,
   {{"Drive", kOverdriveDrive}, {"Tone", kOverdriveTone}, {"Level", kOverdriveLevel}, {"Mix", kOverdriveMix}}},
  {"CHORUS", kChorusActive, kChorusEngaged, kChorusSlot,
   {{"Rate", kChorusRate}, {"Depth", kChorusDepth}, {"Mix", kChorusMix}, {nullptr, -1}}},
  {"DELAY", kDelayActive, kDelayEngaged, kDelaySlot,
   {{"Time", kDelayTime}, {"Feedback", kDelayFeedback}, {"Mix", kDelayMix}, {nullptr, -1}}},
  {"REVERB", kReverbActive, kReverbEngaged, kReverbSlot,
   {{"Size", kReverbSize}, {"Mix", kReverbMix}, {nullptr, -1}, {nullptr, -1}}},
  {"EQ", kEqPedalActive, kEqPedalEngaged, kEqPedalSlot,
   {{"Low", kEqPedalLow}, {"Mid", kEqPedalMid}, {"High", kEqPedalHigh}, {"Level", kEqPedalLevel}}},
  {"TREMOLO", kTremoloActive, kTremoloEngaged, kTremoloSlot,
   {{"Rate", kTremoloRate}, {"Depth", kTremoloDepth}, {"Mix", kTremoloMix}, {nullptr, -1}}},
  // Tube Lab: its popup has 6 knobs and a transfer-curve plot, so it's laid out separately (see the
  // kTubePedalIdx case in the popup-building pass) rather than from this 4-knob list.
  {"TUBE LAB", kTubeActive, kTubeEngaged, kTubeSlot, {{nullptr, -1}, {nullptr, -1}, {nullptr, -1}, {nullptr, -1}}},
  // CAB LAB: also laid out separately (see the kCabLabPedalIdx case in the popup-building pass).
  {"CAB LAB", kCabLabActive, kCabLabEngaged, kCabLabSlot, {{nullptr, -1}, {nullptr, -1}, {nullptr, -1}, {nullptr, -1}}},
  {"CAPTURE 1", kNamPedal0Active, kNamPedal0Engaged, kNamPedal0Slot,
   {{"Level", kNamPedal0Level}, {"Mix", kNamPedal0Mix}, {nullptr, -1}, {nullptr, -1}}},
  {"CAPTURE 2", kNamPedal1Active, kNamPedal1Engaged, kNamPedal1Slot,
   {{"Level", kNamPedal1Level}, {"Mix", kNamPedal1Mix}, {nullptr, -1}, {nullptr, -1}}},
};

// The extra copies of the effect pedals (see chaingrid::kFirstExtraPedal): the same knobs as the
// original, on their own params (from kExtraPedalParams0), named "REVERB 2" etc.
static std::string gExtraPedalNames[chaingrid::kNumExtraPedals];
static PedalDef gExtraPedals[chaingrid::kNumExtraPedals];
static bool _InitExtraPedalDefs()
{
  for (int e = 0; e < chaingrid::kNumExtraPedals; e++)
  {
    const int id = chaingrid::kFirstExtraPedal + e;
    const PedalDef& base = kPedals[chaingrid::PedalType(id)];
    gExtraPedalNames[e] = std::string(base.name) + " " + std::to_string(chaingrid::PedalCopy(id) + 1);
    const int p0 = kExtraPedalParams0 + ExtraPedalParamOffset(e);
    PedalDef& d = gExtraPedals[e];
    d.name = gExtraPedalNames[e].c_str();
    d.activeParam = p0;
    d.engagedParam = p0 + 1;
    d.slotParam = -1; // pre-1.17 states never had these, so there's nothing to migrate
    int next = p0 + 2;
    for (int k = 0; k < 4; k++)
    {
      d.knobs[k].label = base.knobs[k].label;
      d.knobs[k].paramIdx = base.knobs[k].paramIdx >= 0 ? next++ : -1;
    }
    assert(next - p0 - 2 == kMultiPedalKnobs[chaingrid::PedalType(id)]);
  }
  return true;
}
static const bool gExtraPedalDefsReady = _InitExtraPedalDefs();

// The pedals added after the extra copies (see chaingrid::kFirstLatePedal), by id - kFirstLatePedal.
static const PedalDef kLatePedals[chaingrid::kNumLatePedals] = {
  {"FLANGER", kFlangerActive, kFlangerEngaged, -1,
   {{"Rate", kFlangerRate}, {"Depth", kFlangerDepth}, {"Regen", kFlangerRegen}, {"Mix", kFlangerMix}}},
  {"PHASER", kPhaserActive, kPhaserEngaged, -1,
   {{"Rate", kPhaserRate}, {"Depth", kPhaserDepth}, {"Regen", kPhaserRegen}, {"Mix", kPhaserMix}}},
  // REAL FEEL: a tandem pedal with the Tube Lab -- the Tube Lab's On switch and Real Feel controls.
  // Laid out separately (see the chaingrid::kRealFeel case in the popup-building pass).
  {"REAL FEEL", kRealFeelActive, kTubeEngaged, -1, {{"Depth", kTubeFeelDepth}, {"Mix", kRealFeelMix}, {nullptr, -1}, {nullptr, -1}}},
  // CAB 2 / CAB 3: extra cab IR blocks. Laid out separately (IR picker; see the ExtraCabIndex case
  // in the popup-building pass).
  {"CAB 2", kCab2Active, kCab2Engaged, -1, {{"Level", kCab2Level}, {"Pan", kCab2Pan}, {nullptr, -1}, {nullptr, -1}}},
  {"CAB 3", kCab3Active, kCab3Engaged, -1, {{"Level", kCab3Level}, {"Pan", kCab3Pan}, {nullptr, -1}, {nullptr, -1}}},
  // 1.44.0 (NewPedals.h). Laid out separately, in a wider popup (see _IsWidePedal); these four are
  // their main knobs.
  {"SKY VERB", kSkyActive, kSkyEngaged, -1, {{"Decay", kSkyDecay}, {"Tone", kSkyTone}, {"Mod", kSkyMod}, {"Mix", kSkyMix}}},
  {"FLANGE DBL", kFDActive, kFDEngaged, -1, {{"Manual", kFDManual}, {"Width", kFDWidth}, {"Speed", kFDSpeed}, {"Mix", kFDMix}}},
  {"WAH", kWahActive, kWahEngaged, -1, {{"Position", kWahPosition}, {"Q", kWahQ}, {"Sens", kWahSens}, {"Level", kWahLevel}}},
  {"PREC DRIVE", kPDActive, kPDEngaged, -1, {{"Volume", kPDVolume}, {"Drive", kPDDrive}, {"Attack", kPDAttack}, {"Gate", kPDGate}}},
  {"APEX PRE", kApexActive, kApexEngaged, -1, {{"Gain", kApexGain}, {"Bass", kApexBass}, {"Mid", kApexMid}, {"Treble", kApexTreble}}},
  // 1.46.0 (ProEQ.h): laid out separately (the EQ graph; see the chaingrid::kProEq case).
  {"PRO EQ", kProEqActive, kProEqEngaged, -1, {{"Output", kProEqOutput}, {nullptr, -1}, {nullptr, -1}, {nullptr, -1}}},
  // 1.52.0 (Dynamics.h): laid out separately (two rows and a meter; see _IsDynamicsPedal).
  {"GATE", kGateActive, kGateEngaged, -1,
   {{"Threshold", kGateThreshold}, {"Release", kGateRelease}, {"Range", kGateRange}, {nullptr, -1}}},
  {"PRO COMP", kSCompActive, kSCompEngaged, -1,
   {{"Threshold", kSCompThreshold}, {"Ratio", kSCompRatio}, {"Makeup", kSCompMakeup}, {"Mix", kSCompMix}}},
  // 1.66.0 (MorePedals.h): in the wider popup (see _IsWidePedal).
  {"TRACK GATE", kTGActive, kTGEngaged, -1,
   {{"Threshold", kTGThreshold}, {"Release", kTGRelease}, {"Range", kTGRange}, {"Ratio", kTGRatio}}},
  {"DIGI DELAY", kDigiActive, kDigiEngaged, -1,
   {{"Time", kDigiTime}, {"Feedback", kDigiFeedback}, {"Level", kDigiLevel}, {nullptr, -1}}},
  {"ANALOG DLY", kAnaActive, kAnaEngaged, -1, {{"Delay", kAnaTime}, {"Regen", kAnaRegen}, {"Mix", kAnaMix}, {nullptr, -1}}},
  {"TAPE ECHO", kTapeActive, kTapeEngaged, -1,
   {{"Rate", kTapeRate}, {"Intensity", kTapeIntensity}, {"Echo", kTapeEcho}, {"Reverb", kTapeReverb}}},
  {"SPRING VERB", kSprActive, kSprEngaged, -1, {{"Dwell", kSprDwell}, {"Tone", kSprTone}, {"Mix", kSprMix}, {"Decay", kSprDecay}}},
  // 1.67.0: TUBE LAB 2 shares the Tube Lab's popup (see _SetUiLab).
  {"TUBE LAB 2", kLab2Active, LabParam(1, kTubeEngaged), -1, {{nullptr, -1}, {nullptr, -1}, {nullptr, -1}, {nullptr, -1}}},
};

// Any pedal's def, by grid id (an original pedal, an extra copy, or a late pedal).
static const PedalDef& PedalOf(int id)
{
  if (chaingrid::IsLatePedal(id))
    return kLatePedals[id - chaingrid::kFirstLatePedal];
  return chaingrid::IsExtraPedal(id) ? gExtraPedals[id - chaingrid::kFirstExtraPedal] : kPedals[id];
}

// 1.65.0: a control's hover help (ParamHelp.h): the effect pedals' extra copies borrow their original's,
// the EQ bands and SPEAKER LAB mics say which band or mic they are, and every pedal's On switch says so.
static std::string ParamHelpFor(int p)
{
  if (const int base = Lab2Base(p); base >= 0) // 1.67.0: TUBE LAB 2's copy says what TUBE LAB's does
    return ParamHelpFor(base);
  for (int e = 0; e < chaingrid::kNumExtraPedals; e++)
  {
    const PedalDef& d = gExtraPedals[e];
    const PedalDef& base = kPedals[chaingrid::PedalType(chaingrid::kFirstExtraPedal + e)];
    for (int k = 0; k < 4; k++)
      if (d.knobs[k].paramIdx == p && base.knobs[k].paramIdx >= 0)
        if (const char* h = BaseParamHelp(base.knobs[k].paramIdx))
          return std::string(d.name) + " -- " + h;
  }
  if (p >= kProEqBandFirst && p <= kProEqBandLast)
    return EqBandHelp(false, (p - kProEqBandFirst) / 4, (p - kProEqBandFirst) % 4, false);
  if (p >= kOutEqBandFirst && p <= kOutEqBandLast)
    return EqBandHelp(true, (p - kOutEqBandFirst) / 4, (p - kOutEqBandFirst) % 4, false);
  if (p >= kProEqBandExtFirst && p <= kProEqBandExtLast)
    return EqBandHelp(false, (p - kProEqBandExtFirst) / 2, (p - kProEqBandExtFirst) % 2, true);
  if (p >= kOutEqBandExtFirst && p <= kOutEqBandExtLast)
    return EqBandHelp(true, (p - kOutEqBandExtFirst) / 2, (p - kOutEqBandExtFirst) % 2, true);
  if (p >= kLabMicFirst && p <= kLabMicLast)
    return LabMicHelp((p - kLabMicFirst) / kNumLabMicFields, (p - kLabMicFirst) % kNumLabMicFields);
  if (p >= kLabMicCabFirst && p <= kLabMicCabLast)
    return std::string("Mic ") + char('A' + p - kLabMicCabFirst) + " Cab: which cab it's on (with two cabs).";
  if (const char* h = BaseParamHelp(p))
    return h;
  for (int id = 0; id < chaingrid::kNumIds; id++)
    if (chaingrid::IsPedal(id) && PedalOf(id).engagedParam == p)
      return std::string(PedalOf(id).name) + " on/off. Off passes the signal through (delays, reverbs and cabs ring out).";
  return std::string();
}

EMsgBoxResult _ShowMessageBox(iplug::igraphics::IGraphics* pGraphics, const char* str, const char* caption,
                              EMsgBoxType type)
{
#ifdef OS_MAC
  // macOS is backwards?
  return pGraphics->ShowMessageBox(caption, str, type);
#else
  return pGraphics->ShowMessageBox(str, caption, type);
#endif
}

const std::string kCalibrateInputParamName = "CalibrateInput";
const bool kDefaultCalibrateInput = false;
const std::string kInputCalibrationLevelParamName = "InputCalibrationLevel";
const double kDefaultInputCalibrationLevel = 12.0;


NeuralAmpModeler::NeuralAmpModeler(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  _InitToneStack();
  nam::activations::Activation::enable_fast_tanh();
  _LoadUiPrefs(); // colour theme and spectrum strip (1.48.0), before the UI is built
  mProEqView.onAnalyzerChanged = mOutEqView.onAnalyzerChanged = [this]() { _SaveUiPrefs(); };
  mTubeStage[0].SetProbe(&mTubeProbe); // Tube Lab debug page / CSV log (captures only while wanted)
#ifndef APP_API
  // Only the non-APP builds declare the extra aux bus (see PLUG_CHANNEL_IO) that Loop Send/Return
  // actually use -- labeling it here is purely cosmetic (shows up in a host's routing UI) but
  // guarding it avoids implying APP/standalone has a loop bus it doesn't.
  SetChannelLabel(ERoute::kInput, 0, "Main L");
  SetChannelLabel(ERoute::kInput, 1, "Main R");
  SetChannelLabel(ERoute::kInput, 2, "Loop Return L");
  SetChannelLabel(ERoute::kInput, 3, "Loop Return R");
  SetChannelLabel(ERoute::kOutput, 0, "Main L");
  SetChannelLabel(ERoute::kOutput, 1, "Main R");
  SetChannelLabel(ERoute::kOutput, 2, "Loop Send L");
  SetChannelLabel(ERoute::kOutput, 3, "Loop Send R");
#endif
  GetParam(kInputLevel)->InitGain("Input", 0.0, -20.0, 20.0, 0.1);
  GetParam(kToneBass)->InitDouble("Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneMid)->InitDouble("Middle", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneTreble)->InitDouble("Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -80.0, -100.0, 0.0, 0.1);
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kEQActive)->InitBool("ToneStack", true);
  GetParam(kOutputMode)->InitEnum("OutputMode", 1, {"Raw", "Normalized", "Calibrated"}); // TODO DRY w/ control
  GetParam(kIRToggle)->InitBool("IRToggle", true);
  GetParam(kCalibrateInput)->InitBool(kCalibrateInputParamName.c_str(), kDefaultCalibrateInput);
  GetParam(kInputCalibrationLevel)
    ->InitDouble(kInputCalibrationLevelParamName.c_str(), kDefaultInputCalibrationLevel, -60.0, 60.0, 0.1, "dBu");
  GetParam(kSlim)->InitDouble("Slim", 0.0, 0.0, 1.0, 0.01);
  GetParam(kABSlot)->InitEnum("ABSlot", 0, {"A", "B"});
  GetParam(kDensity)->InitDouble("Density", 0.0, -10.0, 10.0, 0.1, "dB");
  GetParam(kDensityActive)->InitBool("DensityActive", true);
  GetParam(kPresence)->InitDouble("Boost", 0.0, -10.0, 10.0, 0.1, "dB");
  GetParam(kPresenceActive)->InitBool("BoostActive", true);

  // AMP and CAB are draggable blocks, same as pedals -- see kAmpSlotMin/Max for why AMP's own
  // range never goes negative (keeps it strictly after every NAM-capture pedal, which is
  // hard-mono internally and must never end up on the stereo side of AMP's position).
  GetParam(kAmpSlot)->InitInt("AmpSlot", 0, kAmpSlotMin, kAmpSlotMax);
  GetParam(kCabSlot)->InitInt("CabSlot", kMaxDraggablePedalSlot, kPedalSlotMin, kMaxDraggablePedalSlot);
  // Defaults put Send just before CAB and Return right after it -- a sensible starting "loop
  // around the cab" position, though (like everything else) it's just a starting point to drag
  // from.
  GetParam(kLoopSendSlot)->InitInt("LoopSendSlot", kMaxDraggablePedalSlot - 2, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kLoopReturnSlot)
    ->InitInt("LoopReturnSlot", kMaxDraggablePedalSlot - 1, kPedalSlotMin, kMaxDraggablePedalSlot);

  // Pedalboard. Slot: negative = pre-amp (more negative = earlier), positive = effects loop
  // (more positive = later). Defaults put Compressor/Overdrive pre-amp, Chorus/Delay/Reverb in the
  // loop. Mix (0-10, default 10 = fully wet) blends dry/processed signal for every pedal.
  GetParam(kCompressorActive)->InitBool("CompressorActive", false);
  GetParam(kCompressorEngaged)->InitBool("CompressorEngaged", true);
  GetParam(kCompressorSlot)->InitInt("CompressorSlot", -2, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kCompressorSustain)->InitDouble("Sustain", 5.0, 0.0, 10.0, 0.1);
  GetParam(kCompressorLevel)->InitDouble("CompLevel", 0.0, -20.0, 20.0, 0.1, "dB");
  GetParam(kCompressorMix)->InitDouble("CompMix", 10.0, 0.0, 10.0, 0.1);
  GetParam(kOverdriveActive)->InitBool("OverdriveActive", false);
  GetParam(kOverdriveEngaged)->InitBool("OverdriveEngaged", true);
  GetParam(kOverdriveSlot)->InitInt("OverdriveSlot", -1, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kOverdriveDrive)->InitDouble("Drive", 3.0, 0.0, 10.0, 0.1);
  GetParam(kOverdriveTone)->InitDouble("Tone", 5.0, 0.0, 10.0, 0.1);
  GetParam(kOverdriveLevel)->InitDouble("ODLevel", 0.0, -20.0, 20.0, 0.1, "dB");
  GetParam(kOverdriveMix)->InitDouble("ODMix", 10.0, 0.0, 10.0, 0.1);
  GetParam(kChorusActive)->InitBool("ChorusActive", false);
  GetParam(kChorusEngaged)->InitBool("ChorusEngaged", true);
  GetParam(kChorusSlot)->InitInt("ChorusSlot", 1, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kChorusRate)->InitDouble("Rate", 0.8, 0.05, 5.0, 0.01, "Hz");
  GetParam(kChorusDepth)->InitDouble("Depth", 5.0, 0.0, 10.0, 0.1);
  GetParam(kChorusMix)->InitDouble("ChorusMix", 5.0, 0.0, 10.0, 0.1);
  GetParam(kDelayActive)->InitBool("DelayActive", false);
  GetParam(kDelayEngaged)->InitBool("DelayEngaged", true);
  GetParam(kDelaySlot)->InitInt("DelaySlot", 2, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kDelayTime)->InitDouble("Time", 350.0, 10.0, 2000.0, 1.0, "ms");
  GetParam(kDelayFeedback)->InitDouble("Feedback", 3.0, 0.0, 10.0, 0.1);
  GetParam(kDelayMix)->InitDouble("Mix", 3.0, 0.0, 10.0, 0.1);
  GetParam(kReverbActive)->InitBool("ReverbActive", false);
  GetParam(kReverbEngaged)->InitBool("ReverbEngaged", true);
  GetParam(kReverbSlot)->InitInt("ReverbSlot", 3, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kReverbSize)->InitDouble("Size", 5.0, 0.0, 10.0, 0.1);
  GetParam(kReverbMix)->InitDouble("ReverbMix", 3.0, 0.0, 10.0, 0.1);
  GetParam(kEqPedalActive)->InitBool("EqPedalActive", false);
  GetParam(kEqPedalEngaged)->InitBool("EqPedalEngaged", true);
  GetParam(kEqPedalSlot)->InitInt("EqPedalSlot", 4, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kEqPedalLow)->InitDouble("EQ Low", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEqPedalMid)->InitDouble("EQ Mid", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEqPedalHigh)->InitDouble("EQ High", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kEqPedalLevel)->InitDouble("EQ Level", 0.0, -20.0, 20.0, 0.1, "dB");
  GetParam(kTremoloActive)->InitBool("TremoloActive", false);
  GetParam(kTremoloEngaged)->InitBool("TremoloEngaged", true);
  GetParam(kTremoloSlot)->InitInt("TremoloSlot", 5, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kTremoloRate)->InitDouble("Rate", 5.0, 0.5, 12.0, 0.01, "Hz");
  GetParam(kTremoloDepth)->InitDouble("Depth", 5.0, 0.0, 10.0, 0.1);
  GetParam(kTremoloMix)->InitDouble("TremoloMix", 10.0, 0.0, 10.0, 0.1);

  // NAM-file pedals: downloaded captures instead of a coded algorithm. Mono-only, so the Slot
  // range is restricted to pre-amp positions (kNamPedalSlotMin..kNamPedalSlotMax = -1).
  GetParam(kNamPedal0Active)->InitBool("NamPedal0Active", false);
  GetParam(kNamPedal0Engaged)->InitBool("NamPedal0Engaged", true);
  GetParam(kNamPedal0Slot)->InitInt("NamPedal0Slot", -3, kNamPedalSlotMin, kNamPedalSlotMax);
  GetParam(kNamPedal0Level)->InitDouble("NamPedal0Level", 0.0, -20.0, 20.0, 0.1, "dB");
  GetParam(kNamPedal0Mix)->InitDouble("NamPedal0Mix", 10.0, 0.0, 10.0, 0.1);
  GetParam(kNamPedal1Active)->InitBool("NamPedal1Active", false);
  GetParam(kNamPedal1Engaged)->InitBool("NamPedal1Engaged", true);
  GetParam(kNamPedal1Slot)->InitInt("NamPedal1Slot", -4, kNamPedalSlotMin, kNamPedalSlotMax);
  GetParam(kNamPedal1Level)->InitDouble("NamPedal1Level", 0.0, -20.0, 20.0, 0.1, "dB");
  GetParam(kNamPedal1Mix)->InitDouble("NamPedal1Mix", 10.0, 0.0, 10.0, 0.1);

  // Tube Lab: 12AX7 stage. Any slot (pre-amp or loop); Bloom/Bright/Mix use the same 0-10 scale as
  // the other pedals' knobs.
  const tube::TubeParams tubeDefaults;
  GetParam(kTubeActive)->InitBool("TubeActive", false);
  GetParam(kTubeEngaged)->InitBool("TubeEngaged", true);
  GetParam(kTubeSlot)->InitInt("TubeSlot", -5, kPedalSlotMin, kMaxDraggablePedalSlot);
  GetParam(kTubeDrive)->InitDouble("TubeDrive", tubeDefaults.driveDB, 0.0, 36.0, 0.1, "dB");
  GetParam(kTubeBias)->InitDouble("TubeBias", tubeDefaults.biasV, -3.0, -0.3, 0.01, "V");
  GetParam(kTubeBloom)->InitDouble("TubeBloom", tubeDefaults.bloom * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTubeBright)->InitDouble("TubeBright", tubeDefaults.bright * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTubeMix)->InitDouble("TubeMix", tubeDefaults.mix * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTubeOutput)->InitGain("TubeOutput", tubeDefaults.outputDB, -24.0, 12.0, 0.1);
  // Circuit dynamics: cathode bias (Rk, bypass-cap time) and supply sag (Rs, reservoir time).
  // Engine + circuit parts. Cap and Supply span two decades, so they get an exponential taper.
  GetParam(kTubeCircuit)->InitBool("TubeCircuit", tubeDefaults.circuit);
  // Capacitors are stepped selectors (new names, so older continuous values aren't misread).
  GetParam(kTubeCathodeCap)
    ->InitEnum("TubeCathodeCapSel", 5, {"None", "0.68 uF", "1 uF", "2.2 uF", "10 uF", "25 uF"});
  GetParam(kTubePlate)->InitDouble("TubePlate", tubeDefaults.plateKOhm, 47.0, 220.0, 1.0, "kOhm");
  GetParam(kTubePlateCap)->InitEnum("TubePlateCapSel", 0, {"None", "47 pF", "100 pF", "220 pF", "470 pF", "1 nF"});
  GetParam(kTubeSagOn)->InitBool("TubeSagOn", tubeDefaults.sag);
  GetParam(kTubeSagSupply)
    ->InitDouble("TubeSagSupply", tubeDefaults.sagSupplyKOhm, 4.7, 470.0, 0.1, "kOhm", 0, "", IParam::ShapeExp());
  GetParam(kTubeSagTime)->InitDouble("TubeSagTime", tubeDefaults.sagTimeS, 0.05, 2.0, 0.01, "s");
  // Second, cascaded stage. Gain 2 and Couple use the same 0-10 scale as the other pedal knobs.
  GetParam(kTubeStage2)->InitBool("TubeStage2", tubeDefaults.stage2);
  GetParam(kTubeGain2)->InitDouble("TubeGain2", tubeDefaults.gain2 * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTubeBias2)->InitDouble("TubeBias2", tubeDefaults.bias2V, -3.0, -0.3, 0.01, "V");
  GetParam(kTubeCap2)->InitEnum("TubeCap2Sel", 1, {"None", "0.68 uF", "1 uF", "2.2 uF", "10 uF", "25 uF"});
  GetParam(kTubePlateCap2)
    ->InitEnum("TubePlateCap2Sel", 4, {"None", "47 pF", "100 pF", "220 pF", "470 pF", "1 nF"});
  GetParam(kTubeCouple)->InitEnum("TubeCoupleSel", 3, {"2.2 nF", "4.7 nF", "10 nF", "22 nF", "47 nF", "100 nF"});
  // Power amp. Knobs on the same 0-10 scale as the other pedals.
  GetParam(kTubePower)->InitBool("TubePower", tubeDefaults.powerAmp);
  GetParam(kTubeMaster)->InitDouble("TubeMaster", tubeDefaults.master * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTubePowerSag)->InitDouble("TubePowerSag", tubeDefaults.powerSag * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTubeFeedback)->InitDouble("TubeFeedback", tubeDefaults.feedback * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTubePresence)->InitDouble("TubePresence", tubeDefaults.presence * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTube6L6)->InitBool("Tube6L6", tubeDefaults.sixL6);
  GetParam(kTubeSpeakerLoad)->InitBool("TubeSpeakerLoad", tubeDefaults.speakerLoad);
  GetParam(kTubeScreenSquish)->InitBool("TubeScreenSquish", tubeDefaults.screenSquish);
  GetParam(kTubeSharedSupply)->InitBool("TubeSharedSupply", tubeDefaults.sharedSupply);
  GetParam(kTubeOTSaturation)->InitBool("TubeOTSaturation", tubeDefaults.otSaturation);
  GetParam(kTubeCoilHeat)->InitBool("TubeCoilHeat", tubeDefaults.coilHeat);
  GetParam(kTubeFeelDepth)->InitDouble("TubeFeelDepth", tubeDefaults.feelDepth, 1.0, 10.0, 0.1, "x");
  GetParam(kTubeEconomy)->InitBool("TubeEconomy", tubeDefaults.economy);
  GetParam(kTubeTouch)->InitDouble("TubeTouch", tubeDefaults.touch, 0.0, 10.0, 0.1);
  GetParam(kTubeRipple)->InitBool("TubeRipple", tubeDefaults.ripple);
  GetParam(kTubeRippleAmount)->InitDouble("TubeRippleAmount", tubeDefaults.rippleAmount, 0.0, 10.0, 0.1);
  GetParam(kTubeRippleMix)->InitDouble("TubeRippleMix", tubeDefaults.rippleMix * 100.0, 0.0, 100.0, 1.0, "%");
  GetParam(kTubeCathodeFollower)->InitBool("TubeCathodeFollower", tubeDefaults.cathodeFollower);
  GetParam(kTubeCathodeBias)->InitBool("TubeCathodeBias", tubeDefaults.cathodeBias);
  GetParam(kTubeSpeakerFeedback)->InitBool("TubeSpeakerFeedback", tubeDefaults.speakerFeedback);
  GetParam(kTubeResonance)->InitDouble("TubeResonance", tubeDefaults.resonance * 10.0, 0.0, 10.0, 0.1);
  GetParam(kTubeHighQuality)->InitBool("TubeHighQuality", tubeDefaults.highQuality);

  // PRO EQ and the Output EQ (1.46.0, ProEQ.h): 8 bands of {On, Freq, Gain, Q} each.
  {
    auto initEq = [this](int first, const char* prefix) {
      for (int b = 0; b < proeq::kNumBands; b++)
      {
        const proeq::Band d = proeq::DefaultBand(b);
        const std::string n = std::string(prefix) + " B" + std::to_string(b + 1);
        GetParam(EqBandParam(first, b, 0))->InitBool((n + " On").c_str(), false);
        GetParam(EqBandParam(first, b, 1))->InitFrequency((n + " Freq").c_str(), d.freq, proeq::kMinHz, proeq::kMaxHz, 1.0);
        GetParam(EqBandParam(first, b, 2))
          ->InitDouble((n + " Gain").c_str(), 0.0, -proeq::kMaxGainDB, proeq::kMaxGainDB, 0.1, "dB");
        GetParam(EqBandParam(first, b, 3))
          ->InitDouble((n + " Q").c_str(), d.q, proeq::kMinQ, proeq::kMaxQ, 0.01, "", 0, "", IParam::ShapeExp());
      }
    };
    GetParam(kProEqActive)->InitBool("ProEqActive", false);
    GetParam(kProEqEngaged)->InitBool("ProEqEngaged", true);
    GetParam(kProEqOutput)->InitDouble("ProEqOutput", 0.0, -18.0, 18.0, 0.1, "dB");
    initEq(kProEqBandFirst, "ProEq");
    GetParam(kOutEqOn)->InitBool("OutEqOn", true);
    GetParam(kOutEqOutput)->InitDouble("OutEqOutput", 0.0, -18.0, 18.0, 0.1, "dB");
    initEq(kOutEqBandFirst, "OutEq");
    // 1.47.0: Trim, and each band's Type and Slope.
    auto initExt = [this](int first, const char* prefix) {
      for (int b = 0; b < proeq::kNumBands; b++)
      {
        const std::string n = std::string(prefix) + " B" + std::to_string(b + 1);
        GetParam(EqBandExtParam(first, b, 0))
          ->InitEnum((n + " Type").c_str(), (int)proeq::DefaultType(b),
                     {"Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch"});
        GetParam(EqBandExtParam(first, b, 1))
          ->InitEnum((n + " Slope").c_str(), 0, {"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"});
      }
    };
    GetParam(kProEqTrim)->InitDouble("ProEqTrim", 0.0, -18.0, 18.0, 0.1, "dB");
    GetParam(kOutEqTrim)->InitDouble("OutEqTrim", 0.0, -18.0, 18.0, 0.1, "dB");
    initExt(kProEqBandExtFirst, "ProEq");
    initExt(kOutEqBandExtFirst, "OutEq");
  }
  // GATE and PRO COMP (1.52.0, Dynamics.h).
  {
    GetParam(kGateActive)->InitBool("GateActive", false);
    GetParam(kGateEngaged)->InitBool("GateEngaged", true);
    GetParam(kGateThreshold)->InitDouble("GateThreshold", -60.0, -100.0, 0.0, 0.1, "dBFS");
    GetParam(kGateHysteresis)->InitDouble("GateClose", 6.0, 0.0, 20.0, 0.1, "dB");
    GetParam(kGateAttack)
      ->InitDouble("GateAttack", 0.5, 0.05, 50.0, 0.01, "ms", 0, "", IParam::ShapePowCurve(3.0));
    GetParam(kGateHold)->InitDouble("GateHold", 60.0, 0.0, 500.0, 1.0, "ms", 0, "", IParam::ShapePowCurve(2.0));
    GetParam(kGateRelease)
      ->InitDouble("GateRelease", 150.0, 5.0, 2000.0, 1.0, "ms", 0, "", IParam::ShapePowCurve(3.0));
    GetParam(kGateRange)->InitDouble("GateRange", -80.0, -90.0, 0.0, 0.5, "dB");
    GetParam(kGateRange)->SetDisplayText(-90.0, "Silent");
    GetParam(kGateKey)->InitBool("GateKeyInput", true);
    GetParam(kGateKeyHpf)->InitFrequency("GateKeyHpf", 20.0, 20.0, 500.0, 1.0);
    GetParam(kGateKeyHpf)->SetDisplayText(20.0, "Off");
    GetParam(kSCompActive)->InitBool("ProCompActive", false);
    GetParam(kSCompEngaged)->InitBool("ProCompEngaged", true);
    GetParam(kSCompThreshold)->InitDouble("ProCompThreshold", -24.0, -60.0, 0.0, 0.1, "dBFS");
    GetParam(kSCompRatio)->InitDouble("ProCompRatio", 4.0, 1.0, 20.0, 0.1, ":1", 0, "", IParam::ShapePowCurve(2.0));
    GetParam(kSCompRatio)->SetDisplayText(20.0, "Limit");
    GetParam(kSCompAttack)
      ->InitDouble("ProCompAttack", 5.0, 0.05, 100.0, 0.01, "ms", 0, "", IParam::ShapePowCurve(3.0));
    GetParam(kSCompRelease)
      ->InitDouble("ProCompRelease", 120.0, 5.0, 2000.0, 1.0, "ms", 0, "", IParam::ShapePowCurve(3.0));
    GetParam(kSCompKnee)->InitDouble("ProCompKnee", 6.0, 0.0, 24.0, 0.1, "dB");
    GetParam(kSCompMakeup)->InitDouble("ProCompMakeup", 0.0, -12.0, 24.0, 0.1, "dB");
    GetParam(kSCompAutoMakeup)->InitBool("ProCompAutoGain", true);
    GetParam(kSCompMix)->InitDouble("ProCompMix", 100.0, 0.0, 100.0, 0.1, "%");
    GetParam(kSCompStyle)->InitEnum("ProCompStyle", 0, {"Clean", "Opto", "FET"});
    GetParam(kSCompScHpf)->InitFrequency("ProCompScHpf", 20.0, 20.0, 500.0, 1.0);
    GetParam(kSCompScHpf)->SetDisplayText(20.0, "Off");
  }
  // 1.66.0 pedals (MorePedals.h).
  {
    GetParam(kTGActive)->InitBool("TrackGateActive", false);
    GetParam(kTGEngaged)->InitBool("TrackGateEngaged", true);
    GetParam(kTGModel)->InitEnum("TrackGateModel", 0, {"Tracking", "Classic", "Smart"});
    GetParam(kTGThreshold)->InitDouble("TrackGateThreshold", -55.0, -90.0, -20.0, 0.1, "dBFS");
    GetParam(kTGRelease)->InitDouble("TrackGateRelease", 4.0, 0.0, 10.0, 0.1);
    GetParam(kTGHold)->InitDouble("TrackGateHold", 20.0, 0.0, 500.0, 1.0, "ms", 0, "", IParam::ShapePowCurve(2.0));
    GetParam(kTGRange)->InitDouble("TrackGateRange", -90.0, -90.0, 0.0, 0.5, "dB");
    GetParam(kTGRange)->SetDisplayText(-90.0, "Silent");
    GetParam(kTGRatio)->InitDouble("TrackGateRatio", 20.0, 1.5, 20.0, 0.1, ":1", 0, "", IParam::ShapePowCurve(2.0));
    GetParam(kTGRatio)->SetDisplayText(20.0, "Gate");
    GetParam(kTGNoise)->InitEnum("TrackGateNoise", 2, {"Hiss", "Mid", "Full"});
    GetParam(kTGKey)->InitBool("TrackGateKey", false);
    GetParam(kDigiActive)->InitBool("DigiDelayActive", false);
    GetParam(kDigiEngaged)->InitBool("DigiDelayEngaged", true);
    GetParam(kDigiMode)->InitEnum("DigiDelayMode", 0, {"Standard", "Mod", "Warm", "Reverse"});
    GetParam(kDigiTime)->InitDouble("DigiDelayTime", 350.0, 20.0, 1200.0, 1.0, "ms", 0, "", IParam::ShapePowCurve(2.0));
    GetParam(kDigiFeedback)->InitDouble("DigiDelayFeedback", 4.0, 0.0, 10.0, 0.1);
    GetParam(kDigiLevel)->InitDouble("DigiDelayLevel", 5.0, 0.0, 10.0, 0.1);
    GetParam(kAnaActive)->InitBool("AnalogDelayActive", false);
    GetParam(kAnaEngaged)->InitBool("AnalogDelayEngaged", true);
    GetParam(kAnaTime)->InitDouble("AnalogDelayTime", 300.0, 20.0, 600.0, 1.0, "ms", 0, "", IParam::ShapePowCurve(1.5));
    GetParam(kAnaRegen)->InitDouble("AnalogDelayRegen", 4.0, 0.0, 10.0, 0.1);
    GetParam(kAnaMix)->InitDouble("AnalogDelayMix", 5.0, 0.0, 10.0, 0.1);
    GetParam(kAnaMod)->InitBool("AnalogDelayMod", false);
    GetParam(kAnaRate)->InitDouble("AnalogDelayRate", 0.8, 0.1, 8.0, 0.01, "Hz", 0, "", IParam::ShapePowCurve(2.0));
    GetParam(kAnaWidth)->InitDouble("AnalogDelayWidth", 4.0, 0.0, 10.0, 0.1);
    GetParam(kTapeActive)->InitBool("TapeEchoActive", false);
    GetParam(kTapeEngaged)->InitBool("TapeEchoEngaged", true);
    GetParam(kTapeHeads)->InitEnum("TapeEchoHeads", 0, {"1", "2", "3", "1+2", "2+3", "1+2+3", "1+3"});
    GetParam(kTapeRate)->InitDouble("TapeEchoRate", 5.0, 0.0, 10.0, 0.1);
    GetParam(kTapeIntensity)->InitDouble("TapeEchoIntensity", 4.0, 0.0, 10.0, 0.1);
    GetParam(kTapeEcho)->InitDouble("TapeEchoLevel", 5.0, 0.0, 10.0, 0.1);
    GetParam(kTapeReverb)->InitDouble("TapeEchoReverb", 0.0, 0.0, 10.0, 0.1);
    GetParam(kTapeWow)->InitDouble("TapeEchoWow", 3.0, 0.0, 10.0, 0.1);
    GetParam(kTapeDrive)->InitDouble("TapeEchoDrive", 3.0, 0.0, 10.0, 0.1);
    GetParam(kSprActive)->InitBool("SpringVerbActive", false);
    GetParam(kSprEngaged)->InitBool("SpringVerbEngaged", true);
    GetParam(kSprDwell)->InitDouble("SpringVerbDwell", 5.0, 0.0, 10.0, 0.1);
    GetParam(kSprTone)->InitDouble("SpringVerbTone", 5.0, 0.0, 10.0, 0.1);
    GetParam(kSprMix)->InitDouble("SpringVerbMix", 3.0, 0.0, 10.0, 0.1);
    GetParam(kSprDecay)->InitDouble("SpringVerbDecay", 5.0, 0.0, 10.0, 0.1);
  }

  // Tap tempo (1.71.0).
  GetParam(kTempoBPM)->InitDouble("TempoBPM", 120.0, 40.0, 300.0, 0.1, "BPM");
  {
    const char* names[] = {"DelayTapDiv", "Delay2TapDiv", "Delay3TapDiv", "DigiDelayTapDiv", "AnalogDelayTapDiv",
                           "TapeEchoTapDiv"};
    for (int k = 0; k < 6; k++)
      GetParam(kDelayTapDiv + k)->InitEnum(names[k], 1, {"Off", "1/4", "1/8.", "1/8", "1/4T", "1/16"});
  }

  // CAB LAB. Defaults: a 4x12 Greenback, an SM57 close and slightly off the cap panned a bit left,
  // an R-121 ribbon a little farther back panned a bit right, and a touch of room.
  {
    const cab::CabLabParams cabDefaults;
    GetParam(kCabLabActive)->InitBool("CabLabActive", false);
    GetParam(kCabLabEngaged)->InitBool("CabLabEngaged", true);
    GetParam(kCabLabSlot)->InitInt("CabLabSlot", kMaxDraggablePedalSlot - 3, kPedalSlotMin, kMaxDraggablePedalSlot);
    GetParam(kCabSource)->InitBool("CabSourceIR", cabDefaults.useIR);
    GetParam(kCabSpeaker)
      ->InitEnum("CabSpeaker", cabDefaults.speaker,
                 {"4x12 V30", "4x12 Greenback", "4x12 G12H-30", "2x12 Alnico Blue", "1x12 Jensen P12", "1x12 EVM12L",
                  "4x10 Jensen P10R"});
    auto initMic = [this](int type, int pos, int dist, int level, int pan, const cab::MicParams& d, const char* tag) {
      const std::string t(tag);
      GetParam(type)->InitEnum((t + "Type").c_str(), d.type,
                               {"SM57", "SM7B", "Beta 57A", "R-121 Ribbon", "MD421", "U87 Condenser"});
      GetParam(pos)->InitDouble((t + "Pos").c_str(), d.position * 10.0, 0.0, 10.0, 0.1);
      GetParam(dist)->InitDouble((t + "Dist").c_str(), d.distanceM * 100.0, 0.5, 100.0, 0.1, "cm", 0, "",
                                 IParam::ShapeExp());
      GetParam(level)->InitGain((t + "Level").c_str(), d.levelDB, -24.0, 6.0, 0.1);
      GetParam(pan)->InitDouble((t + "Pan").c_str(), d.pan * 100.0, -100.0, 100.0, 1.0, "%");
    };
    initMic(kMicAType, kMicAPos, kMicADist, kMicALevel, kMicAPan, cabDefaults.micA, "MicA");
    GetParam(kMicBOn)->InitBool("MicBOn", cabDefaults.micB.on);
    initMic(kMicBType, kMicBPos, kMicBDist, kMicBLevel, kMicBPan, cabDefaults.micB, "MicB");
    GetParam(kMicBInvert)->InitBool("MicBInvert", cabDefaults.micB.invert);
    GetParam(kRoomLevel)->InitGain("RoomLevel", cabDefaults.roomLevelDB, -60.0, 0.0, 0.1);
    GetParam(kRoomSize)->InitDouble("RoomSize", cabDefaults.roomSize * 10.0, 0.0, 10.0, 0.1);
    GetParam(kRoomWidth)->InitDouble("RoomWidth", cabDefaults.width * 100.0, 0.0, 100.0, 1.0, "%");
    GetParam(kCabOutput)->InitGain("CabOutput", cabDefaults.outputDB, -24.0, 12.0, 0.1);
    // SPEAKER LAB (1.59.0). A new CAB LAB starts in 3D; older states load as classic (see
    // _ResetChainGridParams).
    GetParam(kCabLab3D)->InitBool("CabLab3D", true);
    GetParam(kCabFormat)->InitEnum("CabFormat", cabDefaults.format, {"4x12", "2x12", "1x12", "4x10"});
    for (int m = 0; m < cab::kNumLabMics; m++)
    {
      const cab::LabMic& d = cabDefaults.lab[m];
      const std::string t = std::string("LabMic") + char('A' + m);
      GetParam(LabMicParam(m, 0))->InitBool((t + "On").c_str(), d.on);
      GetParam(LabMicParam(m, 1))
        ->InitEnum((t + "Type").c_str(), d.type,
                   {"SM57", "SM7B", "Beta 57A", "R-121 Ribbon", "MD421", "U87 Condenser", "e609", "e906", "M160 Ribbon",
                    "C414 Condenser"});
      GetParam(LabMicParam(m, 2))->InitDouble((t + "X").c_str(), d.x * 100.0, -50.0, 50.0, 0.1, "cm");
      GetParam(LabMicParam(m, 3))->InitDouble((t + "Y").c_str(), d.y * 100.0, -50.0, 50.0, 0.1, "cm");
      GetParam(LabMicParam(m, 4))
        ->InitDouble((t + "Z").c_str(), d.z * 100.0, 0.0, 150.0, 0.1, "cm", 0, "", IParam::ShapePowCurve(3.0));
      GetParam(LabMicParam(m, 5))->InitGain((t + "Level").c_str(), d.levelDB, -24.0, 6.0, 0.1);
      GetParam(LabMicParam(m, 6))->InitDouble((t + "Pan").c_str(), d.pan * 100.0, -100.0, 100.0, 1.0, "%");
      GetParam(LabMicParam(m, 7))->InitBool((t + "Invert").c_str(), d.invert);
      GetParam(kLabMicCabFirst + m)->InitEnum((t + "Cab").c_str(), d.cab, {"Cab 1", "Cab 2"});
    }
    // 1.61.0: dual cabs.
    GetParam(kCabDual)->InitEnum("CabDual", cabDefaults.dual ? 1 : 0, {"Single", "Dual"});
    GetParam(kCab2Format)->InitEnum("Cab2Format", cabDefaults.format2, {"4x12", "2x12", "1x12", "4x10"});
    GetParam(kCab2Speaker)
      ->InitEnum("Cab2Speaker", cabDefaults.speaker2,
                 {"4x12 V30", "4x12 Greenback", "4x12 G12H-30", "2x12 Alnico Blue", "1x12 Jensen P12", "1x12 EVM12L",
                  "4x10 Jensen P10R"});
    GetParam(kCab2SourceIR)->InitBool("Cab2SourceIR", cabDefaults.useIR2);
  }
  GetParam(kTubeCanFail)->InitBool("TubeCanFail", false); // 1.62.0
  GetParam(kTubeCapture)->InitBool("TubeCaptureAmp", false); // 1.64.0
  // 1.68.0: the chassis sockets V5, V6 (named "TubeSock5Role" etc.) and the quad switch (TUBE LAB 2 copies
  // them below).
  GetParam(kTubeQuad)->InitBool("TubeQuad", false);
  {
    const tube::SocketParams d;
    for (int k = 0; k < tube::kNumSockets; k++)
    {
      const std::string n = "TubeSock" + std::to_string(5 + k);
      GetParam(SocketParam(k, kSockRole))->InitEnum((n + "Role").c_str(), d.role, {"Off", "Gain", "Follower", "Boost"});
      GetParam(SocketParam(k, kSockTube))->InitEnum((n + "Tube").c_str(), d.tube, {"12AX7", "12AT7", "12AU7", "5751"});
      GetParam(SocketParam(k, kSockGain))->InitDouble((n + "Gain").c_str(), d.gain * 10.0, 0.0, 10.0, 0.1);
      GetParam(SocketParam(k, kSockBias))->InitDouble((n + "Bias").c_str(), d.biasV, -8.0, -0.3, 0.01, "V");
      GetParam(SocketParam(k, kSockCap))
        ->InitEnum((n + "CapSel").c_str(), 1, {"None", "0.68 uF", "1 uF", "2.2 uF", "10 uF", "25 uF"});
      GetParam(SocketParam(k, kSockPlateCap))
        ->InitEnum((n + "PlateCapSel").c_str(), 4, {"None", "47 pF", "100 pF", "220 pF", "470 pF", "1 nF"});
      GetParam(SocketParam(k, kSockCouple))
        ->InitEnum((n + "CoupleSel").c_str(), 3, {"2.2 nF", "4.7 nF", "10 nF", "22 nF", "47 nF", "100 nF"});
    }
  }

  // Signal-chain grid: not automatable (they're the routing, not sound settings). The defaults are
  // "unset"; _MigrateLegacyChain lays the Slot params' order out on the grid.
  GetParam(kChainGridV1)->InitBool("ChainGridV1", false, "", IParam::kFlagCannotAutomate);
  // The cable masks need more bits than an int param holds (32 since 1.28.0, more since 1.34.0), so
  // their params are stepped doubles (exact for whole numbers up to 2^53). States saved with the old
  // int params, or narrower masks, load the same: they're whole numbers either way.
  constexpr double kMaxMask = (double)((1ll << chaingrid::kNumIds) - 1);
  constexpr int kMaskFlags = IParam::kFlagCannotAutomate | IParam::kFlagStepped;
  for (int b = 0; b < chaingrid::kNumIds; b++)
  {
    if (b == chaingrid::kInput)
      continue;
    // Named by grid id (not param slot), so the names 1.17 saved keep meaning the same blocks.
    const std::string n = std::to_string(b);
    GetParam(GridCellParam(b))
      ->InitInt(("GridCell" + n).c_str(), -1, -1, chaingrid::kNumCells - 1, "", IParam::kFlagCannotAutomate);
    GetParam(GridInParam(b))
      ->InitDouble(("GridIn" + n).c_str(), 0.0, 0.0, kMaxMask, 1.0, "", (IParam::EFlags)kMaskFlags);
  }
  GetParam(kGridOut)->InitDouble("GridOut", 0.0, 0.0, kMaxMask, 1.0, "", (IParam::EFlags)kMaskFlags);
  GetParam(kAmpEngaged)->InitBool("AmpEngaged", true);
  // Off by default, so presets from before the tone stack sound as they did.
  GetParam(kTubeStack)->InitEnum("TubeStack", 0, {"Off", "Fender", "Marshall"});
  GetParam(kTubeBass)->InitDouble("TubeBass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kTubeMid)->InitDouble("TubeMid", 5.0, 0.0, 10.0, 0.1);
  GetParam(kTubeTreble)->InitDouble("TubeTreble", 5.0, 0.0, 10.0, 0.1);
  // Flanger: a slow, deep sweep with some regen and an even mix (the deepest notches) by default.
  GetParam(kFlangerActive)->InitBool("FlangerActive", false);
  GetParam(kFlangerEngaged)->InitBool("FlangerEngaged", true);
  GetParam(kFlangerRate)->InitDouble("Flanger Rate", 0.25, 0.05, 5.0, 0.01, "Hz", 0, "", IParam::ShapeExp());
  GetParam(kFlangerDepth)->InitDouble("Flanger Depth", 7.0, 0.0, 10.0, 0.1);
  GetParam(kFlangerRegen)->InitDouble("Flanger Regen", 5.0, -10.0, 10.0, 0.1);
  GetParam(kFlangerMix)->InitDouble("Flanger Mix", 5.0, 0.0, 10.0, 0.1);
  // Phaser: a medium sweep with a little regen and an even mix by default.
  GetParam(kPhaserActive)->InitBool("PhaserActive", false);
  GetParam(kPhaserEngaged)->InitBool("PhaserEngaged", true);
  GetParam(kPhaserRate)->InitDouble("Phaser Rate", 0.5, 0.05, 8.0, 0.01, "Hz", 0, "", IParam::ShapeExp());
  GetParam(kPhaserDepth)->InitDouble("Phaser Depth", 7.0, 0.0, 10.0, 0.1);
  GetParam(kPhaserRegen)->InitDouble("Phaser Regen", 3.0, -10.0, 10.0, 0.1);
  GetParam(kPhaserMix)->InitDouble("Phaser Mix", 5.0, 0.0, 10.0, 0.1);
  // REAL FEEL pedal: only whether it's in the chain (its other controls are the Tube Lab's).
  GetParam(kRealFeelActive)->InitBool("RealFeelActive", false);
  GetParam(kRealFeelMix)->InitDouble("RealFeelMix", 100.0, 0.0, 100.0, 1.0, "%");
  GetParam(kHumFilter)->InitBool("HumFilter", false);
  GetParam(kOutputLimiter)->InitBool("OutputLimiter", false);
  GetParam(kOutputMono)->InitBool("OutputMono", false);
  // 1.58.0: the Tube Lab bench (debug page) and the OUTPUT page.
  GetParam(kTubeVariac)->InitDouble("TubeVariac", 120.0, 90.0, 135.0, 0.5, "V");
  GetParam(kTubeBiasPot)->InitDouble("TubeBiasPot", 100.0, 50.0, 150.0, 1.0, "%");
  GetParam(kTubePreampAge)->InitDouble("TubePreampAge", 0.0, 0.0, 100.0, 1.0, "%");
  GetParam(kTubePowerAge)->InitDouble("TubePowerAge", 0.0, 0.0, 100.0, 1.0, "%");
  GetParam(kLimiterCeiling)->InitDouble("LimiterCeiling", -1.0, -12.0, -1.0, 0.1, "dB");
  GetParam(kLimiterRelease)->InitDouble("LimiterRelease", OutputLimiter::kReleaseS * 1000.0, 5.0, 500.0, 1.0, "ms", 0, "",
                                        IParam::ShapeExp());
  GetParam(kOutputWidth)->InitDouble("OutputWidth", 100.0, 0.0, 200.0, 1.0, "%");
  GetParam(kOutputWiden)->InitDouble("OutputWiden", 0.0, 0.0, 100.0, 1.0, "%");
  // CAB 2 / CAB 3: on, unity, centred by default.
  for (int k = 0; k < kNumExtraCabs; k++)
  {
    const std::string n = "Cab" + std::to_string(k + 2);
    const int p0 = k == 0 ? kCab2Active : kCab3Active;
    GetParam(p0)->InitBool((n + "Active").c_str(), false);
    GetParam(p0 + 1)->InitBool((n + "Engaged").c_str(), true);
    GetParam(p0 + 2)->InitGain((n + " Level").c_str(), 0.0, -24.0, 12.0, 0.1);
    GetParam(p0 + 3)->InitDouble((n + " Pan").c_str(), 0.0, -100.0, 100.0, 1.0, "%");
  }
  // On, fully wet by default: the loop behaves as it did before these existed.
  GetParam(kLoopEngaged)->InitBool("LoopEngaged", true);
  GetParam(kLoopMix)->InitDouble("LoopMix", 100.0, 0.0, 100.0, 1.0, "%");

  // 1.44.0 pedals (NewPedals.h). Not in the chain by default, on when placed.
  GetParam(kSkyActive)->InitBool("SkyVerbActive", false);
  GetParam(kSkyEngaged)->InitBool("SkyVerbEngaged", true);
  GetParam(kSkyMode)->InitEnum("SkyVerb Mode", 0, {"Hall", "Plate", "Room", "Shimmer", "Cloud", "Bloom"});
  GetParam(kSkyDecay)->InitDouble("SkyVerb Decay", 5.0, 0.0, 10.0, 0.1);
  GetParam(kSkyPredelay)->InitDouble("SkyVerb Predelay", 20.0, 0.0, 500.0, 1.0, "ms");
  GetParam(kSkyTone)->InitDouble("SkyVerb Tone", 6.0, 0.0, 10.0, 0.1);
  GetParam(kSkyMod)->InitDouble("SkyVerb Mod", 3.0, 0.0, 10.0, 0.1);
  GetParam(kSkyMix)->InitDouble("SkyVerb Mix", 3.5, 0.0, 10.0, 0.1);
  GetParam(kSkyFreeze)->InitBool("SkyVerb Freeze", false);
  GetParam(kFDActive)->InitBool("FlangeDblActive", false);
  GetParam(kFDEngaged)->InitBool("FlangeDblEngaged", true);
  GetParam(kFDDoubler)->InitBool("FlangeDbl Doubler", false);
  GetParam(kFDManual)->InitDouble("FlangeDbl Manual", 5.0, 0.0, 10.0, 0.1);
  GetParam(kFDWidth)->InitDouble("FlangeDbl Width", 6.0, 0.0, 10.0, 0.1);
  GetParam(kFDSpeed)->InitDouble("FlangeDbl Speed", 0.4, 0.05, 5.0, 0.01, "Hz", 0, "", IParam::ShapeExp());
  GetParam(kFDRegen)->InitDouble("FlangeDbl Regen", 3.0, -10.0, 10.0, 0.1);
  GetParam(kFDMix)->InitDouble("FlangeDbl Mix", 5.0, 0.0, 10.0, 0.1);
  GetParam(kWahActive)->InitBool("WahActive", false);
  GetParam(kWahEngaged)->InitBool("WahEngaged", true);
  GetParam(kWahModel)->InitEnum("Wah Model", 0, {"Cry Baby", "V847", "Auto"});
  GetParam(kWahPosition)->InitDouble("Wah Position", 5.0, 0.0, 10.0, 0.1);
  GetParam(kWahQ)->InitDouble("Wah Q", 5.0, 0.0, 10.0, 0.1);
  GetParam(kWahSens)->InitDouble("Wah Sens", 5.0, 0.0, 10.0, 0.1);
  GetParam(kWahLevel)->InitDouble("Wah Level", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kPDActive)->InitBool("PrecDriveActive", false);
  GetParam(kPDEngaged)->InitBool("PrecDriveEngaged", true);
  GetParam(kPDVolume)->InitDouble("PrecDrive Volume", -10.0, -30.0, 10.0, 0.1, "dB");
  GetParam(kPDDrive)->InitDouble("PrecDrive Drive", 5.0, 0.0, 10.0, 0.1);
  GetParam(kPDAttack)->InitDouble("PrecDrive Attack", 5.0, 0.0, 10.0, 0.1);
  GetParam(kPDGate)->InitDouble("PrecDrive Gate", 0.0, 0.0, 10.0, 0.1);
  GetParam(kPDBright)->InitBool("PrecDrive Bright", false);
  GetParam(kApexActive)->InitBool("ApexActive", false);
  GetParam(kApexEngaged)->InitBool("ApexEngaged", true);
  GetParam(kApexGain)->InitDouble("Apex Gain", 6.0, 0.0, 10.0, 0.1);
  GetParam(kApexBass)->InitDouble("Apex Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kApexMid)->InitDouble("Apex Mid", 5.0, 0.0, 10.0, 0.1);
  GetParam(kApexTreble)->InitDouble("Apex Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kApexPresence)->InitDouble("Apex Presence", 4.0, 0.0, 10.0, 0.1);
  GetParam(kApexVolume)->InitDouble("Apex Volume", -6.0, -40.0, 10.0, 0.1, "dB");
  GetParam(kApexBright)->InitBool("Apex Bright", false);

  // Extra pedal copies: the original's knobs (same ranges and defaults), as "Reverb2 Size" etc.
  {
    static const char* const kTypeParamNames[chaingrid::kNumMultiPedalTypes] = {
      "Compressor", "Overdrive", "Chorus", "Delay", "Reverb", "EqPedal", "Tremolo"};
    for (int e = 0; e < chaingrid::kNumExtraPedals; e++)
    {
      const int id = chaingrid::kFirstExtraPedal + e;
      const int type = chaingrid::PedalType(id);
      const PedalDef& def = PedalOf(id);
      const std::string prefix = kTypeParamNames[type] + std::to_string(chaingrid::PedalCopy(id) + 1);
      GetParam(def.activeParam)->InitBool((prefix + "Active").c_str(), false);
      GetParam(def.engagedParam)->InitBool((prefix + "Engaged").c_str(), true);
      for (int k = 0; k < 4; k++)
      {
        if (def.knobs[k].paramIdx < 0)
          continue;
        const IParam* src = GetParam(kPedals[type].knobs[k].paramIdx);
        GetParam(def.knobs[k].paramIdx)
          ->InitDouble((prefix + " " + def.knobs[k].label).c_str(), src->GetDefault(), src->GetMin(), src->GetMax(),
                       src->GetStep(), src->GetLabel());
      }
    }
  }
  // TUBE LAB 2 (1.67.0): a copy of each Tube Lab param (same range, default and display texts), named
  // Lab2Tube*. Last, after every Tube Lab param is set up (each one's name starts with "Tube").
  GetParam(kLab2Active)->InitBool("Lab2Active", false);
  for (int i = 0; i < kNumLabParams; i++)
    GetParam(kLab2First + i)->Init(*GetParam(kTubeLabParams[i]), "Tube", "Lab2Tube");
  for (int p = kTubeChassisFirst; p <= kTubeChassisLast; p++) // 1.68.0: and of the chassis's
    GetParam(LabParam(1, p))->Init(*GetParam(p), "Tube", "Lab2Tube");
  _MigrateLegacyChain();

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#else
    auto scaleFactor = 1.0f;
#endif

    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, scaleFactor);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    // Rebuild the whole UI fresh on every resize so all rects are recomputed against the current
    // window size (this is what actually makes the layout scale -- without this, and without
    // layoutOnResize=true below, the window frame resizes but the content inside stays frozen).
    pGraphics->RemoveAllControls();
    _RebuildStyles();
    // RemoveAllControls() just destroyed every control this layout previously created, but these
    // block-selection member vectors/pointers live on this (long-lived) plugin object, not the
    // (rebuilt every resize) IGraphics control stack -- without clearing them here they'd
    // accumulate dangling pointers from past layouts on top of the fresh ones below, and the next
    // block click would Hide() a freed control and crash.
    for (int i = 0; i < chaingrid::kNumIds; i++)
      mPedalDetailControls[i].clear();
    for (int i = 0; i < chaingrid::kNumIds; i++)
      mPedalChips[i] = nullptr;
    mCabDetailControls.clear();
    mLabOnly[0].clear();
    mLabOnly[1].clear();
    mLabRetarget.clear();
    mCabButton = nullptr;
    mSpecStripCtls.clear();
    mAmpChip = nullptr;
    mAmpDetailControls.clear();
    mLoopSendChip = nullptr;
    mLoopReturnChip = nullptr;
    mLoopDetailControls.clear();
    mClipShown = false; // the OUT label below starts out saying "OUT"
    mBlockHintLabel = nullptr;
    mLatencyLabel = nullptr;
    mLibraryToggleButton = nullptr;
    mBlockPopupBackdrop = nullptr;
    mBlockPopupCardBg = nullptr;
    mBlockPopupCloseButton = nullptr;
    mBlockPopupTitleLabel = nullptr;
    pGraphics->AttachCornerResizer(EUIResizerMode::Size, true);
    pGraphics->AttachTextEntryControl();
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->LoadFont("Michroma-Regular", MICHROMA_FN);

    const auto gearSVG = pGraphics->LoadSVG(GEAR_FN);
    const auto fileSVG = pGraphics->LoadSVG(FILE_FN);
    const auto globeSVG = pGraphics->LoadSVG(GLOBE_ICON_FN);
    const auto crossSVG = pGraphics->LoadSVG(CLOSE_BUTTON_FN);
    const auto rightArrowSVG = pGraphics->LoadSVG(RIGHT_ARROW_FN);
    const auto leftArrowSVG = pGraphics->LoadSVG(LEFT_ARROW_FN);
    const auto modelIconSVG = pGraphics->LoadSVG(MODEL_ICON_FN);
    const auto irIconOnSVG = pGraphics->LoadSVG(IR_ICON_ON_FN);
    const auto irIconOffSVG = pGraphics->LoadSVG(IR_ICON_OFF_FN);
    const auto slimIconSVG = pGraphics->LoadSVG(SLIMMABLE_ICON_FN);

    const auto backgroundBitmap = pGraphics->LoadBitmap(BACKGROUND_FN); // Settings page backdrop
    const auto logoBitmap = pGraphics->LoadBitmap(LOGO_FN);
    const auto fileBackgroundBitmap = pGraphics->LoadBitmap(FILEBACKGROUND_FN);
    const auto inputLevelBackgroundBitmap = pGraphics->LoadBitmap(INPUTLEVELBACKGROUND_FN);
    const auto linesBitmap = pGraphics->LoadBitmap(LINES_FN);
    const auto knobBackgroundBitmap = pGraphics->LoadBitmap(KNOBBACKGROUND_FN);
    const auto switchHandleBitmap = pGraphics->LoadBitmap(SLIDESWITCHHANDLE_FN);

    const auto b = pGraphics->GetBounds();
    // Everything below scales off of b's actual current size (not fixed pixel constants), clamped to
    // sane min/max so controls stay usable at the smallest window and don't balloon absurdly at the largest.
    const auto marginPx = std::clamp(std::min(b.W(), b.H()) * 0.045f, 12.0f, 34.0f);
    const auto mainArea = b.GetPadded(-marginPx);
    // Collapsible: when toggled off, this is 0 and the content area below reclaims the space.
    const auto libraryPanelWidth = mShowLibraryPanel ? std::clamp(b.W() * 0.24f, 170.0f, 320.0f) : 0.0f;
    const auto libraryPanelArea = mainArea.GetFromRight(libraryPanelWidth);
    const auto contentPadPx = marginPx * 0.5f;
    const auto contentArea =
      mainArea.GetReducedFromRight(libraryPanelWidth + marginPx).GetPadded(-contentPadPx);

    // Misc Areas
    const auto settingsButtonArea = CornerButtonArea(b);
    // Header buttons, right to left from the gear: TUNE, the panel arrow, SAVE, LOAD. Wide, short
    // pills with smaller text (square buttons made the words fill them edge to edge).
    auto headerButtonLeftOf = [&settingsButtonArea](const IRECT& r, float w, float gap) {
      return IRECT(r.L - gap - w, settingsButtonArea.MH() - 13.f, r.L - gap, settingsButtonArea.MH() + 13.f);
    };
    const auto tunerButtonArea = headerButtonLeftOf(settingsButtonArea, 54.f, 10.f);
    const auto libraryToggleButtonArea = headerButtonLeftOf(tunerButtonArea, 30.f, 6.f);
#ifdef APP_API
    const auto savePresetButtonArea = headerButtonLeftOf(libraryToggleButtonArea, 54.f, 6.f);
    const auto loadPresetButtonArea = headerButtonLeftOf(savePresetButtonArea, 54.f, 6.f);
    const auto midiLearnButtonArea = headerButtonLeftOf(loadPresetButtonArea, 54.f, 6.f);
#else
    const auto midiLearnButtonArea = headerButtonLeftOf(libraryToggleButtonArea, 54.f, 6.f);
#endif
    const auto tapButtonArea = headerButtonLeftOf(midiLearnButtonArea, 44.f, 6.f); // 1.71.0: tap tempo
    const auto tempoArea = headerButtonLeftOf(tapButtonArea, 86.f, 4.f).GetVPadded(-3.f); // As tall as the buttons look
    const IVStyle headerButtonStyle = style.WithLabelText(IText(12.f, EAlign::Center, PluginColors::NAM_ONACCENT))
                                        .WithValueText(IText(12.f, EAlign::Center, PluginColors::NAM_ONACCENT));
    const auto tunerPanelArea = b.GetPadded(-40.f);

    const auto titleHeight = std::clamp(contentArea.H() * 0.115f, 32.0f, 58.0f);
    // Stops short of the header buttons -- with the AMP NEST panel hidden the content area runs
    // under them, and the A/B switch ended up on top of TUNE.
    const float headerButtonsL = tempoArea.L;
    auto titleArea = contentArea.GetFromTop(titleHeight);
    titleArea.R = std::min(titleArea.R, headerButtonsL - 12.f);
    const auto abSwitchArea = titleArea.GetFromRight(std::clamp(contentArea.W() * 0.11f, 70.0f, 110.0f)).GetPadded(-10.0f);
    const auto titleLabelArea = titleArea.GetReducedFromRight(abSwitchArea.W() + 10.0f);

    // The main screen IS the signal chain: a grid of blocks (AMP, CAB, the FX loop and pedals)
    // wired with cables; clicking a block opens its popup.
    const auto rackOuterArea = contentArea.GetReducedFromTop(titleHeight + contentArea.H() * 0.02f);
    // IN and OUT meters: horizontal bars side by side along the bottom, each with its caption to
    // its left (OUT is a stereo pair, L over R).
    const float meterStripH = 30.f, meterCaptionW = 34.f;
    const auto meterStrip = contentArea.GetFromBottom(meterStripH);
    const auto inputMeterArea = IRECT(meterStrip.L + meterCaptionW, meterStrip.T, meterStrip.MW() - 14.f, meterStrip.B);
    const auto outputMeterArea =
      IRECT(meterStrip.MW() + 14.f + meterCaptionW, meterStrip.T, meterStrip.R, meterStrip.B);

    // The signal-chain grid (ChainGrid.h), above the meters, centred vertically in its space.
    // The output spectrum strip (1.46.0) sits just above the meters; the grid fits above that.
    const float smallStripH = mSpecFloating ? 0.f : !mShowSpecStrip ? 20.f : std::clamp(contentArea.H() * 0.12f, 50.f, 90.f);
    const float specStripH = mSpecStripExpanded && mShowSpecStrip && !mSpecFloating
                               ? std::clamp(contentArea.H() * 0.32f, 110.f, 300.f)
                               : smallStripH;
    const auto specStripArea = IRECT(contentArea.L, meterStrip.T - 10.f - specStripH, contentArea.R, meterStrip.T - 10.f);
    // Pedal popups stay above the strip (_LiftAboveStrip); the big cards above its small size.
    mSpecStripRect = specStripH > 0.f ? specStripArea : IRECT();
    mSpecStripSmallRect = smallStripH > 0.f ? IRECT(contentArea.L, meterStrip.T - 10.f - smallStripH, contentArea.R,
                                                    meterStrip.T - 10.f)
                                            : IRECT();
    const auto gridFitArea =
      rackOuterArea.GetHPadded(-12.f).GetReducedFromBottom(meterStripH + 12.f + specStripH + 10.f);
    const float gridSlack =
      std::max(0.f, gridFitArea.H() - NAMChainGridGeometry(gridFitArea, gridFitArea.H()).grid.H());
    const NAMChainGridGeometry gridGeom(gridFitArea.GetReducedFromTop(gridSlack * 0.5f), gridFitArea.H());
    const auto chainArea = IRECT(rackOuterArea.L, gridGeom.grid.T, rackOuterArea.R, gridGeom.grid.B);

    // Pedal/CAB popup "window": a floating card over the rack, shown while a pedal chip (or CAB)
    // is selected -- its own dedicated controls window, dismissed via its own close button or by
    // clicking the dimmed backdrop behind it.
    const auto popupCardArea = _ComputePedalPopupCardArea(b);
    const auto popupHeaderArea = popupCardArea.GetFromTop(32.f).GetPadded(-6.f);
    const auto popupCloseArea = popupHeaderArea.GetFromRight(22.f);
    const auto popupLabelArea = popupHeaderArea.GetReducedFromRight(28.f);
    const auto pedalPopupBodyArea = popupCardArea.GetReducedFromTop(38.f).GetPadded(-10.f);

    // The Amp's own popup: shares the same card/backdrop/title/close chrome as a pedal's (resized
    // per-block in _SelectBlock), but much bigger and a different shape -- wide and short, like a
    // real amp head's chassis, rather than a small centered square. It needs to fit all 8 knobs,
    // two toggle rows, the model/IR loaders (including the stereo R-channel IR controls), and the
    // meters, none of which fit in the compact shared pedal/cab card.
    //
    // User-resizable via the grip in its corner (see NAMResizeGripControl below): mAmpPopupWidth/
    // Height persist across relayouts. 0 means "never set" (first layout ever), so compute a wide/
    // short default then; every layout after that just re-clamps whatever's stored against the
    // CURRENT window, since the window itself can be resized independently of this (host corner
    // resizer) and a stored size larger than a since-shrunk window would otherwise overflow it.
    if (mAmpPopupWidth <= 0.f || mAmpPopupHeight <= 0.f)
    {
      // Pure percentage of the current window, no absolute pixel cap -- a fixed cap (previously
      // 640x300) looks fine at the default 900x500 plugin size but leaves the popup looking tiny
      // and lost in the middle of a window the corner-resizer has been dragged much bigger, since
      // the window can grow up to PLUG_MAX_WIDTH/HEIGHT (3x the default in each dimension).
      mAmpPopupWidth = b.W() * 0.82f;
      mAmpPopupHeight = b.H() * 0.5f;
    }
    // std::clamp is UB if lo > hi -- guard against a window narrower/shorter than the minimum
    // (possible transiently, or with an unusually small host-resized window) by never letting the
    // ceiling drop below the floor.
    mAmpPopupWidth = std::clamp(mAmpPopupWidth, kAmpPopupMinWidth, std::max(kAmpPopupMinWidth, b.W() - 16.f));
    mAmpPopupHeight = std::clamp(mAmpPopupHeight, kAmpPopupMinHeight, std::max(kAmpPopupMinHeight, b.H() - 16.f));
    const auto ampPopupCardArea = _ComputeAmpPopupCardArea(b);
    const auto ampPopupBodyArea = ampPopupCardArea.GetReducedFromTop(38.f).GetPadded(-10.f);
    const auto ampResizeGripArea =
      IRECT(ampPopupCardArea.R - 22.f, ampPopupCardArea.B - 22.f, ampPopupCardArea.R - 4.f, ampPopupCardArea.B - 4.f);

    // The amp's own controls, laid out within its (now much bigger) popup body instead of the
    // always-visible strip under the rack they used to occupy.
    const auto ampControlsArea = ampPopupBodyArea;

    // Amp controls are laid out bottom-up so the stack always fits inside ampControlsArea: the
    // model/IR loader rows are pinned to the bottom and the knob row takes whatever is left above
    // them. The previous version sized two knob rows off fixed fractions of the area's height and
    // overran it -- the Density/Boost toggles ended up painted on top of the "Select model" row.
    const auto knobsPad = std::clamp(ampControlsArea.W() * 0.022f, 8.0f, 24.0f);
    const auto singleKnobPad = -2.0f;

    // The model loader row, pinned to the bottom of the amp controls area. (The IR loader that used
    // to sit under it lives in CAB's popup since 1.24.0, so there's just the one row.)
    const auto fileWidth = std::clamp(ampControlsArea.W() * 0.34f, 150.0f, 260.0f);
    const auto fileHeight = std::clamp(ampControlsArea.H() * 0.09f, 22.0f, 36.0f);
    const auto fileRowsHeight = fileHeight + 8.0f;

    // Everything above the loader rows: one row of knobs with a strip of toggles beneath it.
    // Density and Boost used to sit on a second row, but ampControlsArea has never been tall
    // enough for two full rows (knob + label + value + toggle is ~120px each), which is what
    // forced the overlap above. All eight share one row now, in signal order.
    // Both reservations below are capped as a fraction of what's actually available, so a host that
    // hands us a window smaller than PLUG_MIN_* still yields a positive knob row rather than an
    // inverted rect -- shrink everything proportionally instead of overlapping, which is the whole
    // point of laying this out bottom-up.
    const auto knobStackArea =
      ampControlsArea.GetReducedFromBottom(std::min(2.f * fileRowsHeight + 8.0f, ampControlsArea.H() * 0.6f));
    // Floor of 38: NAMSwitchControl draws its handle at the switch bitmap's own (fixed, logical)
    // height rather than a fraction of its bounds, so a shorter zone than this leaves no room
    // underneath for the South-oriented label and the pill paints over its own caption.
    const auto toggleZoneHeight =
      std::min(std::clamp(knobStackArea.H() * 0.28f, 38.0f, 46.0f), knobStackArea.H() * 0.45f);
    const auto knobRowHeight = knobStackArea.H() - toggleZoneHeight;
    const auto knobsArea =
      knobStackArea.GetFromTop(knobRowHeight).GetReducedFromLeft(knobsPad).GetReducedFromRight(knobsPad);

    // Column indices for that row. Deliberately independent of EParams' ordering, which used to
    // double as the column index (hence the "these need to be the first ones" comment on the enum)
    // and so could never describe a row holding anything but the original six knobs.
    enum EKnobColumn
    {
      kColInput = 0,
      kColThreshold,
      kColBass,
      kColMid,
      kColTreble,
      kColDensity,
      kColBoost,
      kColOutput,
      kNumKnobColumns
    };
    auto knobCell = [&](int col) {
      return knobsArea.GetGridCell(0, col, 1, kNumKnobColumns).GetPadded(-singleKnobPad);
    };
    // Toggles hang directly under their own knob's column, so each switch reads as belonging to
    // the knob above it.
    auto toggleUnder = [&](const IRECT& knobArea) {
      return IRECT(knobArea.L, knobArea.B, knobArea.R, knobArea.B + toggleZoneHeight).GetPadded(-2.0f);
    };

    const auto inputKnobArea = knobCell(kColInput);
    const auto noiseGateArea = knobCell(kColThreshold);
    const auto bassKnobArea = knobCell(kColBass);
    const auto midKnobArea = knobCell(kColMid);
    const auto trebleKnobArea = knobCell(kColTreble);
    const auto densityKnobArea = knobCell(kColDensity);
    const auto presenceKnobArea = knobCell(kColBoost);
    const auto outputKnobArea = knobCell(kColOutput);

    const auto ngToggleArea = toggleUnder(noiseGateArea);
    const auto eqToggleArea = toggleUnder(midKnobArea);
    const auto densityToggleArea = toggleUnder(densityKnobArea);
    const auto presenceToggleArea = toggleUnder(presenceKnobArea);

    const auto modelArea =
      ampControlsArea.GetFromBottom(2.f * fileRowsHeight).GetFromTop(fileHeight).GetMidHPadded(fileWidth).GetVShifted(-1);
    // 1.70.2: the PACK dropdown, the same size as the model dropdown, one row under it (the bottom row).
    const auto packArea = modelArea.GetVShifted(fileRowsHeight);
    const auto slimIconArea =
      IRECT(modelArea.R + 6.f, modelArea.MH() - 14.f, modelArea.R + 6.f + 2.f * 28.f, modelArea.MH() + 14.f);
    const auto modelIconArea = modelArea.GetFromLeft(30).GetTranslated(-40, 10);


    // Model loader button
    auto loadModelCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        // Sets mNAMPath and mStagedNAM
        const std::string msg = _StageModel(fileName);
        // TODO error messages like the IR loader.
        if (msg.size())
        {
          std::stringstream ss;
          ss << "Failed to load the model. Message:\n\n" << msg;
          _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
        }
        std::cout << "Loaded: " << fileName.Get() << std::endl;
      }
    };

#ifdef APP_API
    // Save/Load Preset (standalone only -- a DAW build's state lives in the host's own project
    // file; these write/read a raw SerializeState chunk straight to disk, which would conflict
    // with how a DAW manages plugin state).
    auto savePresetCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
        _SaveStateToFile(fileName.Get());
    };
    auto loadPresetCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
        _LoadStateFromFile(fileName.Get());
    };
#endif

    // IR loader button
    auto loadIRCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        mIRPath = fileName;
        const dsp::wav::LoadReturnCode retCode = _StageIR(fileName);
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);

          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load IR!", kMB_OK);
        }
      }
    };

    // NAM-file pedal loader (downloaded captures, e.g. Tone3000 stompbox/pedal .nam files).
    // Returns a completion handler bound to a specific pedal slot (0 or 1).
    auto loadNamPedalCompletionHandler = [&](int slot) {
      return [this, slot](const WDL_String& fileName, const WDL_String& path) {
        if (fileName.GetLength())
        {
          const std::string msg = _StageNamPedal(slot, fileName);
          if (msg.size())
          {
            std::stringstream ss;
            ss << "Failed to load pedal model. Message:\n\n" << msg;
            _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load pedal!", kMB_OK);
          }
          else
          {
            // Loading a file also turns the pedal on, so it's immediately audible/visible in the
            // pedalboard chain rack rather than sitting loaded-but-inactive.
            const int activeParam = slot == 0 ? kNamPedal0Active : kNamPedal1Active;
            SendParameterValueFromUI(activeParam, 1.0);
          }
        }
      };
    };

    // Right-channel IR loader (stereo cab). No dedicated icon, so a plain text button.
    auto loadIRRCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        const dsp::wav::LoadReturnCode retCode = _StageIR_R(fileName);
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load right-channel IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);
          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load IR!", kMB_OK);
        }
      }
    };

    // Plain dark background (the art's own base tone), no big watermark: the Griffin emblem is now
    // a small logo (Logo.png, cut from Background.jpg) in the top-left corner of the title bar,
    // with the title centred in the space to its right so the two never overlap.
    // Theme (1.48.0): the colour comes from Colors.h; the line art is light-on-dark, so it's left off
    // in light mode.
    pGraphics->AttachControl(new NAMPanelControl(b, PluginColors::NAM_BG, 0.f));
    if (!PluginColors::NAM_LIGHT)
      pGraphics->AttachControl(new IBitmapControl(b, linesBitmap));

    const float logoSize = titleArea.H() - 8.f;
    const auto logoArea = titleArea.GetFromLeft(logoSize + 8.f).GetCentredInside(logoSize, logoSize);
    // IBitmapControl draws at the bitmap's native size, so scale the logo to fit the title bar.
    pGraphics->AttachControl(new ILambdaControl(
      logoArea,
      [logoBitmap](ILambdaControl*, IGraphics& g, IRECT& r) {
        if (PluginColors::NAM_LIGHT) // the emblem is light art
          g.FillEllipse(IColor(255, 34, 33, 36), r.GetPadded(2.f));
        g.DrawFittedBitmap(logoBitmap, r);
      },
      DEFAULT_ANIMATION_DURATION, false, false, kNoParameter, true));
    pGraphics->AttachControl(
      new IVLabelControl(titleLabelArea.GetReducedFromLeft(logoSize + 8.f), "GRIFFIN AMPLIFICATION", titleStyle));
    // Build version, so it's always clear which build is running (see CHANGELOG.md). Small, out of
    // the way in the window's bottom-right corner.
    pGraphics->AttachControl(new IVLabelControl(
      IRECT(b.R - 90.f, b.B - 16.f, b.R - 6.f, b.B - 2.f), "v" PLUG_VERSION_STR,
      DEFAULT_STYLE
        .WithValueText(IText(10, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.4f), "Roboto-Regular", EAlign::Far))
        .WithDrawFrame(false)));
    // Latency readout, bottom-left: the audio buffer (in and out each add one buffer's worth) and
    // the chain's own processing delay (resampled models, Tube Lab oversampling, path alignment).
    // Refreshed from OnIdle when either changes.
    mLatencyLabel = pGraphics->AttachControl(new ILambdaControl(
      IRECT(b.L + 8.f, b.B - 16.f, b.L + 420.f, b.B - 2.f),
      [this](ILambdaControl*, IGraphics& g, IRECT& r) {
        const double sr = GetSampleRate() > 0. ? GetSampleRate() : 48000.;
        // The audio device's buffer (the standalone app reads it from its settings; a plugin host
        // only tells us the block size it calls us with).
        const int block = mDeviceBufferSize > 0 ? mDeviceBufferSize : GetBlockSize();
        const int dsp = GetLatency();
        const double bufMs = 1000. * block / sr, dspMs = 1000. * dsp / sr;
        char s[200];
        std::snprintf(s, sizeof(s),
                      "Buffer %d @ %.0f kHz = %.1f ms each way   |   Processing delay %.1f ms   |   Round trip about %.1f ms "
                      "+ driver",
                      block, sr / 1000., bufMs, dspMs, 2. * bufMs + dspMs);
        g.DrawText(IText(10, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.45f), "Roboto-Regular", EAlign::Near), s, r);
      },
      DEFAULT_ANIMATION_DURATION, false, false, kNoParameter, true));
    pGraphics->AttachControl(new IVTabSwitchControl(abSwitchArea, kABSlot, {"A", "B"}, "", radioButtonStyle));

    // Everything below through the meters is the amp's own controls -- now living in the amp's
    // own click-to-open popup (see kAmpBlockId / mAmpDetailControls / _SelectBlock), same pattern
    // as a pedal's or CAB's popup contents.
    //
    // Construction stays right here (all the geometry/handlers/bitmaps it needs are already in
    // scope at this point), but actually attaching each control to the graphics context is
    // deferred to a loop further below, run AFTER the popup chrome (mBlockPopupCardBg) and every
    // other popup's own detail content are attached -- attachment order is z-order in this
    // framework, so attaching these here-and-now (before the chips/popup chrome that come later in
    // the function) would leave them at a LOWER z-order than the popup card background and
    // therefore invisible, painted over by it, once opened. This matters far more for the amp than
    // it ever did for a pedal: the amp's popup card is nearly the whole window (see _SelectBlock),
    // not a small centered rectangle a pedal's few knobs could get away with visually escaping.
    // SetTooltip/SetAnimationEndActionFunction (used on a couple of these below) are safe to call
    // on a constructed-but-not-yet-attached IControl -- they only set member fields.
    std::vector<std::tuple<IControl*, int, std::string>> pendingAmpDetails;
    auto addAmpDetail = [&](IControl* c, int tag = -1, const char* group = "") {
      pendingAmpDetails.emplace_back(c, tag, std::string(group));
      return c;
    };

    // Amp-head-styled faceplate, added first so every other amp detail control (attached right
    // after it, in the same deferred group) ends up drawn on top of it instead of behind it.
    addAmpDetail(new NAMAmpFaceplateControl(ampControlsArea));

    // Resize grip: drag to resize the whole popup. Each drag frame just accumulates into the
    // persisted mAmpPopupWidth/Height (cheap); the actual relayout that reflows every knob to fit
    // the new size only happens once, on mouse-up, via the same deferred-relayout mechanism the
    // AMP NEST panel's collapse toggle already uses (mLibraryPanelRelayoutPending).
    addAmpDetail(new NAMResizeGripControl(
      ampResizeGripArea,
      [this](float dX, float dY) {
        float maxW = kAmpPopupMinWidth, maxH = kAmpPopupMinHeight;
        if (auto* pGraphics = GetUI())
        {
          const IRECT winBounds = pGraphics->GetBounds();
          maxW = std::max(kAmpPopupMinWidth, winBounds.W() - 16.f);
          maxH = std::max(kAmpPopupMinHeight, winBounds.H() - 16.f);
        }
        mAmpPopupWidth = std::clamp(mAmpPopupWidth + dX, kAmpPopupMinWidth, maxW);
        mAmpPopupHeight = std::clamp(mAmpPopupHeight + dY, kAmpPopupMinHeight, maxH);
      },
      [this]() { mLibraryPanelRelayoutPending = true; }));

    addAmpDetail(new ISVGControl(modelIconArea, modelIconSVG));

#ifdef NAM_PICK_DIRECTORY
    const std::string defaultNamFileString = "Select model directory...";
    const std::string defaultIRString = "Select IR directory...";
#else
    const std::string defaultNamFileString = "Select model...";
    const std::string defaultIRString = "Select IR...";
#endif
    // Getting started page listing additional resources
    const char* const getUrl = "https://www.neuralampmodeler.com/users#comp-marb84o5";
    addAmpDetail(new NAMFileBrowserControl(modelArea, kMsgTagClearModel, defaultNamFileString.c_str(), "nam",
                                           loadModelCompletionHandler, style, fileSVG, crossSVG, leftArrowSVG,
                                           rightArrowSVG, fileBackgroundBitmap, globeSVG, "Get Amp Models", getUrl),
                 kCtrlTagModelFileBrowser);

    // 1.70.2 PACK PICKER: one dropdown, styled like the model dropdown above it, always shown. Its menu:
    // the pack's captures, the packs in the library, and choosing / rescanning a pack folder.
    addAmpDetail(new NAMPackButtonControl(
                   packArea, [this]() { return _PackLabel(0); }, nullptr,
                   [this](IControl* pCaller) { _PackShowMenu(0, pCaller); },
                   [this](IPopupMenu* pMenu) { _PackOnMenu(pMenu); }, fileBackgroundBitmap, style.valueText))
      ->SetTooltip("PACK: switch between the captures of one capture pack (e.g. an unzipped TONE3000 download). "
                   "Pick a pack from your library, then a capture. Switching keeps the volume level-matched "
                   "(Raw output mode). The model loader above works as before.");
    auto hideSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(true);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(true);
      ui->SetAllControlsDirty();
    };
    auto showSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(false);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(false);
      ui->SetAllControlsDirty();
    };

    addAmpDetail(new NAMSquareButtonControl(slimIconArea, DefaultClickActionFunc, slimIconSVG), kCtrlTagSlimmableIcon)
      ->SetAnimationEndActionFunction(showSlimOverlay);

    // The cab IR's controls are in CAB's own popup (see the CAB popup content further down).
    addAmpDetail(new NAMSwitchControl(toggleUnder(inputKnobArea), kAmpEngaged, "On", style, switchHandleBitmap))
      ->SetTooltip("Amp on/off: off passes the signal straight through the AMP block");
    addAmpDetail(new NAMSwitchControl(ngToggleArea, kNoiseGateActive, "Gate", style, switchHandleBitmap));
    addAmpDetail(new NAMSwitchControl(eqToggleArea, kEQActive, "EQ", style, switchHandleBitmap));
    addAmpDetail(new NAMSwitchControl(toggleUnder(bassKnobArea), kHumFilter, "Hum Filter", style, switchHandleBitmap))
      ->SetTooltip("Hum Filter: notches 60 Hz mains hum and its harmonics (up to 480 Hz) out of your guitar's "
                   "signal, before the gate and the whole chain (works even with the amp off). The notches are "
                   "very narrow, so your notes are barely touched.");

    // Tone3000 library panel: persistent buttons pre-populated from the user's downloaded capture folders,
    // so picking a tone doesn't require the OS file-open dialog every time. Collapsible -- see the
    // toggle button below -- since it eats into the main content area's width while open.
    // Geometry is computed unconditionally (even while collapsed) because the pedal picker list's
    // rects are also where Pass 1 below positions every *inactive* pedal chip -- only the actual
    // AttachControl calls are skipped when the panel's hidden.
    // The gear / TUNE / collapse-arrow buttons are anchored to the window's top-right corner, which
    // is inside this panel -- so the panel's own content starts below them rather than underneath.
    // Without this inset the "AMP NEST" title sat directly behind TUNE and the collapse arrow.
    const auto libraryHeaderInset = CornerButtonArea(b).GetScaledAboutCentre(2.2f).B - libraryPanelArea.T + 6.0f;
    const auto libraryTitleArea = libraryPanelArea.GetReducedFromTop(libraryHeaderInset).GetFromTop(30.0f);
    const auto libraryButtonsArea = libraryPanelArea.GetReducedFromTop(libraryHeaderInset + 40.0f);

    // Fixed-height stacked rows (rather than stretching to fill the whole panel height) so
    // there's real room left below for the pedal picker list.
    const float libRowH = std::clamp(libraryButtonsArea.H() * 0.075f, 28.f, 38.f);
    const float libRowGap = 6.f;
    // 1.44.1: the library buttons go two to a row (AMPS | CAB IRs, PEDAL 1 | PEDAL 2), which
    // leaves the pedal list below room for every pedal.
    const auto libRow0 = libraryButtonsArea.GetFromTop(libRowH);
    const auto libRow1 = libRow0.GetVShifted(libRowH + libRowGap);
    const float libHalfGap = libRowGap * 0.5f;
    const auto ampLibraryButtonArea = libRow0.GetFromLeft(libRow0.W() * 0.5f - libHalfGap);
    const auto irLibraryButtonArea = libRow0.GetFromRight(libRow0.W() * 0.5f - libHalfGap);
    const auto namPedal0ButtonArea = libRow1.GetFromLeft(libRow1.W() * 0.5f - libHalfGap);
    const auto namPedal1ButtonArea = libRow1.GetFromRight(libRow1.W() * 0.5f - libHalfGap);
    const auto getPedalsButtonArea = libRow1.GetVShifted(libRowH + libRowGap);

    // Pedal picker: every pedal (built-in + NAM capture slots) gets a fixed row here, by index --
    // if a pedal's active (in the rack), its row here is simply blank, same as the old tray row
    // used to leave a gap for whichever pedals were in the chain. Drag one out into the rack to
    // activate it. The list is usually taller than the panel has room for, so it scrolls, with a
    // scrollbar down its right side (and the mouse wheel).
    const auto pedalPickerHeaderArea = getPedalsButtonArea.GetVShifted(libRowH + libRowGap);
    const auto pedalPickerListArea =
      IRECT(libraryButtonsArea.L, pedalPickerHeaderArea.B + 4.f, libraryButtonsArea.R, libraryPanelArea.B - 8.f);

    // Laid out as a grid of small pedals, kPedalPickerCols across (full-width rows made each pedal a
    // huge card and only ~3 fit on screen). Room for the scrollbar is always reserved so the cell
    // size doesn't depend on whether the list scrolls.
    // 1.44.1: with 21 pedal types the list ran past the panel at 4 across, so the 1.44.0 pedals sat
    // below the fold ("missing pedals"). The library buttons above now pair up to make room, and the
    // list uses 4 columns, or 5 if 4 won't fit, with rows squashed down to the panel if needed (never
    // under 40); the scrollbar is only a fallback for very short windows. (6 made names unreadable.)
    const float pedalPickerBarW = 10.f;
    const auto pedalPickerRowsArea = pedalPickerListArea.GetReducedFromRight(pedalPickerBarW + 6.f);
    constexpr int kPedalPickerTypes = kNumPedals + chaingrid::kNumLatePedals;
    int kPedalPickerCols = 5;
    for (int cols = 4; cols <= 5; cols++)
    {
      const int rows = (kPedalPickerTypes + cols - 1) / cols;
      if (pedalPickerListArea.H() / rows >= 44.f)
      {
        kPedalPickerCols = cols;
        break;
      }
    }
    const float pedalPickerCellW = pedalPickerRowsArea.W() / kPedalPickerCols;
    const int pedalPickerNumRows = (kPedalPickerTypes + kPedalPickerCols - 1) / kPedalPickerCols;
    const float kPedalPickerRowH = std::max(
      40.f, std::min(std::clamp(pedalPickerCellW * 0.9f, 40.f, 72.f), pedalPickerListArea.H() / pedalPickerNumRows));
    const float pedalPickerContentH = kPedalPickerRowH * pedalPickerNumRows;
    const float pedalPickerMaxScroll = std::max(0.f, pedalPickerContentH - pedalPickerListArea.H());
    mLibraryScrollY = std::clamp(mLibraryScrollY, 0.f, pedalPickerMaxScroll);
    // Each pedal's cell is at its index, moved up by mLibraryScrollY, and clipped to
    // pedalPickerListArea (see the chips' clip getter).
    NAMScrollBarControl* pedalPickerScrollBar = nullptr;
    // 1.60.0: the AMP NEST's LABS (TUBE LAB and CAB LAB, one slot each), the PEDALS button that opens
    // the drawer, and the drawer itself: over the grid's lower part, above the spectrum strip. The
    // pedal list above is no longer shown (its geometry is kept for the panel's other rows).
    auto isLab = [](int id) { return id == kTubePedalIdx || id == chaingrid::kTubeLab2 || id == kCabLabPedalIdx; };
    const float labSlotH = std::clamp(pedalPickerListArea.H() * 0.3f, 56.f, 86.f);
    const IRECT labSlotsArea(pedalPickerListArea.L, pedalPickerHeaderArea.B + 4.f, pedalPickerListArea.R,
                             pedalPickerHeaderArea.B + 4.f + labSlotH);
    auto labSlotRect = [labSlotsArea](int id) {
      // 1.67.0: TUBE LAB, TUBE LAB 2, CAB LAB.
      return labSlotsArea.SubRectHorizontal(3, id == kTubePedalIdx ? 0 : id == chaingrid::kTubeLab2 ? 1 : 2).GetPadded(-4.f);
    };
    const IRECT pedalsButtonArea(pedalPickerListArea.L + 4.f, labSlotsArea.B + 12.f, pedalPickerListArea.R - 4.f,
                                 labSlotsArea.B + 12.f + std::max(32.f, libRowH * 1.15f));
    // 1.61.1: the drawer opens over the AMP NEST panel itself (from its title down), with an X; the
    // grid stays in full view, so it stays open while you drag pedals out. CAB, taken off the grid,
    // waits in its first slot.
    const IRECT drawerRect(libraryPanelArea.L + 6.f, libraryTitleArea.T - 4.f, libraryPanelArea.R - 6.f,
                           libraryPanelArea.B - 6.f);
    const IRECT drawerChipsArea = drawerRect.GetPadded(-8.f).GetReducedFromTop(52.f);
    std::vector<int> drawerTypes;
    for (int t = 0; t < chaingrid::kNumIds; t++) // (The later pedals' types are past kPedalPickerTypes)
      if (chaingrid::IsPedalType(t) && t != chaingrid::PedalType(kTubePedalIdx)
          && t != chaingrid::PedalType(kCabLabPedalIdx) && t != chaingrid::PedalType(chaingrid::kTubeLab2))
        drawerTypes.push_back(t);
    const int drawerCols = 4;
    const int drawerRows = ((int)drawerTypes.size() + 1 + drawerCols - 1) / drawerCols; // (+1: CAB)
    auto drawerSlotRect = [drawerChipsArea, drawerCols, drawerRows](int slot) {
      const float cw = drawerChipsArea.W() / drawerCols;
      const float rh = std::min(drawerChipsArea.H() / drawerRows, cw * 1.25f);
      const float left = drawerChipsArea.L + (slot % drawerCols) * cw, top = drawerChipsArea.T + (slot / drawerCols) * rh;
      return IRECT(left, top, left + cw, top + rh).GetPadded(-3.f);
    };

    if (mShowLibraryPanel)
    {
      const auto libraryDividerArea =
        IRECT(libraryPanelArea.L - 10.f, mainArea.T, libraryPanelArea.L - 9.f, mainArea.B);
      pGraphics->AttachControl(new NAMPanelControl(libraryDividerArea, PluginColors::NAM_THEMECOLOR.WithOpacity(0.25f), 0.f));

      const auto libraryPanelBg = libraryPanelArea.GetPadded(8.0f);
      pGraphics->AttachControl(new NAMPanelControl(libraryPanelBg, PluginColors::NAM_THEMECOLOR.WithOpacity(0.06f)));

      pGraphics->AttachControl(new IVLabelControl(libraryTitleArea, "AMP NEST", titleStyle.WithValueText(
                                                                       IText(16, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular"))));

      // Library folders. These moved into Desktop\Griffin Amplification (the old Desktop\Neural Amp
      // and Desktop\tone folders no longer exist) -- update here if they move again.
      const std::string griffinDir = GriffinLibraryDir();
      const std::vector<std::string> ampLibraryPaths = {griffinDir + "IRs and Amp Profiles (copied)"};
      pGraphics->AttachControl(new NAMPresetBrowserControl(ampLibraryButtonArea, "AMPS", "nam",
                                                            ampLibraryPaths, true, loadModelCompletionHandler, style));

      const std::vector<std::string> irLibraryPaths = {
        griffinDir + "tone", griffinDir + "IRs and Amp Profiles (copied)\\Griffin tone folder captures"};
      pGraphics->AttachControl(new NAMPresetBrowserControl(irLibraryButtonArea, "CAB IRs", "wav", irLibraryPaths,
                                                            false, loadIRCompletionHandler, style));

      // Pedal captures (e.g. Tone3000 stompbox/pedal .nam downloads) share the same source folders
      // as the amp library, since Tone3000 downloads amps and pedals into the same place by default.
      const std::vector<std::string> pedalLibraryPaths = {griffinDir + "IRs and Amp Profiles (copied)",
                                                            griffinDir + "tone"};
      pGraphics->AttachControl(new NAMPresetBrowserControl(
        namPedal0ButtonArea, "PEDAL 1", "nam", pedalLibraryPaths, true, loadNamPedalCompletionHandler(0), style));
      pGraphics->AttachControl(new NAMPresetBrowserControl(
        namPedal1ButtonArea, "PEDAL 2", "nam", pedalLibraryPaths, true, loadNamPedalCompletionHandler(1), style));

      // Jumps out to Tone3000 in the browser to download more pedal captures -- same idea as the
      // "Get Amp Models"/"Get IRs" globe buttons on the main model/IR pickers.
      pGraphics->AttachControl(new IVButtonControl(
        getPedalsButtonArea,
        [](IControl* pCaller) {
          pCaller->GetUI()->OpenURL("https://www.tone3000.com");
        },
        "GET PEDALS (TONE3000)", style));

      pGraphics->AttachControl(new IVLabelControl(
        pedalPickerHeaderArea, "LABS",
        DEFAULT_STYLE.WithValueText(IText(15, IColor(255, 214, 170, 90), "Michroma-Regular")).WithDrawFrame(false)));
      // (The controls the PEDALS drawer covers are in "UNDER_DRAWER": hidden while it's open, so their
      // edges don't show around it.)
      for (int id : {kTubePedalIdx, chaingrid::kTubeLab2, kCabLabPedalIdx})
        pGraphics
          ->AttachControl(new NAMLabSlotControl(
            labSlotRect(id), PedalOf(id).name, id == kCabLabPedalIdx ? 2 : 1,
            [this, id]() { return _ReadChainGrid().Placed(id); },
            [this, id]() { return GetParam(PedalOf(id).engagedParam)->Value() > 0.5; },
            [this, id]() { _SelectBlock(id); }), kNoTag, "UNDER_DRAWER")
          ->SetTooltip(id == kTubePedalIdx
                         ? "TUBE LAB: a tube amp simulated part by part -- preamp triodes, tone stack, phase "
                           "inverter, power tubes and their supply, into a real speaker load."
                       : id == chaingrid::kTubeLab2
                         ? "TUBE LAB 2: a second Tube Lab with its own settings and its own capture (Capture + "
                           "LOAD in its popup), for dual amps: put both on the grid, side by side on two rows."
                         : "CAB LAB: speaker cabs with up to four mics placed in 3D (SPEAKER LAB), and a room.");
      pGraphics
        ->AttachControl(new NAMPedalsButtonControl(
          pedalsButtonArea,
          [this]() {
            // Pedal types with a copy still off the grid (what the drawer shows), and CAB if it's out.
            int n = _ReadChainGrid().Placed(chaingrid::kCab) ? 0 : 1;
            for (int t = 0; t < chaingrid::kNumIds; t++)
            {
              if (!chaingrid::IsPedalType(t) || t == chaingrid::PedalType(kTubePedalIdx)
                  || t == chaingrid::PedalType(kCabLabPedalIdx) || t == chaingrid::PedalType(chaingrid::kTubeLab2))
                continue;
              for (int copy = 0; copy < chaingrid::NumCopies(t); copy++)
              {
                const int id = chaingrid::PedalCopyId(t, copy);
                if (GetParam(PedalOf(id).activeParam)->Value() <= 0.5 || GetParam(GridCellParam(id))->Value() < 0.0)
                {
                  n++;
                  break;
                }
              }
            }
            return n;
          },
          [this]() { return mPedalDrawerOpen; }, [this]() { _SetPedalDrawerOpen(!mPedalDrawerOpen); }),
          kNoTag, "UNDER_DRAWER")
        ->SetTooltip("Open the pedals: drag one onto the grid.");
      pGraphics->AttachControl(new NAMHintTextControl(
        IRECT(pedalPickerListArea.L, pedalsButtonArea.B + 10.f, pedalPickerListArea.R, pedalPickerListArea.B),
        "Drag a lab or a pedal onto the grid; drag it off the grid to take it out. Anything switched off "
        "uses no CPU."), kNoTag, "UNDER_DRAWER");
      pGraphics->ForControlInGroup("UNDER_DRAWER", [this](IControl* ctrl) { ctrl->Hide(mPedalDrawerOpen); });
      if (false) // 1.60.0: the scrolling pedal list is now the drawer
      {
        // Scrolling moves the rows live (no relayout): each chip re-reads its position, which
        // follows mLibraryScrollY.
        pedalPickerScrollBar = new NAMScrollBarControl(
          pedalPickerListArea.GetFromRight(pedalPickerBarW), pedalPickerContentH, pedalPickerListArea.H(),
          kPedalPickerRowH * 0.5f, [this]() { return mLibraryScrollY; },
          [this](float v) {
            mLibraryScrollY = v;
            for (auto* chip : mPedalChips)
              if (chip)
                chip->As<NAMPedalChipControl>()->RefreshPosition();
            if (auto* ui = GetUI())
              ui->SetAllControlsDirty();
          });
        // The wheel works over the whole list, including the empty rows of pedals in the chain.
        pGraphics->AttachControl(new NAMWheelForwardControl(pedalPickerRowsArea, pedalPickerScrollBar));
        pGraphics->AttachControl(pedalPickerScrollBar);
      }
    }

    // The knobs
    // Input and Output are the whole app's levels (before the chain / after it), not the amp's --
    // they apply even with the AMP block off -- so they're labeled as master controls.
    // Knob label/value text sized to the knob columns, so "Master Out" / "-82.3 dB" sit inside
    // their column with room to spare instead of filling it edge to edge.
    IText ampKnobLabelText = style.labelText, ampKnobValueText = style.valueText;
    ampKnobLabelText.mSize = ampKnobValueText.mSize =
      std::clamp(inputKnobArea.W() * 0.15f, 11.f, style.labelText.mSize);
    const IVStyle ampKnobStyle = style.WithLabelText(ampKnobLabelText).WithValueText(ampKnobValueText);
    addAmpDetail(new NAMKnobControl(inputKnobArea, kInputLevel, "Master In", ampKnobStyle, knobBackgroundBitmap))
      ->SetTooltip("Master input: the level into the whole chain (works even with the amp off)");
    addAmpDetail(new NAMKnobControl(noiseGateArea, kNoiseGateThreshold, "", ampKnobStyle, knobBackgroundBitmap));
    addAmpDetail(new NAMKnobControl(bassKnobArea, kToneBass, "", ampKnobStyle, knobBackgroundBitmap), -1, "EQ_KNOBS");
    addAmpDetail(new NAMKnobControl(midKnobArea, kToneMid, "", ampKnobStyle, knobBackgroundBitmap), -1, "EQ_KNOBS");
    addAmpDetail(new NAMKnobControl(trebleKnobArea, kToneTreble, "", ampKnobStyle, knobBackgroundBitmap), -1, "EQ_KNOBS");
    addAmpDetail(new NAMKnobControl(outputKnobArea, kOutputLevel, "Master Out", ampKnobStyle, knobBackgroundBitmap))
      ->SetTooltip("Master output: the level out of the whole chain. If the OUT meter shows CLIP, turn this down "
                   "(or turn the Limiter on).");
    // Limiter and the OUTPUT page (1.58.0; it replaced 1.55.0's Mono switch) share the space under
    // Master Out, side by side.
    const IRECT outputToggles = toggleUnder(outputKnobArea);
    addAmpDetail(new NAMSwitchControl(outputToggles.SubRectHorizontal(2, 0), kOutputLimiter, "Limiter", style,
                                      switchHandleBitmap))
      ->SetTooltip("Output limiter: catches the brief pick peaks (at -1 dBFS unless you set its Ceiling lower on "
                   "the OUTPUT page), so you can turn Master Out up for a louder sound without clipping. Adds 0.7 ms "
                   "of latency.");
    addAmpDetail(new NAMFlatButtonControl(outputToggles.SubRectHorizontal(2, 1).GetCentredInside(
                                            outputToggles.W() * 0.5f - 4.f, std::min(24.f, outputToggles.H())),
                                          "OUTPUT",
                                          [pGraphics]() {
                                            if (auto* page = pGraphics->GetControlWithTag(kCtrlTagOutputPage))
                                              page->Hide(false);
                                          }))
      ->SetTooltip("The OUTPUT page: Master Out, the limiter's Ceiling and Release, and stereo Width / Widen "
                   "(Width 0% is mono).");

    // Post-amp tone shaping: low-end density (200-500Hz shelf) and high-end boost (2-6kHz shelf).
    // Each has its own on/off toggle, same pattern as the EQ switch.
    addAmpDetail(new NAMKnobControl(densityKnobArea, kDensity, "Density", ampKnobStyle, knobBackgroundBitmap));
    addAmpDetail(new NAMKnobControl(presenceKnobArea, kPresence, "Boost", ampKnobStyle, knobBackgroundBitmap));
    addAmpDetail(new NAMSwitchControl(densityToggleArea, kDensityActive, "Density", style, switchHandleBitmap));
    addAmpDetail(new NAMSwitchControl(presenceToggleArea, kPresenceActive, "Boost", style, switchHandleBitmap));

    // The meters -- always visible, not part of any popup. Attachment deferred to after the
    // popup chrome/rack/chips section below (see the matching comment there) so they stay on
    // top of even the Amp's near-fullscreen popup card instead of being covered by it -- the
    // old shared pedal/cab card only ever reached ~360x280 centered, never overlapping these
    // edge-pinned bars, so this never mattered until the Amp's popup could cover the whole window.
    // Captured here so _SelectBlock can carry the meters into the Amp popup and back -- see the
    // member declarations' comment for why that's a reposition of the one real control rather
    // than a second mirrored instance.
    mInputMeterHomeRect = inputMeterArea;
    mOutputMeterHomeRect = outputMeterArea;
    // Caption to the left of the bar (the bars sit above the meter's dB numbers).
    auto meterCaption = [meterCaptionW](const IRECT& meter) {
      return IRECT(meter.L - meterCaptionW, meter.T, meter.L - 4.f, meter.B - 10.f);
    };
    mInputMeterLabelHomeRect = meterCaption(inputMeterArea);
    mOutputMeterLabelHomeRect = meterCaption(outputMeterArea);

    auto attachMeters = [&]() {
      pGraphics->AttachControl(new NAMMeterControl<1>(inputMeterArea, style), kCtrlTagInputMeter);
      auto* outMeter = new NAMMeterControl<2>(outputMeterArea, style);
      outMeter->SetClickFunc([pGraphics]() {
        if (auto* page = pGraphics->GetControlWithTag(kCtrlTagOutputPage))
          page->Hide(false);
      });
      pGraphics->AttachControl(outMeter, kCtrlTagOutputMeter)
        ->SetTooltip("The final output. Click for the OUTPUT page (Master Out, limiter, stereo width).");
      // The meters are otherwise two unlabeled bars pinned to opposite edges, with nothing saying
      // which end of the chain each one is measuring.
      const IVStyle meterLabelStyle =
        DEFAULT_STYLE.WithValueText(IText(11, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.6f), "Roboto-Regular"))
          .WithDrawFrame(false);
      pGraphics->AttachControl(new IVLabelControl(mInputMeterLabelHomeRect, "IN", meterLabelStyle),
                               kCtrlTagInputMeterLabel);
      pGraphics->AttachControl(new IVLabelControl(mOutputMeterLabelHomeRect, "OUT", meterLabelStyle),
                               kCtrlTagOutputMeterLabel);
    };

    // Settings/help/about box. Only the trigger button attaches here -- the page itself is a
    // full-window overlay (see below, attached late alongside the Slim overlay) so it actually
    // draws on top of the always-visible routing view instead of being buried under it.
    pGraphics->AttachControl(new NAMCircleButtonControl(
      settingsButtonArea,
      [pGraphics](IControl* pCaller) {
        pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
      },
      gearSVG));

    // Tuner: same story -- only the trigger button here, the page itself attached late.
    pGraphics->AttachControl(new IVButtonControl(
      tunerButtonArea,
      [pGraphics](IControl* pCaller) { pGraphics->GetControlWithTag(kCtrlTagTunerBox)->Hide(false); }, "TUNE", headerButtonStyle),
      kCtrlTagTuneButton);

    // Output EQ (1.46.0): the spectrum strip above the meters opens its page.
    auto openOutEq = [pGraphics]() {
      if (auto* page = pGraphics->GetControlWithTag(kCtrlTagOutEqPage))
        page->Hide(false);
    };
    // The X on its left hides it (1.48.0); SPECTRUM brings it back. Both are also in Settings. The
    // arrow under the X makes it taller. It's attached later, next to the meters (attachSpecStrip),
    // so it draws above a pedal's popup -- which sits above it -- and you can watch it while you
    // turn the pedal's knobs. The big cards (Amp, Tube Lab, CAB LAB, PRO EQ) hide it.
    auto attachSpecStrip = [&, openOutEq]() {
      auto add = [&](IControl* c, const char* tip, const IRECT& full, const IRECT& shrunk) {
        pGraphics->AttachControl(c)->SetTooltip(tip);
        mSpecStripCtls.push_back({c, full, shrunk});
      };
      if (mSpecFloating)
      {
        // 1.50.0: its own window, above every popup.
        IRECT r = mSpecFloatRect;
        if (r.W() <= 0.f || r.H() <= 0.f)
        {
          const float w = std::min(560.f, b.W() * 0.5f), h = 240.f;
          r = IRECT(b.L + 24.f, meterStrip.T - 20.f - h, b.L + 24.f + w, meterStrip.T - 20.f);
        }
        const IRECT ub = NAMSpectrumWindowControl::Usable(b);
        const float w = std::clamp(r.W(), NAMSpectrumWindowControl::kMinW, std::max(NAMSpectrumWindowControl::kMinW, ub.W()));
        const float h = std::clamp(r.H(), NAMSpectrumWindowControl::kMinH, std::max(NAMSpectrumWindowControl::kMinH, ub.H()));
        const float x = std::clamp(r.L, ub.L, std::max(ub.L, ub.R - w)), y = std::clamp(r.T, ub.T, std::max(ub.T, ub.B - h));
        auto* graph = new NAMEqGraphControl(IRECT(x, y, x + w, y + h), kOutEqBandFirst, kOutEqBandExtFirst, kOutEqTrim,
                                            kOutEqOutput, nullptr, &mOutTapOut, nullptr, "OUTPUT", &mStripView, true,
                                            openOutEq);
        graph->SetFrameRate(60.0);
        graph->SetTooltip("The live spectrum of the final output, with the Output EQ's curve. Click to open the Output EQ.");
        NAMSpectrumWindowControl::Callbacks cb;
        cb.dock = [this]() { _SetSpecFloating(false); };
        cb.close = [this]() { _SetSpecStripShown(false); };
        cb.moved = [this](const IRECT& rect) {
          mSpecFloatRect = rect;
          _SaveUiPrefs();
        };
        pGraphics->AttachControl(new NAMSpectrumWindowControl(IRECT(x, y, x + w, y + h), graph, std::move(cb)));
        return;
      }
      if (mShowSpecStrip)
      {
        // The left column: X, bigger/smaller, pop out -- sized to fit the strip's height. With a big
        // card open the strip drops to its small size (mSpecStripSmallRect), so both are worked out.
        auto column = [](const IRECT& area, int k) {
          const float bs = std::min(22.f, (area.H() - 8.f) / 3.f);
          return IRECT(area.L, area.T + k * (bs + 4.f), area.L + 22.f, area.T + k * (bs + 4.f) + bs);
        };
        const IRECT& full = specStripArea;
        const IRECT& shrunk = mSpecStripSmallRect;
        add(new NAMFlatButtonControl(column(full, 0), nullptr, [this]() { _SetSpecStripShown(false); }),
            "Hide the spectrum strip (bring it back with SPECTRUM, or in Settings)", column(full, 0), column(shrunk, 0));
        add(new NAMFlatButtonControl(column(full, 1), mSpecStripExpanded ? "#down" : "#up",
                                     [this]() { _SetSpecStripExpanded(!mSpecStripExpanded); }),
            mSpecStripExpanded ? "Make the spectrum strip smaller" : "Make the spectrum strip bigger", column(full, 1),
            column(shrunk, 1));
        add(new NAMFlatButtonControl(column(full, 2), "#pop", [this]() { _SetSpecFloating(true); }),
            "Pop the spectrum out into a window you can move and resize (it stays above the popups)", column(full, 2),
            column(shrunk, 2));
        add(new NAMEqGraphControl(full.GetReducedFromLeft(28.f), kOutEqBandFirst, kOutEqBandExtFirst, kOutEqTrim,
                                  kOutEqOutput, nullptr, &mOutTapOut, nullptr, "OUTPUT", &mStripView, true, openOutEq),
            "The live spectrum of the final output, with the Output EQ's curve. Click to open the Output EQ.",
            full.GetReducedFromLeft(28.f), shrunk.GetReducedFromLeft(28.f));
      }
      else
      {
        const IRECT r1 = specStripArea.GetFromLeft(96.f), r2 = r1.GetTranslated(104.f, 0.f);
        add(new NAMFlatButtonControl(r1, "SPECTRUM", [this]() { _SetSpecStripShown(true); }),
            "Show the spectrum strip again", r1, r1);
        add(new NAMFlatButtonControl(r2, "OUTPUT EQ", openOutEq), "Open the Output EQ", r2, r2);
      }
    };

    // MIDI learn (1.43.0): turns the overlay on and off (see NAMMidiLearnOverlayControl).
    pGraphics
      ->AttachControl(new IVButtonControl(
        midiLearnButtonArea, [this](IControl* pCaller) { _SetMidiLearn(!mMidiLearnOn); }, "MIDI", headerButtonStyle))
      ->SetTooltip("MIDI learn: click a knob or switch, then press or move a control on your MIDI controller (a "
                   "footswitch, the expression pedal). Click MIDI again when you're done.");

    // Tap tempo (1.71.0): the BPM, and TAP (MIDI-learnable, for a footswitch).
    pGraphics->AttachControl(new NAMTempoControl(tempoArea, kTempoBPM, [this]() { return _BeatPhase(); }));
    pGraphics
      ->AttachControl(new IVButtonControl(
                        tapButtonArea,
                        [this](IControl* pCaller) {
                          _TempoTap(std::chrono::duration<double>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
                        },
                        "TAP", headerButtonStyle),
                      kCtrlTagTapButton)
      ->SetTooltip("Tap tempo: tap two or more times on the beat. Every delay set to a Tap note value follows. "
                   "MIDI learn it to a footswitch to tap with your foot.");

#ifdef APP_API
    pGraphics
      ->AttachControl(new IVButtonControl(
        savePresetButtonArea,
        // The handler by value: it's a local of this layout function, long gone by the time the
        // button is clicked ([&] crashed on SAVE in 1.38.1).
        [this, savePresetCompletionHandler](IControl* pCaller) {
          WDL_String fileName("preset.namp"), path;
          _GetPresetsDir(path);
          pCaller->GetUI()->PromptForFile(fileName, path, EFileAction::Save, "namp", savePresetCompletionHandler);
        },
        "SAVE", headerButtonStyle))
      ->SetTooltip("Save the current amp + pedalboard settings as a preset file");

    pGraphics
      ->AttachControl(new IVButtonControl(
        loadPresetButtonArea,
        [this, loadPresetCompletionHandler](IControl* pCaller) {
          WDL_String fileName, path;
          _GetPresetsDir(path);
          pCaller->GetUI()->PromptForFile(fileName, path, EFileAction::Open, "namp", loadPresetCompletionHandler);
        },
        "LOAD", headerButtonStyle))
      ->SetTooltip("Load a saved preset file");
#endif

    // ===================== The routing view: AMP block + pedal chips =====================
    // This IS the main screen now -- always visible, no separate "FX" page. Clicking any block
    // pops up its own controls (the amp knobs above, or a pedal's, per the popup chrome below).
    {
      // The chain grid: cells, cables and ports, drawn under the blocks (attached first, so the
      // block chips paint over it and win clicks on themselves).
      auto* gridControl = new NAMChainGridControl(
        chainArea, gridGeom, [this]() { return _ReadChainGrid(); },
        [this](const chaingrid::Grid& g) { _WriteChainGrid(g); });
      gridControl->SetOutClick([pGraphics]() {
        if (auto* page = pGraphics->GetControlWithTag(kCtrlTagOutputPage))
          page->Hide(false);
      });
      pGraphics->AttachControl(gridControl);

      // Where a block sits: its grid cell's chip.
      auto blockRect = [this, gridGeom](int block) {
        // By param slot, not block id: the ids after Input (the extra pedal copies) are one slot
        // lower. Using the id here drew REVERB 2 where REVERB 3 was, and REVERB 3 at cell 0.
        const int c = (int)std::round(GetParam(GridCellParam(block))->Value());
        return gridGeom.Chip(std::clamp(c, 0, chaingrid::kNumCells - 1));
      };
      // The cell a block dropped at (x, y) lands in: the nearest empty one to the cell under it
      // (anywhere over the chain area counts, so a drop just off the grid's edge still lands), or
      // -1 off the chain area.
      auto dropCell = [this, gridGeom, chainArea](int block, float x, float y) {
        if (!chainArea.Contains(x, y))
          return -1;
        return _ReadChainGrid().NearestFreeCell(gridGeom.CellAtClamped(x, y), block);
      };
      auto previewDrop = [gridControl, dropCell](int block, float x, float y, bool dragging) {
        gridControl->SetDropPreview(dragging ? dropCell(block, x, y) : -1);
      };
      // Dropping a block re-plugs it where it lands (see ChainGrid::Insert): into a column that
      // has blocks, in parallel with them; into an empty column, in series. A pedal dropped off
      // the grid leaves the chain (back to the AMP NEST list); so does CAB since 1.61.1 (into the
      // PEDALS drawer); AMP and the loop snap back.
      auto dropBlock = [this, dropCell](int block, float x, float y) {
        const bool isPedal = chaingrid::IsPedal(block);
        chaingrid::Grid g = _ReadChainGrid();
        const int c = dropCell(block, x, y);
        NAMDragLog("dropBlock block=%d at %.1f,%.1f cell=%d placed=%d", block, x, y, c,
                                     (int)g.Placed(block));
        if (c < 0)
        {
          if ((isPedal || block == chaingrid::kCab) && g.Placed(block))
          {
            g.Remove(block);
            _WriteChainGrid(g);
            if (isPedal)
              SendParameterValueFromUI(PedalOf(block).activeParam, 0.0);
            if (auto* ui = GetUI())
              ui->SetAllControlsDirty();
          }
          return;
        }
        if (g.Placed(block))
          g.Move(block, c);
        else
        {
          // In the chain first: until the grid below is written, nothing is cabled to it, so the
          // audio thread just sees an unconnected block for a moment.
          if (isPedal)
            SendParameterValueFromUI(PedalOf(block).activeParam, 1.0);
          g.Insert(block, c);
        }
        NAMDragLog("drop block=%d at %.1f,%.1f cell=%d -> placed cell=%d in=%u", block, x, y, c,
                                     g.cell[block], (unsigned)g.in[block]);
        _WriteChainGrid(g);
      };
      // AMP, CAB and the FX loop are always on the grid. Each is draggable like a pedal.
      auto makeFixedBlock = [&](int block, const char* name, int chipId) {
        auto* chip = new NAMPedalChipControl(blockRect(block), name, chipId, style,
                                             [dropBlock, block](int, float x, float y) { dropBlock(block, x, y); });
        chip->SetDragPreviewFunc(
          [previewDrop, block](float x, float y, bool dragging) { previewDrop(block, x, y, dragging); });
        chip->SetActiveGetter([]() { return true; });
        chip->SetPositionGetter([blockRect, block]() { return blockRect(block); });
        return chip;
      };

      // The AMP block: draggable and clickable -- opens its own (bigger) popup.
      auto* ampChip = makeFixedBlock(chaingrid::kAmp, "AMP", kAmpBlockId);
      ampChip->SetActiveGetter([this]() { return GetParam(kAmpEngaged)->Value() > 0.5 && !_TubeTakesCapture(); });
      ampChip->SetNameGetter([this]() { return std::string(_TubeTakesCapture() ? "AMP IN LAB" : ""); });
      ampChip->SetSelectedGetter([this]() { return mSelectedBlock == kAmpBlockId; });
      ampChip->SetSelectedFunc([this](int) { _SelectBlock(kAmpBlockId); });
      mAmpChip = ampChip;
      pGraphics->AttachControl(ampChip);

      // FX Loop Send/Return: taps into the aux I/O bus (see PLUG_CHANNEL_IO). Either one opens the
      // loop's popup (On/Off and Mix); both light up while the loop is on.
      auto setUpLoopChip = [this](NAMPedalChipControl* chip) {
        chip->SetActiveGetter([this]() { return GetParam(kLoopEngaged)->Value() > 0.5; });
        chip->SetSelectedGetter([this]() { return mSelectedBlock == kLoopBlockId; });
        chip->SetSelectedFunc([this](int) { _SelectBlock(kLoopBlockId); });
      };
      auto* loopSendChip = makeFixedBlock(chaingrid::kLoopSend, "LOOP OUT", -4);
      loopSendChip->SetTooltip("FX Loop Send -- taps the signal here out to the aux output bus");
      setUpLoopChip(loopSendChip);
      mLoopSendChip = loopSendChip;
      pGraphics->AttachControl(loopSendChip);

      auto* loopReturnChip = makeFixedBlock(chaingrid::kLoopReturn, "LOOP IN", -5);
      loopReturnChip->SetTooltip("FX Loop Return -- blends in whatever comes back on the aux input bus (Mix)");
      setUpLoopChip(loopReturnChip);
      mLoopReturnChip = loopReturnChip;
      pGraphics->AttachControl(loopReturnChip);

      // 1.60.0: the PEDALS drawer's panel. Its chips are the pedals' own (attached next, above it).
      pGraphics->AttachControl(new NAMPedalDrawerControl(drawerRect, [this]() { _SetPedalDrawerOpen(false); }),
                               kCtrlTagPedalDrawer)
        ->Hide(!mPedalDrawerOpen);
      // Pass 1: every chip, so the popup (attached after) always draws on top of all of them. A
      // pedal that isn't in the chain sits in the AMP NEST panel's list, one row per pedal type. The
      // effect types come in several copies: the row shows the first copy that's free, so after
      // one Reverb goes into the chain the row offers the next ("REVERB 2"), until all are in use.
      auto inNest = [this](int id) {
        return GetParam(PedalOf(id).activeParam)->Value() <= 0.5
               || GetParam(GridCellParam(id))->Value() < 0.0;
      };
      auto nestFront = [inNest](int type) {
        for (int copy = 0; copy < chaingrid::NumCopies(type); copy++)
          if (inNest(chaingrid::PedalCopyId(type, copy)))
            return chaingrid::PedalCopyId(type, copy);
        return -1;
      };
      for (int p = 0; p < chaingrid::kNumIds; p++)
      {
        if (!chaingrid::IsPedal(p))
          continue;
        const PedalDef& def = PedalOf(p);
        const int engagedParam = def.engagedParam;
        const int nestRow = chaingrid::PedalType(p);

        auto inPicker = [inNest, p]() { return inNest(p); };
        const bool lab = isLab(p);
        auto positionGetter = [this, blockRect, p, lab, labSlotRect, drawerTypes, drawerSlotRect, inPicker, nestFront]() {
          if (inPicker())
          {
            if (lab)
              return labSlotRect(p); // 1.60.0: its own slot in the AMP NEST's LABS
            // In the drawer, packed: the types with every copy in the chain are skipped, so it has
            // no holes (and CAB, off the grid, comes first).
            int slot = _ReadChainGrid().Placed(chaingrid::kCab) ? 0 : 1;
            for (int t : drawerTypes)
            {
              if (t == chaingrid::PedalType(p))
                break;
              if (nestFront(t) >= 0)
                slot++;
            }
            return drawerSlotRect(slot);
          }
          return blockRect(p);
        };

        auto* chip = new NAMPedalChipControl(positionGetter(), def.name, p, style,
                                             [dropBlock](int pedalIdx, float x, float y) { dropBlock(pedalIdx, x, y); });
        if (lab)
          chip->SetLabKind(p == kCabLabPedalIdx ? 2 : 1);
        chip->SetDragPreviewFunc([previewDrop, p](float x, float y, bool dragging) { previewDrop(p, x, y, dragging); });
        chip->SetActiveGetter(
          [this, engagedParam]() { return GetParam(engagedParam)->Value() > 0.5; });
        chip->SetPositionGetter(positionGetter);
        // 1.60.0: off the grid, a lab shows in its AMP NEST slot (while the panel's open, and the
        // drawer, which covers the slots, isn't) and a pedal in the drawer (while it's open, its
        // type's front copy only).
        chip->SetClipGetter([this, inPicker, nestFront, nestRow, p, lab, labSlotRect, drawerChipsArea](IRECT& clip) {
          if (!inPicker())
            return false;
          if (lab)
            clip = mShowLibraryPanel && !mPedalDrawerOpen ? labSlotRect(p) : IRECT();
          else
            clip = (mShowLibraryPanel && mPedalDrawerOpen && nestFront(nestRow) == p) ? drawerChipsArea : IRECT();
          return true;
        });
        chip->SetSelectedGetter([this, p]() { return mSelectedBlock == p; });
        chip->SetSelectedFunc([this](int pedalIdx) { _SelectBlock(pedalIdx); });
        if (p >= kNumBuiltInPedals && p < kNumPedals)
        {
          const int namSlot = p - kNumBuiltInPedals;
          chip->SetNameGetter([this, namSlot, fallback = std::string(def.name)]() -> std::string {
            const char* fileName = GetNamPedalFileName(namSlot);
            return fileName ? std::string(fileName) : fallback;
          });
        }
        mPedalChips[p] = chip;
        pGraphics->AttachControl(chip);
      }

      // The CAB block: draggable and clickable -- it opens a popup (same card/backdrop chrome as a
      // pedal) holding the IR/cab loading controls. Attached after every pedal chip so it's always
      // on top and clickable.
      auto* cabChip = makeFixedBlock(chaingrid::kCab, "CAB", kCabBlockId);
      // 1.61.1: CAB can come off the grid (drag it off, like a pedal); then it waits in the PEDALS
      // drawer's first slot until it's dragged back in.
      cabChip->SetPositionGetter([this, blockRect, drawerSlotRect]() {
        return _ReadChainGrid().Placed(chaingrid::kCab) ? blockRect(chaingrid::kCab) : drawerSlotRect(0);
      });
      cabChip->SetClipGetter([this, drawerChipsArea](IRECT& clip) {
        if (_ReadChainGrid().Placed(chaingrid::kCab))
          return false;
        clip = mShowLibraryPanel && mPedalDrawerOpen ? drawerChipsArea : IRECT();
        return true;
      });
      cabChip->SetActiveGetter([this]() { return GetParam(kIRToggle)->Value() > 0.5; });
      cabChip->SetSelectedGetter([this]() { return mSelectedBlock == kCabBlockId; });
      cabChip->SetSelectedFunc([this](int) { _SelectBlock(kCabBlockId); });
      cabChip->SetTooltip("IR / cabinet simulation");
      mCabButton = cabChip;
      pGraphics->AttachControl(cabChip);

      // Popup chrome (backdrop + card background) attached now, after everything that's always
      // visible (amp knobs, model/IR pickers, the rack, pedal chips) -- so when shown, it actually
      // overlays that content instead of being buried under it. This has to sit strictly between
      // the always-visible content above and the popup's own detail content below: any earlier and
      // the amp knobs/chips (attached after it) would draw on top of it, defeating the overlay;
      // any later and it would itself paint over the popup's own knobs/title/close button.
      // A lighter dim than the Slim overlay's default (0.45) -- this is a quick zoom-in popover on
      // one pedal, not a full-screen modal takeover, so the rest of the rack should stay legible
      // behind it.
      mBlockPopupBackdrop = pGraphics->AttachControl(
        new NAMSlimOverlayBackdropControl(b, [this](IControl*) { _SelectBlock(-1); }, 0.22f));
      mBlockPopupBackdrop->Hide(true);
      mBlockPopupCardBg = pGraphics->AttachControl(
        new NAMBorderedPanelControl(popupCardArea, PluginColors::NAM_CARD, PluginColors::NAM_THEMECOLOR.WithOpacity(0.5f)));
      mBlockPopupCardBg->Hide(true);
      // Clicking inside the popup keeps it open; clicking outside it (or its X) closes it.
      static_cast<NAMSlimOverlayBackdropControl*>(mBlockPopupBackdrop)->SetKeepOpenArea([this]() {
        return mBlockPopupCardBg ? mBlockPopupCardBg->GetRECT() : IRECT();
      });

      mBlockPopupCloseButton =
        pGraphics->AttachControl(new NAMSquareButtonControl(popupCloseArea, [this](IControl*) { _SelectBlock(-1); }, crossSVG));
      mBlockPopupCloseButton->Hide(true);

      // Popup title: shared by whichever block is open (amp or a pedal), so it's tracked as its
      // own dedicated control rather than folded into mAmpDetailControls/mPedalDetailControls --
      // being in every one of those lists would just mean the last list processed always wins.
      mBlockPopupTitleLabel = pGraphics->AttachControl(new IVLabelControl(
        popupLabelArea, "", DEFAULT_STYLE.WithValueText(IText(20, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular")).WithDrawFrame(false)));
      mBlockPopupTitleLabel->Hide(true);
      // The label text itself tracks whichever block is selected -- done in _SelectBlock.

      // Pass 2: each pedal's own label/switch/knobs, sharing the popup card position -- only the
      // selected block's set is ever un-hidden (see _SelectBlock).
      const auto pedalDetailArea = pedalPopupBodyArea;
      const IVStyle pedalLabelStyle =
        DEFAULT_STYLE.WithValueText(IText(17, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular")).WithDrawFrame(false);
      for (int p = 0; p < chaingrid::kNumIds; p++)
      {
        if (!chaingrid::IsPedal(p))
          continue;
        if (p == chaingrid::kProEq)
        {
          // PRO EQ (1.46.0): On and Output across the top, the EQ graph below (EQControls.h).
          const IRECT body = _ComputeTubePopupCardArea(b).GetReducedFromTop(38.f).GetPadded(-10.f);
          const IRECT top = body.GetFromTop(64.f);
          auto* onSwitch = new NAMSwitchControl(top.GetFromLeft(110.f).GetCentredInside(100.f, 60.f), kProEqEngaged, "On",
                                                style, switchHandleBitmap);
          auto* trimKnob = new NAMKnobControl(top.GetReducedFromLeft(120.f).GetFromLeft(190.f), kProEqTrim, "Trim",
                                              style, knobBackgroundBitmap);
          trimKnob->SetSideText(true);
          trimKnob->SetTooltip("Trim: the level into the EQ (before the bands).");
          auto* outKnob = new NAMKnobControl(top.GetReducedFromLeft(320.f).GetFromLeft(190.f), kProEqOutput, "Output",
                                             style, knobBackgroundBitmap);
          outKnob->SetSideText(true);
          auto* graph = new NAMEqGraphControl(body.GetReducedFromTop(72.f), kProEqBandFirst, kProEqBandExtFirst, kProEqTrim,
                                              kProEqOutput, &mProEqTapIn, &mProEqTapOut, "IN", "OUT", &mProEqView);
          for (IControl* c :
               {(IControl*)onSwitch, (IControl*)trimKnob, (IControl*)outKnob, (IControl*)graph})
          {
            pGraphics->AttachControl(c);
            c->Hide(true);
            mPedalDetailControls[p].push_back(c);
          }
          continue;
        }
        if (p == chaingrid::kTubeLab2)
          continue; // 1.67.0: it opens the Tube Lab's popup (see _SetUiLab)
        if (p == kTubePedalIdx)
        {
          // Tube Lab: its own, bigger card (see _ComputeTubePopupCardArea) -- the On and Circuit
          // (engine) switches and the live transfer curve across the top, then a row of 6 knobs,
          // then the circuit row (cathode bypass cap, plate load, supply), the Stage 2 row, and the
          // Power Amp row.
          const IRECT tubeBody = _ComputeTubePopupCardArea(b).GetReducedFromTop(38.f).GetPadded(-10.f);
          const IRECT topRow = tubeBody.GetFromTop(tubeBody.H() * 0.2f);
          const IRECT switchesArea = topRow.GetFromLeft(std::min(600.f, topRow.W() * 0.66f));
          // Seven cells: On, Circuit, AMP Capture (1.64.0), Spkr Load, Economy, 8x (1.46.0: up here with
          // Economy, its opposite), and PRESETS / DEBUG stacked.
          const float switchW = std::min(100.f, switchesArea.W() / 7.f - 4.f);
          const IRECT switchArea = switchesArea.GetGridCell(0, 0, 1, 7).GetCentredInside(switchW, 60.f);
          const IRECT engineSwitchArea = switchesArea.GetGridCell(0, 1, 1, 7).GetCentredInside(switchW, 60.f);
          const IRECT captureSwitchArea = switchesArea.GetGridCell(0, 2, 1, 7).GetCentredInside(switchW, 60.f);
          const IRECT loadSwitchArea = switchesArea.GetGridCell(0, 3, 1, 7).GetCentredInside(switchW, 60.f);
          const IRECT economySwitchArea = switchesArea.GetGridCell(0, 4, 1, 7).GetCentredInside(switchW, 60.f);
          const IRECT hqSwitchArea = switchesArea.GetGridCell(0, 5, 1, 7).GetCentredInside(switchW, 60.f);
          const IRECT buttonsCell = switchesArea.GetGridCell(0, 6, 1, 7).GetCentredInside(switchW, 58.f);
          const IRECT presetButtonArea = buttonsCell.GetFromTop(26.f);
          const IRECT debugButtonArea = buttonsCell.GetFromBottom(26.f);
          const IRECT curveArea = topRow.GetReducedFromLeft(switchesArea.W() + 10.f);
          // Below the top row (1.46.0): a tab bar and one page of controls per tab -- PREAMP, POWER AMP,
          // FEEL -- each on the same 4 x 7 grid. On one page (1.45.0) there were six rows and every
          // dial shrank; the Circuit+ row's labels were cut off.
          const IRECT tabBarArea = tubeBody.GetReducedFromTop(topRow.H() + 6.f).GetFromTop(32.f);
          const IRECT lowerArea = tubeBody.GetReducedFromTop(topRow.H() + 6.f + 32.f + 10.f);
          constexpr int kTubeCols = 7, kTubeRows = 4;
          mTubeTabControls.clear();
          auto addToTab = [this, p](IControl* c, int tab) {
            mPedalDetailControls[p].push_back(c);
            mTubeTabControls.push_back({c, tab});
          };
          // 1.62.0: CHASSIS, at the tab bar's right end: the preamp as a chassis (TubeChassisControls.h).
          {
            auto* chassisButton = new NAMFlatButtonControl(tabBarArea.GetFromRight(tabBarArea.W() * 0.16f).GetPadded(-2.f),
                                                           "CHASSIS", [pGraphics]() {
                                                             if (auto* page = pGraphics->GetControlWithTag(kCtrlTagTubeChassis))
                                                               page->Hide(false);
                                                           });
            chassisButton->SetTooltip("The whole amp as a chassis in 3D (drag to turn it): the preamp tubes, four sockets "
                                      "for more preamp tubes (gain stages, followers or boosts), the power tubes and "
                                      "output transformer, the turret board's parts, the Variac. Click anything to see and "
                                      "set it; double-click to go to its page.");
            pGraphics->AttachControl(chassisButton);
            chassisButton->Hide(true);
            mPedalDetailControls[p].push_back(chassisButton);
          }
          auto* tabBar = new NAMTabBarControl(
            tabBarArea.GetReducedFromRight(tabBarArea.W() * 0.16f + 4.f), {"PREAMP", "POWER AMP", "FEEL"},
            [this]() { return mTubeTab; },
            [this](int tab) {
              mTubeTab = tab;
              _ApplyTubeTab();
            });
          pGraphics->AttachControl(tabBar);
          tabBar->Hide(true);
          mPedalDetailControls[p].push_back(tabBar);

          auto* onOffSwitch = new NAMSwitchControl(switchArea, kTubeEngaged, "On", style, switchHandleBitmap);
          pGraphics->AttachControl(onOffSwitch);
          onOffSwitch->Hide(true);
          mPedalDetailControls[p].push_back(onOffSwitch);

          auto* engineSwitch =
            new NAMSwitchControl(engineSwitchArea, kTubeCircuit, "Circuit", style, switchHandleBitmap);
          pGraphics->AttachControl(engineSwitch);
          engineSwitch->SetTooltip("On: the circuit-model engine. Off: the Classic lookup-table engine.");
          engineSwitch->Hide(true);
          mPedalDetailControls[p].push_back(engineSwitch);

          auto* loadSwitch =
            new NAMSwitchControl(loadSwitchArea, kTubeSpeakerLoad, "Spkr Load", style, switchHandleBitmap);
          pGraphics->AttachControl(loadSwitch);
          loadSwitch->SetTooltip("The power amp drives a real speaker's impedance (bass resonance, treble rise) "
                                 "instead of a resistor. Needs Power Amp on.");
          loadSwitch->Hide(true);
          mPedalDetailControls[p].push_back(loadSwitch);

          {
            auto* captureSwitch =
              new NAMSwitchControl(captureSwitchArea, kTubeCapture, "AMP Capture", style, switchHandleBitmap);
            pGraphics->AttachControl(captureSwitch);
            captureSwitch->SetTooltip(
              "Use the capture loaded in the AMP block as the Tube Lab's preamp: Drive is the gain into it, and its "
              "output drives the Tube Lab's own power amp -- Master, Sag, Feedback, Presence, EL34/6L6, the bench "
              "(Variac, bias pot, tube age), Real Feel, the speaker load, failing tubes -- and the tone stack if one's "
              "on. The AMP block steps aside while this is on (the capture runs once, here). Best with a preamp or DI "
              "capture: a full-amp capture already has a power amp in it.");
            captureSwitch->Hide(true);
            mPedalDetailControls[p].push_back(captureSwitch);
            mLabOnly[0].push_back(captureSwitch);
            // 1.67.0: TUBE LAB 2's own capture, in the same place, and its loader under the curve.
            auto* capture2 = new NAMSwitchControl(captureSwitchArea, LabParam(1, kTubeCapture), "Capture", style,
                                                  switchHandleBitmap);
            pGraphics->AttachControl(capture2);
            capture2->SetTooltip(
              "Use TUBE LAB 2's own capture (LOAD CAPTURE, under the curve) as its preamp: Drive is the gain into it, "
              "and its output drives TUBE LAB 2's power amp, bench, Real Feel and speaker load -- and its tone stack if "
              "one's on. A second amp beside TUBE LAB and the AMP block. Best with a preamp or DI capture.");
            capture2->Hide(true);
            mPedalDetailControls[p].push_back(capture2);
            mLabOnly[1].push_back(capture2);
            const IRECT lab2Row = topRow.GetReducedFromLeft(switchesArea.W() + 10.f).GetFromBottom(20.f);
            const std::string griffinDir = GriffinLibraryDir();
            const std::vector<std::string> capPaths = {griffinDir + "IRs and Amp Profiles (copied)", griffinDir + "tone"};
            auto* loader = new NAMPresetBrowserControl(
              lab2Row.GetFromLeft(lab2Row.W() * 0.4f), "LOAD CAPTURE", "nam", capPaths, true,
              [this](const WDL_String& fileName, const WDL_String&) {
                if (!fileName.GetLength())
                  return;
                const std::string msg = _StageLab2Model(fileName);
                if (msg.size())
                {
                  _ShowMessageBox(GetUI(), ("Failed to load the capture:\n\n" + msg).c_str(), "TUBE LAB 2", kMB_OK);
                  return;
                }
                // Loading one switches it in (Circuit engine, Capture on).
                SendParameterValueFromUI(LabParam(1, kTubeCircuit), 1.0);
                SendParameterValueFromUI(LabParam(1, kTubeCapture), 1.0);
              },
              style);
            pGraphics->AttachControl(loader);
            loader->SetTooltip("Load a .nam capture into TUBE LAB 2 (it switches Capture on).");
            loader->Hide(true);
            mPedalDetailControls[p].push_back(loader);
            mLabOnly[1].push_back(loader);
            auto* capName = new IVLabelControl(
              lab2Row.GetReducedFromLeft(lab2Row.W() * 0.42f),
              mLab2ModelPath.GetLength() ? mLab2ModelPath.get_filepart() : "No capture loaded",
              DEFAULT_STYLE.WithDrawFrame(false).WithValueText(
                IText(10.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.8f), "Roboto-Regular")));
            pGraphics->AttachControl(capName, kCtrlTagLab2CaptureName);
            capName->Hide(true);
            mPedalDetailControls[p].push_back(capName);
            mLabOnly[1].push_back(capName);
          }
          auto* economySwitch =
            new NAMSwitchControl(economySwitchArea, kTubeEconomy, "Economy", style, switchHandleBitmap);
          pGraphics->AttachControl(economySwitch);
          economySwitch->SetTooltip("Runs the Tube Lab at half the oversampling (2x instead of 4x at 44.1/48 kHz): "
                                    "about half its CPU, so a smaller buffer (lower latency) fits. A slightly "
                                    "different top end; more aliasing only on very bright, high-gain settings. "
                                    "Flipping it restarts the Tube Lab (a 10 ms dip).");
          economySwitch->Hide(true);
          mPedalDetailControls[p].push_back(economySwitch);

          auto* hqSwitch = new NAMSwitchControl(hqSwitchArea, kTubeHighQuality, "8x", style, switchHandleBitmap);
          pGraphics->AttachControl(hqSwitch);
          hqSwitch->SetTooltip("Runs the Tube Lab at twice the oversampling (8x at 44.1/48 kHz): less aliasing on very "
                               "high-gain settings (a hard-clipping Stage 2: -38 dB -> -52 dB on a 2.5 kHz note), for "
                               "about twice the Tube Lab's CPU. No difference you'd hear at moderate gain. With Economy "
                               "also on, they cancel out. Flipping it restarts the Tube Lab (a 10 ms dip).");
          hqSwitch->Hide(true);
          mPedalDetailControls[p].push_back(hqSwitch);

          // Presets (TubePresets.h): a menu that sets every knob and switch below (not On).
          auto* presetButton = new IVButtonControl(
            presetButtonArea,
            [this](IControl* pCaller) {
              mTubePresetMenu.Clear();
              for (int i = 0; i < kNumTubePresets; i++)
                mTubePresetMenu.AddItem(kTubePresets[i].name);
              mTubePresetMenu.SetFunction([this](IPopupMenu* pMenu) {
                if (pMenu)
                  _ApplyTubePreset(pMenu->GetChosenItemIdx());
              });
              pCaller->GetUI()->CreatePopupMenu(*pCaller, mTubePresetMenu, pCaller->GetRECT());
            },
            "PRESETS", style);
          pGraphics->AttachControl(presetButton);
          presetButton->SetTooltip("Load a Tube Lab preset: clean preamps, boosts, and whole amps (with the power "
                                   "amp and speaker load). All are level-matched.");
          presetButton->Hide(true);
          mPedalDetailControls[p].push_back(presetButton);

          // Debug page (TubeDebugControls.h): circuit map with live voltages, readout, scope, CSV log.
          auto* debugButton = new IVButtonControl(
            debugButtonArea,
            [](IControl* pCaller) {
              if (auto* pPage = pCaller->GetUI()->GetControlWithTag(kCtrlTagTubeDebug))
                pPage->Hide(false);
            },
            "DEBUG", style);
          pGraphics->AttachControl(debugButton);
          debugButton->SetTooltip("Open the Tube Lab debug page: an oscilloscope on the circuit's real node voltages, a "
                                  "live schematic, a plain-English readout of what each setting is doing, and a CSV "
                                  "log.");
          debugButton->Hide(true);
          mPedalDetailControls[p].push_back(debugButton);

          auto* curve = new TubeCurveControl(curveArea, [this](double x) {
            return _UiStages()[0].TransferCurve(x, GetParam(_TP(kTubeDrive))->Value(), GetParam(_TP(kTubeBias))->Value(),
                                               GetParam(_TP(kTubeCircuit))->Bool(), GetParam(_TP(kTubePlate))->Value() * 1000.0,
                                               GetParam(_TP(kTubeStage2))->Bool(), GetParam(_TP(kTubeGain2))->Value() / 10.0,
                                               GetParam(_TP(kTubeBias2))->Value());
          });
          pGraphics->AttachControl(curve, kCtrlTagTubeCurve);
          curve->SetDimmed(GetParam(_TP(kTubeEngaged))->Value() <= 0.5);
          curve->Hide(true);
          mPedalDetailControls[p].push_back(curve);

          // Every control on the pages: {param, label, switch?, tab, row, column, tooltip}.
          enum
          {
            kPre = 0,
            kPow,
            kFeel
          };
          struct Cell
          {
            int paramIdx;
            const char* label;
            bool isSwitch;
            int tab, row, col;
            const char* tooltip;
          };
          const Cell cells[] = {
            // PREAMP: the input knobs, the stage-1 parts, Stage 2, the cathode follower; the tone stack
            // down the right.
            {kTubeDrive, "Drive", false, kPre, 0, 0, nullptr},
            {kTubeBias, "Bias", false, kPre, 0, 1, nullptr},
            {kTubeBloom, "Bloom", false, kPre, 0, 2, nullptr},
            {kTubeBright, "Bright", false, kPre, 0, 3, nullptr},
            {kTubeMix, "Mix", false, kPre, 0, 4, nullptr},
            {kTubeOutput, "Output", false, kPre, 0, 5, nullptr},
            {kTubeCathodeCap, "Cap", false, kPre, 1, 0, nullptr},
            {kTubePlate, "Plate", false, kPre, 1, 1, nullptr},
            {kTubePlateCap, "Plate Cap", false, kPre, 1, 2, nullptr},
            {kTubeSagOn, "Sag", true, kPre, 1, 3, nullptr},
            {kTubeSagSupply, "Supply", false, kPre, 1, 4, nullptr},
            {kTubeSagTime, "Recovery", false, kPre, 1, 5, nullptr},
            {kTubeStage2, "Stage 2", true, kPre, 2, 0, nullptr},
            {kTubeGain2, "Gain 2", false, kPre, 2, 1, nullptr},
            {kTubeBias2, "Bias 2", false, kPre, 2, 2, nullptr},
            {kTubeCap2, "Cap 2", false, kPre, 2, 3, nullptr},
            {kTubePlateCap2, "Plate Cap 2", false, kPre, 2, 4, nullptr},
            {kTubeCouple, "Couple", false, kPre, 2, 5, nullptr},
            {kTubeCathodeFollower, "Follower", true, kPre, 3, 0,
             "Cathode follower: the Marshall / 5150 stage between the preamp and the tone stack. It follows the "
             "last gain stage's plate, but the tone stack loads it, so on big downward swings it cuts off and "
             "flattens the bottom of the wave, and at the very top its grid conducts and rounds the plate over: "
             "lopsided, compressed crunch that thickens as you dig in. A little quieter when driven hard (it "
             "clips), as on the real amp. Circuit engine only."},
            {kTubeStack, "Tone Stack", false, kPre, 0, 6, nullptr},
            {kTubeBass, "Bass", false, kPre, 1, 6, nullptr},
            {kTubeMid, "Mid", false, kPre, 2, 6, nullptr},
            {kTubeTreble, "Treble", false, kPre, 3, 6, nullptr},
            // POWER AMP: the power amp, then how it's biased and where its feedback comes from.
            {kTubePower, "Power Amp", true, kPow, 0, 0, nullptr},
            {kTubeMaster, "Master", false, kPow, 0, 1, nullptr},
            {kTubePowerSag, "Sag", false, kPow, 0, 2, nullptr},
            {kTubeFeedback, "Feedback", false, kPow, 0, 3, nullptr},
            {kTubePresence, "Presence", false, kPow, 0, 4, nullptr},
            {kTube6L6, "6L6", true, kPow, 0, 5, nullptr},
            {kTubeCathodeBias, "Cath Bias", true, kPow, 1, 0,
             "Cathode bias (AC30, Deluxe, 18-watt style) instead of a fixed bias supply: the power tubes share a "
             "cathode resistor and bypass cap, so when you dig in their average current rises, the cathode "
             "rises, and they bias themselves colder -- the power amp sags and blooms back over ~50 ms. Only "
             "noticeable with the power amp pushed (Master up). Needs Power Amp on."},
            {kTubeSpeakerFeedback, "Spkr FB", true, kPow, 1, 1,
             "Negative feedback taken from the speaker side of the output transformer, as real amps do, instead "
             "of from the plates: the transformer's bass and treble roll-offs sit inside the loop, so Feedback "
             "no longer flattens them -- a looser, fuller low end (+2 dB at 40-70 Hz at your settings). Needs "
             "Power Amp on; most noticeable with Feedback up."},
            {kTubeQuad, "4 Tubes", true, kPow, 1, 3,
             "Four power tubes (a quad, like a 100 W head) instead of two: two per side in parallel into half the "
             "load, a stiffer supply and a bigger output transformer. Small signals are as loud; the supply sags "
             "the same per tube and the transformer saturates later, so big chords stay firmer. With failing on, "
             "a failed tube only halves its side. Needs Power Amp on."},
            {kTubeResonance, "Resonance", false, kPow, 1, 2,
             "Resonance (like a Mesa's or a 5150's): takes part of the negative feedback away below ~150 Hz, so "
             "the power amp's low end comes back -- and with Spkr Load on, the speaker's resonance thump (+3.5 dB "
             "at 70 Hz at 10). 0 is off. Needs Power Amp on and some Feedback."},
            // FEEL: the Real Feel switches and Depth, Touch, and 60 Hz.
            {kTubeScreenSquish, "Squish", true, kFeel, 0, 0,
             "Screen squish: when the power tubes' plates swing low, their screens take the current the plates "
             "can't, so the screen supply sags and the power amp compresses -- the spongy feel of a cranked "
             "amp. Above 1x Depth it squashes your pick attack harder and lets it bloom back as the screens "
             "recover. Needs Power Amp on."},
            {kTubeSharedSupply, "Shared B+", true, kFeel, 0, 1,
             "Shared supply: the preamp's B+ hangs off the power amp's supply (through a decoupling filter), so "
             "hard power-amp hits sag the preamp too and the whole amp breathes together. Needs Power Amp on."},
            {kTubeOTSaturation, "OT Iron", true, kFeel, 0, 2,
             "Output transformer saturation: big, low notes at volume saturate the transformer's iron, so the low "
             "end compresses and thickens. Needs Power Amp on."},
            {kTubeCoilHeat, "Coil Heat", true, kFeel, 0, 3,
             "Voice coil heating: over a few seconds of loud playing the speaker's coil warms and its resistance "
             "rises, so the amp's load changes -- slow speaker compression. Needs Power Amp and Spkr Load on."},
            {kTubeFeelDepth, "Depth", false, kFeel, 0, 4,
             "How strongly the Real Feel switches act. 1x is real parts; higher exaggerates them (softer screen "
             "supply, smaller transformer, hotter coil, more of the sag reaching the preamp), up to 10x. Feedback "
             "cancels much of what they do, as on a real amp, so high-Feedback settings need more Depth. With the "
             "REAL FEEL pedal in the chain, these switches act there instead (after the AMP block), not in the "
             "Tube Lab."},
            {kTubeTouch, "Touch", false, kFeel, 1, 0,
             "Touch: picks softer than your recent hardest also get less drive into the tubes, so they clean up "
             "while hard picks keep the full sound. Loudness and sustain don't change, only how dirty a note is. 0 "
             "is off; at 3, every dB softer you pick takes about 0.9 dB more off the drive (3 dB at 10). Works on "
             "any setting, but at very high Drive even soft picks are past breakup: lower Drive so your hard picks "
             "sit just past it. Not a real amp part. Works without the power amp and without the REAL FEEL pedal."},
            {kTubeRipple, "60 Hz", true, kFeel, 1, 2,
             "60 Hz: the amp's power supply ripple. A real amp's B+ is rectified 60 Hz mains, so it carries a 120 Hz "
             "ripple. Here it starves a clipping stage after the tubes: its headroom wobbles at 120 Hz, more when you "
             "dig in, so sustained notes sputter and grind; plus a faint hum. Amount sets how starved, Mix blends it "
             "with the clean Tube Lab sound. Works in both engines, with or without the power amp. Stays in the Tube "
             "Lab even with the REAL FEEL pedal placed. Into a high-gain AMP block, the hum gets amplified with "
             "everything else: keep Amount low there. With the AMP's Gate on, the hum is gated out between notes, "
             "like any other noise."},
            {kTubeRippleAmount, "Amount", false, kFeel, 1, 3,
             "How starved the supply is: 0 is barely any grind, 5 a clear 120 Hz grind on held notes, 7-10 ramp up to "
             "a full sputtering fuzz. Level-matched to your loudest picks. The hum rises with it (about -56 dBFS at "
             "10, at the Tube Lab's output)."},
            {kTubeRippleMix, "Mix", false, kFeel, 1, 4,
             "60 Hz dry/wet: how much of the grind (and hum) is blended with the clean Tube Lab sound, in parallel. "
             "100% is the full grind, 50% about 6 dB less of it."},
          };
          for (const Cell& cell : cells)
          {
            // POWER AMP and FEEL have two rows: the same cell size as PREAMP's, centred on the page.
            const IRECT pageArea = cell.tab == kPre ? lowerArea : lowerArea.GetMidVPadded(lowerArea.H() * 0.25f);
            const IRECT cellArea = pageArea.GetGridCell(cell.row, cell.col, cell.tab == kPre ? kTubeRows : 2, kTubeCols);
            IControl* control = nullptr;
            if (cell.isSwitch)
              control = new NAMSwitchControl(cellArea.GetCentredInside(std::min(110.f, cellArea.W()), std::min(60.f, cellArea.H())),
                                             cell.paramIdx, cell.label, style, switchHandleBitmap);
            else
            {
              auto* knob = new NAMKnobControl(cellArea.GetPadded(-4.f), cell.paramIdx, cell.label, style, knobBackgroundBitmap);
              knob->SetSideText(true);
              control = knob;
            }
            if (cell.tooltip)
              control->SetTooltip(cell.tooltip);
            pGraphics->AttachControl(control);
            control->Hide(true);
            addToTab(control, cell.tab);
          }
          // Only after attaching: SetDisabled redraws, and a knob looks its param up through the UI
          // it's attached to (calling it before AttachControl crashed on launch in 1.9.0).
          _RefreshTubeLabControls(pGraphics);
          continue;
        }

        if (ExtraCabIndex(p) >= 0)
        {
          // CAB 2 / CAB 3: On, the IR picker (the same folders as CAB LAB's) with the loaded IR's
          // name under it, then Level and Pan.
          const int k = ExtraCabIndex(p);
          const int p0 = k == 0 ? kCab2Active : kCab3Active;
          auto attach = [&](IControl* c, int tag = kNoTag) {
            pGraphics->AttachControl(c, tag);
            c->Hide(true);
            mPedalDetailControls[p].push_back(c);
            return c;
          };
          const float labelH = std::clamp(pedalDetailArea.H() * 0.16f, 18.f, 30.f);
          const IRECT switchRow = pedalDetailArea.GetFromTop(labelH);
          const IRECT body = pedalDetailArea.GetReducedFromTop(labelH + 6.f);
          attach(new NAMSwitchControl(switchRow.GetMidHPadded(std::min(70.f, switchRow.W() * 0.5f)), p0 + 1, "On", style,
                                      switchHandleBitmap));
          const IRECT irArea = body.GetFromTop(std::min(64.f, body.H() * 0.45f)).GetPadded(-4.f);
          const std::string griffinDir = GriffinLibraryDir();
          const std::vector<std::string> irPaths = {
            griffinDir + "tone", griffinDir + "IRs and Amp Profiles (copied)\\Griffin tone folder captures"};
          attach(new NAMPresetBrowserControl(irArea.GetFromTop(irArea.H() * 0.55f), k == 0 ? "CAB 2 IR" : "CAB 3 IR",
                                             "wav", irPaths, false,
                                             [this, k](const WDL_String& fileName, const WDL_String&) {
                                               if (fileName.GetLength())
                                                 _StageExtraCabIR(k, fileName);
                                             },
                                             style));
          attach(new IVLabelControl(irArea.GetFromBottom(irArea.H() * 0.45f),
                                    mExtraCabIRPath[k].GetLength() ? mExtraCabIRPath[k].get_filepart()
                                                                   : "No IR loaded (passes the signal through)",
                                    DEFAULT_STYLE.WithDrawFrame(false).WithValueText(IText(
                                      14, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.7f), "Roboto-Regular"))),
                 k == 0 ? kCtrlTagCab2IRName : kCtrlTagCab3IRName);
          const IRECT knobRow = body.GetReducedFromTop(irArea.H() + 8.f);
          const float knobSize = std::clamp(std::min(knobRow.W() / 3.f, knobRow.H()), 50.f, NAM_KNOB_HEIGHT);
          attach(new NAMKnobControl(knobRow.GetGridCell(0, 0, 1, 2).GetCentredInside(knobSize), p0 + 2, "Level", style,
                                    knobBackgroundBitmap));
          attach(new NAMKnobControl(knobRow.GetGridCell(0, 1, 1, 2).GetCentredInside(knobSize), p0 + 3, "Pan", style,
                                    knobBackgroundBitmap))
            ->SetTooltip("Where this cab sits left to right (centred: full level on both sides).");
          continue;
        }

        if (_IsDynamicsPedal(p))
        {
          // GATE / PRO COMP (1.52.0): On, what it is and its meter across the top, then two rows of
          // knobs and switches (dial left, text right, like the Tube Lab's).
          const bool isGate = p == chaingrid::kGate;
          struct Ctl
          {
            int param;
            const char* label;
            bool isSwitch;
            const char* tip;
          };
          std::vector<Ctl> ctls;
          if (isGate)
            ctls = {
              {kGateThreshold, "Threshold", false,
               "Opens when what it listens to goes above this (dBFS). Watch the KEY meter: set it just above "
               "the bar's level with the strings muted, under your softest notes."},
              {kGateHysteresis, "Close", false,
               "How far under the threshold it waits before closing (dB). A few dB stops it chattering on a "
               "note that hovers around the threshold; more lets notes ring out longer."},
              {kGateAttack, "Attack", false, "How fast it opens (ms). 0.5 ms keeps the pick; slower softens it."},
              {kGateHold, "Hold", false,
               "How long it stays open after the level drops under the close point (ms): rides over the gaps "
               "in fast riffs and palm mutes."},
              {kGateRelease, "Release", false,
               "How long it takes to close all the way (ms). Short is tight and choppy (metal stops); long lets "
               "notes fade naturally."},
              {kGateRange, "Range", false,
               "How far it turns down when closed: -90 is silence, -20 just lowers the hiss between notes."},
              {kGateKeyHpf, "Key HPF", false,
               "A high-pass on what the gate listens to (20 Hz = off). Raise it if mains hum or low rumble keeps "
               "it open; the sound itself isn't filtered."},
              {kGateKey, "Key Input", true,
               "On: the gate listens to your clean guitar (after Master In), wherever it sits, so after a "
               "high-gain amp the amp's hiss and added sustain can't hold it open and your picking decides. "
               "Off: it listens to its own input."},
            };
          else
            ctls = {
              {kSCompThreshold, "Threshold", false,
               "Level (dBFS) above which it compresses. Watch the IN meter: the marker is the threshold."},
              {kSCompRatio, "Ratio", false,
               "How hard: 4:1 means 4 dB over the threshold comes out 1 dB over. 20 = Limit (nothing gets over)."},
              {kSCompAttack, "Attack", false,
               "How fast it clamps down (ms). Fast (0.1-1 ms) catches the pick; 10-30 ms lets the pick snap "
               "through, then holds the note."},
              {kSCompRelease, "Release", false,
               "How fast it lets go (ms). Short pumps and adds sustain; long is smooth. Opto slows it further "
               "after heavy squeezing."},
              {kSCompKnee, "Knee", false, "0 dB is a hard knee; higher eases into compression around the threshold."},
              {kSCompMakeup, "Makeup", false, "Output gain (dB), on top of Auto Makeup if that's on."},
              {kSCompAutoMakeup, "Auto Gain", true,
               "Adds back what a -12 dBFS peak loses, so turning Threshold and Ratio doesn't change the loudness much."},
              {kSCompMix, "Mix", false, "Dry to compressed (%). 30-60% is parallel (New York) compression: punch "
                                       "and sustain without losing the pick."},
              {kSCompStyle, "Style", false,
               "Clean: transparent, fast, peak-sensing (VCA). Opto: smooth, RMS-sensing, slower to let go the "
               "harder it works (LA-2A). FET: fast and aggressive with a little grit as it compresses (1176)."},
              {kSCompScHpf, "SC HPF", false,
               "A high-pass on what the compressor listens to (20 Hz = off). Raise it (80-150 Hz) so low notes "
               "and palm mutes don't pump everything."},
            };
          auto attach = [&](IControl* c) {
            pGraphics->AttachControl(c);
            c->Hide(true);
            mPedalDetailControls[p].push_back(c);
            return c;
          };
          const IRECT body = _ComputeDynamicsPopupCardArea(b).GetReducedFromTop(38.f).GetPadded(-10.f);
          const IRECT topRow = body.GetFromTop(std::min(80.f, body.H() * 0.3f));
          attach(new NAMSwitchControl(topRow.GetFromLeft(110.f).GetCentredInside(100.f, std::min(60.f, topRow.H())),
                                      PedalOf(p).engagedParam, "On", style, switchHandleBitmap));
          const IRECT meterArea = topRow.GetFromRight(std::min(330.f, topRow.W() * 0.5f));
          attach(new IVLabelControl(
            IRECT(topRow.L + 120.f, topRow.T, meterArea.L - 10.f, topRow.B),
            isGate ? "A full noise gate. Key Input listens to your clean guitar, so it stays solid after a high-gain amp."
                   : "A studio compressor: Clean, Opto or FET, with parallel Mix and a sidechain high-pass.",
            DEFAULT_STYLE.WithDrawFrame(false).WithValueText(
              IText(13, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.7f), "Roboto-Regular"))));
          NAMDynamicsMeterControl::Callbacks mc;
          if (isGate)
          {
            mc.keyDB = [this]() { return mGate.meterKeyDB.exchange(-120.f, std::memory_order_relaxed); };
            mc.closedDB = [this]() { return mGate.meterClosedDB.exchange(0.f, std::memory_order_relaxed); };
            mc.isOpen = [this]() { return mGate.meterOpen.load(std::memory_order_relaxed); };
            mc.thresholdDB = [this]() { return (float)GetParam(kGateThreshold)->Value(); };
            mc.closeDB = [this]() {
              return (float)(GetParam(kGateThreshold)->Value() - GetParam(kGateHysteresis)->Value());
            };
          }
          else
          {
            mc.inDB = [this]() { return mStudioComp.meterInDB.exchange(-120.f, std::memory_order_relaxed); };
            mc.grDB = [this]() { return mStudioComp.meterGR.exchange(0.f, std::memory_order_relaxed); };
            mc.outDB = [this]() { return mStudioComp.meterOutDB.exchange(-120.f, std::memory_order_relaxed); };
            mc.thresholdDB = [this]() { return (float)GetParam(kSCompThreshold)->Value(); };
          }
          attach(new NAMDynamicsMeterControl(meterArea, isGate, mc));
          const IRECT rows = body.GetReducedFromTop(topRow.H() + 8.f);
          const int n = (int)ctls.size(), cols = (n + 1) / 2;
          for (int k = 0; k < n; k++)
          {
            const IRECT cellArea = rows.GetGridCell(k / cols, k % cols, 2, cols);
            IControl* c = nullptr;
            if (ctls[k].isSwitch)
              c = new NAMSwitchControl(cellArea.GetCentredInside(std::min(110.f, cellArea.W() - 4.f), std::min(60.f, cellArea.H())),
                                       ctls[k].param, ctls[k].label, style, switchHandleBitmap);
            else
            {
              auto* knob = new NAMKnobControl(cellArea.GetPadded(-4.f), ctls[k].param, ctls[k].label, style,
                                              knobBackgroundBitmap);
              knob->SetSideText(true);
              c = knob;
            }
            attach(c)->SetTooltip(ctls[k].tip);
          }
          continue;
        }

        if (_IsWidePedal(p))
        {
          // 1.44.0 pedals: a wider card (see _ComputeWidePopupCardArea), the On switch on top, then
          // one row of knobs and switches.
          struct Ctl
          {
            int param;
            const char* label;
            bool isSwitch;
            const char* tip;
          };
          std::vector<Ctl> ctls;
          const char* about = "";
          switch (p)
          {
            case chaingrid::kSkyVerb:
              about = "Studio reverb in the spirit of a Strymon BigSky.";
              ctls = {{kSkyMode, "Mode", false,
                       "Hall: big and smooth. Plate: dense, bright, instant. Room: small. Shimmer: an octave up keeps "
                       "rising in the tail (Mod sets how much). Cloud: huge and ambient. Bloom: swells in after each note."},
                      {kSkyDecay, "Decay", false, "How long the reverb rings: 0.3 s up to 40 s."},
                      {kSkyPredelay, "Pre-delay", false, "A gap before the reverb starts (0-500 ms): keeps the note clear."},
                      {kSkyTone, "Tone", false, "Dark to bright: how fast the reverb's top end dies away."},
                      {kSkyMod, "Mod", false, "Chorus-like movement in the tail. In Shimmer, how much octave-up."},
                      {kSkyMix, "Mix", false, "Dry to wet."},
                      {kSkyFreeze, "Freeze", true,
                       "Holds the reverb as it is, forever (no new notes go in): pad underneath you. Map it to a "
                       "footswitch with MIDI learn."}};
              break;
            case chaingrid::kFlangerDoubler:
              about = "After the MXR 126 Flanger/Doubler.";
              ctls = {{kFDDoubler, "Doubler", true,
                       "Off: flanger (a short swept delay, the jet sound). On: doubler (a 15-70 ms wandering delay that "
                       "sounds like the part played twice)."},
                      {kFDManual, "Manual", false, "The delay time: flanger 0.3-4 ms, doubler 15-70 ms."},
                      {kFDWidth, "Width", false, "How far the delay sweeps (flanger) or wanders (doubler)."},
                      {kFDSpeed, "Speed", false, "The sweep's speed."},
                      {kFDRegen, "Regen", false, "Feedback: + sharp and metallic, - hollow. In Doubler, repeats."},
                      {kFDMix, "Mix", false, "How much of the delayed signal: 5 is the classic even blend."}};
              break;
            case chaingrid::kWah:
              about = "Cry Baby, Vox V847 or auto-wah. Map Position to your expression pedal with MIDI learn.";
              ctls = {{kWahModel, "Model", false,
                       "Cry Baby: 350 Hz-2.2 kHz, sharp. V847: 450 Hz-1.6 kHz, rounder. Auto: opens with your picking."},
                      {kWahPosition, "Position", false,
                       "Heel (0) to toe (10). For a real wah, map it to the expression pedal in MIDI learn (that takes "
                       "its CC off the volume pedal). In Auto, where the sweep starts."},
                      {kWahQ, "Q", false, "How sharp the peak is: 5 is the pedal's own."},
                      {kWahSens, "Sens", false, "Auto: how far under your hardest picks it starts to open."},
                      {kWahLevel, "Level", false, "Output level."}};
              break;
            case chaingrid::kPrecisionDrive:
              about = "After the Horizon Devices Precision Drive: a tight overdrive for high gain amps.";
              ctls = {{kPDVolume, "Volume", false, "Output level."},
                      {kPDDrive, "Drive", false, "Gain into the clipping: a clean boost at 0, a saturated push at 10."},
                      {kPDAttack, "Attack", false,
                       "How much low end goes into the clipping: low is full and smooth, high is tight and percussive."},
                      {kPDGate, "Gate", false, "Noise gate: 0 off, then a threshold from -90 to -30 dBFS on the input."},
                      {kPDBright, "Bright", true, "More top end after the clipping."}};
              break;
            case chaingrid::kTrackGate:
              about = "Three gates: Tracking (after the ISP Decimator), Classic (Boss NS-2), Smart (MXR Smart Gate).";
              ctls = {{kTGModel, "Model", false, BaseParamHelp(kTGModel)},
                      {kTGThreshold, "Threshold", false, BaseParamHelp(kTGThreshold)},
                      {kTGRelease, "Release", false, BaseParamHelp(kTGRelease)},
                      {kTGHold, "Hold", false, BaseParamHelp(kTGHold)},
                      {kTGRange, "Range", false, BaseParamHelp(kTGRange)},
                      {kTGRatio, "Ratio", false, BaseParamHelp(kTGRatio)},
                      {kTGNoise, "Noise", false, BaseParamHelp(kTGNoise)},
                      {kTGKey, "Key In", true, BaseParamHelp(kTGKey)}};
              break;
            case chaingrid::kDigiDelay:
              about = "Digital delay after the Boss DD-3 / DD-8: clean repeats, chorused, warm or reversed.";
              ctls = {{kDigiMode, "Mode", false, BaseParamHelp(kDigiMode)},
                      {kDigiTime, "Time", false, BaseParamHelp(kDigiTime)},
                      {kDigiFeedback, "Feedback", false, BaseParamHelp(kDigiFeedback)},
                      {kDigiLevel, "Level", false, BaseParamHelp(kDigiLevel)},
                      {kDigiTapDiv, "Tap", false, BaseParamHelp(kDigiTapDiv)}};
              break;
            case chaingrid::kAnalogDelay:
              about = "Bucket-brigade delay after the MXR Carbon Copy: dark repeats that can run away.";
              ctls = {{kAnaTime, "Delay", false, BaseParamHelp(kAnaTime)},
                      {kAnaRegen, "Regen", false, BaseParamHelp(kAnaRegen)},
                      {kAnaMix, "Mix", false, BaseParamHelp(kAnaMix)},
                      {kAnaMod, "Mod", true, BaseParamHelp(kAnaMod)},
                      {kAnaRate, "Rate", false, BaseParamHelp(kAnaRate)},
                      {kAnaWidth, "Width", false, BaseParamHelp(kAnaWidth)},
                      {kAnaTapDiv, "Tap", false, BaseParamHelp(kAnaTapDiv)}};
              break;
            case chaingrid::kTapeEcho:
              about = "Tape echo after the Boss RE-20 / Roland Space Echo: three heads, wow, saturation and a spring.";
              ctls = {{kTapeHeads, "Heads", false, BaseParamHelp(kTapeHeads)},
                      {kTapeRate, "Rate", false, BaseParamHelp(kTapeRate)},
                      {kTapeIntensity, "Intensity", false, BaseParamHelp(kTapeIntensity)},
                      {kTapeEcho, "Echo", false, BaseParamHelp(kTapeEcho)},
                      {kTapeReverb, "Reverb", false, BaseParamHelp(kTapeReverb)},
                      {kTapeWow, "Wow", false, BaseParamHelp(kTapeWow)},
                      {kTapeDrive, "Saturate", false, BaseParamHelp(kTapeDrive)},
                      {kTapeTapDiv, "Tap", false, BaseParamHelp(kTapeTapDiv)}};
              break;
            case chaingrid::kSpringVerb:
              about = "Spring reverb after a '63 Fender Reverb / Boss FRV-1: the drip and splash of two springs.";
              ctls = {{kSprDwell, "Dwell", false, BaseParamHelp(kSprDwell)},
                      {kSprTone, "Tone", false, BaseParamHelp(kSprTone)},
                      {kSprMix, "Mix", false, BaseParamHelp(kSprMix)},
                      {kSprDecay, "Decay", false, BaseParamHelp(kSprDecay)}};
              break;
            case chaingrid::kApexPreamp:
            default:
              about = "After the Horizon Devices Apex Preamp: a three-stage high-gain preamp.";
              ctls = {{kApexGain, "Gain", false, "Preamp gain."},
                      {kApexBass, "Bass", false, "Low end (around 120 Hz)."},
                      {kApexMid, "Mid", false, "Mids (around 700 Hz)."},
                      {kApexTreble, "Treble", false, "Highs (above 2.8 kHz)."},
                      {kApexPresence, "Presence", false, "Upper highs (5 kHz and up)."},
                      {kApexVolume, "Volume", false, "Output level. It's a preamp: put a CAB after it."},
                      {kApexBright, "Bright", true, "More top end into the gain."}};
              break;
          }
          auto attach = [&](IControl* c) {
            pGraphics->AttachControl(c);
            c->Hide(true);
            mPedalDetailControls[p].push_back(c);
            return c;
          };
          const IRECT body = _ComputeWidePopupCardArea(b).GetReducedFromTop(38.f).GetPadded(-10.f);
          const IRECT topRow = body.GetFromTop(std::min(48.f, body.H() * 0.3f));
          attach(new NAMSwitchControl(topRow.GetFromLeft(110.f).GetCentredInside(100.f, topRow.H()),
                                      PedalOf(p).engagedParam, "On", style, switchHandleBitmap));
          attach(new IVLabelControl(topRow.GetReducedFromLeft(120.f), about,
                                    DEFAULT_STYLE.WithDrawFrame(false).WithValueText(
                                      IText(13, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.7f), "Roboto-Regular"))));
          const IRECT row = body.GetReducedFromTop(topRow.H() + 8.f);
          const int n = (int)ctls.size();
          for (int k = 0; k < n; k++)
          {
            const IRECT cellArea = row.GetGridCell(0, k, 1, n);
            IControl* c = ctls[k].isSwitch
                            ? (IControl*)new NAMSwitchControl(
                                cellArea.GetCentredInside(std::min(90.f, cellArea.W() - 4.f), std::min(56.f, cellArea.H())),
                                ctls[k].param, ctls[k].label, style, switchHandleBitmap)
                            : (IControl*)new NAMKnobControl(
                                cellArea.GetCentredInside(std::min(cellArea.W() - 4.f, NAM_KNOB_HEIGHT + 10.f),
                                                          std::min(cellArea.H(), NAM_KNOB_HEIGHT + 30.f)),
                                ctls[k].param, ctls[k].label, style, knobBackgroundBitmap);
            attach(c)->SetTooltip(ctls[k].tip);
          }
          continue;
        }

        if (p == chaingrid::kRealFeel)
        {
          // REAL FEEL: a tandem pedal with the Tube Lab. Its On switch is the Tube Lab's, and its
          // switches and Depth are the Tube Lab's Real Feel controls (the same params), so either
          // popup sets both.
          auto attach = [&](IControl* c) {
            pGraphics->AttachControl(c);
            c->Hide(true);
            mPedalDetailControls[p].push_back(c);
            return c;
          };
          const float labelH = std::clamp(pedalDetailArea.H() * 0.16f, 18.f, 30.f);
          const IRECT switchRow = pedalDetailArea.GetFromTop(labelH);
          const IRECT body = pedalDetailArea.GetReducedFromTop(labelH + 6.f);
          attach(new NAMSwitchControl(switchRow.GetMidHPadded(std::min(70.f, switchRow.W() * 0.5f)), kTubeEngaged, "On",
                                      style, switchHandleBitmap))
            ->SetTooltip("Tandem with the TUBE LAB: this is the Tube Lab's own On switch, so the two go on and off "
                         "together.");
          attach(new IVLabelControl(
            body.GetFromTop(18.f), "TANDEM WITH TUBE LAB",
            DEFAULT_STYLE.WithValueText(IText(12, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.7f), "Michroma-Regular"))
              .WithDrawFrame(false)));
          const IRECT row = body.GetReducedFromTop(22.f);
          struct FeelSwitch
          {
            int paramIdx;
            const char* label;
            const char* tooltip;
          };
          const std::array<FeelSwitch, 4> feelSwitches = {{
            {kTubeScreenSquish, "Squish",
             "The power section's screen supply sags on hard picks and recovers over ~150 ms: the pick is squashed, "
             "then blooms back."},
            {kTubeSharedSupply, "Shared B+",
             "The preamp shares the sagging supply: as it sags, the signal into the AMP block is turned down (up to "
             "6 dB), changing how hard the amp model is driven."},
            {kTubeOTSaturation, "OT Iron",
             "The output transformer's iron saturates on big low notes, taking low end out of the loudest ones."},
            {kTubeCoilHeat, "Coil Heat",
             "The speaker coil heats over a few seconds of loud playing: slightly quieter, with relatively more low "
             "end around the cone's resonance."},
          }};
          for (int k = 0; k < 4; k++)
          {
            const IRECT cellArea = row.GetGridCell(0, k, 1, 6);
            attach(new NAMSwitchControl(
                     cellArea.GetCentredInside(std::min(100.f, cellArea.W() - 4.f), std::min(56.f, cellArea.H())),
                     feelSwitches[k].paramIdx, feelSwitches[k].label, style, switchHandleBitmap))
              ->SetTooltip(feelSwitches[k].tooltip);
          }
          attach(new NAMKnobControl(row.GetGridCell(0, 4, 1, 6).GetPadded(-2.f), kTubeFeelDepth, "Depth", style,
                                    knobBackgroundBitmap))
            ->SetTooltip("How strongly the switches act: 1x is a real amp's power section, up to 10x exaggerated.");
          attach(new NAMKnobControl(row.GetGridCell(0, 5, 1, 6).GetPadded(-2.f), kRealFeelMix, "Mix", style,
                                    knobBackgroundBitmap))
            ->SetTooltip("Dry/wet: how much of REAL FEEL's effect you hear. 100% is the full effect, 0% passes the "
                         "signal through untouched, in between blends the two (they line up: the pedal adds no "
                         "delay). Shared B+'s turn-down of the AMP block's input follows it too.");
          continue;
        }

        if (p == kCabLabPedalIdx)
        {
          // CAB LAB: same big card as the Tube Lab. Top row: On, IR switch, speaker selector, IR
          // library button + the loaded IR's name. Then Mic A, Mic B, and room/output rows.
          const IRECT cabBody = _ComputeTubePopupCardArea(b).GetReducedFromTop(38.f).GetPadded(-10.f);
          const IRECT topRow = cabBody.GetFromTop(cabBody.H() * 0.24f);
          const IRECT lowerArea = cabBody.GetReducedFromTop(topRow.H() + 8.f);
          auto cell = [](const IRECT& row, int i) { return row.GetGridCell(0, i, 1, 6); };
          auto attach = [&](IControl* c) {
            pGraphics->AttachControl(c);
            c->Hide(true);
            mPedalDetailControls[p].push_back(c);
            return c;
          };
          auto addSwitch = [&](const IRECT& area, int param, const char* label) {
            return attach(new NAMSwitchControl(area.GetCentredInside(110.f, std::min(60.f, area.H())), param, label,
                                               style, switchHandleBitmap));
          };
          auto addKnob = [&](const IRECT& area, int param, const char* label) {
            return attach(new NAMKnobControl(area.GetPadded(-4.f), param, label, style, knobBackgroundBitmap));
          };

          addSwitch(cell(topRow, 0), kCabLabEngaged, "On");
          addSwitch(cell(topRow, 1), kCabSource, "IR");
          addKnob(cell(topRow, 2), kCabSpeaker, "Speaker");
          {
            const IRECT irArea = topRow.GetReducedFromLeft(topRow.W() * 0.5f).GetPadded(-6.f);
            const std::string griffinDir = GriffinLibraryDir();
            const std::vector<std::string> irPaths = {
              griffinDir + "tone", griffinDir + "IRs and Amp Profiles (copied)\\Griffin tone folder captures"};
            attach(new NAMPresetBrowserControl(irArea.GetFromTop(irArea.H() * 0.5f).GetPadded(-4.f), "CAB LAB IR", "wav",
                                               irPaths, false,
                                               [this](const WDL_String& fileName, const WDL_String&) {
                                                 if (fileName.GetLength() && _StageCabLabIR(fileName))
                                                   SendParameterValueFromUI(kCabSource, 1.0); // Loading one uses it
                                               },
                                               style));
            auto* irName = new IVLabelControl(
              irArea.GetFromBottom(irArea.H() * 0.5f),
              mCabLabIRPath.GetLength() ? mCabLabIRPath.get_filepart() : "No IR loaded",
              DEFAULT_STYLE.WithDrawFrame(false).WithValueText(
                IText(14, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.7f), "Roboto-Regular")));
            pGraphics->AttachControl(irName, kCtrlTagCabLabIRName);
            irName->Hide(true);
            mPedalDetailControls[p].push_back(irName);
          }

          const IRECT micARow = lowerArea.GetGridCell(0, 0, 3, 1);
          const IRECT micBRow = lowerArea.GetGridCell(1, 0, 3, 1);
          const IRECT roomRow = lowerArea.GetGridCell(2, 0, 3, 1);
          addKnob(cell(micARow, 0), kMicAType, "Mic A");
          addKnob(cell(micARow, 1), kMicAPos, "Position");
          addKnob(cell(micARow, 2), kMicADist, "Distance");
          addKnob(cell(micARow, 3), kMicALevel, "Level");
          addKnob(cell(micARow, 4), kMicAPan, "Pan");
          addSwitch(cell(micBRow, 0), kMicBOn, "Mic B");
          addKnob(cell(micBRow, 1), kMicBType, "Mic B");
          addKnob(cell(micBRow, 2), kMicBPos, "Position");
          addKnob(cell(micBRow, 3), kMicBDist, "Distance");
          addKnob(cell(micBRow, 4), kMicBLevel, "Level");
          addKnob(cell(micBRow, 5), kMicBPan, "Pan");
          addSwitch(cell(roomRow, 0), kMicBInvert, "Flip B");
          addKnob(cell(roomRow, 1), kRoomLevel, "Room");
          addKnob(cell(roomRow, 2), kRoomSize, "Size");
          addKnob(cell(roomRow, 3), kRoomWidth, "Width");
          addKnob(cell(roomRow, 4), kCabOutput, "Output");
          // 1.59.0: the SPEAKER LAB page (4x12 / 2x12 / 1x12 / 4x10 and four mics placed in 3D).
          attach(new NAMFlatButtonControl(cell(roomRow, 5).GetCentredInside(cell(roomRow, 5).W() - 6.f, 28.f),
                                          "SPEAKER LAB", [this]() { _ShowSpeakerLab(true); }))
            ->SetTooltip("The SPEAKER LAB: pick the cab (4x12, 2x12, 1x12, 4x10) and place up to four mics in X, Y "
                         "and Z, with front, side and 3D views and the response they make.");
          _RefreshCabLabControls(pGraphics);
          continue;
        }

        const float labelH = std::clamp(pedalDetailArea.H() * 0.16f, 18.f, 30.f);
        // This strip used to hold the on/off switch crammed into the popup's header row instead
        // (squeezed to ~20px tall, overlapping the pedal name text right next to the close button --
        // a real but nearly-unclickable target). It's a full-width row of the popup body, so give
        // the switch a properly sized, clearly separate spot here instead.
        const auto switchRow = pedalDetailArea.GetFromTop(labelH);
        const auto switchArea = switchRow.GetMidHPadded(std::min(70.f, switchRow.W() * 0.5f));
        const auto panelKnobsArea = pedalDetailArea.GetReducedFromTop(labelH + 10.f);

        const PedalDef& def = PedalOf(p);
        const int engagedParam = def.engagedParam;

        auto* onOffSwitch = new NAMSwitchControl(switchArea, engagedParam, "On", style, switchHandleBitmap);
        pGraphics->AttachControl(onOffSwitch);
        onOffSwitch->Hide(true);
        mPedalDetailControls[p].push_back(onOffSwitch);

        // DELAY (and its copies) also gets its Tap note value (1.71.0), as a fifth knob.
        std::vector<std::pair<int, const char*>> knobs;
        for (int k = 0; k < 4; k++)
          if (def.knobs[k].paramIdx >= 0)
            knobs.push_back({def.knobs[k].paramIdx, def.knobs[k].label});
        if (chaingrid::IsPedal(p) && !chaingrid::IsLatePedal(p) && chaingrid::PedalType(p) == 3)
          knobs.push_back({kDelayTapDiv + chaingrid::PedalCopy(p), "Tap"});
        const int nKnobs = (int)knobs.size();
        const float knobSize = std::clamp(std::min(panelKnobsArea.W() / 4.5f, panelKnobsArea.H() * 0.7f), 55.f,
                                          NAM_KNOB_HEIGHT);
        const float knobSpacing = knobSize * 1.3f;
        const float rowWidth = nKnobs * knobSpacing;
        const float rowLeft = panelKnobsArea.MW() - rowWidth * 0.5f;
        const float knobTop = panelKnobsArea.T + std::max(0.f, (panelKnobsArea.H() - knobSize) * 0.25f);
        for (int k = 0; k < nKnobs; k++)
        {
          const float cx = rowLeft + knobSpacing * (k + 0.5f);
          const IRECT knobArea(cx - knobSize * 0.5f, knobTop, cx + knobSize * 0.5f, knobTop + knobSize);
          auto* knob = new NAMKnobControl(knobArea, knobs[k].first, knobs[k].second, style, knobBackgroundBitmap);
          pGraphics->AttachControl(knob);
          knob->Hide(true);
          mPedalDetailControls[p].push_back(knob);
        }
      }

      // CAB popup (1.24.0): the cab IR's own controls -- On switch, the IR picker, and the
      // right-channel (stereo cab) IR. They used to sit under the amp's Model picker in the AMP
      // popup, leaving CAB's popup empty; they're tagged, so they can only live in one place.
      {
        auto attachCab = [&](IControl* c, int tag = kNoTag) {
          pGraphics->AttachControl(c, tag);
          c->Hide(true);
          mCabDetailControls.push_back(c);
          return c;
        };
        const IRECT body = pedalPopupBodyArea;
        const float labelH = std::clamp(body.H() * 0.16f, 18.f, 30.f);
        const IRECT switchRow = body.GetFromTop(labelH);
        attachCab(new NAMSwitchControl(switchRow.GetMidHPadded(std::min(70.f, switchRow.W() * 0.5f)), kIRToggle, "On",
                                       style, switchHandleBitmap))
          ->SetTooltip("Cab IR on/off");

        const IRECT browserArea(body.L + 8.f, switchRow.B + 28.f, body.R - 8.f, switchRow.B + 28.f + 34.f);
        attachCab(new NAMFileBrowserControl(browserArea, kMsgTagClearIR, defaultIRString.c_str(), "wav",
                                            loadIRCompletionHandler, style, fileSVG, crossSVG, leftArrowSVG,
                                            rightArrowSVG, fileBackgroundBitmap, globeSVG, "Get IRs", getUrl),
                  kCtrlTagIRFileBrowser);

        // Right-channel IR (stereo cab). Until you load one, the right channel mirrors the main IR
        // with its own independent state -- a mono cab.
        const IRECT rRow(body.L + 8.f, browserArea.B + 18.f, body.R - 8.f, browserArea.B + 18.f + 28.f);
        const IRECT rClearArea = rRow.GetFromRight(20.f).GetMidVPadded(10.f);
        const IRECT rButtonArea = IRECT(rClearArea.L - 6.f - 28.f, rRow.T, rClearArea.L - 6.f, rRow.B);
        attachCab(new IVLabelControl(IRECT(rRow.L, rRow.T, rButtonArea.L - 8.f, rRow.B), "Right channel (stereo cab)",
                                     DEFAULT_STYLE
                                       .WithValueText(IText(13, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.7f),
                                                            "Roboto-Regular", EAlign::Far))
                                       .WithDrawFrame(false)));
        attachCab(new IVButtonControl(
                    rButtonArea,
                    [loadIRRCompletionHandler](IControl* pCaller) {
                      WDL_String fileName, path;
                      pCaller->GetUI()->PromptForFile(fileName, path, EFileAction::Open, "wav", loadIRRCompletionHandler);
                    },
                    "R", style),
                  kCtrlTagIRFileBrowserR)
          ->SetTooltip("Load a separate IR for the right channel (stereo cab). Without one, a stereo IR file plays its "
                       "own left and right channels, and a mono one plays on both sides.");
        // Back to mirroring the main IR. A harmless no-op if nothing was loaded (see mShouldRemoveIR_R).
        attachCab(new NAMSquareButtonControl(
                    rClearArea, [](IControl* pCaller) { pCaller->GetDelegate()->SendArbitraryMsgFromUI(kMsgTagClearIR_R); },
                    crossSVG))
          ->SetTooltip("Clear the right-channel IR (the right side goes back to following the main IR)");
      }

      // FX loop popup: On switch on top, Mix knob under it -- same layout as a pedal's popup.
      {
        const float labelH = std::clamp(pedalDetailArea.H() * 0.16f, 18.f, 30.f);
        const auto switchRow = pedalDetailArea.GetFromTop(labelH);
        const auto switchArea = switchRow.GetMidHPadded(std::min(70.f, switchRow.W() * 0.5f));
        const auto knobsArea = pedalDetailArea.GetReducedFromTop(labelH + 10.f);
        const float knobSize = std::clamp(std::min(knobsArea.W() / 4.5f, knobsArea.H() * 0.7f), 55.f, NAM_KNOB_HEIGHT);
        const float knobTop = knobsArea.T + std::max(0.f, (knobsArea.H() - knobSize) * 0.25f);
        const IRECT mixKnobArea(knobsArea.MW() - knobSize * 0.5f, knobTop, knobsArea.MW() + knobSize * 0.5f,
                                knobTop + knobSize);
        for (IControl* c : {(IControl*)new NAMSwitchControl(switchArea, kLoopEngaged, "On", style, switchHandleBitmap),
                            (IControl*)new NAMKnobControl(mixKnobArea, kLoopMix, "Mix", style, knobBackgroundBitmap)})
        {
          pGraphics->AttachControl(c);
          c->Hide(true);
          mLoopDetailControls.push_back(c);
        }
        mLoopDetailControls.back()->SetTooltip(
          "How much of the loop's return to use: 100% replaces the signal with the return, lower blends "
          "the signal arriving at LOOP IN back in");
      }

      // Actually attach every amp detail control now -- see addAmpDetail's comment above for why
      // this is deferred to here: after the popup chrome (mBlockPopupCardBg et al.) and every
      // other popup's own detail content, so the amp's knobs/browsers/switches end up at a higher
      // z-order and draw on top of the (possibly huge) card instead of being buried under it.
      for (auto& pending : pendingAmpDetails)
      {
        auto* attached = pGraphics->AttachControl(std::get<0>(pending), std::get<1>(pending),
                                                  std::get<2>(pending).c_str());
        attached->Hide(true);
        mAmpDetailControls.push_back(attached);
      }

      // Now that the popup chrome/content is fully attached, put the meters on top of all of it
      // (see attachMeters' definition/comment above) so they're genuinely always visible.
      attachMeters();
      attachSpecStrip(); // after the meters: the floating window goes over them too

      // Re-apply whatever was selected before this relayout (e.g. from a window resize) against
      // the freshly-built controls above -- otherwise the selection index would still be set but
      // nothing would actually be shown until the user clicked a block again.
      _SelectBlock(mSelectedBlock);
    }

    // Collapse/expand the AMP NEST panel on the right -- it eats into the main content
    // width while open, so this is the "get it out of the way" escape hatch. Reclaiming/giving back
    // that width needs a full relayout (RemoveAllControls + rebuild), but doing that synchronously
    // from inside this very button's own click callback would destroy the button control while its
    // callback is still executing -- deferred to the next OnIdle tick instead, well outside any
    // control's callback stack, same as the model/pedal loaders' error-message flags.
    // Hidden while any block's popup is open (see _SelectBlock) -- it's attached after the popup
    // chrome/deferred amp content, so without that it would float on top of even the Amp's
    // near-fullscreen popup, clickable and triggering a relayout out from under the open popup.
    mLibraryToggleButton = pGraphics->AttachControl(new NAMSquareButtonControl(
      libraryToggleButtonArea,
      [this](IControl* pCaller) {
        mShowLibraryPanel = !mShowLibraryPanel;
        mLibraryPanelRelayoutPending = true;
      },
      mShowLibraryPanel ? leftArrowSVG : rightArrowSVG));
    mLibraryToggleButton->SetTooltip(mShowLibraryPanel ? "Hide the amp/pedal library panel"
                                                       : "Show the amp/pedal library panel");
    // Same isPopupBlock test _SelectBlock uses -- duplicated rather than shared since that's a
    // local lambda scoped to _SelectBlock, not reachable from here.
    const bool anyPopupOpenAtLayout =
      chaingrid::IsPedal(mSelectedBlock) || mSelectedBlock == kCabBlockId || mSelectedBlock == kAmpBlockId
      || mSelectedBlock == kLoopBlockId;
    mLibraryToggleButton->Hide(anyPopupOpenAtLayout);

    // Settings and Tuner pages: full-window overlays, attached this late (after the always-visible
    // routing view -- chain wire, AMP/CAB/pedal chips, amp knobs -- and everything else above) so
    // they draw on top of it instead of being buried underneath, same principle as the pedal/CAB
    // popup chrome elsewhere in this function.
    pGraphics
      ->AttachControl(new NAMSettingsPageControl(
                        b, backgroundBitmap, inputLevelBackgroundBitmap, switchHandleBitmap, crossSVG, style,
                        radioButtonStyle,
                        NAMBufferSizeControl::Callbacks{
                          [this]() {
                            if (mPendingBufferSize > 0)
                              return mPendingBufferSize;
                            if (mRequestedBufferSize > 0)
                              return mRequestedBufferSize;
                            return mDeviceBufferSize > 0 ? mDeviceBufferSize : GetBlockSize();
                          },
                          [this]() { return mPendingBufferSize > 0 ? 0 : mDeviceBufferSize; },
                          [this](int size) { mPendingBufferSize = size; },
                          [this]() { mPendingDriverPanel = true; },
                          [this]() { mPendingDevicePrefs = true; }},
#if defined(APP_API) && defined(OS_WIN)
                        true, // the standalone owns its audio device; a DAW owns the buffer
#else
                        false,
#endif
                        new NAMThemePickerControl(
                          IRECT(), NAMThemePickerControl::Callbacks{
                                     [this]() { return mThemeIndex; }, [this]() { return mThemeLight; },
                                     [this]() { return mShowSpecStrip; },
                                     [this](int theme, bool light) { _SetTheme(theme, light); },
                                     [this](bool show) { _SetSpecStripShown(show); }})),
                      kCtrlTagSettingsBox)
      ->Hide(true);
    // A theme change from Settings rebuilds the UI; put Settings back up so you can keep choosing.
    if (mReopenSettingsAfterRelayout)
    {
      mReopenSettingsAfterRelayout = false;
      pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
    }
    pGraphics
      ->AttachControl(new NAMTunerPageControl(tunerPanelArea, crossSVG, style), kCtrlTagTunerBox)
      ->Hide(true);
    {
      NAMTubeDebugPageControl::Callbacks callbacks;
      callbacks.isLogging = [this]() { return mTubeLog.IsOpen(); };
      callbacks.setLogging = [this](bool on) { _SetTubeLogging(on); };
      callbacks.openLogFolder = [this]() {
        const std::string dir = _TubeLogDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
#ifdef OS_WIN
        ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
      };
      callbacks.isAlwaysOn = [this]() { return mTubeProbesAlways; };
      callbacks.toggleAlwaysOn = [this, pGraphics]() {
        if (!mTubeProbesAlways)
        {
          const EMsgBoxResult r = _ShowMessageBox(
            pGraphics,
            "ALWAYS ON keeps the Tube Lab's probes recording every sample even with this page closed, so the readouts, "
            "the bench and the scope's history are already live when you open it.\n\n"
            "It costs audio CPU the whole time: about 3% more of a core (measured on your Tube Lab settings: 18.7% -> "
            "21.9%), which leaves less room before dropouts at small buffer sizes. While this page is open it also "
            "redraws the map and scope (GPU), as it always has; closed, it draws nothing.\n\nTurn it on?",
            "Tube Lab probes: ALWAYS ON", kMB_OKCANCEL);
          if (r != kOK)
            return;
        }
        mTubeProbesAlways = !mTubeProbesAlways;
        _SaveUiPrefs();
      };
      // Stays open across relayouts (a window resize rebuilds every control).
      auto* debugPage = new NAMTubeDebugPageControl(b, crossSVG, &mTubeScopeSettings, callbacks);
      pGraphics->AttachControl(debugPage, kCtrlTagTubeDebug);
      // The bench knobs (1.58.0), on the page's bench row.
      {
        const struct
        {
          int param;
          const char* label;
          const char* tip;
        } benchKnobs[NAMTubeDebugPageControl::kNumBenchKnobs] = {
          {kTubeVariac, "Variac",
           "Variac: the wall voltage into the amp (120 V stock). Lower it for less headroom everywhere, a saggier, "
           "browner feel and cooler heaters; raise it for more clean headroom and a hotter, tighter amp. The "
           "supplies follow in a fraction of a second, the cathodes' temperature over a few seconds."},
          {kTubeBiasPot, "Bias",
           "Bias pot: the power tubes' idle current, as % of stock (set at 120 V with new tubes). Colder means more "
           "crossover grit and less dissipation; hotter means smoother and fuller, until it red-plates. Watch the "
           "POWER TUBES readout."},
          {kTubePreampAge, "Pre Age",
           "Preamp tube age: 0% new .. 100% worn out. Worn cathodes have less gain and much less emission to spare, "
           "so hard peaks get rounded off (more so with the Variac low: cooler heaters)."},
          {kTubePowerAge, "Pwr Age",
           "Power tube age: 0% new .. 100% worn out. Less gain and idle current, and the peaks run into what the "
           "cathodes can still emit: a softer, compressed attack."}};
        for (int i = 0; i < NAMTubeDebugPageControl::kNumBenchKnobs; i++)
          debugPage
            ->AddChildControl(new NAMKnobControl(debugPage->BenchKnobRect(i), benchKnobs[i].param, benchKnobs[i].label,
                                                 style, knobBackgroundBitmap))
            ->SetTooltip(benchKnobs[i].tip);
      }
      debugPage->Hide(!mTubeDebugVisible);
    }
    // The Output EQ page (1.46.0): the final output's spectrum in detail, and the editable Output EQ.
    {
      auto* page = new NAMOverlayPanelControl(b, "OUTPUT EQ", crossSVG);
      pGraphics->AttachControl(page, kCtrlTagOutEqPage);
      const IRECT body = b.GetReducedFromTop(56.f).GetPadded(-24.f);
      const IRECT top = body.GetFromTop(64.f);
      page->AddChildControl(new NAMSwitchControl(top.GetFromLeft(110.f).GetCentredInside(100.f, 60.f), kOutEqOn, "On", style,
                                                 switchHandleBitmap))
        ->SetTooltip("Off: the Output EQ is out of the path (flat). Your bands are kept.");
      auto* trimKnob =
        new NAMKnobControl(top.GetReducedFromLeft(120.f).GetFromLeft(190.f), kOutEqTrim, "Trim", style, knobBackgroundBitmap);
      trimKnob->SetSideText(true);
      trimKnob->SetTooltip("Trim: the level into the Output EQ (before the bands).");
      page->AddChildControl(trimKnob);
      auto* outKnob =
        new NAMKnobControl(top.GetReducedFromLeft(320.f).GetFromLeft(190.f), kOutEqOutput, "Output", style, knobBackgroundBitmap);
      outKnob->SetSideText(true);
      page->AddChildControl(outKnob);
      page->AddChildControl(new NAMEqGraphControl(body.GetReducedFromTop(72.f), kOutEqBandFirst, kOutEqBandExtFirst,
                                                  kOutEqTrim, kOutEqOutput, &mOutTapIn, &mOutTapOut, "BEFORE EQ",
                                                  "OUTPUT", &mOutEqView));
      page->Hide(true);
    }

    // The OUTPUT page (1.58.0, OutputPageControls.h): Master Out, the limiter and the stereo controls.
    {
      auto* page = new NAMOverlayPanelControl(b, "OUTPUT", crossSVG);
      pGraphics->AttachControl(page, kCtrlTagOutputPage);
      const IRECT body = b.GetReducedFromTop(64.f).GetPadded(-24.f);
      const float gap = 16.f;
      const float cardW = std::min(360.f, (body.W() - 2.f * gap) / 3.f);
      const float cardH = body.H();
      const float left = body.MW() - 1.5f * cardW - gap;
      auto card = [&](int i) { return IRECT(left + i * (cardW + gap), body.T, left + i * (cardW + gap) + cardW, body.T + cardH); };
      auto knobRow = [&](const IRECT& c, int row) {
        return IRECT(c.L + 14.f, c.T + 40.f + row * 74.f, c.R - 14.f, c.T + 40.f + row * 74.f + 66.f);
      };
      auto sideKnob = [&](const IRECT& r, int param, const char* label, const char* tip) {
        auto* k = new NAMKnobControl(r, param, label, style, knobBackgroundBitmap);
        k->SetSideText(true);
        k->SetTooltip(tip);
        page->AddChildControl(k);
      };
      // LEVEL
      {
        const IRECT c = card(0);
        page->AddChildControl(new NAMOutputCardControl(
          c, "LEVEL",
          "Master Out: the level out of the whole chain, after the Output EQ and the stereo controls, into the "
          "limiter. If the OUT meter shows CLIP, turn this down or switch the limiter on.",
          c.T + 40.f + 80.f));
        sideKnob(knobRow(c, 0), kOutputLevel, "Master Out", "Master output: the level out of the whole chain.");
      }
      // LIMITER
      {
        const IRECT c = card(1);
        page->AddChildControl(new NAMOutputCardControl(
          c, "LIMITER",
          "A lookahead peak limiter: the output never goes past the Ceiling. It only turns down the brief pick "
          "peaks, so Master Out can go up without clipping. Adds 0.7 ms of latency.",
          c.T + 40.f + 3.f * 74.f + 44.f));
        page->AddChildControl(new NAMSwitchControl(knobRow(c, 0).GetFromLeft(110.f).GetCentredInside(100.f, 60.f),
                                                   kOutputLimiter, "On", style, switchHandleBitmap))
          ->SetTooltip("Output limiter on/off (it fades over a few ms, so switching doesn't click).");
        sideKnob(knobRow(c, 1), kLimiterCeiling, "Ceiling", "The most the output can reach (dBFS).");
        sideKnob(knobRow(c, 2), kLimiterRelease, "Release",
                 "How fast the limiter lets go after a peak. 30 ms (the default) barely moves the sustain; longer is "
                 "smoother but can pump, shorter is more transparent on fast playing.");
        const IRECT m = knobRow(c, 3);
        page->AddChildControl(new NAMOutputMeterControl(IRECT(m.L, m.T, m.R, m.T + 40.f),
                                                        NAMOutputMeterControl::Kind::GainReduction),
                              kCtrlTagLimiterGR);
      }
      // STEREO
      {
        const IRECT c = card(2);
        page->AddChildControl(new NAMOutputCardControl(
          c, "STEREO",
          "Width: 0% is mono (both outputs get L + R; this was the Mono switch), 100% is as it is, 200% is twice as "
          "wide. Widen: stereo from a mono chain. A delayed copy of the middle goes to the sides, opposite ways "
          "round, so it sounds wide, the bass stays centred, and played through one speaker nothing cancels.",
          c.T + 40.f + 2.f * 74.f + 44.f));
        sideKnob(knobRow(c, 0), kOutputWidth, "Width", "Stereo width: 0% mono, 100% as is, 200% twice as wide.");
        sideKnob(knobRow(c, 1), kOutputWiden, "Widen", "Makes stereo out of what's in the middle (a mono guitar).");
        const IRECT m = knobRow(c, 2);
        page->AddChildControl(new NAMOutputMeterControl(IRECT(m.L, m.T, m.R, m.T + 40.f),
                                                        NAMOutputMeterControl::Kind::Correlation),
                              kCtrlTagOutCorrelation);
      }
      page->Hide(true);
    }

    // The CHASSIS page (1.62.0; the whole amp in 3D, with the preamp sockets, since 1.68.0;
    // TubeChassisControls.h).
    {
      using namespace chassis;
      auto* page = new NAMOverlayPanelControl(b, "TUBE LAB CHASSIS", crossSVG);
      pGraphics->AttachControl(page, kCtrlTagTubeChassis);
      const IRECT body = b.GetReducedFromTop(52.f).GetPadded(-14.f);
      const IRECT sceneR = body.GetFromLeft(body.W() * 0.64f);
      const IRECT side = body.GetReducedFromLeft(sceneR.W() + 10.f);
      NAMTubeChassisControl::Callbacks cb;
      cb.view = [this]() {
        View v;
        const tube::TubeParams tp = _ReadTubeParams(mUiLab);
        v.circuit = tp.circuit;
        v.capture = tp.capture;
        v.engaged = GetParam(_TP(kTubeEngaged))->Value() > 0.5;
        v.title = mUiLab == 0 ? "GRIFFIN  TUBE LAB" : "GRIFFIN  TUBE LAB 2";
        v.variacV = GetParam(_TP(kTubeVariac))->Value();
        v.heat = v.variacV / tube::bench::kMainsNominal;
        v.inUse[0] = tp.circuit;
        v.inUse[1] = tp.circuit && tp.stage2;
        v.inUse[2] = tp.circuit && tp.cathodeFollower;
        v.inUse[3] = tp.circuit && tp.powerAmp;
        for (int k = 0; k < tube::kNumSockets; k++)
        {
          v.socketRole[k] = GetParam(_TP(SocketParam(k, kSockRole)))->Int();
          v.socketTube[k] = GetParam(_TP(SocketParam(k, kSockTube)))->Int();
          v.inUse[4 + k] = tp.socket[k].role != tube::kSocketOff;
        }
        for (int i = 0; i < tube::TubeStage::kNumStageLevels; i++)
          v.level[i] = mUiStageLevel[i].load(std::memory_order_relaxed);
        v.powerAmp = tp.circuit && tp.powerAmp;
        v.sixL6 = tp.sixL6;
        v.quad = tp.quad;
        for (int k = 0; k < 4; k++)
        {
          v.life[k] = mUiPowerLife[k].load(std::memory_order_relaxed);
          v.dead[k] = mUiPowerDead[k].load(std::memory_order_relaxed);
          v.dissipation[k] = mUiDissRatio[k].load(std::memory_order_relaxed);
        }
        const int knobs[6] = {kTubeDrive, kTubeBass, kTubeMid, kTubeTreble, kTubeMaster, kTubePresence};
        for (int k = 0; k < 6; k++)
          v.knob[k] = GetParam(_TP(knobs[k]))->GetNormalized();
        v.rk[0] = _UiStages()[0].StageRk(0);
        v.rk[1] = _UiStages()[0].StageRk(1);
        v.raKOhm = tp.plateKOhm;
        v.gain2 = tp.gain2;
        auto disp = [this](int param) {
          WDL_String s;
          GetParam(_TP(param))->GetDisplayWithLabel(s);
          return std::string(s.Get());
        };
        char buf[48];
        const double cinNF = tube::TubeParams::BloomToCinFarads(tp.bloom) * 1e9;
        snprintf(buf, sizeof(buf), cinNF < 10.0 ? "%.1f nF" : "%.0f nF", cinNF);
        v.value[kInputCap] = buf;
        snprintf(buf, sizeof(buf), "%.0f ohm", v.rk[0]);
        v.value[kRk1] = buf;
        v.value[kCk1] = disp(kTubeCathodeCap);
        v.value[kRa] = disp(kTubePlate);
        v.value[kCp1] = disp(kTubePlateCap);
        v.value[kCouple] = disp(kTubeCouple);
        v.value[kGain2Pot] = disp(kTubeGain2);
        snprintf(buf, sizeof(buf), "%.0f ohm", v.rk[1]);
        v.value[kRk2] = buf;
        v.value[kCk2] = disp(kTubeCap2);
        v.value[kCp2] = disp(kTubePlateCap2);
        return v;
      };
      cb.selected = [this]() { return mChassisSelected; };
      cb.select = [this](int item) {
        mChassisSelected = item;
        mChassisSignature = -1.0; // Redraw now
      };
      cb.nudgeVariac = [this](double d) {
        const double normalized = std::clamp(GetParam(_TP(kTubeVariac))->GetNormalized() + d, 0.0, 1.0);
        const int variac = _TP(kTubeVariac);
        BeginInformHostOfParamChangeFromUI(variac);
        SendParameterValueFromUI(variac, normalized);
        EndInformHostOfParamChangeFromUI(variac);
        if (auto* ui = GetUI())
          ui->ForControlWithParam(variac, [normalized](IControl* c) { c->SetValueFromDelegate(normalized); });
      };
      cb.go = [this](int link) { _ChassisGo(link); };
      cb.yaw = &mChassisYaw;
      cb.pitch = &mChassisPitch;
      page->AddChildControl(new NAMTubeChassisControl(sceneR, cb));

      // The side column: what's selected and what it does, its controls, links to its pages, failing, the
      // power tubes.
      struct ItemInfo
      {
        const char* title;
        const char* text;
      };
      static const ItemInfo kInfo[kNumItems] = {
        {"INPUT CAP (BLOOM)",
         "The coupling cap into V1's grid, 2.2 to 220 nF. Big, it passes the lows and a hard pick charges it, so the "
         "tube sits colder for a moment after (blocking) and swells back: bloom. Small: tighter, brighter, cleaner "
         "recovery."},
        {"V1 CATHODE RESISTOR (BIAS)",
         "Sets where V1 idles: the resistor is the value that gives this grid bias. Toward 0 V: more gain, and it "
         "clips on top (grid current) sooner. Colder: it cuts off sooner on the bottom -- sputterier, more "
         "asymmetric."},
        {"V1 BYPASS CAP",
         "Across V1's cathode resistor: full gain above its corner, less below. None: clean, tight, less gain. "
         "0.68 uF: tight lows into the gain (the classic high-gain trick). 25 uF: full and fat."},
        {"PLATE RESISTORS",
         "The plate loads of V1, V2 and every gain socket. Bigger: more gain, and more squash before the stage clips. "
         "Smaller: cleaner, more headroom."},
        {"V1 PLATE CAP",
         "Across V1's plate resistor: rolls off fizz above a few kHz. None is the brightest; 1 nF is dark."},
        {"COUPLING CAP (V1 TO V2)",
         "Between the stages. Small: less low end into V2, so a lot of gain stays tight. Big: the full low end (and "
         "more blocking on hard hits)."},
        {"GAIN 2 POT", "How much of V1 reaches V2 (an audio-taper pot): how hard the second stage is pushed."},
        {"V2 CATHODE RESISTOR (BIAS 2)",
         "Where V2 idles, like V1's. A little colder than V1 is the classic high-gain choice: sharper, more "
         "aggressive clipping."},
        {"V2 BYPASS CAP", "V2's cathode bypass, like V1's: small for tight, big for full."},
        {"V2 PLATE CAP", "Across V2's plate resistor. 470 pF is the classic: it tames the second stage's fizz."},
        {"V1  12AX7  INPUT STAGE",
         "The first gain stage: your guitar straight into its grid. Drive sets how hard. It glows with what you play."},
        {"V2  12AX7  GAIN STAGE",
         "The second gain stage, cascaded after V1 through the Gain 2 pot and the coupling cap. Off, it sits cold."},
        {"V3  12AX7  CATHODE FOLLOWER",
         "Drives the tone stack from a low impedance, and squashes and rounds the biggest swings (Marshall, 5150). It "
         "follows the last gain stage, sockets included. Off, it sits cold."},
        {"V4  12AX7  PHASE INVERTER",
         "The long-tail pair that splits the signal into two opposite halves for the push-pull power tubes. It "
         "lights with the power amp."},
        {"VARIAC",
         "The wall voltage into the amp. Down: less headroom everywhere, a saggier feel, cooler and redder heaters. "
         "Up: more headroom and hotter, whiter heaters -- and with the power tubes biased hot, they can red-plate "
         "(and, with failing on, wear out). Drag the dial, or use the wheel over it."},
        {"", ""}, // The sockets: see socketInfo
        {"", ""},
        {"V9  POWER TUBE",
         "One side of the push-pull power amp, driven by V4. EL34: the British crunch, mids forward; 6L6: the American "
         "clean, deeper and tighter. Past its rated plate dissipation it red-plates (the plates glow); with failing on, "
         "it wears out and can fail. 4 Tubes makes the amp a quad: V11 joins V9 on this side, V12 joins V10."},
        {"V10  POWER TUBE", "The other side of the push-pull power amp, from V4's other output. See V9."},
        {"V11  POWER TUBE",
         "A quad's second tube on V9's side, in parallel with it (4 Tubes on): the two share that side's current. With "
         "4 Tubes off, an empty socket."},
        {"V12  POWER TUBE",
         "A quad's second tube on V10's side, in parallel with it (4 Tubes on). With 4 Tubes off, an empty socket."},
        {"OUTPUT TRANSFORMER",
         "Matches the power tubes to the speaker. With Spkr Load on, they drive a real speaker's impedance through it "
         "(bass resonance, treble rise); OT Iron (FEEL) lets its core saturate on big low notes."},
        {"POWER TRANSFORMER AND FILTER CAPS",
         "The supply: mains in, B+ out through the filter caps. How stiff it is sets the power amp's sag (Power Sag); "
         "Shared B+ lets the preamp sag with it; the 60 Hz switch adds its ripple."},
        {"INPUT JACK",
         "Your guitar in, after Drive. With Capture on, the AMP capture plays the preamp here instead of the tubes, "
         "and drives this power amp."},
        {"SPEAKER JACK", "The power amp out to the speaker: on to the CAB / CAB LAB in your chain."},
        {"FRONT PANEL", "The knobs you'd reach for: Drive, Bass, Middle, Treble, Master, Presence. They show the Tube "
                        "Lab's settings; set them here (Drive, Master) or on the PREAMP and POWER AMP tabs."}};
      auto socketInfo = [this](int k) {
        static const char* roles[tube::kNumSocketRoles] = {
          "An empty socket. Give it a job: GAIN (another gain stage after the one before it), FOLLOWER (a cathode "
          "follower on the stage before it), or BOOST (a gain stage with its cathode unbypassed: less gain, more "
          "headroom). The sockets run V5 then V6, after V2 and before V3.",
          "A gain stage like V2, driven by the stage before it through its own Gain pot and coupling cap. Each one "
          "stacked adds gain and compression, the way a 5150 or a Soldano cascades its stages.",
          "A cathode follower on the stage before it: it drives what's next from a low impedance and rounds off the "
          "biggest swings. It has no gain of its own, so only its tube type applies.",
          "A gain stage with its cathode unbypassed: local feedback leaves about a third of the gain, cleaner and "
          "tighter, with more headroom -- a clean boost into what's next. Cap is ignored."};
        static const char* tubes[tube::kNumTubeTypes] = {
          "12AX7: mu 100, the high-gain standard.",
          "12AT7: mu 60 and about four times the current: less gain, more headroom, a firmer drive.",
          "12AU7: mu 17: low gain, a lot of headroom, biased around -5 V. The clean-boost and driver tube.",
          "5751: mu 70, a 12AX7 with about 30% less gain: smoother, later breakup."};
        const int role = std::clamp(GetParam(_TP(SocketParam(k, kSockRole)))->Int(), 0, tube::kNumSocketRoles - 1);
        const int type = std::clamp(GetParam(_TP(SocketParam(k, kSockTube)))->Int(), 0, tube::kNumTubeTypes - 1);
        char title[64];
        snprintf(title, sizeof(title), "V%d  SOCKET  %s%s%s", 5 + k, role ? chassis::TubeTypeName(type) : "",
                 role ? "  " : "", chassis::SocketRoleName(role));
        std::string text = roles[role];
        if (role)
        {
          text += std::string(" ") + tubes[type];
          const double rk = _UiStages()[0].SocketRk(k);
          if (rk > 0.0)
          {
            char buf[64];
            snprintf(buf, sizeof(buf), " Its cathode resistor for this bias: %.0f ohm.", rk);
            text += buf;
          }
          if (!GetParam(_TP(kTubeCircuit))->Bool() || GetParam(_TP(kTubeCapture))->Bool())
            text += " (Not heard: the sockets need the Circuit engine, without Capture.)";
        }
        return std::make_pair(std::string(title), text);
      };
      const float textH = std::max(96.f, side.H() * 0.29f);
      page->AddChildControl(new NAMDynamicTextControl(side.GetFromTop(textH), [this, socketInfo]() {
        const int item = std::clamp(mChassisSelected, 0, (int)kNumItems - 1);
        if (IsSocket(item))
          return socketInfo(item - kSocket5);
        return std::make_pair(std::string(kInfo[item].title), std::string(kInfo[item].text));
      }));
      // The selected item's fields: rows of 22 px under the text (each control shown only while its item is
      // selected, see _UpdateChassis).
      using Mode = NAMLabFieldControl::Mode;
      const IColor gold(255, 214, 170, 90);
      const float rowH = 24.f, fieldsT = side.T + textH + 6.f;
      auto row = [&](int r) { return IRECT(side.L, fieldsT + r * rowH, side.R, fieldsT + r * rowH + rowH - 3.f); };
      mChassisFields.clear();
      auto field = [&](int item, const IRECT& r, int param, const char* label, Mode mode) {
        auto* f = new NAMLabFieldControl(r, param, label, mode, gold);
        page->AddChildControl(f);
        mChassisFields.push_back({item, f});
        return f;
      };
      const struct
      {
        int item, param;
        const char* label;
        Mode mode;
      } single[] = {{kInputCap, kTubeBloom, "Set", Mode::Drag},
                    {kRk1, kTubeBias, "Set", Mode::Drag},
                    {kCk1, kTubeCathodeCap, "Set", Mode::Stepper},
                    {kRa, kTubePlate, "Set", Mode::Drag},
                    {kCp1, kTubePlateCap, "Set", Mode::Stepper},
                    {kCouple, kTubeCouple, "Set", Mode::Stepper},
                    {kGain2Pot, kTubeGain2, "Set", Mode::Drag},
                    {kRk2, kTubeBias2, "Set", Mode::Drag},
                    {kCk2, kTubeCap2, "Set", Mode::Stepper},
                    {kCp2, kTubePlateCap2, "Set", Mode::Stepper},
                    {kTubeV1, kTubeDrive, "Drive", Mode::Drag},
                    {kTubeV2, kTubeStage2, "Stage 2", Mode::Toggle},
                    {kTubeV3, kTubeCathodeFollower, "Follower", Mode::Toggle},
                    {kTubeV4, kTubePower, "Power Amp", Mode::Toggle},
                    {kVariac, kTubeVariac, "Variac", Mode::Drag},
                    {kOutputTransformer, kTubeSpeakerLoad, "Spkr Load", Mode::Toggle},
                    {kPowerSupply, kTubePowerSag, "Power Sag", Mode::Drag},
                    {kInputJack, kTubeCapture, "Capture", Mode::Toggle},
                    {kSpeakerJack, kTubeSpeakerLoad, "Spkr Load", Mode::Toggle}};
      for (const auto& s : single)
        field(s.item, row(0), s.param, s.label, s.mode);
      for (int k = 0; k < 4; k++)
      {
        field(kPowerTube1 + k, row(0).GetFromLeft(row(0).W() * 0.5f - 3.f), kTube6L6, "6L6", Mode::Toggle);
        field(kPowerTube1 + k, row(0).GetFromRight(row(0).W() * 0.5f - 3.f), kTubeQuad, "4 Tubes", Mode::Toggle)
          ->SetTooltip("Four power tubes (a quad, like a 100 W head) instead of two: two per side in parallel, a "
                       "stiffer supply and a bigger output transformer. Same volume; the supply and transformer stay "
                       "out of the way longer, and a failed tube only halves its side.");
        field(kPowerTube1 + k, row(1), kTubeBiasPot, "Bias", Mode::Drag);
      }
      field(kFrontPanel, row(0).GetFromLeft(row(0).W() * 0.5f - 3.f), kTubeDrive, "Drive", Mode::Drag);
      field(kFrontPanel, row(0).GetFromRight(row(0).W() * 0.5f - 3.f), kTubeMaster, "Master", Mode::Drag);
      for (int k = 0; k < tube::kNumSockets; k++)
      {
        const int item = kSocket5 + k;
        field(item, row(0), SocketParam(k, kSockRole), "Job", Mode::Segments)
          ->SetTooltip("What the socket does: empty, a gain stage, a cathode follower on the stage before it, or a "
                       "boost (an unbypassed gain stage).");
        field(item, row(1), SocketParam(k, kSockTube), "Tube", Mode::Segments)
          ->SetTooltip("The tube in it. Changing it sets Bias to where that tube usually runs.");
        field(item, row(2).GetFromLeft(row(2).W() * 0.5f - 3.f), SocketParam(k, kSockGain), "Gain", Mode::Drag)
          ->SetTooltip("Its pot: how much of the stage before it drives this one (audio taper).");
        field(item, row(2).GetFromRight(row(2).W() * 0.5f - 3.f), SocketParam(k, kSockBias), "Bias", Mode::Drag)
          ->SetTooltip("Where it idles (grid to cathode). Toward 0 V: more gain, clips on top sooner. Colder: cuts "
                       "off sooner, sharper and more lopsided.");
        field(item, row(3).GetFromLeft(row(3).W() * 0.5f - 3.f), SocketParam(k, kSockCap), "Cap", Mode::Stepper)
          ->SetTooltip("Its cathode bypass cap (Gain only): small for tight lows, big for full.");
        field(item, row(3).GetFromRight(row(3).W() * 0.5f - 3.f), SocketParam(k, kSockPlateCap), "Plate C",
              Mode::Stepper)
          ->SetTooltip("Across its plate resistor: rolls off fizz. 470 pF is the classic on a late stage.");
        field(item, row(4).GetFromLeft(row(4).W() * 0.5f - 3.f), SocketParam(k, kSockCouple), "Couple", Mode::Stepper)
          ->SetTooltip("The coupling cap into it: small keeps a lot of gain tight, big passes the full low end.");
      }
      // The selected item's links to its pages, then failing and the power tubes.
      const IRECT linksRow = row(5).GetTranslated(0.f, 2.f);
      page->AddChildControl(new NAMChassisLinksControl(
        linksRow, [this]() { return mChassisSelected; }, [this](int link) { _ChassisGo(link); }));
      const IRECT failRow = linksRow.GetTranslated(0.f, rowH + 2.f);
      page->AddChildControl(new NAMLabFieldControl(failRow.GetFromLeft(failRow.W() * 0.6f), kTubeCanFail,
                                                   "Power tubes can fail", Mode::Toggle, IColor(255, 235, 90, 60)))
        ->SetTooltip("On: power tubes run past their rated plate dissipation (red-plating -- the Variac up and the "
                     "bias pot hot) wear out, faster the further over, and fail with a bang: the amp runs on the other "
                     "tube alone, thin and fizzy, or not at all with both gone. REPLACE puts a new one in.");
      page->AddChildControl(new NAMPowerTubeStatusControl(
        IRECT(side.L, failRow.B + 6.f, side.R, side.B),
        [this]() {
          NAMPowerTubeStatusControl::Reading r;
          const tube::TubeParams tp = _ReadTubeParams(mUiLab);
          r.powerAmp = tp.circuit && tp.powerAmp;
          r.sixL6 = tp.sixL6;
          r.canFail = tp.canFail;
          r.quad = tp.quad;
          for (int k = 0; k < 4; k++)
          {
            r.life[k] = mUiPowerLife[k].load(std::memory_order_relaxed);
            r.dead[k] = mUiPowerDead[k].load(std::memory_order_relaxed);
            r.dissipation[k] = mUiDissRatio[k].load(std::memory_order_relaxed);
          }
          return r;
        },
        [this](int k) { (mUiLab == 0 ? mReplacePowerReq : mReplacePowerReq2).fetch_or(1 << k); }));
      page->Hide(true);
    }

    // The SPEAKER LAB page (1.59.0, SpeakerLabControls.h).
    {
      mLabModel.params = [this]() { return _ReadCabLabParams(); };
      mLabModel.setMic = [this](int m, double x, double y, double z) { _SetLabMicPosition(m, x, y, z); };
      mLabModel.select = [this](int m) {
        mLabSelectedMic = std::clamp(m, 0, cab::kNumLabMics - 1);
        const cab::CabLabParams cp = _ReadCabLabParams();
        if (cp.dual)
          mLabSelectedCab = cp.MicCab(mLabSelectedMic); // The views follow the mic to its cab
        _SpeakerLabChanged();
      };
      mLabModel.selected = &mLabSelectedMic;
      mLabModel.selectedCab = &mLabSelectedCab;
      mLabModel.selectCab = [this](int c) {
        mLabSelectedCab = std::clamp(c, 0, 1);
        _SpeakerLabChanged();
      };
      mLabModel.yaw = &mLabYaw;
      mLabModel.pitch = &mLabPitch;
      mLabModel.redrawViews = [this]() { _SpeakerLabChanged(); };
      mLabFormatShown = GetParam(kCabFormat)->Int();
      mLabFormat2Shown = GetParam(kCab2Format)->Int();
      auto* page = new NAMOverlayPanelControl(b, "SPEAKER LAB", crossSVG);
      pGraphics->AttachControl(page, kCtrlTagSpeakerLab);
      // 1.67.1: back to CAB LAB's classic card (and its Mic A / Mic B knobs: 3D off).
      page->AddChildControl(new NAMFlatButtonControl(IRECT(b.R - 66.f - 120.f, b.T + 14.f, b.R - 66.f, b.T + 38.f),
                                                     "CAB LAB", [this]() { _ShowSpeakerLab(false); }))
        ->SetTooltip("CAB LAB's classic card: its Mic A / Mic B knobs (switches 3D off). Its SPEAKER LAB button "
                     "brings you back here.");
      using Mode = NAMLabFieldControl::Mode;
      const IRECT body = b.GetReducedFromTop(52.f).GetPadded(-14.f);
      // Top row: single or dual cabs, the cab tabs, 3D on/off, CAB LAB's output.
      const IRECT top = body.GetFromTop(22.f);
      page->AddChildControl(new NAMLabFieldControl(top.GetFromLeft(top.W() * 0.22f), kCabDual, "Cabs", Mode::Segments,
                                                   speakerlab::CabColor(0)))
        ->SetTooltip("One cab, or two side by side (each with its own size, speaker or IR). Each mic is on one of "
                     "them; the pans stay as they are.");
      page->AddChildControl(new NAMSpeakerLabCabTabsControl(
        IRECT(top.L + top.W() * 0.24f, top.T, top.L + top.W() * 0.62f, top.B), &mLabModel));
      page->AddChildControl(new NAMLabFieldControl(
                              IRECT(top.L + top.W() * 0.64f, top.T, top.L + top.W() * 0.74f, top.B), kCabLab3D, "3D",
                              Mode::Toggle, speakerlab::CabColor(0))) // 1.67.1: gold, so On shows
        ->SetTooltip("3D placement (this page). Off: CAB LAB's classic Mic A / Mic B knobs, as older presets were made.");
      page->AddChildControl(new NAMLabFieldControl(top.GetFromRight(top.W() * 0.24f), kCabOutput, "Output", Mode::Drag))
        ->SetTooltip("CAB LAB's output level. Drag sideways (Shift: fine), wheel to nudge, double-click to reset.");
      // Second row: the selected cab's size, speaker, source and IR (one set per cab; OnIdle shows the
      // selected cab's).
      const IRECT cabRow = top.GetTranslated(0.f, 26.f);
      for (auto& v : mLabCabControls)
        v.clear();
      for (int cab = 0; cab < 2; cab++)
      {
        const IColor accent = speakerlab::CabColor(cab);
        auto add = [&](IControl* c, const char* tip) {
          c->SetTooltip(tip);
          page->AddChildControl(c);
          mLabCabControls[cab].push_back(c);
        };
        add(new NAMLabFieldControl(cabRow.GetFromLeft(cabRow.W() * 0.3f), cab == 0 ? kCabFormat : kCab2Format, "Size",
                                   Mode::Segments, accent),
            "This cab's size: where its cones sit, how big, open or closed back. Changing it moves its mics to the "
            "same spot on the matching cone of the new cab.");
        add(new NAMLabFieldControl(IRECT(cabRow.L + cabRow.W() * 0.32f, cabRow.T, cabRow.L + cabRow.W() * 0.6f, cabRow.B),
                                   cab == 0 ? kCabSpeaker : kCab2Speaker, "Speaker", Mode::Stepper, accent),
            "This cab's speakers (with IR on, the IR is the speaker instead).");
        add(new NAMLabFieldControl(IRECT(cabRow.L + cabRow.W() * 0.62f, cabRow.T, cabRow.L + cabRow.W() * 0.68f, cabRow.B),
                                   cab == 0 ? kCabSource : kCab2SourceIR, "IR", Mode::Toggle, accent),
            "Use this cab's IR (loaded with the button beside it) as its speaker, instead of the model.");
        const IRECT irArea(cabRow.L + cabRow.W() * 0.7f, cabRow.T, cabRow.R, cabRow.B);
        const std::string griffinDir = GriffinLibraryDir();
        const std::vector<std::string> irPaths = {
          griffinDir + "tone", griffinDir + "IRs and Amp Profiles (copied)\\Griffin tone folder captures"};
        add(new NAMPresetBrowserControl(irArea.GetFromLeft(irArea.W() * 0.42f), cab == 0 ? "CAB 1 IR" : "CAB 2 IR",
                                        "wav", irPaths, false,
                                        [this, cab](const WDL_String& fileName, const WDL_String&) {
                                          if (!fileName.GetLength())
                                            return;
                                          if (cab == 0 ? _StageCabLabIR(fileName) : _StageCabLabIR2(fileName))
                                            SendParameterValueFromUI(cab == 0 ? kCabSource : kCab2SourceIR, 1.0);
                                        },
                                        style),
            "Load an IR for this cab (it switches IR on).");
        const WDL_String& path = cab == 0 ? mCabLabIRPath : mCabLabIR2Path;
        auto* name = new IVLabelControl(
          irArea.GetReducedFromLeft(irArea.W() * 0.44f), path.GetLength() ? path.get_filepart() : "No IR loaded",
          DEFAULT_STYLE.WithDrawFrame(false).WithValueText(IText(10.f, accent.WithOpacity(0.85f), "Roboto-Regular")));
        page->AddChildControl(name, cab == 0 ? kCtrlTagCabLabIR1Name : kCtrlTagCabLabIR2Name);
        mLabCabControls[cab].push_back(name);
      }
      // The views.
      const IRECT rest = body.GetReducedFromTop(54.f);
      const IRECT views = rest.GetFromTop(rest.H() * 0.52f);
      const float gap = 6.f;
      const float frontW = views.W() * 0.38f, sideW = views.W() * 0.31f;
      const IRECT frontR(views.L, views.T, views.L + frontW, views.B);
      const IRECT sideR(frontR.R + gap, views.T, frontR.R + gap + sideW, views.B);
      const IRECT perspR(sideR.R + gap, views.T, views.R, views.B);
      using View = NAMSpeakerLabViewControl::View;
      page->AddChildControl(new NAMSpeakerLabViewControl(frontR, View::Front, &mLabModel));
      page->AddChildControl(new NAMSpeakerLabViewControl(sideR, View::Side, &mLabModel));
      page->AddChildControl(new NAMSpeakerLabViewControl(perspR, View::Persp, &mLabModel));
      // Bottom: the response plot; the mic tabs, the selected mic's fields, its readout.
      const IRECT bottom = rest.GetReducedFromTop(views.H() + gap);
      page->AddChildControl(new NAMSpeakerLabResponseControl(bottom.GetFromLeft(bottom.W() * 0.4f), &mLabModel));
      const IRECT right = bottom.GetReducedFromLeft(bottom.W() * 0.4f + gap);
      page->AddChildControl(new NAMSpeakerLabTabsControl(right.GetFromTop(24.f), &mLabModel));
      const IRECT row1 = right.GetReducedFromTop(28.f).GetFromTop(22.f);
      const IRECT row2 = row1.GetTranslated(0.f, 26.f);
      for (auto& v : mLabMicControls)
        v.clear(); // (The last layout's controls are gone)
      for (int m = 0; m < cab::kNumLabMics; m++)
      {
        const IColor accent = speakerlab::MicColor(m);
        auto add = [&](const IRECT& r, int field, const char* label, Mode mode, const char* tip) {
          auto* c = new NAMLabFieldControl(r.GetPadded(-2.f), LabMicParam(m, field), label, mode, accent);
          c->SetTooltip(tip);
          page->AddChildControl(c);
          mLabMicControls[m].push_back(c);
        };
        add(row1.SubRectHorizontal(8, 0), 0, "On", Mode::Toggle, "This mic on or off.");
        add(IRECT(row1.SubRectHorizontal(8, 1).L, row1.T, row1.SubRectHorizontal(8, 3).R, row1.B), 1, "Mic",
            Mode::Stepper, "The mic: its response, polar pattern and proximity effect.");
        {
          // 1.61.0: which cab it's on (dual cabs).
          auto* cabField = new NAMLabFieldControl(row1.SubRectHorizontal(8, 4).GetPadded(-2.f), kLabMicCabFirst + m, "",
                                                  Mode::Stepper, accent);
          cabField->SetTooltip("Which cab this mic is on (with two cabs). It keeps its spot on the matching cone.");
          page->AddChildControl(cabField);
          mLabMicControls[m].push_back(cabField);
        }
        add(IRECT(row1.SubRectHorizontal(8, 5).L, row1.T, row1.SubRectHorizontal(8, 6).R, row1.B), 5, "Level",
            Mode::Drag, "This mic's level. Drag (Shift: fine), wheel, double-click to reset.");
        add(row1.SubRectHorizontal(8, 7), 7, "Flip", Mode::Toggle,
            "Polarity flip: try it when two mics thin each other out.");
        const char* tips[3] = {"Across the baffle: centimetres right of the cab's centre (left is negative).",
                               "Up and down: centimetres above the cab's centre.",
                               "Out from the grille cloth, in centimetres (0 is touching it)."};
        add(row2.SubRectHorizontal(4, 0), 2, "X", Mode::Drag, tips[0]);
        add(row2.SubRectHorizontal(4, 1), 3, "Y", Mode::Drag, tips[1]);
        add(row2.SubRectHorizontal(4, 2), 4, "Z", Mode::Drag, tips[2]);
        add(row2.SubRectHorizontal(4, 3), 6, "Pan", Mode::Drag, "Where this mic sits left to right.");
      }
      page->AddChildControl(new NAMSpeakerLabReadoutControl(right.GetReducedFromTop(28.f + 52.f + 4.f), &mLabModel));
      page->Hide(true);
    }

    const auto slimKnobArea = b.GetCentredInside(100.f, NAM_KNOB_HEIGHT + 24.f);
    pGraphics->AttachControl(new NAMSlimOverlayBackdropControl(b, hideSlimOverlay), kCtrlTagSlimOverlayBackdrop)
      ->Hide(true);
    pGraphics
      ->AttachControl(new NAMKnobControl(slimKnobArea, kSlim, "Slim", style, knobBackgroundBitmap), kCtrlTagSlimKnob)
      ->Hide(true);

    pGraphics->ForAllControlsFunc([](IControl* pControl) {
      pControl->SetMouseEventsWhenDisabled(true);
      pControl->SetMouseOverWhenDisabled(true);
    });

    // 1.67.0: every control bound to a Tube Lab param follows the lab being shown (but REAL FEEL's, which
    // is TUBE LAB's own, and the per-lab capture switches).
    mLabRetarget.clear();
    pGraphics->ForAllControlsFunc([this](IControl* ctrl) {
      const int p = ctrl->GetParamIdx();
      if (p < 0 || LabParam(1, p) == p)
        return;
      for (auto* own : mPedalDetailControls[chaingrid::kRealFeel])
        if (own == ctrl)
          return;
      for (auto* own : mLabOnly[0])
        if (own == ctrl)
          return;
      mLabRetarget.push_back({ctrl, p});
    });
    {
      const int lab = mUiLab; // A relayout keeps showing it
      mUiLab = 0;
      _SetUiLab(lab);
    }

    // 1.65.0: hover help on every knob and switch that doesn't already have its own tooltip (ParamHelp.h).
    pGraphics->ForAllControlsFunc([](IControl* ctrl) {
      const int p = ctrl->GetParamIdx();
      if (p < 0 || p >= kNumParams || (ctrl->GetTooltip() && *ctrl->GetTooltip()))
        return;
      const std::string help = ParamHelpFor(p);
      if (!help.empty())
        ctrl->SetTooltip(help.c_str());
    });

    // MIDI learn overlay (1.43.0): last, so it's over everything. Stays on across relayouts.
    {
      NAMMidiLearnOverlayControl::Callbacks cb;
      cb.targetOf = [](IControl* c) -> int {
        if (c->GetTag() == kCtrlTagTuneButton)
          return kMidiTargetTuner;
        if (c->GetTag() == kCtrlTagTapButton)
          return kMidiTargetTap;
        // A block on the grid (or in the pedal list): its On switch (1.43.2).
        if (auto* chip = dynamic_cast<NAMPedalChipControl*>(c))
        {
          const int id = chip->GetPedalIdx();
          if (chaingrid::IsPedal(id))
            return PedalOf(id).engagedParam;
          if (id == kAmpBlockId)
            return kAmpEngaged;
          if (id == kCabBlockId)
            return kIRToggle;
          if (id == -4 || id == -5) // LOOP OUT / LOOP IN
            return kLoopEngaged;
          return NAMMidiLearnOverlayControl::kNoTarget;
        }
        const int p = c->GetParamIdx();
        // The grid's routing params aren't knobs or switches (and never have a control anyway).
        if (p < 0 || p >= kNumParams || p == kChainGridV1 || (p >= kGridCell0 && p <= kGridOut))
          return NAMMidiLearnOverlayControl::kNoTarget;
        return p;
      };
      cb.ccOf = [this](int target) { return _MidiCCOf(target); };
      cb.selected = [this]() { return mMidiLearnTarget; };
      cb.select = [this](int target, bool add) {
        mMidiLearnTarget = target;
        mMidiLearnAdd = add;
        mMidiSeqSeen = mMidiSeq.load(std::memory_order_acquire); // Wait for a new message
      };
      cb.clear = [this](int target) { _MidiClearTarget(target); };
      cb.status = [this]() -> std::string {
        if (mMidiLearnTarget != kMidiNoTarget)
        {
          const int cc = _MidiCCOf(mMidiLearnTarget);
          return _MidiTargetName(mMidiLearnTarget)
                 + (mMidiLearnAdd ? " (adding)" : "")
                 + (cc >= 0 ? ": mapped to CC " + std::to_string(cc) + " (press another to change it)"
                            : ": press or move a control on your MIDI controller...");
        }
        const int cc = mMidiLastCC.load(std::memory_order_relaxed);
        if (cc < 0)
          return "No MIDI in yet. (Check the MIDI input in Preferences, and that the controller is connected.)";
        return "Last MIDI in: CC " + std::to_string(cc) + " = " + std::to_string(mMidiLastValue.load());
      };
      cb.mappings = [this]() {
        std::vector<std::string> lines;
        for (int cc = 0; cc < 128; cc++)
        {
          if (mMidiMap[cc].empty())
            continue;
          std::string line = "CC " + std::to_string(cc) + ": ";
          for (size_t k = 0; k < mMidiMap[cc].size(); k++)
          {
            // Short, for the list: a pedal's On switch is just the pedal's name.
            std::string name = _MidiTargetName(mMidiMap[cc][k]);
            if (name.size() > 3 && name.compare(name.size() - 3, 3, " On") == 0)
              name.resize(name.size() - 3);
            line += (k ? ", " : "") + name;
          }
          lines.push_back(line);
        }
        return lines;
      };
      pGraphics->AttachControl(new NAMMidiLearnOverlayControl(b, cb), kCtrlTagMidiLearn)->Hide(!mMidiLearnOn);
    }

    // pGraphics->GetControlWithTag(kCtrlTagOutNorm)->SetMouseEventsWhenDisabled(false);
    // pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetMouseEventsWhenDisabled(false);

    // Light mode (1.48.0): iPlug2's labels draw a dark drop shadow under their text, which reads as a
    // smudge on a light background.
    if (PluginColors::NAM_LIGHT)
      pGraphics->ForStandardControlsFunc([](IControl* pControl) {
        if (auto* label = dynamic_cast<IVLabelControl*>(pControl))
          label->SetColor(EVColor::kSH, COLOR_TRANSPARENT);
      });
  };

  // MIDI mappings (1.43.0): the whole app's, not a preset's.
  _LoadMidiMap();

#ifdef APP_API
  // Standalone only: a DAW build's state lives in the host's project file, but the bare .exe has
  // no host to do that for it. Load whatever was last autosaved (see _AutosaveIfDirty) so a fresh
  // launch picks up where the previous session left off, instead of always coming up at defaults.
  WDL_String autosavePath;
  _GetAutosavePath(autosavePath);
  _LoadStateFromFile(autosavePath.Get());
#endif
}

NeuralAmpModeler::~NeuralAmpModeler()
{
#ifdef APP_API
  // Belt-and-suspenders alongside the periodic autosave in OnIdle: catches whatever changed since
  // the last throttled write, on a clean close.
  WDL_String autosavePath;
  _GetAutosavePath(autosavePath);
  _SaveStateToFile(autosavePath.Get());
#endif
  _DeallocateIOPointers();
}

void NeuralAmpModeler::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  const size_t numChannelsInternal = kNumChannelsInternal;
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  _PrepareBuffers(numChannelsInternal, numFrames);
  _PrepareGridBuffers(numFrames);
  // The FX Loop Return's aux input, copied now: the host may share input and output buffers, and
  // the Loop Send writes the aux outputs mid-chain.
  if (numChannelsExternalIn > (size_t)kLoopBusChannelOffset)
    for (size_t c = 0; c < 2; c++)
    {
      const size_t from = std::min(kLoopBusChannelOffset + c, numChannelsExternalIn - 1);
      std::copy(inputs[from], inputs[from] + numFrames, mLoopReturnPtrs[c]);
    }
  // Only the main bus feeds the chain and gets its output; the aux bus is the FX loop's.
  const size_t numChannelsMainIn = std::min(numChannelsExternalIn, (size_t)kLoopBusChannelOffset);
  const size_t numChannelsMainOut = std::min(numChannelsExternalOut, (size_t)kLoopBusChannelOffset);
  // Input is collapsed to mono in preparation for the NAM.
  _ProcessInput(inputs, numFrames, numChannelsMainIn, numChannelsInternal);
  // Hum Filter (1.39.0, MainsHum.h): mains hum out of the input, before the gate and the chain.
  mHumFilter.SetOn(GetParam(kHumFilter)->Bool());
  mHumFilter.Process(mInputArray[0].data(), numFrames);
  _ApplyDSPStaging();

  // Tuner: always analyzes the raw, unprocessed input so it reflects the actual string pitch. Taken
  // before the Input knob and input calibration (1.29.2): calibration alone can be -15 dB or more,
  // which pushed a modest interface level under the tuner's gate.
  if (numChannelsMainIn > 0)
    mTuner.Process(inputs, (int)numFrames);

  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value();
  const bool toneStackActive = GetParam(kEQActive)->Value();

  // GATE's key (1.52.0): the clean input after Master In, copied for this block while Key Input is on
  // (a block may process the input in place before the GATE runs).
  mGateKeyChannels = 0;
  if ((GetParam(kGateKey)->Bool() && GetParam(GridCellParam(chaingrid::kGate))->Value() >= 0.0)
      || (GetParam(kTGKey)->Bool() && GetParam(GridCellParam(chaingrid::kTrackGate))->Value() >= 0.0)) // 1.66.0
  {
    const int keyChannels = (int)std::min<size_t>(numChannelsInternal, 2);
    for (int c = 0; c < keyChannels; c++)
    {
      if (mGateKeyBuf[c].size() < numFrames)
        mGateKeyBuf[c].resize(numFrames);
      std::copy(mInputPointers[c], mInputPointers[c] + numFrames, mGateKeyBuf[c].data());
      mGateKeyPtrs[c] = mGateKeyBuf[c].data();
    }
    mGateKeyChannels = keyChannels;
  }

  // Noise gate trigger
  sample** triggerOutput = mInputPointers;
  if (noiseGateActive)
  {
    const double time = 0.01;
    const double threshold = GetParam(kNoiseGateThreshold)->Value(); // GetParam...
    const double ratio = 0.1; // Quadratic...
    const double openTime = 0.005;
    const double holdTime = 0.01;
    const double closeTime = 0.05;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsInternal, numFrames);
  }

  // The signal chain is the grid (ChainGrid.h): blocks wired with cables, processed column by
  // column. Each block processes the average of everything cabled into it; Output is the average of
  // everything cabled into Output. A block nothing live reaches doesn't process at all.
  const chaingrid::Grid grid = _ReadChainGrid();
  const bool tubeInChain = grid.Placed(kTubePedalIdx);
  if (tubeInChain != mTubeWasInChain)
  {
    // Start from silence-state filters/bloom rather than whatever was left from last time.
    for (auto& stage : mTubeStage)
      stage.Reset(sampleRate, GetBlockSize());
    mTubeWasInChain = tubeInChain;
  }
  if (grid.Placed(chaingrid::kTubeLab2) != mTube2WasInChain) // 1.67.0
  {
    for (auto& stage : mTubeStage2)
      stage.Reset(sampleRate, GetBlockSize());
    mTube2WasInChain = grid.Placed(chaingrid::kTubeLab2);
  }
  // Whether each block on the grid actually processes, or passes its input straight through. A
  // pedal processes when it's engaged (its on/off switch -- MIDI/automation-mappable, doesn't move
  // it); a NAM pedal also needs a capture loaded.
  bool processes[chaingrid::kNumIds] = {};
  for (int p = 0; p < kNumBuiltInPedals; p++)
    processes[p] = GetParam(kPedals[p].engagedParam)->Value() > 0.5;
  for (int id = chaingrid::kFirstExtraPedal; id < chaingrid::kNumIds; id++)
    processes[id] = GetParam(PedalOf(id).engagedParam)->Value() > 0.5;
  // Tube Lab: its on/off switch is a crossfade inside the stage (see mTubeStage), not a bypass, so
  // the reported latency stays put.
  processes[kTubePedalIdx] = true;
  processes[chaingrid::kTubeLab2] = true; // 1.67.0: likewise
  // REAL FEEL follows the Tube Lab's On switch by fading itself (see RealFeel.h), not by bypassing:
  // a bypass would step its squish and Shared B+ gains.
  processes[chaingrid::kRealFeel] = true;
  // CAB 2 / CAB 3 pass the signal through until they have an IR.
  processes[chaingrid::kCab2] = processes[chaingrid::kCab2] && mExtraCabIR[0] != nullptr;
  processes[chaingrid::kCab3] = processes[chaingrid::kCab3] && mExtraCabIR[1] != nullptr;
  for (int i = 0; i < kNumNamPedals; i++)
    processes[kNumBuiltInPedals + i] =
      mNamPedalModel[i] != nullptr && GetParam(kPedals[kNumBuiltInPedals + i].engagedParam)->Value() > 0.5;
  processes[kCabPedalIdx] = mIR != nullptr && GetParam(kIRToggle)->Value();
  // Loop Send/Return only do anything when the host has the aux bus's channels connected (see
  // PLUG_CHANNEL_IO) -- never in the standalone APP build, which doesn't declare the bus. An
  // unconnected loop is a transparent pass-through rather than silence. Switched off (kLoopEngaged)
  // it's a pass-through too, and the aux output carries silence.
  const bool loopOn = GetParam(kLoopEngaged)->Value() > 0.5;
  processes[kLoopSendPedalIdx] = loopOn && numChannelsExternalOut > (size_t)kLoopBusChannelOffset;
  processes[kLoopReturnPedalIdx] = loopOn && numChannelsExternalIn > (size_t)kLoopBusChannelOffset;
  const float loopMixTarget = (float)(GetParam(kLoopMix)->Value() / 100.0);
  processes[chaingrid::kAmp] = GetParam(kAmpEngaged)->Value() > 0.5 && !_TubeTakesCapture();
  // 1.42.0: which blocks can process at all (have their IR / capture), so one switched off can keep
  // processing while it fades out; and which blocks' On switches crossfade (see mBlockFade). The
  // Tube Lab and REAL FEEL fade inside themselves, and the FX loop only exists with the aux bus.
  bool available[chaingrid::kNumIds];
  bool fades[chaingrid::kNumIds];
  for (int id = 0; id < chaingrid::kNumIds; id++)
  {
    available[id] = true;
    fades[id] = id != kTubePedalIdx && id != chaingrid::kTubeLab2 && id != chaingrid::kRealFeel && id != kLoopSendPedalIdx
                && id != kLoopReturnPedalIdx && id != chaingrid::kInput;
  }
  available[chaingrid::kCab2] = mExtraCabIR[0] != nullptr;
  available[chaingrid::kCab3] = mExtraCabIR[1] != nullptr;
  for (int i = 0; i < kNumNamPedals; i++)
    available[kNumBuiltInPedals + i] = mNamPedalModel[i] != nullptr;
  available[kCabPedalIdx] = mIR != nullptr;
  if (!mBlockFadeInit)
  {
    for (int id = 0; id < chaingrid::kNumIds; id++)
    {
      mBlockFade[id] = processes[id] ? 1.0 : 0.0;
      mBlockTail[id] = false;
    }
    mBlockFadeInit = true;
  }
  const double blockFadeStep = 1.0 / (kBlockFadeS * sampleRate);
  // The blocks with a memory worth hearing out when they're switched off (see mBlockTail).
  auto hasTail = [](int id) {
    const int type = chaingrid::IsPedal(id) ? chaingrid::PedalType(id) : -1;
    return type == 2 || type == 3 || type == 4 || id == chaingrid::kFlanger || id == chaingrid::kPhaser
           || id == kCabPedalIdx || id == kCabLabPedalIdx || id == chaingrid::kCab2 || id == chaingrid::kCab3
           || id == chaingrid::kSkyVerb || id == chaingrid::kFlangerDoubler || id == chaingrid::kDigiDelay
           || id == chaingrid::kAnalogDelay || id == chaingrid::kTapeEcho || id == chaingrid::kSpringVerb;
  };
  // A/B switch in progress: the gain for the AMP and CAB outputs this block (see _ABBeginBlock).
  const bool abFading = _ABBeginBlock(numFrames);

  // The effect pedals (the types that come in several copies): each copy has its own DSP object and
  // knobs; the knobs are read by position in its PedalDef.
  auto processEffect = [this, numFrames](int id, sample** in, size_t numChannels) -> sample** {
    const PedalDef& def = PedalOf(id);
    auto knob = [&](int k) { return GetParam(def.knobs[k].paramIdx)->Value(); };
    const int copy = chaingrid::PedalCopy(id);
    const int nc = (int)numChannels, nf = (int)numFrames;
    switch (chaingrid::PedalType(id))
    {
      case 0:
      {
        auto& o = copy ? mCompressorCopy[copy - 1] : mCompressor;
        o.SetParams(knob(0) / 10.0, knob(1), knob(2) / 10.0);
        return o.Process(in, nc, nf);
      }
      case 1:
      {
        auto& o = copy ? mOverdriveCopy[copy - 1] : mOverdrive;
        o.SetParams(knob(0) / 10.0, knob(1) / 10.0, knob(2), knob(3) / 10.0);
        return o.Process(in, nc, nf);
      }
      case 2:
      {
        auto& o = copy ? mChorusCopy[copy - 1] : mChorus;
        o.SetParams(knob(0), knob(1) / 10.0, knob(2) / 10.0);
        return o.Process(in, nc, nf);
      }
      case 3:
      {
        auto& o = copy ? mDelayCopy[copy - 1] : mDelay;
        o.SetParams(knob(0), knob(1) / 10.0, knob(2) / 10.0);
        return o.Process(in, nc, nf);
      }
      case 4:
      {
        auto& o = copy ? mReverbCopy[copy - 1] : mReverb;
        o.SetParams(knob(0) / 10.0, knob(1) / 10.0);
        return o.Process(in, nc, nf);
      }
      case 5:
      {
        auto& o = copy ? mEqPedalCopy[copy - 1] : mEqPedal;
        o.SetParams(knob(0), knob(1), knob(2), knob(3));
        return o.Process(in, nc, nf);
      }
      case 6:
      {
        auto& o = copy ? mTremoloCopy[copy - 1] : mTremolo;
        o.SetParams(knob(0), knob(1) / 10.0, knob(2) / 10.0);
        return o.Process(in, nc, nf);
      }
      default: return in;
    }
  };

  auto applyPedal = [this, numFrames, inputs, outputs, processEffect, loopMixTarget, noiseGateActive,
                     toneStackActive](int pedalIdx, sample** in,
                                                                                     size_t numChannels) -> sample** {
    if (chaingrid::IsPedal(pedalIdx) && chaingrid::PedalType(pedalIdx) < chaingrid::kNumMultiPedalTypes)
      return processEffect(pedalIdx, in, numChannels);
    switch (pedalIdx)
    {
      case chaingrid::kFlanger:
        mFlanger.SetParams(GetParam(kFlangerRate)->Value(), GetParam(kFlangerDepth)->Value() / 10.0,
                           GetParam(kFlangerRegen)->Value() / 10.0, GetParam(kFlangerMix)->Value() / 10.0);
        return mFlanger.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kPhaser:
        mPhaser.SetParams(GetParam(kPhaserRate)->Value(), GetParam(kPhaserDepth)->Value() / 10.0,
                          GetParam(kPhaserRegen)->Value() / 10.0, GetParam(kPhaserMix)->Value() / 10.0);
        return mPhaser.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kSkyVerb:
        mSkyVerb.SetParams(GetParam(kSkyMode)->Int(), GetParam(kSkyDecay)->Value() / 10.0, GetParam(kSkyPredelay)->Value(),
                           GetParam(kSkyTone)->Value() / 10.0, GetParam(kSkyMod)->Value() / 10.0,
                           GetParam(kSkyMix)->Value() / 10.0, GetParam(kSkyFreeze)->Bool());
        return mSkyVerb.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kFlangerDoubler:
        mFlangerDoubler.SetParams(GetParam(kFDDoubler)->Bool(), GetParam(kFDManual)->Value() / 10.0,
                                  GetParam(kFDWidth)->Value() / 10.0, GetParam(kFDSpeed)->Value(),
                                  GetParam(kFDRegen)->Value() / 10.0, GetParam(kFDMix)->Value() / 10.0);
        return mFlangerDoubler.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kWah:
        mWah.SetParams(GetParam(kWahModel)->Int(), GetParam(kWahPosition)->Value() / 10.0, GetParam(kWahQ)->Value() / 10.0,
                       GetParam(kWahSens)->Value() / 10.0, GetParam(kWahLevel)->Value());
        return mWah.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kPrecisionDrive:
        mPrecisionDrive.SetParams(GetParam(kPDVolume)->Value(), GetParam(kPDDrive)->Value() / 10.0,
                                  GetParam(kPDAttack)->Value() / 10.0, GetParam(kPDGate)->Value() / 10.0,
                                  GetParam(kPDBright)->Bool());
        return mPrecisionDrive.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kProEq:
      {
        // PRO EQ (1.46.0): in place, with the spectrum of its input and output for its popup.
        mProEq.SetParams(_ReadEqParams(kProEqBandFirst, kProEqBandExtFirst, kProEqTrim, kProEqOutput, true));
        mProEqTapIn.Push(in, (int)numChannels, (int)numFrames);
        if (mProEq.Active())
          mProEq.Process(in, (int)numChannels, (int)numFrames);
        mProEqTapOut.Push(in, (int)numChannels, (int)numFrames);
        return in;
      }
      case chaingrid::kGate:
      {
        // GATE (1.52.0): with Key Input on it listens to the clean input copied at the top of the block.
        dynamics::GateParams gp;
        gp.thresholdDB = GetParam(kGateThreshold)->Value();
        gp.hysteresisDB = GetParam(kGateHysteresis)->Value();
        gp.attackMs = GetParam(kGateAttack)->Value();
        gp.holdMs = GetParam(kGateHold)->Value();
        gp.releaseMs = GetParam(kGateRelease)->Value();
        gp.rangeDB = GetParam(kGateRange)->Value();
        gp.keyInput = GetParam(kGateKey)->Bool();
        gp.keyHpfHz = GetParam(kGateKeyHpf)->Value();
        mGate.SetParams(gp);
        return mGate.Process(in, (int)numChannels, (int)numFrames, mGateKeyChannels > 0 ? mGateKeyPtrs : nullptr,
                             mGateKeyChannels);
      }
      case chaingrid::kStudioComp:
      {
        dynamics::CompParams cp;
        cp.thresholdDB = GetParam(kSCompThreshold)->Value();
        cp.ratio = GetParam(kSCompRatio)->Value();
        cp.attackMs = GetParam(kSCompAttack)->Value();
        cp.releaseMs = GetParam(kSCompRelease)->Value();
        cp.kneeDB = GetParam(kSCompKnee)->Value();
        cp.makeupDB = GetParam(kSCompMakeup)->Value();
        cp.autoMakeup = GetParam(kSCompAutoMakeup)->Bool();
        cp.mix = GetParam(kSCompMix)->Value() / 100.0;
        cp.style = GetParam(kSCompStyle)->Int();
        cp.scHpfHz = GetParam(kSCompScHpf)->Value();
        mStudioComp.SetParams(cp);
        return mStudioComp.Process(in, (int)numChannels, (int)numFrames);
      }
      // 1.66.0 (MorePedals.h).
      case chaingrid::kTrackGate:
        mTrackGate.SetParams(GetParam(kTGModel)->Int(), GetParam(kTGThreshold)->Value(),
                             GetParam(kTGRelease)->Value() / 10.0, GetParam(kTGHold)->Value(), GetParam(kTGRange)->Value(),
                             GetParam(kTGRatio)->Value(), GetParam(kTGNoise)->Int());
        return mTrackGate.Process(in, (int)numChannels, (int)numFrames,
                                  GetParam(kTGKey)->Bool() && mGateKeyChannels > 0 ? mGateKeyPtrs : nullptr,
                                  GetParam(kTGKey)->Bool() ? mGateKeyChannels : 0);
      case chaingrid::kDigiDelay:
        mDigiDelay.SetParams(GetParam(kDigiMode)->Int(), GetParam(kDigiTime)->Value(),
                             GetParam(kDigiFeedback)->Value() / 10.0, GetParam(kDigiLevel)->Value() / 10.0);
        return mDigiDelay.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kAnalogDelay:
        mAnalogDelay.SetParams(GetParam(kAnaTime)->Value(), GetParam(kAnaRegen)->Value() / 10.0,
                               GetParam(kAnaMix)->Value() / 10.0, GetParam(kAnaMod)->Bool(), GetParam(kAnaRate)->Value(),
                               GetParam(kAnaWidth)->Value() / 10.0);
        return mAnalogDelay.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kTapeEcho:
        mTapeEcho.SetParams(GetParam(kTapeHeads)->Int(), GetParam(kTapeRate)->Value() / 10.0,
                            GetParam(kTapeIntensity)->Value() / 10.0, GetParam(kTapeEcho)->Value() / 10.0,
                            GetParam(kTapeReverb)->Value() / 10.0, GetParam(kTapeWow)->Value() / 10.0,
                            GetParam(kTapeDrive)->Value() / 10.0);
        return mTapeEcho.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kSpringVerb:
        mSpringVerb.SetParams(GetParam(kSprDwell)->Value() / 10.0, GetParam(kSprTone)->Value() / 10.0,
                              GetParam(kSprMix)->Value() / 10.0, GetParam(kSprDecay)->Value() / 10.0);
        return mSpringVerb.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kApexPreamp:
        mApexPreamp.SetParams(GetParam(kApexGain)->Value() / 10.0, GetParam(kApexBass)->Value() / 10.0,
                              GetParam(kApexMid)->Value() / 10.0, GetParam(kApexTreble)->Value() / 10.0,
                              GetParam(kApexPresence)->Value() / 10.0, GetParam(kApexVolume)->Value(),
                              GetParam(kApexBright)->Bool());
        return mApexPreamp.Process(in, (int)numChannels, (int)numFrames);
      case chaingrid::kCab2:
      case chaingrid::kCab3:
      {
        // An extra cab: the signal into its speaker is mono (the channels' average), convolved with
        // its IR, then placed in stereo by Level and Pan (balance: centred is unity on both sides),
        // ramped across the block so turning them doesn't click.
        const int k = ExtraCabIndex(pedalIdx);
        const size_t n = numFrames;
        sample* mono = mExtraCabMono[k].data();
        for (size_t s = 0; s < n; s++)
          mono[s] = numChannels > 1 ? 0.5 * (in[0][s] + in[1][s]) : in[0][s];
        sample* monoIn[1] = {mono};
        const sample* cab = mExtraCabIR[k]->Process(monoIn, 1, n)[0];
        const int p0 = k == 0 ? kCab2Active : kCab3Active;
        const double level = DBToAmp(GetParam(p0 + 2)->Value());
        const double pan = std::clamp(GetParam(p0 + 3)->Value() / 100.0, -1.0, 1.0);
        const double to[2] = {level * std::min(1.0, 1.0 - pan), level * std::min(1.0, 1.0 + pan)};
        // 1.42.0: a 20 ms glide (it was a ramp across one block, a few ms: a jump still clicked).
        const double glide = 1.0 - std::exp(-1.0 / (0.02 * GetSampleRate()));
        for (int c = 0; c < 2; c++)
        {
          double g = mExtraCabGain[k][c];
          sample* out = mExtraCabPtrs[k][c];
          for (size_t s = 0; s < n; s++)
          {
            g += glide * (to[c] - g);
            if (std::fabs(to[c] - g) <= 1e-9 * std::max(1.0, to[c]))
              g = to[c];
            out[s] = cab[s] * g;
          }
          mExtraCabGain[k][c] = g;
        }
        return mExtraCabPtrs[k];
      }
      case chaingrid::kRealFeel:
      {
        // The Tube Lab's Real Feel switches and Depth, on with the Tube Lab (a tandem pedal).
        realfeel::Params rp;
        rp.engaged = GetParam(kTubeEngaged)->Value() > 0.5;
        rp.squish = GetParam(kTubeScreenSquish)->Bool();
        rp.sharedSupply = GetParam(kTubeSharedSupply)->Bool();
        rp.otIron = GetParam(kTubeOTSaturation)->Bool();
        rp.coilHeat = GetParam(kTubeCoilHeat)->Bool();
        rp.depth = GetParam(kTubeFeelDepth)->Value();
        rp.mix = GetParam(kRealFeelMix)->Value() / 100.0;
        mRealFeel.SetParams(rp);
        mRealFeel.Process(in, mRealFeelPtrs, (int)numChannels, (int)numFrames);
        return mRealFeelPtrs;
      }
      case kTubePedalIdx:
      case chaingrid::kTubeLab2:
      {
        // 1.67.0: TUBE LAB 2 is the same, with its own params (LabParam), stages and capture. The
        // probe and the chassis page's readings come from the lab whose popup was opened last.
        const int lab = pedalIdx == kTubePedalIdx ? 0 : 1;
        tube::TubeStage* stages = lab == 0 ? mTubeStage : mTubeStage2;
        sample** labChannels = lab == 0 ? mTubeChannels : mTubeChannels2;
        const bool shown = mUiLabAudio.load(std::memory_order_relaxed) == lab;
        stages[0].SetProbe(shown ? &mTubeProbe : nullptr);
        tube::TubeParams params = _ReadTubeParams(lab);
        if (GetParam(LabParam(lab, kTubeEngaged))->Value() <= 0.5)
        {
          // Switched off: the stage glides (its params are smoothed) to the delayed dry signal.
          params.mix = 0.0;
          params.outputDB = 0.0;
        }
        // 1.62.0: power tube replacements asked for on the chassis page; channel 0 wears the tubes and
        // channel 1 follows it.
        if (const int replace = (lab == 0 ? mReplacePowerReq : mReplacePowerReq2).exchange(0))
          for (int k = 0; k < tube::TubeStage::kNumPowerTubes; k++)
            if (replace & (1 << k))
              for (int c = 0; c < 2; c++)
                stages[c].ReplacePowerTube(k);
        const bool chassis = shown && mChassisOpen.load(std::memory_order_relaxed);
        auto report = [&]() {
          if (!shown)
            return;
          for (int k = 0; k < tube::TubeStage::kNumPowerTubes; k++)
          {
            mUiPowerLife[k].store((float)stages[0].PowerLife(k), std::memory_order_relaxed);
            mUiPowerDead[k].store(stages[0].PowerDead(k), std::memory_order_relaxed);
            mUiDissRatio[k].store((float)stages[0].DissipationRatio(k), std::memory_order_relaxed);
          }
        };
        if (params.capture)
        {
          // 1.64.0: the AMP block's capture is the preamp. It's mono (as in the AMP block): the
          // channels' average, through Drive (12 dB = unity, ramped across the block), into the
          // capture, and its output into the Tube Lab's power amp. One stage; both channels get it.
          if (mCapIn.size() < numFrames)
          {
            mCapIn.resize(numFrames);
            mCapOut.resize(numFrames);
          }
          double& capGain = lab == 0 ? mCapGain : mCapGain2;
          const double gainTo = std::pow(10.0, (GetParam(LabParam(lab, kTubeDrive))->Value() - 12.0) / 20.0);
          for (size_t s = 0; s < numFrames; s++)
          {
            const double g = capGain + (gainTo - capGain) * (double)(s + 1) / (double)numFrames;
            mCapIn[s] = (numChannels > 1 ? 0.5 * (in[0][s] + in[1][s]) : in[0][s]) * g;
          }
          capGain = gainTo;
          mCapInPtr[0] = mCapIn.data();
          mCapOutPtr[0] = mCapOut.data();
          // TUBE LAB runs the AMP block's capture; TUBE LAB 2 its own.
          ResamplingNAM* capture = lab == 0 ? mModel.get() : mLab2Model.get();
          if (capture != nullptr)
            capture->process(mCapInPtr, mCapOutPtr, (int)numFrames);
          else
            std::copy(mCapIn.begin(), mCapIn.begin() + numFrames, mCapOut.begin());
          // All of the AMP block, as it would be: its noise gate and its Bass / Mid / Treble after the
          // capture (the gate's trigger already read the chain's input this block). TUBE LAB 2's capture
          // isn't the AMP block's, so it has neither.
          sample** ampOut = mCapOutPtr;
          if (lab == 0 && noiseGateActive)
            ampOut = mNoiseGateGain.Process(ampOut, 1, numFrames);
          if (lab == 0 && toneStackActive && mToneStack != nullptr)
            ampOut = mToneStack->Process(ampOut, 1, (int)numFrames);
          stages[0].SetParams(params);
          stages[0].SetWearMaster(true);
          stages[0].SetUiWanted(chassis);
          labChannels[0] = stages[0].Process(ampOut, 1, numFrames)[0];
          labChannels[1] = labChannels[0];
          report();
          return labChannels;
        }
        for (size_t c = 0; c < numChannels && c < 2; c++)
        {
          sample* channel[1] = {in[c]};
          stages[c].SetParams(params);
          stages[c].SetWearMaster(c == 0);
          stages[c].SetUiWanted(c == 0 && chassis);
          if (c == 1)
            stages[1].SyncTubesFrom(stages[0]);
          labChannels[c] = stages[c].Process(channel, 1, numFrames)[0];
        }
        report();
        if (chassis)
          for (int i = 0; i < tube::TubeStage::kNumStageLevels; i++)
            mUiStageLevel[i].store((float)stages[0].StageLevel(i), std::memory_order_relaxed);
        return labChannels;
      }
      case kCabLabPedalIdx:
      {
        // CAB LAB: mono into the speaker (the pre-amp side is mono; on the loop side, the two
        // channels' average), stereo out of the mics and room.
        const size_t n = numFrames;
        if (mCabMono.size() < n)
        {
          mCabMono.resize(n);
          mCabL.resize(n);
          mCabR.resize(n);
        }
        for (size_t s = 0; s < n; s++)
          mCabMono[s] = numChannels > 1 ? 0.5 * (in[0][s] + in[1][s]) : in[0][s];

        const cab::CabLabParams cp = _ReadCabLabParams();
        mCabLab.SetParams(cp);

        const double* irSpeaker = nullptr;
        if (cp.useIR && mCabLabIR != nullptr)
        {
          sample* mono[1] = {mCabMono.data()};
          irSpeaker = mCabLabIR->Process(mono, 1, n)[0];
        }
        // 1.61.0: cab 2's own IR (dual cabs, 3D).
        const double* irSpeaker2 = nullptr;
        if (cp.lab3D && cp.dual && cp.useIR2 && mCabLabIR2 != nullptr)
        {
          sample* mono[1] = {mCabMono.data()};
          irSpeaker2 = mCabLabIR2->Process(mono, 1, n)[0];
        }
        mCabLab.Process(mCabMono.data(), mCabL.data(), mCabR.data(), n, irSpeaker, irSpeaker2);
        // Always stereo out (1.42.0): a mono block after it folds it back itself (the grid mixes
        // a stereo source down for a mono input), but a stereo path keeps the mics' pan and the room.
        mCabChannels[0] = mCabL.data();
        mCabChannels[1] = mCabR.data();
        return mCabChannels;
      }
      case kNumBuiltInPedals + 0:
      case kNumBuiltInPedals + 1:
      {
        // NAM-file pedals: mono-only, so the grid always hands them a mono mix (see monoOnly below).
        const int i = pedalIdx - kNumBuiltInPedals;
        if (mNamPedalModel[i] == nullptr)
          return in;
        if (mNamPedalBuffer[i].size() != numFrames)
          mNamPedalBuffer[i].resize(numFrames);
        mNamPedalBufferPtr[i][0] = mNamPedalBuffer[i].data();
        mNamPedalModel[i]->process(in, mNamPedalBufferPtr[i], (int)numFrames);
        const double levelGain = DBToAmp(GetParam(i == 0 ? kNamPedal0Level : kNamPedal1Level)->Value());
        // Mix (0-10, default 10 = fully wet) blends the dry input with the captured pedal's output --
        // same convention as every other pedal's Mix knob. A capture freezes its own Drive/Tone/etc.
        // (that's inherent to how NAM works), so Mix is the one useful "how strong" knob left to give it.
        const double mix = GetParam(i == 0 ? kNamPedal0Mix : kNamPedal1Mix)->Value() / 10.0;
        // 1.42.0: Level and Mix glide over ~20 ms instead of jumping.
        if (mNamPedalLevelSm[i] < 0.0)
        {
          mNamPedalLevelSm[i] = levelGain;
          mNamPedalMixSm[i] = mix;
        }
        const double glide = 1.0 - std::exp(-1.0 / (0.02 * GetSampleRate()));
        double lv = mNamPedalLevelSm[i], mx = mNamPedalMixSm[i];
        for (size_t s = 0; s < numFrames; s++)
        {
          lv = std::fabs(levelGain - lv) <= 1e-9 * levelGain ? levelGain : lv + glide * (levelGain - lv);
          mx = std::fabs(mix - mx) <= 1e-9 ? mix : mx + glide * (mix - mx);
          const double wet = mNamPedalBufferPtr[i][0][s] * lv;
          mNamPedalBufferPtr[i][0][s] = static_cast<sample>(in[0][s] * (1.0 - mx) + wet * mx);
        }
        mNamPedalLevelSm[i] = lv;
        mNamPedalMixSm[i] = mx;
        return mNamPedalBufferPtr[i];
      }
      case kCabPedalIdx:
      {
        // Cab IR: processed one channel at a time through independent ImpulseResponse instances --
        // ImpulseResponse's convolution history is a single stateful ring buffer, so the same
        // instance can't be reused for two different channels' signals within one block. mIR_R is
        // always kept either auto-mirrored from mIR or the user's own explicit choice (see
        // _ApplyDSPStaging), so both channels get correct, independently-stateful processing even
        // in the default (mono cab) case. CAB can end up called here at numChannels==1 too, if
        // it's been dragged to a slot ahead of wherever AMP currently sits -- unusual, but the IR
        // convolution itself doesn't care about channel count, so it's handled the same either way.
        if (mStereoBufferL.size() != numFrames)
          mStereoBufferL.resize(numFrames);
        if (mStereoBufferR.size() != numFrames)
          mStereoBufferR.resize(numFrames);
        // `in` may or may not already BE mStereoBufferL/R depending on what ran before CAB -- copy
        // in first so the convolution isn't reading and writing the same memory out from under itself.
        if (in[0] != mStereoBufferL.data())
        {
          for (size_t s = 0; s < numFrames; s++)
          {
            mStereoBufferL[s] = in[0][s];
            mStereoBufferR[s] = numChannels > 1 ? in[1][s] : in[0][s];
          }
        }
        sample* lArr[1] = {mStereoBufferL.data()};
        sample** lOut = mIR->Process(lArr, 1, (int)numFrames);
        sample* rArr[1] = {mStereoBufferR.data()};
        sample** rOut = mIR_R != nullptr ? mIR_R->Process(rArr, 1, (int)numFrames) : nullptr;
        for (size_t s = 0; s < numFrames; s++)
        {
          mStereoBufferL[s] = lOut[0][s];
          if (rOut != nullptr)
            mStereoBufferR[s] = rOut[0][s];
        }
        mStereoChannels[0] = mStereoBufferL.data();
        mStereoChannels[1] = mStereoBufferR.data();
        return mStereoChannels;
      }
      case kLoopSendPedalIdx:
      {
        // Taps the chain's current signal out to the aux output bus (channels
        // kLoopBusChannelOffset.. -- see PLUG_CHANNEL_IO) for the DAW to route to external
        // hardware or another plugin. Doesn't alter what continues through the chain itself.
        for (size_t s = 0; s < numFrames; s++)
        {
          outputs[kLoopBusChannelOffset][s] = in[0][s];
          outputs[kLoopBusChannelOffset + 1][s] = numChannels > 1 ? in[1][s] : in[0][s];
        }
        return in;
      }
      case kLoopReturnPedalIdx:
      {
        // Whatever's coming back on the aux input bus (copied at the top of ProcessBlock, before
        // anything could write the host's possibly-shared buffers), blended with the signal
        // arriving here by Mix -- ramped across the block so turning it doesn't click.
        const float from = mLoopMixSmoothed, to = loopMixTarget;
        for (size_t c = 0; c < 2; c++)
        {
          const sample* dry = in[std::min(c, numChannels - 1)];
          sample* ret = mLoopReturnPtrs[c];
          for (size_t s = 0; s < numFrames; s++)
          {
            const float wet = from + (to - from) * (float)(s + 1) / (float)numFrames;
            ret[s] = wet * ret[s] + (1.f - wet) * dry[s];
          }
        }
        mLoopMixSmoothed = to;
        return mLoopReturnPtrs;
      }
      default: return in;
    }
  };

  // Grid evaluation. Every source (block, or Input) records what it output this block, and keeps a
  // short history in mGridRing so that where paths with different latencies merge, the faster ones
  // can be delayed to line up with the slowest (no comb filtering between parallel paths).
  struct NodeOut
  {
    sample** ptr = nullptr;
    int ch = 0;
    int latency = 0;
    bool live = false; // something from Input reaches it
    bool postAmp = false; // AMP is upstream of it (so it runs in stereo)
  };
  NodeOut node[chaingrid::kNumIds]; // every source id: blocks and Input
  constexpr int kRingMask = kGridRingSize - 1;
  const int maxGridDelay = std::max(0, kGridRingSize - (int)numFrames);
  auto ringWrite = [&](int n) {
    const NodeOut& o = node[n];
    for (int c = 0; c < 2; c++)
    {
      // A mono source is stored in both channels, so folding its history back to mono is exact.
      const sample* src = o.ptr[std::min(c, o.ch - 1)];
      sample* ring = mGridRing[n][c].data();
      for (size_t s = 0; s < numFrames; s++)
        ring[(unsigned)(mGridRingPos + (int)s) & kRingMask] = src[s];
    }
  };
  node[chaingrid::kInput].ptr = triggerOutput;
  node[chaingrid::kInput].ch = 1;
  node[chaingrid::kInput].live = true;
  ringWrite(chaingrid::kInput);

  // The live sources cabled into dst (a block id, or kOutput), and what their mix needs.
  auto gatherSources = [&](int dst, int* srcs, int& maxLat, bool& anyStereo, bool& postAmp) -> int {
    int n = 0;
    maxLat = 0;
    anyStereo = postAmp = false;
    const chaingrid::Mask mask = grid.InputsOf(dst);
    for (int s = 0; s < chaingrid::kNumIds; s++)
      if (((mask >> s) & 1) && node[s].live && grid.EdgeValid(s, dst))
      {
        srcs[n++] = s;
        maxLat = std::max(maxLat, node[s].latency);
        anyStereo |= node[s].ch == 2;
        postAmp |= s == chaingrid::kAmp || node[s].postAmp;
      }
    return n;
  };
  // Averages the sources into mGridMix[mixIdx] at ch channels (a stereo source is folded for a mono
  // mix; a mono one is copied to both sides of a stereo mix). With no sources, it's silence.
  auto mixSources = [&](int mixIdx, const int* srcs, int n, int maxLat, int ch) {
    for (int c = 0; c < ch; c++)
      std::fill(mGridMixPtrs[mixIdx][c], mGridMixPtrs[mixIdx][c] + numFrames, 0.0);
    for (int i = 0; i < n; i++)
    {
      const NodeOut& o = node[srcs[i]];
      const int delay = std::min(maxLat - o.latency, maxGridDelay);
      for (int c = 0; c < ch; c++)
      {
        sample* mix = mGridMixPtrs[mixIdx][c];
        if (delay <= 0)
        {
          if (ch == 1 && o.ch == 2)
            for (size_t s = 0; s < numFrames; s++)
              mix[s] += 0.5 * (o.ptr[0][s] + o.ptr[1][s]);
          else
          {
            const sample* in = o.ptr[std::min(c, o.ch - 1)];
            for (size_t s = 0; s < numFrames; s++)
              mix[s] += in[s];
          }
        }
        else
        {
          const int start = mGridRingPos - delay;
          const sample* r0 = mGridRing[srcs[i]][0].data();
          const sample* r1 = mGridRing[srcs[i]][1].data();
          const sample* r = ch == 1 ? nullptr : mGridRing[srcs[i]][c].data();
          for (size_t s = 0; s < numFrames; s++)
          {
            const unsigned idx = (unsigned)(start + (int)s) & kRingMask;
            mix[s] += r ? r[idx] : 0.5 * (r0[idx] + r1[idx]);
          }
        }
      }
    }
    if (n > 1)
      for (int c = 0; c < ch; c++)
        for (size_t s = 0; s < numFrames; s++)
          mGridMixPtrs[mixIdx][c][s] /= (double)n;
  };

  // Cables only run rightwards, so column order is a valid processing order.
  int order[chaingrid::kNumIds];
  int numOrdered = 0;
  for (int col = 0; col < chaingrid::kCols; col++)
    for (int b = 0; b < chaingrid::kNumIds; b++)
      if (grid.Placed(b) && chaingrid::CellCol(grid.cell[b]) == col)
        order[numOrdered++] = b;

  bool loopSendRan = false;
  int srcs[chaingrid::kNumIds];
  for (int i = 0; i < numOrdered; i++)
  {
    const int b = order[i];
    int maxLat = 0;
    bool anyStereo = false, postAmp = false;
    const int n = gatherSources(b, srcs, maxLat, anyStereo, postAmp);
    if (n == 0)
      continue;
    // The amp model and NAM pedals are mono-only (a hard constraint of the NAM engine). Everything
    // else runs in stereo after the amp (so Chorus/Delay width and a stereo cab can be heard) or
    // once a stereo block feeds it, and in mono before that (as cheap as the old pre-amp side).
    const bool monoOnly = b == chaingrid::kAmp || (b >= kNumBuiltInPedals && b < kNumPedals);
    const int ch = monoOnly ? 1 : ((anyStereo || postAmp) ? 2 : 1);
    mixSources(b, srcs, n, maxLat, ch);
    sample** in = mGridMixPtrs[b];
    NodeOut out;
    out.ptr = in;
    out.ch = ch;
    out.latency = maxLat;
    out.live = true;
    out.postAmp = postAmp || b == chaingrid::kAmp;
    // 1.42.0: a block whose On switch just flipped fades its input in or out (kBlockFadeS,
    // smoothstep) and the dry signal the other way: out = (1 - g) x in + block(g x in). Feeding the
    // block a fading input (rather than crossfading its output) means a delay line or reverb fills
    // and empties smoothly -- no echo starting mid-note -- and a delay, reverb, modulation or cab
    // switched off keeps ringing out (trails) until it's silent. Blocks that can't process (no IR or
    // capture) are simply out of the path.
    const double fadeTarget = processes[b] ? 1.0 : 0.0;
    if (!fades[b] || !available[b])
    {
      mBlockFade[b] = fadeTarget;
      mBlockTail[b] = false;
    }
    const bool fading = mBlockFade[b] != fadeTarget;
    const bool tailing = !fading && fadeTarget == 0.0 && mBlockTail[b];
    if (fading || tailing)
    {
      double f = mBlockFade[b];
      for (size_t s = 0; s < numFrames; s++)
      {
        f = fadeTarget > f ? std::min(fadeTarget, f + blockFadeStep) : std::max(fadeTarget, f - blockFadeStep);
        mBlockFadeGain[s] = f * f * (3.0 - 2.0 * f);
      }
      mBlockFade[b] = f;
      for (int c = 0; c < ch; c++)
      {
        std::copy(in[c], in[c] + numFrames, mBlockFadeDry[c].data());
        for (size_t s = 0; s < numFrames; s++)
          in[c][s] *= mBlockFadeGain[s];
      }
      if (fading && f == 0.0)
      {
        mBlockTail[b] = hasTail(b); // Switched off: let it ring out
        mBlockTailSamples[b] = 0;
      }
    }
    if (processes[b] || fading || tailing)
    {
      if (b == chaingrid::kAmp)
      {
        // AMP: the model, then the noise gate's gain (its trigger reads the chain's input, above),
        // then the tone stack. With the REAL FEEL pedal's Shared B+ on, its sag first turns down the
        // signal into the model (the preamp on the sagging supply), ramped across the block.
        const double feedTo = _RealFeelPedalPlaced() ? mRealFeel.PreampGain() : 1.0;
        if (feedTo != 1.0 || mAmpFeedGain != 1.0)
        {
          const double feedFrom = mAmpFeedGain;
          for (size_t s = 0; s < numFrames; s++)
            in[0][s] *= feedFrom + (feedTo - feedFrom) * (double)(s + 1) / (double)numFrames;
          mAmpFeedGain = feedTo;
        }
        // A/B pre-roll (1.42.0): the incoming slot's model hears the same input, unheard.
        if (_ABPrerolling())
        {
          const int incoming = mABSlotTarget.load();
          if (incoming >= 0 && incoming < kNumABSlots && incoming != mActiveSlot && mSlotModel[incoming] != nullptr
              && mABPrerollBuf[0].size() >= numFrames)
          {
            std::copy(in[0], in[0] + numFrames, mABPrerollBuf[0].data());
            mABPrerollIn[0] = mABPrerollBuf[0].data();
            mABPrerollOut[0] = mABPrerollBuf[1].data();
            mSlotModel[incoming]->process(mABPrerollIn, mABPrerollOut, nFrames);
          }
        }
        if (mModel != nullptr)
          mModel->process(in, mOutputPointers, nFrames);
        else
          _FallbackDSP(in, mOutputPointers, numChannelsInternal, numFrames);
        sample** gated =
          noiseGateActive ? mNoiseGateGain.Process(mOutputPointers, numChannelsInternal, numFrames) : mOutputPointers;
        out.ptr = (toneStackActive && mToneStack != nullptr) ? mToneStack->Process(gated, numChannelsInternal, nFrames)
                                                             : gated;
        out.latency += mModel != nullptr ? mModel->GetLatency() : 0;
      }
      else
      {
        out.ptr = applyPedal(b, in, (size_t)ch);
        // Stereo out whatever comes in: the cabs and the loop return (and CAB LAB's mics and room
        // since 1.42.0; it used to fold them to mono unless AMP or a stereo block came first).
        if (b == kCabPedalIdx || b == kLoopReturnPedalIdx || ExtraCabIndex(b) >= 0 || b == kCabLabPedalIdx)
          out.ch = 2;
        if (b == kTubePedalIdx)
          out.latency += mTubeStage[0].GetLatency() + (_TubeTakesCapture() && mModel != nullptr ? mModel->GetLatency() : 0);
        else if (b == chaingrid::kTubeLab2)
          out.latency += mTubeStage2[0].GetLatency()
                         + (_ReadTubeParams(1).capture && mLab2Model != nullptr ? mLab2Model->GetLatency() : 0);
        else if (b >= kNumBuiltInPedals && b < kNumPedals)
          out.latency += mNamPedalModel[b - kNumBuiltInPedals]->GetLatency();
        loopSendRan |= b == kLoopSendPedalIdx;
      }
      if (fading || tailing)
      {
        // The dry signal, faded the other way, plus what the block made of its faded input.
        const int outCh = std::max(out.ch, ch);
        double peak = 0.0;
        for (int c = 0; c < outCh; c++)
        {
          const sample* dry = mBlockFadeDry[std::min(c, ch - 1)].data();
          const sample* wet = out.ptr[std::min(c, out.ch - 1)];
          sample* dst = mBlockFadeOutPtrs[b][c];
          for (size_t s = 0; s < numFrames; s++)
          {
            dst[s] = dry[s] * (1.0 - mBlockFadeGain[s]) + wet[s];
            peak = std::max(peak, std::fabs((double)wet[s]));
          }
        }
        out.ptr = mBlockFadeOutPtrs[b];
        out.ch = outCh;
        // A ringing-out block stops once it's silent (or after kMaxTailS, for one that never is).
        if (tailing && (peak < 1e-7 || (mBlockTailSamples[b] += (int)numFrames) > (int)(kMaxTailS * sampleRate)))
          mBlockTail[b] = false;
      }
    }
    if (abFading && (b == chaingrid::kAmp || b == kCabPedalIdx))
      for (int c = 0; c < out.ch; c++)
        for (size_t s = 0; s < numFrames; s++)
          out.ptr[c][s] *= mABGainBuf[s];
    node[b] = out;
    ringWrite(b);
  }

  // Output: stereo, the average of what's cabled into it.
  {
    int maxLat = 0;
    bool anyStereo = false, postAmp = false;
    const int n = gatherSources(chaingrid::kOutput, srcs, maxLat, anyStereo, postAmp);
    mixSources(chaingrid::kNumIds, srcs, n, maxLat, 2);
    mGridLatency = maxLat;
  }
  mGridRingPos = (mGridRingPos + (int)numFrames) & kRingMask;
  if (GetLatency() != _TotalLatency())
    _UpdateLatency();
  // A connected aux output with the Send off the chain carries silence, not stale buffer contents.
  if (!loopSendRan && numChannelsExternalOut > (size_t)kLoopBusChannelOffset)
    for (size_t c = kLoopBusChannelOffset; c < numChannelsExternalOut; c++)
      std::fill(outputs[c], outputs[c] + numFrames, 0.0);

  const size_t kNumChannelsStereo = 2;
  sample** irPointers = mGridMixPtrs[chaingrid::kNumIds];
  _ABEndBlock(numFrames);

  // Post-amp tone shaping: low-end density and high-end boost.
  // Each is a bipolar biquad gain around 0 dB (unity at 0), gated by its own on/off toggle like the EQ switch.
  // 1.42.0: the toggles crossfade over kBlockFadeS, like the blocks' On switches (they clicked).
  auto shelf = [&](auto& filter, double& fade, bool on, double freq, double gainDB, sample** in,
                   std::vector<sample>* dryBuf) -> sample** {
    const double target = on ? 1.0 : 0.0;
    if (fade == 0.0 && target == 0.0)
      return in;
    const recursive_linear_filter::BiquadParams params(sampleRate, freq, 0.707, gainDB);
    filter.SetParams(params);
    if (fade == 1.0 && target == 1.0)
      return filter.Process(in, kNumChannelsStereo, numFrames);
    for (size_t c = 0; c < kNumChannelsStereo; c++)
      std::copy(in[c], in[c] + numFrames, dryBuf[c].data());
    sample** wet = filter.Process(in, kNumChannelsStereo, numFrames);
    double f = fade;
    for (size_t c = 0; c < kNumChannelsStereo; c++)
    {
      f = fade;
      sample* d = dryBuf[c].data();
      for (size_t s = 0; s < numFrames; s++)
      {
        f = target > f ? std::min(target, f + blockFadeStep) : std::max(target, f - blockFadeStep);
        const double g = f * f * (3.0 - 2.0 * f);
        d[s] += g * (wet[c][s] - d[s]);
      }
    }
    fade = f;
    mShelfPtrs[0] = dryBuf[0].data();
    mShelfPtrs[1] = dryBuf[1].data();
    return mShelfPtrs;
  };
  sample** densityPointers = shelf(mDensityFilter, mDensityFade, GetParam(kDensityActive)->Bool(), 350.0,
                                   GetParam(kDensity)->Value(), irPointers, mShelfDry);
  // (Copied out of the shelf's pointer array before the Boost reuses it.)
  sample* densityCopy[2] = {densityPointers[0], densityPointers[1]};
  sample** presencePointers = shelf(mPresenceFilter, mBoostFade, GetParam(kPresenceActive)->Bool(), 4000.0,
                                    GetParam(kPresence)->Value(), densityCopy, mBlockFadeDry);

  // And the HPF for DC offset (Issue 271)
  const double highPassCutoffFreq = kDCBlockerFrequency;
  // const double lowPassCutoffFreq = 20000.0;
  const recursive_linear_filter::HighPassParams highPassParams(sampleRate, highPassCutoffFreq);
  // const recursive_linear_filter::LowPassParams lowPassParams(sampleRate, lowPassCutoffFreq);
  mHighPass.SetParams(highPassParams);
  // mLowPass.SetParams(lowPassParams);
  sample** hpfPointers = mHighPass.Process(presencePointers, kNumChannelsStereo, numFrames);

  // Output EQ (1.46.0, ProEQ.h): the last thing before Master Out and the limiter. Flat (every band
  // off), it's out of the path entirely. Its input and the final output feed the spectrum page/strip.
  mOutEq.SetParams(
    _ReadEqParams(kOutEqBandFirst, kOutEqBandExtFirst, kOutEqTrim, kOutEqOutput, GetParam(kOutEqOn)->Bool()));
  mOutTapIn.Push(hpfPointers, (int)kNumChannelsStereo, (int)numFrames);
  if (mOutEq.Active())
    mOutEq.Process(hpfPointers, (int)kNumChannelsStereo, (int)numFrames);
  // Width and Widen (1.58.0, StereoWidth.h; Width 0% is 1.55.0's Mono): at 100% / 0% it's skipped.
  mStereoWidth.SetTargets(GetParam(kOutputWidth)->Value() / 100.0, GetParam(kOutputWiden)->Value() / 100.0);
  mStereoWidth.Process(hpfPointers[0], hpfPointers[1], (int)numFrames);
  if (mOutputPageOpen.load(std::memory_order_relaxed))
    mOutCorrelation.store(
      (float)StereoWidth::Correlation(hpfPointers[0], hpfPointers[1], (int)numFrames), std::memory_order_relaxed);
  // sample** lpfPointers = mLowPass.Process(hpfPointers, numChannelsInternal, numFrames);

  // restore previous floating point state
  std::feupdateenv(&fe_state);

  // Let's get outta here
  // This is where we exit mono for whatever the output requires.
  _ProcessOutput(hpfPointers, outputs, numFrames, kNumChannelsStereo, numChannelsMainOut);
  mOutTapOut.Push(outputs, (int)std::min<size_t>(numChannelsMainOut, 2), (int)numFrames);
  // _ProcessOutput(lpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  // * Output of input leveling (inputs -> mInputPointers),
  // * Output of output leveling (mOutputPointers -> outputs)
  _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
}

void NeuralAmpModeler::OnReset()
{
  const auto sampleRate = GetSampleRate();
  const int maxBlockSize = GetBlockSize();

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  // If there is a model or IR loaded, they need to be checked for resampling.
  _ResetModelAndIR(sampleRate, GetBlockSize());
  mToneStack->Reset(sampleRate, maxBlockSize);
  mCompressor.SetSampleRate(sampleRate);
  mOverdrive.SetSampleRate(sampleRate);
  mChorus.SetSampleRate(sampleRate);
  mDelay.SetSampleRate(sampleRate);
  mReverb.SetSampleRate(sampleRate);
  mEqPedal.SetSampleRate(sampleRate);
  mTremolo.SetSampleRate(sampleRate);
  mFlanger.SetSampleRate(sampleRate);
  mSkyVerb.SetSampleRate(sampleRate);
  mFlangerDoubler.SetSampleRate(sampleRate);
  mWah.SetSampleRate(sampleRate);
  mPrecisionDrive.SetSampleRate(sampleRate);
  mApexPreamp.SetSampleRate(sampleRate);
  mGate.SetSampleRate(sampleRate);
  mGate.Reset();
  mStudioComp.SetSampleRate(sampleRate);
  mStudioComp.Reset();
  mTrackGate.SetSampleRate(sampleRate); // 1.66.0
  mDigiDelay.SetSampleRate(sampleRate);
  mAnalogDelay.SetSampleRate(sampleRate);
  mTapeEcho.SetSampleRate(sampleRate);
  mSpringVerb.SetSampleRate(sampleRate);
  for (auto& k : mGateKeyBuf)
    k.resize(std::max(maxBlockSize, 1));
  mProEq.Reset(sampleRate);
  mOutEq.Reset(sampleRate);
  for (spectrum::Tap* t : {&mProEqTapIn, &mProEqTapOut, &mOutTapIn, &mOutTapOut})
    t->SetSampleRate(sampleRate);
  mPhaser.SetSampleRate(sampleRate);
  mRealFeel.Reset(sampleRate);
  mHumFilter.Reset(sampleRate);
  mLimiter.Reset(sampleRate); // (Its window is in samples: rebuilt for the rate.)
  mStereoWidth.Reset(sampleRate);
  mAmpFeedGain = 1.0;
  // 1.42.0: the switch fades and gain ramps start where they're going.
  mBlockFadeInit = false;
  mDensityFade = GetParam(kDensityActive)->Bool() ? 1.0 : 0.0;
  mBoostFade = GetParam(kPresenceActive)->Bool() ? 1.0 : 0.0;
  mInputGainApplied = mOutputGainApplied = -1.0;
  for (int i = 0; i < chaingrid::kExtraCopiesPerType; i++)
  {
    mCompressorCopy[i].SetSampleRate(sampleRate);
    mOverdriveCopy[i].SetSampleRate(sampleRate);
    mChorusCopy[i].SetSampleRate(sampleRate);
    mDelayCopy[i].SetSampleRate(sampleRate);
    mReverbCopy[i].SetSampleRate(sampleRate);
    mEqPedalCopy[i].SetSampleRate(sampleRate);
    mTremoloCopy[i].SetSampleRate(sampleRate);
  }
  for (auto& stage : mTubeStage)
    stage.Reset(sampleRate, maxBlockSize);
  for (auto& stage : mTubeStage2) // 1.67.0
    stage.Reset(sampleRate, maxBlockSize);
  if (mStagedLab2Model != nullptr)
    mStagedLab2Model->Reset(sampleRate, maxBlockSize);
  else if (mLab2Model != nullptr)
    mLab2Model->Reset(sampleRate, maxBlockSize);
  mTuner.SetSampleRate(sampleRate);
  _ResetNamPedals(sampleRate, maxBlockSize);
  mCabLab.Reset(sampleRate, maxBlockSize);
  mCabMono.assign(static_cast<size_t>(std::max(maxBlockSize, 1)), 0.0);
  mCabL.assign(mCabMono.size(), 0.0);
  mCabR.assign(mCabMono.size(), 0.0);
  _PrepareGridBuffers(static_cast<size_t>(std::max(maxBlockSize, 1)));
  for (auto& ring : mGridRing)
    for (auto& channel : ring)
      std::fill(channel.begin(), channel.end(), 0.0);
  mGridRingPos = 0;
  // An IR is resampled for the rate it was loaded at; reload it if the host rate changed.
  if (mCabLabIR != nullptr && mCabLabIR->GetSampleRate() != sampleRate && mCabLabIRPath.GetLength())
    _StageCabLabIR(mCabLabIRPath);
  if (mCabLabIR2 != nullptr && mCabLabIR2->GetSampleRate() != sampleRate && mCabLabIR2Path.GetLength())
    _StageCabLabIR2(mCabLabIR2Path);
  for (int k = 0; k < kNumExtraCabs; k++)
    if (mExtraCabIR[k] != nullptr && mExtraCabIR[k]->GetSampleRate() != sampleRate && mExtraCabIRPath[k].GetLength())
      _StageExtraCabIR(k, mExtraCabIRPath[k]);
  _UpdateLatency();
}

void NeuralAmpModeler::ProcessMidiMsg(const IMidiMsg& msg)
{
  // Audio thread: nothing here touches params or the GUI (see _ApplyMidiMappings).
  if (msg.StatusMsg() != IMidiMsg::kControlChange)
    return;
  const int cc = msg.mData1;
  const int value = msg.mData2;
  if (cc < 0 || cc > 127)
    return;
  // Every message counts as a press for a switch target: latching footswitches toggle (one
  // press sends 127, the next 0), so this toggles the app's current state instead of having to
  // agree with the pedal's own on/off memory. Knob targets use the last value.
  mMidiCCValue[cc].store(value, std::memory_order_relaxed);
  mMidiCCCount[cc].fetch_add(1, std::memory_order_relaxed);
  mMidiLastValue.store(value, std::memory_order_relaxed);
  mMidiLastCC.store(cc, std::memory_order_relaxed);
  mMidiSeq.fetch_add(1, std::memory_order_release);
  if (mMidiCCIsVolume[cc].load(std::memory_order_relaxed))
  {
    const double x = std::clamp(value / 127.0, 0.0, 1.0);
    mExprVolumeTarget.store(x * x, std::memory_order_relaxed); // squared: a volume pedal's taper
  }
  // Tap tempo (1.71.0): timed now, not when OnIdle gets to it (that would be up to a frame late).
  // Every message is a tap (a latching switch sends 127, then 0).
  if (mMidiCCIsTap[cc].load(std::memory_order_relaxed))
  {
    const int w = mMidiTapWrite.load(std::memory_order_relaxed);
    mMidiTapTime[w % kMidiTapQueue].store(
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),
      std::memory_order_relaxed);
    mMidiTapWrite.store(w + 1, std::memory_order_release);
  }
}

bool NeuralAmpModeler::_MidiIsToggle(int target) const
{
  if (target == kMidiTargetTuner)
    return true;
  if (target < 0 || target >= kNumParams)
    return false;
  const IParam* p = GetParam(target);
  return p->Type() == IParam::kTypeBool || (p->Type() == IParam::kTypeEnum && p->NDisplayTexts() == 2);
}

void NeuralAmpModeler::_ApplyMidiMappings()
{
  {
    const int w = mMidiTapWrite.load(std::memory_order_acquire);
    if (w - mMidiTapRead > kMidiTapQueue)
      mMidiTapRead = w - kMidiTapQueue;
    for (; mMidiTapRead < w; mMidiTapRead++)
      if (!mMidiLearnOn)
        _TempoTap(mMidiTapTime[mMidiTapRead % kMidiTapQueue].load(std::memory_order_relaxed));
  }
  if (mTempoPushPending)
  {
    mTempoPushPending = false;
    _PushTempoToDelays();
  }
  const int seq = mMidiSeq.load(std::memory_order_acquire);
  if (mMidiLearnOn)
  {
    // Learning: the next message maps the selected control to its CC. Nothing else acts.
    if (seq != mMidiSeqSeen)
    {
      mMidiSeqSeen = seq;
      const int cc = mMidiLastCC.load(std::memory_order_relaxed);
      if (mMidiLearnTarget != kMidiNoTarget && cc >= 0)
        _MidiLearnAssign(mMidiLearnTarget, cc, mMidiLearnAdd);
    }
    for (auto& count : mMidiCCCount)
      count.exchange(0, std::memory_order_relaxed);
    return;
  }
  mMidiSeqSeen = seq;

  auto set = [this](int param, double normalized) {
    SetParameterValue(param, normalized); // sets it, tells the host, and runs OnParamChange
    SendParameterValueFromDelegate(param, normalized, true); // GUI controls + OnParamChangeUI
  };
  // Whether a switch's block is in the chain (a pedal's On switch with its pedal off the grid
  // doesn't count when deciding which way a group goes).
  auto inChain = [this](int param) {
    bool isPedalSwitch = false;
    for (int id = 0; id < chaingrid::kNumIds; id++)
      if (chaingrid::IsPedal(id) && PedalOf(id).engagedParam == param)
      {
        isPedalSwitch = true;
        if (GetParam(PedalOf(id).activeParam)->Value() > 0.5)
          return true;
      }
    return !isPedalSwitch;
  };
  bool changed = false;
  for (int cc = 0; cc < 128; cc++)
  {
    const int count = mMidiCCCount[cc].exchange(0, std::memory_order_relaxed);
    if (count == 0 || mMidiMap[cc].empty())
      continue;
    const int value = mMidiCCValue[cc].load(std::memory_order_relaxed);
    std::vector<int> toggles;
    for (int target : mMidiMap[cc])
    {
      if (target == kMidiTargetVolume)
        continue;
      if (_MidiIsToggle(target))
        toggles.push_back(target);
      else if (target >= 0 && target < kNumParams)
      {
        set(target, std::clamp(value / 127.0, 0.0, 1.0));
        changed = true;
        if (target == kTempoBPM)
          _PushTempoToDelays();
      }
    }
    if (toggles.empty() || count % 2 == 0) // An even number of presses since the last tick: no change
      continue;
    changed = true;
    // One switch, several targets: they move together. Any on (in the chain) -> all off; else all on.
    bool anyOn = false, anyCounted = false;
    for (int t : toggles)
      if (t >= 0 && inChain(t))
      {
        anyCounted = true;
        anyOn |= GetParam(t)->Value() > 0.5;
      }
    if (!anyCounted)
      for (int t : toggles)
        if (t >= 0)
          anyOn |= GetParam(t)->Value() > 0.5;
    for (int t : toggles)
    {
      if (t == kMidiTargetTuner)
      {
        if (auto* pGraphics = GetUI())
          if (auto* pTuner = pGraphics->GetControlWithTag(kCtrlTagTunerBox))
            pTuner->Hide(!pTuner->IsHidden());
        continue;
      }
      if (toggles.size() == 1)
        anyOn = GetParam(t)->Value() > 0.5;
      set(t, GetParam(t)->ToNormalized(anyOn ? 0.0 : 1.0));
    }
  }
  if (changed)
    if (auto* pGraphics = GetUI())
      pGraphics->SetAllControlsDirty(); // pedal chips draw their Engaged state but aren't bound to it
}

void NeuralAmpModeler::_SetDefaultMidiMap()
{
  // The default footswitch layout of 1.29.0-1.42.1: footswitches CC 76-83, the toe switch CC 64, and
  // CC 1 / 4 / 7 / 11 as a volume pedal.
  for (auto& targets : mMidiMap)
    targets.clear();
  mMidiMap[76] = {kCompressorEngaged};
  mMidiMap[77] = {kOverdriveEngaged};
  mMidiMap[78] = {kChorusEngaged};
  mMidiMap[79] = {kDelayEngaged};
  constexpr int kReverbType = 4;
  for (int copy = 0; copy < chaingrid::NumCopies(kReverbType); copy++) // Every reverb, together (1.42.1)
    mMidiMap[80].push_back(PedalOf(chaingrid::PedalCopyId(kReverbType, copy)).engagedParam);
  mMidiMap[81] = {kABSlot};
  mMidiMap[82] = {kMidiTargetTuner};
  mMidiMap[83] = {kLoopEngaged};
  mMidiMap[64] = {kTubeEngaged};
  for (int cc : {1, 4, 7, 11})
    mMidiMap[cc] = {kMidiTargetVolume};
}

std::string NeuralAmpModeler::_MidiMapPath() const
{
  // Next to settings.ini and the autosave, shared by the app and the plug-in: it's your pedal's
  // layout, not part of a sound.
  const char* base = std::getenv("LOCALAPPDATA");
  std::string dir = base ? std::string(base) + "\\" BUNDLE_NAME : std::string(".");
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
  return dir + "\\midimap.txt";
}

void NeuralAmpModeler::_LoadMidiMap()
{
  std::ifstream f(std::filesystem::u8path(_MidiMapPath()));
  std::string header;
  if (!f || !std::getline(f, header) || header.rfind("GriffinMidiMap", 0) != 0)
  {
    _SetDefaultMidiMap();
    _SyncMidiVolumeFlags();
    return;
  }
  for (auto& targets : mMidiMap)
    targets.clear();
  std::string line;
  while (std::getline(f, line))
  {
    std::istringstream in(line);
    int cc = -1;
    std::string kind;
    if (!(in >> cc >> kind) || cc < 0 || cc > 127)
      continue;
    int target = kMidiNoTarget;
    if (kind == "tuner")
      target = kMidiTargetTuner;
    else if (kind == "volume")
      target = kMidiTargetVolume;
    else if (kind == "tap")
      target = kMidiTargetTap;
    else if (kind == "param")
    {
      // By index (params never move), checked against the name; else the first param of that name.
      int idx = -1;
      std::string name;
      in >> idx;
      std::getline(in >> std::ws, name);
      if (idx >= 0 && idx < kNumParams && name == GetParam(idx)->GetName())
        target = idx;
      else
        for (int p = 0; p < kNumParams && target == kMidiNoTarget; p++)
          if (name == GetParam(p)->GetName())
            target = p;
    }
    if (target != kMidiNoTarget
        && std::find(mMidiMap[cc].begin(), mMidiMap[cc].end(), target) == mMidiMap[cc].end())
      mMidiMap[cc].push_back(target);
  }
  _SyncMidiVolumeFlags();
}

void NeuralAmpModeler::_SaveMidiMap() const
{
  std::ofstream f(std::filesystem::u8path(_MidiMapPath()), std::ios::trunc);
  if (!f)
    return;
  f << "GriffinMidiMap 1 (cc, then param <index> <name> / tuner / volume / tap)\n";
  for (int cc = 0; cc < 128; cc++)
    for (int target : mMidiMap[cc])
    {
      if (target == kMidiTargetTuner)
        f << cc << " tuner\n";
      else if (target == kMidiTargetVolume)
        f << cc << " volume\n";
      else if (target == kMidiTargetTap)
        f << cc << " tap\n";
      else if (target >= 0 && target < kNumParams)
        f << cc << " param " << target << " " << GetParam(target)->GetName() << "\n";
    }
}

void NeuralAmpModeler::_SyncMidiVolumeFlags()
{
  for (int cc = 0; cc < 128; cc++)
  {
    mMidiCCIsVolume[cc].store(std::find(mMidiMap[cc].begin(), mMidiMap[cc].end(), kMidiTargetVolume)
                                != mMidiMap[cc].end(),
                              std::memory_order_relaxed);
    mMidiCCIsTap[cc].store(std::find(mMidiMap[cc].begin(), mMidiMap[cc].end(), kMidiTargetTap) != mMidiMap[cc].end(),
                           std::memory_order_relaxed);
  }
}

void NeuralAmpModeler::_TempoTap(double t)
{
  // Taps more than 2 s apart (slower than 30 BPM) start a new count.
  if (mNumTaps > 0 && t - mTapTimes[mNumTaps - 1] > 2.0)
    mNumTaps = 0;
  if (mNumTaps == 5)
  {
    std::rotate(mTapTimes, mTapTimes + 1, mTapTimes + 5);
    mNumTaps = 4;
  }
  mTapTimes[mNumTaps++] = t;
  mBeatAnchor = t;
  if (mNumTaps < 2)
    return;
  // The average of the last (up to 4) gaps.
  const double beat = (mTapTimes[mNumTaps - 1] - mTapTimes[0]) / (mNumTaps - 1);
  if (beat <= 0.0)
    return;
  const IParam* p = GetParam(kTempoBPM);
  const double bpm = std::clamp(std::round(600.0 / beat) / 10.0, p->GetMin(), p->GetMax());
  const double normalized = p->ToNormalized(bpm);
  SetParameterValue(kTempoBPM, normalized);
  SendParameterValueFromDelegate(kTempoBPM, normalized, true);
  _PushTempoToDelays();
}

void NeuralAmpModeler::_PushTempoToDelays()
{
  static const double kBeats[] = {0.0, 1.0, 0.75, 0.5, 2.0 / 3.0, 0.25}; // Off, 1/4, 1/8., 1/8, 1/4T, 1/16
  const double beatMs = 60000.0 / GetParam(kTempoBPM)->Value();
  auto set = [this](int param, double value) {
    const double normalized = GetParam(param)->ToNormalized(value);
    SetParameterValue(param, normalized);
    SendParameterValueFromDelegate(param, normalized, true);
  };
  // A time past the delay's range is halved (or doubled) until it fits: still in time.
  auto fit = [](double ms, double lo, double hi) {
    while (ms > hi && ms * 0.5 >= lo)
      ms *= 0.5;
    while (ms < lo && ms * 2.0 <= hi)
      ms *= 2.0;
    return std::clamp(ms, lo, hi);
  };
  auto timeFor = [&](int divParam) { return beatMs * kBeats[std::clamp(GetParam(divParam)->Int(), 0, 5)]; };
  constexpr int kDelayType = 3;
  static_assert(chaingrid::NumCopies(kDelayType) == 3, "DELAY, DELAY 2, DELAY 3");
  for (int copy = 0; copy < 3; copy++)
  {
    const int divParam = kDelayTapDiv + copy;
    if (GetParam(divParam)->Int() == 0)
      continue;
    const int timeParam = PedalOf(chaingrid::PedalCopyId(kDelayType, copy)).knobs[0].paramIdx;
    const IParam* p = GetParam(timeParam);
    set(timeParam, fit(timeFor(divParam), p->GetMin(), p->GetMax()));
  }
  static const int kOtherDelays[2][2] = {{kDigiTapDiv, kDigiTime}, {kAnaTapDiv, kAnaTime}};
  for (const auto& d : kOtherDelays)
    if (GetParam(d[0])->Int() != 0)
      set(d[1], fit(timeFor(d[0]), GetParam(d[1])->GetMin(), GetParam(d[1])->GetMax()));
  if (GetParam(kTapeTapDiv)->Int() != 0)
  {
    // The tape's longest head on the beat (head 1 = 300 - 230 x Rate/10 ms; heads 2 and 3 at 2x and 3x).
    static const int kLongestHead[7] = {1, 2, 3, 2, 3, 3, 3}; // 1, 2, 3, 1+2, 2+3, 1+2+3, 1+3
    const int heads = kLongestHead[std::clamp(GetParam(kTapeHeads)->Int(), 0, 6)];
    const double head1Ms = fit(timeFor(kTapeTapDiv) / heads, 70.0, 300.0);
    set(kTapeRate, (300.0 - head1Ms) / 230.0 * 10.0);
  }
  if (auto* pGraphics = GetUI())
    pGraphics->SetAllControlsDirty();
}

double NeuralAmpModeler::_BeatPhase() const
{
  if (mBeatAnchor < 0.0)
    return -1.0;
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  const double beat = 60.0 / GetParam(kTempoBPM)->Value();
  const double phase = std::fmod(now - mBeatAnchor, beat) / beat;
  return phase < 0.0 ? phase + 1.0 : phase;
}

void NeuralAmpModeler::_SetMidiLearn(bool on)
{
  mMidiLearnOn = on;
  mMidiLearnTarget = kMidiNoTarget;
  mMidiSeqSeen = mMidiSeq.load(std::memory_order_acquire);
  for (auto& count : mMidiCCCount) // Presses made while learning don't act afterwards
    count.exchange(0, std::memory_order_relaxed);
  if (auto* pGraphics = GetUI())
  {
    if (auto* overlay = pGraphics->GetControlWithTag(kCtrlTagMidiLearn))
      overlay->Hide(!on);
    pGraphics->SetAllControlsDirty();
  }
}

void NeuralAmpModeler::_MidiLearnAssign(int target, int cc, bool add)
{
  // A control has one CC: it leaves the one it had. Learning replaces whatever the CC did before
  // (1.43.1: it used to add to it, so a footswitch kept its old job too -- REVERB learned onto the
  // A/B switch flipped A/B as well); Shift-click adds instead, for several controls on one switch
  // (they toggle together). Even then a CC stops being the volume pedal.
  for (auto& targets : mMidiMap)
    targets.erase(std::remove(targets.begin(), targets.end(), target), targets.end());
  auto& to = mMidiMap[cc];
  if (add)
    to.erase(std::remove(to.begin(), to.end(), kMidiTargetVolume), to.end());
  else
    to.clear();
  to.push_back(target);
  _SyncMidiVolumeFlags();
  _SaveMidiMap();
}

void NeuralAmpModeler::_MidiClearTarget(int target)
{
  for (auto& targets : mMidiMap)
    targets.erase(std::remove(targets.begin(), targets.end(), target), targets.end());
  if (mMidiLearnTarget == target)
    mMidiLearnTarget = kMidiNoTarget;
  _SyncMidiVolumeFlags();
  _SaveMidiMap();
}

int NeuralAmpModeler::_MidiCCOf(int target) const
{
  for (int cc = 0; cc < 128; cc++)
    if (std::find(mMidiMap[cc].begin(), mMidiMap[cc].end(), target) != mMidiMap[cc].end())
      return cc;
  return -1;
}

std::string NeuralAmpModeler::_MidiTargetName(int target) const
{
  if (target == kMidiTargetTuner)
    return "Tuner";
  if (target == kMidiTargetVolume)
    return "Volume pedal";
  if (target == kMidiTargetTap)
    return "Tap tempo";
  if (target < 0 || target >= kNumParams)
    return "?";
  for (int id = 0; id < chaingrid::kNumIds; id++)
  {
    if (!chaingrid::IsPedal(id))
      continue;
    const PedalDef& def = PedalOf(id);
    if (def.engagedParam == target)
      return std::string(def.name) + " On";
    for (const auto& knob : def.knobs)
      if (knob.paramIdx == target && knob.label)
        return std::string(def.name) + " " + knob.label;
  }
  switch (target)
  {
    case kAmpEngaged: return "AMP On";
    case kIRToggle: return "CAB On";
    case kABSlot: return "A/B";
    case kLoopEngaged: return "FX LOOP On";
    case kInputLevel: return "Master In";
    case kOutputLevel: return "Master Out";
    default: break;
  }
  std::string name = GetParam(target)->GetName();
  if (name.rfind("Tube", 0) == 0 && name.size() > 4)
    return "TUBE LAB " + name.substr(4);
  return name;
}

void NeuralAmpModeler::OnIdle()
{
  _PackPollZip(); // 1.70.3
#ifdef APP_API
  _AutosaveIfDirty();
#endif

  _ApplyMidiMappings();

  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);

#if defined(APP_API) && defined(OS_WIN)
  // A buffer size picked on the Settings page: restart the app's audio with it (saved to the INI).
  if (mPendingBufferSize > 0)
  {
    const int size = mPendingBufferSize;
    mPendingBufferSize = 0;
    if (auto* pHost = IPlugAPPHost::sInstance.get())
    {
      pHost->SetBufferSize((uint32_t)size);
      mRequestedBufferSize = size;
      const int got = (int)pHost->GetStreamBufferSize();
      mDeviceBufferSize = got > 0 ? got : size;
      mDeviceBufferPollTicks = 0; // the INI now says the same; no need to re-read it right away
      if (auto* pUI = GetUI()) { if (auto* pPage = pUI->GetControlWithTag(kCtrlTagSettingsBox)) pPage->SetDirty(false); }
    }
  }
  // DRIVER PANEL / DEVICE on the Settings page (1.51.0).
  if (mPendingDriverPanel || mPendingDevicePrefs)
  {
    const bool panel = mPendingDriverPanel;
    mPendingDriverPanel = mPendingDevicePrefs = false;
    if (auto* pHost = IPlugAPPHost::sInstance.get())
    {
      if (panel)
      {
        pHost->OpenDriverControlPanel();
        mDriverPanelRetryTicks = 60; // about 2 s
      }
      else
        pHost->OpenPreferencesDialog();
    }
  }
  if (mDriverPanelRetryTicks > 0 && --mDriverPanelRetryTicks == 0)
  {
    // Still in front: the panel didn't come up (the first call may only have started the driver's helper).
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    if (foregroundPid == GetCurrentProcessId())
      if (auto* pHost = IPlugAPPHost::sInstance.get())
        pHost->OpenDriverControlPanel();
  }
  // The interface's own panel changed its buffer size (or the driver asked for a reset): the stream
  // restarts with the driver's size (1.51.0). Checked about once a second, with the INI below.
  if (mDeviceBufferPollTicks + 1 >= 30)
    if (auto* pHost = IPlugAPPHost::sInstance.get())
      pHost->RestartAudioIfDriverAsked();
  // The standalone's audio buffer: the size the running stream got, and the size in its settings file
  // (re-read about once a second, since it's changed in its own Preferences dialog, which doesn't tell us).
  if (++mDeviceBufferPollTicks >= 30)
  {
    mDeviceBufferPollTicks = 0;
    WDL_String ini;
    if (const char* appData = std::getenv("LOCALAPPDATA"))
    {
      ini.SetFormatted(1024, "%s\\" BUNDLE_NAME "\\settings.ini", appData);
      mRequestedBufferSize = (int)GetPrivateProfileIntA("audio", "buffer", 0, ini.Get());
    }
    const int got = IPlugAPPHost::sInstance ? (int)IPlugAPPHost::sInstance->GetStreamBufferSize() : 0;
    const int shown = got > 0 ? got : mRequestedBufferSize;
    if (shown != mDeviceBufferSize)
    {
      mDeviceBufferSize = shown;
      if (auto* pUI = GetUI()) { if (auto* pPage = pUI->GetControlWithTag(kCtrlTagSettingsBox)) pPage->SetDirty(false); }
    }
  }
#endif
  const int shownBlock = mDeviceBufferSize > 0 ? mDeviceBufferSize : GetBlockSize();
  if (mLatencyLabel
      && (shownBlock != mShownBlockSize || GetLatency() != mShownLatency || GetSampleRate() != mShownSampleRate))
  {
    mShownBlockSize = shownBlock;
    mShownLatency = GetLatency();
    mShownSampleRate = GetSampleRate();
    mLatencyLabel->SetDirty(false);
  }
  _UpdateClipIndicator();

  // The pedal list is packed, so taking one pedal out of it (or putting one back) moves the others.
  // A chip only re-reads its position when it draws, and only draws where the screen is being
  // repainted -- so without this, a moved chip could stay invisible and unclickable at its old spot.
  // The same goes for AMP, CAB and the loop: a stale CAB rect right after startup once took a click
  // meant for the block above it.
  if (GetUI())
  {
    for (auto* chip : mPedalChips)
      if (chip)
        chip->As<NAMPedalChipControl>()->RefreshPosition();
    for (auto* chip : {mAmpChip, mCabButton, mLoopSendChip, mLoopReturnChip})
      if (chip)
        chip->As<NAMPedalChipControl>()->RefreshPosition();
  }

  if (mLibraryPanelRelayoutPending)
  {
    mLibraryPanelRelayoutPending = false;
    if (auto* pGraphics = GetUI())
      LayoutUI(pGraphics);
  }

  {
    // The pitch search only runs while the tuner page is open (1.52.2).
    IControl* pTunerBox = GetUI() ? GetUI()->GetControlWithTag(kCtrlTagTunerBox) : nullptr;
    mTuner.SetActive(pTunerBox && !pTunerBox->IsHidden());
    const double tunerData[2] = {mTuner.GetFrequencyHz(), mTuner.GetInputRMS()};
    SendControlMsgFromDelegate(kCtrlTagTunerDisplay, kMsgTagTunerUpdate, sizeof(tunerData), tunerData);
  }
  _UpdateTubeDebug();
  _UpdateOutputPage();
  _UpdateChassis();
  if (auto* pGraphics = GetUI())
    if (auto* page = pGraphics->GetControlWithTag(kCtrlTagSpeakerLab); page && !page->IsHidden())
    {
      const bool dual = GetParam(kCabDual)->Int() == 1;
      const int cabShown = dual ? mLabSelectedCab : 0;
      for (int m = 0; m < cab::kNumLabMics; m++)
        for (IControl* c : mLabMicControls[m])
        {
          const bool hide = m != mLabSelectedMic || (!dual && c->GetParamIdx() == kLabMicCabFirst + m);
          if (c->IsHidden() != hide)
            c->Hide(hide);
        }
      for (int cab = 0; cab < 2; cab++)
        for (IControl* c : mLabCabControls[cab])
          if (c->IsHidden() != (cab != cabShown))
            c->Hide(cab != cabShown);
    }

  if (mNewModelLoadedInDSP)
  {
    if (auto* pGraphics = GetUI())
    {
      _UpdateControlsFromModel();
      mNewModelLoadedInDSP = false;
    }
  }
  if (mModelCleared)
  {
    if (auto* pGraphics = GetUI())
    {
      // FIXME -- need to disable only the "normalized" model
      // pGraphics->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
      static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->ClearModelInfo();
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))
        p->Hide(true);
      pGraphics->SetAllControlsDirty();
      mModelCleared = false;
    }
  }
  if (mSlotSwitched)
  {
    if (auto* pGraphics = GetUI())
    {
      if (mNAMPath.GetLength())
        SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
      else
        SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagClearedDisplay);

      if (mIRPath.GetLength())
        SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
      else
        SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagClearedDisplay);

      if (mIRPath_R.GetLength())
        SendControlMsgFromDelegate(kCtrlTagIRFileBrowserR, kMsgTagLoadedIR, mIRPath_R.GetLength(), mIRPath_R.Get());
      else
        SendControlMsgFromDelegate(kCtrlTagIRFileBrowserR, kMsgTagClearedDisplay);

      mSlotSwitched = false;
    }
  }
}

// UI preferences (1.48.0): a few "key=value" lines in ui.ini, next to the autosave.
void NeuralAmpModeler::_LoadUiPrefs()
{
  WDL_String path;
  INIPath(path, BUNDLE_NAME);
  path.Append("\\ui.ini");
  // 1.54.0: the PRO EQ and Output EQ pages' analyzers start off (to save CPU and GPU) until
  // switched on, and then stay on. The spectrum strip has its own show/hide.
  mProEqView.analyzer = mOutEqView.analyzer = false;
  std::ifstream f(path.Get());
  std::string line;
  while (std::getline(f, line))
  {
    const size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = line.substr(0, eq);
    const int value = std::atoi(line.c_str() + eq + 1);
    if (key == "theme")
      mThemeIndex = std::clamp(value, 0, PluginColors::kNumThemes - 1);
    else if (key == "light")
      mThemeLight = value != 0;
    else if (key == "spectrum")
      mShowSpecStrip = value != 0;
    else if (key == "spectrum_big")
      mSpecStripExpanded = value != 0;
    else if (key == "spectrum_float")
      mSpecFloating = value != 0;
    else if (key == "pro_eq_analyzer")
      mProEqView.analyzer = value != 0;
    else if (key == "out_eq_analyzer")
      mOutEqView.analyzer = value != 0;
    else if (key == "tube_probes_always")
      mTubeProbesAlways = value != 0;
    else if (key == "float_x")
      mSpecFloatRect.L = (float)value;
    else if (key == "float_y")
      mSpecFloatRect.T = (float)value;
    else if (key == "float_w")
      mSpecFloatRect.R = mSpecFloatRect.L + (float)value;
    else if (key == "float_h")
      mSpecFloatRect.B = mSpecFloatRect.T + (float)value;
  }
  PluginColors::ApplyTheme(mThemeIndex, mThemeLight);
}

void NeuralAmpModeler::_SaveUiPrefs() const
{
  WDL_String path;
  INIPath(path, BUNDLE_NAME);
  std::filesystem::create_directories(path.Get());
  path.Append("\\ui.ini");
  std::ofstream f(path.Get(), std::ios::trunc);
  f << "theme=" << mThemeIndex << "\nlight=" << (mThemeLight ? 1 : 0) << "\nspectrum=" << (mShowSpecStrip ? 1 : 0)
    << "\nspectrum_big=" << (mSpecStripExpanded ? 1 : 0) << "\nspectrum_float=" << (mSpecFloating ? 1 : 0)
    << "\nfloat_x=" << (int)mSpecFloatRect.L << "\nfloat_y=" << (int)mSpecFloatRect.T
    << "\nfloat_w=" << (int)mSpecFloatRect.W() << "\nfloat_h=" << (int)mSpecFloatRect.H()
    << "\npro_eq_analyzer=" << (mProEqView.analyzer ? 1 : 0) << "\nout_eq_analyzer=" << (mOutEqView.analyzer ? 1 : 0)
    << "\ntube_probes_always=" << (mTubeProbesAlways ? 1 : 0) << "\n";
}

// Both rebuild the UI on the next OnIdle tick (they're called from inside a control's click).
void NeuralAmpModeler::_SetTheme(int theme, bool light)
{
  mThemeIndex = std::clamp(theme, 0, PluginColors::kNumThemes - 1);
  mThemeLight = light;
  PluginColors::ApplyTheme(mThemeIndex, mThemeLight);
  _SaveUiPrefs();
  mReopenSettingsAfterRelayout = true;
  mLibraryPanelRelayoutPending = true;
}

void NeuralAmpModeler::_SetSpecFloating(bool floating)
{
  mSpecFloating = floating;
  mShowSpecStrip = true;
  _SaveUiPrefs();
  mLibraryPanelRelayoutPending = true;
}

void NeuralAmpModeler::_SetSpecStripExpanded(bool expanded)
{
  mSpecStripExpanded = expanded;
  _SaveUiPrefs();
  mLibraryPanelRelayoutPending = true;
}

void NeuralAmpModeler::_SetSpecStripShown(bool show)
{
  mShowSpecStrip = show;
  if (!show)
    mSpecFloating = false;
  _SaveUiPrefs();
  if (auto* pGraphics = GetUI())
    if (auto* settings = pGraphics->GetControlWithTag(kCtrlTagSettingsBox))
      mReopenSettingsAfterRelayout = !settings->IsHidden();
  mLibraryPanelRelayoutPending = true;
}

#ifdef APP_API
void NeuralAmpModeler::_GetAutosavePath(WDL_String& path) const
{
  INIPath(path, BUNDLE_NAME); // Same folder as settings.ini: %LOCALAPPDATA%\GriffinAmplification (no trailing slash)
  std::filesystem::create_directories(path.Get());
  path.Append("\\autosave.namstate");
}

void NeuralAmpModeler::_GetPresetsDir(WDL_String& path) const
{
  INIPath(path, BUNDLE_NAME);
  path.Append("\\Presets");
  std::filesystem::create_directories(path.Get());
}

void NeuralAmpModeler::_SaveStateToFile(const char* path) const
{
  IByteChunk chunk;
  SerializeState(chunk);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (f.good())
    f.write(reinterpret_cast<const char*>(chunk.GetData()), chunk.Size());
}

bool NeuralAmpModeler::_LoadStateFromFile(const char* path)
{
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.good())
    return false;
  const std::streamsize size = f.tellg();
  if (size <= 0)
    return false;
  f.seekg(0, std::ios::beg);
  std::vector<char> buf(static_cast<size_t>(size));
  if (!f.read(buf.data(), size))
    return false;

  IByteChunk chunk;
  chunk.PutBytes(buf.data(), (int)size);
  UnserializeState(chunk, 0);

  // UnserializeState only updates the IParam objects (and, via OnParamChange, the DSP) -- it
  // doesn't push those new values out to any already-attached UI controls (that normally happens
  // via OnUIOpen when a DAW closes/reopens the editor around a state load, which doesn't happen
  // here since the window stays open the whole time the user is picking a preset file). Mirror
  // what a DAW wrapper does after a state restore: explicitly resync every control.
  if (GetUI())
    OnRestoreState();

  return true;
}

void NeuralAmpModeler::_AutosaveIfDirty()
{
  // Throttle: only actually touch disk every ~90 idle ticks (a few seconds at the ~30Hz OnIdle is
  // typically called at) while something's changed, not on every single knob tick.
  constexpr int kTicksBetweenSaves = 90;
  if (!mStateDirty)
    return;
  if (++mIdleTicksSinceDirty < kTicksBetweenSaves)
    return;

  WDL_String path;
  _GetAutosavePath(path);
  _SaveStateToFile(path.Get());
  mStateDirty = false;
  mIdleTicksSinceDirty = 0;
}
#endif

bool NeuralAmpModeler::SerializeState(IByteChunk& chunk) const
{
  // If this isn't here when unserializing, then we know we're dealing with something before v0.8.0.
  WDL_String header("###NeuralAmpModeler###"); // Don't change this!
  chunk.PutStr(header.Get());
  // Plugin version, so we can load legacy serialized states in the future!
  WDL_String version(PLUG_VERSION_STR);
  chunk.PutStr(version.Get());
  // Model directory (don't serialize the model itself; we'll just load it again
  // when we unserialize)
  chunk.PutStr(mNAMPath.Get());
  chunk.PutStr(mIRPath.Get());
  // Params [0, kNumPositionalParams) positionally, exactly as every 1.x build has always saved them;
  // later params go by name at the end (see kNumPositionalParams).
  bool ok = true;
  for (int i = 0; i < kNumPositionalParams && ok; i++)
  {
    const double v = GetParam(i)->Value();
    ok &= (chunk.Put(&v) > 0);
  }
  // NAM-file pedal paths, appended after the base state (guarded on load for backward compatibility
  // with states saved before this feature existed).
  for (int i = 0; i < kNumNamPedals; i++)
    chunk.PutStr(mNamPedalPath[i].Get());
  // Every param past the positional ones (the Tube Lab's, so far), by name.
  chunk.PutStr("###ExtraParamsV1###");
  const int numExtraParams = kNumParams - kNumPositionalParams;
  chunk.Put(&numExtraParams);
  for (int i = kNumPositionalParams; i < kNumParams; i++)
  {
    chunk.PutStr(GetParam(i)->GetName());
    const double v = GetParam(i)->Value();
    chunk.Put(&v);
  }
  // CAB LAB's IR, by path (like the amp model and cab IR).
  chunk.PutStr("###CabLabIRV1###");
  chunk.PutStr(mCabLabIRPath.Get());
  // CAB 2 / CAB 3's IRs, by path (1.36.0).
  chunk.PutStr("###ExtraCabIRsV1###");
  const int numExtraCabs = kNumExtraCabs;
  chunk.Put(&numExtraCabs);
  for (int k = 0; k < kNumExtraCabs; k++)
    chunk.PutStr(mExtraCabIRPath[k].Get());
  // 1.42.0: the right-channel cab IR (empty: none, the right side follows the cab IR file), and the
  // A/B slot that isn't live -- its index, then its model, cab IR and right-channel IR paths.
  chunk.PutStr("###CabIRRightV1###");
  chunk.PutStr(mIRRExplicit ? mIRPath_R.Get() : "");
  chunk.PutStr("###ABSlotsV1###");
  const int other = 1 - mActiveSlot;
  chunk.Put(&other);
  chunk.PutStr(mSlotNAMPath[other].Get());
  chunk.PutStr(mSlotIRPath[other].Get());
  chunk.PutStr(mSlotIRRExplicit[other] ? mSlotIRPath_R[other].Get() : "");
  // 1.61.0: CAB LAB cab 2's IR, by path (last, so older builds just stop before it).
  chunk.PutStr("###CabLabIR2V1###");
  chunk.PutStr(mCabLabIR2Path.Get());
  // 1.67.0: TUBE LAB 2's capture, by path (last again).
  chunk.PutStr("###TubeLab2CaptureV1###");
  chunk.PutStr(mLab2ModelPath.Get());
  // 1.70.0 PACK PICKER: the pack folder and pick (last, so older builds stop before it). The model
  // itself is still saved as mNAMPath above, so a missing pack folder just loses the dropdown.
  chunk.PutStr("###PackV1###");
  chunk.PutStr((mPackZipPath.empty() ? mPack.folder : mPackZipPath).c_str()); // 1.70.3: a zip pack saves the zip
  chunk.PutStr(mPackGroup.c_str());
  chunk.PutStr(mPackKey.c_str());
  chunk.PutStr(mPackSize.c_str());
  chunk.PutStr(mPackIRChoice.c_str());
  chunk.PutStr(mPackIRSetPath.c_str());
  {
    const int autoIR = mPackAutoIR ? 1 : 0, hasRef = mPackHasRef ? 1 : 0;
    chunk.Put(&autoIR);
    chunk.Put(&hasRef);
    chunk.Put(&mPackRefLoudness);
  }
  return ok;
}

// Like IByteChunk::GetStr, but returns -1 instead of a bogus position when the length prefix is
// negative or runs off the end -- for probing bytes that might not actually be a string.
static int _TryGetStr(const IByteChunk& chunk, WDL_String& str, int pos)
{
  int len = 0;
  if (pos < 0 || chunk.Get(&len, pos) < 0 || len < 0 || pos + static_cast<int>(sizeof(len)) + len > chunk.Size())
    return -1;
  return chunk.GetStr(str, pos);
}

int NeuralAmpModeler::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // Look for the expected header. If it's there, then we'll know what to do.
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);
  // While loading, ABSlot says which slot the state's model belongs to, rather than switching.
  struct UnserializeFlag
  {
    bool& flag;
    explicit UnserializeFlag(bool& f) : flag(f) { flag = true; }
    ~UnserializeFlag() { flag = false; }
  } unserializing(mInUnserialize);

  // A state saved before the chain grid has no grid params; clearing them first means it isn't
  // left with whatever grid was loaded before it (see the migration at the end).
  ENTER_PARAMS_MUTEX
  _ResetChainGridParams();
  LEAVE_PARAMS_MUTEX

  const char* kExpectedHeader = "###NeuralAmpModeler###";
  if (strcmp(header.Get(), kExpectedHeader) == 0)
  {
    pos = _UnserializeStateWithKnownVersion(chunk, pos);
  }
  else
  {
    pos = _UnserializeStateWithUnknownVersion(chunk, startPos);
  }

  // NAM-file pedal paths. Guarded so that states saved before this feature existed still load fine
  // (they simply won't have any pedal captures loaded -- everything else behaves as before).
  if (pos < chunk.Size())
  {
    for (int i = 0; i < kNumNamPedals; i++)
    {
      WDL_String path;
      pos = chunk.GetStr(path, pos);
      if (path.GetLength())
        _StageNamPedal(i, path);
    }
  }

  // Params past the positional ones, by name. Absent in older states, which keep the defaults.
  WDL_String extraHeader;
  int extraPos = _TryGetStr(chunk, extraHeader, pos);
  if (extraPos >= 0 && strcmp(extraHeader.Get(), "###ExtraParamsV1###") == 0)
  {
    int numExtraParams = 0;
    extraPos = chunk.Get(&numExtraParams, extraPos);
    ENTER_PARAMS_MUTEX
    for (int i = 0; i < numExtraParams && extraPos >= 0; i++)
    {
      WDL_String name;
      double v = 0.0;
      extraPos = _TryGetStr(chunk, name, extraPos);
      if (extraPos >= 0)
        extraPos = chunk.Get(&v, extraPos);
      if (extraPos < 0)
        break;
      for (int p = kNumPositionalParams; p < kNumParams; p++)
      {
        if (strcmp(GetParam(p)->GetName(), name.Get()) == 0)
        {
          GetParam(p)->Set(v);
          break;
        }
      }
    }
    // 1.58.0: Width replaced the Mono switch; a state saved with Mono on loads as Width 0% (mono).
    if (GetParam(kOutputMono)->Bool())
    {
      GetParam(kOutputMono)->Set(0.0);
      GetParam(kOutputWidth)->Set(0.0);
    }
    OnParamReset(iplug::EParamSource::kPresetRecall);
    LEAVE_PARAMS_MUTEX
    if (extraPos >= 0)
      pos = extraPos;
  }

  // CAB LAB IR path. Absent in older states.
  WDL_String cabHeader;
  int cabPos = _TryGetStr(chunk, cabHeader, pos);
  if (cabPos >= 0 && strcmp(cabHeader.Get(), "###CabLabIRV1###") == 0)
  {
    WDL_String cabPath;
    cabPos = _TryGetStr(chunk, cabPath, cabPos);
    if (cabPos >= 0)
    {
      pos = cabPos;
      if (cabPath.GetLength())
        _StageCabLabIR(cabPath);
    }
  }

  // CAB 2 / CAB 3's IRs. A state without them (older, or saved with none) clears them, so it
  // doesn't inherit whatever the last state loaded.
  WDL_String extraCabPaths[kNumExtraCabs];
  WDL_String extraCabHeader;
  int extraCabPos = _TryGetStr(chunk, extraCabHeader, pos);
  if (extraCabPos >= 0 && strcmp(extraCabHeader.Get(), "###ExtraCabIRsV1###") == 0)
  {
    int count = 0;
    extraCabPos = chunk.Get(&count, extraCabPos);
    for (int k = 0; k < count && extraCabPos >= 0; k++)
    {
      WDL_String path;
      extraCabPos = _TryGetStr(chunk, path, extraCabPos);
      if (extraCabPos >= 0 && k < kNumExtraCabs)
        extraCabPaths[k] = path;
    }
    if (extraCabPos >= 0)
      pos = extraCabPos;
  }
  for (int k = 0; k < kNumExtraCabs; k++)
    if (!(extraCabPaths[k].GetLength() && _StageExtraCabIR(k, extraCabPaths[k])))
      _StageExtraCabIR(k, WDL_String());

  // 1.42.0: the right-channel cab IR. Absent or empty: none (the right side follows the cab IR).
  WDL_String rightHeader, rightPath;
  int rightPos = _TryGetStr(chunk, rightHeader, pos);
  if (rightPos >= 0 && strcmp(rightHeader.Get(), "###CabIRRightV1###") == 0)
  {
    rightPos = _TryGetStr(chunk, rightPath, rightPos);
    if (rightPos >= 0)
      pos = rightPos;
    else
      rightPath.Set("");
  }
  if (rightPath.GetLength())
    _StageIR_R(rightPath);
  else if (mIRRExplicit || mIRPath_R.GetLength())
    mShouldRemoveIR_R = true;

  // 1.42.0: the A/B slot that isn't live. Absent (an older state): that slot is empty, rather than
  // keeping whatever the last state had there.
  {
    const int live = std::clamp((int)GetParam(kABSlot)->Value(), 0, kNumABSlots - 1);
    WDL_String abHeader, slotNAM, slotIR, slotIRR;
    int abPos = _TryGetStr(chunk, abHeader, pos);
    if (abPos >= 0 && strcmp(abHeader.Get(), "###ABSlotsV1###") == 0)
    {
      int other = 1 - live;
      abPos = chunk.Get(&other, abPos);
      if (abPos >= 0)
        abPos = _TryGetStr(chunk, slotNAM, abPos);
      if (abPos >= 0)
        abPos = _TryGetStr(chunk, slotIR, abPos);
      if (abPos >= 0)
        abPos = _TryGetStr(chunk, slotIRR, abPos);
      if (abPos >= 0)
        pos = abPos;
      else
        slotNAM.Set(""), slotIR.Set(""), slotIRR.Set("");
    }
    _StageInactiveSlot(1 - live, slotNAM, slotIR, slotIRR);
  }

  // 1.61.0: CAB LAB cab 2's IR. Absent (older) or empty: none, rather than the last state's.
  {
    WDL_String header2, path2;
    const int p2 = _TryGetStr(chunk, header2, pos);
    if (p2 >= 0 && strcmp(header2.Get(), "###CabLabIR2V1###") == 0)
    {
      const int p3 = _TryGetStr(chunk, path2, p2);
      if (p3 >= 0)
        pos = p3;
      else
        path2.Set("");
    }
    if (!(path2.GetLength() && _StageCabLabIR2(path2)))
      _StageCabLabIR2(WDL_String());
  }

  // 1.67.0: TUBE LAB 2's capture. Absent (older) or empty: none.
  {
    WDL_String header3, path3;
    const int p2 = _TryGetStr(chunk, header3, pos);
    if (p2 >= 0 && strcmp(header3.Get(), "###TubeLab2CaptureV1###") == 0)
    {
      const int p3 = _TryGetStr(chunk, path3, p2);
      if (p3 >= 0)
        pos = p3;
      else
        path3.Set("");
    }
    if (!(path3.GetLength() && _StageLab2Model(path3).empty()))
    {
      mStagedLab2Model = nullptr;
      mShouldRemoveLab2Model = true;
      mLab2ModelPath.Set("");
      if (auto* pGraphics = GetUI())
        if (auto* label = pGraphics->GetControlWithTag(kCtrlTagLab2CaptureName))
          static_cast<IVLabelControl*>(label)->SetStr("No capture loaded");
    }
  }

  // 1.70.0 PACK PICKER. Absent (older states) or unreadable: no pack, no trim -- exactly as before.
  {
    mPack = griffinpack::Pack();
    mPackGroup.clear();
    mPackKey.clear();
    mPackSize.clear();
    mPackIRChoice.clear();
    mPackIRSetPath.clear();
    mPackAutoIR = true;
    mPackHasRef = false;
    mPackRefLoudness = 0.0;
    mPackModelPtr = nullptr;
    mPackTrimDB = 0.0;
    WDL_String hdr;
    int q = _TryGetStr(chunk, hdr, pos);
    if (q >= 0 && strcmp(hdr.Get(), "###PackV1###") == 0)
    {
      WDL_String s[6];
      bool good = true;
      for (int i = 0; i < 6 && good; i++)
      {
        q = _TryGetStr(chunk, s[i], q);
        good = q >= 0;
      }
      int autoIR = 1, hasRef = 0;
      double ref = 0.0;
      good = good && (q = chunk.Get(&autoIR, q)) >= 0 && (q = chunk.Get(&hasRef, q)) >= 0
             && (q = chunk.Get(&ref, q)) >= 0;
      if (good)
      {
        pos = q;
        mPackAutoIR = autoIR != 0;
        mPackHasRef = hasRef != 0;
        mPackRefLoudness = ref;
        mPackIRChoice = s[4].Get();
        mPackIRSetPath = s[5].Get();
        mPackZipPath.clear();
        if (s[0].GetLength() && griffinpack::IsZipPath(s[0].Get()))
        {
          // 1.70.3: a zip pack: its cache (unpacked again here if it was cleared or the zip changed).
          std::string err, log;
          bool reused = false;
          const std::string dir = griffinpack::ResolveZip(s[0].Get(), err, log, reused);
          _PackLog("preset zip " + std::string(s[0].Get()) + "\n" + log + (err.empty() ? "" : "  error: " + err));
          if (!dir.empty())
          {
            mPack = griffinpack::Scan(dir);
            mPackZipPath = s[0].Get();
            // The saved model was in the cache: if that copy is gone, load the same capture from this one.
            std::error_code ec;
            if (!std::filesystem::exists(std::filesystem::u8path(mNAMPath.Get()), ec))
            {
              const std::string want = griffinpack::U8(std::filesystem::u8path(mNAMPath.Get()).filename());
              for (const auto& c : mPack.captures)
                if (c.file == want)
                {
                  _StageModel(WDL_String(c.path.c_str()));
                  break;
                }
            }
          }
        }
        else if (s[0].GetLength())
          mPack = griffinpack::Scan(s[0].Get()); // Empty if the folder's gone: the saved model still loads
        mPackGroup = s[1].Get();
        mPackKey = s[2].Get();
        mPackSize = s[3].Get();
        // The model was restored from mNAMPath above; if it's this pack's capture, give it its trim.
        const griffinpack::Capture* c = mPack.FindPath(mNAMPath.Get());
        if (c && c->hasLoudness && mPackHasRef)
          mPackTrimDB = mPackRefLoudness - c->loudness;
        if (c)
          mPackModelPtr = mLastStagedModelPtr;
        mPackGainDirty = true;
      }
    }
  }

  if (GetParam(kChainGridV1)->Value() < 0.5)
  {
    ENTER_PARAMS_MUTEX
    _MigrateLegacyChain();
    LEAVE_PARAMS_MUTEX
  }
  return pos;
}

void NeuralAmpModeler::LayoutUI(IGraphics* pGraphics)
{
  NAMDragLog("layout %d x %d", pGraphics->Width(), pGraphics->Height());
  Plugin::LayoutUI(pGraphics); // Runs mLayoutFunc, which starts with RemoveAllControls() + a full rebuild.

  // A freshly-constructed IControl starts at normalized value 0 (see IControl::IControl in
  // IGraphics/IControl.cpp -- it stores {paramIdx, 0.} and nothing more; the constructor can't read
  // GetParam() because it doesn't have a delegate yet). It only shows the real current value once
  // something pushes it there via SendParameterValueFromDelegate, which normally happens through
  // OnUIOpen's SendCurrentParamValuesFromDelegate() call when a host (re)opens the editor.
  //
  // mLayoutFunc's RemoveAllControls() + rebuild also runs on every *in-app* relayout that isn't a
  // fresh editor open -- toggling the AMP NEST library panel (mLibraryPanelRelayoutPending) and any
  // window resize via the corner resizer both call LayoutUI directly, without going through
  // OnUIOpen again. Every one of those was leaving every freshly-rebuilt knob/switch/slider stuck
  // at its 0-normalized position -- indistinguishable, to the eye, from every setting having reset.
  // Explicitly re-syncing here (harmlessly redundant on the very first layout, where OnUIOpen's own
  // call covers it) fixes every relayout trigger at once instead of chasing them one by one.
  SendCurrentParamValuesFromDelegate();
  // Same for the file pickers: rebuilt ones show "Select model..." / "Select IR..." until told
  // what's loaded. (Since 1.23.4 a relayout also happens on every window resize, startup included.)
  _SendLoadedFileNamesToUI();
}

void NeuralAmpModeler::OnUIOpen()
{
  Plugin::OnUIOpen();
  _SendLoadedFileNamesToUI();
}

void NeuralAmpModeler::_SendLoadedFileNamesToUI()
{
  if (mNAMPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
    // If it's not loaded yet, then mark as failed.
    // If it's yet to be loaded, then the completion handler will set us straight once it runs.
    if (mModel == nullptr && mStagedModel == nullptr)
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
  }

  if (mIRPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
    if (mIR == nullptr && mStagedIR == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }
  // 1.42.0: the right-channel IR is saved now, so it's there after a restart.
  if (mIRPath_R.GetLength())
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserR, kMsgTagLoadedIR, mIRPath_R.GetLength(), mIRPath_R.Get());

  if (mModel != nullptr)
  {
    _UpdateControlsFromModel();
  }
}

void NeuralAmpModeler::OnParamChange(int paramIdx)
{
#ifdef APP_API
  // Any param change -- a knob turn, a pedal drag's Slot/Active update, an A/B switch -- means the
  // on-disk autosave is now stale. Flagged here rather than saved immediately so rapid changes
  // (e.g. dragging a knob) don't hit disk on every single tick; see _AutosaveIfDirty in OnIdle.
  mStateDirty = true;
#endif

  switch (paramIdx)
  {
    // Changes to the input gain
    case kCalibrateInput:
    case kInputCalibrationLevel:
    case kInputLevel: _SetInputGain(); break;
    // Changes to the output gain
    case kOutputLevel:
    case kOutputMode: _SetOutputGain(); break;
    // Tone stack:
    case kToneBass: mToneStack->SetParam("bass", GetParam(paramIdx)->Value()); break;
    case kToneMid: mToneStack->SetParam("middle", GetParam(paramIdx)->Value()); break;
    case kToneTreble: mToneStack->SetParam("treble", GetParam(paramIdx)->Value()); break;
    case kSlim: _ApplySlimParamToLoadedNAMs(); break;
    case kABSlot:
    {
      // 1.42.0: the audio thread does the switch (with a fade; see _ProcessABSwitch). Loading a
      // state instead says which slot its model and IR belong to (see mABRelabel).
      const int slot = std::clamp((int)GetParam(kABSlot)->Value(), 0, kNumABSlots - 1);
      mABSlotTarget = slot;
      if (mInUnserialize)
        mABRelabel = slot;
      break;
    }
    default: break;
  }
}

void NeuralAmpModeler::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (auto pGraphics = GetUI())
  {
    bool active = GetParam(paramIdx)->Bool();

    // 1.67.0: a TUBE LAB 2 param acts as the Tube Lab's while its popup is the one shown.
    int which = paramIdx;
    if (const int base = Lab2Base(paramIdx); base >= 0)
      which = mUiLab == 1 ? base : -1000;
    // 1.68.0: a chassis socket given another tube takes that tube's usual bias (a 12AU7 at a 12AX7's
    // -1.5 V would sit nearly saturated).
    {
      const int lab = Lab2Base(paramIdx) >= 0 ? 1 : 0;
      const int base = lab == 1 ? Lab2Base(paramIdx) : paramIdx;
      if (source == EParamSource::kUI && base >= kTubeSockFirst && base <= kTubeSockLast
          && (base - kTubeSockFirst) % kSocketFields == kSockTube)
      {
        const int biasParam = LabParam(lab, base - kSockTube + kSockBias);
        const double normalized = GetParam(biasParam)->ToNormalized(tube::TypicalBias(GetParam(paramIdx)->Int()));
        BeginInformHostOfParamChangeFromUI(biasParam);
        SendParameterValueFromUI(biasParam, normalized);
        EndInformHostOfParamChangeFromUI(biasParam);
        pGraphics->ForControlWithParam(biasParam, [normalized](IControl* c) { c->SetValueFromDelegate(normalized); });
        mChassisSignature = -1.0;
      }
    }
    if (paramIdx == kTempoBPM && source == EParamSource::kUI)
      mTempoPushPending = true;
    switch (which)
    {
      case kNoiseGateActive: pGraphics->GetControlWithParamIdx(kNoiseGateThreshold)->SetDisabled(!active); break;
      case kEQActive:
        pGraphics->ForControlInGroup("EQ_KNOBS", [active](IControl* pControl) { pControl->SetDisabled(!active); });
        break;
      case kIRToggle:
        pGraphics->GetControlWithTag(kCtrlTagIRFileBrowser)->SetDisabled(!active);
        if (mCabButton)
          mCabButton->SetDirty(false); // the CAB chip shows the IR switch as on/off
        break;
      case kAmpEngaged:
        if (mAmpChip)
          mAmpChip->SetDirty(false);
        break;
      case kTubeStack:
      case kTubeRipple:
      case kRealFeelActive:
      case GridCellParam(chaingrid::kRealFeel): _RefreshTubeLabControls(pGraphics); break;
      case kWahModel: // 1.44.0: Sens only acts in Auto
        if (auto* c = pGraphics->GetControlWithParamIdx(kWahSens))
          c->SetDisabled(GetParam(kWahModel)->Int() != pedals::Wah::kAuto);
        break;
      case kTubeCircuit:
      case kTubeSagOn:
      case kTubeStage2:
      case kTubePower:
      case kTubeSpeakerLoad:
        _RefreshTubeLabControls(pGraphics);
        [[fallthrough]];
      case kTubeEngaged:
      case kTubeDrive:
      case kTubeBias:
      case kTubePlate:
      case kTubeGain2:
      case kTubeBias2:
        if (auto* pCurve = pGraphics->GetControlWithTag(kCtrlTagTubeCurve))
          pCurve->As<TubeCurveControl>()->SetDimmed(GetParam(_TP(kTubeEngaged))->Value() <= 0.5);
        break;
      case kCabSource:
      case kMicBOn: _RefreshCabLabControls(pGraphics); break;
      case kCabLab3D:
        _RefreshCabLabControls(pGraphics);
        _SpeakerLabChanged();
        break;
      case kCabFormat:
      {
        // A new cab picked on the page: each mic keeps its spot relative to its cone (a preset load
        // brings its own placements).
        const int format = GetParam(kCabFormat)->Int();
        if (source == EParamSource::kUI && mLabFormatShown >= 0 && format != mLabFormatShown)
          _RemapLabMicsForFormat(0, mLabFormatShown, format);
        mLabFormatShown = format;
        _SpeakerLabChanged();
        break;
      }
      case kCab2Format:
      {
        const int format = GetParam(kCab2Format)->Int();
        if (source == EParamSource::kUI && mLabFormat2Shown >= 0 && format != mLabFormat2Shown)
          _RemapLabMicsForFormat(1, mLabFormat2Shown, format);
        mLabFormat2Shown = format;
        _SpeakerLabChanged();
        break;
      }
      case kCabDual:
        if (GetParam(kCabDual)->Int() == 0)
          mLabSelectedCab = 0;
        else if (source == EParamSource::kUI)
        {
          // Two cabs, but no mic on the second (by default only MIC C is, and it starts off): put one
          // there so cab 2 is heard -- MIC C if it's free, else the first mic that's off -- turned on,
          // just off the dust cap of cab 2's first cone. Its pan stays as it was.
          const cab::CabLabParams cp = _ReadCabLabParams();
          bool heard = false;
          for (int m = 0; m < cab::kNumLabMics; m++)
            heard |= cp.lab[m].on && cp.lab[m].cab == 1;
          if (!heard)
          {
            int pick = !cp.lab[2].on ? 2 : -1;
            for (int m = 0; m < cab::kNumLabMics && pick < 0; m++)
              if (!cp.lab[m].on)
                pick = m;
            if (pick >= 0)
            {
              auto send = [this, pGraphics](int param, double value) {
                const double normalized = GetParam(param)->ToNormalized(value);
                BeginInformHostOfParamChangeFromUI(param);
                SendParameterValueFromUI(param, normalized);
                EndInformHostOfParamChangeFromUI(param);
                pGraphics->ForControlWithParam(param, [normalized](IControl* c) { c->SetValueFromDelegate(normalized); });
              };
              const cab::CabFormat& f2 = cab::GetCabFormat(cp.format2);
              send(kLabMicCabFirst + pick, 1.0);
              send(LabMicParam(pick, 0), 1.0);
              _SetLabMicPosition(pick, f2.cx[0] + 0.035, f2.cy[0], 0.025);
              mLabSelectedMic = pick;
            }
          }
          mLabSelectedCab = 1; // Show the new cab
        }
        _SpeakerLabChanged();
        break;
      default:
        if (paramIdx >= kLabMicCabFirst && paramIdx <= kLabMicCabLast && source == EParamSource::kUI)
        {
          // A mic moved to the other cab: same spot on the matching cone there, and the views follow it.
          const int m = paramIdx - kLabMicCabFirst, to = GetParam(paramIdx)->Int();
          _RemapLabMicsForFormat(to, GetParam(to == 0 ? kCab2Format : kCabFormat)->Int(),
                                 GetParam(to == 0 ? kCabFormat : kCab2Format)->Int(), m);
          if (m == mLabSelectedMic)
            mLabSelectedCab = to;
        }
        if ((paramIdx >= kLabMicFirst && paramIdx <= kLabMicLast) || paramIdx == kCabSpeaker
            || (paramIdx >= kCabDual && paramIdx <= kLabMicCabLast))
          _SpeakerLabChanged();
        break;
    }
  }
}

bool NeuralAmpModeler::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagClearModel: mShouldRemoveModel = true; return true;
    case kMsgTagClearIR: mShouldRemoveIR = true; return true;
    case kMsgTagClearIR_R: mShouldRemoveIR_R = true; return true;
    case kMsgTagHighlightColor:
    {
      mHighLightColor.Set((const char*)pData);

      if (GetUI())
      {
        GetUI()->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            IColor color = IColor::FromColorCodeStr(mHighLightColor.Get());

            pVectorBase->SetColor(kX1, color);
            pVectorBase->SetColor(kPR, color.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, color.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, color.WithContrast(0.1f));
          }
          pControl->GetUI()->SetAllControlsDirty();
        });
      }

      return true;
    }
    default: return false;
  }
}

// Private methods ============================================================

void NeuralAmpModeler::_AllocateIOPointers(const size_t nChans)
{
  if (mInputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mInputPointers without freeing");
  mInputPointers = new sample*[nChans];
  if (mInputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mOutputPointers without freeing");
  mOutputPointers = new sample*[nChans];
  if (mOutputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_ApplyDSPStaging()
{
  // 1.70.0 PACK PICKER: a pack level trim changed (e.g. a preset restored it).
  if (mPackGainDirty.exchange(false))
    _SetOutputGain();
  if (mCabLabIRStaged)
  {
    mCabLabIR = std::move(mStagedCabLabIR);
    mCabLabIRStaged = false;
  }
  if (mCabLabIR2Staged)
  {
    mCabLabIR2 = std::move(mStagedCabLabIR2); // (null clears it)
    mCabLabIR2Staged = false;
  }
  for (int k = 0; k < kNumExtraCabs; k++)
    if (mExtraCabIRStaged[k])
    {
      mExtraCabIR[k] = std::move(mStagedExtraCabIR[k]); // (null clears it)
      mExtraCabIRStaged[k] = false;
    }
  // Remove marked modules
  if (mShouldRemoveModel)
  {
    mModel = nullptr;
    mNAMPath.Set("");
    mShouldRemoveModel = false;
    mModelCleared = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mShouldRemoveIR)
  {
    mIR = nullptr;
    mIRPath.Set("");
    mShouldRemoveIR = false;
    // No left cab means no mirrored right cab either.
    if (!mIRRExplicit)
    {
      mIR_R = nullptr;
      mIRPath_R.Set("");
      mIRStereoPair = false;
    }
  }
  if (mShouldRemoveIR_R)
  {
    mIRRExplicit = false;
    mIRStereoPair = false;
    mIRPath_R.Set("");
    // Fall back to an auto-mirrored copy of the left IR (still independently-stateful) rather
    // than just going null, so the right channel doesn't suddenly lose its cab.
    mIR_R = (mIR != nullptr) ? std::make_unique<FastImpulseResponse>(mIR->GetData(), GetSampleRate()) : nullptr;
    mShouldRemoveIR_R = false;
  }
  // Move things from staged to live
  if (mStagedModel != nullptr)
  {
    mModel = std::move(mStagedModel);
    mStagedModel = nullptr;
    mNewModelLoadedInDSP = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mStagedIR != nullptr)
  {
    mIR = std::move(mStagedIR);
    mStagedIR = nullptr;
    // Keep the right channel in sync unless the user has explicitly loaded their own right-channel
    // IR: a stereo file's own right channel (1.42.0), or else an independently-stateful mirror.
    if (!mIRRExplicit)
    {
      mIRStereoPair = mStagedIRPairR != nullptr;
      mIR_R = mIRStereoPair ? std::move(mStagedIRPairR)
                            : std::make_unique<FastImpulseResponse>(mIR->GetData(), GetSampleRate());
    }
    mStagedIRPairR = nullptr;
  }
  if (mStagedIR_R != nullptr)
  {
    mIR_R = std::move(mStagedIR_R);
    mIRRExplicit = true;
    mIRStereoPair = false;
    mStagedIR_R = nullptr;
  }
  // A state just loaded: its A/B slot is the live one (the model and IR staged above are its), and
  // its other slot goes in place (1.42.0).
  const int relabel = mABRelabel.exchange(-1);
  if (relabel >= 0 && relabel < kNumABSlots)
  {
    mActiveSlot = relabel;
    mABFadeStage = 0;
    mABGain = 1.0;
  }
  if (mSlotStaged)
  {
    const int k = mStagedSlotIndex;
    if (k != mActiveSlot)
    {
      mSlotModel[k] = std::move(mStagedSlotModel);
      mSlotIR[k] = std::move(mStagedSlotIR);
      mSlotIR_R[k] = std::move(mStagedSlotIR_R);
      mSlotNAMPath[k] = mStagedSlotNAMPath;
      mSlotIRPath[k] = mStagedSlotIRPath;
      mSlotIRPath_R[k] = mStagedSlotIRPath_R;
      mSlotIRRExplicit[k] = mStagedSlotIRRExplicit;
      mSlotIRStereoPair[k] = mStagedSlotIRStereoPair;
    }
    mStagedSlotModel = nullptr;
    mStagedSlotIR = nullptr;
    mStagedSlotIR_R = nullptr;
    mSlotStaged = false;
  }
  _ApplyNamPedalDSPStaging();
}

void NeuralAmpModeler::_StageInactiveSlot(int slot, const WDL_String& namPath, const WDL_String& irPath,
                                          const WDL_String& irPathR)
{
  // Built here (UI thread) and handed over whole; the audio thread only moves the pointers.
  std::unique_ptr<ResamplingNAM> model;
  if (namPath.GetLength())
  {
    try
    {
      std::unique_ptr<nam::DSP> dsp = nam::get_dsp(std::filesystem::u8path(namPath.Get()));
      if (dsp->NumInputChannels() == 1 && dsp->NumOutputChannels() == 1)
      {
        model = std::make_unique<ResamplingNAM>(std::move(dsp), GetSampleRate());
        model->Reset(GetSampleRate(), GetBlockSize());
        if (nam::SlimmableModel* slimmable = model->GetSlimmableModel())
          slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
      }
    }
    catch (std::exception& e)
    {
      std::cerr << "Failed to load slot " << slot << "'s model: " << e.what() << std::endl;
    }
  }
  auto loadIR = [this](const WDL_String& path, int channel) -> std::unique_ptr<dsp::ImpulseResponse> {
    if (!path.GetLength())
      return nullptr;
    std::vector<std::vector<float>> channels;
    double fileRate = 48000.0;
    if (dsp::wav::LoadChannels(path.Get(), channels, fileRate) != dsp::wav::LoadReturnCode::SUCCESS
        || (int)channels.size() <= channel)
      return nullptr;
    dsp::ImpulseResponse::IRData data;
    data.mRawAudio = channels[channel];
    data.mRawAudioSampleRate = fileRate;
    return std::make_unique<FastImpulseResponse>(data, GetSampleRate());
  };
  mSlotStaged = false;
  mStagedSlotIndex = slot;
  mStagedSlotModel = std::move(model);
  mStagedSlotNAMPath = mStagedSlotModel ? namPath : WDL_String();
  mStagedSlotIR = loadIR(irPath, 0);
  mStagedSlotIRPath = mStagedSlotIR ? irPath : WDL_String();
  mStagedSlotIRRExplicit = irPathR.GetLength() > 0;
  mStagedSlotIR_R = mStagedSlotIRRExplicit ? loadIR(irPathR, 0) : loadIR(irPath, 1);
  mStagedSlotIRStereoPair = !mStagedSlotIRRExplicit && mStagedSlotIR_R != nullptr;
  if (mStagedSlotIRRExplicit && mStagedSlotIR_R == nullptr)
    mStagedSlotIRRExplicit = false;
  if (mStagedSlotIR_R == nullptr && mStagedSlotIR != nullptr)
    mStagedSlotIR_R = std::make_unique<FastImpulseResponse>(mStagedSlotIR->GetData(), GetSampleRate());
  mStagedSlotIRPath_R = mStagedSlotIRRExplicit ? irPathR : WDL_String();
  mSlotStaged = true;
}

bool NeuralAmpModeler::_ABBeginBlock(const size_t numFrames)
{
  // Audio thread, before the chain: where the A/B switch is (see mABSlotTarget), and the gain this
  // block's AMP and CAB outputs get (into mABGainBuf). Returns whether that gain applies.
  const int want = mABSlotTarget.load();
  if (mABFadeStage == 0 && want != mActiveSlot && want >= 0 && want < kNumABSlots)
  {
    mABFadeStage = mSlotModel[want] != nullptr ? 5 : 1;
    mABFadeCount = 0;
  }
  if (mABFadeStage == 5 && want == mActiveSlot)
    mABFadeStage = 0; // Switched back before anything was heard
  if (mABFadeStage == 0 || mABFadeStage == 5)
    return false;
  if (mABGainBuf.size() < numFrames)
    mABGainBuf.resize(numFrames);
  const int outLen = std::max(1, (int)(kABFadeOutS * GetSampleRate()));
  const int inLen = std::max(1, (int)(kABFadeInS * GetSampleRate()));
  for (size_t s = 0; s < numFrames; s++)
  {
    double g = 1.0;
    if (mABFadeStage == 1)
    {
      g = 1.0 - (double)std::min(++mABFadeCount, outLen) / outLen;
      if (mABFadeCount >= outLen)
        mABFadeStage = 3; // silent for the rest of this block; swap at its end
    }
    else if (mABFadeStage == 3)
      g = 0.0;
    else if (mABFadeStage == 2 || mABFadeStage == 4)
    {
      g = (double)std::min(++mABFadeCount, inLen) / inLen;
      if (mABFadeCount >= inLen)
        mABFadeStage = 4; // done once this block ends
    }
    mABGainBuf[s] = g * g * (3.0 - 2.0 * g);
  }
  return true;
}

void NeuralAmpModeler::_ABEndBlock(const size_t numFrames)
{
  // After the chain: swap at silence, finish, or move on from the pre-roll.
  if (mABFadeStage == 3)
  {
    _SwitchABSlot(mABSlotTarget.load());
    mABFadeStage = 2;
    mABFadeCount = 0;
  }
  else if (mABFadeStage == 4)
    mABFadeStage = 0;
  else if (mABFadeStage == 5 && (mABFadeCount += (int)numFrames) >= (int)(kABPrerollS * GetSampleRate()))
  {
    mABFadeStage = 1;
    mABFadeCount = 0;
  }
}

void NeuralAmpModeler::_SwitchABSlot(int newSlot)
{
  if (newSlot == mActiveSlot || newSlot < 0 || newSlot >= kNumABSlots)
    return;

  // Stash the slot we're leaving
  mSlotModel[mActiveSlot] = std::move(mModel);
  mSlotIR[mActiveSlot] = std::move(mIR);
  mSlotIR_R[mActiveSlot] = std::move(mIR_R);
  mSlotNAMPath[mActiveSlot] = mNAMPath;
  mSlotIRPath[mActiveSlot] = mIRPath;
  mSlotIRPath_R[mActiveSlot] = mIRPath_R;
  mSlotIRRExplicit[mActiveSlot] = mIRRExplicit;
  mSlotIRStereoPair[mActiveSlot] = mIRStereoPair;

  mActiveSlot = newSlot;

  // Bring in the slot we're switching to
  mModel = std::move(mSlotModel[mActiveSlot]);
  mIR = std::move(mSlotIR[mActiveSlot]);
  mIR_R = std::move(mSlotIR_R[mActiveSlot]);
  mNAMPath = mSlotNAMPath[mActiveSlot];
  mIRPath = mSlotIRPath[mActiveSlot];
  mIRPath_R = mSlotIRPath_R[mActiveSlot];
  mIRRExplicit = mSlotIRRExplicit[mActiveSlot];
  mIRStereoPair = mSlotIRStereoPair[mActiveSlot];
  // A cab IR that comes back in still holds what it heard when it was last live: clear it.
  for (auto* ir : {mIR.get(), mIR_R.get()})
    if (auto* fast = dynamic_cast<FastImpulseResponse*>(ir))
      fast->ClearHistory();
  // An IR with no right channel of its own (a slot stashed before it had one) gets a mirror.
  if (mIR != nullptr && mIR_R == nullptr)
    mIR_R = std::make_unique<FastImpulseResponse>(mIR->GetData(), GetSampleRate());

  _UpdateLatency();
  _SetInputGain();
  _SetOutputGain();
  mNewModelLoadedInDSP = true; // Safe no-op in _UpdateControlsFromModel if the new slot is empty.
  mModelCleared = (mModel == nullptr);
  mSlotSwitched = true;
}

void NeuralAmpModeler::_DeallocateIOPointers()
{
  if (mInputPointers != nullptr)
  {
    delete[] mInputPointers;
    mInputPointers = nullptr;
  }
  if (mInputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
  {
    delete[] mOutputPointers;
    mOutputPointers = nullptr;
  }
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels,
                                    const size_t numFrames)
{
  // Copies `inputs` (whatever the pre-amp chain produced), not mInputArray (the raw input): copying
  // the raw input silently threw away every pre-amp pedal -- Tube Lab, OD, Comp, NAM pedals --
  // whenever no amp model was loaded.
  for (auto c = 0; c < numChannels; c++)
    for (auto s = 0; s < numFrames; s++)
      mOutputArray[c][s] = inputs[c][s];
}

void NeuralAmpModeler::_ResetModelAndIR(const double sampleRate, const int maxBlockSize)
{
  // Model
  if (mStagedModel != nullptr)
  {
    mStagedModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mModel != nullptr)
  {
    mModel->Reset(sampleRate, maxBlockSize);
  }

  // IR
  if (mStagedIR != nullptr)
  {
    const double irSampleRate = mStagedIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mStagedIR->GetData();
      mStagedIR = std::make_unique<FastImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIR != nullptr)
  {
    const double irSampleRate = mIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mIR->GetData();
      // 1.42.0: a stereo file's right channel (or an explicit right-channel IR) is rebuilt for the
      // new rate too; it used to be left at the old one (or replaced by a mirror of the left).
      if (mIR_R != nullptr && mIR_R->GetSampleRate() != sampleRate && (mIRStereoPair || mIRRExplicit))
      {
        auto right = std::make_unique<FastImpulseResponse>(mIR_R->GetData(), sampleRate);
        if (mIRRExplicit)
          mStagedIR_R = std::move(right);
        else
          mStagedIRPairR = std::move(right);
      }
      mStagedIR = std::make_unique<FastImpulseResponse>(irData, sampleRate);
    }
  }
  // The A/B slot that isn't live, likewise (it used to come back in at the old rate).
  for (int k = 0; k < kNumABSlots; k++)
  {
    if (mSlotModel[k] != nullptr)
      mSlotModel[k]->Reset(sampleRate, maxBlockSize);
    for (auto* ir : {&mSlotIR[k], &mSlotIR_R[k]})
      if (*ir != nullptr && (*ir)->GetSampleRate() != sampleRate)
        *ir = std::make_unique<FastImpulseResponse>((*ir)->GetData(), sampleRate);
  }
}

void NeuralAmpModeler::_SetInputGain()
{
  iplug::sample inputGainDB = GetParam(kInputLevel)->Value();
  // Input calibration
  if ((mModel != nullptr) && (mModel->HasInputLevel()) && GetParam(kCalibrateInput)->Bool())
  {
    inputGainDB += GetParam(kInputCalibrationLevel)->Value() - mModel->GetInputLevel();
  }
  mInputGain = DBToAmp(inputGainDB);
}

void NeuralAmpModeler::_SetOutputGain()
{
  double gainDB = GetParam(kOutputLevel)->Value();
  if (mModel != nullptr)
  {
    const int outputMode = GetParam(kOutputMode)->Int();
    switch (outputMode)
    {
      case 1: // Normalized
        if (mModel->HasLoudness())
        {
          const double loudness = mModel->GetLoudness();
          const double targetLoudness = -18.0;
          gainDB += (targetLoudness - loudness);
        }
        break;
      case 2: // Calibrated
        if (mModel->HasOutputLevel())
        {
          const double inputLevel = GetParam(kInputCalibrationLevel)->Value();
          const double outputLevel = mModel->GetOutputLevel();
          gainDB += (outputLevel - inputLevel);
        }
        break;
      case 0: // Raw
        // 1.70.0 PACK PICKER: a pack capture is level-matched to the first one picked in its pack (from
        // the .nam loudness), so switching captures keeps the volume. Any other model: no change.
        if (mModel.get() == mPackModelPtr.load())
          gainDB += mPackTrimDB.load();
        break;
      default: break;
    }
  }
  mOutputGain = DBToAmp(gainDB);
}

void NeuralAmpModeler::_ApplySlimParamToLoadedNAMs()
{
  const double v = GetParam(kSlim)->Value();
  auto apply = [v](ResamplingNAM* p) {
    if (p == nullptr)
      return;
    if (nam::SlimmableModel* s = p->GetSlimmableModel())
      s->SetSlimmableSize(v);
  };
  apply(mModel.get());
  apply(mStagedModel.get());
}

std::string NeuralAmpModeler::_StageModel(const WDL_String& modelPath)
{
  WDL_String previousNAMPath = mNAMPath;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);

    // Check that the model has 1 input and 1 output channel
    if (model->NumInputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 input channel, but has " + std::to_string(model->NumInputChannels()));
    }
    if (model->NumOutputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 output channel, but has "
                               + std::to_string(model->NumOutputChannels()));
    }

    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
    {
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    }
    // 1.70.0 PACK PICKER: which model this is (identity only), so only a pack capture gets its level trim.
    mLastStagedModelPtr = temp.get();
    if (mPackStaging)
      mPackModelPtr = temp.get();
    mStagedModel = std::move(temp);
    mNAMPath = modelPath;
#ifdef APP_API
    mStateDirty = true;
#endif
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIR(const WDL_String& irPath)
{
  // FIXME it'd be better for the path to be "staged" as well. Just in case the
  // path and the model got caught on opposite sides of the fence...
  WDL_String previousIRPath = mIRPath;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    // 1.42.0: a stereo file is a stereo cab: its left channel on the left and its right channel on
    // the right (unless a right-channel IR is loaded explicitly, which still wins). A mono file, as
    // before, plays on both sides.
    std::vector<std::vector<float>> channels;
    double fileRate = 48000.0;
    wavState = dsp::wav::LoadChannels(irPath.Get(), channels, fileRate);
    mStagedIRPairR = nullptr;
    if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
    {
      dsp::ImpulseResponse::IRData left;
      left.mRawAudio = channels[0];
      left.mRawAudioSampleRate = fileRate;
      auto leftIR = std::make_unique<FastImpulseResponse>(left, sampleRate);
      if (channels.size() > 1)
      {
        dsp::ImpulseResponse::IRData right;
        right.mRawAudio = channels[1];
        right.mRawAudioSampleRate = fileRate;
        mStagedIRPairR = std::make_unique<FastImpulseResponse>(right, sampleRate);
      }
      // The right channel first: the audio thread takes the pair when it sees mStagedIR.
      mStagedIR = std::move(leftIR);
    }
  }
  catch (std::exception& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPath = irPath;
#ifdef APP_API
    mStateDirty = true;
#endif
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadedIR, mIRPath.GetLength(), mIRPath.Get());
  }
  else
  {
    if (mStagedIR != nullptr)
    {
      mStagedIR = nullptr;
    }
    mStagedIRPairR = nullptr;
    mIRPath = previousIRPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowser, kMsgTagLoadFailed);
  }

  return wavState;
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIR_R(const WDL_String& irPath)
{
  WDL_String previousIRPath = mIRPath_R;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    mStagedIR_R = std::make_unique<FastImpulseResponse>(irPath.Get(), sampleRate);
    wavState = mStagedIR_R->GetWavState();
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load right-channel IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPath_R = irPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserR, kMsgTagLoadedIR, mIRPath_R.GetLength(), mIRPath_R.Get());
  }
  else
  {
    if (mStagedIR_R != nullptr)
    {
      mStagedIR_R = nullptr;
    }
    mIRPath_R = previousIRPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserR, kMsgTagLoadFailed);
  }

  return wavState;
}

size_t NeuralAmpModeler::_GetBufferNumChannels() const
{
  // Assumes input=output (no mono->stereo effects)
  return mInputArray.size();
}

size_t NeuralAmpModeler::_GetBufferNumFrames() const
{
  if (_GetBufferNumChannels() == 0)
    return 0;
  return mInputArray[0].size();
}

void NeuralAmpModeler::_InitToneStack()
{
  // If you want to customize the tone stack, then put it here!
  mToneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
}
void NeuralAmpModeler::_PrepareBuffers(const size_t numChannels, const size_t numFrames)
{
  const bool updateChannels = numChannels != _GetBufferNumChannels();
  const bool updateFrames = updateChannels || (_GetBufferNumFrames() != numFrames);
  //  if (!updateChannels && !updateFrames)  // Could we do this?
  //    return;

  if (updateChannels)
  {
    _PrepareIOPointers(numChannels);
    mInputArray.resize(numChannels);
    mOutputArray.resize(numChannels);
  }
  if (updateFrames)
  {
    for (auto c = 0; c < mInputArray.size(); c++)
    {
      mInputArray[c].resize(numFrames);
      std::fill(mInputArray[c].begin(), mInputArray[c].end(), 0.0);
    }
    for (auto c = 0; c < mOutputArray.size(); c++)
    {
      mOutputArray[c].resize(numFrames);
      std::fill(mOutputArray[c].begin(), mOutputArray[c].end(), 0.0);
    }
  }
  // Would these ever get changed by something?
  for (auto c = 0; c < mInputArray.size(); c++)
    mInputPointers[c] = mInputArray[c].data();
  for (auto c = 0; c < mOutputArray.size(); c++)
    mOutputPointers[c] = mOutputArray[c].data();
}

void NeuralAmpModeler::_PrepareIOPointers(const size_t numChannels)
{
  _DeallocateIOPointers();
  _AllocateIOPointers(numChannels);
}

void NeuralAmpModeler::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                     const size_t nChansOut)
{
  // We'll assume that the main processing is mono for now. We'll handle dual amps later.
  if (nChansOut != 1)
  {
    std::stringstream ss;
    ss << "Expected mono output, but " << nChansOut << " output channels are requested!";
    throw std::runtime_error(ss.str());
  }

  // On the standalone, we can probably assume that the user has plugged into only one input and they expect it to be
  // carried straight through. Don't apply any division over nChansIn because we're just "catching anything out there."
  // However, in a DAW, it's probably something providing stereo, and we want to take the average in order to avoid
  // doubling the loudness. (This would change w/ double mono processing)
  double gain = mInputGain;
#ifndef APP_API
  gain /= (float)nChansIn;
#endif
  // 1.42.0: ramped across the block from the gain last applied, so a jump (automation, MIDI, a
  // model's input calibration) doesn't step the level between two samples.
  const double from = mInputGainApplied < 0.0 ? gain : mInputGainApplied;
  mInputGainApplied = gain;
  // Assume _PrepareBuffers() was already called
  for (size_t c = 0; c < nChansIn; c++)
    for (size_t s = 0; s < nFrames; s++)
    {
      const double g = from == gain ? gain : from + (gain - from) * (double)(s + 1) / (double)nFrames;
      if (c == 0)
        mInputArray[0][s] = g * inputs[c][s];
      else
        mInputArray[0][s] += g * inputs[c][s];
    }
  if (nChansIn == 0)
    for (size_t s = 0; s < nFrames; s++)
      mInputArray[0][s] = 0.0;
}

// Standalone only: a gentle safety saturator on the output, so overs round off instead of being
// chopped flat (a hard clip is what makes digital overload sound so harsh). Up to 0.9 (-0.9 dBFS)
// it's untouched; above that it bends smoothly toward 1.0 (same slope at the knee, so no corner)
// and never reaches it. No lookahead, so no added latency. The clip warning still shows the over.
static inline double _SafetySoftClip(double x)
{
  constexpr double knee = 0.9;
  const double a = std::fabs(x);
  if (a <= knee)
    return x;
  const double y = knee + (1.0 - knee) * std::tanh((a - knee) / (1.0 - knee));
  return x < 0.0 ? -y : y;
}

void NeuralAmpModeler::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansIn, const size_t nChansOut)
{
  // 1.42.0: Master Out (and Output mode) ramp across the block from the gain last applied, so a jump
  // doesn't step the level between two samples.
  const double gainTo = mOutputGain;
  const double gainFrom = mOutputGainApplied < 0.0 ? gainTo : mOutputGainApplied;
  mOutputGainApplied = gainTo;
  auto gainAt = [gainFrom, gainTo, nFrames](size_t s) {
    return gainFrom == gainTo ? gainTo : gainFrom + (gainTo - gainFrom) * (double)(s + 1) / (double)nFrames;
  };
  // Assume _PrepareBuffers() was already called
  if (nChansIn != 1 && nChansIn != 2)
    throw std::runtime_error("Plugin is supposed to process in mono or true stereo.");
  // Expression (volume) pedal: a one-pole glide (~10 ms) evaluated per block, ramped linearly
  // across the block so pedal moves don't zipper.
  const double volStart = mExprVolume;
  const double volTarget = mExprVolumeTarget.load(std::memory_order_relaxed);
  const double glide = 1.0 - std::exp(-(double)nFrames / (0.01 * GetSampleRate()));
  const double volEnd = volStart + (volTarget - volStart) * glide;
  mExprVolume = std::fabs(volEnd - volTarget) < 1e-6 ? volTarget : volEnd;
  const double volStep = nFrames > 0 ? (mExprVolume - volStart) / (double)nFrames : 0.0;
  // Mono input: broadcast to all output channels (legacy/back-compat path).
  // Stereo input: channel 0->L outputs, channel 1->R outputs (channel-for-channel up to
  // min(nChansIn, nChansOut)); if there are more outputs than input channels, the last input
  // channel is broadcast to the extras.
  bool clipped = false;
  // Output limiter: follows its switch through a short fade out and back in (switching it moves the
  // output by its lookahead, which would click), and when it's in the path, the gained signal goes
  // through it before the soft clip below.
  const bool limiterWanted = GetParam(kOutputLimiter)->Bool();
  mLimiter.SetParams(GetParam(kLimiterCeiling)->Value(), GetParam(kLimiterRelease)->Value());
  // The OUTPUT page's gain-reduction meter: the most reduction since it last looked.
  auto showGR = [this]() {
    if (!mOutputPageOpen.load(std::memory_order_relaxed))
      return;
    const float gr = mLimiterOn ? (float)mLimiter.BlockGainReductionDB() : 0.f;
    if (gr < mLimiterGRShown.load(std::memory_order_relaxed))
      mLimiterGRShown.store(gr, std::memory_order_relaxed);
  };
  if (limiterWanted != mLimiterOn || mLimiterFadeStage != 0)
  {
    if (mLimiterBuf[0].size() < nFrames)
      for (auto& b : mLimiterBuf)
        b.resize(nFrames);
    const int fade = std::max(1, static_cast<int>(0.005 * GetSampleRate()));
    double* lr[2] = {mLimiterBuf[0].data(), mLimiterBuf[1].data()};
    for (size_t c = 0; c < 2; c++)
    {
      const size_t cin = std::min(c, nChansIn - 1);
      for (size_t s = 0; s < nFrames; s++)
        lr[c][s] = gainAt(s) * (volStart + volStep * (s + 1)) * inputs[cin][s];
    }
    if (mLimiterOn)
    {
      mLimiter.Process(lr[0], lr[1], (int)nFrames);
      showGR();
    }
    for (size_t s = 0; s < nFrames; s++)
    {
      if (mLimiterFadeStage == 0 && limiterWanted != mLimiterOn)
      {
        mLimiterFadeStage = 1;
        mLimiterFadeCount = 0;
      }
      double g = 1.0;
      if (mLimiterFadeStage == 1)
      {
        g = 1.0 - (double)(++mLimiterFadeCount) / fade;
        if (mLimiterFadeCount >= fade)
          mLimiterFadeStage = 3; // Silent for the rest of this block; switch at its end
      }
      else if (mLimiterFadeStage == 3)
        g = 0.0;
      else if (mLimiterFadeStage == 2)
      {
        g = (double)(++mLimiterFadeCount) / fade;
        if (mLimiterFadeCount >= fade)
          mLimiterFadeStage = 0;
      }
      g = g * g * (3.0 - 2.0 * g);
      lr[0][s] *= g;
      lr[1][s] *= g;
    }
    if (mLimiterFadeStage == 3)
    {
      // Faded out: switch between blocks, so no block mixes the delayed and undelayed signal. The
      // limiter starts with an empty delay line, so its first output is silence, then fades in.
      mLimiterOn = limiterWanted;
      mLimiter.Reset(GetSampleRate());
      mLimiterFadeStage = 2;
      mLimiterFadeCount = 0;
    }
    for (auto cout = 0; cout < nChansOut; cout++)
    {
      const double* src = lr[std::min((size_t)cout, (size_t)1)];
      for (auto s = 0; s < nFrames; s++)
      {
        const double v = src[s];
        clipped |= std::fabs(v) > 1.0;
#ifdef APP_API
        outputs[cout][s] = _SafetySoftClip(v);
#else
        outputs[cout][s] = v;
#endif
      }
    }
    if (clipped)
      mOutputClipped.store(true, std::memory_order_relaxed);
    return;
  }
  if (mLimiterOn)
  {
    if (mLimiterBuf[0].size() < nFrames)
      for (auto& b : mLimiterBuf)
        b.resize(nFrames);
    double* lr[2] = {mLimiterBuf[0].data(), mLimiterBuf[1].data()};
    for (size_t c = 0; c < 2; c++)
    {
      const size_t cin = std::min(c, nChansIn - 1);
      for (size_t s = 0; s < nFrames; s++)
        lr[c][s] = gainAt(s) * (volStart + volStep * (s + 1)) * inputs[cin][s];
    }
    mLimiter.Process(lr[0], lr[1], (int)nFrames);
    showGR();
    for (auto cout = 0; cout < nChansOut; cout++)
    {
      const double* src = lr[std::min((size_t)cout, (size_t)1)];
      for (auto s = 0; s < nFrames; s++)
      {
        const double v = src[s];
        clipped |= std::fabs(v) > 1.0;
#ifdef APP_API
        outputs[cout][s] = _SafetySoftClip(v);
#else
        outputs[cout][s] = v;
#endif
      }
    }
    if (clipped)
      mOutputClipped.store(true, std::memory_order_relaxed);
    return;
  }
  for (auto cout = 0; cout < nChansOut; cout++)
  {
    const size_t cin = std::min((size_t)cout, nChansIn - 1);
    for (auto s = 0; s < nFrames; s++)
    {
      const double v = gainAt(s) * (volStart + volStep * (s + 1)) * inputs[cin][s];
      clipped |= std::fabs(v) > 1.0;
#ifdef APP_API // Ensure valid output to interface
      outputs[cout][s] = _SafetySoftClip(v);
#else // In a DAW, other things may come next and should be able to handle large
      // values.
      outputs[cout][s] = v;
#endif
    }
  }
  if (clipped)
    mOutputClipped.store(true, std::memory_order_relaxed);
}

void NeuralAmpModeler::_UpdateControlsFromModel()
{
  if (mModel == nullptr)
  {
    return;
  }
  if (auto* pGraphics = GetUI())
  {
    ModelInfo modelInfo;
    modelInfo.sampleRate.known = true;
    modelInfo.sampleRate.value = mModel->GetEncapsulatedSampleRate();
    modelInfo.inputCalibrationLevel.known = mModel->HasInputLevel();
    modelInfo.inputCalibrationLevel.value = mModel->HasInputLevel() ? mModel->GetInputLevel() : 0.0;
    modelInfo.outputCalibrationLevel.known = mModel->HasOutputLevel();
    modelInfo.outputCalibrationLevel.value = mModel->HasOutputLevel() ? mModel->GetOutputLevel() : 0.0;

    static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->SetModelInfo(modelInfo);

    const bool disableInputCalibrationControls = !mModel->HasInputLevel();
    pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetDisabled(disableInputCalibrationControls);
    pGraphics->GetControlWithTag(kCtrlTagInputCalibrationLevel)->SetDisabled(disableInputCalibrationControls);
    {
      auto* c = static_cast<OutputModeControl*>(pGraphics->GetControlWithTag(kCtrlTagOutputMode));
      c->SetNormalizedDisable(!mModel->HasLoudness());
      c->SetCalibratedDisable(!mModel->HasOutputLevel());
    }

    if (auto* pSlimIcon = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
    {
      // It lives in the amp popup, so it only shows while that's open.
      const bool show = mModel->GetSlimmableModel() != nullptr && mSelectedBlock == kAmpBlockId;
      pSlimIcon->Hide(!show);
    }
  }
}

void NeuralAmpModeler::_UpdateLatency()
{
  // The chain grid's path latency (into Output), measured by the last ProcessBlock -- parallel
  // paths are aligned to the longest one where they merge, so it's the longest path, not a sum.
  const int latency = _TotalLatency(); // The grid's, plus the output limiter's lookahead when it's on

  // Feels weird to have to do this.
  if (GetLatency() != latency)
  {
    SetLatency(latency);
  }
}

void NeuralAmpModeler::_UpdateMeters(sample** inputPointer, sample** outputPointer, const size_t nFrames,
                                     const size_t nChansIn, const size_t nChansOut)
{
  // Input: mono. Output: L and R (the main pair; the FX loop's send channels come after them).
  // With only one output connected, both meters show it.
  mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, 1);
  sample* outLR[2] = {outputPointer[0], nChansOut > 1 ? outputPointer[1] : outputPointer[0]};
  mOutputSender.ProcessBlock(outLR, (int)nFrames, kCtrlTagOutputMeter, 2);
}

// NAM-file pedals ==============================================================

std::string NeuralAmpModeler::_StageNamPedal(int slot, const WDL_String& modelPath)
{
  if (slot < 0 || slot >= kNumNamPedals)
    return "Invalid pedal slot";

  WDL_String previousPath = mNamPedalPath[slot];
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);

    if (model->NumInputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 input channel, but has " + std::to_string(model->NumInputChannels()));
    }
    if (model->NumOutputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 output channel, but has "
                               + std::to_string(model->NumOutputChannels()));
    }

    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
    {
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    }
    mStagedNamPedalModel[slot] = std::move(temp);
    mNamPedalPath[slot] = modelPath;
#ifdef APP_API
    mStateDirty = true;
#endif
  }
  catch (std::runtime_error& e)
  {
    mStagedNamPedalModel[slot] = nullptr;
    mNamPedalPath[slot] = previousPath;
    std::cerr << "Failed to load pedal model for slot " << slot << ": " << e.what() << std::endl;
    return e.what();
  }
  return "";
}

// 1.67.0: TUBE LAB 2's capture (mono in and out, like the AMP block's), staged for the audio thread.
std::string NeuralAmpModeler::_StageLab2Model(const WDL_String& path)
{
  try
  {
    std::unique_ptr<nam::DSP> model = nam::get_dsp(std::filesystem::u8path(path.Get()));
    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
      throw std::runtime_error("The capture must have 1 input and 1 output channel.");
    auto temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    mStagedLab2Model = std::move(temp);
    mLab2ModelPath = path;
#ifdef APP_API
    mStateDirty = true;
#endif
  }
  catch (std::exception& e)
  {
    std::cerr << "Failed to load TUBE LAB 2's capture: " << e.what() << std::endl;
    return e.what();
  }
  if (auto* pGraphics = GetUI())
    if (auto* label = pGraphics->GetControlWithTag(kCtrlTagLab2CaptureName))
      static_cast<IVLabelControl*>(label)->SetStr(mLab2ModelPath.get_filepart());
  return "";
}

void NeuralAmpModeler::_RemoveNamPedal(int slot)
{
  if (slot < 0 || slot >= kNumNamPedals)
    return;
  mShouldRemoveNamPedal[slot] = true;
}

void NeuralAmpModeler::_ApplyNamPedalDSPStaging()
{
  bool changed = false;
  for (int i = 0; i < kNumNamPedals; i++)
  {
    if (mShouldRemoveNamPedal[i])
    {
      mNamPedalModel[i] = nullptr;
      mNamPedalPath[i].Set("");
      mShouldRemoveNamPedal[i] = false;
      changed = true;
    }
    if (mStagedNamPedalModel[i] != nullptr)
    {
      mNamPedalModel[i] = std::move(mStagedNamPedalModel[i]);
      mStagedNamPedalModel[i] = nullptr;
      changed = true;
    }
  }
  // 1.67.0: TUBE LAB 2's capture, the same way.
  if (mShouldRemoveLab2Model)
  {
    mLab2Model = nullptr;
    mShouldRemoveLab2Model = false;
    changed = true;
  }
  if (mStagedLab2Model != nullptr)
  {
    mLab2Model = std::move(mStagedLab2Model);
    mStagedLab2Model = nullptr;
    changed = true;
  }
  if (changed)
    _UpdateLatency();
}

void NeuralAmpModeler::_ResetNamPedals(const double sampleRate, const int maxBlockSize)
{
  for (int i = 0; i < kNumNamPedals; i++)
  {
    if (mStagedNamPedalModel[i] != nullptr)
      mStagedNamPedalModel[i]->Reset(sampleRate, maxBlockSize);
    else if (mNamPedalModel[i] != nullptr)
      mNamPedalModel[i]->Reset(sampleRate, maxBlockSize);
  }
}

int NeuralAmpModeler::_NamPedalLatency() const
{
  int latency = 0;
  for (int i = 0; i < kNumNamPedals; i++)
    if (mNamPedalModel[i])
      latency += mNamPedalModel[i]->GetLatency();
  return latency;
}

// Signal-chain grid ===========================================================

chaingrid::Grid NeuralAmpModeler::_ReadChainGrid() const
{
  chaingrid::Grid g;
  for (int b = 0; b < chaingrid::kNumIds; b++)
  {
    if (b == chaingrid::kInput)
      continue;
    const int c = (int)std::round(GetParam(GridCellParam(b))->Value());
    const bool inChain = !chaingrid::IsPedal(b) || GetParam(PedalOf(b).activeParam)->Value() > 0.5;
    g.cell[b] = (inChain && c >= 0 && c < chaingrid::kNumCells) ? c : -1;
    g.in[b] = (chaingrid::Mask)std::llround(GetParam(GridInParam(b))->Value());
  }
  g.out = (chaingrid::Mask)std::llround(GetParam(kGridOut)->Value());
  return g;
}

void NeuralAmpModeler::_WriteChainGrid(const chaingrid::Grid& grid)
{
  auto send = [this](int paramIdx, double v) {
    IParam* p = GetParam(paramIdx);
    if (p->Value() != v)
      SendParameterValueFromUI(paramIdx, p->ToNormalized(v));
  };
  for (int b = 0; b < chaingrid::kNumIds; b++)
  {
    if (b == chaingrid::kInput)
      continue;
    send(GridCellParam(b), grid.cell[b]);
    send(GridInParam(b), grid.in[b]);
  }
  send(kGridOut, grid.out);
  send(kChainGridV1, 1.0);
}

void NeuralAmpModeler::_ResetChainGridParams()
{
  GetParam(kChainGridV1)->Set(0.0);
  // Not grid params, but a state saved before they existed must load with their defaults (amp on,
  // no Tube Lab tone stack), not with whatever the previous state left.
  GetParam(kAmpEngaged)->Set(1.0);
  GetParam(kTubeStack)->Set(0.0);
  GetParam(kTubeBass)->Set(5.0);
  GetParam(kTubeMid)->Set(5.0);
  GetParam(kTubeTreble)->Set(5.0);
  for (int param : kTubeRealFeelParams)
    GetParam(param)->Set(0.0);
  GetParam(kTubeFeelDepth)->Set(tube::TubeParams().feelDepth);
  GetParam(kRealFeelActive)->Set(0.0); // 1.34.0: older states never had the REAL FEEL pedal
  GetParam(kOutputLimiter)->Set(0.0); // 1.37.0: nor the output limiter
  for (int p0 : {kCab2Active, kCab3Active}) // 1.36.0: nor CAB 2 / CAB 3
  {
    GetParam(p0)->Set(0.0);
    GetParam(p0 + 1)->Set(1.0);
    GetParam(p0 + 2)->Set(0.0);
    GetParam(p0 + 3)->Set(0.0);
  }
  GetParam(kTubeEconomy)->Set(0.0); // 1.35.0: older states ran at full quality
  GetParam(kTubeTouch)->Set(0.0); // 1.38.0: nor had Touch
  GetParam(kTubeRippleAmount)->Set(tube::TubeParams().rippleAmount); // 1.39.0 (60 Hz is off, above)
  GetParam(kRealFeelMix)->Set(100.0);
  GetParam(kTubeRippleMix)->Set(100.0);
  GetParam(kHumFilter)->Set(0.0);
  for (int p = kNewPedalParamsFirst; p <= kNewPedalParamsLast; p++) // 1.44.0: older states never had them
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p : kTubeCircuitPlusParams) // 1.45.0: nor Circuit+
    GetParam(p)->Set(GetParam(p)->GetDefault());
  GetParam(kTubeHighQuality)->Set(0.0); // Nor 8x
  for (int p = kProEqActive; p <= kProEqBandLast; p++) // 1.46.0: nor PRO EQ or the Output EQ
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p = kOutEqOn; p <= kOutEqBandLast; p++)
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p = kProEqTrim; p <= kOutEqBandExtLast; p++) // 1.47.0: nor Trim, band types or slopes
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p = kDynamicsParamsFirst; p <= kDynamicsParamsLast; p++) // 1.52.0: nor GATE or PRO COMP
    GetParam(p)->Set(GetParam(p)->GetDefault());
  GetParam(kOutputMono)->Set(0.0); // 1.55.0: nor Mono
  for (int p : kTubeBenchParams) // 1.58.0: nor the bench or the OUTPUT page's controls
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p : kOutputPageParams)
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p = kCabFormat; p <= kLabMicLast; p++) // 1.59.0: nor the SPEAKER LAB: their CAB LAB stays classic
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p = kCabDual; p <= kLabMicCabLast; p++) // 1.61.0: nor dual cabs (single)
    GetParam(p)->Set(GetParam(p)->GetDefault());
  GetParam(kTubeCanFail)->Set(0.0); // 1.62.0: nor failing tubes
  GetParam(kTubeCapture)->Set(0.0); // 1.64.0: nor the capture in the Tube Lab
  GetParam(kLab2Active)->Set(0.0); // 1.67.0: nor TUBE LAB 2
  for (int p = kLab2First; p <= kLab2Last; p++)
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p = kTubeChassisFirst; p <= kLab2ChassisLast; p++) // 1.68.0: nor the chassis (empty sockets, a pair)
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p = kMorePedalParamsFirst; p <= kMorePedalParamsLast; p++) // 1.66.0: nor the new gate, delays, reverb
    GetParam(p)->Set(GetParam(p)->GetDefault());
  for (int p = kTempoParamsFirst; p <= kTempoParamsLast; p++) // 1.71.0: nor tap tempo
    GetParam(p)->Set(GetParam(p)->GetDefault());
  GetParam(kCabLab3D)->Set(0.0);
  for (int b = 0; b < chaingrid::kNumIds; b++)
  {
    if (b == chaingrid::kInput)
      continue;
    GetParam(GridCellParam(b))->Set(-1.0);
    GetParam(GridInParam(b))->Set(0.0);
  }
  GetParam(kGridOut)->Set(0.0);
}

void NeuralAmpModeler::_MigrateLegacyChain()
{
  // The pre-1.17 order, exactly as its ProcessBlock built it: every block in the chain, stably
  // sorted by Slot (ties keep this array order), with AMP after everything at or below its slot.
  struct Entry
  {
    int slot;
    int block;
  };
  std::vector<Entry> entries;
  for (int p = 0; p < kNumPedals; p++)
    if (GetParam(kPedals[p].activeParam)->Value() > 0.5)
      entries.push_back({(int)std::round(GetParam(kPedals[p].slotParam)->Value()), p});
  entries.push_back({(int)std::round(GetParam(kCabSlot)->Value()), chaingrid::kCab});
  entries.push_back({(int)std::round(GetParam(kLoopSendSlot)->Value()), chaingrid::kLoopSend});
  entries.push_back({(int)std::round(GetParam(kLoopReturnSlot)->Value()), chaingrid::kLoopReturn});
  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.slot < b.slot; });
  const int ampSlot = (int)std::round(GetParam(kAmpSlot)->Value());
  std::vector<int> order;
  bool ampPlaced = false;
  for (const auto& e : entries)
  {
    if (!ampPlaced && e.slot > ampSlot)
    {
      order.push_back(chaingrid::kAmp);
      ampPlaced = true;
    }
    order.push_back(e.block);
  }
  if (!ampPlaced)
    order.push_back(chaingrid::kAmp);

  const chaingrid::Grid g = chaingrid::FromSerialOrder(order.data(), (int)order.size());
  for (int b = 0; b < chaingrid::kNumIds; b++)
  {
    if (b == chaingrid::kInput)
      continue;
    GetParam(GridCellParam(b))->Set(g.cell[b]);
    GetParam(GridInParam(b))->Set(g.in[b]);
  }
  GetParam(kGridOut)->Set(g.out);
  GetParam(kChainGridV1)->Set(1.0);
}

void NeuralAmpModeler::_PrepareGridBuffers(const size_t numFrames)
{
  for (int n = 0; n <= chaingrid::kNumIds; n++)
    for (int c = 0; c < 2; c++)
    {
      if (mGridMix[n][c].size() < numFrames)
        mGridMix[n][c].resize(numFrames);
      mGridMixPtrs[n][c] = mGridMix[n][c].data();
    }
  for (int n = 0; n < chaingrid::kNumIds; n++)
    for (int c = 0; c < 2; c++)
    {
      if (mGridRing[n][c].size() != (size_t)kGridRingSize)
        mGridRing[n][c].assign(kGridRingSize, 0.0);
      if (mBlockFadeOut[n][c].size() < numFrames)
        mBlockFadeOut[n][c].resize(numFrames);
      mBlockFadeOutPtrs[n][c] = mBlockFadeOut[n][c].data();
    }
  if (mBlockFadeGain.size() < numFrames)
    mBlockFadeGain.resize(numFrames);
  for (int c = 0; c < 2; c++)
  {
    if (mBlockFadeDry[c].size() < numFrames)
      mBlockFadeDry[c].resize(numFrames);
    if (mABPrerollBuf[c].size() < numFrames)
      mABPrerollBuf[c].resize(numFrames);
    if (mShelfDry[c].size() < numFrames)
      mShelfDry[c].resize(numFrames);
    if (mLoopReturnBuf[c].size() < numFrames)
      mLoopReturnBuf[c].resize(numFrames);
    mLoopReturnPtrs[c] = mLoopReturnBuf[c].data();
    if (mRealFeelBuf[c].size() < numFrames)
      mRealFeelBuf[c].resize(numFrames);
    mRealFeelPtrs[c] = mRealFeelBuf[c].data();
    for (int k = 0; k < kNumExtraCabs; k++)
    {
      if (mExtraCabOut[k][c].size() < numFrames)
        mExtraCabOut[k][c].resize(numFrames);
      mExtraCabPtrs[k][c] = mExtraCabOut[k][c].data();
    }
  }
  for (int k = 0; k < kNumExtraCabs; k++)
    if (mExtraCabMono[k].size() < numFrames)
      mExtraCabMono[k].resize(numFrames);
}

void NeuralAmpModeler::_ApplyTubePreset(int index)
{
  if (index < 0 || index >= kNumTubePresets)
    return;
  IGraphics* pGraphics = GetUI();
  auto send = [this, pGraphics](int baseParam, double value) {
    const int param = _TP(baseParam); // 1.67.0: into the lab being shown
    const double normalized = GetParam(param)->ToNormalized(value);
    BeginInformHostOfParamChangeFromUI(param);
    SendParameterValueFromUI(param, normalized);
    EndInformHostOfParamChangeFromUI(param);
    if (pGraphics)
      pGraphics->ForControlWithParam(param, [normalized](IControl* c) { c->SetValueFromDelegate(normalized); });
  };
  // The presets were voiced and level-matched without the Real Feel switches, Touch or Circuit+.
  for (int param : kTubeRealFeelParams)
    send(param, 0.0);
  for (int param : kTubeCircuitPlusParams)
    send(param, 0.0);
  send(kTubeTouch, 0.0);
  // 1.63.0: and on a stock bench (new tubes, 120 V, bias at 100%): a preset on worn tubes at 90 V
  // isn't the sound it was voiced as.
  for (int param : kTubeBenchParams)
    send(param, GetParam(param)->GetDefault());
  // 1.68.0: and with the chassis sockets empty, on a pair of power tubes.
  for (int k = 0; k < tube::kNumSockets; k++)
    send(SocketParam(k, kSockRole), tube::kSocketOff);
  send(kTubeQuad, 0.0);
  for (const auto& v : kTubePresets[index].values)
    send(v.param, v.value);
  if (pGraphics)
  {
    _RefreshTubeLabControls(pGraphics);
    pGraphics->SetAllControlsDirty();
  }
}

void NeuralAmpModeler::_UpdateClipIndicator()
{
  const auto now = std::chrono::steady_clock::now();
  if (mOutputClipped.exchange(false, std::memory_order_relaxed))
    mClipShownUntil = now + std::chrono::milliseconds(kClipHoldMs);
  const bool show = now < mClipShownUntil;
  if (show == mClipShown)
    return;
  mClipShown = show;
  if (auto* pGraphics = GetUI())
    if (auto* label = pGraphics->GetControlWithTag(kCtrlTagOutputMeterLabel))
    {
      auto* l = label->As<IVLabelControl>();
      l->SetStyle(DEFAULT_STYLE
                    .WithValueText(show ? IText(12, COLOR_RED, "Roboto-Regular")
                                        : IText(11, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.6f), "Roboto-Regular"))
                    .WithDrawFrame(false));
      l->SetStr(show ? "CLIP" : "OUT");
      l->SetDirty(false);
    }
}

// 1.67.0: the Tube Lab popup, debug page and chassis page now edit lab 0 (TUBE LAB) or 1 (TUBE LAB 2).
void NeuralAmpModeler::_SetUiLab(int lab)
{
  if (lab == mUiLab)
    return;
  mUiLab = lab;
  mUiLabAudio.store(lab, std::memory_order_relaxed);
  for (auto& [control, base] : mLabRetarget)
  {
    const int idx = LabParam(lab, base);
    control->SetParamIdx(idx);
    control->SetValueFromDelegate(GetParam(idx)->GetNormalized());
  }
  mChassisSignature = -1.0;
  mTubeHaveSignalStats = false; // The debug page's "last thing played" was the other lab's
  if (auto* pGraphics = GetUI())
  {
    _RefreshTubeLabControls(pGraphics);
    if (auto* pCurve = pGraphics->GetControlWithTag(kCtrlTagTubeCurve))
      pCurve->As<TubeCurveControl>()->SetDimmed(GetParam(_TP(kTubeEngaged))->Value() <= 0.5);
    pGraphics->SetAllControlsDirty();
  }
}

void NeuralAmpModeler::_ApplyTubeTab()
{
  const bool open = _TubeLabSelected();
  for (auto& [control, tab] : mTubeTabControls)
    if (control)
      control->Hide(!open || tab != mTubeTab);
  if (auto* pGraphics = GetUI())
    pGraphics->SetAllControlsDirty();
}

void NeuralAmpModeler::_RefreshTubeLabControls(IGraphics* pGraphics)
{
  if (pGraphics == nullptr)
    return;
  const bool circuit = GetParam(_TP(kTubeCircuit))->Bool();
  // {control's param, up to two switches it also depends on (-1: none)}; everything here also
  // needs the Circuit engine.
  struct Rule
  {
    int param, needs1, needs2;
  };
  const std::array<Rule, 29> rules = {{{kTubeCathodeCap, -1, -1},
                                       {kTubeQuad, kTubePower, -1},
                                       {kTubeCathodeFollower, -1, -1},
                                       {kTubeCathodeBias, kTubePower, -1},
                                       {kTubeSpeakerFeedback, kTubePower, -1},
                                       {kTubeResonance, kTubePower, -1},
                                       {kTubePlate, -1, -1},
                                       {kTubePlateCap, -1, -1},
                                       {kTubeSagOn, -1, -1},
                                       {kTubeSagSupply, kTubeSagOn, -1},
                                       {kTubeSagTime, kTubeSagOn, -1},
                                       {kTubeStage2, -1, -1},
                                       {kTubeGain2, kTubeStage2, -1},
                                       {kTubeBias2, kTubeStage2, -1},
                                       {kTubeCap2, kTubeStage2, -1},
                                       {kTubePlateCap2, kTubeStage2, -1},
                                       {kTubeCouple, kTubeStage2, -1},
                                       {kTubePower, -1, -1},
                                       {kTubeMaster, kTubePower, -1},
                                       {kTubePowerSag, kTubePower, -1},
                                       {kTubeFeedback, kTubePower, -1},
                                       {kTubePresence, kTubePower, -1},
                                       {kTube6L6, kTubePower, -1},
                                       {kTubeSpeakerLoad, kTubePower, -1},
                                       {kTubeScreenSquish, kTubePower, -1},
                                       {kTubeSharedSupply, kTubePower, -1},
                                       {kTubeOTSaturation, kTubePower, -1},
                                       {kTubeCoilHeat, kTubePower, kTubeSpeakerLoad},
                                       {kTubeFeelDepth, kTubePower, -1}}};
  auto on = [this](int param) { return param < 0 || GetParam(_TP(param))->Bool(); };
  // With the REAL FEEL pedal in the chain, the Real Feel controls act there, whatever the Tube Lab's
  // engine and Power Amp are doing.
  const bool feelPedal = mUiLab == 0 && _RealFeelPedalPlaced(); // (REAL FEEL is TUBE LAB's)
  auto isFeel = [](int param) {
    return param == kTubeScreenSquish || param == kTubeSharedSupply || param == kTubeOTSaturation
           || param == kTubeCoilHeat || param == kTubeFeelDepth;
  };
  for (const auto& r : rules)
    if (auto* pControl = pGraphics->GetControlWithParamIdx(_TP(r.param)))
      pControl->SetDisabled(!(feelPedal && isFeel(r.param)) && (!circuit || !on(r.needs1) || !on(r.needs2)));
  // The tone stack works in both engines; its knobs are greyed while it's Off.
  const bool stackOn = GetParam(_TP(kTubeStack))->Int() > 0;
  for (int param : {kTubeBass, kTubeMid, kTubeTreble})
    if (auto* pControl = pGraphics->GetControlWithParamIdx(_TP(param)))
      pControl->SetDisabled(!stackOn);
  // 60 Hz works in both engines, with or without the power amp; Amount is greyed while it's off.
  for (int param : {kTubeRippleAmount, kTubeRippleMix})
    if (auto* pControl = pGraphics->GetControlWithParamIdx(_TP(param)))
      pControl->SetDisabled(!GetParam(_TP(kTubeRipple))->Bool());
}

cab::CabLabParams NeuralAmpModeler::_ReadCabLabParams() const
{
  cab::CabLabParams cp;
  cp.useIR = GetParam(kCabSource)->Bool();
  cp.speaker = GetParam(kCabSpeaker)->Int();
  auto micFrom = [this](int on, int type, int pos, int dist, int level, int pan, int invert) {
    cab::MicParams m;
    m.on = on < 0 || GetParam(on)->Bool();
    m.type = GetParam(type)->Int();
    m.position = GetParam(pos)->Value() / 10.0;
    m.distanceM = GetParam(dist)->Value() / 100.0;
    m.levelDB = GetParam(level)->Value();
    m.pan = GetParam(pan)->Value() / 100.0;
    m.invert = invert >= 0 && GetParam(invert)->Bool();
    return m;
  };
  cp.micA = micFrom(-1, kMicAType, kMicAPos, kMicADist, kMicALevel, kMicAPan, -1);
  cp.micB = micFrom(kMicBOn, kMicBType, kMicBPos, kMicBDist, kMicBLevel, kMicBPan, kMicBInvert);
  cp.roomLevelDB = GetParam(kRoomLevel)->Value();
  cp.roomSize = GetParam(kRoomSize)->Value() / 10.0;
  cp.width = GetParam(kRoomWidth)->Value() / 100.0;
  cp.outputDB = GetParam(kCabOutput)->Value();
  cp.lab3D = GetParam(kCabLab3D)->Bool();
  cp.format = std::clamp(GetParam(kCabFormat)->Int(), 0, cab::kNumCabFormats - 1);
  for (int m = 0; m < cab::kNumLabMics; m++)
  {
    cab::LabMic& lm = cp.lab[m];
    lm.on = GetParam(LabMicParam(m, 0))->Bool();
    lm.type = std::clamp(GetParam(LabMicParam(m, 1))->Int(), 0, cab::kNumMics - 1);
    lm.x = GetParam(LabMicParam(m, 2))->Value() / 100.0;
    lm.y = GetParam(LabMicParam(m, 3))->Value() / 100.0;
    lm.z = GetParam(LabMicParam(m, 4))->Value() / 100.0;
    lm.levelDB = GetParam(LabMicParam(m, 5))->Value();
    lm.pan = GetParam(LabMicParam(m, 6))->Value() / 100.0;
    lm.invert = GetParam(LabMicParam(m, 7))->Bool();
    lm.cab = std::clamp(GetParam(kLabMicCabFirst + m)->Int(), 0, 1);
  }
  cp.dual = GetParam(kCabDual)->Int() == 1;
  cp.format2 = std::clamp(GetParam(kCab2Format)->Int(), 0, cab::kNumCabFormats - 1);
  cp.speaker2 = GetParam(kCab2Speaker)->Int();
  cp.useIR2 = GetParam(kCab2SourceIR)->Bool();
  return cp;
}

void NeuralAmpModeler::_RefreshCabLabControls(IGraphics* pGraphics)
{
  if (pGraphics == nullptr)
    return;
  if (auto* c = pGraphics->GetControlWithParamIdx(kCabSpeaker))
    c->SetDisabled(GetParam(kCabSource)->Bool());
  // 1.59.0: in the SPEAKER LAB's 3D mode the classic Mic A / Mic B knobs do nothing.
  const bool lab3D = GetParam(kCabLab3D)->Bool();
  const bool micB = GetParam(kMicBOn)->Bool();
  for (int param : {kMicAType, kMicAPos, kMicADist, kMicALevel, kMicAPan, kMicBOn})
    pGraphics->ForControlWithParam(param, [lab3D](IControl* c) { c->SetDisabled(lab3D); });
  for (int param : {kMicBType, kMicBPos, kMicBDist, kMicBLevel, kMicBPan, kMicBInvert})
    pGraphics->ForControlWithParam(param, [lab3D, micB](IControl* c) { c->SetDisabled(lab3D || !micB); });
}

bool NeuralAmpModeler::_StageExtraCabIR(const int cab, const WDL_String& path)
{
  std::unique_ptr<dsp::ImpulseResponse> ir;
  if (path.GetLength())
  {
    try
    {
      ir = std::make_unique<FastImpulseResponse>(path.Get(), GetSampleRate());
    }
    catch (std::exception& e)
    {
      std::cerr << "Failed to load CAB " << cab + 2 << " IR: " << e.what() << std::endl;
      return false;
    }
    if (ir->GetWavState() != dsp::wav::LoadReturnCode::SUCCESS)
      return false;
  }
  // Hand over to the audio thread (an empty path hands over nothing: no IR).
  mExtraCabIRStaged[cab] = false;
  mStagedExtraCabIR[cab] = std::move(ir);
  mExtraCabIRStaged[cab] = true;
  mExtraCabIRPath[cab] = path;
#ifdef APP_API
  mStateDirty = true;
#endif
  if (auto* pGraphics = GetUI())
    if (auto* label = pGraphics->GetControlWithTag(cab == 0 ? kCtrlTagCab2IRName : kCtrlTagCab3IRName))
      static_cast<IVLabelControl*>(label)->SetStr(path.GetLength() ? mExtraCabIRPath[cab].get_filepart()
                                                                   : "No IR loaded (passes the signal through)");
  return true;
}

bool NeuralAmpModeler::_StageCabLabIR(const WDL_String& path)
{
  std::unique_ptr<dsp::ImpulseResponse> ir;
  try
  {
    ir = std::make_unique<FastImpulseResponse>(path.Get(), GetSampleRate());
  }
  catch (std::exception& e)
  {
    std::cerr << "Failed to load CAB LAB IR: " << e.what() << std::endl;
    return false;
  }
  if (ir->GetWavState() != dsp::wav::LoadReturnCode::SUCCESS)
    return false;
  // Hand over to the audio thread (if it hasn't picked up a previous one yet, replace that).
  mCabLabIRStaged = false;
  mStagedCabLabIR = std::move(ir);
  mCabLabIRStaged = true;
  mCabLabIRPath = path;
#ifdef APP_API
  mStateDirty = true;
#endif
  if (auto* pGraphics = GetUI())
    for (int tag : {(int)kCtrlTagCabLabIRName, (int)kCtrlTagCabLabIR1Name})
      if (auto* label = pGraphics->GetControlWithTag(tag))
        static_cast<IVLabelControl*>(label)->SetStr(mCabLabIRPath.get_filepart());
  return true;
}

bool NeuralAmpModeler::_StageCabLabIR2(const WDL_String& path)
{
  std::unique_ptr<dsp::ImpulseResponse> ir;
  if (path.GetLength())
  {
    try
    {
      ir = std::make_unique<FastImpulseResponse>(path.Get(), GetSampleRate());
    }
    catch (std::exception& e)
    {
      std::cerr << "Failed to load CAB LAB cab 2 IR: " << e.what() << std::endl;
      return false;
    }
    if (ir->GetWavState() != dsp::wav::LoadReturnCode::SUCCESS)
      return false;
  }
  mCabLabIR2Staged = false;
  mStagedCabLabIR2 = std::move(ir);
  mCabLabIR2Staged = true;
  mCabLabIR2Path = path;
#ifdef APP_API
  mStateDirty = true;
#endif
  if (auto* pGraphics = GetUI())
    if (auto* label = pGraphics->GetControlWithTag(kCtrlTagCabLabIR2Name))
      static_cast<IVLabelControl*>(label)->SetStr(path.GetLength() ? mCabLabIR2Path.get_filepart() : "No IR loaded");
  return true;
}

IRECT NeuralAmpModeler::_ComputeTubePopupCardArea(const IRECT& winBounds) const
{
  // Kept above the IN/OUT meter strip along the bottom (the meters stay on top of every popup but
  // the Amp's), so the tall card never ends up underneath them.
  const float left = winBounds.L + 20.f, right = winBounds.R - 20.f;
  const float top = winBounds.T + 8.f;
  float bottom = mInputMeterHomeRect.H() > 0.f ? mInputMeterHomeRect.T - 8.f : winBounds.B - 8.f;
  if (mSpecStripSmallRect.H() > 0.f) // and above the spectrum strip (1.50.0)
    bottom = std::min(bottom, mSpecStripSmallRect.T - 8.f);
  // 820 (was 620, 1.38.1): the Tube Lab's knobs put their name and value beside the dial, so width
  // is what they use.
  const float w = std::max(0.f, std::min(right - left, 820.f));
  const float h = std::max(0.f, std::min(bottom - top, 860.f));
  const float cx = (left + right) * 0.5f;
  const float cy = (top + bottom) * 0.5f;
  return IRECT(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
}

IRECT NeuralAmpModeler::_ComputeWidePopupCardArea(const IRECT& winBounds) const
{
  // The 1.44.0 pedals: a plain pedal's card, wide enough for up to seven controls in a row (1.66.0: eight,
  // for TRACK GATE).
  return _LiftAboveStrip(
    winBounds.GetCentredInside(std::min(winBounds.W() - 40.f, 740.f), std::min(winBounds.H() * 0.55f, 280.f)), winBounds);
}

IRECT NeuralAmpModeler::_ComputeDynamicsPopupCardArea(const IRECT& winBounds) const
{
  // GATE / PRO COMP (1.52.0): wider and taller than the 1.44.0 pedals' cards: a meter and two rows.
  return _LiftAboveStrip(
    winBounds.GetCentredInside(std::min(winBounds.W() - 40.f, 780.f), std::min(winBounds.H() * 0.62f, 340.f)), winBounds);
}

IRECT NeuralAmpModeler::_ComputePedalPopupCardArea(const IRECT& winBounds) const
{
  return _LiftAboveStrip(
    winBounds.GetCentredInside(std::min(winBounds.W() * 0.5f, 360.f), std::min(winBounds.H() * 0.55f, 280.f)), winBounds);
}

// Moves a pedal's card up so it ends above the spectrum strip (1.48.0), but not off the top.
IRECT NeuralAmpModeler::_LiftAboveStrip(const IRECT& card, const IRECT& winBounds) const
{
  if (mSpecStripRect.H() <= 0.f)
    return card;
  const float limit = mSpecStripRect.T - 8.f;
  if (card.B <= limit)
    return card;
  const float top = std::max(winBounds.T + 8.f, card.T - (card.B - limit));
  return card.GetTranslated(0.f, top - card.T);
}

IRECT NeuralAmpModeler::_ComputeAmpPopupCardArea(const IRECT& winBounds) const
{
  // std::clamp is UB if lo > hi -- guard against a window narrower/shorter than the minimum.
  const float w = std::clamp(mAmpPopupWidth, kAmpPopupMinWidth, std::max(kAmpPopupMinWidth, winBounds.W() - 16.f));
  // 1.70.2: +40 for the PACK dropdown's row under the model dropdown.
  const float h = std::clamp(mAmpPopupHeight + 40.f, kAmpPopupMinHeight, std::max(kAmpPopupMinHeight, winBounds.H() - 16.f));
  const IRECT centered = winBounds.GetCentredInside(w, h);
  // Biased down from dead-center: the routing rack (AMP/pedal chips) sits in the upper portion of
  // the main page behind this popup, and pure centering ignores that, leaving the card's top edge
  // butted right against (or overlapping) the rack with no visual breathing room. Push it down a
  // bit, clamped so the bottom never runs past the window.
  const float downBias = winBounds.H() * 0.1f;
  const float maxTop = winBounds.B - h;
  const float top = std::min(centered.T + downBias, maxTop);
  IRECT card(centered.L, top, centered.R, top + h);
  // 1.50.0: ends above the spectrum strip (its small size), moving up and then shrinking if it must.
  if (mSpecStripSmallRect.H() > 0.f)
  {
    const float limit = mSpecStripSmallRect.T - 8.f;
    if (card.B > limit)
    {
      const float t = std::max(winBounds.T + 8.f, limit - card.H());
      card = IRECT(card.L, t, card.R, std::max(t + 100.f, limit));
    }
  }
  return card;
}

void NeuralAmpModeler::_SelectBlock(int blockId)
{
  // Pedals (0..kNumPedals-1), the fixed CAB block (kCabBlockId), and now the AMP block
  // (kAmpBlockId) each get a popup, sharing the same card/backdrop/title/close chrome (resized
  // per-block below -- the Amp's is much bigger, since it hosts all 8 knobs plus the model/IR
  // loaders and toggles, not just a couple of knobs). -1 means no popup open.
  auto isPopupBlock = [](int id) {
    return chaingrid::IsPedal(id) || id == kCabBlockId || id == kAmpBlockId || id == kLoopBlockId;
  };
  const bool wasOpen = isPopupBlock(mSelectedBlock);
  if (blockId == kTubePedalIdx || blockId == chaingrid::kTubeLab2)
    _SetUiLab(blockId == kTubePedalIdx ? 0 : 1);
  // 1.67.0: TUBE LAB 2 shows the Tube Lab's controls (now bound to its params).
  const int shownId = blockId == chaingrid::kTubeLab2 ? kTubePedalIdx : blockId;
  mSelectedBlock = blockId;
  const bool popupOpen = isPopupBlock(blockId);
  const bool isAmp = blockId == kAmpBlockId;
  // True only on the actual open transition (a fresh click), not when re-applying selection state
  // after a relayout/resize -- see the AnimateGrowFrom call below for why this matters.
  const bool justOpened = popupOpen && !wasOpen;
  // 1.68.0: opening TUBE LAB or TUBE LAB 2 opens its chassis first (its X shows the knobs underneath).
  if (justOpened && (blockId == kTubePedalIdx || blockId == chaingrid::kTubeLab2))
    if (IGraphics* pGraphics = GetUI())
      if (auto* page = pGraphics->GetControlWithTag(kCtrlTagTubeChassis))
        page->Hide(false);

  // Detail controls (knobs/switch) show immediately, in step with the card/backdrop -- they used
  // to wait for AnimateGrowFrom's completion callback (deferred until the card finished zooming
  // in, so knobs wouldn't briefly appear "floating" outside a still-small card), but that made
  // them depend on the animation actually reaching completion. It doesn't reliably: this app's
  // event loop can deliver a second, closing selection before a 160ms animation finishes (e.g. via
  // the platform's own post-mouse-up hover-refresh re-hitting the backdrop that just appeared over
  // the same point), which left the completion callback permanently unfired -- and the knobs
  // permanently hidden. Showing them immediately trades a few frames of "knobs slightly ahead of
  // the card" cosmetic overlap for the popup actually working every time.
  for (int i = 0; i < chaingrid::kNumIds; i++)
    for (auto* c : mPedalDetailControls[i])
      if (c)
        c->Hide(shownId != i);
  for (int lab = 0; lab < 2; lab++)
    for (auto* c : mLabOnly[lab])
      if (c)
        c->Hide(shownId != kTubePedalIdx || mUiLab != lab);
  for (auto* c : mCabDetailControls)
    if (c)
      c->Hide(blockId != kCabBlockId);
  for (auto* c : mAmpDetailControls)
    if (c)
      c->Hide(blockId != kAmpBlockId);
  for (auto* c : mLoopDetailControls)
    if (c)
      c->Hide(blockId != kLoopBlockId);
  _ApplyTubeTab(); // Of the Tube Lab's pages, only the open tab's

  // The shared card/close-button/title-label chrome is repositioned to a much bigger target rect
  // when the Amp is the one opening -- same three controls, different bounds -- rather than
  // building the Amp a whole second set of popup chrome. Recomputed from GetBounds() rather than
  // cached from mLayoutFunc since this is a separate member function; the amp case is computed by
  // the shared _ComputeAmpPopupCardArea (also used by mLayoutFunc) so the two can't drift apart.
  if (auto* pGraphics = GetUI())
  {
    const IRECT winBounds = pGraphics->GetBounds();
    const IRECT cardArea = isAmp                      ? _ComputeAmpPopupCardArea(winBounds)
                           : (shownId == kTubePedalIdx || blockId == kCabLabPedalIdx || blockId == chaingrid::kProEq)
                             ? _ComputeTubePopupCardArea(winBounds)
                           : _IsDynamicsPedal(blockId) ? _ComputeDynamicsPopupCardArea(winBounds)
                           : _IsWidePedal(blockId) ? _ComputeWidePopupCardArea(winBounds)
                                                      : _ComputePedalPopupCardArea(winBounds);
    // Every card now ends above the strip, which stays live on top. With a big card open (Amp, Tube
    // Lab, CAB LAB, PRO EQ) an expanded strip drops to its small size to leave them room.
    const bool bigCard = popupOpen && (isAmp || shownId == kTubePedalIdx || blockId == kCabLabPedalIdx
                                       || blockId == chaingrid::kProEq);
    for (auto& s : mSpecStripCtls)
      s.control->SetTargetAndDrawRECTs(bigCard ? s.shrunk : s.full);
    pGraphics->SetAllControlsDirty();
    const IRECT headerArea = cardArea.GetFromTop(32.f).GetPadded(-6.f);
    const IRECT closeArea = headerArea.GetFromRight(22.f);
    const IRECT labelArea = headerArea.GetReducedFromRight(28.f);
    // SetTargetAndDrawRECTs, not SetRECT: SetRECT only moves where a control draws, and its click
    // target stays where it was built (the small pedal card). The X on the bigger AMP / TUBE LAB /
    // CAB LAB cards was drawn in the right place but clicks there went to the backdrop (1.37.1).
    if (mBlockPopupCardBg)
      mBlockPopupCardBg->SetTargetAndDrawRECTs(cardArea);
    if (mBlockPopupCloseButton)
      mBlockPopupCloseButton->SetTargetAndDrawRECTs(closeArea);
    if (mBlockPopupTitleLabel)
      mBlockPopupTitleLabel->SetTargetAndDrawRECTs(labelArea);
    // The Amp's popup is much bigger and more modal-feeling than a pedal's small popover, so it
    // gets a stronger dim behind it -- makes the routing rack read as clearly "behind" instead of
    // competing visually with the popup sitting close in front of it.
    if (mBlockPopupBackdrop)
      static_cast<NAMSlimOverlayBackdropControl*>(mBlockPopupBackdrop)->SetDimOpacity(isAmp ? 0.6f : 0.22f);

    // Carry the IN/OUT meters (and their captions) into the Amp popup, flanking the card right
    // next to the Input/Output knobs they correspond to, instead of leaving them stranded out at
    // the window's outer edges while the popup covers the middle. Put back at their normal
    // main-page position whenever anything other than the Amp is selected (including nothing).
    constexpr float kMeterW = 30.f, kMeterH = 100.f;
    // Slid back inside the window (which can mean overlapping the card's own edge slightly,
    // rather than going off-screen) if the popup's been resized wide enough that there's no room
    // outside it -- the meters are attached above the card in z-order regardless, so an overlap
    // still renders correctly, just snug against the card instead of clear of it.
    auto clampToWindow = [&winBounds](IRECT r) {
      if (r.L < winBounds.L)
        r = r.GetTranslated(winBounds.L - r.L, 0.f);
      if (r.R > winBounds.R)
        r = r.GetTranslated(winBounds.R - r.R, 0.f);
      return r;
    };
    const IRECT popupInputMeterRect = clampToWindow(
      IRECT(cardArea.L - kMeterW - 10.f, cardArea.MH() - kMeterH * 0.5f, cardArea.L - 10.f, cardArea.MH() + kMeterH * 0.5f));
    const IRECT popupOutputMeterRect = clampToWindow(IRECT(
      cardArea.R + 10.f, cardArea.MH() - kMeterH * 0.5f, cardArea.R + kMeterW + 10.f, cardArea.MH() + kMeterH * 0.5f));
    auto meterCaptionFor = [](const IRECT& meter) {
      return IRECT(meter.L - 6.f, meter.B + 2.f, meter.R + 6.f, meter.B + 16.f);
    };
    if (auto* inputMeter = pGraphics->GetControlWithTag(kCtrlTagInputMeter))
      inputMeter->SetTargetAndDrawRECTs(isAmp ? popupInputMeterRect : mInputMeterHomeRect);
    if (auto* outputMeter = pGraphics->GetControlWithTag(kCtrlTagOutputMeter))
      outputMeter->SetTargetAndDrawRECTs(isAmp ? popupOutputMeterRect : mOutputMeterHomeRect);
    if (auto* inputMeterLabel = pGraphics->GetControlWithTag(kCtrlTagInputMeterLabel))
      inputMeterLabel->SetTargetAndDrawRECTs(isAmp ? meterCaptionFor(popupInputMeterRect) : mInputMeterLabelHomeRect);
    if (auto* outputMeterLabel = pGraphics->GetControlWithTag(kCtrlTagOutputMeterLabel))
      outputMeterLabel->SetTargetAndDrawRECTs(isAmp ? meterCaptionFor(popupOutputMeterRect) : mOutputMeterLabelHomeRect);
  }

  if (mBlockPopupTitleLabel)
  {
    if (popupOpen)
      static_cast<IVLabelControl*>(mBlockPopupTitleLabel)
        ->SetStr(blockId == kCabBlockId    ? "CAB"
                 : blockId == kLoopBlockId ? "FX LOOP"
                 : isAmp                   ? "AMP"
                                           : PedalOf(blockId).name);
    mBlockPopupTitleLabel->Hide(!popupOpen);
  }
  if (mBlockHintLabel)
    mBlockHintLabel->Hide(popupOpen);
  if (mLibraryToggleButton)
    mLibraryToggleButton->Hide(popupOpen);
  if (mBlockPopupBackdrop)
    mBlockPopupBackdrop->Hide(!popupOpen);
  if (mBlockPopupCardBg)
    mBlockPopupCardBg->Hide(!popupOpen);
  if (mBlockPopupCloseButton)
    mBlockPopupCloseButton->Hide(!popupOpen);

  // Zoom the card in from the clicked block's own position, and fade the dim in alongside it,
  // rather than having the popup and a full-strength dim both snap in instantly -- this is what
  // makes it read as a zoom on that one block rather than a full-screen takeover. Purely cosmetic
  // now (see above) -- if this animation gets interrupted for any reason, the popup and its
  // contents are already fully shown regardless.
  if (justOpened)
  {
    IControl* originControl = blockId == kCabBlockId    ? mCabButton
                              : blockId == kLoopBlockId ? mLoopSendChip
                              : isAmp                   ? mAmpChip
                                                        : mPedalChips[blockId];
    if (mBlockPopupCardBg && originControl)
      static_cast<NAMBorderedPanelControl*>(mBlockPopupCardBg)->AnimateGrowFrom(originControl->GetRECT());
    if (mBlockPopupBackdrop)
      static_cast<NAMSlimOverlayBackdropControl*>(mBlockPopupBackdrop)->AnimateFadeIn();
  }

  // 1.67.1: CAB LAB opens into the SPEAKER LAB.
  if (justOpened && blockId == kCabLabPedalIdx)
    _ShowSpeakerLab(true);

  if (auto* pGraphics = GetUI())
    pGraphics->SetAllControlsDirty();
}

void NeuralAmpModeler::_ShowSpeakerLab(bool show)
{
  // The page is what you hear: 3D on while it's the one shown, classic on the card.
  if (GetParam(kCabLab3D)->Bool() != show)
  {
    const double v = show ? 1.0 : 0.0;
    BeginInformHostOfParamChangeFromUI(kCabLab3D);
    SendParameterValueFromUI(kCabLab3D, v);
    EndInformHostOfParamChangeFromUI(kCabLab3D);
    if (auto* ui = GetUI())
      ui->ForControlWithParam(kCabLab3D, [v](IControl* c) { c->SetValueFromDelegate(v); });
  }
  if (auto* pGraphics = GetUI())
  {
    _RefreshCabLabControls(pGraphics);
    if (auto* page = pGraphics->GetControlWithTag(kCtrlTagSpeakerLab))
      page->Hide(!show);
    pGraphics->SetAllControlsDirty();
  }
  _SpeakerLabChanged();
}

proeq::Params NeuralAmpModeler::_ReadEqParams(int firstBandParam, int firstExtParam, int trimParam, int outputParam,
                                              bool enabled) const
{
  proeq::Params p;
  for (int b = 0; b < proeq::kNumBands; b++)
  {
    p.bands[b].on = enabled && GetParam(EqBandParam(firstBandParam, b, 0))->Bool();
    p.bands[b].freq = GetParam(EqBandParam(firstBandParam, b, 1))->Value();
    p.bands[b].gainDB = GetParam(EqBandParam(firstBandParam, b, 2))->Value();
    p.bands[b].q = GetParam(EqBandParam(firstBandParam, b, 3))->Value();
    p.bands[b].type =
      (proeq::Type)std::clamp(GetParam(EqBandExtParam(firstExtParam, b, 0))->Int(), 0, proeq::kNumTypes - 1);
    p.bands[b].slope = std::clamp(GetParam(EqBandExtParam(firstExtParam, b, 1))->Int(), 0, proeq::kNumSlopes - 1);
  }
  p.outputDB = enabled ? GetParam(outputParam)->Value() : 0.0;
  p.trimDB = enabled ? GetParam(trimParam)->Value() : 0.0;
  return p;
}

tube::TubeParams NeuralAmpModeler::_ReadTubeParams(int lab) const
{
  tube::TubeParams params;
  params.driveDB = GetParam(LabParam(lab, kTubeDrive))->Value();
  params.biasV = GetParam(LabParam(lab, kTubeBias))->Value();
  params.bloom = GetParam(LabParam(lab, kTubeBloom))->Value() / 10.0;
  params.bright = GetParam(LabParam(lab, kTubeBright))->Value() / 10.0;
  params.mix = GetParam(LabParam(lab, kTubeMix))->Value() / 10.0;
  params.outputDB = GetParam(LabParam(lab, kTubeOutput))->Value();
  params.circuit = GetParam(LabParam(lab, kTubeCircuit))->Bool();
  auto sel = [this, lab](int paramIdx) { return std::clamp(GetParam(LabParam(lab, paramIdx))->Int(), 0, 5); };
  params.cathodeCap = sel(kTubeCathodeCap) > 0;
  params.cathodeCapUF = params.cathodeCap ? kTubeCathodeCapUF[sel(kTubeCathodeCap)] : 22.0;
  params.plateCapPF = kTubePlateCapPF[sel(kTubePlateCap)];
  params.plateKOhm = GetParam(LabParam(lab, kTubePlate))->Value();
  params.sag = GetParam(LabParam(lab, kTubeSagOn))->Bool();
  params.sagSupplyKOhm = GetParam(LabParam(lab, kTubeSagSupply))->Value();
  params.sagTimeS = GetParam(LabParam(lab, kTubeSagTime))->Value();
  params.stage2 = GetParam(LabParam(lab, kTubeStage2))->Bool();
  params.gain2 = GetParam(LabParam(lab, kTubeGain2))->Value() / 10.0;
  params.bias2V = GetParam(LabParam(lab, kTubeBias2))->Value();
  params.cathodeCap2 = sel(kTubeCap2) > 0;
  params.cathodeCap2UF = params.cathodeCap2 ? kTubeCathodeCapUF[sel(kTubeCap2)] : 0.68;
  params.plateCap2PF = kTubePlateCapPF[sel(kTubePlateCap2)];
  params.coupleNF = kTubeCoupleNF[sel(kTubeCouple)];
  params.powerAmp = GetParam(LabParam(lab, kTubePower))->Bool();
  params.master = GetParam(LabParam(lab, kTubeMaster))->Value() / 10.0;
  params.powerSag = GetParam(LabParam(lab, kTubePowerSag))->Value() / 10.0;
  params.feedback = GetParam(LabParam(lab, kTubeFeedback))->Value() / 10.0;
  params.presence = GetParam(LabParam(lab, kTubePresence))->Value() / 10.0;
  params.sixL6 = GetParam(LabParam(lab, kTube6L6))->Bool();
  params.speakerLoad = GetParam(LabParam(lab, kTubeSpeakerLoad))->Bool();
  params.screenSquish = GetParam(LabParam(lab, kTubeScreenSquish))->Bool();
  params.sharedSupply = GetParam(LabParam(lab, kTubeSharedSupply))->Bool();
  params.otSaturation = GetParam(LabParam(lab, kTubeOTSaturation))->Bool();
  params.coilHeat = GetParam(LabParam(lab, kTubeCoilHeat))->Bool();
  params.feelDepth = GetParam(LabParam(lab, kTubeFeelDepth))->Value();
  params.economy = GetParam(LabParam(lab, kTubeEconomy))->Bool();
  params.touch = GetParam(LabParam(lab, kTubeTouch))->Value();
  params.ripple = GetParam(LabParam(lab, kTubeRipple))->Bool();
  params.rippleAmount = GetParam(LabParam(lab, kTubeRippleAmount))->Value();
  params.rippleMix = GetParam(LabParam(lab, kTubeRippleMix))->Value() / 100.0;
  params.cathodeFollower = GetParam(LabParam(lab, kTubeCathodeFollower))->Bool();
  params.cathodeBias = GetParam(LabParam(lab, kTubeCathodeBias))->Bool();
  params.speakerFeedback = GetParam(LabParam(lab, kTubeSpeakerFeedback))->Bool();
  params.resonance = GetParam(LabParam(lab, kTubeResonance))->Value() / 10.0;
  params.highQuality = GetParam(LabParam(lab, kTubeHighQuality))->Bool();
  params.mainsV = GetParam(LabParam(lab, kTubeVariac))->Value();
  params.powerBias = GetParam(LabParam(lab, kTubeBiasPot))->Value() / 100.0;
  params.preampAge = GetParam(LabParam(lab, kTubePreampAge))->Value() / 100.0;
  params.powerAge = GetParam(LabParam(lab, kTubePowerAge))->Value() / 100.0;
  params.canFail = GetParam(LabParam(lab, kTubeCanFail))->Bool();
  params.quad = GetParam(LabParam(lab, kTubeQuad))->Bool(); // 1.68.0
  params.capture = params.circuit && GetParam(LabParam(lab, kTubeCapture))->Bool();
  // With the REAL FEEL pedal in the chain, the switches act there instead (the Tube Lab's own fade
  // out, so they don't act twice).
  if (lab == 0 && _RealFeelPedalPlaced()) // (The pedal is TUBE LAB's)
    params.screenSquish = params.sharedSupply = params.otSaturation = params.coilHeat = false;
  params.toneStack = std::clamp(GetParam(LabParam(lab, kTubeStack))->Int(), 0, 2);
  params.bass = GetParam(LabParam(lab, kTubeBass))->Value() / 10.0;
  params.mid = GetParam(LabParam(lab, kTubeMid))->Value() / 10.0;
  params.treble = GetParam(LabParam(lab, kTubeTreble))->Value() / 10.0;
  // 1.68.0: the chassis sockets (circuit engine only: the Classic engine has no stages to add to, and
  // with the capture as the preamp there are no preamp tubes).
  for (int k = 0; k < tube::kNumSockets; k++)
  {
    tube::SocketParams& s = params.socket[k];
    auto get = [&](int field) { return GetParam(LabParam(lab, SocketParam(k, field))); };
    s.role = params.circuit && !params.capture ? std::clamp(get(kSockRole)->Int(), 0, tube::kNumSocketRoles - 1)
                                               : tube::kSocketOff;
    s.tube = std::clamp(get(kSockTube)->Int(), 0, tube::kNumTubeTypes - 1);
    s.gain = get(kSockGain)->Value() / 10.0;
    s.biasV = get(kSockBias)->Value();
    const int cap = std::clamp(get(kSockCap)->Int(), 0, 5);
    s.cathodeCapUF = cap > 0 ? kTubeCathodeCapUF[cap] : 0.0;
    s.plateCapPF = kTubePlateCapPF[std::clamp(get(kSockPlateCap)->Int(), 0, 5)];
    s.coupleNF = kTubeCoupleNF[std::clamp(get(kSockCouple)->Int(), 0, 5)];
  }
  return params;
}

bool NeuralAmpModeler::_RealFeelPedalPlaced() const
{
  return GetParam(kRealFeelActive)->Value() > 0.5 && GetParam(GridCellParam(chaingrid::kRealFeel))->Value() >= 0.0;
}

std::string NeuralAmpModeler::_ChainDescription() const
{
  const chaingrid::Grid grid = _ReadChainGrid();
  std::vector<std::pair<int, int>> order; // (column-major cell position, block id)
  for (int id = 0; id < chaingrid::kNumIds; id++)
    if (grid.Placed(id))
      order.push_back({chaingrid::CellCol(grid.cell[id]) * chaingrid::kRows + chaingrid::CellRow(grid.cell[id]), id});
  std::sort(order.begin(), order.end());
  std::string out;
  for (const auto& entry : order)
  {
    const int id = entry.second;
    std::string name;
    if (chaingrid::IsPedal(id))
    {
      name = PedalOf(id).name;
      if (GetParam(PedalOf(id).engagedParam)->Value() <= 0.5)
        name += " (off)";
    }
    else
      name = id == chaingrid::kAmp          ? "AMP"
             : id == chaingrid::kCab        ? "CAB"
             : id == chaingrid::kLoopSend   ? "LOOP SEND"
             : id == chaingrid::kLoopReturn ? "LOOP RETURN"
                                            : "?";
    out += (out.empty() ? "" : " > ") + name;
  }
  return out;
}

std::string NeuralAmpModeler::_TubeLogDir() const
{
  if (const char* appData = std::getenv("LOCALAPPDATA"))
    return (std::filesystem::path(appData) / BUNDLE_NAME / "debug-logs").string();
  std::error_code ec;
  return (std::filesystem::temp_directory_path(ec) / BUNDLE_NAME "-debug-logs").string();
}

// Local wall-clock time formatted with strftime.
static std::string _LocalTimeString(const char* format)
{
  const std::time_t t = std::time(nullptr);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &t);
#else
  localtime_r(&t, &local);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), format, &local);
  return buf;
}

void NeuralAmpModeler::_SetTubeLogging(bool on)
{
  if (!on)
  {
    mTubeLog.Close();
    return;
  }
  if (mTubeLog.IsOpen())
    return;
  const std::string dir = _TubeLogDir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = (std::filesystem::path(dir) / _LocalTimeString("griffin-debug-%Y%m%d-%H%M%S.csv")).string();
  if (mTubeLog.Open(path))
  {
    mTubeLogStart = std::chrono::steady_clock::now();
    mTubeLogLastRowS = -1e9;
  }
}

void NeuralAmpModeler::_UpdateTubeDebug()
{
  using namespace std::chrono;
  IGraphics* pGraphics = GetUI();
  NAMTubeDebugPageControl* page = nullptr;
  if (pGraphics)
    if (auto* pControl = pGraphics->GetControlWithTag(kCtrlTagTubeDebug))
      page = pControl->As<NAMTubeDebugPageControl>();
  const bool visible = page && !page->IsHidden();
  if (pGraphics)
    mTubeDebugVisible = visible; // With the editor closed, remember whether it was open
  const bool logging = mTubeLog.IsOpen();
  const bool wanted = visible || logging || mTubeProbesAlways;
  const auto now = steady_clock::now();
  if (wanted && !mTubeProbe.Enabled())
    mTubeStatsLastArrived = now; // Give the first measurement time to arrive before saying "no audio"
  mTubeProbe.SetEnabled(wanted);
  if (!wanted)
    return;

  const tube::TubeParams params = _ReadTubeParams(mUiLab);
  const bool engaged = GetParam(_TP(kTubeEngaged))->Value() > 0.5;
  const bool inChain = _ReadChainGrid().Placed(mUiLab == 0 ? kTubePedalIdx : chaingrid::kTubeLab2);
  tube::ProbeStats stats;
  if (mTubeProbe.TakeStats(stats))
  {
    mTubeStatsLastArrived = now;
    const bool signal = tube::debug::HasSignal(stats);
    if (logging)
    {
      // Rows with signal always; silent ones only when a setting changed, or every 5 s.
      const double t = duration<double>(now - mTubeLogStart).count();
      if (signal || mTubeLog.Rows() == 0 || mTubeLog.SettingsChanged(params, engaged) || t - mTubeLogLastRowS >= 5.0)
      {
        mTubeLog.Write(t, _LocalTimeString("%H:%M:%S").c_str(), stats, params, engaged, GetBlockSize(), GetLatency(),
                       _ChainDescription());
        mTubeLogLastRowS = t;
      }
    }
    if (signal)
    {
      mTubeLastSignalStats = stats;
      mTubeLastSignalTime = now;
      mTubeHaveSignalStats = true;
    }
    if (visible)
    {
      // Between notes, keep explaining the last thing played (against the current settings).
      const double sinceSignal = duration<double>(now - mTubeLastSignalTime).count();
      const bool hold = !signal && mTubeHaveSignalStats && sinceSignal < 10.0;
      tube::ProbeStats shown = stats;
      if (hold)
      {
        shown = mTubeLastSignalStats;
        shown.CopyRestingFrom(stats);
      }
      std::string header = hold ? tube::debug::Format("Holding the last thing you played (%.0f s ago).", sinceSignal)
                                : std::string("Live: the last 0.25 s, updated 4 times a second.");
      page->Readout()->SetLines(tube::debug::Explain(shown, params, engaged), std::move(header));
      page->Map()->SetActivity(shown);
      page->Bench()->SetStats(stats, params.circuit, params.powerAmp); // Live (quiet: the idle current)
    }
  }
  if (now >= mTubeStatsNextRequest)
  {
    mTubeProbe.RequestStats();
    mTubeStatsNextRequest = now + milliseconds(kTubeStatsMs);
  }
  if (!visible)
    return;

  // No measurements arriving: say why.
  const bool stale = duration<double>(now - mTubeStatsLastArrived).count() > 1.0;
  if (stale)
  {
    std::vector<tube::debug::Line> lines;
    if (!inChain)
      lines.push_back({"FEEL",
                       "The Tube Lab isn't on the signal-chain grid, so no audio reaches it. Drag TUBE LAB from the "
                       "AMP NEST onto the grid and cable it in.",
                       true});
    else
      lines.push_back(
        {"FEEL", "No audio is being processed. Is the audio device running (Settings, or the app's Preferences)?", true});
    page->Readout()->SetLines(std::move(lines), "Waiting for audio...");
  }
  page->Map()->SetSections(params.circuit, params.stage2, params.toneStack, params.powerAmp, params.sixL6);
  page->Map()->SetSelected(mTubeScopeSettings.chA, mTubeScopeSettings.chB);
  page->Scope()->Update(mTubeProbe, GetSampleRate());
  const int live = mTubeProbe.ReadLatest(std::max(64, static_cast<int>(0.05 * GetSampleRate())), mTubeLiveFrames);
  page->Map()->SetLive(mTubeLiveFrames, stale ? 0 : live);

  const double sr = GetSampleRate();
  const int block = mDeviceBufferSize > 0 ? mDeviceBufferSize : GetBlockSize();
  const std::string app = tube::debug::Format(
    "%.1f kHz, %d-sample buffer (%.1f ms), plugin latency %d samples. Tube Lab: %s, %s, %dx oversampled.", sr / 1e3,
    block, 1e3 * block / std::max(sr, 1.0), GetLatency(), inChain ? "on the grid" : "NOT on the grid",
    engaged ? "on" : "bypassed", _UiStages()[0].GetOversampling());
  const std::string log = logging
                            ? tube::debug::Format("CSV log: %d rows to %s", mTubeLog.Rows(), mTubeLog.Path().c_str())
                            : "CSV log off. Logs are saved in " + _TubeLogDir();
  page->SetStatus(app, log);
}

// 1.64.0: the Tube Lab is on the grid with the AMP block's capture as its preamp (circuit engine).
bool NeuralAmpModeler::_TubeTakesCapture() const
{
  return GetParam(kTubeCapture)->Bool() && GetParam(kTubeCircuit)->Bool() && _ReadChainGrid().Placed(kTubePedalIdx);
}

// The PREAMP CHASSIS page (1.62.0): while it's open, the audio thread reports the glow levels, and
// the page redraws when something it shows has changed -- at most every 40 ms.
void NeuralAmpModeler::_UpdateChassis()
{
  IGraphics* pGraphics = GetUI();
  auto* page = pGraphics ? dynamic_cast<IContainerBase*>(pGraphics->GetControlWithTag(kCtrlTagTubeChassis)) : nullptr;
  const bool open = page && !page->IsHidden();
  mChassisOpen.store(open, std::memory_order_relaxed);
  if (!open)
  {
    mChassisSignature = -1.0;
    return;
  }
  for (auto& [item, control] : mChassisFields)
    if (control->IsHidden() != (item != mChassisSelected))
      control->Hide(item != mChassisSelected);
  const auto now = std::chrono::steady_clock::now();
  if (now - mChassisLastDraw < std::chrono::milliseconds(40))
    return;
  // A signature of everything drawn: glow levels (to ~1 dB), the params, the power tubes.
  double sig = mChassisSelected;
  for (int i = 0; i < tube::TubeStage::kNumStageLevels; i++)
    sig = sig * 1.3 + std::round(20.0 * std::log10(std::max(1e-6f, mUiStageLevel[i].load())));
  for (int p : {kTubeVariac, kTubeBloom, kTubeBias, kTubeCathodeCap, kTubePlate, kTubePlateCap, kTubeCouple, kTubeGain2,
                kTubeBias2, kTubeCap2, kTubePlateCap2, kTubeStage2, kTubeCathodeFollower, kTubePower, kTubeCircuit,
                kTubeCanFail, kTube6L6, kTubeEngaged, kTubeCapture, kTubeDrive, kTubeBass, kTubeMid, kTubeTreble,
                kTubeMaster, kTubePresence, kTubeSpeakerLoad, kTubeBiasPot, kTubePowerSag})
    sig = sig * 1.1 + GetParam(_TP(p))->Value();
  for (int p = kTubeChassisFirst; p <= kTubeChassisLast; p++) // 1.68.0: the sockets, the quad
    sig = sig * 1.05 + GetParam(_TP(p))->Value();
  for (int k = 0; k < 4; k++)
    sig = sig * 1.7 + std::round(100.0 * mUiPowerLife[k].load()) + (mUiPowerDead[k].load() ? 1000.0 : 0.0)
          + std::round(100.0 * mUiDissRatio[k].load());
  if (sig == mChassisSignature)
    return;
  mChassisSignature = sig;
  mChassisLastDraw = now;
  for (int i = 0; i < page->NChildren(); i++)
  {
    IControl* c = page->GetChild(i);
    if (dynamic_cast<NAMTubeChassisControl*>(c) || dynamic_cast<NAMPowerTubeStatusControl*>(c)
        || dynamic_cast<NAMDynamicTextControl*>(c) || dynamic_cast<NAMChassisLinksControl*>(c))
      c->SetDirty(false);
  }
}

// 1.68.0: the chassis's links (chassis::Link): the page closes and the other one opens.
void NeuralAmpModeler::_ChassisGo(int link)
{
  IGraphics* pGraphics = GetUI();
  if (!pGraphics)
    return;
  const int labBlock = mUiLab == 0 ? kTubePedalIdx : chaingrid::kTubeLab2;
  switch (link)
  {
    case chassis::kLinkPreamp:
    case chassis::kLinkPowerAmp:
    case chassis::kLinkFeel:
      // The Tube Lab's popup (for the lab shown) on that tab. It's normally open under the chassis.
      if (mSelectedBlock != labBlock)
        _SelectBlock(labBlock);
      mTubeTab = link - chassis::kLinkPreamp;
      _ApplyTubeTab();
      break;
    case chassis::kLinkDebug:
      if (auto* debug = dynamic_cast<NAMTubeDebugPageControl*>(pGraphics->GetControlWithTag(kCtrlTagTubeDebug)))
      {
        debug->Hide(false);
        if (debug->Scope() && debug->Map())
        {
          debug->Scope()->SetChannel(chassis::ProbeOf(mChassisSelected), false);
          debug->Map()->SetSelected(mTubeScopeSettings.chA, mTubeScopeSettings.chB);
        }
      }
      break;
    case chassis::kLinkSpeakerLab: _ShowSpeakerLab(true); break;
    case chassis::kLinkCapture:
      // TUBE LAB uses the AMP block's capture: open it. TUBE LAB 2's own is loaded on its popup (LOAD CAPTURE).
      if (mUiLab == 0)
        _SelectBlock(kAmpBlockId);
      else if (mSelectedBlock != labBlock)
        _SelectBlock(labBlock);
      break;
    default: break;
  }
  // (Last: opening the Tube Lab's popup above opens the chassis too.)
  if (auto* page = pGraphics->GetControlWithTag(kCtrlTagTubeChassis))
    page->Hide(true);
  pGraphics->SetAllControlsDirty();
}

// The PEDALS drawer (1.60.0): its panel shows or hides, and every chip re-reads where it is.
void NeuralAmpModeler::_SetPedalDrawerOpen(bool open)
{
  mPedalDrawerOpen = open;
  if (IGraphics* pGraphics = GetUI())
  {
    if (auto* drawer = pGraphics->GetControlWithTag(kCtrlTagPedalDrawer))
      drawer->Hide(!open);
    pGraphics->ForControlInGroup("UNDER_DRAWER", [open](IControl* ctrl) { ctrl->Hide(open); });
    pGraphics->SetAllControlsDirty();
  }
}

// SPEAKER LAB (1.59.0): redraw the views, readout and tabs, and recompute the plot -- only after a
// change (they never animate).
void NeuralAmpModeler::_SpeakerLabChanged()
{
  IGraphics* pGraphics = GetUI();
  if (!pGraphics)
    return;
  auto* page = dynamic_cast<IContainerBase*>(pGraphics->GetControlWithTag(kCtrlTagSpeakerLab));
  if (!page)
    return; // (Hidden, it's marked anyway: opening it then draws the current state.)
  for (int i = 0; i < page->NChildren(); i++)
  {
    IControl* c = page->GetChild(i);
    if (auto* plot = dynamic_cast<NAMSpeakerLabResponseControl*>(c))
      plot->MarkStale();
    else if (dynamic_cast<NAMSpeakerLabViewControl*>(c) || dynamic_cast<NAMSpeakerLabTabsControl*>(c)
             || dynamic_cast<NAMSpeakerLabReadoutControl*>(c))
      c->SetDirty(false);
  }
}

// A drag on one of the views: the mic's X, Y, Z params (cm), as host-visible edits.
void NeuralAmpModeler::_SetLabMicPosition(int mic, double x, double y, double z)
{
  const double cm[3] = {100.0 * x, 100.0 * y, 100.0 * z};
  IGraphics* pGraphics = GetUI();
  for (int f = 0; f < 3; f++)
  {
    const int param = LabMicParam(mic, 2 + f);
    if (std::fabs(GetParam(param)->Value() - cm[f]) < 1e-9)
      continue;
    const double normalized = GetParam(param)->ToNormalized(cm[f]);
    BeginInformHostOfParamChangeFromUI(param);
    SendParameterValueFromUI(param, normalized);
    EndInformHostOfParamChangeFromUI(param);
    if (pGraphics)
      pGraphics->ForControlWithParam(param, [normalized](IControl* c) { c->SetValueFromDelegate(normalized); });
  }
}

// Each mic keeps its offset from its nearest cone, moved to the matching cone of the new cab (the
// last one if the new cab has fewer).
void NeuralAmpModeler::_RemapLabMicsForFormat(int cab, int oldFormat, int newFormat, int onlyMic)
{
  const cab::CabFormat& from = cab::GetCabFormat(oldFormat);
  const cab::CabFormat& to = cab::GetCabFormat(newFormat);
  const cab::CabLabParams p = _ReadCabLabParams();
  for (int m = 0; m < cab::kNumLabMics; m++)
  {
    const cab::LabMic& lm = p.lab[m];
    if (onlyMic >= 0 ? m != onlyMic : std::clamp(lm.cab, 0, 1) != cab)
      continue; // (Only the mics on the cab that changed)
    int k = 0;
    cab::LabProximityDB(lm, from, &k);
    const int k2 = std::min(k, to.numCones - 1);
    const double x = to.cx[k2] + (lm.x - from.cx[k]), y = to.cy[k2] + (lm.y - from.cy[k]);
    _SetLabMicPosition(m, std::clamp(x, -0.5, 0.5), std::clamp(y, -0.5, 0.5), lm.z);
  }
}

// The OUTPUT page's meters (1.58.0): only while it's open; the audio thread measures only then too.
void NeuralAmpModeler::_UpdateOutputPage()
{
  IGraphics* pGraphics = GetUI();
  IControl* page = pGraphics ? pGraphics->GetControlWithTag(kCtrlTagOutputPage) : nullptr;
  const bool open = page && !page->IsHidden();
  mOutputPageOpen.store(open, std::memory_order_relaxed);
  if (!open)
    return;
  const float gr = mLimiterGRShown.exchange(0.f, std::memory_order_relaxed);
  if (auto* c = pGraphics->GetControlWithTag(kCtrlTagLimiterGR))
    c->As<NAMOutputMeterControl>()->SetReading(gr, GetParam(kOutputLimiter)->Bool());
  if (auto* c = pGraphics->GetControlWithTag(kCtrlTagOutCorrelation))
    c->As<NAMOutputMeterControl>()->SetReading(mOutCorrelation.load(std::memory_order_relaxed), true);
}

// HACK
// 1.70.0 PACK PICKER ==========================================================

// 1.70.1: the pack scan log, next to the app / plug-in binary ("pack-scan.log"); if that folder can't
// be written, %LOCALAPPDATA%\GriffinAmplification instead. Appends; trimmed when it passes 512 KB.
static void _PackLog(const std::string& text)
{
  std::string path;
#ifdef _WIN32
  HMODULE mod = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&_PackLog), &mod))
  {
    wchar_t buf[MAX_PATH * 2] = {0};
    if (GetModuleFileNameW(mod, buf, MAX_PATH * 2))
      path = griffinpack::U8(std::filesystem::path(buf).parent_path() / "pack-scan.log");
  }
#endif
  auto write = [&](const std::string& p) {
    if (p.empty())
      return false;
    std::error_code ec;
    const auto fp = std::filesystem::u8path(p);
    if (std::filesystem::exists(fp, ec) && std::filesystem::file_size(fp, ec) > 512 * 1024)
      std::filesystem::remove(fp, ec);
    std::ofstream f(fp, std::ios::app);
    if (!f.good())
      return false;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char stamp[64];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    f << "[" << stamp << "] v" << PLUG_VERSION_STR << " " << text << (text.size() && text.back() == '\n' ? "" : "\n");
    return f.good();
  };
  if (!write(path))
    if (const char* base = std::getenv("LOCALAPPDATA"))
      write(std::string(base) + "\\" BUNDLE_NAME "\\pack-scan.log");
}

void NeuralAmpModeler::_PackOpenFolder(const std::string& folderIn)
{
  namespace fs = std::filesystem;
  std::string folder = folderIn;
  while (folder.size() > 3 && (folder.back() == '\\' || folder.back() == '/'))
    folder.pop_back();
  std::error_code ec;
  // A capture (or any file) picked: use its folder. A .zip: refuse, it needs unzipping.
  if (griffinpack::IsZipPath(folder))
  {
    // 1.70.3: open it directly: unpack (or reuse) its cache on a worker thread; _PackPollZip opens it.
    std::string zip = folder;
    const auto lz = griffinpack::Lower(zip);
    const auto at = lz.find(".zip");
    if (at != std::string::npos)
      zip = zip.substr(0, at + 4); // (A folder "inside" a zip, from the folder chooser: the zip itself.)
    if (!mPackUnpacking.empty())
    {
      _ShowMessageBox(GetUI(), ("Still unpacking " + mPackUnpacking + "...").c_str(), "PACK", kMB_OK);
      return;
    }
    mPackUnpacking = griffinpack::U8(fs::u8path(zip).filename());
    mPackZipFuture = std::async(std::launch::async, [zip]() {
      PackZipJob job;
      job.zip = zip;
      const auto t0 = std::chrono::steady_clock::now();
      try
      {
        job.dir = griffinpack::ResolveZip(zip, job.err, job.log, job.reused);
      }
      catch (const std::exception& e)
      {
        job.err = e.what();
      }
      job.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      return job;
    });
    _PackLog("unpacking (worker thread): " + zip);
    if (GetUI())
      GetUI()->SetAllControlsDirty();
    return;
  }
  if (fs::is_regular_file(fs::u8path(folder), ec))
    folder = griffinpack::U8(fs::u8path(folder).parent_path());
  griffinpack::Pack pack = griffinpack::Scan(folder);
  _PackLog(pack.log);
  if (pack.Empty())
  {
    std::string msg = "No .nam captures in:\n\n" + folder + "\n\n(or in its subfolders).";
    if (!pack.zips.empty())
      msg += "\n\nIt has " + std::to_string(pack.zips.size()) + " .zip file(s), e.g.\n"
             + griffinpack::U8(fs::u8path(pack.zips[0]).filename())
             + "\nUnzip a pack first (right-click > Extract All...), then choose the unzipped folder.";
    _ShowMessageBox(GetUI(), msg.c_str(), "PACK", kMB_OK);
    return;
  }
  mPack = std::move(pack);
  mPackZipPath = mPackOpeningZip; // 1.70.3: "" for a plain folder
  mPackOpeningZip.clear();
  mPackGroup.clear();
  mPackKey.clear();
  mPackSize.clear();
  mPackIRChoice.clear();
  mPackHasRef = false;
  mPackRefLoudness = 0.0;
  // If the loaded model is in this pack, show it as picked (no reload).
  if (const griffinpack::Capture* c = mPack.FindPath(mNAMPath.Get()))
  {
    mPackGroup = c->group;
    mPackKey = c->key;
    mPackSize = c->size;
  }
#ifdef APP_API
  mStateDirty = true;
#endif
  if (GetUI())
    GetUI()->SetAllControlsDirty();
}
void NeuralAmpModeler::_PackPollZip()
{
  if (!mPackZipFuture.valid()
      || mPackZipFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;
  PackZipJob job = mPackZipFuture.get();
  mPackUnpacking.clear();
  char ms[64];
  snprintf(ms, sizeof(ms), "%.0f ms", job.ms);
  _PackLog("zip " + job.zip + (job.reused ? " (cache reused, " : " (unpacked, ") + ms + ")\n" + job.log
           + (job.err.empty() ? "" : "  error: " + job.err));
  if (job.dir.empty())
  {
    _ShowMessageBox(GetUI(), ("Couldn't open the zip:\n\n" + job.zip + "\n\n" + job.err).c_str(), "PACK", kMB_OK);
    if (GetUI())
      GetUI()->SetAllControlsDirty();
    return;
  }
  mPackOpeningZip = job.zip;
  _PackOpenFolder(job.dir);
  mPackOpeningZip.clear();
}
std::string NeuralAmpModeler::_PackIRPath() const
{
  if (!mPackIRChoice.empty())
    for (const auto& ir : mPack.irs)
      if (ir == mPackIRChoice)
        return ir;
  return mPack.defaultIR;
}

void NeuralAmpModeler::_PackSelect(const griffinpack::Capture* c)
{
  if (!c)
    return;
  // Level match: the first capture picked in this pack sets the reference; the rest are trimmed to it.
  if (!mPackHasRef && c->hasLoudness)
  {
    mPackHasRef = true;
    mPackRefLoudness = c->loudness;
  }
  mPackTrimDB = (mPackHasRef && c->hasLoudness) ? mPackRefLoudness - c->loudness : 0.0;
  // Latest wins: staging replaces whatever was staged and not yet swapped in.
  mPackStaging = true;
  const std::string msg = _StageModel(WDL_String(c->path.c_str()));
  mPackStaging = false;
  if (msg.size())
  {
    _ShowMessageBox(GetUI(), ("Failed to load the capture:\n\n" + msg).c_str(), "PACK", kMB_OK);
    return;
  }
  mPackGroup = c->group;
  mPackKey = c->key;
  mPackSize = c->size;
  // The pack's IR, for amp-only captures -- only into an empty cab slot, or one the pack filled itself.
  const std::string ir = _PackIRPath();
  if (mPackAutoIR && c->ampOnly && !ir.empty() && ir != std::string(mIRPath.Get())
      && (mIRPath.GetLength() == 0 || mPackIRSetPath == mIRPath.Get()))
  {
    if (_StageIR(WDL_String(ir.c_str())) == dsp::wav::LoadReturnCode::SUCCESS)
      mPackIRSetPath = ir;
  }
  if (GetUI())
    GetUI()->SetAllControlsDirty();
}

std::string NeuralAmpModeler::_PackLabel(int which) const
{
  namespace fs = std::filesystem;
  switch (which)
  {
    case 0:
    {
      // 1.70.2: the dropdown's text: the pack, and the capture picked from it.
      if (!mPackUnpacking.empty())
        return "Unpacking " + mPackUnpacking + "...";
      if (mPack.Empty())
        return "Select pack...";
      const std::string pack = griffinpack::U8(fs::u8path(mPackZipPath.empty() ? mPack.folder : mPackZipPath).filename());
      const griffinpack::Capture* c = mPackKey.empty() ? nullptr : mPack.Find(mPackGroup, mPackKey, mPackSize);
      if (!c)
        return pack + ": pick a capture";
      std::string s = mPackKey + (mPackSize.empty() ? "" : " - " + mPackSize);
      if (c->path != mNAMPath.Get())
        s += " (not loaded)";
      return s;
    }
    case 1:
    {
      if (mPack.Empty())
        return "capture";
      const griffinpack::Capture* c = mPack.Find(mPackGroup, mPackKey, mPackSize);
      if (!c || mPackKey.empty())
        return "pick a capture";
      return c->path == mNAMPath.Get() ? mPackKey : mPackKey + " (not loaded)";
    }
    case 2: return mPackSize.empty() ? std::string("size") : mPackSize;
    case 3:
      if (mPack.irs.empty())
        return "IR: none";
      return mPackAutoIR ? "IR: auto" : "IR: off";
    default: return "";
  }
}

void NeuralAmpModeler::_PackShowMenu(int which, IControl* pCaller)
{
  // 1.70.2: one menu: [the pack's captures] / packs in the library / choose a folder / rescan / IR.
  IGraphics* ui = pCaller ? pCaller->GetUI() : nullptr;
  if (!ui)
    return;
  namespace fs = std::filesystem;
  mPackMenu.Clear();
  mPackMenuTargets.clear();
  auto add = [&](IPopupMenu& menu, const std::string& text, bool checked, std::function<void()> f) {
    IPopupMenu::Item* item = menu.AddItem(text.c_str());
    item->SetChecked(checked);
    mPackMenuTargets.push_back({item, std::move(f)});
  };
  // The library's capture folders (same roots as the AMPS button).
  const std::string griffinDir = GriffinLibraryDir();
  std::vector<std::pair<std::string, int>> folders;
  std::vector<std::string> zips;
  for (const char* root : {"IRs and Amp Profiles (copied)", "tone"})
    griffinpack::FindPackFolders(griffinDir + root, folders, zips);
  auto addLibrary = [&](IPopupMenu& menu) {
    for (const auto& f : folders)
    {
      const std::string folder = f.first;
      const std::string name = folder.size() > griffinDir.size() ? folder.substr(griffinDir.size()) : folder;
      add(menu, name + "  (" + std::to_string(f.second) + ")", folder == mPack.folder,
          [this, folder]() { _PackOpenFolder(folder); });
    }
    for (const auto& z : zips)
    {
      const std::string zip = z;
      const std::string zname = z.size() > griffinDir.size() ? z.substr(griffinDir.size()) : z;
      add(menu, zname, z == mPackZipPath,
          [this, zip]() { _PackOpenFolder(zip); });
    }
    if (folders.empty() && zips.empty())
      add(menu, "(no capture folders found in the library)", false, []() {});
  };

  if (!mPack.Empty())
  {
    // The pack's captures: a submenu per group (when there's more than one), and per capture with more
    // than one size, a submenu of its sizes.
    const auto groups = mPack.Groups();
    for (const auto& g : groups)
    {
      IPopupMenu* gsub = groups.size() > 1 ? new IPopupMenu() : nullptr;
      IPopupMenu& gmenu = gsub ? *gsub : mPackMenu;
      for (const auto& key : g.second)
      {
        const auto sizes = mPack.Sizes(g.first, key);
        const bool picked = g.first == mPackGroup && key == mPackKey;
        if (sizes.size() <= 1)
        {
          const std::string path = sizes.empty() ? std::string() : sizes[0]->path;
          add(gmenu, key + (sizes.size() == 1 && !sizes[0]->size.empty() ? "  - " + sizes[0]->size : ""), picked,
              [this, path]() { _PackSelect(mPack.FindPath(path)); });
        }
        else
        {
          IPopupMenu* ssub = new IPopupMenu();
          for (const griffinpack::Capture* c : sizes)
          {
            const std::string path = c->path;
            add(*ssub, c->size.empty() ? std::string("(no size)") : c->size, picked && c->size == mPackSize,
                [this, path]() { _PackSelect(mPack.FindPath(path)); });
          }
          IPopupMenu::Item* item = gmenu.AddItem((key + (picked ? "  *" : "")).c_str(), ssub);
          (void)item;
        }
      }
      if (gsub)
        mPackMenu.AddItem(g.first.c_str(), gsub);
    }
    mPackMenu.AddSeparator();
    IPopupMenu* lib = new IPopupMenu();
    addLibrary(*lib);
    mPackMenu.AddItem("Packs in your library", lib);
  }
  else
    addLibrary(mPackMenu); // No pack yet: the library's packs, right at the top.

  mPackMenu.AddSeparator();
  add(mPackMenu, "Choose a pack folder...", false, [this]() {
    IGraphics* g = GetUI();
    if (!g)
      return;
    WDL_String dir(mPack.folder.c_str());
    g->PromptForDirectory(dir, [this](const WDL_String& fileName, const WDL_String& path) {
      const std::string d = path.GetLength() ? path.Get() : fileName.Get();
      _PackLog("folder chooser returned: \"" + d + "\"");
      if (d.empty())
      {
        _ShowMessageBox(GetUI(),
                        "No folder came back from the chooser.\n\nIf you picked a .zip, unzip it first (right-click > "
                        "Extract All...), then choose the unzipped folder.",
                        "PACK", kMB_OK);
        return;
      }
      _PackOpenFolder(d);
    });
  });
  add(mPackMenu, "Choose a capture file (its folder becomes the pack)...", false, [this]() {
    IGraphics* g = GetUI();
    if (!g)
      return;
    WDL_String file, path(mPack.folder.c_str());
    g->PromptForFile(file, path, EFileAction::Open, "nam zip", [this](const WDL_String& fileName, const WDL_String& p) {
      _PackLog("file chooser returned: \"" + std::string(fileName.Get()) + "\"");
      if (fileName.GetLength())
        _PackOpenFolder(fileName.Get());
    });
  });
  if (!mPack.Empty())
  {
    add(mPackMenu, "Rescan this pack", false, [this]() { _PackOpenFolder(mPack.folder); });
    IPopupMenu* irs = new IPopupMenu();
    add(*irs, "Auto-pair the pack's IR (amp-only captures)", mPackAutoIR, [this]() { mPackAutoIR = !mPackAutoIR; });
    const std::string current = _PackIRPath();
    for (const auto& ir : mPack.irs)
      add(*irs, griffinpack::U8(fs::u8path(ir).filename()), ir == current, [this, ir]() {
        mPackIRChoice = ir;
        const griffinpack::Capture* c = mPack.Find(mPackGroup, mPackKey, mPackSize);
        if (mPackAutoIR && c && c->ampOnly && (mIRPath.GetLength() == 0 || mPackIRSetPath == mIRPath.Get()))
          if (_StageIR(WDL_String(ir.c_str())) == dsp::wav::LoadReturnCode::SUCCESS)
            mPackIRSetPath = ir;
      });
    mPackMenu.AddItem(mPack.irs.empty() ? "Cab IR (none in this pack)" : "Cab IR", irs);
  }
  add(mPackMenu, "Clear pack cache", false, [this]() {
    const int n = griffinpack::ClearPackCache(mPackZipPath.empty() ? std::string() : mPack.folder);
    _PackLog("cache cleared: " + std::to_string(n) + " pack(s) removed");
    _ShowMessageBox(GetUI(), ("Removed " + std::to_string(n) + " unpacked pack(s) from the cache.\n\nYour zips are untouched; "
                              "a zip is unpacked again the next time you open it.").c_str(), "PACK", kMB_OK);
  });
  ui->CreatePopupMenu(*pCaller, mPackMenu, pCaller->GetRECT());
}
void NeuralAmpModeler::_PackOnMenu(IPopupMenu* pMenu)
{
  const IPopupMenu::Item* item = pMenu ? pMenu->GetChosenItem() : nullptr;
  if (!item)
    return;
  for (auto& target : mPackMenuTargets)
    if (target.first == item)
    {
      auto f = target.second;
      f();
      break;
    }
  if (GetUI())
    GetUI()->SetAllControlsDirty();
}
#include "Unserialization.cpp"
#include "PackZip.cpp" // 1.70.3
