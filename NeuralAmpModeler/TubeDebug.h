#pragma once

// Tube Lab debug readout and CSV log (1.32.0).
//
// Explain() turns one window of probe statistics (TubeProbe.h) plus the current settings into
// plain-English lines about what the circuit is doing and how that plays: where each tube is
// biased, how much of the time it's clipping and on which side, blocking, sag, crossover in the
// power amp, and an overall "feel" verdict. TubeDebugLog writes the same numbers to a CSV file,
// one row per window, with the settings that changed since the last row.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "TubeProbe.h"
#include "TubeStage.h"

// The debug page's scope settings, owned by the plugin so they survive a relayout.
struct TubeScopeSettings
{
  int chA = tube::kProbeV1Plate;
  int chB = tube::kProbeV1Vgk; // -1 = off
  int timebase = 2; // Index into kTubeScopeMsPerDiv (TubeDebugControls.h)
  bool freeze = false;
};

namespace tube
{
namespace debug
{
inline double ToDB(double amp) { return amp > 1e-9 ? 20.0 * std::log10(amp) : -180.0; }

// A sentence with a tag ("V1", "POWER", ...) for the readout's left column.
struct Line
{
  std::string tag;
  std::string text;
  bool warning = false; // Drawn highlighted: clipping the next block, CPU near its limit, ...
};

inline std::string Format(const char* fmt, ...)
{
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return buf;
}

// Below this input peak the window counts as silence (-80 dBFS).
constexpr double kSilence = 1e-4;

inline bool HasSignal(const ProbeStats& s) { return s.inPeak > kSilence; }

// How much of the time any stage is clipping (grid conduction or cutoff), in percent: the basis of
// the feel verdict. The power amp's cutoff counts half, since class AB hands the waveform from one
// tube to the other even when clean.
inline double Dirt(const ProbeStats& s)
{
  double dirt = 0.0;
  if (!s.circuit)
    return 0.0;
  for (int i = 0; i < (s.stage2 ? 2 : 1); i++)
    dirt = std::max(dirt, s.Percent(s.gridConducting[i] + s.cutoff[i], s.samples));
  if (s.powerAmp)
    dirt = std::max(dirt, s.Percent(s.paGridConducting, s.paSamples) + 0.5 * s.Percent(s.paCutoff, s.paSamples));
  return std::min(dirt, 100.0);
}

inline const char* FeelLabel(const ProbeStats& s)
{
  if (!HasSignal(s))
    return "No signal";
  if (!s.circuit)
    return "Classic engine";
  const double d = Dirt(s);
  return d < 0.5 ? "Clean" : d < 5.0 ? "Edge of breakup" : d < 20.0 ? "Crunch" : d < 45.0 ? "Overdrive" : "Saturated";
}

inline std::string FeelSentence(const ProbeStats& s)
{
  const double d = Dirt(s);
  if (d < 0.5)
    return "the tubes stay in their linear range. Dynamics come from your hands, not the amp; it takes pedals "
           "well.";
  if (d < 5.0)
    return "only your hardest pick attacks clip. Dig in for grit, pick lighter or roll the guitar's volume back "
           "and it cleans up.";
  if (d < 20.0)
    return "every note has grit but still follows how hard you pick: the classic rock rhythm zone.";
  if (d < 45.0)
    return "most of the waveform is clipping: long sustain, compressed and smooth, less dynamic range.";
  return "the stages clip nearly all the time: thick, compressed, lots of sustain, and little response to "
         "picking dynamics. Tight lows (cathode cap) and less Bloom keep it from getting muddy.";
}

inline const char* BiasCharacter(double biasV)
{
  if (biasV > -1.0)
    return "hot: close to grid conduction, so it clips on the attack side first. Warm, compressed, more even "
           "harmonics";
  if (biasV >= -2.0)
    return "centered: about equal headroom both ways, the textbook preamp bias";
  return "cold: it runs out of current (cutoff) before the grid conducts, so it clips on the other half first. "
         "Spittier and raspier, more headroom for the attack";
}

// Rough plate-cap treble corner: the cap across Ra, against Ra || rp (rp ~ 62.5k for a 12AX7).
inline double PlateCapCornerHz(double plateKOhm, double capPF)
{
  if (capPF <= 0.0)
    return 0.0;
  const double ra = plateKOhm * 1e3, rp = 62.5e3;
  return 1.0 / (2.0 * 3.14159265358979 * (ra * rp / (ra + rp)) * capPF * 1e-12);
}

inline void ExplainStage(std::vector<Line>& out, const ProbeStats& s, const TubeParams& p, int i, bool signal)
{
  const char* tag = i == 0 ? "V1" : "V2";
  const double bias = i == 0 ? p.biasV : p.bias2V;
  out.push_back({tag, Format("Bias %.2f V (Rk %.2fk, %.2f mA idle, plate resting at %.0f V of %.0f V B+) is %s.", bias,
                             s.rk[i] / 1e3, s.idleMA[i], s.plateRest[i], s.rail, BiasCharacter(bias))});

  const bool cap = i == 0 ? p.cathodeCap : p.cathodeCap2;
  const double capUF = i == 0 ? p.cathodeCapUF : p.cathodeCap2UF;
  const double corner = s.cathodeCornerHz[i];
  if (!cap)
    out.push_back({tag, "No cathode bypass cap: the cathode resistor's local feedback roughly halves the gain. Tighter, "
                        "cleaner, more headroom."});
  else if (corner > 300.0)
    out.push_back({tag, Format("Cathode cap %.2f uF: full gain only above ~%.0f Hz, so the lows are cut before they "
                               "distort. Tight and focused, no flub on palm mutes.",
                               capUF, corner)});
  else if (corner > 60.0)
    out.push_back({tag, Format("Cathode cap %.2f uF: full gain above ~%.0f Hz; the lowest bass is trimmed a little.", capUF,
                               corner)});
  else
    out.push_back({tag, Format("Cathode cap %.1f uF: full gain down to ~%.0f Hz. Big, full bottom (it can get flubby "
                               "with lots of gain after it).",
                               capUF, std::max(corner, 1.0))});
  const double plateCap = i == 0 ? p.plateCapPF : p.plateCap2PF;
  if (plateCap > 0.0)
    out.push_back({tag, Format("Plate cap %.0f pF rolls the fizz off above ~%.1f kHz.", plateCap,
                               PlateCapCornerHz(p.plateKOhm, plateCap) / 1e3)});

  if (!signal)
    return;
  const double cond = s.Percent(s.gridConducting[i], s.samples), cut = s.Percent(s.cutoff[i], s.samples);
  std::string act = Format("Grid swings %.2f to %+.2f V; plate %.0f to %.0f V.", s.vgkMin[i], s.vgkMax[i], s.plateMin[i],
                           s.plateMax[i]);
  if (cond < 0.05 && cut < 0.05)
    act += " Never clips: linear, clean gain.";
  else
  {
    if (cond >= 0.05)
      act += Format(" Grid conducting %.1f%% of the time: the grid clamps the peaks, a soft, rounded clip.", cond);
    if (cut >= 0.05)
      act += Format(" Cut off %.1f%% of the time: a harder clip on the other half, more odd harmonics.", cut);
    if (cond >= 0.05 && cut >= 0.05)
      act += " Clipping on both sides: it's being driven well past its headroom.";
  }
  out.push_back({tag, act});

  const double block = s.blockMax[i];
  if (block > 0.1)
  {
    std::string b = Format("Blocking: hard hits charged the %s cap and pushed the bias up to %.2f V colder (to %.2f V); "
                           "it recovers over ~%.0f ms.",
                           i == 0 ? "input" : "interstage", block, bias - block, s.blockRecoverMs[i]);
    if (block > std::fabs(bias))
      b += " That's past the whole bias: the stage chokes for a moment after big hits (a gated, farty decay). Lower "
           "Bloom / Couple shortens it.";
    else if (block > 0.3)
      b += " You hear it as the note 'blooming' back after the pick attack.";
    out.push_back({tag, b});
  }
}

// The readout: an overall verdict first, then each section of the circuit.
inline std::vector<Line> Explain(const ProbeStats& s, const TubeParams& p, bool engaged)
{
  std::vector<Line> out;
  const bool signal = HasSignal(s);
  if (!engaged)
    out.push_back({"FEEL", "The Tube Lab is switched off (bypassed): the numbers below are what it would do.", true});
  if (!signal)
    out.push_back({"FEEL", "No signal reaching the Tube Lab (below -80 dBFS). Play something, or check it's on the "
                           "grid and cabled in."});
  else if (!p.circuit)
    out.push_back({"FEEL", "Classic engine (lookup-table tube): there are no circuit nodes to probe. Turn Circuit on "
                           "for the full readout."});
  else
    out.push_back({"FEEL", std::string(FeelLabel(s)) + ": " + FeelSentence(s)});

  if (signal)
  {
    const double headroom = std::fabs(p.biasV);
    std::string in = Format("Input peaks %.1f dBFS; Drive %+.1f dB makes that %.2f V at V1's grid", ToDB(s.inPeak),
                            p.driveDB, s.gridDrivePeak);
    if (p.circuit)
      in += Format(", %.1fx the %.2f V of headroom before its grid conducts.", s.gridDrivePeak / std::max(headroom, 0.1),
                   headroom);
    else
      in += ".";
    out.push_back({"IN", in});
  }
  if (!p.circuit)
    return out;

  ExplainStage(out, s, p, 0, signal);
  if (p.sag)
  {
    std::string sag = Format("Preamp sag on: %.0fk supply resistor, recovers over %.2f s.", p.sagSupplyKOhm, p.sagTimeS);
    if (signal)
    {
      const double drop = s.rail - s.railMin;
      sag += Format(" B+ dipped %.1f V (%.1f%%).", drop, 100.0 * drop / s.rail);
      if (drop / s.rail > 0.02)
        sag += " The attack compresses, then the note swells as the supply recovers.";
    }
    out.push_back({"SUPPLY", sag});
  }
  if (p.stage2)
  {
    out.push_back({"V2", Format("Second stage, fed through Gain 2 (%.0f%% of V1's swing) and a %.1f nF coupling cap.",
                                100.0 * TubeParams::PotToGain(p.gain2), p.coupleNF)});
    ExplainStage(out, s, p, 1, signal);
  }
  if (p.toneStack > 0)
  {
    std::string t = p.toneStack == 1 ? "Fender stack: a deep mid scoop at noon, sparkly highs, the clean-amp voice."
                                     : "Marshall stack: less mid scoop than a Fender, more upper-mid push: the crunch "
                                       "voice.";
    if (p.mid < 0.3)
      t += " Mid is low: heavier and scooped, but it can get lost in a band mix.";
    else if (p.mid > 0.7)
      t += " Mid is high: forward and vocal, cuts through a mix.";
    t += " Being passive, it sits after the preamp clipping, so it shapes the distortion rather than what clips.";
    out.push_back({"TONE", t});
  }
  if (p.powerAmp)
  {
    out.push_back({"POWER", Format("%s pair, biased at %.1f V for %.0f mA idle per tube, %.0f V B+. %s",
                                   p.sixL6 ? "6L6" : "EL34", s.paBias, s.paIdleMA, s.paRailRest,
                                   p.sixL6 ? "6L6s: big clean headroom, tight lows, glassy highs (Fender/American)."
                                           : "EL34s: midrange grind, breaks up earlier (Marshall/British).")});
    if (signal && s.paSamples > 0)
    {
      const double cond = s.Percent(s.paGridConducting, s.paSamples), cut = s.Percent(s.paCutoff, s.paSamples);
      std::string a = Format("Power grids reach %+.1f V (bias %.1f V).", s.paGridMax, s.paBias);
      if (cond > 0.05)
        a += Format(" Driven past 0 V %.1f%% of the time: grid current charges the coupling caps, so the power amp "
                    "compresses and briefly blocks after hits.",
                    cond);
      if (cut > 50.0)
        a += Format(" One tube is cut off %.0f%% of the time: both halves are pushed into hard class AB. The "
                    "'cranked amp' squash and sustain.",
                    cut);
      else if (cut > 1.0)
        a += Format(" One tube is cut off %.0f%% of the time: class AB handing the waveform from one tube to the "
                    "other (crossover).",
                    cut);
      else
        a += " Both tubes conduct throughout (class A region): clean power amp.";
      out.push_back({"POWER", a});
      const double drop = s.paRailRest - s.paRailMin;
      std::string sag = Format("B+ sagged %.0f V (%.1f%%), screens down to %.0f V (rest %.0f V).", drop,
                               100.0 * drop / s.paRailRest, s.paScreenMin, s.paScreenRest);
      if (drop / s.paRailRest > 0.03)
        sag += " Spongy: the supply gives under hard playing, then notes bloom back.";
      else
        sag += " Stiff: the supply holds, so the attack stays punchy.";
      out.push_back({"POWER", sag});
    }
    std::string fb = Format("%.1f dB of negative feedback: ", s.feedbackDB);
    fb += s.feedbackDB > 6.0 ? "tight, flat, cleaner and more controlled."
          : s.feedbackDB > 2.0 ? "a balance of control and bite."
                               : "loose and raw: more grind and a rougher breakup.";
    if (p.speakerLoad)
      fb += " Speaker load on: the power tubes see the speaker's impedance, so less feedback means more bass "
            "resonance (~95 Hz) and a brighter top.";
    out.push_back({"POWER", fb});
  }

  if (signal && s.outSamples > 0)
  {
    const double rms = std::sqrt(s.outSumSq / s.outSamples);
    Line o{"OUT", Format("Tube Lab output peaks %.1f dBFS (RMS %.1f dBFS).", ToDB(s.outPeak), ToDB(rms))};
    if (s.outPeak > 1.0)
    {
      o.text += " Over full scale: lower Output, or whatever follows will clip.";
      o.warning = true;
    }
    out.push_back(o);
  }
  if (s.audioSeconds > 0.0)
  {
    const double cpu = 100.0 * s.cpuSeconds / s.audioSeconds;
    Line c{"CPU", Format("Tube Lab uses %.0f%% of real time (%dx oversampled to %.0f kHz)", cpu, s.oversample,
                         s.oversample * s.sampleRate / 1e3)};
    if (s.samples > 0)
      c.text += Format("; circuit solver %.1f iterations per sample on average, %d at most.",
                       static_cast<double>(s.newtonSum) / s.samples, s.newtonMax);
    else
      c.text += ".";
    if (cpu > 50.0)
    {
      c.text += " Close to the limit: a bigger audio buffer avoids dropouts.";
      c.warning = true;
    }
    out.push_back(c);
  }
  return out;
}

// One CSV row per statistics window. The settings columns come from TubeParams; `changes` lists
// what was changed since the previous row ("Drive 12.0 -> 18.0; ...").
class TubeDebugLog
{
public:
  ~TubeDebugLog() { Close(); }

  bool Open(const std::string& path)
  {
    Close();
    mFile = std::fopen(path.c_str(), "w");
    if (!mFile)
      return false;
    mPath = path;
    mRows = 0;
    mHavePrev = false;
    std::fputs("time_s,clock,feel,changes,tube_on,engine,drive_db,bias_v,bloom,bright,mix,output_db,cathode_cap_uf,"
               "plate_kohm,plate_cap_pf,sag,supply_kohm,recovery_s,stage2,gain2,bias2_v,cap2_uf,plate_cap2_pf,"
               "couple_nf,power_amp,master,power_sag,feedback,presence,tubes,speaker_load,tone_stack,bass,mid,"
               "treble,in_peak_dbfs,grid_drive_peak_v,v1_vgk_min,v1_vgk_max,v1_grid_cond_pct,v1_cutoff_pct,"
               "v1_plate_min,v1_plate_max,v1_block_max_v,v2_vgk_min,v2_vgk_max,v2_grid_cond_pct,v2_cutoff_pct,"
               "v2_plate_min,v2_plate_max,v2_block_max_v,pre_rail_min,pa_grid_max,pa_grid_cond_pct,pa_cutoff_pct,"
               "pa_current_max_ma,pa_rail_min,pa_screen_min,out_peak_dbfs,out_rms_dbfs,dirt_pct,cpu_pct,"
               "newton_avg,newton_max,sample_rate,oversample,block_size,latency,chain\n",
               mFile);
    std::fflush(mFile);
    return true;
  }

  void Close()
  {
    if (mFile)
      std::fclose(mFile);
    mFile = nullptr;
  }

  bool IsOpen() const { return mFile != nullptr; }
  const std::string& Path() const { return mPath; }
  int Rows() const { return mRows; }

  void Write(double timeS, const char* clock, const ProbeStats& s, const TubeParams& p, bool engaged, int blockSize,
             int latency, const std::string& chain)
  {
    if (!mFile)
      return;
    const std::string changes = mHavePrev ? Changes(mPrev, p, mPrevEngaged, engaged) : "start";
    mPrev = p;
    mPrevEngaged = engaged;
    mHavePrev = true;
    const bool sig = HasSignal(s);
    auto num = [&](double v, const char* fmt = "%.4g") { return sig ? Format(fmt, v) : std::string(); };
    auto pct = [&](int64_t n, int64_t of) { return sig ? Format("%.2f", s.Percent(n, of)) : std::string(); };
    const bool st2 = sig && p.stage2 && p.circuit;
    const bool pa = sig && p.powerAmp && p.circuit && s.paSamples > 0;
    const double rms = s.outSamples > 0 ? std::sqrt(s.outSumSq / s.outSamples) : 0.0;
    std::string row = Format("%.3f,%s,%s,\"%s\",%d,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.0f,%d,%.1f,%.2f,%d,"
                             "%.2f,%.2f,%.2f,%.0f,%.1f,%d,%.2f,%.2f,%.2f,%.2f,%s,%d,%s,%.2f,%.2f,%.2f",
                             timeS, clock, FeelLabel(s), changes.c_str(), engaged ? 1 : 0,
                             p.circuit ? "circuit" : "classic", p.driveDB, p.biasV, p.bloom, p.bright, p.mix,
                             p.outputDB, p.cathodeCap ? p.cathodeCapUF : 0.0, p.plateKOhm, p.plateCapPF, p.sag ? 1 : 0,
                             p.sagSupplyKOhm, p.sagTimeS, p.stage2 ? 1 : 0, p.gain2, p.bias2V,
                             p.cathodeCap2 ? p.cathodeCap2UF : 0.0, p.plateCap2PF, p.coupleNF, p.powerAmp ? 1 : 0,
                             p.master, p.powerSag, p.feedback, p.presence, p.sixL6 ? "6L6" : "EL34",
                             p.speakerLoad ? 1 : 0,
                             p.toneStack == 0 ? "off" : p.toneStack == 1 ? "fender" : "marshall", p.bass, p.mid,
                             p.treble);
    row += "," + (sig ? Format("%.1f", ToDB(s.inPeak)) : std::string()) + "," + num(s.gridDrivePeak);
    const bool st1 = sig && p.circuit;
    for (int i = 0; i < 2; i++)
    {
      const bool on = i == 0 ? st1 : st2;
      auto n = [&](double v) { return on ? Format("%.4g", v) : std::string(); };
      auto pc = [&](int64_t c) { return on ? Format("%.2f", s.Percent(c, s.samples)) : std::string(); };
      row += "," + n(s.vgkMin[i]) + "," + n(s.vgkMax[i]) + "," + pc(s.gridConducting[i]) + "," + pc(s.cutoff[i]) + ","
             + n(s.plateMin[i]) + "," + n(s.plateMax[i]) + "," + n(s.blockMax[i]);
    }
    row += "," + (st1 ? Format("%.2f", s.railMin) : std::string());
    auto pn = [&](double v) { return pa ? Format("%.4g", v) : std::string(); };
    row += "," + pn(s.paGridMax) + "," + (pa ? pct(s.paGridConducting, s.paSamples) : std::string()) + ","
           + (pa ? pct(s.paCutoff, s.paSamples) : std::string()) + "," + pn(1e3 * s.paCurrentMax) + ","
           + pn(s.paRailMin) + "," + pn(s.paScreenMin);
    row += "," + (sig ? Format("%.1f", ToDB(s.outPeak)) : std::string()) + ","
           + (sig ? Format("%.1f", ToDB(rms)) : std::string()) + "," + num(Dirt(s), "%.2f");
    row += Format(",%.1f", s.audioSeconds > 0.0 ? 100.0 * s.cpuSeconds / s.audioSeconds : 0.0);
    row += "," + (s.samples > 0 && st1 ? Format("%.2f", static_cast<double>(s.newtonSum) / s.samples) : std::string())
           + "," + (st1 ? Format("%d", s.newtonMax) : std::string());
    row += Format(",%.0f,%d,%d,%d,\"%s\"\n", s.sampleRate, s.oversample, blockSize, latency, chain.c_str());
    std::fputs(row.c_str(), mFile);
    mRows++;
    if (mRows % 8 == 0) // About every 2 s, so a crash loses little
      std::fflush(mFile);
  }

  // What changed between two settings, as "Drive 12.0 -> 18.0; Bias -1.50 -> -2.00" (empty if nothing).
  static std::string Changes(const TubeParams& a, const TubeParams& b, bool engagedA, bool engagedB)
  {
    std::string out;
    auto add = [&](const char* name, double x, double y, const char* fmt = "%.2f") {
      if (std::fabs(x - y) < 1e-9)
        return;
      if (!out.empty())
        out += "; ";
      out += std::string(name) + " " + Format(fmt, x) + " -> " + Format(fmt, y);
    };
    add("On", engagedA, engagedB, "%.0f");
    add("Circuit", a.circuit, b.circuit, "%.0f");
    add("Drive", a.driveDB, b.driveDB, "%.1f");
    add("Bias", a.biasV, b.biasV);
    add("Bloom", a.bloom, b.bloom);
    add("Bright", a.bright, b.bright);
    add("Mix", a.mix, b.mix);
    add("Output", a.outputDB, b.outputDB, "%.1f");
    add("Cap uF", a.cathodeCap ? a.cathodeCapUF : 0.0, b.cathodeCap ? b.cathodeCapUF : 0.0);
    add("Plate k", a.plateKOhm, b.plateKOhm, "%.0f");
    add("Plate Cap pF", a.plateCapPF, b.plateCapPF, "%.0f");
    add("Sag", a.sag, b.sag, "%.0f");
    add("Supply k", a.sagSupplyKOhm, b.sagSupplyKOhm, "%.0f");
    add("Recovery s", a.sagTimeS, b.sagTimeS);
    add("Stage 2", a.stage2, b.stage2, "%.0f");
    add("Gain 2", a.gain2, b.gain2);
    add("Bias 2", a.bias2V, b.bias2V);
    add("Cap 2 uF", a.cathodeCap2 ? a.cathodeCap2UF : 0.0, b.cathodeCap2 ? b.cathodeCap2UF : 0.0);
    add("Plate Cap 2 pF", a.plateCap2PF, b.plateCap2PF, "%.0f");
    add("Couple nF", a.coupleNF, b.coupleNF, "%.1f");
    add("Power Amp", a.powerAmp, b.powerAmp, "%.0f");
    add("Master", a.master, b.master);
    add("Power Sag", a.powerSag, b.powerSag);
    add("Feedback", a.feedback, b.feedback);
    add("Presence", a.presence, b.presence);
    add("6L6", a.sixL6, b.sixL6, "%.0f");
    add("Spkr Load", a.speakerLoad, b.speakerLoad, "%.0f");
    add("Tone Stack", a.toneStack, b.toneStack, "%.0f");
    add("Bass", a.bass, b.bass);
    add("Mid", a.mid, b.mid);
    add("Treble", a.treble, b.treble);
    return out;
  }
  // Whether Write() would log a settings change.
  bool SettingsChanged(const TubeParams& p, bool engaged) const
  {
    return mHavePrev && !Changes(mPrev, p, mPrevEngaged, engaged).empty();
  }

private:
  std::FILE* mFile = nullptr;
  std::string mPath;
  int mRows = 0;
  TubeParams mPrev;
  bool mPrevEngaged = true, mHavePrev = false;
};
} // namespace debug
} // namespace tube
