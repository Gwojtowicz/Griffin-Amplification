#pragma once

// The OUTPUT page (1.58.0): Master Out, the output limiter (on/off, Ceiling, Release, and a
// gain-reduction meter), and the output's stereo Width / Widen with a correlation meter. Opened from
// the OUTPUT button under Master Out (AMP popup), the grid's OUT, or a click on the OUT meter.
// Included from NeuralAmpModeler.cpp after TubeDebugControls.h (uses tubedebug::Wrap).

#include <atomic>
#include <functional>
#include <string>

using namespace iplug;
using namespace igraphics;

// A section card: a rounded panel with a title and a few lines of explanation under its controls.
class NAMOutputCardControl : public IControl
{
public:
  NAMOutputCardControl(const IRECT& bounds, const char* title, std::string note, float noteTop)
  : IControl(bounds)
  , mTitle(title)
  , mNote(std::move(note))
  , mNoteTop(noteTop)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(PluginColors::NAM_CARD.WithOpacity(0.6f), mRECT, 6.f);
    g.DrawRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.35f), mRECT, 6.f);
    const IRECT inner = mRECT.GetPadded(-14.f);
    g.DrawText(IText(13.f, PluginColors::NAM_THEMECOLOR, "Michroma-Regular", EAlign::Near, EVAlign::Top), mTitle.c_str(),
               inner);
    const IText noteText(11.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.6f), "Roboto-Regular", EAlign::Near,
                         EVAlign::Top);
    float y = mNoteTop;
    for (const std::string& line : tubedebug::Wrap(g, noteText, mNote, inner.W()))
    {
      if (y + 14.f > inner.B)
        break;
      g.DrawText(noteText, line.c_str(), IRECT(inner.L, y, inner.R, y + 14.f));
      y += 14.f;
    }
  }

private:
  std::string mTitle, mNote;
  float mNoteTop;
};

// A horizontal meter bar with a caption and a number: the limiter's gain reduction (0 .. -12 dB,
// filling from the right like a GR meter), or the stereo correlation (-1 .. +1, from the centre).
class NAMOutputMeterControl : public IControl
{
public:
  enum class Kind
  {
    GainReduction,
    Correlation
  };

  NAMOutputMeterControl(const IRECT& bounds, Kind kind)
  : IControl(bounds)
  , mKind(kind)
  {
    mIgnoreMouse = true;
    mValue = mShown = kind == Kind::Correlation ? 1.f : 0.f;
  }

  // From OnIdle. Gain reduction falls instantly and recovers over ~0.3 s, so peaks are readable.
  void SetReading(float v, bool active)
  {
    mActive = active;
    if (mKind == Kind::GainReduction)
      mShown = v < mShown ? v : mShown + 0.12f * (v - mShown);
    else
      mShown += 0.25f * (v - mShown);
    if (std::fabs(mShown - mValue) > 0.01f || active != mWasActive)
    {
      mValue = mShown;
      mWasActive = active;
      SetDirty(false);
    }
  }

  void Draw(IGraphics& g) override
  {
    const IColor ink = PluginColors::NAM_THEMEFONTCOLOR;
    const IRECT caption = mRECT.GetFromTop(16.f);
    const IRECT bar = mRECT.GetReducedFromTop(20.f).GetFromTop(10.f);
    const IRECT scale = IRECT(bar.L, bar.B + 2.f, bar.R, bar.B + 14.f);
    const IText capText(11.f, ink.WithOpacity(0.75f), "Roboto-Regular", EAlign::Near, EVAlign::Middle);
    const IText numText(11.f, ink.WithOpacity(0.9f), "Roboto-Regular", EAlign::Far, EVAlign::Middle);
    const IText scaleText(9.f, ink.WithOpacity(0.45f), "Roboto-Regular", EAlign::Center, EVAlign::Top);
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.35f), bar, 3.f);
    char num[32];
    if (mKind == Kind::GainReduction)
    {
      g.DrawText(capText, mActive ? "Gain reduction" : "Gain reduction (limiter off)", caption);
      snprintf(num, sizeof(num), "%.1f dB", mValue > -0.05f ? 0.f : mValue);
      const float frac = std::clamp(-mValue / 12.f, 0.f, 1.f);
      if (mActive && frac > 0.f)
        g.FillRoundRect(PluginColors::NAM_THEMECOLOR, IRECT(bar.R - bar.W() * frac, bar.T, bar.R, bar.B), 3.f);
      for (int db : {0, -3, -6, -9, -12})
      {
        const float x = std::clamp(bar.R - bar.W() * (-db / 12.f), bar.L + 8.f, bar.R - 8.f);
        char t[8];
        snprintf(t, sizeof(t), "%d", db);
        g.DrawText(scaleText, t, IRECT(x - 12.f, scale.T, x + 12.f, scale.B));
      }
    }
    else
    {
      g.DrawText(capText, "Stereo correlation", caption);
      snprintf(num, sizeof(num), "%+.2f", mValue);
      const float mid = bar.MW(), x = mid + 0.5f * bar.W() * std::clamp(mValue, -1.f, 1.f);
      const IColor c = mValue < 0.f ? IColor(255, 235, 90, 60) : PluginColors::NAM_THEMECOLOR;
      if (mActive)
        g.FillRoundRect(c, IRECT(std::min(mid, x), bar.T, std::max(mid, x), bar.B), 3.f);
      g.DrawLine(ink.WithOpacity(0.5f), mid, bar.T - 2.f, mid, bar.B + 2.f);
      const char* labels[3] = {"-1 out of phase", "0 wide", "+1 mono"};
      const float xs[3] = {bar.L + 34.f, mid, bar.R - 22.f};
      for (int i = 0; i < 3; i++)
        g.DrawText(scaleText, labels[i], IRECT(xs[i] - 40.f, scale.T, xs[i] + 40.f, scale.B));
    }
    g.DrawText(numText, num, caption);
  }

private:
  Kind mKind;
  float mValue = 0.f, mShown = 0.f;
  bool mActive = false, mWasActive = true;
};
