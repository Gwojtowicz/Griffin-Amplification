#pragma once

// Tube Lab debug page (1.32.0): a full-window overlay with
// * a circuit map: the Tube Lab's preamp and power amp drawn as a schematic, with the live voltage
//   at each probe point (click a point to put it on scope channel A, right-click for channel B),
// * a readout: plain-English lines on what the circuit is doing and how it plays (TubeDebug.h),
// * a two-channel oscilloscope on any of the probe points (TubeProbe.h): triggered at 0.5-20 ms/div,
//   rolling at 50-200 ms/div for slow things like sag and blocking recovery,
// * a CSV log switch, and a line on the app's audio settings.
// Included from NeuralAmpModeler.cpp after NeuralAmpModelerControls.h (uses NAMOverlayPanelControl).

#include <functional>
#include <string>
#include <vector>

#include "TubeDebug.h"
#include "TubeProbe.h"

using namespace iplug;
using namespace igraphics;

static const double kTubeScopeMsPerDiv[] = {0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0};
constexpr int kTubeScopeNumTimebases = 9;
constexpr int kTubeScopeFirstRoll = 6; // From here on: untriggered, from the decimated history

namespace tubedebug
{
inline IColor ColorA() { return PluginColors::NAM_THEMECOLOR; }
inline IColor ColorB() { return IColor(255, 90, 200, 255); }
inline IColor Amber() { return IColor(255, 255, 185, 60); }
inline IColor Dim() { return PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.35f); }
inline IColor Text() { return PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.85f); }

inline std::string FormatValue(double v, const char* unit)
{
  char buf[48];
  const double a = std::fabs(v);
  if (a < 0.005)
    v = 0.0; // Not "-0.00"
  if (a >= 100.0)
    snprintf(buf, sizeof(buf), "%.0f %s", v, unit);
  else if (a >= 10.0)
    snprintf(buf, sizeof(buf), "%.1f %s", v, unit);
  else
    snprintf(buf, sizeof(buf), "%.2f %s", v, unit);
  return buf;
}

// Width of `str` in `text`.
inline float TextWidth(IGraphics& g, const IText& text, const char* str)
{
  IRECT r;
  return g.MeasureText(text, str, r);
}

// Greedy word wrap to `width`.
inline std::vector<std::string> Wrap(IGraphics& g, const IText& text, const std::string& str, float width)
{
  std::vector<std::string> lines;
  std::string line;
  size_t pos = 0;
  while (pos < str.size())
  {
    size_t next = str.find(' ', pos);
    if (next == std::string::npos)
      next = str.size();
    const std::string word = str.substr(pos, next - pos);
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && TextWidth(g, text, candidate.c_str()) > width)
    {
      lines.push_back(line);
      line = word;
    }
    else
      line = candidate;
    pos = next + 1;
  }
  if (!line.empty())
    lines.push_back(line);
  return lines;
}
} // namespace tubedebug

// Two-channel oscilloscope on the probe points.
class TubeScopeControl : public IControl
{
public:
  TubeScopeControl(const IRECT& bounds, TubeScopeSettings* settings)
  : IControl(bounds)
  , mSettings(settings)
  {
  }

  // UI thread, every idle tick while the page is open.
  void Update(const tube::TubeProbe& probe, double sampleRate)
  {
    if (sampleRate != mSampleRate)
    {
      mSampleRate = sampleRate;
      mDecimate = std::max(1, static_cast<int>(std::lround(sampleRate / 24000.0)));
      mHistFrames = static_cast<int>(2.2 * sampleRate / mDecimate);
      mHist.assign(static_cast<size_t>(mHistFrames) * tube::kNumProbes, 0.f);
      mHistPos = mHistCount = 0;
      mCursor = probe.Written();
    }
    // History for the rolling timebases: every mDecimate-th frame.
    const int n = probe.ReadSince(mCursor, mTmp);
    for (int i = 0; i < n; i++)
    {
      if (++mDecimPhase < mDecimate)
        continue;
      mDecimPhase = 0;
      std::copy_n(&mTmp[static_cast<size_t>(i) * tube::kNumProbes], tube::kNumProbes,
                  &mHist[static_cast<size_t>(mHistPos) * tube::kNumProbes]);
      mHistPos = (mHistPos + 1) % mHistFrames;
      mHistCount = std::min(mHistCount + 1, mHistFrames);
    }
    if (mSettings->freeze)
      return;
    if (mSettings->timebase < kTubeScopeFirstRoll)
    {
      const int window = _WindowFrames(mSampleRate);
      mFastCount = probe.ReadLatest(2 * window + 2, mFast);
    }
    else
    {
      // Unroll the newest window of history into mFast.
      const int window = std::min(_WindowFrames(mSampleRate / mDecimate), mHistCount);
      mFast.resize(static_cast<size_t>(window) * tube::kNumProbes);
      for (int i = 0; i < window; i++)
      {
        const int src = (mHistPos - window + i + mHistFrames) % mHistFrames;
        std::copy_n(&mHist[static_cast<size_t>(src) * tube::kNumProbes], tube::kNumProbes,
                    &mFast[static_cast<size_t>(i) * tube::kNumProbes]);
      }
      mFastCount = window;
    }
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    using namespace tubedebug;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.35f), mRECT, 4.f);
    _LayoutHeader();
    const IText headerText(11.f, Text(), "Roboto-Regular", EAlign::Near, EVAlign::Middle);
    auto pill = [&](const IRECT& r, const std::string& s, IColor c, bool lit = false) {
      g.FillRoundRect(lit ? c.WithOpacity(0.35f) : COLOR_BLACK.WithOpacity(0.3f), r, 3.f);
      g.DrawRoundRect(c.WithOpacity(0.6f), r, 3.f);
      IText t = headerText;
      t.mFGColor = c;
      g.DrawText(t, s.c_str(), r.GetPadded(-5.f, 0.f, -2.f, 0.f));
    };
    pill(mAButton, std::string("A: ") + tube::GetProbeInfo(mSettings->chA).name + "  v", ColorA());
    pill(mBButton,
         std::string("B: ") + (mSettings->chB < 0 ? "off" : tube::GetProbeInfo(mSettings->chB).name) + "  v", ColorB());
    char tb[48];
    snprintf(tb, sizeof(tb), "%g ms/div%s  v", kTubeScopeMsPerDiv[mSettings->timebase],
             mSettings->timebase >= kTubeScopeFirstRoll ? " (roll)" : "");
    pill(mTimeButton, tb, Text());
    pill(mFreezeButton, mSettings->freeze ? "FROZEN" : "FREEZE", mSettings->freeze ? Amber() : Text(),
         mSettings->freeze);

    // Graticule: 10 x 8 divisions.
    const IRECT plot = mPlot;
    const IColor gridColor = PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.08f);
    for (int i = 0; i <= 10; i++)
    {
      const float x = plot.L + plot.W() * i / 10.f;
      g.DrawLine(i == 5 ? gridColor.WithOpacity(0.18f) : gridColor, x, plot.T, x, plot.B);
    }
    for (int i = 0; i <= 8; i++)
    {
      const float y = plot.T + plot.H() * i / 8.f;
      g.DrawLine(i == 4 ? gridColor.WithOpacity(0.18f) : gridColor, plot.L, y, plot.R, y);
    }

    const bool roll = mSettings->timebase >= kTubeScopeFirstRoll;
    const double rate = roll ? mSampleRate / mDecimate : mSampleRate;
    const int window = std::min(_WindowFrames(rate), mFastCount);
    if (window < 2)
    {
      const IText waiting(13.f, Dim(), "Roboto-Regular");
      g.DrawText(waiting, "No probe data yet: play something with the Tube Lab in the chain.", plot);
      return;
    }
    // Trigger: the newest rising crossing of channel A's midpoint that still leaves a full window
    // after it (triggered timebases only), so a steady note stands still.
    int start = mFastCount - window;
    if (!roll && mFastCount > window + 1)
    {
      float lo = 1e30f, hi = -1e30f;
      for (int i = mFastCount - window; i < mFastCount; i++)
      {
        const float v = _At(i, mSettings->chA);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      const float level = 0.5f * (lo + hi);
      const float hyst = 0.1f * (hi - lo);
      for (int i = mFastCount - window; i > 0; i--)
        if (_At(i - 1, mSettings->chA) < level && _At(i, mSettings->chA) >= level
            && _At(std::max(0, i - 8), mSettings->chA) < level - hyst)
        {
          start = i;
          break;
        }
    }
    _Trace(g, plot, start, window, mSettings->chB, ColorB(), mScaleB, false);
    _Trace(g, plot, start, window, mSettings->chA, ColorA(), mScaleA, true);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    _LayoutHeader();
    mMenu.Clear();
    if (mAButton.Contains(x, y) || mBButton.Contains(x, y))
    {
      mMenuTarget = mAButton.Contains(x, y) ? 0 : 1;
      if (mMenuTarget == 1)
        mMenu.AddItem("Off");
      for (int c = 0; c < tube::kNumProbes; c++)
      {
        const auto& info = tube::GetProbeInfo(c);
        mMenu.AddItem((std::string(info.name) + " (" + info.unit + ")").c_str());
      }
      GetUI()->CreatePopupMenu(*this, mMenu, mMenuTarget == 0 ? mAButton : mBButton);
    }
    else if (mTimeButton.Contains(x, y))
    {
      mMenuTarget = 2;
      for (int i = 0; i < kTubeScopeNumTimebases; i++)
      {
        char s[48];
        snprintf(s, sizeof(s), "%g ms/div%s", kTubeScopeMsPerDiv[i], i >= kTubeScopeFirstRoll ? " (roll)" : "");
        mMenu.AddItem(s);
      }
      GetUI()->CreatePopupMenu(*this, mMenu, mTimeButton);
    }
    else if (mFreezeButton.Contains(x, y))
    {
      mSettings->freeze = !mSettings->freeze;
      SetDirty(false);
    }
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (!pSelectedMenu)
      return;
    const int idx = pSelectedMenu->GetChosenItemIdx();
    if (idx < 0)
      return;
    if (mMenuTarget == 0)
      mSettings->chA = std::clamp(idx, 0, tube::kNumProbes - 1);
    else if (mMenuTarget == 1)
      mSettings->chB = idx == 0 ? -1 : std::clamp(idx - 1, 0, tube::kNumProbes - 1);
    else if (mMenuTarget == 2)
      mSettings->timebase = std::clamp(idx, 0, kTubeScopeNumTimebases - 1);
    mScaleA.valid = mScaleB.valid = false;
    SetDirty(false);
  }

  // A probe point picked on the circuit map.
  void SetChannel(int probe, bool channelB)
  {
    (channelB ? mSettings->chB : mSettings->chA) = probe;
    (channelB ? mScaleB : mScaleA).valid = false;
    SetDirty(false);
  }

private:
  struct Scale
  {
    bool valid = false;
    float lo = 0.f, hi = 1.f;
  };

  int _WindowFrames(double rate) const
  {
    return std::max(2, static_cast<int>(kTubeScopeMsPerDiv[mSettings->timebase] * 10.0 * 1e-3 * rate));
  }

  float _At(int frame, int channel) const { return mFast[static_cast<size_t>(frame) * tube::kNumProbes + channel]; }

  void _LayoutHeader()
  {
    const IRECT header = mRECT.GetFromTop(24.f).GetPadded(-4.f);
    const float w = header.W();
    mAButton = header.GetFromLeft(w * 0.3f).GetPadded(-1.f);
    mBButton = header.GetReducedFromLeft(w * 0.3f).GetFromLeft(w * 0.3f).GetPadded(-1.f);
    mTimeButton = header.GetReducedFromLeft(w * 0.6f).GetFromLeft(w * 0.24f).GetPadded(-1.f);
    mFreezeButton = header.GetFromRight(w * 0.16f).GetPadded(-1.f);
    mPlot = mRECT.GetReducedFromTop(26.f).GetPadded(-46.f, -4.f, -46.f, -6.f);
  }

  // One channel's trace, auto-scaled (it widens at once and narrows slowly, so it doesn't jump),
  // with its range labelled on its own side.
  void _Trace(IGraphics& g, const IRECT& plot, int start, int window, int channel, const IColor& color, Scale& scale,
              bool leftLabels)
  {
    if (channel < 0)
      return;
    float lo = 1e30f, hi = -1e30f;
    for (int i = start; i < start + window; i++)
    {
      const float v = _At(i, channel);
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    const auto& info = tube::GetProbeInfo(channel);
    const float minSpan = std::string(info.unit) == "FS" ? 0.02f : 0.1f;
    if (hi - lo < minSpan)
    {
      const float mid = 0.5f * (lo + hi);
      lo = mid - 0.5f * minSpan;
      hi = mid + 0.5f * minSpan;
    }
    const float pad = 0.08f * (hi - lo);
    lo -= pad;
    hi += pad;
    if (!scale.valid || lo < scale.lo || hi > scale.hi)
    {
      scale.lo = scale.valid ? std::min(lo, scale.lo) : lo;
      scale.hi = scale.valid ? std::max(hi, scale.hi) : hi;
      scale.valid = true;
    }
    else
    {
      scale.lo += 0.05f * (lo - scale.lo);
      scale.hi += 0.05f * (hi - scale.hi);
    }
    auto yOf = [&](float v) { return plot.B - (v - scale.lo) / (scale.hi - scale.lo) * plot.H(); };

    // One vertical min-max line per pixel column when there are more samples than pixels, else a
    // polyline through the samples.
    const int cols = std::max(1, static_cast<int>(plot.W()));
    float prevX = 0.f, prevY = 0.f;
    if (window > 2 * cols)
    {
      for (int c = 0; c < cols; c++)
      {
        const int i0 = start + static_cast<int>(static_cast<int64_t>(window) * c / cols);
        const int i1 = std::max(i0 + 1, start + static_cast<int>(static_cast<int64_t>(window) * (c + 1) / cols));
        float mn = 1e30f, mx = -1e30f;
        for (int i = i0; i < i1; i++)
        {
          const float v = _At(i, channel);
          mn = std::min(mn, v);
          mx = std::max(mx, v);
        }
        const float x = plot.L + c + 0.5f;
        g.DrawLine(color, x, yOf(mx), x, std::max(yOf(mn), yOf(mx) + 1.f), nullptr, 1.2f);
        const float y = yOf(0.5f * (mn + mx));
        if (c > 0)
          g.DrawLine(color.WithOpacity(0.5f), prevX, prevY, x, y);
        prevX = x;
        prevY = y;
      }
    }
    else
    {
      for (int i = 0; i < window; i++)
      {
        const float x = plot.L + plot.W() * i / (window - 1);
        const float y = yOf(_At(start + i, channel));
        if (i > 0)
          g.DrawLine(color, prevX, prevY, x, y, nullptr, 1.5f);
        prevX = x;
        prevY = y;
      }
    }
    const IText labelText(10.f, color, "Roboto-Regular", leftLabels ? EAlign::Far : EAlign::Near, EVAlign::Middle);
    const float lx = leftLabels ? plot.L - 46.f : plot.R + 3.f;
    const IRECT topLabel(lx, plot.T - 6.f, lx + 43.f, plot.T + 6.f);
    const IRECT midLabel(lx, plot.MH() - 6.f, lx + 43.f, plot.MH() + 6.f);
    const IRECT botLabel(lx, plot.B - 6.f, lx + 43.f, plot.B + 6.f);
    g.DrawText(labelText, tubedebug::FormatValue(scale.hi, info.unit).c_str(), topLabel);
    g.DrawText(labelText, tubedebug::FormatValue(0.5 * (scale.lo + scale.hi), info.unit).c_str(), midLabel);
    g.DrawText(labelText, tubedebug::FormatValue(scale.lo, info.unit).c_str(), botLabel);
  }

  TubeScopeSettings* mSettings;
  double mSampleRate = 0.0;
  int mDecimate = 1, mDecimPhase = 0;
  std::vector<float> mHist, mTmp, mFast;
  int mHistFrames = 1, mHistPos = 0, mHistCount = 0, mFastCount = 0;
  int64_t mCursor = 0;
  Scale mScaleA, mScaleB;
  IRECT mAButton, mBButton, mTimeButton, mFreezeButton, mPlot;
  IPopupMenu mMenu;
  int mMenuTarget = 0;
};

// Where the circuit map's parts sit, as fractions of its drawing area (laid out so that no value
// label crosses a wire, a part or another label at the page's size).
namespace tubemap
{
constexpr float kRailY = 0.07f, kPlateY = 0.34f, kTubeY = 0.60f, kCathodeY = 0.82f, kCathodeB = 0.95f;
constexpr float kInX = 0.02f, kInY = 0.47f, kInCapX = 0.06f, kLeakX = 0.115f, kLeakB = 0.74f, kVgk1X = 0.155f;
constexpr float kV1X = 0.21f, kStageCapDX = 0.035f;
constexpr float kRailX = 0.15f;
constexpr float kInterCapX = 0.265f, kPotX = 0.305f, kPotB = 0.68f, kVgk2X = 0.34f, kV2X = 0.395f;
constexpr float kStackX0 = 0.495f, kStackX1 = 0.565f, kStackOutX = 0.59f;
constexpr float kPIX0 = 0.625f, kPIX1 = 0.655f, kPIY0 = 0.28f, kPIY1 = 0.72f, kPIOutX = 0.675f;
constexpr float kPowerX = 0.81f, kPowerTopY = 0.20f, kPowerBotY = 0.80f, kGrid1X = 0.735f, kPlateNodeX = 0.855f;
constexpr float kMidY = 0.50f, kPowerRailX = 0.868f, kOTX = 0.90f, kSecondaryX = 0.945f, kSecondaryY0 = 0.32f,
                kSecondaryY1 = 0.68f, kSpeakerX = 0.968f;
} // namespace tubemap

// The Tube Lab drawn as a schematic, with the live value at each probe point.
class TubeCircuitMapControl : public IControl
{
public:
  TubeCircuitMapControl(const IRECT& bounds, std::function<void(int, bool)> onPick)
  : IControl(bounds)
  , mOnPick(std::move(onPick))
  {
    std::fill(std::begin(mMean), std::end(mMean), 0.f);
    std::fill(std::begin(mMin), std::end(mMin), 0.f);
    std::fill(std::begin(mMax), std::end(mMax), 0.f);
  }

  void SetSections(bool circuit, bool stage2, int toneStack, bool powerAmp, bool sixL6)
  {
    if (circuit != mCircuit || stage2 != mStage2 || toneStack != mToneStack || powerAmp != mPowerAmp || sixL6 != mSixL6)
    {
      mCircuit = circuit;
      mStage2 = stage2;
      mToneStack = toneStack;
      mPowerAmp = powerAmp;
      mSixL6 = sixL6;
      SetDirty(false);
    }
  }

  // Mean, min and max of each probe over the newest frames.
  void SetLive(const std::vector<float>& frames, int count)
  {
    mHaveData = count > 0;
    for (int c = 0; c < tube::kNumProbes; c++)
    {
      double sum = 0.0;
      float mn = 1e30f, mx = -1e30f;
      for (int i = 0; i < count; i++)
      {
        const float v = frames[static_cast<size_t>(i) * tube::kNumProbes + c];
        sum += v;
        mn = std::min(mn, v);
        mx = std::max(mx, v);
      }
      mMean[c] = count > 0 ? static_cast<float>(sum / count) : 0.f;
      mMin[c] = count > 0 ? mn : 0.f;
      mMax[c] = count > 0 ? mx : 0.f;
    }
    SetDirty(false);
  }

  // Which parts are clipping in the latest statistics window: their probe points light up.
  void SetActivity(const tube::ProbeStats& s)
  {
    for (int i = 0; i < 2; i++)
    {
      mConducting[i] = s.Percent(s.gridConducting[i], s.samples) > 0.1;
      mCutoff[i] = s.Percent(s.cutoff[i], s.samples) > 0.1;
    }
    mPAConducting = s.Percent(s.paGridConducting, s.paSamples) > 0.1;
    mPACutoff = s.Percent(s.paCutoff, s.paSamples) > 1.0;
    SetDirty(false);
  }

  void SetSelected(int a, int b)
  {
    mSelA = a;
    mSelB = b;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    using namespace tubedebug;
    using namespace tubemap;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.25f), mRECT, 4.f);
    mArea = mRECT.GetPadded(-8.f).GetReducedFromBottom(16.f);
    mR = std::clamp(std::min(mArea.W() * 0.045f, mArea.H() * 0.11f), 8.f, 17.f);
    const IColor on = PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.55f);
    const IColor off = PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.15f);
    const IColor pre = mCircuit ? on : off;
    const IColor v2c = mCircuit && mStage2 ? on : off;
    const IColor stackc = mCircuit && mToneStack > 0 ? on : off;
    const IColor pac = mCircuit && mPowerAmp ? on : off;

    // Preamp B+ rail.
    _Line(g, pre, kRailX, kRailY, mStage2 ? kV2X : kV1X, kRailY);
    // Input, coupling cap, grid leak, into V1.
    _Line(g, pre, kInX, kInY, kInX, kTubeY);
    _Line(g, pre, kInX, kTubeY, kInCapX, kTubeY);
    _CapH(g, pre, kInCapX, kTubeY);
    _Line(g, pre, kInCapX, kTubeY, kV1X - _Rx(), kTubeY);
    _ResV(g, pre, kLeakX, kTubeY, kLeakB);
    _Ground(g, pre, kLeakX, kLeakB);
    _Stage(g, pre, kV1X, "V1");
    // V1 plate to V2 through the interstage cap and Gain 2 (or straight on to the tone stack).
    _Line(g, v2c, kV1X, kPlateY, kPotX, kPlateY);
    _CapH(g, v2c, kInterCapX, kPlateY);
    _ResV(g, v2c, kPotX, kPlateY, kPotB);
    _Ground(g, v2c, kPotX, kPotB);
    _Line(g, v2c, kPotX + 0.012f, kTubeY, kV2X - _Rx(), kTubeY);
    _Arrow(g, v2c, kPotX + 0.012f, kTubeY);
    _Stage(g, v2c, kV2X, "V2");
    // Tone stack.
    _Line(g, pre, mStage2 ? kV2X : kV1X, kPlateY, kStackX0, kPlateY);
    _Box(g, stackc, kStackX0, kPlateY - 0.06f, kStackX1, kPlateY + 0.06f,
         mToneStack == 1 ? "FENDER" : mToneStack == 2 ? "MARSHALL" : "NO STACK");
    if (mToneStack == 0)
      _Line(g, pre, kStackX0, kPlateY, kStackX1, kPlateY);
    _Line(g, pre, kStackX1, kPlateY, kPIX0, kPlateY);

    // Power amp: phase inverter, two output tubes, output transformer, speaker.
    _Box(g, pac, kPIX0, kPIY0, kPIX1, kPIY1, "PI");
    for (const float y : {kPowerTopY, kPowerBotY})
    {
      const float fromY = y < kMidY ? kPlateY : 1.f - kPlateY;
      _Line(g, pac, kPIX1, fromY, kPIOutX, fromY);
      _Line(g, pac, kPIOutX, fromY, kPIOutX, y);
      _Line(g, pac, kPIOutX, y, kPowerX - _Rx(), y);
      _Tube(g, pac, y, mSixL6 ? "6L6" : "EL34");
      // Plate to the transformer primary.
      g.DrawLine(pac, _X(kPowerX) + mR * 0.45f, _Y(y), _X(kOTX), _Y(y), nullptr, 1.2f);
    }
    // Screens: both fed from the power B+ (the primary's center tap) through the screen resistor.
    const float sx = _ScreenX();
    g.DrawLine(pac, sx, _Y(kPowerTopY) + mR * 0.98f, sx, _Y(kPowerBotY) - mR * 0.98f, nullptr, 1.2f);
    _ResH(g, pac, (sx - mArea.L) / std::max(1.f, mArea.W()), kPowerRailX, kMidY);
    _Line(g, pac, kPowerRailX, kMidY, kOTX, kMidY);
    _Coil(g, pac, kOTX, kPowerTopY, kPowerBotY, true);
    _Coil(g, pac, kSecondaryX, kSecondaryY0, kSecondaryY1, false);
    _Speaker(g, pac);
    g.DrawText(IText(9.f, pac, "Roboto-Regular"), "OT", _Rect(kOTX, 0.22f, kSpeakerX - kOTX + 0.02f, 0.08f));
    if (!mPowerAmp || !mCircuit)
    {
      // Power amp off: the preamp goes straight to the output.
      _Line(g, pre, kPIX0, kPlateY, kPIOutX, kPlateY);
      _Line(g, pre, kPIOutX, kPlateY, kPIOutX, 0.02f);
      _Line(g, pre, kPIOutX, 0.02f, 0.99f, 0.02f);
      _Line(g, pre, 0.99f, 0.02f, 0.99f, kSecondaryY1);
      _Line(g, pre, 0.99f, kSecondaryY1, kSecondaryX, kSecondaryY1);
    }

    if (!mCircuit)
    {
      const IText msg(13.f, Text(), "Roboto-Regular");
      g.DrawText(msg, "Classic engine: no circuit to probe. Turn Circuit on.", mArea);
    }

    // Probe points.
    for (int n = 0; n < kNumNodes; n++)
    {
      const Node& node = kNodes[n];
      const bool active = _Active(node.section);
      float x, y;
      _NodePos(node, x, y);
      IColor dot = active ? on.WithOpacity(0.9f) : off;
      if (active && _Lit(node.probe))
        dot = Amber();
      if (active && node.dy != 0.f)
      {
        // Leader line down (or up) to a label placed away from the crowd.
        const float ly = y + node.dy * mArea.H();
        g.DrawLine(Dim(), x, y, x, node.dy > 0.f ? ly - 6.f : ly + 6.f);
      }
      g.FillCircle(dot, x, y, 3.5f);
      if (node.probe == mSelA)
        g.DrawCircle(tubedebug::ColorA(), x, y, 6.f, nullptr, 2.f);
      if (node.probe == mSelB)
        g.DrawCircle(tubedebug::ColorB(), x, y, node.probe == mSelA ? 8.5f : 6.f, nullptr, 2.f);
      if (node.probe == mHoverProbe)
        g.DrawCircle(Text(), x, y, 10.f);
      if (!active)
        continue;
      const std::string label = mHaveData ? std::string(node.label) + " " + _Value(node) : std::string(node.label);
      const IText t(9.5f, _Lit(node.probe) ? Amber() : Text(), "Roboto-Regular", _LabelAlign(node), EVAlign::Middle);
      g.DrawText(t, label.c_str(), _LabelRect(node, x, y));
    }

    // Status line: the hovered point, or how to use the map.
    const IRECT status = mRECT.GetFromBottom(16.f).GetPadded(-8.f, 0.f, -8.f, -2.f);
    const IText statusText(10.f, Dim().WithOpacity(0.7f), "Roboto-Regular", EAlign::Near, EVAlign::Middle);
    std::string s = "Click a point: scope A.  Right-click: scope B.  Amber = clipping there right now.  \xC2\xB1 = swing.";
    if (mHoverProbe >= 0)
    {
      const auto& info = tube::GetProbeInfo(mHoverProbe);
      s = std::string(info.name) + ": " + FormatValue(mMean[mHoverProbe], info.unit) + " average, "
          + FormatValue(mMin[mHoverProbe], info.unit) + " to " + FormatValue(mMax[mHoverProbe], info.unit);
    }
    g.DrawText(statusText, s.c_str(), status);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int probe = _Hit(x, y);
    if (probe >= 0 && mOnPick)
      mOnPick(probe, mod.R || mod.S);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const int probe = _Hit(x, y);
    if (probe != mHoverProbe)
    {
      mHoverProbe = probe;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    mHoverProbe = -1;
    SetDirty(false);
  }

private:
  enum Section
  {
    kPre,
    kV2,
    kStack,
    kPower
  };
  // What a point's label shows. A plain average hides the guitar signal (it averages to about 0),
  // so signal points show their peak, and points that sit at a DC level show the level plus how far
  // the signal swings it.
  enum Show
  {
    kLevel, // Average only (supply rails, blocking: they move slowly)
    kSwing, // Average, plus +- half the min-to-max swing when it's big enough to matter
    kPeak // +- the peak (audio signals, centered on 0)
  };
  struct Node
  {
    int probe;
    float x, y;
    const char* label;
    // Label placement: 'l'eft, 'r'ight, 'a'bove, 'b'elow (centered), 'A'bove or 'B'elow starting at the
    // point and running right, 'E': above, ending at the point, 'W': below, ending at the point.
    char side;
    Section section;
    Show show;
    // Non-zero dy: the label sits this far away (fraction of the area) on a leader line, centered
    // dx to the side ('W': its right end there).
    float dx = 0.f, dy = 0.f;
  };
  static constexpr int kNumNodes = tube::kNumProbes;
  static constexpr Node kNodes[kNumNodes] = {
    {tube::kProbeInput, tubemap::kInX, tubemap::kInY, "IN", 'A', kPre, kPeak},
    {tube::kProbeV1Block, tubemap::kInCapX, tubemap::kTubeY, "blk", 'b', kPre, kLevel},
    {tube::kProbeV1Vgk, tubemap::kVgk1X, tubemap::kTubeY, "Vgk", 'E', kPre, kSwing},
    {tube::kProbeV1Plate, tubemap::kV1X, tubemap::kPlateY, "P", 'l', kPre, kSwing},
    {tube::kProbeV1Cathode, tubemap::kV1X, tubemap::kCathodeY, "K", 'l', kPre, kSwing},
    {tube::kProbeV1Current, tubemap::kV1X, 0.20f, "I", 'l', kPre, kSwing},
    {tube::kProbePreRail, tubemap::kRailX, tubemap::kRailY, "B+", 'l', kPre, kLevel},
    {tube::kProbeV2Block, tubemap::kInterCapX, tubemap::kPlateY, "blk", 'a', kV2, kLevel},
    {tube::kProbeV2Vgk, tubemap::kVgk2X, tubemap::kTubeY, "Vgk", 'b', kV2, kSwing, -0.04f, 0.145f},
    {tube::kProbeV2Plate, tubemap::kV2X, tubemap::kPlateY, "P", 'A', kV2, kSwing},
    {tube::kProbeStackOut, tubemap::kStackOutX, tubemap::kPlateY, "out", 'a', kPre, kPeak, 0.f, -0.09f},
    {tube::kProbePAGrid1, tubemap::kGrid1X, tubemap::kPowerTopY, "G1", 'E', kPower, kSwing},
    {tube::kProbePAGrid2, tubemap::kGrid1X, tubemap::kPowerBotY, "G1", 'W', kPower, kSwing},
    {tube::kProbePAPlate1, tubemap::kPlateNodeX, tubemap::kPowerTopY, "P", 'A', kPower, kSwing},
    {tube::kProbePAPlate2, tubemap::kPlateNodeX, tubemap::kPowerBotY, "P", 'A', kPower, kSwing},
    {tube::kProbePACurrent1, tubemap::kPowerX, tubemap::kPowerTopY, "I", 'l', kPower, kSwing}, // x, y: see _NodePos
    {tube::kProbePACurrent2, tubemap::kPowerX, tubemap::kPowerBotY, "I", 'r', kPower, kSwing},
    {tube::kProbePARail, tubemap::kPowerRailX, tubemap::kMidY, "B+", 'a', kPower, kLevel, -0.008f},
    {tube::kProbePAScreen, tubemap::kPowerX, tubemap::kMidY, "G2", 'l', kPower, kLevel}, // x: see _NodePos
    {tube::kProbeOutput, tubemap::kSecondaryX, tubemap::kSecondaryY1, "OUT", 'W', kPre, kPeak, 0.055f, 0.29f},
  };

  std::string _Value(const Node& node) const
  {
    const int p = node.probe;
    const char* unit = tube::GetProbeInfo(p).unit;
    if (node.show == kPeak)
      return "\xC2\xB1" + tubedebug::FormatValue(std::max(std::fabs(mMin[p]), std::fabs(mMax[p])), unit);
    const double half = 0.5 * (mMax[p] - mMin[p]);
    if (node.show == kLevel || half < std::max(0.02, 0.03 * std::fabs(mMean[p])))
      return tubedebug::FormatValue(mMean[p], unit);
    // "-1.54 +-2.4 V": the swing to two figures, so the label stays short.
    char swing[24];
    snprintf(swing, sizeof(swing), half >= 10.0 ? "%.0f" : half >= 1.0 ? "%.1f" : "%.2f", half);
    std::string mean = tubedebug::FormatValue(mMean[p], unit);
    mean.resize(mean.size() - std::string(unit).size() - 1); // Drop the unit; it goes after the swing
    return mean + " \xC2\xB1" + swing + " " + unit;
  }

  EAlign _LabelAlign(const Node& node) const
  {
    switch (node.side)
    {
      case 'l':
      case 'E':
      case 'W': return EAlign::Far;
      case 'r':
      case 'A':
      case 'B': return EAlign::Near;
      default: return EAlign::Center;
    }
  }

  IRECT _LabelRect(const Node& node, float x, float y) const
  {
    constexpr float w = 84.f, h = 11.f;
    if (node.dy != 0.f)
    {
      const float lx = x + node.dx * mArea.W(), ly = y + node.dy * mArea.H();
      return node.side == 'W' ? IRECT(lx - w, ly - h / 2, lx, ly + h / 2)
                              : IRECT(lx - w / 2, ly - h / 2, lx + w / 2, ly + h / 2);
    }
    const float lx = x + node.dx * mArea.W();
    switch (node.side)
    {
      case 'l': return IRECT(lx - 6.f - w, y - h / 2, lx - 6.f, y + h / 2);
      case 'r': return IRECT(lx + 6.f, y - h / 2, lx + 6.f + w, y + h / 2);
      case 'a': return IRECT(lx - w / 2, y - 6.f - h, lx + w / 2, y - 6.f);
      case 'A': return IRECT(lx + 5.f, y - 6.f - h, lx + 5.f + w, y - 6.f); // Clear of a wire going up
      case 'B': return IRECT(lx - 4.f, y + 6.f, lx - 4.f + w, y + 6.f + h);
      case 'E': return IRECT(lx + 4.f - w, y - 8.f - h, lx + 4.f, y - 8.f);
      case 'W': return IRECT(lx + 4.f - w, y + 6.f, lx + 4.f, y + 6.f + h);
      default: return IRECT(lx - w / 2, y + 6.f, lx + w / 2, y + 6.f + h);
    }
  }

  bool _Active(Section s) const
  {
    if (!mCircuit)
      return false;
    switch (s)
    {
      case kV2: return mStage2;
      case kStack: return mToneStack > 0;
      case kPower: return mPowerAmp;
      default: return true;
    }
  }
  bool _Lit(int probe) const
  {
    switch (probe)
    {
      case tube::kProbeV1Vgk: return mConducting[0];
      case tube::kProbeV1Plate:
      case tube::kProbeV1Current: return mCutoff[0];
      case tube::kProbeV2Vgk: return mConducting[1];
      case tube::kProbeV2Plate: return mCutoff[1];
      case tube::kProbePAGrid1:
      case tube::kProbePAGrid2: return mPAConducting;
      case tube::kProbePACurrent1:
      case tube::kProbePACurrent2: return mPACutoff;
      default: return false;
    }
  }
  int _Hit(float x, float y) const
  {
    int best = -1;
    float bestD = 12.f * 12.f;
    for (const Node& n : kNodes)
    {
      if (!_Active(n.section))
        continue;
      float nx, ny;
      _NodePos(n, nx, ny);
      const float dx = x - nx, dy = y - ny, d = dx * dx + dy * dy;
      if (d < bestD)
      {
        bestD = d;
        best = n.probe;
      }
    }
    return best;
  }

  // A node's position in pixels. Some sit on wires whose place depends on the tube symbol's size:
  // the power tubes' current probes on their cathode leads, the screens on the screen feed.
  void _NodePos(const Node& n, float& x, float& y) const
  {
    x = _X(n.x);
    y = _Y(n.y);
    if (n.probe == tube::kProbePACurrent1 || n.probe == tube::kProbePACurrent2)
    {
      x = _CathodeX();
      y = _Y(n.y) + mR * 0.94f + 3.f;
    }
    else if (n.probe == tube::kProbePAScreen)
      x = _ScreenX();
  }
  float _X(float nx) const { return mArea.L + nx * mArea.W(); }
  float _Y(float ny) const { return mArea.T + ny * mArea.H(); }
  float _Rx() const { return mR / std::max(1.f, mArea.W()); } // Tube radius in normalized x
  float _CathodeX() const { return _X(tubemap::kPowerX) - mR * 0.35f; } // Power tubes' cathode leads
  float _ScreenX() const { return _X(tubemap::kPowerX) + mR * 0.2f; } // Power tubes' screen feed
  IRECT _Rect(float nx, float ny, float nw, float nh) const
  {
    return IRECT(_X(nx), _Y(ny), _X(nx + nw), _Y(ny + nh));
  }
  void _Line(IGraphics& g, const IColor& c, float x0, float y0, float x1, float y1) const
  {
    g.DrawLine(c, _X(x0), _Y(y0), _X(x1), _Y(y1), nullptr, 1.2f);
  }
  // A resistor (box) along a vertical wire from y0 to y1.
  void _ResV(IGraphics& g, const IColor& c, float x, float y0, float y1) const
  {
    const float len = _Y(y1) - _Y(y0);
    const IRECT body(_X(x) - 3.5f, _Y(y0) + len * 0.2f, _X(x) + 3.5f, _Y(y1) - len * 0.2f);
    g.DrawLine(c, _X(x), _Y(y0), _X(x), body.T, nullptr, 1.2f);
    g.DrawLine(c, _X(x), body.B, _X(x), _Y(y1), nullptr, 1.2f);
    g.DrawRect(c, body, nullptr, 1.2f);
  }
  // A resistor along a horizontal wire from x0 to x1.
  void _ResH(IGraphics& g, const IColor& c, float x0, float x1, float y) const
  {
    const float len = _X(x1) - _X(x0);
    const IRECT body(_X(x0) + len * 0.2f, _Y(y) - 3.5f, _X(x1) - len * 0.2f, _Y(y) + 3.5f);
    g.DrawLine(c, _X(x0), _Y(y), body.L, _Y(y), nullptr, 1.2f);
    g.DrawLine(c, body.R, _Y(y), _X(x1), _Y(y), nullptr, 1.2f);
    g.DrawRect(c, body, nullptr, 1.2f);
  }
  // A capacitor in a horizontal wire, centered at (x, y).
  void _CapH(IGraphics& g, const IColor& c, float x, float y) const
  {
    const float cx = _X(x), cy = _Y(y);
    g.FillRect(IColor(255, 15, 14, 14), IRECT(cx - 2.f, cy - 6.f, cx + 2.f, cy + 6.f)); // Break the wire (page background)
    g.DrawLine(c, cx - 2.5f, cy - 6.f, cx - 2.5f, cy + 6.f, nullptr, 1.5f);
    g.DrawLine(c, cx + 2.5f, cy - 6.f, cx + 2.5f, cy + 6.f, nullptr, 1.5f);
  }
  void _CapV(IGraphics& g, const IColor& c, float x, float y) const
  {
    const float cx = _X(x), cy = _Y(y);
    g.DrawLine(c, cx - 6.f, cy - 2.5f, cx + 6.f, cy - 2.5f, nullptr, 1.5f);
    g.DrawLine(c, cx - 6.f, cy + 2.5f, cx + 6.f, cy + 2.5f, nullptr, 1.5f);
  }
  void _Ground(IGraphics& g, const IColor& c, float x, float y) const { _GroundAt(g, c, _X(x), _Y(y)); }
  void _GroundAt(IGraphics& g, const IColor& c, float cx, float cy) const
  {
    g.DrawLine(c, cx - 6.f, cy, cx + 6.f, cy, nullptr, 1.2f);
    g.DrawLine(c, cx - 4.f, cy + 3.f, cx + 4.f, cy + 3.f, nullptr, 1.2f);
    g.DrawLine(c, cx - 2.f, cy + 6.f, cx + 2.f, cy + 6.f, nullptr, 1.2f);
  }
  void _Arrow(IGraphics& g, const IColor& c, float x, float y) const
  {
    const float cx = _X(x) - 4.f, cy = _Y(y);
    g.DrawLine(c, cx, cy, cx + 6.f, cy - 4.f, nullptr, 1.2f);
    g.DrawLine(c, cx, cy, cx + 6.f, cy + 4.f, nullptr, 1.2f);
  }
  void _Box(IGraphics& g, const IColor& c, float x0, float y0, float x1, float y1, const char* label) const
  {
    const IRECT r(_X(x0), _Y(y0), _X(x1), _Y(y1));
    g.DrawRoundRect(c, r, 3.f, nullptr, 1.2f);
    g.DrawText(IText(9.f, c, "Roboto-Regular"), label, r);
  }
  // A transformer winding: five loops from y0 to y1, bulging right (or left).
  void _Coil(IGraphics& g, const IColor& c, float x, float y0, float y1, bool right) const
  {
    const float cx = _X(x), top = _Y(y0), bot = _Y(y1);
    const int loops = 5;
    const float h = (bot - top) / loops;
    for (int i = 0; i < loops; i++)
      g.DrawArc(c, cx, top + h * (i + 0.5f), h * 0.5f, right ? 0.f : 180.f, right ? 180.f : 360.f, nullptr, 1.2f);
  }
  // The speaker, fed from both ends of the secondary.
  void _Speaker(IGraphics& g, const IColor& c) const
  {
    const float cx = _X(tubemap::kSpeakerX), cy = _Y(tubemap::kMidY);
    const IRECT magnet(cx - 4.f, cy - 7.f, cx + 1.f, cy + 7.f);
    g.DrawRect(c, magnet, nullptr, 1.2f);
    g.DrawLine(c, cx + 1.f, cy - 7.f, cx + 9.f, cy - 14.f, nullptr, 1.2f);
    g.DrawLine(c, cx + 1.f, cy + 7.f, cx + 9.f, cy + 14.f, nullptr, 1.2f);
    g.DrawLine(c, cx + 9.f, cy - 14.f, cx + 9.f, cy + 14.f, nullptr, 1.2f);
    g.DrawLine(c, _X(tubemap::kSecondaryX), _Y(tubemap::kSecondaryY0), cx - 4.f, cy - 3.f, nullptr, 1.2f);
    g.DrawLine(c, _X(tubemap::kSecondaryX), _Y(tubemap::kSecondaryY1), cx - 4.f, cy + 3.f, nullptr, 1.2f);
  }
  // A triode: circle, grid (dashed) from the left, plate on top, cathode below.
  void _TriodeSymbol(IGraphics& g, const IColor& c, float cx, float cy, const char* name) const
  {
    g.DrawCircle(c, cx, cy, mR, nullptr, 1.2f);
    g.DrawDottedLine(c, cx - mR * 0.6f, cy, cx + mR * 0.6f, cy);
    g.DrawLine(c, cx - mR * 0.5f, cy - mR * 0.55f, cx + mR * 0.5f, cy - mR * 0.55f, nullptr, 2.f);
    g.DrawLine(c, cx - mR * 0.5f, cy + mR * 0.55f, cx + mR * 0.5f, cy + mR * 0.55f, nullptr, 1.5f);
    g.DrawText(IText(9.f, c, "Roboto-Regular", EAlign::Near), name, IRECT(cx + mR + 2.f, cy - 5.f, cx + mR + 30.f, cy + 5.f));
  }
  // One preamp stage at normalized x: plate load up from the plate wire to the rail, Rk || Ck from
  // the cathode down to ground.
  void _Stage(IGraphics& g, const IColor& c, float x, const char* name) const
  {
    using namespace tubemap;
    const float cx = _X(x), cy = _Y(kTubeY);
    _TriodeSymbol(g, c, cx, cy, name);
    g.DrawLine(c, cx, cy - mR * 0.55f, cx, _Y(kPlateY), nullptr, 1.2f);
    _ResV(g, c, x, kRailY, kPlateY - 0.03f);
    _Line(g, c, x, kPlateY - 0.03f, x, kPlateY);
    g.DrawLine(c, cx, cy + mR * 0.55f, cx, _Y(kCathodeY), nullptr, 1.2f);
    _ResV(g, c, x, kCathodeY, kCathodeB);
    // Bypass cap beside Rk, both to ground.
    const float capX = x + kStageCapDX, capY = 0.5f * (kCathodeY + kCathodeB);
    _Line(g, c, x, kCathodeY, capX, kCathodeY);
    _Line(g, c, capX, kCathodeY, capX, capY - 0.02f);
    _CapV(g, c, capX, capY);
    _Line(g, c, capX, capY + 0.02f, capX, kCathodeB);
    _Line(g, c, x, kCathodeB, capX, kCathodeB);
    _Ground(g, c, x + 0.5f * kStageCapDX, kCathodeB);
  }
  // A power pentode at (kPowerX, y): control grid from the left, screen grid, plate to the right,
  // cathode lead down to ground. Its name sits clear of the value labels.
  void _Tube(IGraphics& g, const IColor& c, float y, const char* name) const
  {
    const float cx = _X(tubemap::kPowerX), cy = _Y(y);
    const bool top = y < tubemap::kMidY;
    g.DrawCircle(c, cx, cy, mR, nullptr, 1.2f);
    g.DrawDottedLine(c, cx - mR * 0.55f, cy - mR * 0.3f, cx - mR * 0.55f, cy + mR * 0.3f);
    g.DrawDottedLine(c, cx - mR * 0.15f, cy - mR * 0.45f, cx - mR * 0.15f, cy + mR * 0.45f);
    g.DrawLine(c, cx + mR * 0.45f, cy - mR * 0.5f, cx + mR * 0.45f, cy + mR * 0.5f, nullptr, 2.f);
    const float kx = _CathodeX(), ky = cy + mR * 0.94f;
    g.DrawLine(c, kx, ky, kx, ky + 9.f, nullptr, 1.2f);
    _GroundAt(g, c, kx, ky + 9.f);
    const IText nameText(9.f, c, "Roboto-Regular", top ? EAlign::Near : EAlign::Far, EVAlign::Middle);
    if (top) // Right of the tube, under the plate wire
      g.DrawText(nameText, name, IRECT(cx + mR + 3.f, cy + 4.f, cx + mR + 33.f, cy + 14.f));
    else // Left of the tube, over the grid wire
      g.DrawText(nameText, name, IRECT(cx - mR - 33.f, cy - 14.f, cx - mR - 3.f, cy - 4.f));
  }

  std::function<void(int, bool)> mOnPick;
  IRECT mArea;
  float mR = 10.f;
  bool mCircuit = true, mStage2 = false, mPowerAmp = false, mSixL6 = false;
  int mToneStack = 0;
  bool mHaveData = false;
  float mMean[tube::kNumProbes], mMin[tube::kNumProbes], mMax[tube::kNumProbes];
  bool mConducting[2] = {false, false}, mCutoff[2] = {false, false}, mPAConducting = false, mPACutoff = false;
  int mSelA = -1, mSelB = -1, mHoverProbe = -1;
};

// The plain-English readout, word-wrapped, scrollable with the mouse wheel.
class TubeReadoutControl : public IControl
{
public:
  TubeReadoutControl(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void SetLines(std::vector<tube::debug::Line> lines, std::string header)
  {
    mLines = std::move(lines);
    mHeader = std::move(header);
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    using namespace tubedebug;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.25f), mRECT, 4.f);
    const IRECT area = mRECT.GetPadded(-8.f);
    const IText headerText(10.f, Dim().WithOpacity(0.8f), "Roboto-Regular", EAlign::Near, EVAlign::Top);
    g.DrawText(headerText, mHeader.c_str(), area.GetFromTop(12.f));
    const IRECT body = area.GetReducedFromTop(16.f);
    constexpr float tagW = 46.f, lineH = 13.5f;
    const IText bodyText(11.f, Text(), "Roboto-Regular", EAlign::Near, EVAlign::Top);
    const IText tagText(9.f, ColorA(), "Michroma-Regular", EAlign::Near, EVAlign::Top);
    // Wrap everything first, to know the total height for scrolling.
    struct Row
    {
      std::string tag, text;
      bool warning, first;
    };
    std::vector<Row> rows;
    std::string lastTag;
    for (const auto& line : mLines)
    {
      const auto wrapped = Wrap(g, bodyText, line.text, body.W() - tagW);
      for (size_t i = 0; i < wrapped.size(); i++)
        rows.push_back({i == 0 && line.tag != lastTag ? line.tag : std::string(), wrapped[i], line.warning, i == 0});
      lastTag = line.tag;
    }
    const float total = rows.size() * lineH + mLines.size() * 3.f;
    mMaxScroll = std::max(0.f, total - body.H());
    mScroll = std::clamp(mScroll, 0.f, mMaxScroll);
    float y = body.T - mScroll;
    g.PathClipRegion(body);
    for (const auto& row : rows)
    {
      if (row.first)
        y += 3.f;
      if (y + lineH >= body.T && y <= body.B)
      {
        if (!row.tag.empty())
        {
          IText t = tagText;
          if (row.warning)
            t.mFGColor = Amber();
          g.DrawText(t, row.tag.c_str(), IRECT(body.L, y + 1.f, body.L + tagW, y + lineH));
        }
        IText t = bodyText;
        if (row.warning)
          t.mFGColor = Amber();
        g.DrawText(t, row.text.c_str(), IRECT(body.L + tagW, y, body.R, y + lineH));
      }
      y += lineH;
    }
    g.PathClipRegion();
    if (mMaxScroll > 0.f)
    {
      // Scroll position indicator.
      const float frac = body.H() / (body.H() + mMaxScroll);
      const float h = body.H() * frac, top = body.T + (body.H() - h) * (mScroll / mMaxScroll);
      g.FillRoundRect(Dim(), IRECT(mRECT.R - 4.f, top, mRECT.R - 1.f, top + h), 1.5f);
    }
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    mScroll = std::clamp(mScroll - d * 20.f, 0.f, mMaxScroll);
    SetDirty(false);
  }

private:
  std::vector<tube::debug::Line> mLines;
  std::string mHeader = "Waiting for the first measurement...";
  float mScroll = 0.f, mMaxScroll = 0.f;
};

// A small pill button whose label comes from a function (so it can say ON/OFF, etc.).
class TubeDebugButton : public IControl
{
public:
  TubeDebugButton(const IRECT& bounds, std::function<std::string()> label, std::function<bool()> lit,
                  std::function<void()> onClick)
  : IControl(bounds)
  , mLabel(std::move(label))
  , mLit(std::move(lit))
  , mOnClick(std::move(onClick))
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool lit = mLit && mLit();
    const IColor c = lit ? tubedebug::Amber() : tubedebug::Text();
    g.FillRoundRect(lit ? c.WithOpacity(0.25f) : COLOR_BLACK.WithOpacity(0.3f), mRECT, 4.f);
    g.DrawRoundRect(c.WithOpacity(mMouseIsOver ? 0.9f : 0.5f), mRECT, 4.f);
    g.DrawText(IText(11.f, c, "Roboto-Regular"), mLabel().c_str(), mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mOnClick)
      mOnClick();
    SetDirty(false);
  }

private:
  std::function<std::string()> mLabel;
  std::function<bool()> mLit;
  std::function<void()> mOnClick;
};

class NAMTubeDebugPageControl : public NAMOverlayPanelControl
{
public:
  struct Callbacks
  {
    std::function<bool()> isLogging;
    std::function<void(bool)> setLogging;
    std::function<void()> openLogFolder;
  };

  NAMTubeDebugPageControl(const IRECT& bounds, const ISVG& closeSVG, TubeScopeSettings* scope, Callbacks callbacks)
  : NAMOverlayPanelControl(bounds, "TUBE LAB DEBUG", closeSVG)
  , mScopeSettings(scope)
  , mCallbacks(std::move(callbacks))
  {
  }

  void OnAttached() override
  {
    NAMOverlayPanelControl::OnAttached();
    const IRECT content = GetRECT().GetPadded(-14.f).GetReducedFromTop(40.f);
    const IRECT bottomBar = content.GetFromBottom(24.f);
    const IRECT rest = content.GetReducedFromBottom(30.f);
    const IRECT top = rest.GetFromTop(rest.H() * 0.58f);
    const IRECT scopeArea = rest.GetReducedFromTop(top.H() + 8.f);
    const IRECT mapArea = top.GetFromLeft(top.W() * 0.62f);
    const IRECT readoutArea = top.GetReducedFromLeft(mapArea.W() + 8.f);

    mScope = new TubeScopeControl(scopeArea, mScopeSettings);
    mMap = new TubeCircuitMapControl(mapArea, [this](int probe, bool channelB) {
      mScope->SetChannel(probe, channelB);
      mMap->SetSelected(mScopeSettings->chA, mScopeSettings->chB);
    });
    mReadout = new TubeReadoutControl(readoutArea);
    AddChildControl(mMap);
    AddChildControl(mReadout);
    AddChildControl(mScope);
    mMap->SetSelected(mScopeSettings->chA, mScopeSettings->chB);

    const IRECT logButton = bottomBar.GetFromLeft(110.f);
    const IRECT folderButton = bottomBar.GetReducedFromLeft(118.f).GetFromLeft(110.f);
    AddChildControl(new TubeDebugButton(
      logButton,
      [this]() { return std::string(mCallbacks.isLogging && mCallbacks.isLogging() ? "CSV LOG: ON" : "CSV LOG: OFF"); },
      [this]() { return mCallbacks.isLogging && mCallbacks.isLogging(); },
      [this]() {
        if (mCallbacks.setLogging && mCallbacks.isLogging)
          mCallbacks.setLogging(!mCallbacks.isLogging());
      }));
    AddChildControl(new TubeDebugButton(
      folderButton, []() { return std::string("OPEN LOG FOLDER"); }, nullptr,
      [this]() {
        if (mCallbacks.openLogFolder)
          mCallbacks.openLogFolder();
      }));
    mStatus = new StatusControl(bottomBar.GetReducedFromLeft(236.f));
    AddChildControl(mStatus);
  }

  TubeScopeControl* Scope() { return mScope; }
  TubeCircuitMapControl* Map() { return mMap; }
  TubeReadoutControl* Readout() { return mReadout; }

  void SetStatus(std::string app, std::string log)
  {
    if (mStatus && (app != mStatus->app || log != mStatus->log))
    {
      mStatus->app = std::move(app);
      mStatus->log = std::move(log);
      mStatus->SetDirty(false);
    }
  }

private:
  // Two lines along the bottom: the app's audio settings, and the CSV log's state.
  struct StatusControl : public IControl
  {
    StatusControl(const IRECT& bounds)
    : IControl(bounds)
    {
      mIgnoreMouse = true;
    }
    void Draw(IGraphics& g) override
    {
      const IText t(10.f, tubedebug::Dim().WithOpacity(0.75f), "Roboto-Regular", EAlign::Near, EVAlign::Middle);
      g.DrawText(t, app.c_str(), mRECT.GetFromTop(mRECT.H() * 0.5f));
      g.DrawText(t, log.c_str(), mRECT.GetFromBottom(mRECT.H() * 0.5f));
    }
    std::string app, log;
  };

  TubeScopeSettings* mScopeSettings;
  Callbacks mCallbacks;
  TubeScopeControl* mScope = nullptr;
  TubeCircuitMapControl* mMap = nullptr;
  TubeReadoutControl* mReadout = nullptr;
  StatusControl* mStatus = nullptr;
};
