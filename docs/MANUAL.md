# Griffin Amplification: the full manual

Everything about how each part works, from the signal chain to the Tube Lab's circuit math. For a quick
tour with pictures, see the [README](../README.md).

A guitar amp app and VST3 plug-in built with [iPlug2](https://iplug2.github.io). Alongside neural amp
captures (`.nam` files), it has a pedal rack, A/B amp
slots, a tuner, an FX loop, a cab IR loader, and two physics-based blocks: the **Tube Lab**, a
simulated tube preamp and power amp, and **CAB LAB**, a speaker cab with mics and a room. The Tube
Lab's options go from the parts of real amps (cathode follower, cathode bias, feedback from the
speaker, Resonance) to exaggerated "Real Feel" effects, and it uses almost no CPU while it's switched
off. It has a debug page with an oscilloscope on its circuit, a live schematic, a plain-English readout
of what each setting does, and a CSV log.

For dynamics there's a **GATE** that stays solid after a high-gain amp (it can listen to your clean
guitar wherever it sits) and a **PRO COMP** studio compressor (Clean, Opto or FET).

Footswitches and pedals on a MIDI controller can drive any knob or switch: click **MIDI** in the
top bar and map them Ableton-style (see [MIDI control](#midi-control)).

The version number appears in the main window's bottom-right corner, and on Settings > About. [CHANGELOG.md](../CHANGELOG.md)
describes every version, with measurements.

- [Signal chain](#signal-chain)
- [The Tube Lab: how the math works](#the-tube-lab-how-the-math-works)
  - [Why simulate the circuit](#why-simulate-the-circuit)
  - [Stage 1: one 12AX7 gain stage](#stage-1-one-12ax7-gain-stage)
  - [Stage 2: the cascaded stage](#stage-2-the-cascaded-stage)
  - [The power amp](#the-power-amp)
  - [Speaker load](#speaker-load)
  - [Real Feel switches](#real-feel-switches)
  - [60 Hz: supply ripple, grind and hum](#60-hz-supply-ripple-grind-and-hum-1390-grind-1400)
  - [Circuit+ (1.45.0)](#circuit-1450): cathode follower, cathode bias, speaker feedback, Resonance, 8x
  - [Oversampling](#oversampling) (and the solver's cost, sleep when off)
  - [Levels](#levels)
  - [The Classic engine](#the-classic-engine)
  - [Debug page: seeing what the circuit does](#debug-page-seeing-what-the-circuit-does)
- [EQ and live spectrum](#eq-and-live-spectrum-1460)
- [Themes](#themes-1480)
- [Audio buffer and your interface](#audio-buffer-and-your-interface-1510)
- [CAB LAB](#cab-lab)
- [GATE and PRO COMP](#gate-and-pro-comp-1520)
- [Other blocks](#other-blocks)
- [Building](#building)
- [A/B slots and the cab IR](#ab-slots-and-the-cab-ir)
- [MIDI control](#midi-control)
- [Saved-state format](#saved-state-format)
- [Credits](#credits)

## Signal chain

The chain is a Fractal-style grid ([`ChainGrid.h`](../NeuralAmpModeler/ChainGrid.h)), 4 rows by 14
columns, between IN and OUT. AMP (an amp capture with A/B slots, then the noise gate and tone stack), CAB
(the IR loader), the FX loop's Send and Return, and the pedals are all blocks on it.

```
 IN --> COMP --> OD --> AMP --+--> CAB -------+--> REVERB --> OUT
                              |               |
                              +--> CAB LAB ---+        (same column = parallel)
```

- **Column = order.** Signal flows left to right, and cables only run from one column to a later
  one, so the columns are the processing order. Dropping a block into an empty column splices it in
  series into the cable that crosses that column.
- **Same column = parallel.** Dropping a block into a column that already has blocks puts it in
  parallel with the nearest one: it gets the same input and feeds the same blocks. A block (or OUT)
  with several cables coming in gets their **average**, so two identical paths come out at the
  level of one.
- **Cables:** drag from a block's port (the dot on either side) to another block, or to IN / OUT, to
  add a cable or remove one that's already there. Click a cable to remove it. Cables are routed at
  right angles through the gaps between cells. Bright cables carry signal from IN to OUT; dim ones
  are dead ends, such as the FX Loop Send, which is a tap.
- **Latency:** each path's latency is tracked, and where paths merge the faster ones are delayed
  (from a short history buffer per block) to line up with the slowest, so parallel paths never
  comb-filter. The DAW is told the latency of the longest path into OUT.
- **Mono and stereo:** the amp model and capture pedals are mono, so they get a mono mix. Other
  blocks run in stereo once AMP or a stereo block is upstream of them, and in mono before that.
- The tuner always reads the raw input, and the noise gate's detector reads IN.

Presets from before 1.17 are laid out automatically on load: their old Slot order goes on the top
row, with the FX Loop Send just below it as a tap. They sound identical.

## The Tube Lab: how the math works

Source files: [`TubeStage.h`](../NeuralAmpModeler/TubeStage.h) (the block: oversampling, knobs, both
engines), [`TubeCircuit.h`](../NeuralAmpModeler/TubeCircuit.h) (the 12AX7 stage), and
[`PowerAmp.h`](../NeuralAmpModeler/PowerAmp.h) (the push-pull power amp and speaker load).

### Why simulate the circuit

The simple way to model a tube is a static curve, output = f(input), with effects such as "sag" or
"bloom" added on top as envelope followers. That approach produces the tube's distortion, but the
added effects behave like a compressor. In a real amp the feel comes from the parts interacting:
- a coupling cap charges when the grid conducts,
- the cathode cap sets which frequencies get full gain,
- the supply droops when the tubes draw more current.

The default **Circuit** engine therefore does not script any of these behaviors. It describes the
actual schematic and solves it for the node voltages at every sample, and the behaviors come out of
that solution. The knobs set component values (resistors and caps).

### Stage 1: one 12AX7 gain stage

```
 Vin --Cin--+--Rstop(68k)--G            B+ --Ra-- P --Cc(22n)--+-- out
            |              | \                    |            |
          Rgl(1M)        Cgk  Cgp                 |          Rl(1M)  (next stage's grid leak)
            |              |    \                 |            |
           gnd             K-----+--- 12AX7 ------+           gnd
                           |
                        Rk || Ck   (cathode bias + bypass cap)
                           |
                          gnd
```

**The tube model.** The stage uses the Dempwolf & Zoelzer 12AX7 model (*"A Physically-motivated
Triode Model for Circuit Simulations"*, DAFx 2011). It was fitted to measured tubes, and it includes
**grid current**:

```math
I_k = G\left(\frac{\ln\left(1 + e^{C\,(V_{pk}/\mu + V_{gk})}\right)}{C}\right)^{\gamma}
\qquad
I_g = G_g\left(\frac{\ln\left(1 + e^{C_g V_{gk}}\right)}{C_g}\right)^{\xi} + I_{g0}
\qquad
I_p = I_k - I_g
```

with G = 2.242e-3, C = 3.40, μ = 103.2, γ = 1.26, G<sub>g</sub> = 6.177e-4, C<sub>g</sub> = 9.901,
ξ = 1.314, I<sub>g0</sub> = 8.025e-8.

The `ln(1 + e^x)` term (softplus) is a smooth version of `max(0, x)`. Where it is positive, the
current follows a 3/2-type power law. Where it is negative, the tube cuts off smoothly with no hard
corner, which matters because Newton's method needs smooth derivatives. Grid current is near zero
while the grid is negative. Once the grid goes above the cathode it rises steeply, so the grid acts
like a diode. That diode clamps the input signal and charges the input cap.

**Lookup tables.** The cathode current depends only on one combined voltage,
`u = Vpk/μ + Vgk`. The grid current depends only on `Vgk`. Both are therefore functions of a single
variable, and each is tabulated once at startup (16,384 points over u in [-8, 8] V and V<sub>gk</sub>
in [-3, 6] V). Each table stores the value and its derivative, and lookups interpolate linearly. Per
sample, no `exp`/`log`/`pow` call is made. Values outside the tables use the exact formula.

**Capacitors: trapezoidal companion models.** For one sample step T, each capacitor C is replaced
by a resistor in parallel with a current source:

```math
i_n = g\,v_n - i_{eq},\qquad g = \frac{2C}{T},\qquad i_{eq} \leftarrow g\,v_n + i_n
```

This is the trapezoidal rule (the same as the bilinear transform). It is stable at any step size, so
it doesn't blow up on a stiff circuit. After this replacement, each sample is a purely
**resistive** circuit with known current sources, plus the tube.

**Modified nodal analysis + Newton-Raphson.** The circuit has five unknown node voltages: A (after
Cin), G (grid), K (cathode), P (plate) and O (output). Kirchhoff's current law says the currents
leaving each node add up to zero:

```math
F(\mathbf{v}) = G_{lin}\,\mathbf{v} - \mathbf{s} + \mathbf{i}_{tube}(\mathbf{v}) = 0
```

- G<sub>lin</sub> is the conductance matrix of the resistors and cap companions. It is built once,
  when a knob changes.
- **s** holds the sources: the input voltage, the cap histories, and B+ through R<sub>a</sub>.
- **i**<sub>tube</sub> is the triode: I<sub>g</sub> leaves G, I<sub>p</sub> leaves P, and I<sub>k</sub>
  arrives at K.

Each sample, Newton's method solves `J Δv = -F` with a 5x5 Gaussian elimination. The Jacobian J is
G<sub>lin</sub> plus the tube's partial derivatives (∂I<sub>k</sub>/∂V<sub>gk</sub>,
∂I<sub>k</sub>/∂V<sub>pk</sub>, ∂I<sub>g</sub>/∂V<sub>gk</sub>), which come straight from the
tables. The solver details:
- It starts from the previous sample's voltages. At 176-192 kHz consecutive samples are close, so it
  needs only a few iterations, with a maximum of 8.
- Each node's step is limited to 100 V per iteration, only as a sanity limit. (Until 1.32.3 the
  input, grid and cathode were held to 2 V per iteration. A cascaded stage 2's input follows stage
  1's plate, which can move tens of volts in one sample, and 8 iterations of 2 V couldn't catch up.
  The solve stopped short, by up to -20 dB re the peak on Modern High Gain. The Dempwolf model grows
  only polynomially past its knees, so plain Newton steps are safe there.)
- It stops when no node moves by more than 10 µV.

**Bias.** The **Bias** knob sets the resting grid-to-cathode voltage. The code then finds the
cathode resistor that produces it, as an amp designer would. It bisects on the plate current until
the load line `Vp = B+ - Ra·Ip` and the tube agree at that V<sub>gk</sub>, then sets
`Rk = -Vbias / Ik`. At the default -1.5 V with a 100k plate, this gives the textbook operating point:
R<sub>k</sub> ≈ 2.3k, plate ≈ 187 V, 0.65 mA, and a gain of about 60.

On reset, the DC operating point is solved with the caps open (40 Newton iterations), and each cap
is pre-charged to its resting voltage. The stage therefore starts silent, with no turn-on thump.

**What each part does, and which knob sets it:**

| Part | Knob | What it does |
|---|---|---|
| Input coupling cap C<sub>in</sub> | **Bloom** (2.2-220 nF, log) | When the grid conducts, I<sub>g</sub> charges C<sub>in</sub>, which pushes the grid colder. The cap then bleeds back through the 1M grid leak. This is **blocking**: the choke after a hard pick and the "bloom" as the note recovers. A bigger cap blocks for longer. |
| Grid stopper 68k + C<sub>gk</sub> 1.6 pF, C<sub>gp</sub> 1.7 pF | (fixed) | C<sub>gp</sub> is multiplied by the stage gain (**Miller effect**), which creates a treble roll-off. The stopper limits grid current, which softens the clamp. |
| Cathode R<sub>k</sub> | **Bias** | The operating point, and therefore headroom and how symmetrically the stage clips. |
| Cathode bypass C<sub>k</sub> | **Cap** (None, 0.68-25 µF) | Above the corner frequency `1/(2π Rk Ck)`, the cathode is grounded for AC and the stage has full gain. Below it, R<sub>k</sub> applies local negative feedback, so the bass gets less gain. A small cap gives a tight, bright stage. With no cap the stage is about 8 dB quieter and cleaner. |
| Plate load R<sub>a</sub> | **Plate** (47-220k) | Gain and headroom (the slope of the load line). |
| Plate cap (across R<sub>a</sub>) | **Plate Cap** (None, 47 pF-1 nF) | Rolls off fizz. With R<sub>a</sub> ‖ r<sub>p</sub> ‖ load ≈ 38k, 470 pF gives a corner near 9 kHz. |
| Supply resistance + reservoir | **Sag**, **Supply**, **Recovery** | B+ is a node fed through R<sub>s</sub>. Its time constant is tenths of a second, about 10,000 samples, so it is updated explicitly after each solve: `V_rail += k·(V_supply - Rs·I - V_rail)`. A single preamp stage barely sags, as in a real amp. Real sag comes from the power amp. |
| Output coupling cap 22 nF into 1M | (fixed) | Blocks the DC from the plate. Asymmetric clipping shifts the average voltage, and this cap is what lets that shift move the next stage's bias. |

**Grid current** is the most important part of the model. Most tube models leave it out, and
without it there is no clamping, no blocking, and no dynamic bias shift.

### Stage 2: the cascaded stage

With **Stage 2** on, stage 1's plate swing goes through the **Gain 2** pot into a second, identical
circuit. The pot uses an audio taper, gain = position², which approximates a log pot. The two stages
interact in two ways:

- **Interstage blocking.** Stage 2's input cap (**Couple**) is driven from stage 1's real output
  resistance, R<sub>a</sub> ‖ r<sub>p</sub> ≈ 38k. In the code, C<sub>in</sub> becomes one conductance
  in series with that source resistance: `g = 1 / (Rsource + 1/g_Cin)`. When stage 2's grid conducts,
  the cap therefore charges at the real circuit's rate. This is where high-gain preamp choke comes
  from. Measured at the default 22 nF: a soft note after a hard hit dips about 10 dB for 20 ms. At
  220 nF it chokes by about 30 dB and recovers over about 0.5 s.
- **Shared supply.** Both stages draw from the same B+ node. Stage 2 reads stage 1's rail voltage
  each sample and reports its own current draw back, so with Sag on, playing harder through stage 2
  also sags stage 1.

Stage 2 defaults to a slightly colder bias (-2.0 V), a small 0.68 µF cathode cap and a 470 pF plate
cap. These are the classic tight, fizz-tamed second stage of high-gain designs.

### The power amp

```
 in --Master--> phase inverter (long-tail pair) --+--> grid 1 (+) --> pentode 1 --+
                     ^                            +--> grid 2 (-) --> pentode 2 --+--> output transformer --> out
                     |                                                            |
                     +------ negative feedback (Feedback; Presence = less of it up top) ---+
 B+  <-- rectifier + supply R + 40 uF reservoir (Sag)    -- drawn on by both plates
 Vg2 <-- screen resistor + 40 uF                         -- drawn on by both screens
```

**Pentode model (Koren).**

```math
I_p = \frac{2}{K_{g1}}\,E_1^{E_x}\,\arctan\!\left(\frac{V_p}{K_{vb}}\right),
\qquad
E_1 = \frac{V_{g2}}{K_p}\,\ln\!\left(1 + e^{K_p\left(\frac{1}{\mu} + \frac{V_{g1}}{V_{g2}}\right)}\right)
```

This factors into three parts: `(V_g2/K_p)^Ex`, a function of the single variable
`x = 1/μ + V_g1/V_g2`, and `arctan(V_p/K_vb)`, the plate "knee" below which current collapses. The
two 1-D functions are tabulated with their slopes, and the V<sub>g2</sub> factor is computed once
per sample. The EL34 and 6L6 use Koren's published constants. The bias is found by bisection so that
each tube idles at 35 mA (EL34) or 40 mA (6L6). This is a fixed, cold-ish class AB bias, so on big
swings one tube cuts off: crossover.

**Push-pull through the transformer.** Each plate sees a quarter of the 3.4k plate-to-plate load
(R<sub>L</sub> = 850 Ω), with opposite signs:

```math
V_{p1} = B^+ - R_L D,\qquad V_{p2} = B^+ + R_L D,\qquad D = I_{p1} - I_{p2}
```

The difference current D is what drives the speaker. The only unknown per sample is D, so Newton
solves the scalar equation `f(D) = D - (Ip1(D) - Ip2(D)) = 0`, warm-started. Since 1.33.0 it's a
safeguarded Newton: each grid's equation is solved exactly for the current D (it falls monotonically
in the grid voltage and is concave, so Newton converges from anywhere, and below -2 V it's linear and
solved directly). That leaves f rising monotonically in D, so once f has been seen on both sides of
zero, the root is bracketed and any step that reaches the bracket's far end bisects it instead. It
takes up to 16 iterations; most samples need 2-4. The old solver, a plain Newton with a fixed ±50 mA
step limit, could bounce between two points forever when one tube was cut off and the other's grid
was conducting. Plexi Crunch's output was off by up to -17 dB re its peak.

The second tube is scaled by 0.96. Real pairs are never perfectly matched, and a perfect pair cancels
the even harmonics completely (measured at -134 dB). The 4% mismatch keeps a little even-harmonic
warmth.

**Phase inverter.** The long-tail pair is modeled as `PI = 70 V · tanh(u / 70 V)`. It feeds +PI to
one grid and -PI to the other, and soft-clips when pushed. **Master** sets
`u = (2 + 78·master²) · (input - feedback)`, a quadratic taper like a real pot.

**Negative feedback is implicit.** Feedback is part of the Newton equation for *this* sample:
`u = drive · (in - β·y(D))`, where the output y depends on the D being solved for. Its derivative
enters the Jacobian (`dU/dD`). A one-sample-delayed feedback loop would ring or go unstable at high
feedback. The implicit version can't. **Feedback** sets β from 0 to 2 (a loop gain of up to about
10 dB). The output is multiplied by `(1 + β)` to make up the closed-loop gain loss, so Feedback
changes feel and tone rather than volume. **Presence** subtracts a 1.5 kHz high-pass of the output
from the fed-back signal, so there is less feedback, and more gain and bite, in the treble.

**Sag.** Class AB tubes draw more average current the harder they're driven, so the supplies are
modeled as RC nodes updated each sample:

```math
V_B \mathrel{+}= k_s\,(V_{supply} - R_s\,(I_{plates} + I_{screens}) - V_B),
\qquad
V_{g2} \mathrel{+}= k_{g2}\,(V_B - R_{g2}\,I_{screens} - V_{g2})
```

**Sag** sets R<sub>s</sub> from 20 Ω (solid-state rectifier) to 600 Ω (tube rectifier and soft
transformer), and the screen resistor from 500 Ω to 2.2k. The screen current is taken as 12% of the
plate current. V<sub>g2</sub> feeds back into the pentode equation, so a sagging screen lowers the
gain. That is the compression and bloom on big chords. Measured: B+ falls from 450 to 390 V and the
screens from 432 to 350 V, about 3 dB of compression.

**Power-tube blocking.** A grid driven above 0 V conducts. Grid current is modeled as a tanh soft
limit near +4 V. The conducting grid charges its coupling cap in about 1 ms, which shifts that tube's
bias colder, and the charge bleeds off over about 20 ms. After loud playing, a soft note dips about
4 dB and blooms back.

**Output transformer.** A 40 Hz high-pass (primary inductance) and a 12 kHz low-pass (leakage
inductance).

### Speaker load

With **Spkr Load** on, the plates drive an 8 Ω 12" speaker's electrical equivalent circuit, scaled
through the transformer (×850/8), instead of a resistor:

```
 Re (6.4 Ω) -- [ Le 0.6 mH || R2 25 Ω ] -- [ Lm || Cm || Rm 39 Ω ]
  voice coil    coil inductance + eddy loss   cone resonance, fs = 95 Hz, Qms = 3.5
```

The parallel RLC follows from `ω0 = 1/√(LC)` and `Q = R·√(C/L)`. The impedance peaks at about 45 Ω
at 95 Hz and rises at the top end. Pentodes have a high output impedance, so they act like current
sources, and the voltage across the speaker follows its impedance curve. Measured with no feedback:
+14 dB at 95 Hz and +7.6 dB by 6 kHz. This is the low-feedback "thump and bite".

The network is linear in D. Every L and C is replaced by a trapezoidal companion, so the plate swing
becomes `u = z_eq·D + u0`, which is the same form as the resistor case (`u = R_L·D`). It folds into
the same scalar Newton solve at no extra cost. The feedback is taken from that voltage, so turning
Feedback up flattens the bump, as it does on a real amp.

### Real Feel switches

Four switches and a Depth knob along the bottom of the Tube Lab (1.33.0, Depth 1.33.1). They're off by default, and every preset
turns them off (the presets were voiced without them). With all four off, the output is exactly what
it was without them. All need **Power Amp** on.

- **Squish (screen squish).** A pentode's cathode current is split between plate and screen. It's set
  by the grid and screen, and is about `a·F(x)·π/2` whatever the plate voltage is. The plate collects
  the `atan(Vp/Kvb)` part of it (Koren's knee). When the plate swings down near the screen voltage,
  the rest lands on the screen: `Ig2 = 0.08·Ip + a·F(x)·(π/2 - atan(Vp/Kvb))`. At idle that's about
  the fixed 12% used without the switch. On big swings the screen current jumps, the screen supply
  sags, and the tubes' gain drops with it. On Plexi Crunch with Master at 10, the screens sag to
  390 V instead of 422 V. With a sustained loud 82 Hz note they reach 289 V.
- **Shared B+ (shared supply).** In a real amp, the preamp's B+ is the far end of the power supply:
  B+, then the screens, then the phase inverter and preamp nodes, each through a decoupling RC. The
  preamp's rail follows the power amp's screen node (relative to rest) through a 0.1 s decoupling
  filter. A hard power-amp hit then sags the preamp too. Cranked Plexi's preamp rail dips to 242 V
  (249 V without it).
- **OT Iron (transformer saturation).** The 40 Hz high-pass of the transformer's primary inductance
  becomes a saturating inductor. Its current is `h(λ) = λ(1 + (λ/λs)⁴)`, where λ is the flux (the
  integral of the voltage across it). λs = 0.6 V·s of plate swing is where the inductance has halved,
  which is a full 400 V swing at about 106 Hz: a modestly sized guitar-amp transformer. It's solved
  per sample, by Newton on one monotonic equation. Only sustained, loud low notes reach it. With
  the presets' bass cuts, played material peaks at about 17% of λs. A held 82 Hz note at your level
  reaches it.
- **Coil Heat (voice coil heating).** Needs **Spkr Load** too. The coil warms with the power in its
  resistance, `ΔT' = (6 K/W · D²·Re - ΔT) / 3 s`, and `Re = Re₀(1 + 0.0039·ΔT)`. The tubes and the
  feedback see the hotter, higher resistance. The output takes the extra voltage drop back out,
  because what you hear follows the coil's current (the cone's force), not the voltage across it.
  With feedback holding the voltage, the coil's current drops (compression). Without feedback, the
  bass resonance loosens. Cranked Plexi heats the coil by 20 K over a few seconds. After the switch
  is turned off, the coil keeps cooling, so the load doesn't jump back.

**Depth** (1.33.1, a knob at the end of the row, 1x-10x, default 4x) exaggerates all four together.
At k×, the Squish screen supply is k times softer (4.7k·k through 22 µF/k, so it recovers just as
fast). The transformer saturates at λs/k. The coil heats k times faster per watt. The preamp's and
phase inverter's supplies move k times the screens' relative sag (never below 25%). 1x is the real
parts. Squish's supply feed is set so the screens idle where they do without it (only the squish
moves them). Shared B+ also scales the phase inverter's swing with its supply: `S(u)` becomes
`k·S(u/k)`, so it clips earlier at the same small-signal gain.

Since 1.33.2 Depth's strength is Depth² (1x = the real parts, 4x = 16, 10x = 100). Squish's knee
(Kvb·√strength) and screen pull (×√strength) grow with it. OT Iron saturates at λs/(2·strength - 1).

**Feedback-proof (1.33.2).** Negative feedback restores most of the gain a sagging supply takes, which
kept Squish and Shared B+ near-silent on high-Feedback settings. With either on, the power amp's
output is multiplied by the open-loop gain change, raised to `beta/(1+beta)`, the share the feedback
cancelled: `(Vg2/rest)^Ex` for Squish, the phase inverter's supply scale for Shared B+. With no
feedback it's exactly 1. It glides through two ~2.5 ms one-poles. This isn't how a real amp behaves;
it's there so the switches can be heard at any Feedback. On Feedback 8.2 settings, all four on, the
attack of a hard chord drops 0.2 dB at 1x, 3.8 dB at 4x and 7.9 dB at 7-10x.

**Squish emphasis (1.33.3).** Above 1x, Squish's gain pull is `(Vg2/rest)^Ex` raised to
`beta/(1+beta) + 2·(Depth - 1)`, soft-capped at 7 dB (`0.8·tanh(x/0.8)` nepers), with or without
feedback. At moderate levels the screens only sag ~10%, so without this the pick's squash was under
1 dB. Squish's Depth-dependent parts glide (two 30 ms one-poles) so turning Depth doesn't click.

Flipping any of them while playing doesn't click: measured on a clean 82 Hz sine and on cranked
Plexi, the high-frequency step at the flip is no bigger than the signal's own.

**The REAL FEEL pedal (1.34.0).** Inside the Tube Lab, the switches shape the Tube Lab's own power
amp. When an AMP block (an amp capture) comes after the Tube Lab, a high-gain capture saturates its
input and flattens what they did: measured on a real rig (Tube Lab into a 5150 capture), Squish at
4x moved the final output's pick level by +0.3 dB. The REAL FEEL pedal puts the same four switches
after the AMP block, where a real amp's power section is. It's a tandem pedal: its On switch is the
Tube Lab's, and its controls are the Tube Lab's Real Feel switches and Depth (either popup sets
both). While it's on the grid, the switches act in the pedal and not in the Tube Lab (they'd
otherwise act twice).

It's a behavioural model (`RealFeel.h`), driven by the finished amp signal rather than a circuit:

- **Full power** is the loudest you've played lately: a peak follower that jumps to every new peak
  and falls 6 dB a minute (never below -40 dBFS). The draw is the signal relative to it, so it works
  at any AMP output level, and harder picks sag more than soft ones.
- **Squish:** the screen supply sags by `0.12·d·(0.3 + 0.7·d²)` at full power (d = draw), steeper
  near full power, as a pentode's screen takes the current its plate can't. A hard pick drains it in
  about 20 ms; it recovers over about 150 ms. The gain follows `(1 - sag)^(1.35·Depth²)`. Above 1x,
  the steady part of the sag (a 1.5 s average) is taken back out, and what's left pulls up to 6
  times harder by 4x. That leaves the pick's squash and bloom rather than a quieter amp. Soft-capped
  at 7 dB.
- **Shared B+:** the preamp's supply follows the sag through a 50 ms decoupling filter. It turns down
  the signal into the AMP block, `1 - 1.2·Depth²·sag`, floored at -6 dB and ramped across each
  block. It reads the pedal's sag from the previous block (~5 ms), far quicker than the sag moves.
- **OT Iron:** the saturating part of the transformer primary's magnetizing current,
  `40 Hz·2π·λ·(λ/λs)^4`, shunts the signal, with the flux λ the (5 Hz-leaky) integral of the output.
  It's solved implicitly per sample. At 1x, full power saturates it only below 30 Hz; the frequency
  scales with `2·Depth² - 1`. The AMP block's capture already has the linear transformer, so only
  the saturation is added.
- **Coil Heat:** the coil's temperature rises with the mean-square power, `40·Depth²` K at sustained
  full power (3 s time constant, 250 K ceiling). The resistance rises 0.39%/K, and the output drops
  by the same ratio except in a band-pass around the cone's 100 Hz resonance, which keeps its level.
- Every switch crossfades over 20 ms (smootherstep) and Depth glides (two 30 ms one-poles). The
  Tube Lab's On switch fades the pedal rather than bypassing it.
- **Mix (1.39.0)** is the pedal's dry/wet: `out = in + mix·(pedal − in)`, gliding over 30 ms. The
  pedal adds no delay, so the two line up. Shared B+'s turn-down of the AMP block's input is scaled
  the same way. At 100% the output is exactly the pedal's.

On the user's chain (Tube Lab > AMP (5150 capture) > REAL FEEL > CAB > reverbs), at their playing
level, a hard chord's pick drops by:

| Depth | Squish | Coil Heat | All four | OT Iron (low end, held chord) |
|---|---|---|---|---|
| 1x | 0.4 dB | 0.1 dB | 0.4 dB | 0.0 dB |
| 4x | 2.3 dB (2.9 dB at 0.3-1 s) | 1.0 dB | 4.0 dB | -1.4 dB |
| 7x | 2.7 dB (5.3 dB) | 1.8 dB | 5.7 dB | -7.5 dB |
| 10x | 2.8 dB (6.2 dB) | 2.2 dB | 6.4 dB | -12.8 dB |

Shared B+ at 4x changes the sound by -14 dB (level-matched) through the capture. With the pedal
placed and every switch off, the output is bit-identical to not having it.

**Touch (1.38.0)**, the knob between Coil Heat and Depth, makes softer picks clean up. It isn't part
of a real amp. The circuit goes from clean to fully dirty over only about 6-12 dB of input. With
Drive set high, every pick you play lands past that point and sounds the same. Touch tracks your
picking (`TubeStage::_TouchGain`):

- An envelope follows the input. It has an instant attack, a 15 ms hold (longer than a low E's
  period) and a 150 ms release.
- A reference follows your hardest recent picks: instantly up, down 0.3 dB/s, never below -40 dBFS.
- The drive into the first tube is scaled by (envelope / reference)^(0.3 x Touch). At Touch 3, each
  dB softer than your hardest pick takes about 0.9 dB more off the drive.
- The same gain is undone after the tubes, delayed to line up with the oversampler. Where the
  circuit is clean, loudness and sustain don't change. Touch only changes how dirty a note is.
  Undoing just half of it made fading notes decay about 30% faster in dB.

It works in both engines, with or without the power amp and the REAL FEEL pedal. Presets and
pre-1.38 states load with Touch at 0, which is bit-identical to 1.37. Lab harness, held note at
-19 dBFS peaks for a hard pick (THD by pick level re hard):

| | -24 dB | -18 | -12 | -6 | 0 |
|---|---|---|---|---|---|
| Drive 24, Touch 0 | -36 | -20 | -16 | -14 | -15 |
| Drive 12, Touch 0 | -53 | -46 | -36 | -20 | -16 |
| Drive 12, Touch 3 | -73 | -62 | -50 | -31 | -16 |

Touch moves the clean-to-dirty change into your playing range but makes it a little steeper. A
version that compressed the drive instead (wider change) was tried. Soft notes never fully cleaned
up and the clean/dirty contrast shrank, so it wasn't kept.

### 60 Hz: supply ripple, grind and hum (1.39.0, grind 1.40.0)

The **60 Hz** switch and its **Amount** and **Mix** knobs, at the end of the REAL FEEL row, add
what a real amp's power supply puts into the sound (`mains::Ripple` in `MainsHum.h`). The B+ is
60 Hz mains through a full-wave rectifier into a reservoir cap, so it carries a 120 Hz ripple. The
ripple is modelled as `r = 2·(|sin(2π·60t)| − 2/π)` (zero mean, -1.27 .. +0.73) and applied to the
Tube Lab's wet signal (A = Amount/10):

- **Grind (1.40.0):** a supply-starved clipping stage. Its headroom is
  `h0 = 4·ref·10^(−(18·A + 12·A⁴)/20)`, where ref is your recent loudest (a peak follower, as REAL
  FEEL uses for full power). That's 4x over your loudest at Amount 0, 30 dB lower at 10; the A⁴ term
  (1.41.0) leaves Amount up to about 6 as it was and makes the top much heavier. The ripple moves it:
  `h = h0·max(0.1, 1 + (0.45·A + 0.4·A⁴)·(0.3 + 0.7·load)·r)`, where load is the envelope against ref,
  because a cap discharges further between peaks the more current is drawn. The stage is
  `h·(tanh(x/h + 0.4) − tanh(0.4))`, which is lopsided and still 0 at 0. A make-up gain brings a
  peak at ref back to ref (set by the side that clips later), computed at the wobble's highest
  headroom (full load, ripple peak) so no peak comes out hotter.
- **Hum:** `0.0024·A·(0.5·r + 0.15·sin(2π·60t))` is added. It peaks about -56 dBFS at Amount 10,
  at the Tube Lab's output. The 60 Hz part stands in for heater and ground hum.
- **Mix:** `out = x + w·(grind − x + hum)`, `w = Mix·min(1, 10·A)`. It's a parallel blend with no
  delay between the two. The `10·A` term fades the stage out as Amount glides to 0 when switched off,
  so the switch lands on the untouched signal without a step.

1.39.0's version only multiplied the gain by `1 + 0.16·A·(0.25 + 0.75·load)·r` (±8% at most),
which changed the user's chain by -33 dB at Amount 5. The grind changes it by -19 dB (-11 at 10),
and loudness stays within 1 dB.

This is a behavioural model, not a supply simulation. The ripple acts after the tubes, not inside
the circuit. It works in both engines, with or without the power amp. It stays in the Tube Lab even
with the REAL FEEL pedal placed. On/off and Amount glide over 20 ms; off is bit-identical.
Presets turn it off. Everything after the Tube Lab amplifies the hum too: in the user's chain (Twin
capture, Master Out) it's about 14 dB louder at the output. The AMP's gate shuts it off between notes.

The **Hum Filter** switch (AMP popup, under Bass) does the opposite, for real hum on the guitar
signal (`mains::HumFilter`). It applies eight RBJ notches at 60, 120 ... 480 Hz, each 2.5 Hz wide
at -3 dB, to the mono input after Master In and before the gate and the chain. Measured: the
harmonics drop by over 150 dB. Notes near them barely move: low E -0.02 dB, A2 -0.07 dB,
B2 (123.5 Hz, the closest) -0.55 dB, B3 (246.9 Hz) -0.15 dB. Injected hum at the output went from
-43 to -87 dB rms. It crossfades on and off over 20 ms. The tuner still reads the raw input.

### Circuit+ (1.45.0)

Five controls, all off by default. Since 1.46.0 they live on the Tube Lab's tabs: Follower on PREAMP, Cath Bias, Spkr FB and Resonance on POWER AMP, and 8x in the top row next to Economy. Off is bit-identical to the circuit
without them. Presets and older saved states turn the four sound controls off; 8x is left alone,
like Economy. Unlike Real Feel's Depth, nothing here is exaggerated: these are real parts
behaving as they do in real amps. Each was checked against a 200-iteration solve of the same
circuit (-120 dB or better re the signal) and for stability at extreme settings.

**Follower: the cathode follower** (`TubeCircuit.h`, `CathodeFollower`). This is the stage that
Marshalls and 5150s put between the last gain stage and the tone stack. It is a 12AX7 whose grid is
wired straight (DC) to the gain stage's plate through a 100k stopper, with its cathode driving the
stack.
- **Parts:** 350 V on its plate, a node above the gain stages' 250 V, as in those amps, and 220k
  from cathode to ground. With Marshall's 100k, a 12AX7 here would need its grid above its cathode
  just to carry the resting current, because our gain stages rest about 35 V higher than theirs.
  220k rests it 0.85 V under its grid at about 1 mA, as theirs do.
- **The stack's load:** taken as 100k for the signal, the stack's input impedance at guitar mids,
  from the cathode's resting voltage.
- **Solve:** it has no caps, so it is solved each sample: Newton on its grid and cathode voltages,
  warm-started. Its grid current is a load on the gain stage's plate node, with its slope from
  implicit differentiation, inside that stage's own Newton solve, so the coupling is implicit.
- **What it does:** small-signal gain 0.983. On a downward plate swing of more than about 67 V,
  the stack's load cuts it off and the bottom of the wave flattens. When the gain stage's plate
  nears its supply, the follower's grid conducts and rounds the top over.
- **Output:** the cathode's swing through the same ~7 Hz bilinear high-pass as the coupling cap it
  replaces. Without that high-pass, switching it on clicked at -37 dB.
- **Measured:** -0.9 dB at the user's settings and -4.3 dB at hot ones (it clips). The difference
  is -12.8 dB re the signal at the user's settings. It costs +3.5% CPU.

**Cath Bias: cathode-biased power amp** (`PowerAmp.h`).
- **Circuit:** the grid leaks return to 0 V, and the pair shares a cathode resistor with a 100 µF
  bypass cap. The resistor is chosen so the resting current is the same as with the fixed bias:
  the cathode rests at 41 V with the 6L6s, Rk ≈ 460 Ω, τ = 46 ms.
- **Solve:** the cap is slow, so the cathode node is updated explicitly after each sample's solve,
  like the supplies. Everything the tubes see (grid, plate and screen voltages, grid current) is
  taken relative to it.
- **What it does:** class AB rectifies, so dig in and the pair's average current rises, the cathode
  rises, and the tubes bias colder. On a hard burst at Master 8 the cathode went 41 → 47 V, and the
  amp came out of the burst ~1 dB quieter than with fixed bias, recovering over ~50 ms. At low Master
  (the user's 2) the tubes stay in class A and nothing moves (-55 dB difference), as on a real amp.

**Spkr FB: feedback from the speaker side** (`PowerAmp.h`). Real amps take the negative feedback
from the output transformer's secondary. Before this, the loop here ended at the plates, and the
transformer's 40 Hz high-pass and 12 kHz low-pass came after it.
- **Solve:** both filters are linear, so the fed-back signal is still a known linear function of
  this sample's output, `fbA·y + fbB`, and the solve stays implicit (no one-sample delay in the loop).
  The filters run on their own state, from the plate voltage the secondary actually carries, so Coil
  Heat's correction stays out of the loop. OT Iron's saturation stays outside the loop.
- **Measured:** +2 dB at 40-70 Hz at the user's settings, +0.4 dB at 8 kHz. The feedback no longer
  flattens what the transformer rolls off.

**Resonance** (`PowerAmp.h`). It takes a low-pass part (150 Hz) of the fed-back signal away, up to
75%, the way the Resonance/Depth control on a Mesa or 5150 does, so the low end and the speaker's
resonance come back. It acts on the signal only (the fed-back signal's swing from rest): taking
away part of the DC feedback moved the operating point, and a knob jump clicked at -20 dB. It
glides over ~10 ms.
- **Measured:** with Spkr FB, +3.5 dB at 70 Hz at 10; Spkr FB and Resonance 10 together give
  about +6 dB at 70 Hz.
- **Why the limits (150 Hz, 75%):** the first design was 100 Hz and 90%. With Spkr FB, three phase
  leads sat inside the loop: the transformer's 40 Hz high-pass, the power grids' coupling caps
  (33 Hz with the EL34s' 22 nF), and the subtraction's own lead below its corner. Together they
  passed 180° with loop gain to spare. At Feedback 10 / Master 10 with EL34s and speaker load it
  motorboated at 32 Hz and never stopped, as a real amp would. 150 Hz / 75% was stable in all 64
  extreme combinations tested (tube type, speaker load, Presence, Cath Bias, Follower, Master),
  at 44.1, 96 and 192 kHz.

**8x** (`TubeStage.h`). Twice the oversampling (8x at 44.1/48 kHz, ~384 kHz), with filters twice as
long, so the latency stays 15 samples.
- **Cost:** 8.6% → 16.6% of a core at the user's settings.
- **Measured:** in-band aliasing on a hard-clipping Stage 2 (Drive 30, Gain 2 at 9): -38 → -52 dB
  on a 2.5 kHz note. No audible change at moderate gain.
- Economy halves the factor and 8x doubles it, so both on cancel out.

**Switching them.**
- Follower, Cath Bias and Spkr FB crossfade over 10 ms, like the other switches. 8x restarts the
  Tube Lab like Economy.
- Measured on a held 110 Hz note (>2 kHz transient re the note's peak): -56 to -58 dB, the same as
  flipping 6L6/EL34.
- The same test found an old bug: **Spkr Load clicked at -13 dB**. It had no crossfade; it now has
  one (-58 dB).

### Oversampling

Each saturating stage creates harmonics well above the host's Nyquist frequency. If the stages ran
at 48 kHz, those harmonics would fold back as aliasing: inharmonic ringing and a metallic sheen. The
Tube Lab runs at about 176-192 kHz whatever the host rate:

| Host rate | Factor | FIR taps | Latency |
|---|---|---|---|
| 44.1 / 48 kHz | 4x | 61 | 15 samples |
| 88.2 / 96 kHz | 2x | 31 | 15 samples |
| 176.4 / 192 kHz | 1x | none | 0 |

How it works:
- Upsampling is zero-stuffing followed by a Kaiser-windowed (β = 7) sinc low-pass with gain × factor.
  Downsampling is the same filter, then decimation. The cutoff sits at 0.87 × the host Nyquist
  (1.45.0). Before that it was a Blackman window cut off exactly at Nyquist, which is only -6 dB at
  24 kHz and -21 dB at 28 kHz, so harmonics the tubes made at 24-32 kHz folded back into 16-24 kHz.
  The new filter has the same length, latency and cost, and rejects 71 dB from 28 kHz up (was
  21 dB), so nothing folds below 20 kHz at full level. Aliasing at 18-24 kHz dropped 13-15 dB on
  every test tone. The price is the very top of the band: flat to 12 kHz, -0.5 dB at 16 kHz and
  -2.7 dB at 18 kHz through both filters, where a guitar speaker is already 30 dB or more down.
- Aliasing inside the band (below 12 kHz) is made by the circuit itself at its ~192 kHz rate, mostly
  by a hard-clipping Stage 2. At hot settings (Drive 30, Gain 2 at 9) a 2.5 kHz tone aliases at
  -38 dB, and 8x oversampling would take that to -52 dB at twice the CPU. At the user's settings it
  is -54 to -84 dB.
- The tap count is `factor × 15 + 1`, so the total delay is always a whole number of host samples. The
  dry path is delayed by the same amount, so **Mix** and the on/off crossfade stay phase-aligned.
- The on/off switch fades to that delayed dry signal instead of removing the block. The latency
  reported to the DAW therefore stays the same when you stomp it.
- Switch flips (engine, Stage 2, caps, power amp, tube type) crossfade over 10 ms. The old circuit
  keeps running in its current state until the fade ends, so there are no clicks. Knob turns don't
  need a crossfade, because the circuit keeps its node voltages and cap charges and moves to the new
  values the way a real pot does.


**Economy (1.35.0)** halves the factor (2x at 44.1/48 kHz, 1x at 88.2/96 kHz) for about half the
Tube Lab's CPU. The FIRs are rebuilt for the new factor, so the latency stays 15 host samples
(none at 1x). Flipping it restarts the stage at the new rate. The output fades out over 15 ms (an
S-curve), stays silent 10 ms while the restarted circuits settle, then fades back in.

**Cost (1.35.0).** The preamp's Newton step reduces the five-node system to the tube's three nodes:
the input-cap node A and output node O are linear and each tied to one tube node, so they're folded
into a 3x3 matrix once per parameter change, solved by Cramer's rule, with A and O
back-substituted. The upsampler sums only the taps that meet non-zero (zero-stuffed) input, which
is bit-identical. Together these halved the Tube Lab's cost; the stock presets match the old solver
to -234 dB.

**Cost (1.45.0).** Another 36% off when it's on, and nearly all of it when it's off. Measured on the
user's settings (two stages, Marshall stack, 6L6 power amp, speaker load) and checked against a
200-iteration solve. The circuit used to match that solve to -155..-185 dB re the signal, far below
what 24-bit audio can hold. It now matches it to -157..-179 dB.
- **Newton's predictor.** Each solve starts from the last two samples' solutions extrapolated in a
  straight line instead of the last one. At the oversampled rate that lands within a fraction of a
  millivolt.
- **Tolerances matched to Newton's quadratic convergence.** The preamp stops once a step moves no node
  more than 0.1 mV (was 0.01 mV, which cost an extra confirming iteration). The power amp stops once
  its step in the difference current is under 1 µA. It then moves the last evaluation along its own
  slopes instead of solving the phase inverter, both grids and both tubes again at the final point
  (a fifth of the power amp's time).
- **Divisions off the serial chains.** Table lookups, the tubes' 1/µ and 1/Vg2, and the input cap's
  companion model now multiply by reciprocals computed once.
- **`pow()` and `exp()` out of the per-sample path.** The pentode's screen factor (Vg2/Kp)^Ex comes from
  a second-order expansion re-anchored when Vg2 drifts 1% (error < 1e-7). The tone stack no longer
  re-derives every pot's resistance, with two `exp()` calls, on every sample.
- **Sleep when off.** Switched off, the Tube Lab is its delayed dry signal (Mix glides to 0), but the
  whole oversampled circuit used to keep running unheard. Once Mix has glided all the way down, it
  stops. Switched back on, it restarts from rest with its input faded in over 20 ms, and runs unheard
  for 50 ms before Mix glides up. Without that pre-roll, a rested circuit hit mid-note made a click
  10-12 dB over the old switch-on while its coupling caps charged. With it, switch-on measures as
  clean as before or cleaner (a held note into the amp: -37 → -50 dB of >2 kHz transient). Switching
  on takes effect 50 ms later; switching off is instant, as before.

Tube Lab alone, user's settings, 48 kHz: 13.4% → 8.6% of a core when on, 8.6% → 0.07% when off. In the
whole plugin on the user's chain at a 128-sample buffer: 33.7% → 19.7% as saved (Tube Lab
footswitched off), and 34.1% → ~28.5% with it on.

A thing that measured worse and was not kept: cubic-Hermite tables (1024 points, interleaved value
and slope) instead of the 16384-point linear ones. The hypothesis was that the 512 KB of tables
missed the CPU's cache. They don't, because the voltages move only a little per sample, and the
extra arithmetic made it 6-11% slower.

### Levels

- **Drive** is real gain staging. 0 dB means a guitar's own level at the grid. Half of the Drive gain
  (in dB) is compensated afterward (`÷ √drive`), so turning Drive up also pushes the amp after it
  harder, as a boost should.
- The stage output is divided by one **fixed reference gain**: the small-signal gain of the default
  stage (-1.5 V bias, 100k plate, bypassed cathode), which is `gm · (rp ‖ Ra ‖ Rl)` ≈ 60. It is not
  re-normalized per setting. Colder bias, a smaller plate resistor or no cathode cap make the stage
  quieter, as on a real amp, and the output stays bounded at extreme settings.
- The power amp is normalized so that small signals come out at unity at Master 5 with no feedback.

### The Classic engine

Turning the **Circuit** switch off selects the 1.8 engine, which is useful for A/B comparisons. It
uses the Koren triode equation solved on a 100k / 250 V load line by bisection into a 4096-point
table, plus a grid-current limit through a 33k stopper. Bloom is a peak follower on the grid
conduction voltage that shifts the bias, with a 2 ms attack and a 150 ms release. It costs about 0.9%
CPU per channel, against about 7% for one circuit stage.

### Debug page: seeing what the circuit does

The **DEBUG** button (under PRESETS in the Tube Lab) opens a full-window page that measures the
circuit simulation while you play. It reads the simulation's real node voltages and currents; it
doesn't model anything separately. Close it with the X in the corner.

- **Circuit map** (top left): the Tube Lab drawn as a schematic, from the input cap through V1, Gain
  2 and V2, the tone stack, the phase inverter and both power tubes, to the output transformer and
  speaker. Each probe point shows its live value over the last 50 ms:
  - **Signal points** (IN, the tone stack's out, OUT) show their peak, for example `OUT ±0.38 FS`.
  - **Points that sit at a voltage and swing around it** (grids, plates, cathode, plate currents)
    show the voltage plus how far the signal swings it, for example `P 188 ±79 V` for V1's plate.
    The swing is left off when it's under 3% of the voltage.
  - **Supply rails, screens and blocking** show the voltage only, for example a power tube's screen
    at `G2 438 V`.

  Hover over a point for its average and its min-to-max range. A point turns **amber** when that
  part is clipping right now: its grid is drawing current, or its tube has cut off. Sections that
  are switched off are greyed out. **Click** a point to put it on scope channel A. **Right-click**
  puts it on channel B.
- **Readout** (top right): plain-English lines, updated 4 times a second. The first line is the
  overall feel, picked from how much of the time any stage is clipping:

  | Feel | Clipping |
  |---|---|
  | Clean | under 0.5% of the time |
  | Edge of breakup | 0.5-5% |
  | Crunch | 5-20% |
  | Overdrive | 20-45% |
  | Saturated | over 45% |

  After that, for each stage: where the bias sits and what that means for the sound, the cathode
  cap's bass corner, the plate cap's treble corner, how far the grid and plate swing, how much of the
  time it clips and on which side, and blocking. Blocking is how far the coupling cap pushes the bias
  colder after hard hits, and how long it takes to recover. Then preamp sag, the tone stack, the
  power amp (bias, grid current, crossover, B+ and screen sag, feedback), the output level, and CPU
  use. Between notes, it keeps showing the last thing you played for 10 seconds. Scroll it with the
  mouse wheel.
- **Oscilloscope** (bottom): two channels, A and B, each on any of 20 probe points. Pick them from
  the menus or from the map. Each channel scales itself and shows its range on its own side.
  - **0.5 to 20 ms/div** is triggered on channel A, so a sustained note stands still.
  - **50 to 200 ms/div** scrolls (roll mode), for slow changes like sag and blocking recovery.
  - **FREEZE** holds the picture.
- **CSV LOG** (bottom left): writes one row every 0.25 s to
  `%LOCALAPPDATA%\NeuralAmpModeler\debug-logs\griffin-debug-<date>-<time>.csv`. **OPEN LOG FOLDER**
  opens that folder. Each row has:
  - every Tube Lab setting, plus a `changes` column, for example `Drive 12.0 -> 18.0`
  - the measurements from the readout, the feel, and CPU
  - sample rate, buffer, latency and the signal chain

  While you aren't playing, it writes a row only when a setting changes, or every 5 s. Logging keeps
  going with the page closed.

How it measures:
- **Grid conducting** means the grid is more than -0.2 V from the cathode, where a 12AX7 starts
  drawing about 1 µA of grid current.
- **Cut off** means the tube's own plate current is under 5% of its idle current.
- **Blocking** is the coupling cap's charge low-passed at 10 Hz, since the cap also carries some of
  the signal.
- It only measures channel 0, and only while the page is open or the log is on. Measuring costs about
  6% of the Tube Lab's CPU and changes nothing in the sound: the output is bit-for-bit identical with
  it on or off.

## EQ and live spectrum (1.46.0)

Two EQs share one engine ([`ProEQ.h`](../NeuralAmpModeler/ProEQ.h)), one analyzer
([`Spectrum.h`](../NeuralAmpModeler/Spectrum.h)) and one graph control
([`EQControls.h`](../NeuralAmpModeler/EQControls.h)):

- **PRO EQ:** a block you place anywhere on the grid. Its popup shows the spectrum going into it (IN)
  and coming out (OUT).
- **Output EQ:** the last thing before Master Out and the limiter. The strip above the meters shows
  the final output's spectrum with the Output EQ's curve; click it for the full page.

**Where to watch it (1.48.0-1.50.0).**
- **The strip** above the meters. Its left column has three buttons:
  - **X** hides it; SPECTRUM brings it back.
  - **Arrow:** about three times taller, or back.
  - **Pop out:** its own window.
- **Popups leave room for it:**
  - Pedal popups sit above the strip, and it stays live while you turn their knobs.
  - The Amp, Tube Lab, CAB LAB and PRO EQ cards end above the strip. An expanded strip drops to its
    normal size while one of those is open.
- **The spectrum window:**
  - Drag the title bar to move it, and the bottom-right corner to resize it.
  - It sits above every popup (the Tube Lab and the Amp included) and above the meters, at 60 fps.
  - DOCK returns it to the strip; X hides it.
  - Its spot and size are remembered.
- **Settings:** SPECTRUM SHOW / HIDE, next to the colour themes.

**EQ.**
- **Bands:** 8, fixed in type: low cut (12 dB/oct, Q sets the resonance), low shelf, four bells, high
  shelf, high cut. Each has On, Freq (20 Hz-20 kHz), Gain (±18 dB) and Q (0.1-18).
- **Filters:** each band is an RBJ biquad (bilinear, prewarped), in transposed direct form II.
- **Gliding:** knob moves glide (a one-pole on log frequency, gain and log Q, ~20 ms). The
  coefficients are recomputed every 16 samples, so dragging a node doesn't zipper. A band switched
  off fades to flat (cuts slide out of the range) before it's bypassed.
- **Flat means bypassed:** with every band off, the EQ is out of the path entirely.
- **Display = audio:** the graph draws the response from the same coefficients the audio runs.
  Processed and drawn agree to 0.00 dB at every test frequency. Through the whole plugin, a +9 dB
  1 kHz bell measured +9.00 dB, and +0.7 dB two octaves away.

**Spectrum.**
- **Audio side:** a lock-free ring of the newest 16384 samples per tap (the channels averaged). The
  audio thread only writes samples and publishes the position: no locks, no allocation, 0.06% of a
  core for two taps.
- **Screen side:** 4-term Blackman-Harris windows and four FFT sizes (since 1.50.0): 8192 points
  (5.9 Hz bins at 48 kHz) below 120 Hz, then 4096 to 400 Hz, 2048 to 1.4 kHz, and 1024 above. Each
  size reads its newest N samples, and neighbouring sizes cross-fade over half an octave. So the highs
  react fast (a 3 kHz note shows within 10 ms; it was 73 ms with 8192 alone), and the lows keep their
  detail (80 Hz: 87 ms). Both taps go through one complex FFT per size, separated by symmetry. It is read on a 1/48-octave log axis: the
  strongest bin in each step, interpolated at the low end where bins are sparser than steps.
- **Accuracy:** a full-scale sine reads 0 dBFS, give or take the window's scalloping (≤0.8 dB
  between bins).
- **Ballistics:** instant rise, 30 dB/s fall, and a peak-hold line that falls 6 dB/s after 1 s.
- **Cost:** 0.4 ms per frame at ~30 fps, only while a view is showing.

**1.47.0 additions.**
- **Band types:** any band can be any type: Bell, Low shelf, High shelf, Low cut, High cut, Notch.
  These are the per-band Type and Slope parameters. Older states give each band its old type by
  position.
- **Cut slopes:** 12-48 dB/oct, as 1-4 cascaded Butterworth sections with
  Q_k = 1 / (2 cos((2k-1)pi / 4n)). The sharpest section carries the resonance (its Q scaled by
  Q / 0.707). Measured: 48.2 dB per octave.
- **Notch:** a 40 dB-deep peaking cut.
- **Type or slope changes:** the band glides to neutral in its old shape (no gain, or a cut slid out
  of range), swaps, clears its state, and glides in. On a held note the largest sample-to-sample
  second difference during a Bell-to-High-shelf swap was 1.27x the steady tone's: no click.
- **Trim:** an input gain before the bands, gliding like Output.
- **Band panel** (drawn by the graph control itself, so it follows whichever band is selected): band
  buttons, Type, Slope, ON, FLIP, FREQ / BOOST-CUT / Q dials, and -1 / +1 dB.
- **ANALYZER** turns a view's spectrum off. **FREEZE** holds it.
- **Taps keep-alive:** a view that shows a live spectrum calls `Tap::KeepAlive()` every frame, and the
  audio thread writes a tap only for half a second after the last call. A hidden, frozen or switched-off
  view therefore costs nothing on either side.
- **Colours:** the output spectrum is drawn as thin slices, each a vertical two-stop gradient in a
  colour taken from a log-frequency palette. NanoVG gradients have only two stops, hence the slices.

**Editing.**
- Drag a node: frequency and gain (cuts: frequency). Shift drags finely.
- Mouse wheel: Q.
- Double-click a node: on/off. Double-click empty space: the nearest band (in octaves) jumps there,
  switched on.
- Alt-click: reset. In iPlug2 a right-click on a parameter control opens the host's menu.
- Band panel (1.47.0): pick a band (1-8 or a node), then Type / Slope / ON / FLIP, the dials, and -1 / +1 dB.
- The IN / OUT (BEFORE EQ / OUTPUT) chips show or hide each spectrum.
- Every edit is a parameter: saved, automatable and MIDI-learnable. Older states load with both EQs
  flat.

## Themes (1.48.0)

Settings has a **THEME** row: six palette swatches, **DARK / LIGHT**, and **SPECTRUM: SHOW / HIDE**
(the strip above the meters; the X on the strip's left hides it too).

- The theme colours live in `Colors.h` as run-time values (`NAM_THEMECOLOR` accent, `NAM_THEMEFONTCOLOR`
  text, `NAM_ONACCENT`, `NAM_BG`, `NAM_CARD`, `NAM_PANEL`, `NAM_SURFACE`, `NAM_INK`), set by
  `ApplyTheme(palette, light)`.
- Controls copy colours when they're created, so a change rebuilds the whole UI on the next idle tick
  (the same deferred relayout the AMP NEST toggle uses), with the IV styles rebuilt first
  (`_RebuildStyles`). Settings reopens after the rebuild so you can keep choosing.
- Light mode turns off iPlug2's label drop shadows, leaves out the dark line art, and puts a dark
  backing behind the white icons and the logo. The EQ graphs and meters stay dark.
- The strip's arrow (1.49.0) toggles a taller size (`spectrum_big=`). The strip is attached after the
  popup chrome, next to the meters, so it draws above a pedal's card. Pedal cards are lifted to end
  above it (`_LiftAboveStrip`), and the big cards hide it (`mSpecStripControls`).
- The spectrum window (1.50.0, `NAMSpectrumWindowControl` in EQControls.h) is a container holding a
  compact `NAMEqGraphControl` at 60 fps. It's attached after the meters, and its rect is saved as
  `float_x/y/w/h`. With it floating, the strip row is gone. Otherwise every popup card ends above the
  strip: pedals via `_LiftAboveStrip`, and the big cards above the strip's small size, which the strip
  drops to while they're open (`mSpecStripCtls` keeps both rects).
- The analyzer (1.50.0) is multi-resolution: FFT sizes 8192 / 4096 / 2048 / 1024, split at 120, 400 and
  1400 Hz with half-octave cross-fades, each reading its newest N samples.
- Stored in `%LOCALAPPDATA%\NeuralAmpModeler\ui.ini` (`theme=`, `light=`, `spectrum=`), shared by the
  app and the plug-in and kept out of presets.

## Audio buffer and your interface (1.51.0)

In the standalone app, Settings has an **AUDIO BUFFER** row:

- **Sizes:** 32, 48, 64, 96, 128, 192, 256, 384, 512, 1024 and 2048 samples. Click one and the audio
  restarts with it. Smaller feels tighter, and bigger is safer from dropouts. With the Tube Lab on, 64
  and below can drop out on this PC, so 128 is the usual choice.
- **DRIVER PANEL** opens your audio interface's own settings window. For the Scarlett 2i2 that's
  "Scarlett 2i2 (3rd Gen) Device Settings": Sample Rate, Buffer Size, Clock Source and Clock Status.
  With a non-ASIO driver it opens the Windows sound settings instead.
- **DEVICE...** opens the full audio Preferences: the interface, sample rate, input and output
  channels, and MIDI input. It's the same dialog as File > Preferences (Ctrl+,).
- The note under the row shows the size and how many milliseconds it adds each way. If your interface
  runs a different size from the one you picked (some only run the size set in their own panel), the
  note says so. The latency readout at the bottom of the main window always uses the real size.

**The app and the interface's panel stay in sync.** Change Buffer Size in the Focusrite panel and the
app restarts its audio at that size within about a second, and the row follows it. Pick a size in the
app and the Focusrite panel shows it too. Either way, the size is saved to `settings.ini`.

In a DAW the row only shows the DAW's buffer size: the DAW owns the audio device, so change it in the
DAW's audio settings.

How it works:

- `NAMBufferSizeControl` (NeuralAmpModelerControls.h) draws the row. A click only sets a pending flag
  (`mPendingBufferSize`, `mPendingDriverPanel`, `mPendingDevicePrefs`). `OnIdle` acts on it, outside
  the click handler, because restarting audio or opening a modal dialog from inside a control's
  callback isn't safe.
- The audio side is a local edit of iPlug2's standalone host (`IPlugAPP_host.h`,
  `IPlugAPP_dialog.cpp`): `SetBufferSize`, `GetStreamBufferSize` (the size the open stream actually
  got), `OpenDriverControlPanel` (`ASIOControlPanel()`), `OpenPreferencesDialog` (posts
  `ID_PREFERENCES` to the main window), and `RestartAudioIfDriverAsked`.
- Keeping the panel in sync: RtAudio ignored the ASIO driver's reset request. It now remembers it
  (`RtApiAsioConsumeResetRequest` in RtAudio.cpp). Once a second, `RestartAudioIfDriverAsked` also
  reads the driver's preferred size (`ASIOGetBufferSize`). After a reset request or a change in that
  size, it reopens the stream asking for size 0, which RtAudio takes as the driver's preferred size,
  and saves the result to the INI. Asking for the old size again would just reopen at the old size,
  since the Focusrite driver accepts any size the app asks for.
- The first `ASIOControlPanel()` call to Focusrite's driver only starts its helper
  (`Focusrite Notifier.exe`). If the app is still the foreground window about 2 s later, DRIVER PANEL
  calls it once more.
- The Preferences dialog's buffer list (`kBufferSizeOptions`) gained 48 and 384 to match.

## CAB LAB

[`CabLab.h`](../NeuralAmpModeler/CabLab.h): a speaker cab, two close mics and a room, mono in and stereo out.

- **Speaker:** 6 modeled cab/speaker types. Each is a set of RBJ biquads (cab resonance, presence
  peaks, HF roll-off) shaped after the manufacturer's published response chart. Alternatively, load
  your own IR.
- **Mics:** each mic type is a filter set approximating its published response. The placement
  physics is modeled directly:
  - **Distance** is a real delay, `d / 343 m/s`, and the level falls off with distance. Two mics at
    different distances therefore comb-filter like real ones. For example, SM57s at 2.5 cm and 30 cm
    notch near 660 Hz, and flipping one turns the notch into a +7 dB peak.
  - **Proximity effect** is a bass boost that grows as the mic gets closer. It is stronger for
    figure-8 mics (the ribbon) than for cardioids.
  - **Position** runs from the bright dust cap to the darker cone edge. Position matters less at a
    distance, because a farther mic hears more of the whole cone.
- **Room:** a stereo room pair with predelay, early reflections and a diffuse tail. **Size** sets the
  decay from about 0.15 to 0.6 s.

The speaker and mic curves approximate spec charts and are not measurements. The placement physics
(delays, fall-off, proximity, phase) is real.

## GATE and PRO COMP (1.52.0)

Two blocks in the AMP NEST pedal list (at the end), placed on the grid like any pedal.
([`Dynamics.h`](../NeuralAmpModeler/Dynamics.h))

**GATE** is a full noise gate:

| Control | What it does |
| --- | --- |
| Threshold | Opens when what it hears goes above this (dBFS) |
| Close | How far under the threshold it waits before closing (hysteresis, 0-20 dB, default 6) |
| Attack | How fast it opens (0.05-50 ms, default 0.5) |
| Hold | How long it stays open after the level drops under the close point (0-500 ms, default 60) |
| Release | How long it takes to close all the way (5-2000 ms, default 150) |
| Range | How far it turns down when closed: -90 is silent, -20 just lowers the hiss |
| Key HPF | A high-pass on what it listens to, for hum and rumble (20 Hz = off); the sound isn't filtered |
| Key Input | On (default): it listens to your clean guitar, wherever it sits. Off: to its own input |

**Key Input is what makes it solid.** After a high-gain amp, a gate listening to its own input has to
sit above the amp's hiss, and the amp keeps a dying note loud, so the gate cuts in late and chatters
on the tail. With Key Input on, the GATE goes after the AMP or Tube Lab but listens to the clean
signal after Master In: your picking opens and closes it, and the amp's noise can't hold it open.
This is how hardware gates like the ISP Decimator are used.

To set it up:

1. Place the GATE after your amp (or last before the cab).
2. Mute the strings and watch the KEY meter.
3. Set Threshold a few dB above the bar, under your softest notes.

The bright marker on the meter is the threshold and the dim one is the close point. The pill next to it
shows OPEN or CLOSED and how far the gate is turning down.

How it works: the detector is a peak follower on the key (the louder channel), rising in 0.3 ms (a lone
noise spike doesn't trip it) and falling over 10 ms (it doesn't drop out between the peaks of a low E).
Above the threshold the gate opens and the hold timer refills. Only after the level stays under the
close point for the whole Hold does it start closing. Opening is a linear ramp in amplitude over
Attack (no click); closing is a straight line in dB down to Range over Release. The key is a copy of
the input taken at the top of each block, because blocks before the GATE may process the input in
place. It isn't delayed to line up with the blocks before the GATE, so with a Tube Lab in front (about
1 ms of latency) the gate opens and closes that much early: an extra bit of lookahead on the pick,
covered by Hold at the end of a note.

**PRO COMP** is a studio compressor:

| Control | What it does |
| --- | --- |
| Threshold | Level (dBFS) above which it compresses (-60 to 0, default -24) |
| Ratio | 1:1 to 20:1; 20 is Limit (infinite ratio) |
| Attack | 0.05-100 ms (default 5). 10-30 ms lets the pick snap through, then holds the note |
| Release | 5-2000 ms (default 120) |
| Knee | 0 (hard) to 24 dB (soft), default 6 |
| Makeup | Output gain, -12 to +24 dB |
| Auto Gain | Adds back what a -12 dBFS peak loses, so Threshold and Ratio don't change the loudness much |
| Mix | Dry to compressed: 30-60% is parallel (New York) compression |
| Style | Clean, Opto or FET (below) |
| SC HPF | A high-pass on what it listens to (20 Hz = off): 80-150 Hz stops low notes pumping everything |

- **Clean:** a feed-forward VCA-style compressor with peak detection. The static curve is the standard
  soft-knee one, and the gain change is smoothed with separate attack and release (a "smooth
  branching" detector), so it's transparent and predictable.
- **Opto:** RMS detection over about 10 ms. Like an LA-2A's light cell, it remembers how hard it's
  been working (a follower rising over 2 s, falling over 5 s), and its release slows by up to 5x after
  heavy squeezing: fast on short peaks, slow and smooth on sustained compression. The knee is at
  least 6 dB.
- **FET:** fast like an 1176, and as it compresses it adds a soft clip whose drive grows with the gain
  reduction (up to 1.6x at 20 dB). It's clean at low levels; at heavy compression the 3rd harmonic is
  at about -36 dB, against -66 dB for Clean.
- Meters: IN with the threshold marked, GR from 0 to 24 dB (ticks at 3, 6, 12 and 18) and OUT.

Both blocks are stereo-linked (one detector on the louder channel, one gain for both) and add no
latency. Each uses under 0.5% of a core at 48 kHz. Their meters read atomics the audio thread fills
with the peak since the last UI frame, so a short hit always shows. The params are at the end of the
enum (`kGateActive`..`kSCompScHpf`, then `kGridCell41/42`), saved by name, and states from before
1.52.0 load with both blocks off the grid. The AMP block's built-in Gate (the original one-knob gate) and
the original COMP pedals are unchanged.

## Other blocks

**Output limiter (1.37.0)**, the Limiter switch under Master Out, is a stereo-linked lookahead peak
limiter at -1 dBFS (`OutputLimiter.h`), before the app's safety soft clip. The gain each sample
needs (`ceiling / peak`, or 1) is held at its minimum for the 0.67 ms window. It's released over 30
ms (a one-pole, instant attack), then averaged over the window. The audio is delayed by the window
less one sample. Each output sample's gain is then an average of values at or below what that
sample needs, so no output exceeds the ceiling, and below it the gain is exactly 1. It adds 31
samples of latency at 48 kHz while on. Switching it fades the output out, switches between blocks,
and fades back in. The COMP pedal is stereo-linked since 1.37.0 (one detector on the louder
channel).

**CAB 2 / CAB 3 (1.36.0)** are extra cab IR blocks for running several cabs. Each has its own IR
(saved by path in a `###ExtraCabIRsV1###` section), On, Level and Pan. It sums its input to mono,
convolves it (`FastImpulseResponse`, FFT, no added latency), and outputs stereo with a balance
pan. Use them in parallel to blend cabs, or in series. A cab with no IR passes its input through.
An amp-and-cab capture (`gear_type: amp_cab` in the `.nam` file) already includes a cab, so cab blocks after it
stack a second one.

- **Switching (1.42.0):** every block's On switch fades its *input* in or out over 20 ms
  (smoothstep) and the dry signal the other way: `out = (1 - g)·in + block(g·in)`. A delay line
  or reverb therefore fills and empties smoothly, and DELAY, REVERB, CHORUS, FLANGER, PHASER and
  the cabs ring out when switched off (they keep processing silence until their output is below
  1e-7, or 20 s). Density and Boost crossfade the same way. Pedal knobs glide (20 ms one-pole);
  DELAY's Time crossfades between the old and new read points over 30 ms. Master In/Out ramp
  across each block.
- **Pedals** ([`Pedals.h`](../NeuralAmpModeler/Pedals.h)): Compressor (envelope follower, 2:1-10:1;
  since 1.42.0 its threshold is 6-40 dB under a peak follower of your recent loudest -- up at
  once, down 1 dB/s -- with that much make-up, so Sustain acts the same at any level),
  Overdrive (tanh clip and tone tilt), Chorus, Delay, Reverb (Freeverb style: 4 damped combs and 2
  allpasses), 3-band EQ, Tremolo, and Flanger (0.25-7 ms triangle sweep, exponential in delay,
  bipolar regen, Hermite-interpolated delay line; one copy), and Phaser (4 allpass stages swept
  150 Hz to ~4 kHz, bipolar regen; one copy). There are also two **capture pedals** (CAPTURE 1 and CAPTURE 2), which load a `.nam`
  capture of a real pedal (mono, like the amp model; they can go anywhere on the grid).
- **1.44.0 pedals** ([`NewPedals.h`](../NeuralAmpModeler/NewPedals.h)), modelled on how the originals
  behave (not their circuits), each with a wider popup:
  - **SKY VERB** (Strymon BigSky style): an 8-line feedback delay network (Householder mixing,
    per-line one-pole damping from Tone, slow line-length modulation from Mod) after a crossfaded
    pre-delay and four input allpasses. Line gains come from RT60 (`10^(-3·len/RT60)`, with RT60 x1.2
    for the damping's own loss). Modes set the line lengths and diffusion: Hall 30-79 ms, Plate
    17-41 ms (denser), Room 7-23 ms, Cloud 61-151 ms (heavier modulation). Shimmer pitch-shifts the
    tank an octave up (two crossfaded taps reading at double speed) and feeds it back; Bloom scales
    the input by a slow envelope over a fast one, so the reverb swells in. Freeze sets the loss and
    damping to none, stops the modulation and rounds the line lengths, so the loop is lossless.
  - **FLANGE DBL** (MXR 126 style): a Hermite-interpolated delay; Flanger sweeps 0.3-4 ms up to x8 with
    a triangle; Doubler sits at 15-70 ms and wanders (a slow sine plus smoothed noise).
  - **WAH**: a TPT state-variable filter (band-pass plus some low-pass), swept exponentially: Cry Baby
    350 Hz-2.2 kHz at Q 5.5, V847 450 Hz-1.6 kHz at Q 3.5; Auto drives the position from an envelope
    against a peak follower of your loudest picks.
  - **PREC DRIVE** (Horizon Precision Drive style): Attack high-pass (60-500 Hz) and a mid push into a
    biased tanh with first-order antiderivative anti-aliasing, a low-pass after (Bright opens it), and a
    gate keyed from the input. **APEX PRE** (Horizon Apex style): three biased anti-aliased tanh stages
    with filtering between them, then shelving/peaking Bass, Mid, Treble and Presence.
- **Tuner** ([`Tuner.h`](../NeuralAmpModeler/Tuner.h)): reads the raw input, decimated to about 22-24 kHz,
  and uses the McLeod pitch method (normalized autocorrelation, first strong peak) so it doesn't jump
  an octave. It is accurate to about 0.04 cents at every sample rate and uses about 1% CPU.
- **Tone stack, noise gate, cab IR loader, FX loop:** inherited from the plug-in this app is forked from (see Credits).
- **MIDI control:** see [MIDI control](#midi-control).

## Building

Windows, with VS2019 Build Tools. The projects target v143, so override the toolset. From
`NeuralAmpModeler/`:

```
MSBuild.exe NeuralAmpModeler.sln /t:NeuralAmpModeler-app  /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v142
MSBuild.exe NeuralAmpModeler.sln /t:NeuralAmpModeler-vst3 /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v142
```

The app is built to `NeuralAmpModeler/build-win/app/x64/Release/NeuralAmpModeler.exe`. The VST3
post-build step copies into `C:\Program Files\Common Files\VST3`, which needs admin rights. Without
them that step fails, and the VST3 has to be copied there by hand. The build is a single DLL; copy
it over `NeuralAmpModeler.vst3\Contents\x86_64-win\NeuralAmpModeler.vst3` inside the installed
bundle, from an elevated (admin) prompt.

Compiler flags: SSE2 (the default). `/arch:AVX2` was measured in 1.45.0 and made the whole plugin
slower on the i7-6700HQ it runs on (19.6% → 20.9% of a core), so it isn't used.

Versioning: `PLUG_VERSION_STR` / `PLUG_VERSION_HEX` in `NeuralAmpModeler/config.h`, with a
[CHANGELOG.md](../CHANGELOG.md) entry for every build that gets used. A patch release is fixes only, a
minor release adds features, and a major release means older saves no longer load correctly.


## Building from a fresh clone

The libraries are git submodules pinned to the upstream commits this fork was built on. Griffin's changes to them are
kept as patch files in [`patches/`](../patches/), and you apply them after cloning. Without them, the app won't build (or
it would lose the faster amp-model kernel in `NeuralAmpModelerCore/NAM/wavenet/a2_fast.cpp`).

```
git clone --recursive https://github.com/Gwojtowicz/Griffin-Amplification.git
cd Griffin-Amplification
powershell -ExecutionPolicy Bypass -File scripts\apply-patches.ps1     # or: bash scripts/apply-patches.sh
cd iPlug2/Dependencies/IPlug && bash ./download-vst3-sdk.sh v3.8.1_build_84 && cd ../../..
cd iPlug2/Dependencies && bash ./download-prebuilt-libs.sh && cd ../..
```

| Submodule | Upstream | Pinned commit | Patch |
|---|---|---|---|
| iPlug2 | github.com/sdatkinson/iPlug2 | `de5a4fb14fd9` | `patches/iPlug2.patch` (NanoVG/GPU, RtAudio, IControl, APP dialog) |
| NeuralAmpModelerCore | github.com/sdatkinson/NeuralAmpModelerCore | `9c7b185de346` | `patches/NeuralAmpModelerCore.patch` (`a2_fast.cpp` AVX2 kernel) |
| AudioDSPTools | github.com/sdatkinson/AudioDSPTools | `e19ef4b5b3bf` | `patches/AudioDSPTools.patch` (stereo IR loading, filter speedups) |
| eigen | gitlab.com/libeigen/eigen | `ed8cda3ce4cb` | none |

Then build as described in [Building](#building). The script can be run again safely: it skips a patch that's already applied.

## A/B slots and the cab IR

- A **stereo IR** in the CAB block plays its left channel on the left and its right channel on
  the right (`dsp::wav::LoadChannels`); a mono one plays on both sides. A right-channel IR loaded
  with the **R** button overrides the file's right channel. CAB LAB and CAB 2/3 convolve mono, so
  they use the average of a stereo file's channels (`dsp::wav::Load` averages them; it used to
  refuse anything but mono).
- **A/B** (1.42.0): each slot has its own model, cab IR and right-channel IR. Switching runs on
  the audio thread: the incoming model first runs on the AMP block's input for 100 ms (unheard),
  then the AMP and CAB outputs fade out (8 ms), the slots swap at silence (the incoming cab IRs'
  convolution history is cleared), and they fade in (15 ms). Everything after AMP and CAB sees the
  fade, so a reverb doesn't ring a step.

## MIDI control

Any knob or switch can be driven from a MIDI controller (a footswitch board, an expression pedal,
a knob box). Mappings are set with an Ableton-style learn mode (1.43.0).

**Mapping a control**

1. Click **MIDI** in the top bar (left of LOAD). Every control you can map gets a blue tint; mapped
   ones turn green and show their CC.
2. Click what you want to control:
   - a **pedal block** on the grid (or in the pedal list) maps its **on/off**; its popup opens too,
     so you can click one of its knobs instead;
   - any **knob or switch** (in a popup, the AMP popup, A/B, TUNE).
   The selected control turns orange.
3. Press or move the control on your MIDI controller. The status line at the bottom right shows the
   CC it got.
4. Click **MIDI** again. That's it: the mapping is saved.

A new mapping **replaces** what that footswitch did before. **Shift-click** a control to **add** it
to a footswitch instead (several controls on one switch toggle together: if any of them is on, all
go off, otherwise all come on, so they can't get out of step). **Right-click** a control to clear
its mapping. The strip along the bottom lists every mapping and the last MIDI message received.

**How mapped controls respond**

- A **switch** (a pedal's on/off, A/B, the tuner) toggles on every message. Latching footswitches
  (like the Rig Kontrol's) send 127 on one press and 0 on the next; each counts as a press.
- A **knob** follows the CC's value: 0-127 across its whole range.
- The **volume pedal** (CC 1, 4, 7 and 11 by default) is an extra output volume, not a saved setting.

**Where mappings live**

Mappings belong to the app, not to a preset: your pedal does the same thing whatever sound you load.
They're saved in `%LOCALAPPDATA%\NeuralAmpModeler\midimap.txt`, shared by the app and the plug-in.
Without that file, the app starts from the original Rig Kontrol 3 layout: CC 76-79 = COMP, OD,
CHORUS, DELAY; CC 80 = every REVERB; CC 81 = A/B; CC 82 = tuner; CC 83 = FX loop; CC 64 (toe
switch) = Tube Lab; CC 1/4/7/11 = volume. Delete the file to go back to that layout.

**Example: a wah on the expression pedal**

Put the **WAH** first on the grid (IN -> WAH -> ...). In MIDI learn, open the WAH's popup, click
**Position** and rock the expression pedal: heel is dark, toe is bright. That takes the pedal's CC
off the volume pedal. To switch the wah on and off with the toe switch, click the WAH block, press
the toe switch, and click MIDI to finish. (Hand-written equivalent in `midimap.txt`:
`4 param <index> Wah Position` and `64 param <index> WahEngaged`; the app reads the file when it starts.)

**Native Instruments Rig Kontrol 3**

- The pedal only sends MIDI while **Controller Editor 1.7.4** is open and connected (the pedal's
  display shows "con"). Start it before the app.
- In the standalone, choose **Rig Kontrol 3 MIDI In** under File > Preferences > MIDI input (or
  Settings > **DEVICE...**). The app
  lists MIDI ports when it starts, so a pedal plugged in later needs an app restart.
- Controller Editor's switch numbers: **1-4** (bottom row) send **CC 80-83**, **5-8** (top row) send
  **CC 76-79**, the toe switch sends **CC 64**, and the expression pedal CC 4.

**Troubleshooting:** with MIDI learn on, press a footswitch and watch the status line. "Last MIDI in:
CC n" means the app hears it. "No MIDI in yet" means it doesn't: check Controller Editor is
connected and the MIDI input in Preferences, then restart the app.

## Saved-state format

The first 75 parameters are saved in a fixed order, as in 1.7.0. The capture-pedal file paths come next,
then a `###ExtraParamsV1###` section that holds every later parameter **by name**. New parameters go
at the end of the parameter enum and are saved by name. That way old presets and autosaves keep
loading, and older versions skip settings they don't recognize. The chain grid is saved this way
too: `GridCellN` (block N's cell, -1 = off the grid), `GridInN` and `GridOut` (bit masks of which
blocks, or bit 15 = IN, are cabled into each block and into OUT), and `ChainGridV1`, for every block
id N but 15. The masks are 64-bit since 1.34.0 (33 blocks), stored as whole-number doubles (exact up
to 53 bits). Blocks after id 31 have their grid params at the end of the parameter list, so adding
one never moves an existing parameter's index. A state without them is laid out from its old Slot
params on load. After the capture-pedal paths come `###CabLabIRV1###` (CAB LAB's IR path),
`###ExtraCabIRsV1###` (a count, then CAB 2 / CAB 3's IR paths), and since 1.42.0
`###CabIRRightV1###` (the right-channel cab IR's path, or empty) and `###ABSlotsV1###` (the index
of the A/B slot that isn't live, then its model, cab IR and right-channel IR paths; `ABSlot` is
the live one). A state without a section clears what it holds. The autosave is in
`%LOCALAPPDATA%\NeuralAmpModeler\autosave.namstate`.

## Credits

- Steven Atkinson's open-source amp-modeling plug-in
  ([source](https://github.com/sdatkinson/NeuralAmpModelerPlugin), MIT) and its core library, which this
  app is forked from. It is built with [iPlug2](https://iplug2.github.io).
- K. Dempwolf and U. Zoelzer, "A Physically-motivated Triode Model for Circuit Simulations", DAFx 2011.
- N. Koren, "Improved VT models for SPICE simulations" (triode and pentode equations).
- P. McLeod and G. Wyvill, "A Smarter Way to Find Pitch" (tuner).
- R. Bristow-Johnson, "Audio EQ Cookbook" (biquads).
