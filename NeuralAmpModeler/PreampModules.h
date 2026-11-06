#pragma once

// The preamp modules (1.72.0) for the MODULAR chassis's bays: each one a real preamp circuit, stage by
// stage, with its parts' values (ModuleCircuit.h runs them). Names describe the circuit's family and era
// only. The power amp each one brings (PowerDef) is set when it's plugged into a bay.
//
// Where a schematic switches parts with relays or pull switches, the module is the channel and mode
// named in its description, with the switches as that mode sets them. Parts a module leaves out (a
// graphic EQ, reverb and effects-loop circuits, channel-switching parts that aren't in the path) are
// listed in its notes below.

#include "ModuleCircuit.h"

namespace tube
{
namespace module
{
enum ModuleId
{
  kModuleLab = 0, // Not a module: the Tube Lab's own preamp (TubeStage's V1, V2, the sockets ...)
  kBrit45,
  kBrit100,
  kBritMV,
  kBrit90,
  kBritUltra,
  kHGSix,
  kCalCascade,
  kCalCascade2,
  kCalModern,
  kCalVintage,
  // 1.75.0: the low-gain ones (new modules go at the end: saved sessions keep their module numbers)
  kUsClean,
  kUsTweed,
  kUs4x10,
  kBritChime,
  kNumModules
};

namespace detail
{
// A guitar input: the jack's grid leak and the grid stopper.
inline void InputJack(StageDef& s, double stopper = 68e3, double leak = 1e6)
{
  s.R(S, GND, leak).R(S, G, stopper);
}

inline ModuleDef UsClean()
{
  // American 22 W reverb combo, mid 60s, its normal channel. The tone stack sits right after the first
  // stage and before the volume: 250 pF / 100k, 0.1 uF, 0.047 uF, a fixed 6.8k where other amps have a
  // Middle pot. 0.047 uF into a 220k mixing resistor, loaded by the other channel and the reverb recovery
  // stage's plate (about 35k), which is much of why it stays so clean; then 1 nF into the inverter.
  ModuleDef m;
  m.name = "US CLEAN";
  m.family = "US";
  m.description = "American 22 W reverb combo, mid 60s: the tone stack before the volume. Sparkling, scooped clean.";
  StageDef v1;
  v1.railV = 261.0;
  v1.raOhms = 100e3;
  v1.rkOhms = 1500.0;
  v1.ckF = 22e-6;
  InputJack(v1);
  StageDef v2;
  v2.railV = 311.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 820.0;
  v2.ckF = 22e-6;
  v2.C(S, N3, 250e-12).Pot(N3, N1, N4, 250e3, kKnobTreble, kLinear).Rheo(N4, N5, 250e3, kKnobBass, kAudio);
  v2.R(N5, GND, 6.8e3).R(S, N6, 100e3).C(N6, N4, 100e-9).C(N6, N5, 47e-9);
  v2.Pot(N1, N2, GND, 1e6, kKnobGain, kAudio).R(N2, G, 1e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.numStages = 2;
  m.output.C(S, N1, 47e-9).R(N1, N2, 220e3).R(N2, GND, 35e3).C(N2, N3, 1e-9).R(N3, GND, 1e6);
  m.output.out = N3;
  m.knobLabel[kKnobGain] = "Volume";
  m.knobLabel[kKnobMid] = nullptr;
  m.knobDefault[kKnobGain] = 0.4;
  m.power = {false, false, 0.4, 0.0, 0.5, 0.35, kPower6V6, 390.0, 6600.0, false};
  m.needs8x = false;
  m.levelDB = 0.0;
  m.outputDB = 19.6;
  return m;
}

inline ModuleDef UsTweed()
{
  // American small tweed combo, 50s, 12 W. A 12AY7 first stage (820 ohm, 25 uF shared by its two triodes),
  // then both volumes' wipers and the tone pot's tie straight to one 12AX7 grid, with no mixing resistors:
  // the volumes interact, and the second channel's turned all the way down grounds the grid, as on the amp.
  // Its pot is fed through 0.1 uF from the idle triode's plate (about 38k). The tone: 500 pF from the first
  // volume's top into a 1M pot, 5 nF to ground. No feedback, no tone stack, cathode bias.
  ModuleDef m;
  m.name = "US TWEED";
  m.family = "US";
  m.description = "American small tweed combo, 50s: one tone knob, two volumes that interact. Early, furry breakup.";
  StageDef v1;
  v1.tube = k12AY7;
  v1.railV = 300.0;
  v1.raOhms = 100e3;
  v1.rkOhms = 1640.0;
  v1.ckF = 12.5e-6;
  InputJack(v1);
  StageDef v2;
  v2.railV = 300.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 1500.0;
  v2.ckF = 25e-6;
  v2.C(S, N1, 100e-9).Pot(N1, N2, GND, 1e6, kKnobGain, kAudio);
  v2.C(N1, N3, 500e-12).Pot(N3, N2, N4, 1e6, kKnobTreble, kAudio).C(N4, GND, 5e-9);
  v2.Pot(N5, N2, GND, 1e6, kKnobGain2, kAudio).C(N5, N6, 100e-9).R(N6, GND, 38e3);
  v2.R(N2, G, 1e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.numStages = 2;
  m.output.C(S, N1, 20e-9).R(N1, GND, 1e6);
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Volume";
  m.knobLabel[kKnobGain2] = "Volume 2";
  m.knobLabel[kKnobBass] = nullptr;
  m.knobLabel[kKnobMid] = nullptr;
  m.knobLabel[kKnobTreble] = "Tone";
  m.knobDefault[kKnobGain] = 0.5;
  m.knobDefault[kKnobGain2] = 0.5;
  m.power = {false, false, 0.0, 0.0, 0.3, 0.8, kPower6V6, 360.0, 8000.0, true};
  m.needs8x = false;
  m.levelDB = 0.0;
  m.outputDB = -7.1;
  return m;
}

inline ModuleDef Us4x10()
{
  // American 40 W tweed bass amp, late 50s, its bright channel: a 12AY7 first stage (820 ohm, 250 uF shared),
  // 100 pF across the volume, a 270k mixing resistor, a 12AX7 stage with an unbypassed 820 ohm cathode, then
  // the DC-coupled cathode follower into a 250 pF / 56k stack with a real Middle pot. The BRIT 45 is this
  // circuit with a 12AX7 first; here it's the two 5881s (6L6s) on a 2k load, 430 V, with feedback.
  ModuleDef m;
  m.name = "US 4X10";
  m.family = "US";
  m.description = "American 40 W tweed bass amp, late 50s: a follower into a three-knob stack. Fat, punchy, big clean.";
  StageDef v1;
  v1.tube = k12AY7;
  v1.railV = 300.0;
  v1.raOhms = 100e3;
  v1.rkOhms = 1640.0;
  v1.ckF = 125e-6;
  InputJack(v1);
  StageDef v2;
  v2.railV = 325.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 820.0;
  v2.C(S, N1, 20e-9).Pot(N1, N2, GND, 1e6, kKnobGain, kAudio).C(N1, N2, 100e-12).R(N2, N3, 270e3);
  v2.R(N3, GND, 770e3).R(N3, G, 1e3);
  v2.Follower(100e3, 325.0, 100e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.numStages = 2;
  m.output.Stack(S, N1, N2, N3, N4, N5, 250e-12, 20e-9, 20e-9, 250e3, 1e6, 25e3, 56e3).R(N1, GND, 1e6);
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Volume";
  m.knobDefault[kKnobGain] = 0.5;
  m.power = {true, false, 0.45, 0.5, 0.5, 0.5, kPowerStock, 430.0, 2000.0, false};
  m.needs8x = false;
  m.levelDB = 0.0;
  m.outputDB = -5.9;
  return m;
}

inline ModuleDef BritChime()
{
  // British 30 W top-boost combo: a 220k-plate first stage, 470 pF into the 500k volume with 100 pF across
  // its top half (the small coupling cap is much of the chime), a second stage (100k, 1.5k, 22 uF), then a
  // DC-coupled cathode follower (56k) into the two-knob top-boost stack: 47 pF, 100k, two 22 nF, a 1M
  // treble and bass, 10k to ground, and a 220k / 330k divider. Four EL84s on cathode bias, no feedback.
  // The inverter's Cut control isn't here: it's a part of the power amp's inverter.
  ModuleDef m;
  m.name = "BRIT CHIME";
  m.family = "BRIT";
  m.description = "British 30 W top-boost combo: EL84s, cathode bias, no feedback. Bright, chimey edge of breakup.";
  StageDef v1;
  v1.railV = 280.0;
  v1.raOhms = 220e3;
  v1.rkOhms = 2700.0;
  v1.ckF = 22e-6;
  InputJack(v1);
  StageDef v2;
  v2.railV = 300.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 1500.0;
  v2.ckF = 22e-6;
  v2.C(S, N1, 470e-12).Pot(N1, N2, GND, 500e3, kKnobGain, kAudio).C(N1, N2, 100e-12).R(N2, G, 1e3);
  v2.Follower(56e3, 300.0, 150e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.numStages = 2;
  m.output.C(S, N1, 47e-12).Pot(N1, N2, N3, 1e6, kKnobTreble, kAudio).R(S, N4, 100e3).C(N4, N3, 22e-9);
  m.output.C(N4, N5, 22e-9).Rheo(N3, N5, 1e6, kKnobBass, kAudio).R(N5, GND, 10e3);
  m.output.R(N2, N6, 220e3).R(N6, GND, 330e3);
  m.output.out = N6;
  m.knobLabel[kKnobGain] = "Volume";
  m.knobLabel[kKnobMid] = nullptr;
  m.knobDefault[kKnobGain] = 0.5;
  m.power = {false, true, 0.0, 0.0, 0.5, 0.6, kPowerEL84, 335.0, 3400.0, true};
  m.needs8x = false;
  m.levelDB = 0.0;
  m.outputDB = 7.3;
  return m;
}

inline ModuleDef Brit45()
{
  // Vintage British 45 W, no master volume. Both channels' first stages share one cathode (820 ohm, 250 uF):
  // modelled as one stage with its share of it. The high-treble channel: its volume pot with a 500 pF
  // bright cap, then a 270k mixing resistor into V2a (the other channel's 270k and pot load it). V2a has
  // no bypass cap; V2b is the cathode follower into a 250 pF / 56k tone stack.
  ModuleDef m;
  m.name = "BRIT 45";
  m.family = "BRIT";
  m.description = "Vintage British 45 W: two stages and a follower, no master. Loose, warm, round crunch.";
  StageDef v1;
  v1.railV = 300.0;
  v1.raOhms = 100e3;
  v1.rkOhms = 1640.0;
  v1.ckF = 125e-6;
  InputJack(v1);
  StageDef v2;
  v2.railV = 300.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 820.0;
  v2.C(S, N1, 20e-9).Pot(N1, N2, GND, 1e6, kKnobGain, kAudio).C(N1, N2, 500e-12).R(N2, N3, 270e3);
  v2.R(N3, GND, 770e3).R(N3, G, 1e3);
  v2.Follower(100e3, 310.0, 100e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.numStages = 2;
  m.output.Stack(S, N1, N2, N3, N4, N5, 270e-12, 20e-9, 20e-9, 250e3, 1e6, 25e3, 56e3).R(N1, GND, 1e6);
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Volume";
  m.knobDefault[kKnobGain] = 0.7;
  m.power = {true, false, 0.55, 0.5, 0.75, 0.7}; // KT66-type beam tubes (a 6L6 here), a pair, tube rectifier
  m.needs8x = false;
  m.levelDB = -11.3;
  return m;
}

inline ModuleDef Brit100()
{
  // British 100 W lead head, late 60s: the bright channel (2.7k / 0.68 uF on its first stage, 0.022 uF
  // coupling, 1M volume with a 5 nF bright cap, 470k mixing resistor bypassed by 500 pF), V2a at 820 ohm /
  // 0.68 uF, the cathode follower into a 500 pF / 33k tone stack, no master.
  ModuleDef m;
  m.name = "BRIT 100";
  m.family = "BRIT";
  m.description = "British 100 W lead, late 60s: bright channel, no master. Open, aggressive, classic rock.";
  StageDef v1;
  v1.railV = 290.0;
  v1.raOhms = 100e3;
  v1.rkOhms = 2700.0;
  v1.ckF = 0.68e-6;
  InputJack(v1);
  StageDef v2;
  v2.railV = 290.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 820.0;
  v2.ckF = 0.68e-6;
  v2.C(S, N1, 22e-9).Pot(N1, N2, GND, 1e6, kKnobGain, kAudio).C(N1, N2, 5e-9).R(N2, N3, 470e3).C(N2, N3, 500e-12);
  v2.R(N3, GND, 720e3).R(N3, G, 1e3);
  v2.Follower(100e3, 300.0, 100e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.numStages = 2;
  m.output.Stack(S, N1, N2, N3, N4, N5, 500e-12, 22e-9, 22e-9, 220e3, 1e6, 25e3, 33e3).R(N1, GND, 1e6);
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Volume";
  m.knobDefault[kKnobGain] = 0.75;
  m.power = {false, true, 0.5, 0.5, 0.8, 0.5};
  m.needs8x = false;
  m.levelDB = -12.3;
  return m;
}

inline ModuleDef BritMV()
{
  // British master-volume lead, 80s (100 W): V1a 2.7k / 0.68 uF with 100 pF from plate to cathode;
  // 470k || 470 pF into the 1M preamp volume with 1 nF across its top; V1b with an unbypassed 10k cathode
  // (the cold clipper); 470k || 470 pF over 470k into V2a (820 ohm, unbypassed); V2b the follower into a
  // 470 pF / 33k tone stack, then the 1M master.
  ModuleDef m;
  m.name = "BRIT MV";
  m.family = "BRIT";
  m.description = "British master-volume lead, 80s: a cold clipper stage, tight and cutting. The hard-rock classic.";
  StageDef v1;
  v1.railV = 300.0;
  v1.raOhms = 100e3;
  v1.rkOhms = 2700.0;
  v1.ckF = 0.68e-6;
  v1.plateCapF = 100e-12;
  InputJack(v1);
  StageDef v2;
  v2.railV = 300.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 10e3;
  v2.C(S, N1, 22e-9).R(N1, N2, 470e3).C(N1, N2, 470e-12).Pot(N2, N3, GND, 1e6, kKnobGain, kAudio).C(N2, N3, 1e-9);
  v2.R(N3, G, 1e3);
  StageDef v3;
  v3.railV = 310.0;
  v3.raOhms = 100e3;
  v3.rkOhms = 820.0;
  v3.C(S, N1, 22e-9).R(N1, N2, 470e3).C(N1, N2, 470e-12).R(N2, GND, 470e3).R(N2, G, 1e3);
  v3.Follower(100e3, 320.0, 100e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.stage[2] = v3;
  m.numStages = 3;
  m.output.Stack(S, N1, N2, N3, N4, N5, 470e-12, 22e-9, 22e-9, 220e3, 1e6, 22e3, 33e3).R(N1, GND, 1e6);
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Preamp";
  m.knobDefault[kKnobGain] = 0.7;
  m.power = {false, true, 0.5, 0.5, 0.5, 0.5};
  m.needs8x = false;
  m.levelDB = -12.4;
  return m;
}

inline ModuleDef Brit90()
{
  // British 90s lead (the "SL" style 100 W): V1a 220k / 2.7k / 3.3 uF; 470k || 470 pF into Gain (1M with
  // 1 nF across its top); a second stage on 27k with 2.7k unbypassed (a low-gain driver); 470k || 470 pF into
  // the second gain pot (1M, 1M after it); V2a on 100k || 1 nF with 820 ohm; 10 nF, 220k over 220k into
  // V3a (220k, 2.7k); V3b the follower into a 470 pF / 33k stack, then 680k + 68k (the loop's input).
  ModuleDef m;
  m.name = "BRIT 90";
  m.family = "BRIT";
  m.description = "British 90s lead: four stages, two gain controls. Thick, saturated, more gain than the 80s classic.";
  StageDef v1;
  v1.railV = 310.0;
  v1.raOhms = 220e3;
  v1.rkOhms = 2700.0;
  v1.ckF = 3.3e-6;
  v1.plateCapF = 100e-12;
  InputJack(v1);
  StageDef v2;
  v2.railV = 300.0;
  v2.raOhms = 27e3;
  v2.rkOhms = 2700.0;
  v2.C(S, N1, 22e-9).R(N1, N2, 470e3).C(N1, N2, 470e-12).Pot(N2, N3, GND, 1e6, kKnobGain, kAudio).C(N2, N3, 1e-9);
  v2.R(N3, G, 1e3);
  StageDef v3;
  v3.railV = 300.0;
  v3.raOhms = 100e3;
  v3.plateCapF = 1e-9;
  v3.rkOhms = 820.0;
  v3.C(S, N1, 22e-9).R(N1, N2, 470e3).C(N1, N2, 470e-12).Pot(N2, N3, GND, 1e6, kKnobGain2, kAudio);
  v3.R(N3, GND, 1e6).R(N3, G, 1e3);
  StageDef v4;
  v4.railV = 310.0;
  v4.raOhms = 220e3;
  v4.rkOhms = 2700.0;
  v4.C(S, N1, 10e-9).R(N1, N2, 220e3).R(N2, GND, 220e3).R(N2, G, 1e3);
  v4.Follower(100e3, 320.0, 100e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.stage[2] = v3;
  m.stage[3] = v4;
  m.numStages = 4;
  m.output.Stack(S, N1, N2, N3, N4, N5, 470e-12, 22e-9, 22e-9, 220e3, 220e3, 22e3, 33e3).R(N1, GND, 748e3);
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Gain";
  m.knobLabel[kKnobGain2] = "Lead";
  m.knobDefault[kKnobGain] = 0.6;
  m.knobDefault[kKnobGain2] = 0.6;
  m.power = {false, true, 0.5, 0.5, 0.5, 0.5};
  m.levelDB = -10.9;
  return m;
}

inline ModuleDef BritUltra()
{
  // British late-90s lead channel: V1 (2.7k, unbypassed) into Gain (1M), V2 (2.7k / 0.68 uF), 680k || 5 nF
  // over 100k into V3 (3.3k, unbypassed), and V3's plate driving the 470 pF / 33k tone stack directly (no
  // follower before it), then 22 nF into a 1M grid (the next tube, which only buffers).
  ModuleDef m;
  m.name = "BRIT ULTRA";
  m.family = "BRIT";
  m.description = "British late-90s lead: three stages, the stack driven from a plate. Modern, compressed, smooth.";
  StageDef v1;
  v1.railV = 300.0;
  v1.raOhms = 100e3;
  v1.rkOhms = 2700.0;
  InputJack(v1);
  StageDef v2;
  v2.railV = 300.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 2700.0;
  v2.ckF = 0.68e-6;
  v2.C(S, N1, 22e-9).Pot(N1, N2, GND, 1e6, kKnobGain, kAudio).R(N2, G, 1e3);
  StageDef v3;
  v3.railV = 310.0;
  v3.raOhms = 100e3;
  v3.rkOhms = 3300.0;
  v3.C(S, N1, 22e-9).R(N1, N2, 680e3).C(N1, N2, 5e-9).R(N2, GND, 100e3).R(N2, G, 1e3);
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.stage[2] = v3;
  m.numStages = 3;
  m.output.Stack(S, N1, N2, N3, N4, N5, 470e-12, 22e-9, 22e-9, 250e3, 1e6, 25e3, 33e3);
  m.output.C(N1, N6, 22e-9).R(N6, GND, 1e6);
  m.output.out = N6;
  m.knobLabel[kKnobGain] = "Gain";
  m.knobDefault[kKnobGain] = 0.6;
  m.power = {false, true, 0.5, 0.5, 0.5, 0.5};
  m.needs8x = false;
  m.levelDB = -6.3;
  return m;
}

inline ModuleDef HGSix()
{
  // American high gain, early 90s, lead channel: six cascaded 12AX7 stages.
  // V1A 220k, 1.82k / 1 uF. Into the lead pre gain: 22 nF, 470k || 2.2 nF, the 1M pot (1M across it, and
  // 1 nF and 1M across its top). V1B 100k, 1.82k / 1 uF. 22 nF and 470k into V2A (1M grid leak), 100k with
  // an unbypassed 39k cathode: the cold clipper. 22 nF with 1 nF and 330k to ground, then 220k into V2B
  // (220k, 1.82k / 1 uF). 1M before 22 nF into V5B (100k grid leak; 220k, 2.2k unbypassed). 1M, 22 nF into
  // V5A (1M grid leak; 100k, 2.2k unbypassed), whose output (22 nF, 470k to ground) feeds back to its own
  // grid through 1M, and drives the tone stack (470 pF / 47k, 250k / 1M / 50k) straight from there; the
  // 1M post gain loads it.
  ModuleDef m;
  m.name = "HG SIX";
  m.family = "HG";
  m.description = "American high gain, early 90s: six cascaded stages, a cold clipper, a plate-driven stack. Huge "
                  "and tight.";
  StageDef v1;
  v1.railV = 330.0;
  v1.raOhms = 220e3;
  v1.rkOhms = 1820.0;
  v1.ckF = 1e-6;
  InputJack(v1);
  StageDef v2;
  v2.railV = 330.0;
  v2.raOhms = 100e3;
  v2.rkOhms = 1820.0;
  v2.ckF = 1e-6;
  v2.C(S, N1, 22e-9).R(N1, N2, 470e3).C(N1, N2, 2.2e-9).Pot(N2, N3, GND, 1e6, kKnobGain, kAudio);
  v2.R(N2, GND, 1e6).C(N2, N3, 1e-9).R(N2, N3, 1e6).R(N3, G, 1e3);
  StageDef v3;
  v3.railV = 330.0;
  v3.raOhms = 100e3;
  v3.rkOhms = 39e3;
  v3.C(S, N1, 22e-9).R(N1, N2, 470e3).R(N2, GND, 1e6).R(N2, G, 1e3);
  StageDef v4;
  v4.railV = 330.0;
  v4.raOhms = 220e3;
  v4.rkOhms = 1820.0;
  v4.ckF = 1e-6;
  v4.C(S, N1, 22e-9).C(N1, GND, 1e-9).R(N1, GND, 330e3).R(N1, G, 220e3);
  StageDef v5;
  v5.railV = 340.0;
  v5.raOhms = 220e3;
  v5.rkOhms = 2200.0;
  v5.R(S, N1, 1e6).C(N1, N2, 22e-9).R(N2, GND, 100e3).R(N2, G, 1e3);
  StageDef v6;
  v6.railV = 340.0;
  v6.raOhms = 100e3;
  v6.rkOhms = 2200.0;
  v6.R(S, N1, 1e6).C(N1, N2, 22e-9).R(N2, GND, 1e6).R(N2, G, 1e3);
  v6.C(P, N3, 22e-9).R(N3, GND, 470e3).R(N2, N3, 1e6);
  v6.out = N3;
  m.stage[0] = v1;
  m.stage[1] = v2;
  m.stage[2] = v3;
  m.stage[3] = v4;
  m.stage[4] = v5;
  m.stage[5] = v6;
  m.numStages = 6;
  m.output.Stack(S, N1, N2, N3, N4, N5, 470e-12, 22e-9, 22e-9, 250e3, 1e6, 50e3, 47e3).R(N1, GND, 1e6);
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Pre Gain";
  m.knobDefault[kKnobGain] = 0.55;
  m.power = {true, true, 0.5, 0.5, 0.4, 0.45};
  m.levelDB = -13.1;
  return m;
}

// The American "cascade" lead preamp: V1A drives the tone stack from its plate, then the volume into V1B,
// then the lead circuit (lead drive into V3B, a divider into V4A), then V2A and V2B toward the power amp.
// The graphic EQ, reverb and effects loop are left out.
inline ModuleDef Cascade(bool second)
{
  ModuleDef m;
  m.name = second ? "CAL CASCADE 2" : "CAL CASCADE";
  m.family = "CAL";
  m.description = second ? "American cascading lead, 90s: the early-80s lead circuit, tighter input and hotter lead "
                           "stages. Focused, singing, fast."
                         : "American cascading lead, early 80s: the stack right after the first tube, then five more "
                           "gain stages. Liquid, vocal leads.";
  StageDef v1a; // Input stage
  v1a.railV = 390.0;
  v1a.raOhms = 150e3;
  v1a.rkOhms = 1500.0;
  v1a.ckF = 0.47e-6;
  if (second)
    v1a.C(K, N9, 2.2e-6).R(N9, GND, 22e3);
  v1a.R(S, GND, 1e6).R(S, G, 1e3);
  StageDef v1b; // The tone stack from V1A's plate (750 pF / 100k; 250k, 250k, 10k), then Volume
  v1b.railV = 390.0;
  v1b.raOhms = 100e3;
  v1b.rkOhms = 1500.0;
  v1b.ckF = 15e-6;
  v1b.Stack(S, N1, N2, N3, N4, N5, 750e-12, 100e-9, 47e-9, 250e3, 250e3, 10e3, 100e3);
  v1b.R(S, N2, 10e6).Pot(N1, N6, GND, 1e6, kKnobGain, kAudio).R(N6, G, 1e3);
  StageDef v3b; // Lead drive
  v3b.railV = 390.0;
  v3b.raOhms = 82e3;
  v3b.rkOhms = 1500.0;
  v3b.ckF = second ? 2.2e-6 : 15e-6;
  v3b.C(S, N1, 47e-9).R(N1, GND, 1e6).R(N1, N2, 680e3).Pot(N2, N3, GND, 1e6, kKnobGain2, kAudio).R(N3, G, 1e3);
  StageDef v4a; // The lead stage after the divider: 3.3k with 0.22 uF + 22k across it, 1 nF plate cap
  v4a.railV = 390.0;
  v4a.raOhms = 270e3;
  v4a.plateCapF = second ? 500e-12 : 1e-9;
  v4a.rkOhms = 3300.0;
  v4a.C(K, N9, 0.22e-6).R(N9, GND, 22e3);
  v4a.C(S, N1, 20e-9).R(N1, N2, second ? 270e3 : 470e3).R(N2, GND, 68e3).C(N2, GND, 1e-9).R(N2, G, 1e3);
  StageDef v2a; // Lead output: 100k || 500 pF to ground, 220k || 250 pF into V2A
  v2a.railV = 390.0;
  v2a.raOhms = 100e3;
  v2a.rkOhms = 1500.0;
  v2a.ckF = second ? 15e-6 : 0.0;
  v2a.C(S, N1, 47e-9).R(N1, GND, 100e3).C(N1, GND, 500e-12).R(N1, N2, 220e3).C(N1, N2, 250e-12);
  v2a.R(N2, GND, 1.5e6).C(N2, GND, 47e-12).R(N2, G, 1e3);
  StageDef v2b; // After the lead master (fixed at half: 250k) and 47k
  v2b.railV = 390.0;
  v2b.raOhms = 120e3;
  v2b.rkOhms = 1000.0;
  v2b.ckF = 0.47e-6;
  v2b.C(S, N1, 47e-9).R(N1, GND, 125e3).R(N1, N2, 47e3).R(N2, GND, 1e6).R(N2, G, 1e3);
  m.stage[0] = v1a;
  m.stage[1] = v1b;
  m.stage[2] = v3b;
  m.stage[3] = v4a;
  m.stage[4] = v2a;
  m.stage[5] = v2b;
  m.numStages = 6;
  m.output.C(S, N1, 47e-9).R(N1, GND, 100e3); // The 100k master
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Volume";
  m.knobLabel[kKnobGain2] = "Lead Drive";
  m.knobDefault[kKnobGain] = 0.6;
  m.knobDefault[kKnobGain2] = 0.6;
  m.power = {true, second ? false : true, 0.5, 0.5, 0.45, 0.4};
  m.levelDB = second ? -24.7 : -24.6;
  return m;
}

// American modern high gain, mid 90s, the red channel: V1A (220k; 1.8k / 1 uF, or in Vintage the 1 uF in
// series with 47k), 20 nF and the input pad (680k || 2 nF with 2.2M || 82 pF, 2.2M to ground) into Gain
// (1M, 1 nF across its top), 470k and 20 pF into V2A (100k; cathode as V1A), 20 nF and 470k over 1M into
// V2B (100k || 1 nF, 39k unbypassed: the cold clipper), 20 nF over 330k and a 220k stopper into V3A (220k,
// 1.8k / 1 uF), V3B the follower into the 680 pF / 47k stack (250k, 1M, 25k), the master (1M) with the
// channel's presence network on it (22k + 3 nF + the 25k pot, at half). Vintage also switches in the
// treble roll-off (3 nF + 10k) and the power amp's negative feedback.
inline ModuleDef CalRect(bool vintage)
{
  ModuleDef m;
  m.name = vintage ? "CAL VINTAGE" : "CAL MODERN";
  m.family = "CAL";
  m.description = vintage ? "American modern high gain, vintage mode: looser input stages, smoother top, feedback on. "
                            "Thick and saggy."
                          : "American modern high gain, mid 90s: a cold clipper, no feedback, tube rectifier. Wide, "
                            "loose low end and a raw top.";
  auto cathode = [vintage](StageDef& s) {
    s.rkOhms = 1800.0;
    if (vintage)
      s.C(K, N9, 1e-6).R(N9, GND, 47e3);
    else
      s.ckF = 1e-6;
  };
  StageDef v1a;
  v1a.railV = 400.0;
  v1a.raOhms = 220e3;
  cathode(v1a);
  v1a.R(S, GND, 1e6).R(S, G, 1e3);
  StageDef v2a;
  v2a.railV = 400.0;
  v2a.raOhms = 100e3;
  cathode(v2a);
  v2a.C(S, N1, 20e-9).R(N1, GND, 2.2e6).R(N1, N2, 520e3).C(N1, N2, 2.08e-9);
  v2a.Pot(N2, N3, GND, 1e6, kKnobGain, kAudio).C(N2, N3, 1e-9).R(N3, N4, 470e3).C(N4, GND, 20e-12).R(N4, G, 1e3);
  StageDef v2b;
  v2b.railV = 400.0;
  v2b.raOhms = 100e3;
  v2b.plateCapF = 1e-9;
  v2b.rkOhms = 39e3;
  v2b.C(S, N1, 20e-9).R(N1, N2, 470e3).R(N2, GND, 1e6).R(N2, G, 1e3);
  StageDef v3a;
  v3a.railV = 420.0;
  v3a.raOhms = 220e3;
  v3a.rkOhms = 1800.0;
  v3a.ckF = 1e-6;
  v3a.C(S, N1, 20e-9).R(N1, GND, 330e3).R(N1, G, 220e3);
  v3a.Follower(100e3, 415.0, 100e3);
  m.stage[0] = v1a;
  m.stage[1] = v2a;
  m.stage[2] = v2b;
  m.stage[3] = v3a;
  m.numStages = 4;
  m.output.Stack(S, N1, N2, N3, N4, N5, 680e-12, 20e-9, 20e-9, 250e3, 1e6, 25e3, 47e3).R(N1, GND, 1e6);
  m.output.R(N1, N6, 22e3).C(N6, N7, 3e-9).R(N7, GND, 12.5e3);
  if (vintage)
    m.output.C(N1, N8, 3e-9).R(N8, GND, 10e3);
  m.output.out = N1;
  m.knobLabel[kKnobGain] = "Gain";
  m.knobDefault[kKnobGain] = 0.6;
  m.power = {true, true, vintage ? 0.5 : 0.0, 0.5, 0.4, 0.65};
  m.needs8x = !vintage;
  m.levelDB = vintage ? -12.3 : -11.2;
  return m;
}
} // namespace detail

// Every module, indexed by ModuleId (index 0, the Tube Lab, is an empty placeholder).
inline const ModuleDef* Modules()
{
  static const ModuleDef* defs = [] {
    static ModuleDef d[kNumModules];
    d[kModuleLab].name = "TUBE LAB";
    d[kModuleLab].family = "LAB";
    d[kModuleLab].description = "The Tube Lab's own preamp: V1, V2, the sockets and the follower, set on its tabs.";
    d[kBrit45] = detail::Brit45();
    d[kBrit100] = detail::Brit100();
    d[kBritMV] = detail::BritMV();
    d[kBrit90] = detail::Brit90();
    d[kBritUltra] = detail::BritUltra();
    d[kHGSix] = detail::HGSix();
    d[kCalCascade] = detail::Cascade(false);
    d[kCalCascade2] = detail::Cascade(true);
    d[kCalModern] = detail::CalRect(false);
    d[kCalVintage] = detail::CalRect(true);
    d[kUsClean] = detail::UsClean();
    d[kUsTweed] = detail::UsTweed();
    d[kUs4x10] = detail::Us4x10();
    d[kBritChime] = detail::BritChime();
    return d;
  }();
  return defs;
}
inline const ModuleDef& GetModule(int id) { return Modules()[std::clamp(id, 0, kNumModules - 1)]; }
} // namespace module
} // namespace tube
