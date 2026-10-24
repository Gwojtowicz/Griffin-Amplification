#pragma once

// 1.70.0 PACK PICKER buttons (see PackScanner.h): a small button whose label is read live from the
// plugin (so it never holds stale state across relayouts), and calls back on click with itself, for
// popping up a menu under it. Included from NeuralAmpModeler.cpp after LibraryControls.h.

#include <functional>
#include <string>

using namespace iplug;
using namespace igraphics;

class NAMPackButtonControl : public IControl
{
public:
  NAMPackButtonControl(const IRECT& bounds, std::function<std::string()> label, std::function<bool()> enabled,
                       std::function<void(IControl*)> onClick, std::function<void(IPopupMenu*)> onMenu = nullptr, const IBitmap& bitmap = IBitmap(),
                       const IText& text = IText(11.f))
  : IControl(bounds)
  , mLabel(std::move(label))
  , mEnabled(std::move(enabled))
  , mOnClick(std::move(onClick))
  , mOnMenu(std::move(onMenu))
  , mBitmap(bitmap)
  , mText(text)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool on = !mEnabled || mEnabled();
    const IColor theme = PluginColors::NAM_THEMECOLOR;
    IRECT inner = mRECT.GetPadded(-5.f);
    IText t(11.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(on ? 0.95f : 0.4f), "Roboto-Regular");
    if (mBitmap.GetAPIBitmap())
    {
      // 1.70.2: drawn like the model dropdown (its background, its text), with a menu arrow on the right.
      g.DrawFittedBitmap(mBitmap, mRECT);
      t = mText;
      inner = mRECT.GetPadded(-6.f).GetHPadded(-2.f).GetReducedFromLeft(28.f).GetReducedFromRight(22.f);
      const IRECT arrow = mRECT.GetFromRight(22.f).GetCentredInside(8.f, 5.f).GetTranslated(-6.f, 0.f);
      g.FillTriangle(t.mFGColor.WithOpacity(mMouseIsOver ? 1.f : 0.7f), arrow.L, arrow.T, arrow.R, arrow.T, arrow.MW(), arrow.B);
    }
    else
    {
      g.FillRoundRect(theme.WithOpacity(on ? (mMouseIsOver ? 0.40f : 0.25f) : 0.08f), mRECT, 5.f);
      g.DrawRoundRect(theme.WithOpacity(on ? 0.85f : 0.3f), mRECT, 5.f, nullptr, 1.2f);
    }
    std::string s = mLabel ? mLabel() : std::string();
    // Shorten from the middle until it fits.
    IRECT r;
    g.MeasureText(t, s.c_str(), r);
    while (s.size() > 6 && r.W() > inner.W())
    {
      const size_t mid = s.size() / 2;
      s = s.substr(0, mid - 2) + ".." + s.substr(mid + 1);
      g.MeasureText(t, s.c_str(), r);
    }
    g.DrawText(t, s.c_str(), inner);
  }

  // The menu this button popped up (or one of its submenus): the item picked is its chosen item.
  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu && mOnMenu)
      mOnMenu(pSelectedMenu);
    SetDirty(false);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if ((!mEnabled || mEnabled()) && mOnClick)
      mOnClick(this);
    SetDirty(false);
  }

private:
  std::function<std::string()> mLabel;
  std::function<bool()> mEnabled;
  std::function<void(IControl*)> mOnClick;
  std::function<void(IPopupMenu*)> mOnMenu;
  IBitmap mBitmap;
  IText mText;
};
