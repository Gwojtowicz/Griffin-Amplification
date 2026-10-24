#pragma once

// Hover help for every knob and switch (1.65.0): what each control does, in a sentence or two. The
// plugin sets it as the tooltip of any control that has a param and no tooltip of its own (see
// NeuralAmpModeler::_ParamHelp, which also maps the effect pedals' extra copies, the EQ bands and the
// SPEAKER LAB mics onto these). Needs the EParams enum (include after NeuralAmpModeler.h).

#include <string>

inline const char* BaseParamHelp(int p)
{
  switch (p)
  {
    // Master / AMP block ---------------------------------------------------------------------------
    case kInputLevel:
      return "Master In: the level into the whole chain, before anything else (dB). Turn it down if your "
             "guitar is hot, up if it's weak. It sets how hard every amp and pedal is hit.";
    case kNoiseGateThreshold:
      return "Noise gate threshold (dBFS): below this, the amp's gate closes and silences the hiss between "
             "notes. Lower = more open (keeps quiet notes, lets more hiss through); higher = tighter.";
    case kNoiseGateActive: return "AMP noise gate on/off. Its detector listens to your guitar at IN.";
    case kToneBass:
      return "AMP Bass: the tone stack's low end after the capture (around 100-200 Hz). Up for weight, down "
             "for tightness at high gain.";
    case kToneMid:
      return "AMP Mid: the tone stack's mids (around 500 Hz-1 kHz). Up to cut through a band mix, down for a "
             "scooped sound.";
    case kToneTreble:
      return "AMP Treble: the tone stack's top end (around 2-5 kHz). Up for bite and clarity, down to tame "
             "fizz and harshness.";
    case kEQActive: return "AMP EQ on/off: the Bass / Mid / Treble tone stack after the capture.";
    case kOutputLevel:
      return "Master Out: the level out of the whole chain (dB). If the OUT meter shows CLIP, turn this down "
             "or switch the Limiter on.";
    case kIRToggle: return "CAB on/off: the cab IR after the amp. Off passes the signal through.";
    case kCalibrateInput:
      return "Calibrate input: match your interface's real input level to the level the capture was made "
             "at, so the capture responds as the real amp did.";
    case kInputCalibrationLevel:
      return "Your interface's input level for a full-scale signal (dBu), for Calibrate input.";
    case kOutputMode: return "How the output level is handled: raw, normalized, or calibrated to the capture.";
    case kDensity:
      return "Density: a low-end shelf (200-500 Hz) after the amp. Up for a thicker, fuller low end; down to "
             "clear out mud.";
    case kDensityActive: return "Density on/off.";
    case kPresence:
      return "Boost: a high shelf (2-6 kHz) after the amp. Up for more bite and presence; down to darken.";
    case kPresenceActive: return "Boost on/off.";
    case kAmpEngaged: return "AMP on/off: off passes the signal straight through the AMP block.";
    case kHumFilter:
      return "Hum Filter: very narrow notches at 60 Hz and its harmonics (up to 480 Hz) remove mains hum "
             "from your guitar before the chain. Your notes are barely touched.";
    case kSlim: return "Slim: for slimmable captures, trade a little accuracy for less CPU.";
    case kLoopEngaged: return "FX loop on/off. Off, the loop passes the signal straight through.";
    case kLoopMix: return "FX loop Mix: how much of what comes back from the loop is blended in (100% = all of it).";

    // Pedals ----------------------------------------------------------------------------------------
    case kCompressorSustain:
      return "COMP Sustain: how hard it squeezes. More sustain and evenness; too much pumps and lifts the "
             "noise between notes.";
    case kCompressorLevel: return "COMP Level: its output volume.";
    case kCompressorMix: return "COMP Mix: dry/wet. Under 100% keeps your pick attack (parallel compression).";
    case kOverdriveDrive: return "OD Drive: how much gain and clipping. Low as a clean-ish boost, high for crunch.";
    case kOverdriveTone: return "OD Tone: darker (left) to brighter (right).";
    case kOverdriveLevel:
      return "OD Level: its output volume. Up to push the amp after it harder (a boost), down to match levels.";
    case kOverdriveMix: return "OD Mix: blends your clean signal back in for clarity under the drive.";
    case kChorusRate: return "CHORUS Rate: how fast it sweeps. Slow for a wide shimmer, fast for a warble.";
    case kChorusDepth: return "CHORUS Depth: how far it sweeps. More = a stronger, more seasick wobble.";
    case kChorusMix: return "CHORUS Mix: how much of the chorused signal is blended in.";
    case kDelayTime: return "DELAY Time: the gap between repeats (it crossfades when changed, so no pitch zip).";
    case kDelayFeedback: return "DELAY Feedback: how many repeats. High settings keep repeating for a long time.";
    case kDelayMix: return "DELAY Mix: how loud the repeats are against your playing.";
    case kReverbSize: return "REVERB Size: from a small room to a big hall (longer decay).";
    case kReverbMix: return "REVERB Mix: how much reverb is blended in.";
    case kEqPedalLow: return "EQ pedal Low: boost or cut the bass.";
    case kEqPedalMid: return "EQ pedal Mid: boost or cut the mids (a mid boost before an amp tightens and focuses it).";
    case kEqPedalHigh: return "EQ pedal High: boost or cut the treble.";
    case kEqPedalLevel: return "EQ pedal Level: its output volume.";
    case kTremoloRate: return "TREMOLO Rate: how fast the volume pulses.";
    case kTremoloDepth: return "TREMOLO Depth: how deep the volume dips. Full = choppy, low = a gentle throb.";
    case kTremoloMix: return "TREMOLO Mix: dry/wet.";
    case kNamPedal0Level:
    case kNamPedal1Level: return "CAPTURE pedal Level: the output volume of the loaded pedal capture.";
    case kNamPedal0Mix:
    case kNamPedal1Mix: return "CAPTURE pedal Mix: blends the pedal capture with your dry signal.";
    case kFlangerRate: return "FLANGER Rate: how fast the jet sweep moves.";
    case kFlangerDepth: return "FLANGER Depth: how wide the sweep is (0.25-7 ms of delay).";
    case kFlangerRegen:
      return "FLANGER Regen: feedback. More = a sharper, more metallic, resonant jet. Negative flips its "
             "character (hollower).";
    case kFlangerMix: return "FLANGER Mix: dry/wet (50% gives the deepest notches).";
    case kPhaserRate: return "PHASER Rate: how fast it sweeps.";
    case kPhaserDepth: return "PHASER Depth: how wide it sweeps (150 Hz to about 4 kHz).";
    case kPhaserRegen: return "PHASER Regen: feedback. More = a more vocal, resonant sweep.";
    case kPhaserMix: return "PHASER Mix: dry/wet.";
    case kSkyMode:
      return "SKY VERB Mode: Hall, Plate (dense, bright), Room (short), Shimmer (an octave up fed back), "
             "Cloud (huge, modulated) or Bloom (swells in).";
    case kSkyDecay: return "SKY VERB Decay: how long the reverb rings (RT60).";
    case kSkyPredelay: return "SKY VERB Pre-delay: a gap before the reverb starts, so your attack stays clear.";
    case kSkyTone: return "SKY VERB Tone: darker (left) to brighter (right) tail.";
    case kSkyMod: return "SKY VERB Mod: slow movement in the tail. More = lusher, chorused; none = static.";
    case kSkyMix: return "SKY VERB Mix: dry/wet.";
    case kSkyFreeze: return "SKY VERB Freeze: holds the current tail forever (an infinite pad) while new playing passes dry.";
    case kFDDoubler: return "FLANGE DBL mode: Flanger (a swept jet) or Doubler (a second, wandering 'player').";
    case kFDManual: return "FLANGE DBL Manual: the delay's centre point. Sets where the sweep sits (Flanger) or how far apart the double is.";
    case kFDWidth: return "FLANGE DBL Width: how far it sweeps or wanders.";
    case kFDSpeed: return "FLANGE DBL Speed: how fast it sweeps or wanders.";
    case kFDRegen: return "FLANGE DBL Regen: feedback (Flanger). More = a more resonant jet.";
    case kFDMix: return "FLANGE DBL Mix: dry/wet.";
    case kWahModel:
      return "WAH model: Cry Baby (classic, peaky), V847 (smoother, narrower sweep) or Auto (follows how hard "
             "you pick).";
    case kWahPosition: return "WAH Position: the pedal's position, heel (dark) to toe (bright). Map it to an expression pedal.";
    case kWahQ: return "WAH Q: how peaky the wah is. More = vocal and nasal; less = subtle.";
    case kWahSens: return "WAH Sens (Auto): how much your picking moves it.";
    case kWahLevel: return "WAH Level: its output volume.";
    case kPDVolume: return "PREC DRIVE Volume: its output level.";
    case kPDDrive: return "PREC DRIVE Drive: gain into the clipper. Low = tight boost, high = saturation.";
    case kPDAttack:
      return "PREC DRIVE Attack: how much low end goes into the drive (60-500 Hz high-pass). Up = tighter, "
             "chunkier palm mutes for high-gain amps.";
    case kPDGate: return "PREC DRIVE Gate: its built-in noise gate (keyed from your clean input).";
    case kPDBright: return "PREC DRIVE Bright: opens the top end after the drive.";
    case kApexGain: return "APEX PRE Gain: how hard its three gain stages are pushed.";
    case kApexBass: return "APEX PRE Bass.";
    case kApexMid: return "APEX PRE Mid.";
    case kApexTreble: return "APEX PRE Treble.";
    case kApexPresence: return "APEX PRE Presence: upper-mid / top bite.";
    case kApexVolume: return "APEX PRE Volume: its output level.";
    case kApexBright: return "APEX PRE Bright switch: more top end into the gain.";
    case kCab2Level:
    case kCab3Level: return "Extra cab Level: this cab's output volume (balance it against the other cabs).";
    case kCab2Pan:
    case kCab3Pan: return "Extra cab Pan: where this cab sits left to right.";

    // Tube Lab: preamp -------------------------------------------------------------------------------
    case kTubeEngaged: return "Tube Lab on/off (it sleeps and uses no CPU when off).";
    case kTubeDrive:
      return "Drive: how hard your guitar hits V1's grid (dB; real gain staging). With AMP Capture on, the "
             "gain into the capture (12 dB = unchanged).";
    case kTubeBias:
      return "Bias (V1): where the first tube idles. Toward 0 V = more gain, clips on top sooner; colder = "
             "cuts off sooner on the bottom (sputterier, asymmetric).";
    case kTubeBloom:
      return "Bloom: the input coupling cap (2.2-220 nF). Big = hard picks make the tube sag colder for a "
             "moment and swell back (bloom); small = tighter, quicker recovery.";
    case kTubeBright: return "Bright: a bright cap across the volume pot. More top end into the first tube.";
    case kTubeMix: return "Mix: dry/wet. 10 = all Tube Lab; lower blends your dry signal back in.";
    case kTubeOutput: return "Output: the Tube Lab's output level (dB). Use it to match levels with it on and off.";
    case kTubeCircuit:
      return "Circuit: the full circuit simulation (on) or the older Classic engine (off, lighter on CPU).";
    case kTubeCathodeCap:
      return "Cap: V1's cathode bypass cap. None = clean, tight, less gain; 0.68 uF = tight lows (the "
             "high-gain trick); 25 uF = full and fat.";
    case kTubePlate:
      return "Plate: the plate resistors (both stages). Bigger = more gain and squash; smaller = cleaner, "
             "more headroom.";
    case kTubePlateCap: return "Plate Cap (V1): a cap across the plate resistor that rolls off fizz. None = brightest.";
    case kTubeSagOn: return "Sag: the preamp's supply sags on hard playing (a squash and bloom).";
    case kTubeSagSupply: return "Supply: the supply's resistance. Higher = more sag.";
    case kTubeSagTime: return "Recovery: how long the supply takes to recover after a hard hit.";
    case kTubeStage2: return "Stage 2: a second, cascaded gain stage (much more gain). Runs at 8x oversampling.";
    case kTubeGain2: return "Gain 2: how much of V1 drives V2 (an audio-taper pot).";
    case kTubeBias2: return "Bias 2: where V2 idles. A little colder than V1 is the classic high-gain choice.";
    case kTubeCap2: return "Cap 2: V2's cathode bypass cap. Small = tight, big = full.";
    case kTubePlateCap2: return "Plate Cap 2: tames V2's fizz. 470 pF is the classic.";
    case kTubeCouple:
      return "Couple: the cap between the stages. Small = less low end into V2 (tight high gain); big = full lows.";
    case kTubeStack: return "Tone stack: none, Fender (scooped mids) or Marshall (mid-forward), between the preamp and power amp.";
    case kTubeBass: return "Tube Lab Bass: the tone stack's lows.";
    case kTubeMid: return "Tube Lab Mid: the tone stack's mids (Fender stacks scoop them).";
    case kTubeTreble: return "Tube Lab Treble: the tone stack's top end.";
    case kTubeTouch:
      return "Touch: softer picks get less drive, so they clean up while hard picks keep the full sound. 0 = off.";
    case kTubeCathodeFollower:
      return "Follower: a cathode follower (V3) driving the tone stack: rounds and squashes the biggest "
             "swings (Marshall, 5150).";
    case kTubeEconomy:
      return "Economy: half the oversampling, for about half the CPU. Adds aliasing grit on high-gain "
             "settings; keep it off for those.";
    case kTubeHighQuality: return "8x: twice the oversampling (cleanest top end, twice the CPU). Two-stage settings get 8x anyway.";
    case kTubeCapture:
      return "AMP Capture: the AMP block's capture becomes the Tube Lab's preamp, and the Tube Lab's power "
             "amp, bench and feel work on it.";

    // Tube Lab: power amp ------------------------------------------------------------------------------
    case kTubePower: return "Power Amp: the push-pull power amp (phase inverter, two power tubes, output transformer).";
    case kTubeMaster: return "Master: how hard the power tubes are driven. Up = power-amp breakup and compression.";
    case kTubePowerSag: return "Sag: the power supply's softness. Higher = more squash and bloom when you dig in (tube rectifier feel).";
    case kTubeFeedback:
      return "Feedback: negative feedback. More = tighter, cleaner, flatter; less = looser, rawer, more "
             "aggressive (vintage amps had little).";
    case kTubePresence: return "Presence: less feedback at the top, so more bite and sparkle.";
    case kTube6L6: return "6L6: 6L6 power tubes (American: clean, round lows) instead of EL34s (British: mid bark, earlier breakup).";
    case kTubeSpeakerLoad:
      return "Spkr Load: the power tubes drive a real speaker's impedance (a bump at the cone's resonance, a "
             "rising top) instead of a resistor.";
    case kTubeCathodeBias: return "Cath Bias: cathode-biased power tubes (AC30, Deluxe): they bias themselves colder as you dig in.";
    case kTubeSpeakerFeedback: return "Spkr FB: feedback taken from the speaker side of the transformer, as in real amps.";
    case kTubeResonance: return "Resonance: less feedback in the lows (around 150 Hz), so the low end and speaker thump come back (Mesa / 5150 'Depth').";

    // Tube Lab: feel ------------------------------------------------------------------------------------
    case kTubeScreenSquish: return "Squish: the screens sag on big swings, so the power tubes' gain drops: spongy compression.";
    case kTubeSharedSupply: return "Shared B+: the preamp sags with the power amp's supply, as in a real amp.";
    case kTubeOTSaturation: return "OT Iron: the output transformer saturates on big low notes: compressed, harmonically rich lows.";
    case kTubeCoilHeat: return "Coil Heat: the speaker's voice coil heats up over seconds of loud playing (power compression).";
    case kTubeFeelDepth: return "Depth: how strongly the feel switches act. 1x = real parts, up to 10x exaggerated.";
    case kTubeRipple: return "60 Hz: supply ripple and hum, the sound of a real amp's power supply.";
    case kTubeRippleAmount: return "60 Hz Amount: how much ripple: at 10, a starved, gritty grind.";
    case kTubeRippleMix: return "60 Hz Mix: dry/wet of the grind.";
    case kRealFeelMix: return "REAL FEEL Mix: how much of its effect you hear (100% = all).";

    // Tube Lab: bench -------------------------------------------------------------------------------------
    case kTubeVariac:
      return "Variac: the wall voltage (120 V stock). Down = less headroom, saggier, cooler heaters (the "
             "'brown sound'); up = more headroom, hotter.";
    case kTubeBiasPot:
      return "Bias pot: the power tubes' idle current (% of stock). Cold = crossover grit; hot = smoother, "
             "fuller, until they red-plate.";
    case kTubePreampAge: return "Preamp tube age: 0% new .. 100% worn out (less gain, rounded peaks).";
    case kTubePowerAge: return "Power tube age: 0% new .. 100% worn out (less gain, a softer, compressed attack).";
    case kTubeCanFail: return "Power tubes can fail: red-plated past their rating, they wear out and die with a bang.";

    // CAB LAB ---------------------------------------------------------------------------------------------
    case kCabLabEngaged: return "CAB LAB on/off.";
    case kCabSource: return "IR: use CAB LAB's loaded IR as the speaker instead of the modelled one.";
    case kCabSpeaker: return "Speaker: the speaker voicing (V30, Greenback, G12H, Alnico Blue, Jensen, EVM, P10R).";
    case kMicAType:
    case kMicBType: return "Mic: its frequency response, polar pattern and proximity effect.";
    case kMicAPos:
    case kMicBPos: return "Position: 0 = on the dust cap (brightest) .. 10 = the cone edge (darkest).";
    case kMicADist:
    case kMicBDist: return "Distance: how far from the grille. Closer = more bass (proximity) and detail; farther = more of the whole cab.";
    case kMicALevel:
    case kMicBLevel: return "Mic Level: this mic's volume in the blend.";
    case kMicAPan:
    case kMicBPan: return "Mic Pan: where this mic sits left to right.";
    case kMicBOn: return "Mic B on/off.";
    case kMicBInvert: return "Flip B: Mic B's polarity. Try it when two mics thin each other out.";
    case kRoomLevel: return "Room: how much of the room mics are blended in.";
    case kRoomSize: return "Size: the room's size and decay.";
    case kRoomWidth: return "Width: how wide the room mics' stereo spread is.";
    case kCabOutput: return "CAB LAB Output: its output level.";
    case kCabLab3D: return "3D: the SPEAKER LAB's 3D mic placement (off: the classic Mic A / Mic B knobs).";
    case kCabFormat: return "Cab size: 4x12 (closed back), 2x12, 1x12, 4x10 (open back).";
    case kCabDual: return "Cabs: one cab, or two side by side.";
    case kCab2Format: return "Cab 2 size.";
    case kCab2Speaker: return "Cab 2 speaker voicing.";
    case kCab2SourceIR: return "Cab 2 IR: use cab 2's own loaded IR as its speaker.";

    // PRO EQ / Output EQ -------------------------------------------------------------------------------
    case kProEqOutput:
    case kOutEqOutput: return "EQ Output: the level after the EQ.";
    case kProEqTrim:
    case kOutEqTrim: return "EQ Trim: the level into the EQ, before the bands.";
    case kOutEqOn: return "Output EQ on/off: off, it's out of the path (your bands are kept).";

    // GATE / PRO COMP -------------------------------------------------------------------------------------
    case kGateThreshold: return "GATE Threshold: below this the gate closes. Set it just over the hiss between notes.";
    case kGateHysteresis: return "GATE Hysteresis: how far under the threshold it waits before closing, so held notes don't chatter.";
    case kGateAttack: return "GATE Attack: how fast it opens. Fast keeps the pick attack.";
    case kGateHold: return "GATE Hold: how long it stays open after the signal drops.";
    case kGateRelease: return "GATE Release: how fast it closes (short = tight stops, long = natural decay).";
    case kGateRange: return "GATE Range: how far it turns down when closed (full = silence).";
    case kGateKey: return "GATE Key Input: listen to your clean guitar (from IN) instead of the signal it gates. Best after a high-gain amp.";
    case kGateKeyHpf: return "GATE Key HPF: ignores low rumble in the detector.";
    case kSCompThreshold: return "PRO COMP Threshold: where compression starts.";
    case kSCompRatio: return "PRO COMP Ratio: how strongly it compresses above the threshold.";
    case kSCompAttack: return "PRO COMP Attack: slower lets the pick's transient through (punch); faster clamps it.";
    case kSCompRelease: return "PRO COMP Release: how fast it lets go.";
    case kSCompKnee: return "PRO COMP Knee: soft = gradual and transparent, hard = obvious grab.";
    case kSCompMakeup: return "PRO COMP Makeup: gain after compression.";
    case kSCompAutoMakeup: return "PRO COMP Auto makeup: matches the level automatically.";
    case kSCompMix: return "PRO COMP Mix: dry/wet (parallel compression).";
    case kSCompStyle: return "PRO COMP Style: Clean (transparent), Opto (smooth, program-dependent) or FET (fast, aggressive).";
    case kSCompScHpf: return "PRO COMP sidechain HPF: stops the low end from triggering the compression (less pumping).";

    // OUTPUT -------------------------------------------------------------------------------------------
    case kOutputLimiter: return "Limiter: catches pick peaks at the Ceiling so you can turn Master Out up without clipping.";
    case kLimiterCeiling: return "Ceiling: the most the output can reach (dBFS).";
    case kLimiterRelease: return "Release: how fast the limiter lets go. Longer = smoother but can pump.";
    case kOutputWidth: return "Width: 0% mono, 100% as is, 200% twice as wide.";
    case kOutputWiden: return "Widen: stereo from a mono chain (the mono sum is unchanged).";
    // 1.66.0 pedals --------------------------------------------------------------------------------------
    case kTGModel:
      return "TRACK GATE Model. Tracking (after the ISP Decimator): the release follows how fast your note is dying, so "
             "sustains ring out and stops are instant. Classic (after the Boss NS-2): a plain threshold and decay. Smart "
             "(after the MXR Smart Gate): gates only the noisy part -- see Noise.";
    case kTGThreshold: return "TRACK GATE Threshold: below this the gate closes. Set it just over the hiss between notes.";
    case kTGRelease:
      return "TRACK GATE Release. Classic: the decay, 5 ms to 1.5 s. Tracking / Smart: how much longer than your note's "
             "own fade it waits (low = tight, high = natural).";
    case kTGHold: return "TRACK GATE Hold: how long it stays open after the signal drops under the threshold.";
    case kTGRange: return "TRACK GATE Range: how far down it goes when closed. Silent is fully off; -20 dB just tidies.";
    case kTGRatio:
      return "TRACK GATE Ratio: under the threshold the level falls this many dB per dB (a downward expander). Gate (20:1) "
             "is a hard gate; 2:1-4:1 is a gentle, natural fade.";
    case kTGNoise:
      return "TRACK GATE Noise (Smart model): Hiss gates only above 3 kHz (keeps the body of a fading chord), Mid above "
             "250 Hz, Full everything.";
    case kTGKey:
      return "TRACK GATE Key In: listen to your clean guitar (from IN) instead of the signal it gates. Best after a "
             "high-gain amp.";
    case kDigiMode:
      return "DIGI DELAY Mode. Standard: clean repeats (the DD-3's ~7 kHz bandwidth). Mod: chorused repeats. Warm: darker, "
             "softly saturating repeats. Reverse: each window played backwards (one repeat; Feedback has no effect).";
    case kDigiTime: return "DIGI DELAY Time: 20 ms (slapback) to 1.2 s.";
    case kDigiFeedback: return "DIGI DELAY Feedback: the number of repeats. Near 10 they go on and on.";
    case kDigiLevel: return "DIGI DELAY Level: how loud the repeats are (your dry signal is unchanged).";
    case kAnaTime:
      return "ANALOG DLY Delay: 20-600 ms. Longer is darker, as on a bucket-brigade chip. Turning it while it repeats "
             "swoops the pitch.";
    case kAnaRegen: return "ANALOG DLY Regen: the number of repeats. Past about 9 it builds up and self-oscillates.";
    case kAnaMix: return "ANALOG DLY Mix: the repeats' level (your dry signal is unchanged).";
    case kAnaMod: return "ANALOG DLY Mod: a slow wobble in the repeats (the Carbon Copy's Mod switch).";
    case kAnaRate: return "ANALOG DLY Rate: the Mod wobble's speed.";
    case kAnaWidth: return "ANALOG DLY Width: the Mod wobble's depth.";
    case kTapeHeads:
      return "TAPE ECHO Heads: which playback heads are on. Head 2 is twice head 1's delay, head 3 three times; combine "
             "them for rhythms.";
    case kTapeRate:
      return "TAPE ECHO Rate: the tape speed. Slow = long, dark echoes (head 1 300 ms); fast = short and bright (70 ms). "
             "Turning it swoops.";
    case kTapeIntensity: return "TAPE ECHO Intensity: the feedback. Up high it runs away into the famous oscillation.";
    case kTapeEcho: return "TAPE ECHO Echo: the echoes' level.";
    case kTapeReverb: return "TAPE ECHO Reverb: the built-in spring's level.";
    case kTapeWow: return "TAPE ECHO Wow: the worn tape's pitch drift (wow) and flutter.";
    case kTapeDrive: return "TAPE ECHO Saturate: how hard the tape is recorded: warmer, more compressed repeats.";
    case kSprDwell:
      return "SPRING VERB Dwell: the drive into the springs. Up high it gets louder, grittier and splashes when you dig in.";
    case kSprTone: return "SPRING VERB Tone: dark to bright drip.";
    case kSprMix: return "SPRING VERB Mix: how much reverb.";
    case kSprDecay: return "SPRING VERB Decay: how long the springs ring (about 1 to 4 s).";
    case kTempoBPM:
      return "Tempo (BPM). Tap TAP (or the footswitch you MIDI-learned to it) to set it, drag up or down, or "
             "double-click to type it. Each delay's Tap setting follows it.";
    case kDelayTapDiv:
    case kDelay2TapDiv:
    case kDelay3TapDiv:
    case kDigiTapDiv:
    case kAnaTapDiv:
    case kTapeTapDiv:
      return "Tap: the note value this delay takes from the tempo when you tap (or change the BPM). 1/8. is a "
             "dotted eighth, 1/4T a quarter-note triplet. Off: taps leave its time alone. A time past the "
             "delay's range is halved (or doubled) until it fits, so it stays in time.";
    default: return nullptr;
  }
}

// An EQ band's param (PRO EQ or Output EQ): which band, and what that field does.
inline std::string EqBandHelp(bool output, int band, int field, bool ext)
{
  const std::string eq = output ? "Output EQ" : "PRO EQ";
  const std::string b = eq + " band " + std::to_string(band + 1) + " ";
  if (ext)
    return field == 0 ? b + "Type: Bell (boost or cut around Freq), low / high shelf, low / high cut, or a Notch."
                      : b + "Slope: how steep a cut is (12-48 dB per octave).";
  switch (field)
  {
    case 0: return b + "on/off.";
    case 1: return b + "Freq: the frequency it works on (lows on the left, highs on the right).";
    case 2: return b + "Gain: boost (+) or cut (-) at Freq, in dB.";
    default: return b + "Q: how narrow the band is. High = a narrow, surgical notch or peak; low = broad and musical.";
  }
}

// A SPEAKER LAB mic's param.
inline std::string LabMicHelp(int mic, int field)
{
  const std::string m = std::string("Mic ") + char('A' + mic) + " ";
  switch (field)
  {
    case 0: return m + "on/off.";
    case 1: return m + "type: its response, polar pattern and proximity effect.";
    case 2: return m + "X: across the baffle (cm from the cab's centre). Between two cones, they comb-filter.";
    case 3: return m + "Y: up and down (cm).";
    case 4: return m + "Z: out from the grille (cm). Closer = more bass and detail, farther = more of the whole cab.";
    case 5: return m + "Level.";
    case 6: return m + "Pan.";
    default: return m + "Flip: polarity. Try it when two mics thin each other out.";
  }
}
