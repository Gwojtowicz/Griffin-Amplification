#pragma once

// The AMP NEST's LABS and the PEDALS drawer (1.60.0).
// * LABS: TUBE LAB and CAB LAB get their own section in the AMP NEST, apart from the pedals, each in
//   a slot of its own (NAMLabSlotControl). Off the grid, the lab's chip sits in its slot to drag out;
//   on the grid, the slot says so and opens it on a click.
// * PEDALS: a drawer that opens over the lower part of the grid (NAMPedalDrawerControl is its panel;
//   the pedal chips sit on it). Starting to drag a pedal closes it, so the grid is in full view
//   where you drop it.
// Included from NeuralAmpModeler.cpp after SpeakerLabControls.h (uses tubedebug::Wrap).

#include <functional>
#include <string>

using namespace iplug;
using namespace igraphics;

// The drawer's panel: title, a hint, and a close X. Clicks on its empty space go nowhere (not to
// the grid underneath).
class NAMPedalDrawerControl : public IControl
{
public:
  NAMPedalDrawerControl(const IRECT& bounds, std::function<void()> onClose)
  : IControl(bounds)
  , mOnClose(std::move(onClose))
  {
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.35f), mRECT.GetTranslated(0.f, 3.f), 8.f); // Shadow
    g.FillRoundRect(PluginColors::NAM_PANEL, mRECT, 8.f);
    g.DrawRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f), mRECT, 8.f, nullptr, 1.5f);
    const IRECT head = mRECT.GetPadded(-10.f).GetFromTop(20.f);
    g.DrawText(IText(14.f, PluginColors::NAM_THEMECOLOR, "Michroma-Regular", EAlign::Near), "PEDALS", head);
    const IText hint(10.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.6f), "Roboto-Regular", EAlign::Near);
    float y = head.B + 4.f;
    for (const std::string& line : tubedebug::Wrap(
           g, hint, "Drag onto the grid; drag off the grid to put back. Click one for its settings.", head.W()))
    {
      g.DrawText(hint, line.c_str(), IRECT(head.L, y, head.R, y + 12.f));
      y += 12.f;
    }
    const IRECT x = _Close();
    const IColor c = PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(mOverClose ? 0.95f : 0.6f);
    g.DrawLine(c, x.L + 5.f, x.T + 5.f, x.R - 5.f, x.B - 5.f, nullptr, 1.5f);
    g.DrawLine(c, x.R - 5.f, x.T + 5.f, x.L + 5.f, x.B - 5.f, nullptr, 1.5f);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (_Close().Contains(x, y) && mOnClose)
      mOnClose();
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const bool over = _Close().Contains(x, y);
    if (over != mOverClose)
    {
      mOverClose = over;
      SetDirty(false);
    }
  }
  void OnMouseOut() override
  {
    mOverClose = false;
    SetDirty(false);
  }

private:
  IRECT _Close() const { return mRECT.GetFromTRHC(30.f, 30.f).GetCentredInside(18.f, 18.f); }
  std::function<void()> mOnClose;
  bool mOverClose = false;
};

// A lab's slot in the AMP NEST. With the lab off the grid its chip covers this (and says to drag it);
// on the grid, this shows the lab's card, a little dimmed, saying so -- and a click opens the lab.
class NAMLabSlotControl : public IControl
{
public:
  NAMLabSlotControl(const IRECT& bounds, const char* name, int kind, std::function<bool()> onGrid,
                    std::function<bool()> active, std::function<void()> open)
  : IControl(bounds)
  , mName(name)
  , mKind(kind)
  , mOnGrid(std::move(onGrid))
  , mActive(std::move(active))
  , mOpen(std::move(open))
  {
  }

  void Draw(IGraphics& g) override
  {
    if (!mOnGrid())
      return;
    DrawLabCard(g, mRECT, mKind, mName, mActive(), false, false,
                mActive() ? "ON THE GRID" : "ON THE GRID (off)",
                mMouseIsOver ? 1.f : 0.85f);
  }

  bool IsHit(float x, float y) const override { return mOnGrid() && IControl::IsHit(x, y); }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mOnGrid() && mOpen)
      mOpen();
  }

private:
  std::string mName;
  int mKind;
  std::function<bool()> mOnGrid, mActive;
  std::function<void()> mOpen;
};

// The AMP NEST's PEDALS button: a row of little stompboxes and the count of pedals off the grid.
class NAMPedalsButtonControl : public IControl
{
public:
  NAMPedalsButtonControl(const IRECT& bounds, std::function<int()> count, std::function<bool()> open,
                         std::function<void()> toggle)
  : IControl(bounds)
  , mCount(std::move(count))
  , mOpen(std::move(open))
  , mToggle(std::move(toggle))
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool open = mOpen();
    const IColor theme = PluginColors::NAM_THEMECOLOR;
    g.FillRoundRect(theme.WithOpacity(open ? 0.55f : (mMouseIsOver ? 0.4f : 0.28f)), mRECT, 6.f);
    g.DrawRoundRect(theme.WithOpacity(0.9f), mRECT, 6.f, nullptr, 1.5f);
    // Three little pedals on the left.
    const float h = mRECT.H() * 0.6f, w = h * 0.7f;
    for (int i = 0; i < 3; i++)
    {
      const IRECT p(mRECT.L + 10.f + i * (w + 3.f), mRECT.MH() - h * 0.5f, mRECT.L + 10.f + i * (w + 3.f) + w,
                    mRECT.MH() + h * 0.5f);
      g.FillRoundRect(IColor(255, 58, 55, 53), p, 2.f);
      g.FillRect(COLOR_BLACK.WithOpacity(0.3f), p.GetFromTop(p.H() * 0.3f).GetPadded(-1.5f));
      g.FillCircle(COLOR_WHITE.WithOpacity(0.5f), p.MW(), p.B - p.H() * 0.22f, w * 0.18f);
    }
    char label[48];
    snprintf(label, sizeof(label), open ? "PEDALS  (close)" : "PEDALS  %d", mCount());
    g.DrawText(IText(13.f, COLOR_WHITE, "Michroma-Regular"), label, mRECT.GetReducedFromLeft(10.f + 3.f * (w + 3.f)));
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mToggle)
      mToggle();
    SetDirty(false);
  }

private:
  std::function<int()> mCount;
  std::function<bool()> mOpen;
  std::function<void()> mToggle;
};

// A few wrapped lines of small text.
class NAMHintTextControl : public IControl
{
public:
  NAMHintTextControl(const IRECT& bounds, const char* text)
  : IControl(bounds)
  , mText(text)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    const IText t(10.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.5f), "Roboto-Regular", EAlign::Center,
                  EVAlign::Top);
    float y = mRECT.T;
    for (const std::string& line : tubedebug::Wrap(g, t, mText, mRECT.W()))
    {
      if (y + 13.f > mRECT.B)
        break;
      g.DrawText(t, line.c_str(), IRECT(mRECT.L, y, mRECT.R, y + 13.f));
      y += 13.f;
    }
  }

private:
  std::string mText;
};
