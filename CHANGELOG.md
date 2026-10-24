# Griffin Amplification changelog

The version shows in the main window's bottom-right corner, and on Settings > About. It comes from
`PLUG_VERSION_STR` / `PLUG_VERSION_HEX` in `NeuralAmpModeler/config.h`. Bump both for every build
that gets used, and add an entry here.

- **Major** (2.0.0): saved presets/sessions from older versions no longer load correctly.
- **Minor** (1.8.0): new features.
- **Patch** (1.8.1): fixes only.

## 1.71.1 (2026-10-23)

- **The title fits again.** 1.71.0's TAP button and BPM counter narrowed the space for the title, and
  at its fixed size "GRIFFIN AMPLIFICATION" was cut off at both ends. It now shrinks to fit.

## 1.71.0 (2026-10-22)

- **Tap tempo and a BPM counter** in the header, left of MIDI.
  - **TAP**: tap two or more times on the beat. The tempo is the average of the last four gaps, and
    taps more than 2 s apart start a new count.
  - **MIDI**: TAP can be MIDI-learned like any switch (click MIDI, click TAP, press a footswitch).
    Every message is a tap, so latching footswitches work. MIDI taps are timed when
    they arrive on the audio thread, not when the screen updates. Saved in midimap.txt as `tap`.
  - **BPM counter** (40-300 BPM): shows the tempo with a light that flashes on the beat. Drag it up or
    down (Shift for fine steps) or double-click it to type a value; the delays follow either way.
- **Each delay has a Tap control** (DELAY, DELAY 2, DELAY 3, DIGI DELAY, ANALOG DLY, TAPE ECHO): Off,
  1/4 (the default), 1/8. (dotted), 1/8, 1/4T (triplet) or 1/16. A tap, or a BPM change, sets the
  delay's Time knob to that note value; Off leaves the delay alone. You can still turn the Time knob
  by hand afterwards, and the next tap sets it again.
  - A time past the delay's range is halved or doubled until it fits, so it stays in time (ANALOG DLY
    tops out at 600 ms, so at 90 BPM a quarter note becomes an eighth).
  - TAPE ECHO puts its longest active head on the note (head 3 for 1+2+3) by setting Rate. Head 1's
    range is 70-300 ms.
- The tempo and the Tap settings are saved in presets. Loading a preset doesn't move any delay's
  time. Older presets load with 120 BPM and Tap = 1/4.

## 1.70.3 (2026-10-20)

- **PACK opens .zip packs directly.** Pick a zip from the library list (zips now show like folders) or
  from the file chooser. It's unpacked once, on a background thread, into
  `%LOCALAPPDATA%\GriffinAmplification\PackCache\<zip>-<id>`. The dropdown says "Unpacking..." meanwhile.
  Opening the same zip again reuses the cache instantly; a changed zip is unpacked again. Your zips
  are only ever read.
  - Only .nam, .wav, .json and .txt files are taken (and zips inside the zip, one level deep).
    Entries that try to write outside the cache are skipped, and packs over 2 GB or 5000 files are
    refused.
  - Presets save the zip's own path, and loading one finds (or rebuilds) its cache.
  - **Clear pack cache** at the bottom of the PACK menu removes unpacked copies (not the one in use).
- Uses miniz 3.0.2 (MIT, vendored in `NeuralAmpModeler/miniz`; see ThirdPartyNotices.txt).
## 1.70.2 (2026-10-20)

- **PACK is one dropdown** in the AMP block, right under the model dropdown and styled like it.
  It's always shown (1.70.0/1.70.1 hid the PACK row when the space beside the model row was
  narrower than 120 px). The model dropdown moves up one row, and the AMP block is 40 px taller (as
  far as the window allows).
- Its menu lists the packs in your library with capture counts (zips are marked *unzip first*), then
  the chosen pack's captures (a submenu per group, sizes as a submenu), plus *Choose a pack folder...*,
  *Choose a capture file...*, *Rescan this pack* and *Cab IR*.

## 1.70.1 (2026-10-20)

- **PACK PICKER fixes** (packs didn't show up):
  - **PACK** is now a menu: *Choose a pack folder...*, *Choose a capture in the pack (.nam or .zip)...*
    (the capture's folder becomes the pack), and *Packs in your library*, a list of every folder in the
    library that holds captures, plus the zips there that still need unzipping.
  - Windows' folder chooser shows .zip files as folders but returns nothing for them, so nothing
    happened. Now an empty result or a .zip says "unzip it first".
  - Scans subfolders too (up to 4 levels), so picking a parent such as *Neural Amp - Sims and IRs*
    finds the Lynx pack inside it, grouped by subfolder.
  - `pack-scan.log`, next to the app / plug-in, records what each scan saw.
## 1.70.0 (2026-10-20)

- **PACK PICKER** in the AMP block, beside the model loader (additive: the loader, AMPS library,
  presets and everything else are unchanged).
  - **PACK...** picks an unzipped capture-pack folder (e.g. a TONE3000 download). Captures are grouped
    from their file names ([AMP]/[PRE]/[POW] tags, else the .nam gear type), with sizes and variants
    (STD, xSTD, lite, nano, device versions) in their own dropdown.
  - **Level match:** switching pack captures keeps the volume. Each one is trimmed to the first capture
    picked in the pack, from its .nam loudness (Raw output mode only; Normalized and Calibrated already
    level it). Your Output knob, and models loaded any other way, are untouched.
  - **IR:** pairs the pack's cab IR with amp-only captures, but only into an empty cab slot or one the
    pack filled itself. You can pick which IR, or turn pairing off.
  - Saved in presets as an optional `###PackV1###` section at the end. Older builds and older presets
    load as before.
## 1.69.1 (2026-10-20)

- An empty socket's description said the sockets run "V5 to V8"; it now says V5 then V6.
- README: the chassis section is rewritten for the 3D chassis, the sockets and 4 Tubes, with two
  pictures (docs/images). No sound changes.

## 1.69.0 (2026-10-20)

- **The chassis now has 6 preamp tubes and 4 power tubes.**
  - **Preamp:** V1, V2, the two sockets **V5 and V6**, V3 (the follower) and V4 (the phase inverter).
    1.68.0's sockets V7 and V8 are gone. If a 1.68.0 save had them set, it loads without them.
  - **Power tubes:** four sockets, V9-V12. A pair uses V9 and V10; V11 and V12 sit empty until
    **4 Tubes** is on.
- **4 Tubes** (POWER AMP tab, or click a power tube on the chassis): a quad, like a 100 W head.
  - Two tubes per side in parallel, into half the load (1.7k plate to plate).
  - A supply with half the resistance and twice the caps, and a bigger output transformer: a 30 Hz
    corner, and saturation at twice the flux.
  - Each tube runs at the same voltages as in the pair, so small signals are as loud (0.2 dB apart).
    Pushed, it's only subtly firmer; the bigger transformer is clearest with OT Iron on.
  - Each tube has its own life, dissipation and REPLACE button (the status panel shows four rows).
    The tubes wear at slightly different rates. With failing on, a failed tube in a quad halves its
    side instead of silencing it. That path isn't covered by the tests below: in a 20 s red-plating
    run, no tube wore out.
  - Off by default. Older states and the Tube Lab presets load with a pair. Saved by name
    (`TubeQuad`, `Lab2TubeQuad`).
- **Opening TUBE LAB or TUBE LAB 2 opens its chassis first.** Its X shows the Tube Lab's knobs
  underneath. The CHASSIS button and the chassis's GO TO links still work.
- Tested through the whole plugin:
  - Your autosave, a snapshot and two hot settings are still **bit-identical to 1.67.1**.
  - So is a 20 s red-plating run with failing on (pair).
  - The quad stays finite with speaker load, OT Iron, Squish and cathode bias on, with EL34s or 6L6s.

## 1.68.0 (2026-10-20)

- **The Tube Lab's CHASSIS page is now the whole amp, in 3D.** CHASSIS (right of the Tube Lab's tabs)
  opens it.
  - It's drawn as real 3D shapes, lit from above: a steel chassis on a bench. **Drag** to turn it all
    the way round, use the **wheel** to zoom, and **RESET VIEW** (or double-click empty space) to put it
    back. It remembers the angle while the app is open.
  - Along the back, the preamp tubes sit in the order the signal passes them: V1, V2, the four new
    sockets V5-V8, V3 (the follower) and V4 (the phase inverter). Then come the **power tubes** V9 and
    V10 (EL34 or 6L6), the **output transformer** and the speaker jack on the back panel. The power
    transformer and filter caps, the turret board, the front panel and the Variac are there too.
  - **The preamp is now wired to the power tubes.** A signal wire runs from the input jack through
    every working tube to the phase inverter, the power tubes and the output transformer. Each leg lights
    with what its stage passes on. Power tubes glow with their heaters, and their plates turn red when
    they red-plate (past their rated dissipation). A failed one goes dark, with a milky getter.
  - The front panel's knobs show Drive, Bass, Middle, Treble, Master and Presence. The pilot light is on
    while the lab is on.
  - **Click** anything to see what it is and set it in the side panel. **Double-click** it to go to its
    page. The side panel's **GO TO** buttons do the same:
    - preamp tubes and parts: the PREAMP tab
    - V4 and the power tubes: the POWER AMP tab
    - the Variac and power supply: the FEEL tab
    - every tube and part: the **DEBUG scope**, set to that tube's node
    - the output transformer and speaker jack: the **SPEAKER LAB**
    - the input jack: the **AMP capture** (TUBE LAB 2: its own popup, where LOAD CAPTURE is)
  - It still redraws only when something changes (or while you drag it).
- **Four new preamp sockets, V5-V8**, between V2 and V3. Each socket has:
  - **Job**:
    - **Empty** (no sound, no CPU)
    - **Gain**: another gain stage like V2, driven by the stage before it
    - **Follower**: a cathode follower on the stage before it
    - **Boost**: a gain stage with its cathode unbypassed. That's about a third of the gain, cleaner,
      with more headroom.
  - **Tube**:
    - **12AX7**
    - **12AT7**: mu 60, more current, less gain
    - **12AU7**: mu 17, the clean-boost / driver tube
    - **5751**: mu 70, a 12AX7 with about 30% less gain
    - Changing the tube sets Bias to where that tube usually runs (12AU7: -5 V).
  - Its own **Gain** pot, **Bias**, cathode **Cap**, **Plate Cap** and **Couple** cap.
  - Gain sockets share the preamp's supply with V1 and V2, so they load it and sag with it. The Follower
    switch (V3) now follows the last gain stage, sockets included.
  - The 12AT7, 12AU7 and 5751 use the 12AX7 model's curve shape with their own mu. Their strength is
    matched to each tube's datasheet at one typical operating point.
  - They need the Circuit engine, and they're bypassed while Capture is on. The first gain socket on a
    lab without Stage 2 switches it to 8x, as Stage 2 does.
  - Each gain socket costs about 0.7% of a core. TUBE LAB 2 has its own four.
  - Saved by name (`TubeSock5Role` ... `TubeSock8CoupleSel`, `Lab2TubeSock*`). Older states and the
    Tube Lab presets load with all four empty.
- The debug scope has four new channels: V5-V8's plates.
- Tested through the whole plugin:
  - Your live autosave and a snapshot of it are **bit-identical to 1.67.1**, as are a hot Stage 2 +
    follower + power amp setting and a V1-only one.
  - Clean V1 (Drive 6, no power amp):
    - one gain socket: +10 dB and much more dirt
    - two gain sockets: +13 dB
    - a 12AU7 boost: +6.5 dB, cleaner
    - a follower: slight rounding
  - Switching a socket while a note is held crossfades. On a clean setting, the first one to switch in
    leaves a faint dip (-56 dB re the note), quieter than switching Stage 2 in (-48 dB, unchanged).
  - TUBE LAB 2's sockets don't touch TUBE LAB. The Classic engine ignores them.

## 1.67.1 (2026-10-19)

- **CAB LAB opens into the SPEAKER LAB.** Clicking CAB LAB switches its 3D on and opens the SPEAKER
  LAB page, so the mics you see and move are the ones you hear. Before, a CAB LAB with 3D off (the
  classic mode) ignored the SPEAKER LAB's mics entirely: moving them changed nothing.
- A new **CAB LAB** button at the top of the SPEAKER LAB goes to the classic card, with its Mic A /
  Mic B knobs, and switches 3D off. The card's SPEAKER LAB button comes back here and switches 3D on.
  The page's 3D switch still works on its own, and now lights gold when it's on. It used the dark theme
  colour, so on and off looked the same.
- No sound changes: a saved rig keeps its 3D setting until you open CAB LAB.

## 1.67.0 (2026-10-19)

- **TUBE LAB 2: dual amps.** A second, complete Tube Lab, in the AMP NEST's LABS between TUBE LAB and
  CAB LAB.
  - It has its own copy of every Tube Lab setting: preamp, Stage 2, tone stack, power amp, Real Feel,
    60 Hz, Circuit+, the bench, failing tubes, Economy / 8x. It also has its own tube engines.
  - It has **its own capture**. In its popup, **LOAD CAPTURE** (under the curve) loads a .nam and
    switches **Capture** on. TUBE LAB keeps using the AMP block's capture, so you can run two
    different amps, e.g. a 5150 in one and a Plexi in the other.
  - Put both on the grid, one on each row and both cabled from IN. They mix at OUT, or you can cable
    each into its own cab first.
  - It opens the same popup as TUBE LAB. Its title says TUBE LAB 2, and every knob, switch, preset,
    the debug page and the chassis page then act on TUBE LAB 2. They go back to TUBE LAB when you open
    TUBE LAB. The debug probes and chassis readings follow the lab you opened last.
  - Unlike TUBE LAB's capture, LAB 2's doesn't pass through the AMP block's noise gate and Bass /
    Mid / Treble, since it isn't the AMP block's capture. The REAL FEEL pedal stays TUBE LAB's.
  - Off the grid it uses no CPU. On the grid it costs the same as TUBE LAB with the same settings.
  - Saved by name (`Lab2Active`, `Lab2Tube*`). Its capture's path is saved in a new last section,
    `###TubeLab2CaptureV1###`. Older states load with TUBE LAB 2 off the grid and no capture.
- Tested through the whole plugin:
  - TUBE LAB 2 with TUBE LAB's settings is bit-identical to TUBE LAB (your settings, and a hot Stage
    2 + power amp + bench setting), with the same latency.
  - TUBE LAB 2 with your 5150 capture loaded is bit-identical to TUBE LAB on the AMP block's copy of
    it (with the AMP block's gate and EQ off).
  - Both on the grid in parallel mix exactly at OUT.
  - The capture path saves and loads back.
  - Your live autosave and the snapshot are bit-identical to 1.66.0.
- A correction to 1.66.0's notes: its "AMP into GATE" check actually used the COMP pedal. Rerun with
  the AMP block into GATE (Key In), 1.67.0 is bit-identical to 1.65.0.

## 1.66.0 (2026-10-19)

Five new pedals (MorePedals.h), in the PEDALS drawer. Each is modelled on how a classic pedal behaves;
none copies its circuit.

- **TRACK GATE**: a second noise gate, with three models and more options than GATE.
  - **Tracking** (after the ISP Decimator): the release follows how fast your note is dying, so a
    slow sustain fades naturally and a stop is cut at once.
  - **Classic** (after the Boss NS-2): threshold and decay.
  - **Smart** (after the MXR Smart Gate): **Noise** sets what it gates. Hiss gates only above 3 kHz, so
    the body of a fading chord stays. Mid gates above 250 Hz, Full gates everything.
  - Also: **Hold**, **Range** (how far down it goes when closed), **Ratio** (a downward expander from
    1.5:1 to a 20:1 gate) and **Key In** (listen to the clean guitar from IN), plus fixed 4 dB
    hysteresis so it never chatters.
  - Measured through the whole plugin, on guitar with a -70 dBFS hiss floor:

    | Setting | Hiss between notes |
    | --- | --- |
    | Tracking | -106 dB |
    | Classic | -138 dB |
    | Smart, Hiss | -77 dB |
    | Smart, Mid | -88 dB |
    | Smart, Full | -109 dB |
    | Ratio 2:1 | -73 dB |
    | Ratio 4:1 | -78 dB |

    In every case the note's decay above the threshold was kept exactly (0.0 dB).
- **DIGI DELAY** (after the Boss DD-3 / DD-8): 20 ms to 1.2 s, with Feedback and Level. Your dry
  signal stays unchanged. The modes:
  - **Standard**: clean repeats with the DD-3's ~7 kHz bandwidth.
  - **Mod**: chorused repeats.
  - **Warm**: 2.8 kHz bandwidth, softly saturating.
  - **Reverse**: each window played backwards.

  A new Time crossfades over 30 ms instead of sweeping the pitch.
- **ANALOG DLY** (after the MXR Carbon Copy), with Delay (20-600 ms), Regen, Mix and the Mod
  switch's Rate and Width:
  - Bucket-brigade repeats that get darker the longer the delay: about 6 kHz at 20 ms, down to
    1.8 kHz at 600 ms.
  - Soft saturation in the loop, so Regen past about 9 builds into self-oscillation without blowing
    up (it peaked at -1 dBFS at Regen 10).
  - Turning Delay swoops the pitch, as on the pedal.
- **TAPE ECHO** (after the Boss RE-20 / Roland Space Echo):
  - Three playback heads (head 2 at twice head 1's delay, head 3 at three times), in 7 combinations.
  - **Rate** is the tape speed: head 1 from 300 ms (slow and dark) to 70 ms (fast and bright).
    Turning it swoops.
  - **Intensity** (feedback, runs away up high), **Echo** level and **Saturate** (tape drive).
  - **Wow**: drift at 0.8 Hz (about 8 cents at full) plus flutter at 12 Hz.
  - A built-in spring reverb (**Reverb** level).
- **SPRING VERB** (after a '63 Fender Reverb / Boss FRV-1), with Dwell, Tone, Mix and Decay (about
  1-4 s):
  - Two springs per side. Each is 24 allpasses in a feedback loop, which delays the lows more than
    the highs, so every bounce comes back as the falling "drip" chirp.
  - **Dwell** drives the spring's transducer: louder, grittier, and it splashes when you dig in.
- All five open in the wide pedal popup (now 740 px, for TRACK GATE's eight controls). Every control
  has hover help that names the pedal it's after.
  - The delays and reverb ring out when switched off.
  - Off the grid, they use no CPU.
  - On the grid, measured per pedal (the old GATE is about 0.04% of a core):

    | Pedal | CPU, % of a core |
    | --- | --- |
    | TRACK GATE | about 0.1% |
    | DIGI DELAY | 0.07% |
    | ANALOG DLY | 0.15% |
    | SPRING VERB | 0.45% |
    | TAPE ECHO, all 3 heads and the spring | 0.66% |
- Grid ids 43-47 (`chaingrid::kTrackGate`..`kSpringVerb`). The params are at the end of the enum
  (`kTGActive`..`kSprDecay`, then `kGridCell43`..`kGridIn47`), saved by name. Older states load
  with the five off the grid.
- Regression, against a 1.65.0 build of the same source: your live autosave, the earlier snapshot,
  and GATE with Key In all come out bit-identical (max difference 0).

## 1.65.0 (2026-10-19)

- **Hover help on every knob and switch** (ParamHelp.h). Each one says what it does to your sound, the
  way the buttons already did: the AMP's Bass / Mid / Treble and where they act, each Tube Lab part
  (Bloom is the input cap, Cap the cathode bypass), the power amp, the bench, every pedal's knobs
  (the extra copies, like REVERB 2, borrow their original's), each PRO EQ / Output EQ band's Freq,
  Gain, Q, Type and Slope (by band number), each SPEAKER LAB mic's X / Y / Z, GATE, PRO COMP and the
  OUTPUT page.
- Controls that already had their own tooltip keep it. No sound or preset changes.

## 1.64.0 (2026-10-19)

- **AMP Capture in the Tube Lab** (new switch on the Tube Lab card's top row, saved as `TubeCaptureAmp`).
  The capture loaded in the AMP block becomes the Tube Lab's preamp, and all of the Tube Lab after
  the preamp works on it:
  - Master, Sag, Feedback, Presence, EL34/6L6, the speaker load
  - the bench (Variac, bias pot, power tube age)
  - Real Feel
  - power tubes that can fail, and the chassis page's power tube health
  - the tone stack if one is on
  - **Drive** is the gain into the capture (12 dB is unity).
  - The AMP block's own noise gate and Bass / Mid / Treble come along with the capture, as in the
    AMP block.
  - The AMP block on the grid steps aside while this is on (its chip says AMP IN LAB). The capture
    runs once, inside the Tube Lab. Mono, as in the AMP block.
  - Tested through the whole plugin, on your 5150 capture:
    - With the Tube Lab's power amp off, it matches the AMP block alone (0.00 dB; the rest is the
      Tube Lab's resampling filters and 15 Hz DC blocker).
    - Switching the AMP block on or off changes nothing while it's in the Tube Lab.
    - The Variac (90 V) and the bias pot (60%) do change the sound with a capture driving the power
      tubes.
  - Best with preamp or DI captures; a full-amp capture already has a power amp in it.
  - With the switch off, nothing changes: single-stage Tube Lab settings and everything else are
    bit-identical to 1.61.1. Two-stage settings differ only by the 1.63.1 oversampling change.

## 1.63.1 (2026-10-19)

- **No more aliasing grit on two-stage Tube Lab settings.** With the second gain stage on, the Tube Lab
  now runs at 8x by itself (it was 4x unless you switched 8x on, and 2x with Economy). On a high E at
  the 800 Lead preset, the aliasing goes from -40 dB (Economy, 2x: audible grit) to -53 dB; Economy now
  only halves it to 4x (-43 dB). Single-stage settings keep 4x (2x with Economy): they don't make
  harmonics high enough to fold back. More CPU with the second stage on (about 13% of a core rather
  than 7% on the test PC).
- **Tube Lab presets reset the bench** (Variac 120 V, new tubes, bias pot 100%). They were voiced on a
  stock bench; on 56%-worn tubes at 90 V they don't sound like their names. The bench is still yours to
  change after picking one.

## 1.63.0 (2026-10-19)

- **Tube Lab presets re-voiced** (PRESETS in the Tube Lab card). "They don't sound like their names;
  fizzy, harsh; thin or muddy."
- **The cause:** they'd been voiced and level-matched with a signal about 9 dB hotter than the Tube Lab
  really gets here (hard picks peak around -19 dBFS, soft ones -31). At a real playing level:
  - The drive presets were under-driven: 800 Lead broke up less than Plexi Crunch, and Tweed Breakup
    didn't break up at all (1% on a hard pick).
  - Their levels jumped by up to 16 dB.
  - The lows were boomy (Tweed's 19 dB over the mids after a 4x12).
- **What was done:** every preset was measured at those levels, through a real 4x12 IR (JCM900 1960
  BLENDED 3), on test tones and on a pickup-filtered strum (a real pickup's top end, not the
  over-bright synthetic one). Each was moved toward targets for its name: how much it breaks up on
  a hard and a soft pick, its low end against the mids, and its top end against the cab's own.
  Results (hard / soft pick):

  | Preset | Hard | Soft |
  |---|---|---|
  | Blackface Clean | 2.5% | 0.6% |
  | Tweed Breakup | 10% | 1% (cleans up with your touch) |
  | Top Boost Chime | 12% | 2% |
  | Plexi Crunch | 32% | 11% |
  | 800 Lead | saturated | saturated |
  | Modern High Gain | saturated | saturated |
  | Singing Lead | saturated | saturated |

  - Lows came down: Tweed 13.6 to 8 dB over the mids, Modern High Gain 9.7 to 4.8.
  - The top is smoother on the leads: Singing Lead's 4-8 kHz is 7 dB under its 1-3 kHz, through the cab.
  - Every preset now plays at the same level as your dry signal; Tight Boost is +6 dB on purpose.
  - Notable changes:
    - Plexi Crunch: less drive and master, presence and treble down, a 220 pF plate cap on V2.
    - 800 Lead: more drive, Gain 2 full, a small V2 bypass cap, treble down.
    - Modern High Gain: Gain 2 down, bass down for tightness, presence down.
    - Tweed Breakup: much more drive and master, bass down.
- Tuned on test signals at your measured level, not by ear: tell me which ones still don't sound right.

## 1.62.0 (2026-10-19)

- **PREAMP CHASSIS** (Tube Lab, the **CHASSIS** button at the right of the PREAMP / POWER AMP / FEEL tabs):
  the preamp as a chassis to look into (TubeChassisControls.h).
  - **Four 12AX7s**, one per real job in the circuit: V1 input stage, V2 gain stage, V3 cathode
    follower, V4 phase inverter. Each glows with its heater: dimmer and redder with the Variac down,
    brighter and whiter with it up. Each pulses with its own stage's signal, and sits cold when its
    stage is off. Glass, getter flash, plates, micas and socket are all drawn.
  - **The turret board:** the parts the knobs really are, drawn as the parts. Orange-drop input and
    coupling caps; blue electrolytic bypass caps; silver-mica plate caps; carbon-comp plate and
    cathode resistors, with their real colour bands for the value; the Gain 2 pot. Click a part or
    a tube to read what it does and set it right there.
  - **The Variac sits on the chassis:** a cream dial from 90 to 135 V, red past 125. Drag it or
    wheel over it.
- **Power tubes can fail** (switch on the chassis page, off by default; `TubeCanFail`, saved by name).
  - With it on, a power tube run past its rated plate dissipation (red-plating: the Variac up and the
    bias pot hot) wears out, faster the further over, faster still with the heaters over-run. Then it
    fails with a bang.
  - The amp carries on through the other tube alone, thin and fizzy (every other half-cycle crosses
    over); with both gone it's silent. **REPLACE** puts a new tube in, and the output warms back up
    over about 2 s.
  - Accelerated (real red-plating takes minutes to hours) so you can hear it. Measured:
    - At stock settings the EL34s sit at 59% of their rating and never wear.
    - At 135 V with the bias pot at 150%, an EL34 fails in about 24 s, and its partner a few seconds
      later: with one tube gone, B+ rises and the other runs even hotter.
    - 6L6s (rated 30 W) last about 107 s at the same settings.
  - The page shows each tube's life, its dissipation against its rating, and its state (ok, hot,
    RED PLATING, FAILED).
  - Off, the sound is bit-identical to 1.61.1 (five Tube Lab setups).
- The page redraws only when something it shows changes, at most 25 times a second while you play.
  Closed, it costs nothing.

## 1.61.1 (2026-10-19)

- **CAB can leave the grid.** Drag it off the grid like a pedal and it waits in the PEDALS menu's first
  slot until you drag it back in (it used to snap back to the grid). Off the grid it isn't processed at
  all. AMP and the FX loop still always stay on the grid.
- **The PEDALS menu opens over the AMP NEST / LABS panel**, with an X to close it, instead of over the
  grid. The grid stays in full view, so the menu stays open while you drag several pedals out. The LABS
  cards and the PEDALS button under it are hidden while it's open, so their edges don't peek out. The
  PEDALS count includes CAB when it's out.

## 1.61.0 (2026-10-19)

- **Dual cabs in CAB LAB** (SPEAKER LAB page, **Cabs: Single / Dual**):
  - A second cab beside the first, with its own **Size** (4x12, 2x12, 1x12, 4x10), **Speaker** voicing,
    and source: the model, or **its own IR** (the second IR load, **CAB 2 IR**; saved by path in the
    state's new last section, `###CabLabIR2V1###`).
  - Each of the four mics is on one cab (its **Cab** field, shown with two cabs). Moving a mic to the
    other cab keeps its spot on the matching cone. Pans stay as they are.
  - The cabs are told apart by colour: **CAB 1 gold, CAB 2 teal**. That shows in their tabs, the size
    and speaker row, each mic's badge, the FRONT/SIDE views (which show the selected cab), and the 3D
    view. The 3D view stands both cabs side by side at their real sizes, so a 2x12 is visibly smaller
    than a 4x12, with each mic's lines going to its own cab's cones.
  - Switching to Dual puts a mic on cab 2 if none is on it: MIC C, turned on, just off its first cone.
    In the first 1.61.0 build, cab 2's only mic started off, so cab 2 was silent: "cab 2 response isn't
    working". A cab with no mic on it now says so.
  - Tested:
    - The RESPONSE plot follows each mic's own cab: 0.35 dB at most from the actual audio.
    - Single mode is bit-identical to 1.60.
    - Classic CAB LAB presets are still bit-identical to 1.57.
    - A cab 2 IR loads, plays and saves its path.
  - Saved by name (`CabDual`, `Cab2Format`, `Cab2Speaker`, `Cab2SourceIR`, `LabMicA..DCab`). Older
    presets load as a single cab.
- The RESPONSE plot follows the CAB 1 / CAB 2 tab with two cabs: the selected cab's mics and their sum,
  with both cabs together as a dashed line (it used to show every mic on both cabs whatever was
  selected, so picking CAB 2 changed nothing). A cab with no mic on says so there too.
- SPEAKER LAB polish: the selected segment (Single/Dual, Size) stands out, the mic tabs' names shrink to
  fit beside their cab badge, and the readout fits all its lines.

## 1.60.0 (2026-10-19)

- **LABS.** TUBE LAB and CAB LAB have their own section in the AMP NEST, apart from the pedals, and their
  own look everywhere: an amp head in tolex with gold piping, a glass preamp tube or a speaker cone, and
  a jewel lamp that's lit while it's on (on the grid too, so they stand out among the pedals). Off the
  grid, a lab sits in its slot to drag onto the grid. On it, the slot still shows the lab's card, marked
  ON THE GRID, and a click opens it.
- **PEDALS drawer.** The pedal list is now a drawer. The **PEDALS** button (with the count off the grid)
  opens it over the lower part of the grid, every pedal in view at once. Start dragging one and the
  drawer gets out of the way, so you can see where it lands; click one for its settings. Blocks under
  the open drawer are hidden by it.
- **Off means off.** Every block's On switch already took it out of the processing once its fade (and
  its trails, for delays, reverbs, modulation and cabs) were done. The Tube Lab sleeps, and REAL FEEL
  passes straight through. Measured through the whole plugin on your chain: everything on 7.8% of a
  core, every block switched off 0.51%, an empty grid 0.41%. So a switched-off block costs next to
  nothing. The rest, at idle, is the input/output stages and meters.
- No sound or preset changes (the grid and every saved setting are as they were).

## 1.59.0 (2026-10-19)

- **SPEAKER LAB**: CAB LAB's new 3D mic placement. Open it with the **SPEAKER LAB** button on the CAB LAB card.
  - **Cab**: 4x12 (closed back), 2x12, 1x12, 4x10 (open back). The cones sit where they really are on the
    baffle, at their real size. There's a new **4x10 Jensen P10R** voicing, and the Speaker voicings still
    apply. Picking a different cab moves each mic to the same spot on the matching cone.
  - **Four mics**, each with On, Mic, X / Y (across the baffle, cm from its centre), Z (cm out from the
    grille), Level, Pan and Flip. There are four new mics, **e609**, **e906**, **M160 Ribbon** and **C414
    Condenser**, as well as the six before. Each has its own response, polar pattern (cardioid,
    supercardioid, hypercardioid, figure-8) and proximity effect.
  - **Each mic hears every cone separately** (CabLab.h):
    - Time of flight from that cone's dust cap.
    - A level that falls like a vibrating piston's: flat within the cone's radius, then 6 dB per
      doubling, so a close mic hears its own cone about 15 dB over the next one.
    - A treble shelf for where it points on the cone: from the bright dust cap, to the edge, to off-axis
      beaming.
    - The mic's own polar pattern and off-axis treble loss.
    - Proximity effect from the nearest cone.
    - The cones are slightly mismatched (level, and tens of microseconds), like real speakers.
    - So a mic between two cones gets real comb filtering, and two mics at different distances
      phase-cancel the way real ones do.
  - **The page**:
    - **FRONT** (to scale, each cab's grille cloth and tolex; drag X/Y, wheel for Z).
    - **SIDE** (drag Z on a square-root scale, so the first centimetres get room).
    - **3D** (drag a mic across the baffle at its distance, drag elsewhere to orbit). It draws lines from
      each mic to each cone, brighter for the ones it hears more.
    - **RESPONSE**: each mic and the sum, computed from the same geometry and filters. Checked against
      the actual audio's impulse response: median 0.02 dB. It differs only above ~8 kHz where cones
      comb, 26 dB+ down already.
    - A readout of the selected mic's distance, angle, delay, level and treble from each cone.
    - Shift for fine moves everywhere; double-click a number to reset it.
    - Nothing animates: it redraws only when something changes, and the plot is recomputed only then.
  - **CPU**: CAB LAB 2.0% of a core in classic, 2.2% with two mics on a 4x12, 2.8% with all four.
    Mics that are off cost nothing. Dragging a mic glides, with no zipper noise you'd hear (-62 dB).
  - **Older presets** load in classic mode (the Mic A / Mic B knobs) and sound bit-identical. The page
    says so, and the **3D** switch turns placement on. A new CAB LAB starts in 3D, with an SM57 and an
    R-121 on the top-left cone where the classic defaults were.
  - Saved by name (`CabLab3D`, `CabFormat`, `LabMicA..DOn/Type/X/Y/Z/Level/Pan/Invert`).

## 1.58.0 (2026-10-19)

- **Tube Lab bench** (on the Tube Lab's DEBUG page, a new BENCH row): the things an amp tech changes on
  the bench, modelled in the circuit itself (TubeBench.h):
  - **Variac** (90 to 135 V, 120 stock): the wall voltage into the amp. B+ everywhere (preamp, phase
    inverter, power tubes and screens) and the fixed-bias supply follow it within a fraction of a second.
    The heaters follow it too, and the cathodes cool or warm over a few seconds, as on a real amp.
  - **Bias** pot (50 to 150% of stock idle current): set like on the bench, at 120 V with new tubes. The
    output level stays matched at the stock bias, so cold sounds colder instead of being made up.
  - **Pre Age** and **Pwr Age** (0 to 100%): worn tubes have less perveance (gain, current) and much
    less emission to spare.
  - Emission follows the cathode temperature (Richardson's law, cathode temperature about the square
    root of the heater voltage). A soft ceiling on each tube's current is what's left of that reserve,
    so low heaters or worn tubes round off the peaks.
  - A live readout:
    - **MAINS**: the Variac, heater volts, cathode temperature in kelvin, with a glow swatch.
    - **EMISSION**: each section's reserve and gm, from healthy to starved.
    - **SUPPLY**: preamp B+ range, power B+, screens, bias supply or cathode volts.
    - **POWER TUBES**: each tube's average current, plate dissipation and % of its rating (EL34 25 W,
      6L6 30 W), on a bar from cold through the sweet spot and hot to RED PLATING. With nothing playing,
      that's the idle.
  - What it does on your settings (Tube Lab alone, a -19 dBFS strum; level-matched difference from
    stock):
    - Variac 90 V: -1.5 dB, -28 dB.
    - Variac 100 V: -0.9 dB, -32 dB.
    - Variac 135 V: +0.6 dB, -35 dB.
    - Bias 50%: -2.4 dB, -26 dB.
    - Pwr Age 100%: -1.3 dB, -32 dB.
    - 90 V with both tube sets worn out: -5.2 dB, peaks -8.4 dB, -17 dB.
  - Through your whole chain, Variac 95 V is a -15 dB level-matched change. Your power amp stays fairly
    clean (power tube peaks 87 mA against a 33 mA idle), so most of the Variac's effect there is
    headroom and level, as on a real amp at that volume.
  - Stock (120 V, 100%, new tubes) skips all of it: bit-identical to 1.57.0 on five Tube Lab setups and on
    your whole autosave. Turning any of it changes things without a click (glides; tested on a held note).
  - CPU: free at stock. With the bench in use, the Tube Lab goes 18.7% -> 23.3% of a core on your 8x
    settings (the emission ceiling is evaluated inside the circuit solve; a series form keeps it at that,
    from 26.9%).
  - Saved by name (`TubeVariac`, `TubeBiasPot`, `TubePreampAge`, `TubePowerAge`). Older presets load at
    stock. Tube Lab presets leave the bench alone: it's the amp's mains and tubes, not its voicing.
- **ALWAYS ON** (DEBUG page, bottom row): keeps the Tube Lab's probes running with the page closed, so
  the readouts, bench and scope history are live the moment you open it. Turning it on asks first,
  because it costs about 3% more of a core the whole time (Tube Lab 18.7% -> 21.9% on your settings). It
  costs no GPU while the page is closed. Remembered in ui.ini (`tube_probes_always`).
- **OUTPUT page.** Opened from the new **OUTPUT** button under Master Out (where Mono was), by clicking
  OUT on the grid, or by clicking the OUT meter. Three cards:
  - **LEVEL**: Master Out.
  - **LIMITER**: On, **Ceiling** (-12 to -1 dBFS) and **Release** (5 to 500 ms; 30 ms was the fixed
    value), with a gain-reduction meter. Tested: Master Out +12 dB with Ceiling -1, -6 and -12 gives
    peaks of exactly -1.00, -6.00 and -12.00 dBFS.
  - **STEREO**:
    - **Width** (0 to 200%): 0% is mono, which replaces 1.55.0's Mono switch. A preset saved with Mono
      on loads as Width 0%, identical to before.
    - **Widen** (0 to 100%): stereo from a mono chain. An 11 ms delayed, 250 Hz high-passed copy of
      the middle goes to the sides with opposite signs. It sounds wide, the bass stays centred, and
      the mono sum is exactly unchanged (tested to 1e-17), unlike a Haas delay.
    - A stereo correlation meter. On your chain, Widen 100% takes the correlation from +0.36 to +0.07.
  - The meters measure only while the page is open. Width 100% / Widen 0% is skipped (bit-identical).
  - Saved by name (`LimiterCeiling`, `LimiterRelease`, `OutputWidth`, `OutputWiden`).
- Fix: loading a preset saved before 1.55.0 now turns Mono off instead of keeping whatever was set.

## 1.57.0 (2026-10-18)

- **Its own plug-in ID and settings folder.** Griffin Amplification used the same plug-in IDs and the
  same settings folder as the plug-in it was forked from, so a DAW couldn't tell the two VST3s apart
  and the two standalone apps shared settings. It now has its own IDs (`GrAm` / `GrWj`) and keeps its
  settings, presets, autosave and MIDI map in `%LOCALAPPDATA%\GriffinAmplification`.
- **Your settings come with you.** On the first run, the old `%LOCALAPPDATA%\NeuralAmpModeler` folder is
  copied across (only if it's this app's: it has `ui.ini`), and the old folder is left in place. Tested:
  the copy is identical, an existing new folder is never overwritten, and the original app's folder
  isn't touched.
- **DAW users:** the VST3 is a new plug-in as far as a DAW is concerned, so a session saved with an
  older version will show it as missing. Insert Griffin Amplification again and load your preset.
  The standalone app isn't affected.

## 1.56.0 (2026-10-18)

- **Griffin Amplification's own name throughout.** Settings > About now reads GRIFFIN AMPLIFICATION
  (linking to https://github.com/Gwojtowicz/Griffin-Amplification), "By Griffin Wojtowicz", the
  version, and a credit line for the original open-source authors (Steven Atkinson, Oli Larkin and
  others), plus the third-party notices. The two capture pedals are now **CAPTURE 1** and
  **CAPTURE 2**, the globe button reads **Get Models**, and a failed load says "Failed to load the
  model". README and changelog reworded the same way. File names, the settings folder and saved
  presets are unchanged, and MIDI mappings still work (they're stored by parameter, not by name).

## 1.55.1 (2026-10-18)

- **About page links to this build's repo.** Settings > About has a new **Griffin Amplification on
  GitHub** line that opens https://github.com/Gwojtowicz/Griffin-Amplification (also set as the
  plugin's URL, `PLUG_URL_STR`, which hosts show). The original authors' credit lines stay. No
  sound or settings changes.

## 1.55.0 (2026-10-18)

- **Mono output switch.** A **Mono** switch next to Limiter, under Master Out, sends the left and right
  channels mixed together ((L + R) / 2) to both outputs, so a single speaker or one monitor gets
  everything: CAB LAB's mic pan and room and the effects' stereo spread fold to the middle instead of
  half of it going missing. It glides in and out over 10 ms, so switching doesn't click. It's applied
  after the Output EQ and before Master Out and the Limiter, so the meters and spectrum show what you
  hear. Off, it does nothing at all (the default; saved by name as `OutputMono`, so older presets
  load as they were). The chain itself still runs in stereo.

## 1.54.0 (2026-10-18)

- **EQ analyzers start off, and remember your choice.** The live spectrum on the PRO EQ and Output EQ
  pages now only runs once you switch ANALYZER on, and the switch is saved (`pro_eq_analyzer` /
  `out_eq_analyzer` in ui.ini), so it stays how you left it after a restart. Off, those pages draw
  only the EQ curve: no audio copied for them, no FFTs, no 30 fps redraw. The spectrum strip on the
  main window keeps its own show/hide (X / SPECTRUM). No sound or saved-preset changes.

## 1.53.2 (2026-10-18)

- **App icon is now the Griffin emblem.** The exe's icon (Desktop shortcut, taskbar, title bar) was
  still the stock upstream square; it's now the same griffin as the in-app logo, on a dark disc so it
  reads on any wallpaper. No sound or settings changes.

## 1.53.1 (2026-10-18)

- **Tube Lab: a little less CPU, bit-identical sound.** The preamp solver reads only the parts of
  its circuit matrix that can be non-zero, and looks the tube tables up once per sample instead of
  on every tube evaluation. About 3% less with the Tube Lab on (23.8% -> 23.0% of a core on your
  rig at 128); output identical to 1.53.0, sample for sample.
- Why not more: measured on your settings, the Tube Lab's solver already needs only 1.3 steps per
  sample in the preamp and 1.03 in the power amp (1 is the minimum), so its time is the circuit math
  itself, not overhead. The one big lever is the 8x switch: 4x would take the Tube Lab from 12.9% to
  7.1% of a core, but changes the top end (the difference is -27 dB at 6-12 kHz, -21 dB above
  12 kHz), so it stays your choice.

## 1.53.0 (2026-10-18)

- **The audio engine uses about a third of the CPU it did: same sound.** On your saved rig at a
  128-sample buffer: 18.9% of a core -> 7.0% (worst block 47% -> 21%). With the Tube Lab on: 35.4%
  -> 23.4%. That leaves much more headroom against crackles, and makes 64 samples realistic with the
  Tube Lab off (worst block 31%).
  - **AMP model:** its layers were computed by a general-purpose matrix routine that repacked
    the data on every call. They now use a dedicated 8-channel kernel, 4 audio frames at a time, with
    AVX2/FMA when the CPU has it (yours does) and SSE2 otherwise. About 5x faster. It's float math
    in a different order: the output differs from 1.52 by -100 dB (rounding noise, uncorrelated with
    the signal), and it's slightly closer to the exact result than before.
  - **CAB IR convolution:** the direct part and the spectrum multiply-adds run 2 doubles at a time.
    Differences ~1e-16, far below what the output can show.
  - **AMP tone stack and other filters:** a dedicated biquad loop. Bit-identical output.
  - Checked on your rig at buffers 64, 128 and 256, and with the Tube Lab on.

## 1.52.2 (2026-10-18)

- **Fixed crackles / audio dropouts at small buffers.** The tuner ran its pitch search on the audio
  thread every 60 ms, even with the tuner page closed, and did it all inside one audio block. That
  block took about 3x as long as the others, often enough to crackle once Windows and the graphics
  added their own delays.
  - The pitch search now runs only while the tuner page is open, and is spread evenly over the
    following blocks. The readings are the same as before (to 0.01 Hz on test notes).
  - Measured on your saved rig at 128 samples: the worst block went from 59% to 34% of the time
    available (average about 19%). At 64 samples, from 110% (guaranteed dropouts) to 43%.

## 1.52.1 (2026-10-17)

- **The window uses about a third of the graphics power it did** (GPU use: 64% -> 21% with a full
  rig playing). Nothing looks different: screenshots match pixel for pixel, the meters still move at
  60 fps and the spectrum is unchanged.
  - Why it was high: the drawing engine clipped in its shaders, so repainting just the IN/OUT meters
    made the GPU go through the full-window background and panels every frame. Each repainted area is
    now clipped in hardware, so only those pixels are drawn.
  - The hidden tuner and the switches in closed popups no longer repaint what's under them.
  - A silent or steady meter no longer repaints at all.

## 1.52.0 (2026-10-17)

Two new blocks in the AMP NEST pedal list, at the end. Drag them anywhere on the grid like any pedal.

- **GATE: a full noise gate.**
  - Threshold (dBFS), Close (how far under the threshold it waits before closing, so a note hovering
    around the threshold doesn't chatter), Attack, Hold, Release, Range (-90 = silent, -20 just turns
    the hiss down) and Key HPF (a high-pass on what it listens to, for hum).
  - **Key Input** (on by default): it listens to your clean guitar, after Master In, wherever it sits.
    Put it after the AMP or the Tube Lab, and the amp's own hiss and the sustain it adds can't hold
    it open: your picking decides. This is the trick that keeps a high-gain gate solid.
  - A KEY meter shows what it hears against the threshold (bright marker) and the close point (dim
    marker), plus OPEN / CLOSED and how far it's turned down.
  - Measured on your Twin capture, at raw interface level: the hiss between notes goes from -86 to
    -166 dBFS, notes come through untouched, and it closes once a note has rung out. Listening to its
    own input instead, it stayed open on the amp's tail after the last note.
- **PRO COMP: a studio compressor.**
  - Threshold, Ratio (20 = Limit), Attack, Release, Knee, Makeup, **Auto Gain** (adds back what a
    -12 dBFS peak loses), Mix (parallel / New York compression) and **SC HPF** (a high-pass on what it
    listens to, so low notes and palm mutes don't pump everything).
  - **Style:** Clean (transparent and fast, peak-sensing, like a VCA), Opto (smooth, RMS-sensing, and
    the harder and longer it works the slower it lets go, like an LA-2A) or FET (fast and aggressive,
    with grit that grows as it compresses, like an 1176).
  - Meters: IN with the threshold marked, GR (gain reduction, 0 to 24 dB) and OUT.
  - Measured after your AMP at -15 dB under the peaks, 4:1: about 3 dB off the pick and 4.7 dB more
    sustain.
- Each uses well under 1% of a CPU core. Neither adds latency.
- The AMP block's built-in Gate and the old COMP pedals are unchanged, so your presets sound the same
  (checked bit-for-bit on your chain).

## 1.51.0 (2026-10-17)

- **More audio buffer sizes** (Settings page, standalone app): 32, 48, 64, 96, 128, 192, 256, 384, 512,
  1024 and 2048 samples (was 128 to 1024). 64 and below can drop out with the Tube Lab on.
- **DRIVER PANEL** button next to the sizes: opens your audio interface's own control panel (its
  ASIO settings), where its buffer size and other driver options live. With a
  non-ASIO driver it opens the Windows sound settings.
- **DEVICE...** button: opens the full audio Preferences (interface, sample rate, input/output channels,
  MIDI input). Same as Ctrl+, before.
- **Shows the size the interface actually runs.** Many interfaces only run the size set in their own
  panel. If it differs from the one you picked, the note under the row says so and the latency readout
  at the bottom uses the real size.
- **The app and the interface's panel stay in sync.** Change the buffer size in the interface's panel and
  the app restarts its audio at that size within about a second, and the row follows. Before, the
  driver's reset request was ignored. Pick a size in the app and the panel shows it too.
- DRIVER PANEL tries a second time if nothing comes up, because the first call to some drivers
  only starts their helper app.
- The Preferences dialog's buffer list also gained 48 and 384.

## 1.50.0 (2026-10-16)

- **Spectrum window:** the third button on the strip's left (a box with an arrow) pops the spectrum out
  into its own window:
  - drag its title bar to move it, and its bottom-right corner to resize it;
  - DOCK puts it back in the strip, and X hides it;
  - it sits above every popup, including the Amp's and the Tube Lab's, and above the meters, so you
    can watch it while you play with anything. Settings, the Output EQ page and the Tuner still
    cover it;
  - it redraws at 60 fps (the strip and the EQ pages redraw at 30);
  - its position and size are remembered in `ui.ini`;
  - it stays 28 units clear of the window's right and bottom edges, so its resize corner never
    lands on the app's own resize corner.
- **The big popups leave room for the strip:** the Amp, Tube Lab, CAB LAB and PRO EQ cards now end
  above the docked spectrum strip, which stays live on top of the dimmed background. With the strip
  expanded, it drops to its small size while one of those cards is open. The Tube Lab still shows all
  its controls.
- **Faster, sharper analyzer (every view):**
  - it now uses four FFT sizes, 8192 for the deep lows down to 1024 for the highs, each over its
    newest samples and blended over half an octave at each split;
  - a 3 kHz note now shows (within 6 dB) after 10 ms instead of 73 ms. 80 Hz still takes about
    87 ms: resolving the lows needs the long window;
  - tones still read at their true level (within 0.7 dB, as before). Hiss in the highs reads a few
    dB higher than it did;
  - it costs 0.6 ms per update for two signals (0.46 before).

## 1.49.0 (2026-10-16)

- **Bigger spectrum strip:** an arrow under the strip's X makes it about three times taller (the chain
  grid shrinks to fit). Click it again for the small size. Remembered in `ui.ini` (`spectrum_big=`).
- **The strip stays visible while a pedal is open:** a pedal's popup now sits above the strip instead of
  covering it. The strip stays live and clickable (not dimmed) while you turn the pedal's knobs, so you
  can watch what the pedal does to the spectrum. The big cards (Amp, Tube Lab, CAB LAB, PRO EQ) still
  cover it.

## 1.48.0 (2026-10-16)

Themes and the spectrum strip:

- **Colour themes** in Settings: six palettes (Patriot Blue, Amber, Emerald, Crimson, Violet, Teal),
  each a swatch you click, plus **DARK / LIGHT**. The whole window follows: background, popups, wires,
  buttons, switches, tabs, knobs' accent and text. Patriot Blue dark is the old look, unchanged. The
  other dark palettes tint the background faintly toward their colour. The EQ graphs and meters stay
  dark in both modes, like a display.
- **Spectrum strip X:** an X on the strip's left hides it, and the chain grid gets the space back. In its
  place are two small buttons: **SPECTRUM** (bring it back) and **OUTPUT EQ** (open the Output EQ page).
  Settings has the same switch (SPECTRUM: SHOW / HIDE).
- Both choices are remembered in `ui.ini` next to the autosave (shared by the app and the plug-in).
  They're not part of a preset or the saved state.
- Your sound is unchanged; this release touches only the UI.

## 1.47.0 (2026-10-16)

The EQs and their visualizers (PRO EQ block, Output EQ page, spectrum strip):

- **ANALYZER on/off** in every view. When it's off, or a view isn't on screen, the audio thread stops
  feeding that spectrum (each one has to be asked for again within half a second) and the view stops
  redrawing. Live, it costs about 0.06% of a core on the audio side and about 1.3% on the screen side.
- **FREEZE:** holds the spectrum (marked "FROZEN") while you keep playing, so you can study a chord or a
  pick attack. Unfreeze to go live again.
- **More colour:** the output spectrum is shaded across frequency (violet lows through blue, teal and
  green to amber and rose highs), each shading down to the floor. Each band's own shape is tinted in its
  node's colour (cuts lightly). The EQ curve glows, and the background fades from navy to black.
- **Band panel** under the graph, for the selected band (pick it with the 1-8 buttons or by clicking a
  node):
  - **Type** (click to cycle, Shift-click back): Bell, Low shelf, High shelf, Low cut, High cut,
    Notch. Any band can be any type.
  - **Slope** for the cuts: 12, 24, 36 or 48 dB/oct (measured 48.2 dB/oct). Q becomes the cut's
    resonance.
  - **ON**, and **FLIP** (turns a boost into the same cut, and back).
  - **FREQ**, **BOOST/CUT** and **Q** dials: drag up/down or use the wheel; Shift is fine;
    double-click returns to default.
  - **-1 dB / +1 dB** buttons.

  Changing a band's type or slope glides it flat, swaps it, and glides the new shape in, so there's
  no click.
- **Trim** knob (the level into the EQ) next to Output, on both EQs.
- **Notch:** a 40 dB-deep, narrow cut (width = Q), for a hum or a harsh resonance.
- The curve you see is still exactly what's processed (0.000 dB difference at every test frequency,
  with 48 dB/oct cuts, a notch, a shelf and Trim). Your saved sound is bit-identical to 1.46.0, and
  your Output EQ settings carry over: older bands keep their type by position.

## 1.46.0 (2026-10-16)

- **Tube Lab on tabs.** The top row is unchanged: On, Circuit, Spkr Load, Economy, **8x** (moved up
  here) and PRESETS/DEBUG. Below it are three tabs, each on the same 4 x 7 grid, so every knob is full
  size with its whole label:
  - **PREAMP:** Drive to Output, the stage-1 parts, Stage 2, Follower, and the tone stack.
  - **POWER AMP:** the power amp, Cath Bias, Spkr FB, Resonance.
  - **FEEL:** the Real Feel switches, Depth, Touch, 60 Hz.

  Before this, the popup had six rows, every dial had shrunk, and the CIRCUIT+ row's labels were cut
  off ("Res...ce"). No sound change.
- **PRO EQ block** (in the pedal list): an 8-band parametric EQ with a live spectrum of what goes into
  it and what comes out.
  - Bands: low cut, low shelf, four bells, high shelf, high cut.
  - Drag the numbered nodes to set frequency and gain. The mouse wheel sets Q, Shift drags finely,
    double-click switches a band on or off (on empty space it moves the nearest band there), and
    Alt-click resets a band.
  - Every move is a parameter, so it saves with the preset and works with MIDI learn.
- **Output EQ** at the very end of the chain, with a **live spectrum strip** above the meters. Click the
  strip for the full Output EQ page: the spectrum of the chain going into the EQ and of what leaves
  the app (after Master Out and the limiter), and the same editable 8-band EQ.
- **How the EQ and spectrum work:**
  - The EQs glide when you move them, so they don't click. The curve drawn is exactly what's
    processed: measured through the plugin, a +9 dB bell read +9.00 dB.
  - Flat (all bands off), an EQ is out of the path, so your sound is bit-identical to 1.45.0.
  - The spectrum is an 8192-point FFT read in 1/48-octave steps. It rises instantly and falls at
    30 dB/s, with a peak-hold line.
  - Cost: about 0.06% of a core on the audio thread, and about 1.3% on the screen side, only while a
    view is showing.
- The header gained no buttons: an EQ button there squeezed the title, so the strip is the way in.

## 1.45.0 (2026-10-16)

The Tube Lab's math: cleaner oversampling, a faster solver, it no longer burns CPU while it's
switched off, and four new physics options in a **CIRCUIT+** row (above REAL FEEL). README
"Oversampling" and "Circuit+" have the details and measurements.

- **CIRCUIT+** (all off by default; your sound is unchanged until you turn them on; presets turn them
  off):
  - **Follower**: the Marshall / 5150 cathode follower between the preamp and the tone stack. Loaded
    by the stack, it cuts off on big downward swings and its grid conducts at the very top, so dig in
    and the crunch gets lopsided and compressed. About 1 dB quieter at your settings (it clips), up
    to 4 dB on hot settings. +3.5% CPU.
  - **Cath Bias**: cathode-biased power tubes (AC30 / Deluxe style). Dig in and they bias themselves
    colder, then bloom back over ~50 ms. Needs the power amp pushed (Master up): at Master 8 the bias
    moves 41 → 47 V and a loud burst leaves the amp ~1 dB down while it recovers. At your Master (2)
    it barely moves, as on a real amp.
  - **Spkr FB**: negative feedback from the speaker side of the output transformer, as real amps do,
    so the transformer's bass and treble roll-offs are inside the loop: +2 dB at 40-70 Hz at your
    settings.
  - **Resonance** knob (Mesa / 5150 style): less feedback below ~150 Hz, so the low end and the
    speaker's thump come back. With Spkr FB, +3.5 dB at 70 Hz at 10; with both up, about +6 dB.
    Limited so it can't motorboat: at its first design it oscillated at 32 Hz on Feedback 10 / Master
    10 with EL34s, as a real amp can; the final one is stable in all 64 extreme combinations tested.
  - **8x**: twice the oversampling for the hottest settings (in-band aliasing on a hard-clipping
    Stage 2: -38 → -52 dB), for twice the Tube Lab's CPU (8.6% → 16.6% at your settings).
- **Fixed: Spkr Load clicked** when flipped mid-note (-13 dB re the note); it now crossfades like the
  other switches (-58 dB).

- **CPU, whole plugin on your chain (128-sample buffer):** 33.7% → **19.7%** of a core as saved
  (Tube Lab footswitched off), 34.1% → **~28.5%** with the Tube Lab on. That's headroom toward a
  smaller buffer, which means lower latency.
  - Switched off, the Tube Lab used to keep running its whole 4x-oversampled circuit unheard.
    Now it sleeps once its Mix has faded out (0.07% instead of 8.6%). Switched on, it wakes from
    rest and settles unheard for 50 ms before fading in, so switch-on is as clean as before or
    cleaner. It takes effect 50 ms after the switch.
  - On: 13.4% → 8.6%. Newton's method now starts from a straight-line prediction, stops at a
    tolerance matched to its quadratic convergence, and skips a redundant full re-solve in the
    power amp. Divisions, a `pow()` and two `exp()` calls are out of the per-sample path. Still
    accurate to 157-179 dB under the signal against a 200-iteration solve, beyond 24-bit.
- **Less aliasing.** The up/down-sampling filter cut off exactly at Nyquist (-6 dB there), so
  harmonics at 24-32 kHz folded back into 16-24 kHz. It's now a Kaiser design with its cutoff
  just under Nyquist: 71 dB of rejection from 28 kHz (was 21 dB), the same length, latency and CPU.
  Aliasing at 18-24 kHz is 13-15 dB lower on every test tone; the top octave is slightly softer
  (-0.5 dB at 16 kHz, after a cab it's inaudible).
- Sound: your saved rig matches 1.44.1 to -133 dB. With the Tube Lab on, the difference is -71 dB
  re the signal (the removed aliasing), with the level unchanged.

## 1.44.1 (2026-10-15)

- **All pedals show in the PEDALS list again.** With the five 1.44.0 pedals there were 21 pedal types,
  and at 4 across only 3 rows fit, so SKY VERB, FLANGE DBL, PREC DRIVE, APEX PRE and CAB 3 sat below
  the fold (reachable only by scrolling). The AMP NEST buttons now go two to a row (AMPS | CAB IRs,
  PEDAL 1 | PEDAL 2; AMP LIBRARY is now called AMPS to fit) and the list goes 5 across, so every
  pedal fits without scrolling. The scrollbar only appears on very short windows. Sound unchanged.

## 1.44.0 (2026-10-14)

Five new pedals (`NewPedals.h`), modelled on how classic pedals behave. Find them in the pedal list and
drag them onto the grid; each has a wider popup with all its controls.

- **SKY VERB** (in the spirit of a Strymon BigSky): Hall, Plate, Room, Shimmer, Cloud and Bloom modes;
  Decay (0.3 s to 40 s), Pre-delay (0-500 ms), Tone, Mod, Mix, and **Freeze** (holds the reverb
  forever; map it to a footswitch). An 8-line feedback delay network with input diffusion, damping and
  slow modulation. Shimmer feeds an octave-up back into the tail; Bloom swells in after each note.
  Measured: RT60 within 4% of the Decay knob (3.44 s for 3.46 s, 39 s for 40 s); Freeze holds its
  level exactly; Shimmer's tail has its octave at -14 dB (Hall: -43 dB). About 2% CPU.
- **FLANGE DBL** (after the MXR 126 Flanger/Doubler): Flanger (0.3-4 ms swept, exponential like a
  BBD) or Doubler (15-70 ms, wandering: the part played twice; the right side 13% later), Manual,
  Width, Speed, Regen (+/-), Mix.
- **WAH**: Cry Baby (350 Hz - 2.2 kHz, sharp +13 dB peak), Vox V847 (450 Hz - 1.6 kHz, rounder +11 dB)
  or Auto (opens with your picking, the same at any level). Position is heel to toe: map it to an
  expression pedal with MIDI learn (that takes CC 4 off the volume pedal). Q, Sens
  (Auto only; greyed out otherwise), Level.
- **PREC DRIVE** (after the Horizon Devices Precision Drive): Volume, Drive, Attack (how much low end
  goes into the clipping: 60-500 Hz high-pass), Gate (0 off, then -90 to -30 dBFS on the input),
  Bright. Anti-aliased clipping (aliasing -50 dB at full drive on a 3.1 kHz tone).
- **APEX PRE** (after the Horizon Devices Apex Preamp): three cascaded gain stages, Gain, Bass, Mid,
  Treble, Presence, Volume, Bright. A preamp: put a CAB after it.
- All of them: knobs glide, switching them on/off and changing modes don't click, off is
  bit-identical to bypass, silence in is silence out, no NaN at any setting. All five in one chain
  cost about 11% of a core. Your saved sound is bit-identical to 1.43.2.

## 1.43.2 (2026-10-14)

- **MIDI learn: click a pedal block to map its on/off.** The blocks on the grid (and in the pedal
  list) weren't mappable -- you had to open a pedal and click the On switch in its popup, so clicking
  a block and pressing a footswitch did nothing. Now a block is tinted like the knobs: clicking it
  selects its On switch (and still opens its popup, for its knobs); AMP, CAB and the FX loop too.
- Sound: bit-identical to 1.43.1.

## 1.43.1 (2026-10-14)

- **MIDI learn replaces what a switch did.** Learning a control onto a footswitch used to add it to
  whatever the switch already did, so REVERB learned onto the A/B switch flipped A/B too (to an
  empty slot B: no amp). Now the switch does only the new control. **Shift-click** a control to add
  it to a switch instead (e.g. both reverbs on one switch).
- MIDI learn's overlay: pedal popups' On switches now show their tint, and the mapping list fits
  more mappings (more columns, short names).
- Your map: switch 1 (CC 80) = FLANGER, switch 2 (CC 81) = REVERB + REVERB 2; A/B is off the pedal.
- Sound: bit-identical to 1.43.0.

## 1.43.0 (2026-10-14)

- **New: MIDI learn**, Ableton-style. Click **MIDI** in the top bar: every knob and switch you can map
  gets a blue tint (green with its CC once mapped). Click one, then press or move a control on your
  MIDI controller -- that's the mapping. Right-click a control to clear it. Click **MIDI** again when
  you're done. Pedal chips and popups still open as usual while it's on, so open a pedal to map its
  On switch or knobs; TUNE can be mapped too. A strip along the bottom shows the last MIDI message
  in and every mapping.
- Switches toggle on every message (a latching footswitch sends 127, then 0). Several switches on
  one CC toggle together: if any of them (in the chain) is on, all go off, else all come on. Knobs
  follow the CC's value across their range.
- Mappings are for the whole app (not a preset), saved in `%LOCALAPPDATA%\NeuralAmpModeler\midimap.txt`,
  shared by the app and the plug-in. They start as the old fixed footswitch map (CC 76-83, toe
  switch CC 64, volume pedal on CC 1/4/7/11), which you can now change.
- Sound: bit-identical to 1.42.1.

## 1.42.1 (2026-10-14)

- **MIDI footswitch 5 (CC 80) switches every reverb in the chain together**: REVERB, REVERB 2 and
  REVERB 3. If any of them is on, a press turns them all off; otherwise it turns them all on, so
  they can't get out of step. It used to switch only REVERB, so your REVERB 2 (in parallel with it)
  stayed on. With no reverb in the chain it switches REVERB, as before.

## 1.42.0 (2026-10-14)

Fixes from a test of the whole app (every block, knob and switch through the plugin, with your
autosave).

- **Stereo IRs load.** The WAV loader refused any file that wasn't mono, so your cab IR ("BLENDED
  3.wav", a stereo file) never loaded: the CAB popup said FAILED and your cab-free DZL capture
  played with no cab at all. Now a stereo IR in the CAB block plays its left channel on the left
  and its right on the right (a right-channel IR you load yourself still wins), and CAB LAB and
  CAB 2 / CAB 3 use the average of its channels. **Your sound changes**: your autosave now has
  the cab you picked (the output is about 0.5 dB louder, much darker, and stereo).
- **No more clicks when switching.** Every block's On switch (all the pedals and their copies,
  CAB, CAB 2/3, CAB LAB, CAPTURE 1/2, AMP), the cab IR switch, and Density/Boost now fade over
  20 ms. Before, they switched between two samples: stomping COMP, OD, CHORUS, DELAY, REVERB,
  TREMOLO, FLANGER or PHASER mid-note clicked 13-37 dB above the note. A block fades its *input*
  in or out, so a delay or reverb fills smoothly, and **DELAY, REVERB, CHORUS, FLANGER, PHASER and
  the cabs now ring out when switched off** (trails) instead of being cut.
- **No more clicks when a knob jumps** (automation, MIDI, a fast turn): the pedals' Mix, Level,
  Drive, Tone, Depth, Feedback, Size and EQ knobs glide over 20 ms; Master In/Out ramp; CAB 2/3
  Level/Pan glide over 20 ms (they ramped over one block); CAPTURE 1/2 Level/Mix glide. A new DELAY
  Time crossfades from the old repeats to the new (30 ms), like a digital delay, instead of
  jumping the read point.
- **A/B switching** fades the amp and cab out and in (8 + 15 ms) instead of clicking, and the
  incoming amp hears the last 100 ms first, so it comes in playing the note. It also runs on the
  audio thread now (it ran on whichever thread changed the param, under the audio).
- **A/B keeps each slot's right-channel cab.** Switching slots left the right channel on the
  other slot's cab.
- **Saved now:** the right-channel cab IR (it was lost on every restart), and the A/B slot that
  isn't live -- its model, cab IR and right IR -- so both slots come back after a restart. An
  older state leaves the other slot empty.
- **CAB LAB is stereo wherever it is.** It folded its mics' pan and the room's width to mono
  unless AMP or another stereo block came first (so a Tube Lab -> CAB LAB rig was mono).
- **No start-up thump.** The Tube Lab's circuit starts from rest and settled with a thump in its
  first ~50 ms: -28 dBFS from the Tube Lab, -13 dBFS after your amp with the gate off (your gate
  hid it). It's now silent for 60 ms after a reset (start-up, a sample-rate change, dragging it
  into the chain) and fades in over 40 ms: the start-up peak went from -13 to -74 dBFS.
- **COMP: Sustain sustains.** Its threshold was fixed and it had no make-up, so at your level
  Sustain 5 did nothing and higher settings only turned it down (12 dB at 10). Now the threshold
  sits 6-40 dB under your recent loudest picks and that much comes back as make-up: the loudest
  picks stay put (within ~3 dB) and the note's tail comes up about 5 dB at Sustain 5-10, the same
  at any input level. Attack 0.3 ms (was 3 ms). **COMP sounds different from 1.41.0.**
- Checked (whole plugin): every pedal but COMP is bit-identical to 1.41.0 when its knobs are
  left alone; every block off is bit-identical to bypass; no NaN or Inf anywhere; a switch-click
  test over 60 switches and knob jumps found none (all within 1 dB of the steady sound).
  CPU unchanged (about 38% of a core at 128 samples on your chain).

## 1.41.0 (2026-10-14)

- **The 60 Hz grind is much heavier at the top of Amount.** Amount now has a steeper top end: up to
  about 6 it's the same as 1.40.0, and from 7 to 10 the supply starves much further (12 dB more at
  10, 30 dB in all) and the 120 Hz wobble grows wider (the headroom swings down to a tenth). Amount
  10 is now a full sputtering fuzz.
- Lab test, a repicked chord: Amount 10 changes the sound by -1.7 dB re the signal (1.40.0: -9.3 dB),
  Amount 8.5 by -8 dB, Amount 5 by -14.5 dB (unchanged). Peaks stay level-matched (within 0.7 dB),
  and the make-up is now set at the wobble's highest headroom so no peak comes out hotter. Amount 10
  is about 3 dB denser (rms), like a fuzz. Switching and Mix still glide without clicks.
- On your chain (whole plugin): Amount 10 changes the sound by -4.1 dB (1.40.0: -11.2), Amount 8.5
  by -8.7 dB, Amount 5 by -18.6 dB (as before). Loudness (rms) is within 0.5 dB, but at Amount 10
  the output's peaks run about 2 dB higher: the extra harmonics peak higher through the amp and
  cabs. Turn on the Limiter or take Master Out down a little if the OUT meter shows CLIP. With 60 Hz
  off, your autosave is bit-identical to 1.40.0.

## 1.40.0 (2026-10-14)

- **The Tube Lab's 60 Hz grind is much stronger, and it has its own Mix.** In 1.39.0 the ripple
  only wobbled the gain by a few percent, which was hard to hear. Now it starves a clipping stage
  after the tubes. Amount lowers that stage's headroom, the 120 Hz ripple wobbles it, and it
  wobbles more when you dig in, so held notes sputter and grind. The clip is lopsided, like a starved
  tube. It's level-matched to your loudest picks, so it changes the texture, not the volume.
- **New knob: Mix** (after Amount, at the end of the REAL FEEL row; 0-100%, default 100%). It's
  the grind's dry/wet: it blends the ground-up signal (and the hum) with the clean Tube Lab sound
  in parallel, with no delay between them. Greyed out while 60 Hz is off.
- On your chain (whole plugin, a test riff): Amount 5 changes the sound by -19 dB re the signal
  (1.39.0: -33 dB), and Amount 10 by -11 dB (was -27 dB). At Mix 50%, those are -25 and -16 dB.
  Loudness stays within 1 dB. With 60 Hz off, your autosave is bit-identical to 1.39.0. Switching
  it on or off, and moving Mix, glide over 20 ms with no clicks.

## 1.39.0 (2026-10-14)

- **New: Hum Filter** (a switch in the AMP popup, under Bass; off by default). It removes 60 Hz
  mains hum and its harmonics (120, 180 ... 480 Hz) from your guitar's signal. It works before the
  gate and the whole chain, even with the amp off. The notches are very narrow: your notes change
  by at most about 0.5 dB (B2, the note closest to 120 Hz), most by under 0.1 dB. Injected hum
  dropped from -43 to -87 dB at the output.
- **New Tube Lab switch: 60 Hz**, with an **Amount** knob (end of the REAL FEEL row; off by
  default, Amount 5). It's a real amp's power supply ripple: a faint 120 Hz hum, plus a slight
  120 Hz wobble in the gain that grows when you play hard, which adds grind to sustained notes. On
  your chain, Amount 5 changes the guitar by -33 dB and Amount 10 by -27 dB (re the signal). The
  hum peaks about -48 dBFS at the output at Amount 5 (-42 at 10). Your Gate shuts it off between notes. Presets
  turn it off. See the README's "60 Hz" section.
- **New: Mix on the REAL FEEL pedal** (dry/wet, 0-100%, default 100%). It blends the pedal's effect
  with its input, with no delay between them, and scales Shared B+ the same way. On your chain at
  Depth 3, the full effect is -6.7 dB re the signal and 50% is -12.7 dB.
- The Tube Lab already had a dry/wet Mix: it's the 5th knob in its top row.
- Checked: with the new features off, your autosave through the whole plugin is bit-identical to
  1.38.2.

## 1.38.2 (2026-10-13)

- **Fix: SAVE (and LOAD) could crash the app.** The buttons' click handlers held a reference to a
  helper that only lived while the window was being built, so a click read freed memory. It worked by
  luck until 1.38.1's layout changes moved things around. The helpers are now copied into the
  buttons. (Your crash: an access violation in `IByteChunk::PutStr` while saving.)

## 1.38.1 (2026-10-13)

- **Fix: the Tube Lab's knobs were dots.** Each knob had a ~47 px cell (the window scales as a whole,
  so resizing never helped). The name above and value below took ~37 px of it, leaving a ~10 px dial,
  and the REAL FEEL row's Touch and Depth had no dial at all. Tube Lab knobs now put the dial on the
  left at the cell's full height, with the name and value stacked beside it, and the Tube Lab card is
  up to 820 px wide (was 620) to make room. Other popups are unchanged except CAB LAB, which shares
  the Tube Lab's card size.

## 1.38.0 (2026-10-13)

- **New Tube Lab knob: Touch** (REAL FEEL row, between Coil Heat and Depth; 0..10, 0 = off).
  Picks softer than your recent hardest also get less drive into the tubes, so they clean up while
  hard picks keep the full sound. The drive cut is undone after the tubes, so loudness and sustain
  don't change, only how dirty a note is. See the README's Touch section. Off is bit-identical to
  1.37 (whole plugin on your autosave, compared sample for sample). It adds no clicks on a held note
  stepping soft/hard, and costs one `pow` every 8 samples. Presets and older states load with it
  at 0.
- **Why the Real Feel "wasn't right" on a test rig** (Tube Lab > AMP (a clean amp capture) > REAL
  FEEL > CAB 2 + CAB 3), measured as distortion of a held note by pick level, through the whole
  plugin:
  - **The Tube Lab got your guitar about 18 dB hotter than it was voiced for.** Calibrate Input is
    off, and the amp capture has no input-level data anyway. The presets and the Real Feel tuning
    assume hard picks at about -19 dBFS at the Tube Lab. At Drive 24 even picks 24 dB softer than
    your hardest were past breakup, so every note was equally distorted.
  - **REAL FEEL's OT Iron at Depth 6.5x acted as a fuzz** on everything after the amp. It saturates
    at about 1/80 of a real transformer's point and pulled hard playing down 6.8 dB. Turning only OT
    Iron off cut the distortion 10-12 dB at every pick level.
  - Before: -14, -10, -8, -8, -5, -3 dB THD from 30 dB under your hard pick to your hard pick. The
    dirt barely tracked your picking.
- **That rig, changed:**
  - Master In -2.2 -> -20 dB, so the Tube Lab gets the level it was voiced for.
  - AMP Gate threshold -53.1 -> -70.9 dB: it listens after Master In.
  - Tube Lab Drive 24 -> 12 dB, Touch 3, Output -13.9 -> -3.65 dB.
  - REAL FEEL Depth 6.5x -> 3x.
  - Master Out +6.1 -> +2.2 dB.
  - After: -52, -47, -43, -28, -17, -14 dB: clean when you pick lightly, gritty in the middle,
    dirty when you dig in. Hard-playing loudness is the same (+0.01 dB).
  - In this chain Touch itself adds about 1 dB of extra clean-up (the capture and REAL FEEL add their
    own dirt after it). Most of the change is the re-gain and Depth.
  - Not changed, as you asked: Economy stays on, the AMP block stays on.

## 1.37.1 (2026-10-13)

- **Fix: the X on the AMP, TUBE LAB and CAB LAB popups did nothing.** Those popups use a bigger card
  than a pedal's, and the shared X was moved there with `SetRECT`, which only moves where a control
  is drawn. Its click area stayed at the small pedal card's corner, so clicks on the visible X went
  to the backdrop behind it, and the backdrop keeps a popup open for clicks inside the card. Now
  the X, card, title and the IN/OUT meters that move into the AMP popup use
  `SetTargetAndDrawRECTs`. Pedal popups (same size as before) were never affected. The Tuner,
  Settings and Tube Lab Debug pages' X buttons hit-tested correctly when checked.
- Found while testing: a lone Alt key press puts the window in Windows menu mode, and the next
  click (on the window's own X too) only leaves that mode. That's standard Windows behaviour, not
  changed here.

## 1.37.0 (2026-10-12)

- **New output limiter** (the Limiter switch under Master Out, off by default). It's a stereo-linked
  lookahead peak limiter at -1 dBFS, before the safety soft clip. Guitar tones have sparse
  sub-millisecond pick peaks several dB above everything else; on your Tube Lab + two cabs setup
  only ~0.1% of samples come within 7 dB of the peak. Turning the master up to make the rest
  louder pushed those into the soft clip. The limiter turns just those down:
  - The gain glides down 0.67 ms ahead of each peak, reaching exactly what the peak needs as it
    plays, and recovers over 30 ms (100 ms pumped audibly after each pick).
  - No output sample can exceed the ceiling, and below the ceiling the gain is exactly 1.
  - It adds 0.67 ms of latency (31 samples at 48 kHz), only while on. Switching it fades out and
    back in (~10 ms).
  - On a test rig: Limiter on, Master Out +6 dB (-3.1 -> +2.9 dB). On a test recording, the average is
    -17.4 dBFS (was -23.1; a JTM45 capture rig was -17.5) with peaks at -1.00 dBFS. The limiting
    differs from the plain boost by -17 to -20 dB, almost all of it the pick peaks.
  - Limiter off: bit-identical to 1.36.0.
- **Fix: COMP in stereo.** The compressor ran each channel's whole block through one shared
  envelope in turn. In stereo (any COMP after the AMP block) its gain jumped between the channels'
  levels at every block boundary: a zipper at the buffer rate with panned sources, and a wobbly
  image. It's now stereo-linked: one detector on the louder channel, one gain for both. Mono is
  unchanged.
- Tried first: COMP 3 at the end to raise the average. With its 3 ms attack the pick peaks pass
  before it reacts, so at the same peak level every setting came out quieter on average (-2.7 to
  -7.7 dB). A peak limiter was the right tool.

## 1.36.0 (2026-10-12)

- **CAB 2 and CAB 3:** two more cab IR blocks in the AMP NEST, for running several cabs. Each has its
  own IR (picked from the same folders as CAB LAB's, saved with the setup by path), On, Level
  (-24..+12 dB) and Pan (balance: centred is unity on both sides).
  - Put them in parallel (one per row in the same column, fed and feeding like CAB) to blend cabs,
    and pan them for width. Or chain them in series.
  - Each takes the signal summed to mono (as a speaker does), convolves it with the fast FFT
    convolution, and outputs stereo.
  - With no IR loaded, a cab passes its input straight through (like the CAB block), so in parallel
    it blends un-cabbed signal until you load one.
  - Checked through the whole plugin on a test rig: unplaced, the sound is bit-identical to 1.35.0.
    CAB 2 with the same IR, in parallel with CAB, reproduces CAB alone exactly. With the SM57 IR it
    blends audibly (-8 dB level-matched). Saved IR paths reload bit-identically.
- **An amp-and-cab capture doesn't want the CAB block.** A capture with `gear_type: amp_cab` already
  includes a mic'd cab. A 4x12 IR in the CAB block on top made it two cabs in series: measured, it
  scooped 500 Hz-2 kHz by about 9 dB and took 15 dB off 6-10 kHz. Switch CAB off for those captures,
  or use it after an amp-only capture or the Tube Lab.

## 1.35.0 (2026-10-12)

- **Half the CPU, for a lower-latency buffer.** Measured on a test rig (Tube Lab > AMP > REAL FEEL >
  CAB > reverbs), whole plugin at a 256 buffer: 65% of each block's time before, about 32% now.
  The slowest blocks went from 114% (crackle territory) to 66%. The sound is unchanged: -133 dB
  from 1.34.0, which is rounding.
  - **Tube Lab solver:** each Newton step solved the preamp's five circuit nodes with a full 5x5
    elimination. The input-cap and output nodes are linear and each tied to one tube node, so
    they're now folded in once per parameter change, leaving a 3x3 solve (Cramer's rule). The
    per-sample copy of the fixed matrix is gone. Same equations; the stock presets match the old
    solver to -234 dB.
  - **Tube Lab upsampler:** the 4x upsampling filter summed all 61 taps on every oversampled
    sample, though three in four meet zero-stuffed input. It now sums only the non-zero terms, in
    the same order, so the result is bit-identical.
  - **CAB IR:** the cab IR was convolved by a dot product per sample (8192 taps, ~5% of a core per
    side). It's now an FFT convolution (128-sample partitions, the first done directly, so no added
    latency): 4.2x cheaper, matching the old one to -141 dB (`FastImpulseResponse.h`).
- **New Economy switch** in the Tube Lab's top row (off by default): 2x oversampling instead of 4x
  (1x at 88.2/96 kHz). It roughly halves the Tube Lab's own cost. On your playing it differs from
  full quality by -26.5 dB (level-matched, no level change), and no extra aliasing showed at your
  level. Flipping it restarts the Tube Lab with a ~40 ms fade out and back in (no click, measured
  on a held note and on chords).

## 1.34.0 (2026-10-12)

- **New REAL FEEL pedal**, a tandem pedal with the Tube Lab. Measured on a test rig, the Real Feel
  switches did almost nothing to the final sound: a 5150 capture after the Tube Lab flattens
  whatever the Tube Lab's power amp does (Squish 4x: +0.3 dB on the pick). Drag REAL FEEL from the
  AMP NEST and drop it after the AMP block. The switches then act there, after the capture, where
  a real amp's power section is.
  - **Tandem:** its On switch is the Tube Lab's, and its switches and Depth are the Tube Lab's Real
    Feel controls (either popup sets both). While it's on the grid, the switches act in the pedal,
    not in the Tube Lab.
  - **Squish:** the pick is squashed (in ~20 ms) and blooms back (~150 ms).
  - **Shared B+:** as the supply sags it turns down the signal into the AMP block (up to 6 dB), which
    changes how hard the capture is driven.
  - **OT Iron:** big low notes lose low end.
  - **Coil Heat:** a few seconds of loud playing makes it slightly quieter, with more low end around
    the speaker's resonance.
  - On your chain at your playing level, all four at 4x: the pick drops 4.0 dB and the whole thing
    is 4.9 dB quieter; the sound differs from the pedal off by -8.4 dB (level-matched). At 4x
    Squish alone takes 2.3 dB off the pick (2.9 dB just after).
  - With every switch off, or the Tube Lab off, the pedal changes nothing (bit-identical). Without the
    pedal on the grid, everything sounds exactly as in 1.33.3, including your saved setup.
  - No clicks: every switch flip, Depth 1x<->10x and 4x<->7x, and the Tube Lab's On switch, measured
    on a held sine and on chords.
- The chain grid's cable masks widened from 32 to 64 bits (REAL FEEL is the 33rd block). Older saves
  load unchanged, and no existing parameter moved (the new block's grid params are at the end).
- Fixed garbled characters in the README's 1.33.3 notes.

## 1.33.3 (2026-10-12)

- **Squish is stronger at your level.** At moderate levels the screens only sag about 10%, under 1 dB
  of tube gain, so the pick's squash was hard to hear. Above 1x, Squish now pulls the gain
  `2 x (Depth - 1)` times harder on top of the feedback-proofing, soft-capped at 7 dB
  (`0.8 Np x tanh`) so settings whose screens already sag a lot aren't crushed. It also works with
  Feedback at 0. 1x is still the real part.
  Squish alone on your current settings, hard chord at your playing level:

  | Depth | Pick (first 0.3 s) | 0.3-1 s | Held (4-5.5 s) | Overall level |
  |---|---|---|---|---|
  | 4x | 4.3 dB (was 0.7) | 1.9 dB (was 0.3) | 0.2 dB | -2.3 dB |
  | 7x | 6.0 dB (was 0.9) | 3.3 dB (was 0.4) | 0.4 dB | -3.5 dB |
  | 10x | 6.0 dB (was 1.2) | 3.7 dB (was 0.6) | 0.5 dB | -3.9 dB |

  All four on at 4x: the pick drops 8.5 dB (was 5.4) and the overall level about 4 dB (was 2.7).
- **Fix: turning Depth with Squish on clicked** on clean settings with little Feedback (a 1x to 10x
  jump on a held note, -20 dB re the note's peak; 1.33.2's click test played chords, which hid it).
  Squish's Depth-dependent parts (its screen supply, the lift that keeps the screens' rest voltage,
  and B+'s resting supply) now glide through two 30 ms one-poles. The same jump measures at the noise
  floor.
- Switches off: bit-identical to 1.33.2. CPU unchanged.

## 1.33.2 (2026-10-12)

- **Real Feel is audible with Feedback at 8.2.** Measured on your current settings, all four on, on a
  hard held chord at your playing level:

  | Depth | Attack (first second) | Held (4-5.5 s) | Difference from switches-off |
  |---|---|---|---|
  | 1x | 0.2 dB | 0.05 dB | -42 dB |
  | 4x (default) | 3.8 dB | 0.7 dB | -14 dB |
  | 7x | 7.9 dB | 2.7 dB | -11 dB |
  | 10x | 7.9 dB | 4.3 dB | -9 dB |

  In 1.33.1, 4x was 0.4 dB and -37 dB. Each switch alone at 4x: Squish 0.3 dB, Shared B+ 0.9 dB, Coil
  Heat 0.6 dB on the attack (0.5 dB still down when held), OT Iron 0.3 dB less low end. At 7x OT Iron
  takes 4 dB off the low end, and at 10x 10 dB: an undersized transformer at its limit.
- **Feedback-proof:** with Squish or Shared B+ on, the power amp gives back the gain change the
  feedback would have cancelled. That's the open-loop change (the tubes' gain follows
  `(Vg2/rest)^Ex`, the phase inverter's swing its supply), raised to the feedback's share of the loop,
  `beta/(1+beta)`. With Feedback at 0 nothing changes; at 8.2 it gives back about 62%. A real amp
  doesn't do this. The change glides (about 5 ms).
- **Depth hits harder:** strength = Depth² (1x still the real parts, 4x = 16, 10x = 100).
  - Squish's knee widens and its screen pull grows with √strength, so the screens act on the swings
    you play, not only when the plates bottom out.
  - OT Iron's saturation point is λs/(2·strength - 1).
- **Clicks:**
  - Squish's whole screen circuit now crossfades over 20 ms when switched (it clicked once Depth was
    strong).
  - A Depth jump keeps OT Iron's core current continuous.
  - Every switch flip, and Depth jumps 1x↔10x and 4x↔7x, measured on a clean sine and on cranked
    Plexi: none stands out from the signal.
- CPU on your settings: 35% with all four on vs 31-35% off (run-to-run noise). The Depth math is
  computed once per change, not every sample. Switches off: bit-identical to 1.33.1.

## 1.33.1 (2026-10-12)

- **Real Feel: a Depth knob** at the end of the Real Feel row, so the switches can be made audible
  at your level. 1x is the real parts of 1.33.0. Higher exaggerates all four together, up to 10x:
  - a screen supply that many times softer
  - a transformer saturating at that fraction of the flux
  - a voice coil heating that many times faster per watt
  - the preamp's and phase inverter's supplies moving that many times the screens' sag

  The default is 4x. It's saved by name, and older saves load at 4x.
- **Why they were quiet:** your Feedback (8.2) cancels most of what they do, as heavy negative
  feedback does in a real amp (it's why blackface-style amps feel stiff). All four switches on your
  current settings, measured at -19 dBFS peaks into the Tube Lab:

  | Feedback | Difference from switches-off, 1x | Sag on a held chord, 1x | Difference, 4x | Sag, 4x |
  |---|---|---|---|---|
  | 8.2 (yours) | -49 dB | 0.1 dB | -37 dB | 0.4 dB |
  | 4 | -39 dB | 0.25 dB | -27 dB | 1.0 dB |
  | 0 | -18 dB | 1.1 dB | -11 dB | 2.9 dB |

  At 10x with your Feedback: -29 dB, about 0.9 dB of sag on the attack. Plexi Crunch at 10x:
  -16 dB, and the held chord drops 4.6 dB, then recovers to 2.1 dB down. So on your settings,
  lowering Feedback is still the bigger lever.
- **Squish no longer just makes things quieter.** Its softer screen supply idled lower, which made
  everything 0.3-1.3 dB quieter whatever you played. Its feed is now set so the screens idle where
  they did, and only the squish itself moves them.
- **Shared B+ now reaches the phase inverter too:** its clipping headroom follows the screen
  supply (0.05 s decoupling), as on a real amp.
- **No clicks when switching OT Iron or turning Depth:**
  - The plain and saturating transformer models now run side by side, with a 20 ms S-curve
    crossfade when OT Iron flips. Handing over instantly kinked the waveform, a click 40 dB over the
    signal's own edges at 10x.
  - Depth glides the saturation point through two smoothers. With one, a 10x-to-1x jump clicked.
  - Every switch flip and a 1x-to-10x Depth jump was measured on a clean sine and on cranked Plexi:
    none of them stands out from the signal.
- With the switches off, the output is bit-identical to 1.33.0. CPU with all four on is within run
  to run noise of off on your settings, and +3 points (36% vs 33%) on cranked Plexi at 10x.

## 1.33.0 (2026-10-12)

- **Fixed: the Tube Lab's circuit solve stopped short of the answer on hard-driven settings.** Each
  sample, Newton's method solves the circuit, and on some samples it gave up before finding the
  answer. The result was junk in the output, heard as harshness and crackle that isn't in the
  circuit. Measured against a solve run to full convergence, on the guitar test material at your
  playing level (worst sample, re the output's peak):
  - Plexi Crunch -17 dB, Modern High Gain -20 dB, 800 Lead -30 dB. A cranked stage 2 (Drive 36,
    Gain 2 at 10, Master 10) was off by nearly its whole peak (-0.7 dB).
  - Blackface Clean, Tweed Breakup, Top Boost Chime, Singing Lead and your current settings were
    already fine. On your autosave through the whole plugin, 1.33.0 differs from 1.32.3 by -136 dB.
  - **Preamp stages:** each Newton step was held to 2 V on the input, grid and cathode. Stage 2's
    input follows stage 1's plate, which can move tens of volts in one sample, so 8 steps of 2 V
    couldn't catch up. It's now only a loose 100 V sanity limit, and 8 iterations converge
    everywhere.
  - **Power amp:** a plain Newton with a fixed +-50 mA step could bounce between the same two points
    forever, when one tube was cut off and the other's grid conducting. (A 400-iteration run of the
    old solver was still stuck.) Now each power-tube grid is solved exactly for the current
    difference current. That leaves one equation that rises monotonically, so the answer is
    bracketed, and a step that would leave the bracket bisects it. On the grid's usual linear
    stretch (below -2 V) it's solved directly, so CPU is unchanged (within run-to-run noise on the
    test harness).
  - Checked on 30 random settings, with inputs up to 12 dB hotter than you play: every output is
    finite and within -146 dB of the full solve.
- **Real Feel switches:** four new switches along the bottom of the Tube Lab, each a physical
  effect of a real amp. All are off by default, and every preset turns them off. With them off,
  the sound is exactly as before. All need Power Amp on. README, "Real Feel switches", has the
  math.
  - **Squish:** the power tubes' screens take the current the plates can't collect when the plates
    swing low. The screen supply sags and the power amp compresses. Cranked Plexi: the screens drop
    to 390 V (422 V without it).
  - **Shared B+:** the preamp's B+ hangs off the power amp's screen supply through a decoupling
    filter, so hard power-amp hits sag the preamp too. Cranked Plexi: the preamp rail dips to
    242 V (249 V without it).
  - **OT Iron:** the output transformer's primary is a saturating inductor. Big, sustained low notes
    at volume compress and thicken. Played material through the presets stays below it (their bass
    cuts leave little low end at the plates). A held, loud 82 Hz note reaches it.
  - **Coil Heat** (also needs Spkr Load): the speaker's voice coil warms over a few seconds of loud
    playing, and its resistance rises 0.39% per degree. Cranked Plexi: +20 K.
  - At your playing level these are subtle. They're physical, so they act when the power amp
    works hard: Master up, sustained notes. All four on your current settings change the output by
    -55 dB. No click when flipped while playing. CPU is unchanged.
  - Old saves load with them off.

## 1.32.3 (2026-10-11)

- **Tube Lab debug page: the circuit map's values now show the signal.** Each point used to show its
  average over the last 50 ms, which is about 0 for anything carrying the guitar signal: IN, the
  tone stack's out and OUT read "0.03 V" and "-0.00 FS" while the readout said Saturated, as if
  nothing reached the power amp. Now:
  - Signal points (IN, out, OUT) show their peak: `OUT ±0.38 FS`.
  - Points that sit at a DC level and swing around it (grids, plates, cathode, currents) show the
    level plus the swing, when it's more than 3% of the level: `P 189 ±79 V`.
  - Supply rails, screens and blocking show the level only (they move slowly).
  - No more "-0.00". Hovering a point still gives the average and the min-to-max range.
- **Circuit map redrawn so nothing overlaps**, laid out in a preview at the page's size first:
  - The cathode resistor and bypass cap of V1 and V2 now go to ground (they ended in mid-air).
  - Value labels no longer cross wires, parts or each other (V1 and V2's Vgk, V2's plate over the
    tone stack box, the stack's out on its wire, the blocking label over the grid leak). V2's Vgk
    and the two outputs sit on short leader lines where there's room.
  - Power amp: the screens hang off one feed between the tubes, through a screen resistor from the
    B+ center tap (it was diagonal lines across the tubes and a plain wire). The cathode leads are
    short, and the tube names moved clear of the labels. The phase inverter's outputs go up and
    down to the tubes' grids.
  - The map is a bit bigger: 62% of the page's width (was 56%) and 58% of its height (was 54%).
    The readout is a little narrower and the scope a little shorter.

## 1.32.2 (2026-10-11)

- **Fixed faint lines from the main screen showing through full-window pages** (seen as a stray
  horizontal line, or the tops of the Tube Lab's knob labels, across the debug page's readout and
  between its panels). Only at some window sizes.
  - Cause (iPlug2's NanoVG drawing): the offscreen frame buffer is whole device pixels,
    `WindowWidth() * screen scale` truncated, but NanoVG was told the untruncated size. When that
    product isn't whole (a resized window 663.6 units tall at 2.5x scaling gets a 1657 px
    buffer for 1657.5 px of drawing), everything was squeezed by a fraction of a pixel. Each
    redrawn region's clip edge then fell mid-pixel, and NanoVG's anti-aliased clipping blended
    whatever was drawn underneath into those edge pixels.
  - Found with a temporary draw log (every region and every control drawn in it, 120 frames): the
    draw order was correct in all 490 regions (the page background always drawn after everything
    under it), which ruled out a layering bug and pointed at the renderer.
  - Fix 1: NanoVG is given the view size that exactly matches the buffer (four frame-begin calls
    and the final copy to the window). At such sizes this also stops the whole UI being resampled
    by a fraction of a pixel on its way to the screen, which had smeared one row into the next (the
    full-width faint line). iPlug2 local edit in `IGraphics/Drawing/IGraphicsNanoVG.cpp`, alongside
    the existing ones in `IControl.h` and `IPlugAPP_host.h`.
  - Fix 2: one row still leaked about 40% of the AMP NEST's PEDAL 2 button (its edge, right at a
    redraw seam) for a reason the renderer's math doesn't account for. So while a full-window page
    (TUBE LAB DEBUG, TUNER) is open, the controls entirely underneath it are now hidden and not
    drawn at all, and exactly those are shown again when it closes (`NAMOverlayPanelControl::Hide`).
    Nothing under the page is left to bleed through, and the frames under it cost less to draw.
  - Measured on screen in a resized window, in two areas that should be flat background (the
    gap above the scope, and the strip the stray line ran along): 1.32.1 had 4 off-background rows
    (up to 58 grey levels off), with fix 1 alone 1 row (35 levels), with both 0.

## 1.32.1 (2026-10-11)

- Fixed the full-window pages' titles (TUBE LAB DEBUG, TUNER) being cut off: the title box was only
  10 px tall for 26 px text.

## 1.32.0 (2026-10-11)

- **Tube Lab debug page.** The DEBUG button in the Tube Lab (under PRESETS) opens a full-window page
  that shows what the circuit simulation is doing while you play:
  - **Circuit map:** the Tube Lab drawn as a schematic (input cap, V1, Gain 2 + V2, tone stack,
    phase inverter, the two power tubes, output transformer, speaker), with the live voltage at
    each probe point. Points turn amber where the circuit is clipping right now (grid conducting,
    or a tube cut off). Sections that are switched off are greyed out. Click a point to put it on
    scope channel A; right-click for channel B.
  - **Oscilloscope:** two channels on any of 20 probe points (V1/V2 grid-cathode, plates, cathode,
    plate current, preamp B+, blocking, tone stack out, power-tube grids, plates and currents,
    power B+, screens, output). Triggered at 0.5 to 20 ms/div; rolling at 50 to 200 ms/div for slow
    things like sag and blocking recovery. Each channel auto-scales and shows its range. FREEZE
    holds the picture.
  - **Readout:** plain-English lines, updated 4 times a second, on what each setting is doing and
    how it plays. An overall feel (Clean / Edge of breakup / Crunch / Overdrive / Saturated, from
    how much of the time any stage clips), then per stage: where the bias sits and what that does,
    the cathode cap's bass corner, the plate cap's treble corner, how far the grid and plate swing,
    how much of the time it clips and on which side, and blocking (how far the coupling cap pushes
    the bias colder, and how long it takes to recover). Then sag, the tone stack, the power amp
    (bias, grid current, crossover / one tube cut off, B+ and screen sag, feedback), the output
    level, and CPU use. Between notes it keeps explaining the last thing played for 10 s.
  - **CSV log:** CSV LOG: ON writes one row per 0.25 s to
    `%LOCALAPPDATA%\NeuralAmpModeler\debug-logs\griffin-debug-<date>-<time>.csv`: every Tube Lab
    setting, a `changes` column ("Drive 12.0 -> 18.0; Bias -1.50 -> -2.00"), the measurements
    above, the feel, CPU, sample rate, buffer, latency and the signal chain. Silent stretches log
    only every 5 s or when a setting changes. OPEN LOG FOLDER opens the folder.
  - The page stays open across window resizes, and the log keeps running with it closed.
- How it works: `TubeProbe.h` captures node voltages and statistics from the circuit on the audio
  thread (lock-free, channel 0 only) only while the page is open or the log is on; `TubeDebug.h`
  turns them into the readout and CSV; `TubeDebugControls.h` is the page.
  - Blocking is measured as the 10 Hz low-passed charge of the coupling cap (the cap also carries
    some signal); "cut off" is the tube's own plate current under 5% of idle; "grid conducting" is
    Vgk above -0.2 V (about 1 uA of grid current).
  - Sound unchanged: a bit-exact test against 1.30.0's Tube Lab (4 settings x 48/96 kHz, with a knob
    turn and a switch flip mid-note) matched, with the probe off and with it on. Capturing costs
    about 6% more Tube Lab CPU while the page or log is on, nothing otherwise.

## 1.31.0 (2026-10-10)

- **Audio buffer on the Settings page.** In the standalone, click 128 / 192 / 256 / 384 / 512 /
  1024 under AUDIO BUFFER: it saves to settings.ini and restarts the audio, like the Preferences
  dialog. The line under it shows the size in ms. In a DAW the row only shows the host's buffer
  and says to change it in the DAW's audio settings (a plug-in can't set the host's buffer).
  - The Tube Lab (4x oversampled circuit simulation) dropped out at 192 samples on the test machine;
    256 fixed it.
  - Applied on the next idle tick, outside the click handler.
  - iPlug2 local edit: public `IPlugAPPHost::SetBufferSize()` (sets the state's buffer, saves the
    INI, `TryToChangeAudio()`). Included in `iplug2-changes.patch` in backups.

## 1.30.0 (2026-10-10)

- **Toe switch = Tube Lab on/off.** A foot controller's toe switch (under the expression pedal,
  CC 64) toggles the Tube Lab block, counted the same way as the footswitches (every message is one
  press).

## 1.29.4 (2026-10-10)

- Tuner confirmed on the real guitar with 1.29.3: all six open strings read (82.0, 109.5-110.5,
  146.3-146.7, 197.4, 246.7-249.4, 330.3 Hz) from strums peaking about -35 to -45 dBFS raw.
- Removed the 1.29.2 diagnostics (tuner-diag.log). The tuner panel's "Input: xx dB" readout stays.

## 1.29.3 (2026-10-10)

- **Tuner reads the raw input**, before the Input knob and input calibration. The 1.29.2 log showed
  why it read nothing: with the 5150 capture's calibration the input gain was 0.133 (-17.5 dB), and
  a hard strum on the test interface peaked at only about -50 dBFS raw (-68 dB after that gain), with
  the idle noise near -92 dBFS. The amp's own gain hides how quiet that is; the tuner couldn't.
- Gate retuned for low interface levels: 12 dB over the tracked noise floor (was 6 dB), -90 dBFS
  minimum (was -74), and the floor now creeps up ~0.3 dB/s (was ~1.1 dB/s per detection rate),
  slower than a ringing note decays. The log showed the old rate letting the floor catch up with a
  fading note and gate it out.
- Test at the measured levels (noise ~-92 dBFS, strum peaking -50/-60/-70 dBFS, decaying, with and
  without 60 Hz hum at -90 dBFS), all six open strings: no wrong readings in any case; a -50 dBFS
  strum reads for ~4.8 s, a -60 dBFS one for ~3.2 s. At 6 dB, notes fading into the hum misread.
- The tuner-diag.log diagnostics from 1.29.2 are still in.

## 1.29.2 (2026-10-10)

- Diagnostic build: the standalone logs the tuner's input once a second (Process calls, peak and
  RMS level, noise floor, detected pitch, input gain, panel visibility) to
  `%LOCALAPPDATA%\NeuralAmpModeler\tuner-diag.log`, to find why the tuner reads nothing while the
  amp is clearly getting the guitar. To be removed once found.

## 1.29.1 (2026-10-10)

- **Tuner fix: it now reads a normally played guitar.** Pitch detection itself was fine (correct on
  every string in a synthetic test), but a fixed level gate ignored anything under -50 dBFS RMS, so
  a guitar on a modest interface level never showed a note, or dropped out within a second.
  - The gate is now relative to the input's own noise floor: 6 dB over the floor, with a -74 dBFS
    minimum. The floor follows quiet windows down at once and rises ~0.7 dB/s, so a ringing note
    doesn't raise it much.
  - Test (3 s of noise plus 60 Hz/180 Hz hum at about -58 dBFS peak, then a note decaying 1/s):
    no false readings from the hum on any run. A -37 dB note reads for ~1.8 s (was ~1.3 s with a
    4x gate, less with the old fixed one); a -53 dB note now reads briefly.
  - While no pitch is found, the panel shows the input level under "Play a note..." ("Input:
    -42 dB" or "Input: no signal"), to tell a missing or quiet signal from an unreadable one.

## 1.29.0 (2026-10-10)

- **MIDI footswitch control** from a MIDI foot controller. MIDI input is
  now on (`PLUG_DOES_MIDI_IN 1`); in the standalone, pick the pedal's port in Settings > MIDI input.
  - Footswitches = CC 76-83 on any channel. 1 Compressor, 2 Overdrive, 3 Chorus, 4 Delay,
    5 Reverb (on/off), 6 A/B amp, 7 tuner show/hide, 8 FX loop on/off.
  - Every message counts as one press and toggles the app's current state, so the pedal's own
    latching (127 on one press, 0 on the next) can never get out of step with the app.
  - Expression pedal (CC 1, 4, 7 or 11) = **volume pedal** on the final output: squared taper,
    10 ms glide. It isn't saved and starts at full volume, so it does nothing until moved.
  - Presses are counted on the audio thread and applied on the next GUI idle tick, where the
    params, host and controls are updated together; switching marks the autosave dirty as usual.

## 1.28.0 (2026-10-10)

- **New PHASER pedal**, in the AMP NEST's pedal list. Knobs: **Rate** (0.05-8 Hz), **Depth**,
  **Regen** (-10..+10: positive sharpens it toward the "vocal" sound, negative softens it) and
  **Mix** (5 = the deepest notches).
  - Classic 4-stage design (Phase 90 / Small Stone style): four first-order allpass filters on one
    swept break frequency, sine LFO, exponential sweep from 150 Hz up to ~4 kHz at full Depth;
    regen from the last stage back into the first, through tanh. The right side sweeps 90 degrees
    ahead in stereo. Knob moves glide over 10 ms.
  - Level-matched: on guitar after the 5150, switching it on at the defaults changes the level by
    -0.3 dB. Notches about -21 dB deep at the defaults; stable with every knob at its extremes.
    At Regen -10 the notches are shallow (about -6 dB): that setting is a gentle, hollow swirl.
- Grid id 31. That fills the chain grid's 32-bit cable masks, so their params (GridIn*, GridOut)
  are now stepped doubles holding up to 2^32 - 1; int params stop at 2^31 - 1. Saved states load
  unchanged: the 1.27 autosave and a 1.27 setup with the Flanger in it render bit-exact through
  1.27.0 and 1.28.0, and cables from the Phaser (bit 31) save and route correctly.
- Another new pedal type will need wider masks (a second param per mask).

## 1.27.0 (2026-10-10)

- **New FLANGER pedal**, in the AMP NEST's pedal list -- drag it onto the grid like any other pedal.
  Knobs: **Rate** (0.05-5 Hz), **Depth** (how far it sweeps), **Regen** (-10..+10: positive is
  metallic/jet, negative is hollow) and **Mix** (5 = an even blend, the deepest notches).
  - A 0.25 ms to ~7 ms delay swept by a triangle LFO, exponential in delay time so the notches
    move evenly in pitch (bucket-brigade style); Hermite-interpolated delay line; tanh in the regen
    path so it can't run away. In stereo (after the amp) the right side sweeps 90 degrees ahead.
    Knob moves glide over 10 ms (no clicks).
  - Level-matched: switching it on at the defaults changes the level by -0.2 dB on guitar (the
    notches would otherwise cost about 4 dB). At Regen +/-10 with Mix 8 it's about 3 dB quieter.
  - Measured on noise: notches about -28 dB deep, the first one sweeping ~90 Hz to ~1.3 kHz at the
    defaults; stable (no runaway, no NaNs) with every knob at its extremes.
- One copy (grid id 30, after every older id). Saved states from 1.26 load unchanged: rendering the
  current autosave through 1.26.0 and 1.27.0 is bit-exact.
- Fixed a latent overflow: the grid's cable-mask param range was `(1 << ids) - 1` in an int, which
  overflows at 31 ids; now computed in 64 bits.

## 1.26.0 (2026-10-09)

- **Latency readout** along the bottom of the window: the audio buffer and how long it is, the
  chain's own processing delay (resampled models, Tube Lab oversampling, parallel-path alignment),
  and the resulting round trip ("+ driver" -- the interface's own converter/safety delay isn't
  visible to the app). In the standalone app the buffer is the audio device's, read from its
  settings; in a plugin host it's the block size the host calls us with.
- Measured on a USB interface with ASIO, 192 samples @ 48 kHz = 4.0 ms each way, processing
  delay 0.0 ms -- so the latency is the buffer, not the app.
- Checked the "app keeps running after closing" report: clicking the window's X exits the process
  immediately; the stray processes found earlier still had their window open.

## 1.25.2 (2026-10-09)

- **Fixed the extra pedal copies (REVERB 2 / 3, DELAY 2, ...) being drawn in the wrong place.** The
  chips looked up their grid position by block id instead of by param slot, and every id after
  Input sits one slot lower -- so each copy was drawn (and clickable) where the *next* copy really
  was, and the last one fell back to the first cell, at the start of the chain. The audio always
  used the right positions, so cabling what you saw wired up different blocks than you meant,
  which is why the extra reverbs seemed not to work. Only the originals were unaffected.

## 1.25.1 (2026-10-09)

- **A stray click can no longer cut a cable.** Clicking a cable removes it only if it was already
  highlighted under the pointer right where you click (you were visibly aiming at it). A click on
  AMP right after launch once landed on the grid instead and silently removed CHORUS -> AMP,
  breaking the chain; the first click after a launch has been seen landing off target three
  times (likely the screen scale settling: startup lays out at 900x500, then 720x400 at a 2.0
  screen scale, then 864x480 at 2.5). Root cause still under investigation -- the debug log
  (`%TEMP%\griffin_drag.log`) now also records grid clicks, window resizes and relayouts.

## 1.25.0 (2026-10-09)

- **Smooth, accurate meters.** The bars glide between readings every frame instead of stepping
  when a new reading arrives: rising within ~10 ms (peaks show at their true height) and falling
  back over ~120 ms. The peak line holds for 1 s, then falls at 20 dB/s -- timed, so the same at any
  frame rate (it used to fall a fixed amount per redraw). A clip stays flagged (red outline, red
  readout) for 2 s. New **peak readout in dB** at the loud end of the IN and OUT meters. Drawn at
  sub-pixel precision. The level detector's own decay went from 300 ms to 100 ms so the two
  smoothings don't add up to lag.

## 1.24.3 (2026-10-09)

- AMP popup: removed the pale ribbed "grille" strip along its bottom edge and the dark corner
  squares, and closed the empty row the IR picker left behind (it moved to CAB in 1.24.0) -- the
  knob row gets that space.
- AMP popup knob labels and values are sized to their columns (11 pt up to the old size), so
  "Master Out" / "-82.3 dB" no longer fill their column edge to edge.

## 1.24.2 (2026-10-09)

- **Text fits everywhere.** Every label, button caption, knob value and file name is shrunk to fit
  its space (down to 70% of its size), and only if it still doesn't fit is it shortened in the
  middle with "..." -- instead of running past its edges. Done once in iPlug2's shared label/value
  drawing (`IVectorBase::DrawTextFitted` in `iPlug2/IGraphics/IControl.h` -- **a change inside the
  iPlug2 submodule**, kept as `iplug2-changes.patch` in the backups), plus the pedal chips' names
  and the switch labels.
- Model / IR pickers show the whole file name when it fits (they used to cut every name at 45
  characters); pedal chips no longer cut names at 10-14 characters first.

## 1.24.1 (2026-10-09)

- Fixed the model / IR pickers showing "Select model..." / "Select IR..." after a relayout even
  though a file was loaded (since 1.23.4 the UI relays out on every window resize, including at
  startup). The loaded file names are now re-sent after every relayout, not only when the editor
  opens.
- Temporary: chip mouse-down / mouse-up / drop events are logged to `%TEMP%\griffin_drag.log`, to
  pin down the reported drag-to-AMP-NEST failure. To be removed once found.

## 1.24.0 (2026-10-09)

- **CAB works:** clicking the CAB block opens its controls -- an **On** switch, the **IR picker**, and
  the **right-channel IR** (stereo cab) with its clear button. They moved out of the AMP popup (it
  used to hold them, and CAB's popup just said "IR loading is under the amp's Model picker").
- **Meters are bright green**, turning **bright red** in the top 3 dB, with a lit highlight. When a
  channel clips, its whole bar gets a red outline along with the red peak line.

## 1.23.4 (2026-10-09)

- **Maximizing the window fills it** (no more white strip down the right side). The standalone
  window's own resizes (maximize, restore, dragging its frame) were reported to the plugin but
  ignored (iPlug2's default `OnParentWindowResize` does nothing), so the UI stayed at its old size.
  It now follows the window at the current scale and relays out, like the corner resizer does.

## 1.23.3 (2026-10-09)

- Fixed the first click after startup sometimes opening the wrong block's popup (clicking DELAY
  opened CAB, the block under it). AMP, CAB and the loop chips now refresh their click areas every
  idle tick, like the pedals have since 1.22.1, instead of only when they happen to be redrawn.

## 1.23.2 (2026-10-09)

- **Popups no longer close when you click inside them.** The card doesn't take clicks itself, so a
  click on any empty part of a pedal's / the amp's / the FX loop's popup fell through to the dimmed
  backdrop behind it, which closes the popup. Clicks inside the card are now ignored by the
  backdrop; clicking outside the popup or its X still closes it.

## 1.23.1 (2026-10-09)

- **On/off switches redrawn** (all of them: amp, pedals, FX loop, Tube Lab, CAB LAB, settings):
  a rounded track with a round knob -- right and blue when on, left and grey when off -- and the
  label underneath, dimmed when off. The old ones drew the handle bitmap as a small blob stuck to
  the top of the pill and let the label run into it.

## 1.23.0 (2026-10-09)

- **FX loop On/Off and Mix.** Clicking LOOP OUT or LOOP IN opens an **FX LOOP** popup with an
  **On** switch and a **Mix** knob. Both chips light up while the loop is on.
  - Off: nothing goes out the loop's send (the aux output carries silence), and LOOP IN passes the
    chain straight through.
  - Mix: 100% (the default) = the return replaces the signal at LOOP IN, as before; lower blends the
    signal arriving at LOOP IN back in (ramped per block, no clicks). The return comes back later
    than the dry signal (by the host's/hardware's round trip), so a partial mix of a mostly-dry
    return can sound a little phasey -- the same as a hardware loop's mix knob.
  - New params `LoopEngaged` / `LoopMix`, saved by name after every older param: older presets load
    with the loop on at 100%, i.e. exactly as before.
  - The loop only does anything when the host connects the aux bus (a DAW with the VST3); in the
    standalone app it's a pass-through either way.

## 1.22.2 (2026-10-09)

- With the AMP NEST panel hidden, the title row (and the A/B switch) no longer runs under the
  LOAD / SAVE / TUNE buttons.

## 1.22.1 (2026-10-09)

- Fixed pedals in the AMP NEST list going invisible / undraggable after another pedal was dragged
  in or out of the chain (the packed list moves them, and a moved chip only updated where it could
  be clicked when it happened to be redrawn). Chip positions are now refreshed every idle tick.

## 1.22.0 (2026-10-09)

UI polish.

- **Meters:** IN and OUT are now horizontal bars along the bottom of the main page, and **OUT is
  stereo** (separate L and R bars; with one output connected both show it). Redrawn from scratch:
  slim bars with 0/6/12/24/48 dB marks, blue up to -18 dB, amber to -6 dB, red above, and a falling
  peak-hold line that turns red after going over. Fixed the stray line they showed in silence and the
  black grid redrawn every frame. Inside the Amp popup they still sit either side of the card,
  vertically.
- **Header buttons:** LOAD / SAVE / TUNE are wide, short buttons with smaller text instead of
  cramped squares.
- **Version** moved to the bottom-right corner of the window.
- **Chain grid:** taller, pedal-shaped blocks, centred vertically above the meters. Two-word names
  (TUBE LAB, CAB LAB, LOOP OUT) go on two lines instead of shrinking; IN / OUT are larger.
- **Pedal list:** a packed 4-across grid of small pedals instead of full-width cards, so the whole
  list fits without gaps where a pedal is already in the chain.
- The Tube Lab popup now stays above the meter strip.

## 1.21.0 (2026-10-09)

- **Simpler look:** the big dim Griffin emblem behind everything is gone (plain dark background);
  the emblem is now a small logo (`Logo.png`) in the top-left of the title bar.
- Removed the "Click a block to adjust it..." hint line under the chain grid.

## 1.20.0 (2026-10-09)

- **Tube Lab tone stack** (`ToneStackCircuit.h`): the real passive Bass / Mid / Treble network of
  Fender and Marshall amps, between the preamp and the power amp as in the real amps, so the knobs
  change what drives the power tubes and interact the way they do on the amp. **Tone Stack**
  selector (Off / Fender / Marshall) and **Bass / Mid / Treble** in the Tube Lab's new right-hand
  column. Off by default: older presets sound as before.
  - Simulated from the parts (Yeh & Smith, DAFx 2006): Fender '59 Bassman (250 pF, 20 nF, 20 nF;
    250k audio treble, 1M bass, 25k mid, 56k slope) and Marshall JCM800/Plexi (470 pF, 22 nF, 22 nF;
    220k linear treble, 1M bass, 25k mid, 33k slope). Trapezoidal capacitors and a 5-node solve
    each sample; knobs glide like real pots (no clicks) and keep the caps' charge.
  - Measured: the per-sample simulation matches the circuit's exact frequency response within
    0.007 dB; inside the plugin within 0.07 dB. Level is made up so 1 kHz at noon is unchanged.
- **Tube Lab power amp: a real phase inverter and real grid circuits.**
  - The phase inverter is now a long-tail pair of 12AX7s (82k / 100k plate loads, 470 ohm bias,
    10k tail), solved exactly from the same triode model as the preamp and tabulated (built once at
    startup, in under 0.3 s). Its two outputs differ a little, as on the real amp (gains 32.9 / 35.4).
  - Each power tube's grid has a coupling cap (22 nF with EL34s, 100 nF with 6L6s), a 220k grid
    leak and real grid current, all solved in the per-sample Newton step together with the
    difference current and the negative feedback. Blocking (the choke after a hard hit) now comes
    from the circuit: measured, a soft note right after a loud chord dips 8-10 dB and recovers
    within ~100 ms. Before, it was an envelope-follower approximation.
  - Measured: small-signal level within 1 dB of unity at the reference Master (any feedback);
    stable at every extreme tested; harmonics behave like push-pull (2nd stays near -35 to -39 dB
    while the 3rd grows with Master). CPU on the current rig: 56% -> 60% of one core.
- Tube Lab presets: the whole-amp presets now use their real tone stack (Fender: Blackface Clean,
  Tweed Breakup; Marshall: Plexi Crunch, 800 Lead, Modern High Gain, Singing Lead), and every
  preset was re-measured for the new power amp (level-matched within 0.05 dB). Tweed Breakup's
  Master went from 8 to 6 to stay in range.
- Sound change: Tube Lab settings with the Power Amp on sound different from 1.19 (that's the new
  phase inverter and grids). Pick the preset again to get its new level.

## 1.19.2 (2026-10-09)

- **Clip warning:** the label under the OUT meter turns red and reads **CLIP** for 1.5 s whenever
  the output goes past full scale (both the app and the plug-in).
- **Safety soft clip (standalone app):** the output used to be hard-clipped at full scale, which is
  what made overloads sound harsh and splatty. Now anything up to -0.9 dBFS is untouched, and above
  that it bends smoothly toward full scale (same slope at the knee, no added latency).
- **Master In / Master Out:** the amp popup's Input and Output knobs are the whole app's levels
  (they apply even with the AMP off), so they're labeled that way now, with tooltips.
- Found while diagnosing "the Tube Lab sounds awful": the saved setup had Output at +29.5 dB
  (peaks of +5.9 dBFS, ~2% of samples hard-clipped) and two cabs in series (CAB LAB into CAB,
  17-28 dB of extra speaker/mic filtering). Fixed in that setup: Master In/Out to 0 dB, CAB LAB off
  (peak now -6 dBFS, no clipping).
- Tube Lab math check, through the plugin, against textbook values: unbypassed cathode -8.0 dB
  (theory 7.7 dB); 0.68 uF cathode shelf -8 dB in the bass rising through ~100-250 Hz; one stage
  is second-harmonic dominant (H2 -31 dB, H3 -53 dB at 12 dB drive); power amp into the speaker
  load +15.5 dB at the 95 Hz resonance with no feedback. Aliasing at very high gain: below -60 dB
  for notes in the guitar's range (1.2 kHz and lower), about -42 dB for a 2.9 kHz tone.

## 1.19.1 (2026-10-09)

- **AMP on/off.** The AMP block couldn't be switched off. Its popup now has an **On** switch
  (under Input). Off passes the signal straight through the block: the model, the gate's gain and
  the tone stack are all skipped. Measured: off is identical to a plain wire. The AMP chip dims
  when it's off, like a bypassed pedal. MIDI/automation-mappable (`AmpEngaged`, saved by name; older
  presets load with the amp on). Switching it changes the latency reported to a DAW by the amp
  model's own latency (0 for a model at the session's sample rate).
- The CAB chip now dims when its IR switch is off, so both fixed blocks show their state.

## 1.19.0 (2026-10-09)

- **Tube Lab presets** (PRESETS button in the Tube Lab, next to the switches). Each sets every
  Tube Lab knob and switch except On. Every preset's Output was measured through the plugin and set
  so its level on a plucked-guitar test signal matches the dry signal (within 0.04 dB), so
  switching presets doesn't jump in volume:
  - Preamp only (good as a pedal in front of an amp capture): **Stock Stage**, **Warm Clean
    Preamp**, **Tight Boost** (0.68 uF cathode cap cuts the lows: a clean boost for high gain).
  - Whole amps (power amp and speaker load on): **Blackface Clean** (6L6, stiff supply, feedback),
    **Tweed Breakup** (saggy, no feedback), **Top Boost Chime** (two stages, EL34, no feedback),
    **Plexi Crunch** (small bright cathode caps, EL34 cranked), **800 Lead** (cold unbypassed
    second stage, 470 pF), **Modern High Gain** (4.7 nF coupling cap for a tight low end, 6L6,
    saggy), **Singing Lead** (fat bypassed stages, big coupling cap, bloom).
  - The table is in `TubePresets.h`.
- **The AMP NEST pedal list refills.** Comp, OD, Chorus, Delay, Reverb, EQ and Tremolo now come
  in three copies each. Drag Reverb into the chain and the list offers **REVERB 2**, then
  **REVERB 3**. Each copy has its own knobs, on/off and popup. Tube Lab, CAB LAB and the capture
  pedals are still one each. Measured: REVERB 2 with the same knobs as REVERB gives identical
  output; two reverbs in series give a much longer tail (8x the RMS level of one, 0.5-1 s after an impulse).
  - Saved state: the copies' params are by name (`Reverb2Active`, `Reverb2 Size`, ...); their grid
    ids are 16-29 (`GridCell16` ...). Cable masks grew to 32 bits. Old presets and 1.17/1.18 grids
    load unchanged; the 1.16 autosave still loads into the same chain.
- **Fix: Tube Lab thump when a capacitor value changes.** Changing the Cap / Cap 2 / Plate Cap
  selectors or turning Bloom (which sets the input cap) rescaled the capacitor's stored voltage by
  the old/new value, a thump that could reach 4x full scale (25 uF to 0.68 uF on the cathode). The
  same happened on load whenever the saved caps differed from the defaults. Capacitors now keep
  their voltage and current across a value change. Measured on the 1.16 autosave: the first 0.2 s
  after loading lose the thump; from 0.5 s on the output matches 1.16 to within 5e-5.

## 1.18.0 (2026-10-09)

- **AMP NEST pedal list: a scrollbar down its right side**, replacing the ^ / v buttons. Drag the
  thumb, click the track to jump, or use the mouse wheel anywhere over the list (including over a
  pedal). The list scrolls live; it no longer rebuilds the whole window on every step.
- Pedals in the list are clipped to it: rows scrolled out of view no longer draw over the buttons
  above the list or past the bottom of the panel, and can't be grabbed there.
- Fix: the amp model's and IR's clear buttons (x) and the Slim icon no longer float over the
  AMP NEST panel while the amp popup is closed. They appeared whenever a model or IR loaded,
  including at startup, and sat on top of the pedal list.

## 1.17.0 (2026-10-09)

- **Signal-chain grid, Fractal style.** The 2-row slot strip is now a 4-row x 14-column grid.
  Blocks go in any cell and are wired with cables that turn at right angles (rounded corners)
  through the gaps between cells. No more diagonal line from the end of the top row back to the
  start of the next.
  - **Column = order.** Signal flows left to right. Dropping a block into an empty column puts it
    in series, spliced into the cable that crosses that column. Moving it up or down in its column
    keeps it in series.
  - **Same column = parallel.** Dropping a block into a column that already has blocks puts it in
    parallel with the nearest one (same input, same output). Where cables merge, the paths are
    averaged, so two identical paths come out at the same level as one.
  - **Cables by hand:** drag from a block's port (the dot on either side) to another block, or to
    IN / OUT, to add a cable (or remove one that's already there). Click a cable to remove it.
    Cables carrying signal from IN to OUT are bright; dead ends are dim. A dragged block shows
    where it will land.
  - Taking a block out of the chain closes the gap behind it, unless it was one of several parallel
    paths (then the others carry on).
  - **Latency-aligned merges:** where paths with different latency meet (e.g. Tube Lab, 15
    samples, beside a dry path), the faster path is delayed to match. Measured: the merged output
    equals the average of the two paths to 8.5e-16; without this it would comb-filter (-19 dB
    notch at 1.6 kHz). The host is told the longest path's latency, not the sum.
  - Mono/stereo as before: the amp model and capture pedals are mono; blocks after the AMP (or after a
    stereo block) run in stereo, blocks before it run in mono.
- **Older presets and the autosave load into the grid automatically**, in their old order on the
  top row; the FX Loop Send (a tap) sits just below. Measured on the current autosave (Tube Lab,
  Comp, OD, CAB, AMP, CAB LAB, Reverb, loop): output bit-identical to 1.16.0, same 15-sample
  latency, same CPU (within 1%). Note: 1.16 and older don't know about the grid, so a preset saved
  in 1.17 opens in them with whatever order its old Slot values had.
- **FX loop fix (DAW only):** the aux bus no longer leaks into the chain. Before, Loop Return's
  aux input was mixed into the main input, the aux outputs got the main output instead of the
  Loop Send, and connecting the aux bus made the main input 6 dB quieter.
- Saved state: new params `ChainGridV1`, `GridCell0-14`, `GridIn0-14`, `GridOut` (by name, after
  the positional block). The old Slot params are kept but no longer used for ordering.

## 1.16.0 (2026-10-07)

- **Sample rates up to 192 kHz.** Every part checked at 44.1, 48, 88.2, 96, 176.4 and 192 kHz:
  - **Tube Lab oversampling now follows the host rate** so the circuit always runs at ~176-192 kHz:
    4x at 44.1/48, 2x at 88.2/96, none at 176.4/192. Before, it was always 4x, so 192 kHz meant
    768 kHz internally and about 4x the CPU. Measured: identical gain, harmonics and aliasing at
    every rate; full-rig CPU 26-30% of one core at every rate. Latency: 15 samples at 44.1-96 kHz,
    0 at 176.4/192 kHz.
  - **Tuner**: its pitch search cost grows with the square of the rate (about a full core in bursts
    at 192 kHz). It now runs on a copy decimated to ~22-24 kHz -- about 1% CPU at every rate -- and
    no longer allocates memory on the audio thread. Picks the first strong autocorrelation peak
    (McLeod pitch method), so it doesn't jump an octave. Measured within 0.04 cents at every rate.
  - Speaker load, CAB LAB, pedals: already rate-independent (measured the same at every rate).
- Notes for high rates: the standard cab IR loader keeps up to 8192 samples of an IR (170 ms at
  48 kHz but 43 ms at 192 kHz) and its CPU grows with the rate; amp captures resample internally.
  At 192 kHz, use a larger audio buffer (e.g. 512-1024) than at 48 kHz.

## 1.15.0 (2026-10-07)

- **Tube Lab: speaker load** (**Spkr Load** switch, needs Power Amp on, off by default). The power
  tubes drive a real 8-ohm guitar speaker's electrical impedance through the output transformer
  (voice coil resistance and inductance with eddy losses, cone resonance at 95 Hz) instead of a
  resistor. Measured with no feedback: +14 dB at the 95 Hz resonance and +7.6 dB by 6 kHz -- the
  low-feedback "thump and bite". Feedback flattens it like on a real amp (+4.5/+3 dB at 5).
- **New pedal: CAB LAB** -- speaker cab, two close mics and a room, stereo out. Drag it in like
  any pedal (best after the amp, in the loop, with the CAB block switched off).
  - **Speaker**: 6 modeled speaker/cab types shaped after published response charts (4x12 V30,
    4x12 Greenback, 4x12 G12H-30, 2x12 Alnico Blue, 1x12 Jensen P12, 1x12 EVM12L), or flip **IR**
    and load your own cab IR with **CAB LAB IR** (saved with the session).
  - **Mic A / Mic B**: SM57, SM7B, Beta 57A, R-121 ribbon, MD421, U87 condenser (published
    responses approximated with filters, each with its polar pattern's proximity effect).
    **Position** (bright dust cap to dark cone edge), **Distance** (0.5-100 cm: real propagation
    delay, level fall-off, proximity bass), **Level**, **Pan**; Mic B can be switched off and
    polarity-flipped (**Flip B**). Two mics at different distances comb-filter like real ones
    (measured: SM57s at 2.5 and 30 cm notch near 660 Hz; flipping one turns it into a +7 dB peak).
  - **Room**: stereo room mics -- **Room** level, **Size** (dead booth to live room, ~0.15-0.6 s),
    **Width**; plus **Output**.
  - The speakers and mic curves are approximations of spec charts, not measurements; the
    placement physics (delays, proximity, fall-off, phase) is real.
  - CPU: about 2% of one core (plus the IR when used).

## 1.14.0 (2026-10-07)

- **4x oversampling** (was 2x) for the whole Tube Lab, same 15-sample latency. Cascaded saturating
  stages make harmonics far past 2x's limit, which folded back down as audible aliasing (ringing
  that doesn't follow the notes, a metallic/digital sheen).
- **Plate caps**: caps across the plate resistors, as real high-gain preamps use to tame fizz.
  **Plate Cap** (stage 1, default None) and **Plate Cap 2** (default 470 pF, the classic value).
- **Capacitors are stepped selectors** with 6 stock values, like swapping parts, instead of
  continuous pots: **Cap / Cap 2** (cathode bypass): None, 0.68, 1, 2.2, 10, 25 uF -- "None"
  replaces the old Cathode Cap on/off switches; **Plate Cap / Plate Cap 2**: None, 47, 100, 220,
  470 pF, 1 nF; **Couple**: 2.2, 4.7, 10, 22, 47, 100 nF. (Saved under new names; earlier cap
  settings reset to the defaults once.) Bloom stays continuous (the Classic engine uses it too).
- Sound notes: **Mix below 10** blends dry signal into a stage whose low end the circuit has
  phase-shifted (coupling and cathode caps), so the two partly cancel -- hollow/thin; real amps
  have no dry blend. **Drive** is real-world gain staging: 0 dB is a guitar's own level into the
  tube, so +25 dB with Stage 2 is a big boost into a two-stage high-gain preamp (squashed).

## 1.13.0 (2026-10-07)

- **Tube Lab: power amp** (Circuit engine), its own row in the window, off by default
  (`NeuralAmpModeler/PowerAmp.h`). After the preamp stage(s):
  - **Phase inverter** (long-tail pair) splitting into two antiphase drives, soft-clipping when
    pushed, into a **push-pull pair of EL34s, or 6L6s** with the **6L6** switch (Koren's pentode
    model with screen voltage and plate knee; lookup tables for the expensive math). Class AB with
    crossover; a realistic 4% mismatch between the pair, so a little even-harmonic warmth survives.
    Measured: EL34 grittier (3rd harmonic -21 dB at moderate drive), 6L6 cleaner with more
    headroom (-35 dB), as their reputations go.
  - **Real sag**: class AB tubes draw more current the harder you play, so B+ (through the
    rectifier/supply resistance) and the screens (through their resistor) droop. **Sag** goes from
    stiff (solid-state rectifier) to saggy (tube rectifier): up to about 3 dB of compression on big
    chords, B+ 450 -> 390 V, screens 432 -> 350 V.
  - **Power-tube blocking**: overdriven grids charge their coupling caps, so after loud playing a
    soft note dips about 4 dB and blooms back over ~200 ms.
  - **Feedback** (negative feedback, solved implicitly so it can't go unstable) and **Presence**
    (less feedback up top = more bite). Feedback's gain loss is made up, so it changes the feel
    and tone, not the volume.
  - **Master**: how hard the phase inverter drives the power tubes, like a real master volume.
  - Output transformer bass and treble roll-off.
- Where to use it: the power amp is most real when the Tube Lab *is* the amp, i.e. no amp model
  loaded (or a capture of a preamp only), followed by a cab IR. In front of a full-amp
  capture it adds a second power amp.
- CPU: about 16% of one core per channel with two preamp stages and the power amp all on.

## 1.12.0 (2026-10-07)

- **Tube Lab: a second, cascaded stage** (Circuit engine), with its own row in the window, off by
  default:
  - **Stage 2** switch: stage 1's plate swing goes through the **Gain 2** pot (the volume control
    between stages, audio taper) and the interstage coupling cap into a second 12AX7 stage.
  - Stage 2 sees stage 1's real output resistance (Ra || rp, about 38k), so when its grid conducts
    it charges the interstage cap at the right rate: **interstage blocking**, where real preamp
    choke and "bloom" come from. Measured after hard picking, a soft note ducks about 10 dB for
    20 ms at the default **Couple** (22 nF), and chokes about 30 dB and recovers over about 0.5 s
    at max (220 nF). At min (2.2 nF) there's no choke.
  - **Bias 2** (default -2.0 V, a slightly colder second stage as in many high-gain designs),
    **Cathode Cap 2** switch and **Cap 2** (default 0.68 uF, the classic tight second stage).
  - Both stages share the plate load (Plate) and the B+ supply, so with Sag on, stage 2's current
    draw sags stage 1's rail too.
  - Two stages add real cascade gain: about +21 dB at small signals with Gain 2 at 5, so clean
    playing gets much louder and dirtier; turn Gain 2 down or use Output to tame it. Saturated
    levels stay in the same range as one stage.
  - Switching Stage 2 or Cathode Cap 2 crossfades (no clicks).
  - CPU: about 11.5% of one core per channel with both stages (6.3% with one).

## 1.11.0 (2026-10-07)

- **Tube Lab: new Circuit engine.** The stage is now simulated as an actual 12AX7 circuit, every
  sample, instead of a tube curve with dynamics added on top (`NeuralAmpModeler/TubeCircuit.h`):
  - The parts: input coupling cap, grid leak and 68k grid stopper; the tube's grid-cathode and
    grid-plate capacitances (Miller effect); cathode resistor with bypass cap; plate load from a B+
    node with supply resistance and reservoir cap; output coupling cap into the next stage's 1M.
  - The tube: the Dempwolf-Zoelzer 12AX7 model (DAFx 2011), fitted to measured tubes, *with grid
    current*. Its two current functions are 1-D, so they're precomputed into lookup tables; the
    circuit is solved with Newton's method on the node voltages.
  - Grid clamping, blocking after hard hits, bias shift, bass tightening with a small cathode cap,
    and treble softening as gain rises all come out of the circuit, not separate effects.
  - Checked against textbook values: at Bias -1.5 V it rests at Rk 2.3k, cathode 1.45 V,
    plate 187 V, 0.65 mA, gain 60.
- **Knobs are now real parts** (Circuit engine): **Bias** picks the cathode resistor; **Bloom** is
  the input coupling cap (2.2-220 nF: how long the stage chokes after a hard hit); the new circuit
  row has **Cathode Cap** (switch: no cap = local feedback, about 8 dB less gain, tighter) with
  **Cap** (0.47-47 uF: small = bright and tight, big = full), **Plate** (47-220k plate load), and
  **Sag** (switch) with **Supply** (4.7-470k) and **Recovery** (0.05-2 s).
- Levels follow the parts like a real amp: colder bias, a smaller plate resistor or no cathode cap
  is quieter. Small signals at default settings match the Classic engine's level.
- **Circuit switch** (next to On) picks the engine; off = **Classic**, the 1.8 lookup-table
  engine, for A/B. Switch flips crossfade over 10 ms (no clicks).
- **Removed** the 1.9-1.10 Cathode/Sag "Depth" exaggerations. They worked like a compressor riding
  the volume, which is why it didn't feel real. Note: sag in a single preamp stage is physically
  tiny (driven hard, a preamp tube draws *less* current); real amp sag comes from the power section.
- CPU: about 7% of one core per channel with the Circuit engine (0.9% Classic).

## 1.10.0 (2026-10-07)

- **Fix: pre-amp pedals did nothing when no amp model was loaded.** The no-amp fallback copied the
  raw input to the output, throwing away the Tube Lab, OD, Comp and capture pedals placed before the
  amp. It now passes the pre-amp chain's output through. (This bug predates the Tube Lab.)
- **Fix: library folders.** AMP LIBRARY, CAB IRs and PEDAL 1/2 pointed at `Desktop\Neural Amp\Sims and
  IRs` and `Desktop\tone`, which were moved into `Desktop\Griffin Amplification`. They now use
  `Griffin Amplification\IRs and Amp Profiles (copied)` and `Griffin Amplification\tone` (plus
  `...\Griffin tone folder captures` for IRs). Presets saved with the old paths need the amp/IR
  reloaded once from the libraries.
- **Tube Lab: Cathode and Sag are now clearly audible.** The physically accurate versions were
  about 1 dB in a single preamp stage. The **Rk** and **Supply R** knobs are replaced by **Depth**
  (0-10, default 3), with Rk (1.5 kOhm) and Supply R (47 kOhm) fixed at realistic values:
  - Cathode Depth adds a bias shift that follows how hard you hit (the grid swing), with the
    Recovery time: up to about 5 dB of compression and a big change in harmonic character
    (at Drive 24, Depth 3 roughly triples the 2nd harmonic). 0 = the physical circuit only.
  - Sag Depth scales the B+ droop and, beyond the physical amount, lets the output level follow
    B+ like power-section sag: about 10 dB of "breathing" at Depth 10 on hard playing, recovering
    when you play softer.
  - Both recover fully after loud passages at every setting (an earlier version could latch
    into silence at Depth 10; caught in testing).

## 1.9.0 (2026-10-07)

- **Tube Lab circuit dynamics.** A new "Circuit" row in the Tube Lab window, with two sections that
  each have their own on/off switch. Both are off by default, so existing sounds don't change.
  - **Cathode**: the cathode bypass cap (V_k) charges toward plate current x Rk, so sustained hard
    playing drifts the bias colder and then recovers. Knobs: **Rk** (0.47-4.7 kOhm, default 1.5:
    how far the bias moves) and **Recovery** (5-100 ms, default 33: how fast).
  - **Sag**: B+ at the plate (V_rail) droops as plate current is drawn through the supply
    resistance into the reservoir cap, then recovers. Knobs: **Supply R** (0-150 kOhm, default 47:
    how far it droops) and **Recovery** (0.1-2 s, default 0.5). In a single preamp stage this is
    subtle (about 0.7 dB of compression over ~2 s at 100 kOhm), as it is in real amps, where sag
    mostly comes from the power section.
  - With either section on, the stage switches from the lookup table to a per-sample circuit solve:
    grid voltage = input x drive + bias; Newton iterations on a 3/2 power-law plate current (cut
    off at zero); then V_k and V_rail updates; output = plate swing, AC-coupled. The power law is
    fitted to the 12AX7 lookup table at the default bias in both current and gain, so switching a
    section on doesn't jump the level. The two engines differ mainly at hard clipping (the power
    law's knee is about 10% harder).
  - The Bias knob still sets where the tube sits at rest; the cathode only moves it while playing.
  - CPU: about 1.5% of one core per channel with either section on (0.9% with both off).

## 1.8.1 (2026-10-07)

- Tube Lab window: now fits between the IN and OUT meters, so the OUT meter no longer covers the
  Output knob on wide windows.

## 1.8.0 (2026-10-07)

- **Tube Lab pedal.** A 12AX7 tube preamp stage, added as a built-in pedal. Drag it from the AMP
  NEST pedal list into any slot, before the amp or in the effects loop. Click its chip to open its
  window: an on/off switch, a live transfer-curve plot, and Drive, Bias, Bloom, Bright, Mix and
  Output knobs.
  - The Koren 12AX7 equations are solved once at startup into a 4096-point lookup table (100k plate
    load, 250 V B+, grid-current limiting), so nothing is solved per sample.
  - Runs at 2x oversampling to keep aliasing down. This adds 15 samples (0.3 ms) of latency while
    the pedal is in the rack. The on/off switch fades to the equally delayed dry signal instead of
    changing the latency, so footswitching it doesn't make the DAW re-sync.
  - Bloom models the bias shift when you dig in (grid conduction charging the coupling cap).
  - Half of the Drive gain is compensated, so more Drive also pushes the amp after it harder.
  - Works in mono before the amp and in stereo in the loop (one stage per channel).
- **Saved-state format.** Parameters added from now on are saved by name in a section at the end
  (`###ExtraParamsV1###`). The original 75 parameters are still saved in the same fixed order as
  1.7.0. 1.7.0 presets, sessions and autosaves load unchanged (Tube Lab off), and 1.7.0 can still
  open 1.8.0 saves (it ignores the Tube Lab settings).
- The version number now shows in the main window.

## 1.7.0

The stable build before the Tube Lab, as launched by the "Griffin Amplification" desktop shortcut.
A copy of its executable, VST3 and source is in
`NeuralAmpModeler/backup-2026-10-07-before-tube-lab/`.
