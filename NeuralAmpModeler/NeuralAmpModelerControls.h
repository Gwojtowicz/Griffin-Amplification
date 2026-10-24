#pragma once

#include <algorithm> // std::clamp
#include <climits> // INT_MIN (MIDI learn's no-target)
#include <chrono> // meter ballistics
#include <cmath> // std::round
#include <cstdarg> // va_list (DragLog)
#include <cstdio> // FILE, fclose
#include <cstdlib> // std::getenv (DragLog)
#include <cstring> // memcpy
#include <functional> // std::function
#include <sstream> // std::stringstream
#include <string> // std::string
#include <unordered_map> // std::unordered_map
#include <vector> // std::vector
#include "IControls.h"
#include "ChainGrid.h"
#include "IPlugPaths.h"

#ifdef OS_WIN
  #include <Windows.h>
  #include <Shellapi.h>
#endif

#define PLUG() static_cast<PLUG_CLASS_NAME*>(GetDelegate())
#define NAM_KNOB_HEIGHT 120.0f
#define NAM_SWTICH_HEIGHT 50.0f

using namespace iplug;
using namespace igraphics;

enum class NAMBrowserState
{
  Empty, // when no file loaded, show "Get" button
  Loaded // when file loaded, show "Clear" button
};

enum class NAMFileLoadSource
{
  ExistingBrowserSelection,
  FilePickerSelection
};

// TEMP drag-debug log (%TEMP%\griffin_drag.log) -- remove once the first-click bug is found.
inline void NAMDragLog(const char* fmt, ...)
{
  char path[512];
  const char* tmp = std::getenv("TEMP");
  std::snprintf(path, sizeof(path), "%s\\griffin_drag.log", tmp ? tmp : ".");
  if (std::FILE* f = std::fopen(path, "a"))
  {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);
    std::fputc('\n', f);
    std::fclose(f);
  }
}

// Where the corner button on the plugin (settings, close settings) goes
// :param rect: Rect for the whole plugin's UI
IRECT CornerButtonArea(const IRECT& rect)
{
  const auto mainArea = rect.GetPadded(-20);
  return mainArea.GetFromTRHC(50, 50).GetCentredInside(20, 20);
};

class NAMSquareButtonControl : public ISVGButtonControl
{
public:
  NAMSquareButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (PluginColors::NAM_LIGHT) // the icons are white or grey
      g.FillRoundRect(COLOR_BLACK.WithOpacity(mMouseIsOver ? 0.6f : 0.42f), mRECT.GetPadded(3.f), 4.f);
    else if (mMouseIsOver)
      g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, 2.f);

    ISVGButtonControl::Draw(g);
  }
};

class NAMCircleButtonControl : public ISVGButtonControl
{
public:
  NAMCircleButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (PluginColors::NAM_LIGHT)
      g.FillEllipse(COLOR_BLACK.WithOpacity(mMouseIsOver ? 0.6f : 0.42f), mRECT.GetPadded(3.f));
    else if (mMouseIsOver)
      g.FillEllipse(PluginColors::MOUSEOVER, mRECT);

    ISVGButtonControl::Draw(g);
  }
};

/// Rounded-rect background fill, used to visually group a section of controls (e.g. the Tone3000 library panel).
class NAMPanelControl : public IControl
{
public:
  NAMPanelControl(const IRECT& bounds, const IColor& color, float cornerRadius = 10.f)
  : IControl(bounds)
  , mColor(color)
  , mCornerRadius(cornerRadius)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override { g.FillRoundRect(mColor, mRECT, mCornerRadius); }

private:
  IColor mColor;
  float mCornerRadius;
};

/// Same as NAMPanelControl, but with a drawn border -- used for the pedal popup "window" so it
/// visually reads as its own floating card rather than a flat fill flush with its surroundings.
class NAMBorderedPanelControl : public IControl
{
public:
  NAMBorderedPanelControl(const IRECT& bounds, const IColor& fillColor, const IColor& borderColor,
                          float cornerRadius = 10.f, float thickness = 1.5f)
  : IControl(bounds)
  , mFillColor(fillColor)
  , mBorderColor(borderColor)
  , mCornerRadius(cornerRadius)
  , mThickness(thickness)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(mFillColor, mRECT, mCornerRadius);
    g.DrawRoundRect(mBorderColor, mRECT, mCornerRadius, nullptr, mThickness);
  }

  // Animates this control's own bounds growing from startRect (e.g. the clicked pedal chip's
  // on-screen rect) to its current (full/final) bounds, so opening the popup reads as a zoom-in
  // from the chip rather than a card appearing instantly at full size. onComplete (if given) fires
  // once the grow finishes -- used to defer revealing the popup's own knobs/controls until the
  // card has actually caught up to full size, instead of them appearing at their final position
  // immediately (looking like they're floating outside the still-small card).
  void AnimateGrowFrom(const IRECT& startRect, int durationMs = 160, std::function<void()> onComplete = nullptr)
  {
    mFinalRECT = mRECT;
    mStartRECT = startRect;
    SetTargetAndDrawRECTs(startRect);
    SetAnimation(
      [this, onComplete](IControl* pCaller) {
        const double t = GetAnimationProgress();
        if (t > 1.)
        {
          SetTargetAndDrawRECTs(mFinalRECT);
          pCaller->OnEndAnimation();
          if (onComplete)
            onComplete();
          return;
        }
        // Ease-out cubic: fast start, gentle settle.
        const float u = 1.f - static_cast<float>(t);
        const float e = 1.f - u * u * u;
        SetTargetAndDrawRECTs(IRECT(mStartRECT.L + (mFinalRECT.L - mStartRECT.L) * e,
                                    mStartRECT.T + (mFinalRECT.T - mStartRECT.T) * e,
                                    mStartRECT.R + (mFinalRECT.R - mStartRECT.R) * e,
                                    mStartRECT.B + (mFinalRECT.B - mStartRECT.B) * e));
      },
      durationMs);
  }

private:
  IColor mFillColor;
  IColor mBorderColor;
  float mCornerRadius;
  float mThickness;
  IRECT mStartRECT;
  IRECT mFinalRECT;
};

// Decorative amp-head-inspired faceplate for the AMP block's popup -- a dark case tone (distinct
// from the plain shared pedal/cab card fill, so the amp's popup reads as its own "unit"), a
// ribbed "grille" band, and a brand plate. This is an ORIGINAL look built from flat vector
// shapes -- not a reproduction of any specific real amplifier's trade dress (script logos,
// tolex/piping color combinations, etc.), deliberately, since those are trademarked. Purely
// decorative (mIgnoreMouse) and drawn via addAmpDetail like every other amp control, so it ends
// up (a) above the shared popup card background and (b) below the amp's own knobs/switches/
// browsers, which are added right after it -- see mLayoutFunc.
class NAMAmpFaceplateControl : public IControl
{
public:
  NAMAmpFaceplateControl(const IRECT& bounds)
  : IControl(bounds)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    const float cR = 8.f;
    g.FillRoundRect(PluginColors::NAM_PANEL, mRECT, cR);
    g.DrawRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.35f), mRECT, cR, nullptr, 1.f);

    // Inset "piping" trim, echoing the welted seam around a real amp head's tolex covering --
    // a construction detail shared by essentially every amp/cab ever built, not brand-specific.
    g.DrawRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.2f), mRECT.GetPadded(-5.f), cR * 0.7f, nullptr, 1.f);

    // (1.24.3: the corner protectors and the ribbed grille strip along the bottom are gone --
    // simpler, and the grille read as a stray pale strip under the model picker.)
  }
};

// Small diagonal-lines resize grip, universal "drag to resize" iconography, for the corner of
// the Amp popup. Reports each incremental drag delta via onDrag (the caller accumulates it into
// whatever persisted size it's adjusting) and onDragEnd once the mouse is released (the caller
// commits/relayouts there -- not on every drag frame, which would mean a full plugin relayout
// dozens of times a second while dragging).
class NAMResizeGripControl : public IControl
{
public:
  using OnDragFunc = std::function<void(float dX, float dY)>;
  using OnDragEndFunc = std::function<void()>;

  NAMResizeGripControl(const IRECT& bounds, OnDragFunc onDrag, OnDragEndFunc onDragEnd)
  : IControl(bounds)
  , mOnDrag(onDrag)
  , mOnDragEnd(onDragEnd)
  {
    SetTooltip("Drag to resize");
  }

  void Draw(IGraphics& g) override
  {
    const IColor color = PluginColors::NAM_THEMECOLOR.WithOpacity(mMouseIsOver ? 0.9f : 0.5f);
    for (float offset : {4.f, 9.f, 14.f})
      g.DrawLine(color, mRECT.R - offset, mRECT.B - 2.f, mRECT.R - 2.f, mRECT.B - offset, nullptr, 1.5f);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mOnDrag)
      mOnDrag(dX, dY);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (mOnDragEnd)
      mOnDragEnd();
  }

private:
  OnDragFunc mOnDrag;
  OnDragEndFunc mOnDragEnd;
};

/// Where everything on the signal-chain grid (ChainGrid.h) goes on screen: the cells, the block
/// chips inside them (with a gutter around each for ports and cables), and the IN / OUT ends.
/// Shared by the plugin's layout (chip positions, drop targets) and NAMChainGridControl.
struct NAMChainGridGeometry
{
  IRECT grid; // the cells
  IRECT inStub, outStub;
  float cellW = 0.f, cellH = 0.f;

  NAMChainGridGeometry() = default;
  // Fills `area`'s width; cells are taller than wide (pedal-shaped, with room for a two-line
  // name), up to maxH for the whole grid.
  NAMChainGridGeometry(const IRECT& area, float maxH)
  {
    const float stubW = std::clamp(area.W() * 0.045f, 22.f, 34.f);
    cellW = (area.W() - 2.f * stubW) / chaingrid::kCols;
    cellH = std::clamp(cellW * 1.5f, 30.f, std::max(30.f, maxH / chaingrid::kRows));
    grid = IRECT(area.L + stubW, area.T, area.R - stubW, area.T + cellH * chaingrid::kRows);
    const float stubH = std::min(cellH * 0.55f, 26.f);
    inStub = IRECT(area.L, RowY(0) - stubH * 0.5f, grid.L - 3.f, RowY(0) + stubH * 0.5f);
    outStub = IRECT(grid.R + 3.f, RowY(0) - stubH * 0.5f, area.R, RowY(0) + stubH * 0.5f);
  }

  float GutterX() const { return std::max(3.f, cellW * 0.12f); }
  float RowY(int row) const { return grid.T + (row + 0.5f) * cellH; }
  IRECT Cell(int cell) const
  {
    const float l = grid.L + chaingrid::CellCol(cell) * cellW, t = grid.T + chaingrid::CellRow(cell) * cellH;
    return IRECT(l, t, l + cellW, t + cellH);
  }
  IRECT Chip(int cell) const { return Cell(cell).GetHPadded(-GutterX()).GetVPadded(-cellH * 0.1f); }
  // The cell under (x, y), or -1 off the grid.
  int CellAt(float x, float y) const { return grid.Contains(x, y) ? CellAtClamped(x, y) : -1; }
  // The cell under (x, y), or the nearest one if it's off the grid.
  int CellAtClamped(float x, float y) const
  {
    const int col = std::clamp((int)((x - grid.L) / cellW), 0, chaingrid::kCols - 1);
    const int row = std::clamp((int)((y - grid.T) / cellH), 0, chaingrid::kRows - 1);
    return chaingrid::MakeCell(row, col);
  }

  // Where a cable leaves a source (a block, or kInput) and enters a destination (a block, or kOutput).
  void OutPort(const chaingrid::Grid& g, int node, float& x, float& y) const
  {
    if (node == chaingrid::kInput)
    {
      x = inStub.R;
      y = RowY(0);
      return;
    }
    const IRECT chip = Chip(g.cell[node]);
    x = chip.R + 2.f;
    y = chip.MH();
  }
  void InPort(const chaingrid::Grid& g, int node, float& x, float& y) const
  {
    if (node == chaingrid::kOutput)
    {
      x = outStub.L;
      y = RowY(0);
      return;
    }
    const IRECT chip = Chip(g.cell[node]);
    x = chip.L - 2.f;
    y = chip.MH();
  }

  // A cable's path: right angles only, through the gutters between cells, never across a block.
  // Out of the source; if the next column is the destination's, down the gutter between them and in.
  // Across a gap, it runs along the source's row (or the destination's) if that row is clear, and
  // otherwise along the gutter between rows nearest the two ends.
  std::vector<std::pair<float, float>> Route(const chaingrid::Grid& g, int src, int dst) const
  {
    std::vector<std::pair<float, float>> pts;
    float x0, y0, x3, y3;
    OutPort(g, src, x0, y0);
    InPort(g, dst, x3, y3);
    const int cs = g.ColOf(src), cd = g.ColOf(dst);
    const float xg1 = grid.L + (cs + 1) * cellW; // the gutter just right of the source
    const float xg2 = grid.L + cd * cellW; // the gutter just left of the destination
    pts.push_back({x0, y0});
    auto rowClear = [&](int row) {
      for (int col = cs + 1; col < cd; col++)
        if (g.BlockAt(chaingrid::MakeCell(row, col)) >= 0)
          return false;
      return true;
    };
    if (y0 != y3 || !(cd == cs + 1 || rowClear(g.RowOf(src))))
    {
      if (cd == cs + 1)
      {
        pts.push_back({xg1, y0});
        pts.push_back({xg1, y3});
      }
      else if (rowClear(g.RowOf(src)))
      {
        pts.push_back({xg2, y0});
        pts.push_back({xg2, y3});
      }
      else if (rowClear(g.RowOf(dst)))
      {
        pts.push_back({xg1, y0});
        pts.push_back({xg1, y3});
      }
      else
      {
        float yg = grid.T, best = 1e9f;
        for (int k = 0; k <= chaingrid::kRows; k++)
        {
          const float y = grid.T + k * cellH;
          const float cost = std::fabs(y - y0) + std::fabs(y - y3);
          if (cost < best)
          {
            best = cost;
            yg = y;
          }
        }
        pts.push_back({xg1, y0});
        pts.push_back({xg1, yg});
        pts.push_back({xg2, yg});
        pts.push_back({xg2, y3});
      }
    }
    pts.push_back({x3, y3});
    return pts;
  }
};

/// The signal-chain grid itself, drawn under the block chips: a dot in each empty cell, the IN / OUT
/// ends, every cable (bright while it carries signal from IN to OUT, dim otherwise), and a port on
/// each side of every block. Drag from a port to a block (or OUT / IN) to add a cable there, or
/// remove it if it's already there; click a cable to remove it. Edits go through `setGrid`.
class NAMChainGridControl : public IControl
{
public:
  NAMChainGridControl(const IRECT& bounds, const NAMChainGridGeometry& geom,
                      std::function<chaingrid::Grid()> getGrid,
                      std::function<void(const chaingrid::Grid&)> setGrid)
  : IControl(bounds)
  , mGeom(geom)
  , mGetGrid(getGrid)
  , mSetGrid(setGrid)
  {
  }

  // 1.58.0: a click on OUT (not on its port) opens the OUTPUT page.
  void SetOutClick(std::function<void()> f) { mOutClick = std::move(f); }

  // Highlights the cell a dragged block would land in (-1 for none).
  void SetDropPreview(int cell)
  {
    if (cell != mPreviewCell)
    {
      mPreviewCell = cell;
      SetDirty(false);
    }
  }

  void Draw(IGraphics& g) override
  {
    const chaingrid::Grid grid = mGetGrid();
    const IColor theme = PluginColors::NAM_THEMECOLOR;
    bool fromInput[chaingrid::kNumIds + 1] = {}, toOutput[chaingrid::kNumIds + 1] = {};
    _Liveness(grid, fromInput, toOutput);

    // Empty cells: a faint dot, so the grid reads as places to put things.
    for (int c = 0; c < chaingrid::kNumCells; c++)
      if (grid.BlockAt(c) < 0)
      {
        const IRECT cell = mGeom.Cell(c);
        g.FillCircle(PluginColors::NAM_INK.WithOpacity(0.13f), cell.MW(), cell.MH(), 1.5f);
      }
    if (mPreviewCell >= 0)
    {
      const IRECT chip = mGeom.Chip(mPreviewCell);
      g.FillRoundRect(theme.WithOpacity(0.15f), chip, 4.f);
      g.DrawRoundRect(theme.WithOpacity(0.9f), chip, 4.f, nullptr, 1.5f);
    }

    // IN / OUT.
    for (const IRECT* stub : {&mGeom.inStub, &mGeom.outStub})
    {
      const bool isIn = stub == &mGeom.inStub;
      g.FillRoundRect(PluginColors::NAM_BG.WithContrast(PluginColors::NAM_LIGHT ? -0.08f : 0.04f), *stub, 4.f);
      g.DrawRoundRect(theme.WithOpacity(0.6f), *stub, 4.f, nullptr, 1.f);
      // Clear of the port dot on the stub's inner edge, and small enough for "OUT" to fit.
      const IRECT textArea = isIn ? stub->GetReducedFromRight(4.f) : stub->GetReducedFromLeft(4.f);
      const IText stubText(std::clamp(std::min(stub->H() * 0.42f, textArea.W() * 0.42f), 8.5f, 11.f),
                           PluginColors::NAM_INK.WithOpacity(0.8f), "Roboto-Regular");
      g.DrawText(stubText, isIn ? "IN" : "OUT", textArea);
    }

    // Cables, dim ones first so the live path draws on top.
    for (int pass = 0; pass < 2; pass++)
      _ForEachEdge(grid, [&](int src, int dst) {
        const bool live = fromInput[src] && toOutput[dst];
        if (live != (pass == 1))
          return;
        const bool hovered = mHoverSrc == src && mHoverDst == dst;
        const IColor color = hovered ? PluginColors::NAM_INK.WithOpacity(0.9f)
                             : live  ? theme.WithOpacity(0.85f)
                                     : PluginColors::NAM_INK.WithOpacity(0.22f);
        _StrokeRoute(g, mGeom.Route(grid, src, dst), color, hovered ? 3.f : 2.f);
      });

    // Ports: out on the right of IN and of each block, in on the left of OUT and of each block.
    auto drawPort = [&](float x, float y, bool hovered) {
      g.FillCircle(PluginColors::NAM_BG, x, y, hovered ? 4.f : 3.f);
      g.DrawCircle(hovered ? PluginColors::NAM_INK : theme.WithOpacity(0.8f), x, y, hovered ? 4.f : 3.f, nullptr, 1.2f);
    };
    _ForEachPort(grid, [&](int node, bool isOut, float x, float y) {
      drawPort(x, y, mHoverPortNode == node && mHoverPortIsOut == isOut);
    });

    // A cable being dragged out of a port.
    if (mDraggingCable)
    {
      float x, y;
      _PortPos(grid, mDragNode, mDragFromOut, x, y);
      g.DrawLine(PluginColors::NAM_INK.WithOpacity(0.85f), x, y, mMouseX, mMouseY, nullptr, 2.f);
      g.FillCircle(PluginColors::NAM_INK.WithOpacity(0.85f), mMouseX, mMouseY, 3.f);
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    _UpdateHover(x, y);
    mOverX = x;
    mOverY = y;
    mOverValid = true;
    SetDirty(false);
  }

  void OnMouseOut() override
  {
    mHoverPortNode = mHoverSrc = mHoverDst = -1;
    mOverValid = false;
    SetDirty(false);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    // A cable is only removed if it was already highlighted under the pointer, right where the
    // click lands -- i.e. you were visibly aiming at it. A click that arrives without that (e.g.
    // the first click after the window opens, which has been seen landing off its target) can't
    // silently cut the chain.
    const bool aimed = mOverValid && std::fabs(x - mOverX) < 3.f && std::fabs(y - mOverY) < 3.f;
    const int aimedSrc = aimed ? mHoverSrc : -1, aimedDst = aimed ? mHoverDst : -1;
    _UpdateHover(x, y);
    NAMDragLog("grid down at %.1f,%.1f port=%d cable=%d->%d aimed=%d screenScale=%.3f drawScale=%.3f", x, y,
               mHoverPortNode, mHoverSrc, mHoverDst, (int)aimed, GetUI()->GetScreenScale(), GetUI()->GetDrawScale());
    if (mHoverPortNode >= 0)
    {
      mDraggingCable = true;
      mDragNode = mHoverPortNode;
      mDragFromOut = mHoverPortIsOut;
      mMouseX = x;
      mMouseY = y;
    }
    else if (mOutClick && mGeom.outStub.Contains(x, y))
      mOutClick();
    else if (mHoverSrc >= 0 && mHoverSrc == aimedSrc && mHoverDst == aimedDst)
    {
      chaingrid::Grid grid = mGetGrid();
      grid.RemoveEdge(mHoverSrc, mHoverDst);
      mSetGrid(grid);
      mHoverSrc = mHoverDst = -1;
    }
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    mMouseX = x;
    mMouseY = y;
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (!mDraggingCable)
      return;
    mDraggingCable = false;
    chaingrid::Grid grid = mGetGrid();
    const int target = _NodeAt(grid, x, y);
    if (target >= 0 && target != mDragNode)
    {
      const bool changed = mDragFromOut ? grid.ToggleEdge(mDragNode, target) : grid.ToggleEdge(target, mDragNode);
      if (changed)
        mSetGrid(grid);
    }
    _UpdateHover(x, y);
    SetDirty(false);
  }

private:
  template <typename F>
  static void _ForEachEdge(const chaingrid::Grid& grid, F&& f)
  {
    for (int dst = 0; dst <= chaingrid::kOutput; dst++)
    {
      if (dst == chaingrid::kInput || (dst < chaingrid::kNumIds && !grid.Placed(dst)))
        continue;
      for (int src = 0; src < chaingrid::kNumIds; src++)
        if (grid.HasEdge(src, dst) && grid.EdgeValid(src, dst))
          f(src, dst);
    }
  }

  template <typename F>
  void _ForEachPort(const chaingrid::Grid& grid, F&& f) const
  {
    float x, y;
    for (int b = 0; b < chaingrid::kNumIds; b++)
      if (grid.Placed(b))
      {
        mGeom.InPort(grid, b, x, y);
        f(b, false, x, y);
        mGeom.OutPort(grid, b, x, y);
        f(b, true, x, y);
      }
    mGeom.OutPort(grid, chaingrid::kInput, x, y);
    f(chaingrid::kInput, true, x, y);
    mGeom.InPort(grid, chaingrid::kOutput, x, y);
    f(chaingrid::kOutput, false, x, y);
  }

  void _PortPos(const chaingrid::Grid& grid, int node, bool isOut, float& x, float& y) const
  {
    if (isOut)
      mGeom.OutPort(grid, node, x, y);
    else
      mGeom.InPort(grid, node, x, y);
  }

  // What's reachable from IN and what reaches OUT, along valid cables (in column order, since
  // cables only run rightwards).
  static void _Liveness(const chaingrid::Grid& grid, bool* fromInput, bool* toOutput)
  {
    fromInput[chaingrid::kInput] = true;
    toOutput[chaingrid::kOutput] = true;
    for (int col = 0; col < chaingrid::kCols; col++)
      for (int b = 0; b < chaingrid::kNumIds; b++)
        if (grid.Placed(b) && grid.ColOf(b) == col)
          for (int s = 0; s < chaingrid::kNumIds; s++)
            if (grid.HasEdge(s, b) && grid.EdgeValid(s, b) && fromInput[s])
              fromInput[b] = true;
    for (int col = chaingrid::kCols - 1; col >= 0; col--)
      for (int b = 0; b < chaingrid::kNumIds; b++)
        if (grid.Placed(b) && grid.ColOf(b) == col)
          for (int d = 0; d <= chaingrid::kOutput; d++)
            if (d != chaingrid::kInput && (d == chaingrid::kOutput || grid.Placed(d)) && grid.HasEdge(b, d)
                && grid.EdgeValid(b, d) && toOutput[d])
              toOutput[b] = true;
    // IN itself reaches OUT if anything it feeds does.
    for (int d = 0; d <= chaingrid::kOutput; d++)
      if (d != chaingrid::kInput && grid.HasEdge(chaingrid::kInput, d) && grid.EdgeValid(chaingrid::kInput, d)
          && toOutput[d])
        toOutput[chaingrid::kInput] = true;
  }

  // Strokes a right-angled path with rounded corners.
  static void _StrokeRoute(IGraphics& g, std::vector<std::pair<float, float>> pts, const IColor& color, float thickness)
  {
    std::vector<std::pair<float, float>> p;
    for (const auto& q : pts)
      if (p.empty() || std::fabs(q.first - p.back().first) + std::fabs(q.second - p.back().second) > 0.01f)
        p.push_back(q);
    if (p.size() < 2)
      return;
    g.PathClear();
    g.PathMoveTo(p[0].first, p[0].second);
    for (size_t i = 1; i + 1 < p.size(); i++)
    {
      const float ax = p[i].first - p[i - 1].first, ay = p[i].second - p[i - 1].second;
      const float bx = p[i + 1].first - p[i].first, by = p[i + 1].second - p[i].second;
      const float la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
      const float r = std::min({6.f, la * 0.5f, lb * 0.5f});
      g.PathLineTo(p[i].first - ax / la * r, p[i].second - ay / la * r);
      g.PathQuadraticBezierTo(p[i].first, p[i].second, p[i].first + bx / lb * r, p[i].second + by / lb * r);
    }
    g.PathLineTo(p.back().first, p.back().second);
    IStrokeOptions options;
    options.mCapOption = ELineCap::Round;
    options.mJoinOption = ELineJoin::Round;
    g.PathStroke(IPattern(color), thickness, options);
  }

  static float _DistToSegment(float x, float y, float x1, float y1, float x2, float y2)
  {
    const float dx = x2 - x1, dy = y2 - y1;
    const float len2 = dx * dx + dy * dy;
    const float t = len2 > 0.f ? std::clamp(((x - x1) * dx + (y - y1) * dy) / len2, 0.f, 1.f) : 0.f;
    const float px = x1 + t * dx - x, py = y1 + t * dy - y;
    return std::sqrt(px * px + py * py);
  }

  // The block (or IN / OUT) under (x, y), or -1.
  int _NodeAt(const chaingrid::Grid& grid, float x, float y) const
  {
    if (mGeom.inStub.GetPadded(4.f).Contains(x, y))
      return chaingrid::kInput;
    if (mGeom.outStub.GetPadded(4.f).Contains(x, y))
      return chaingrid::kOutput;
    const int c = mGeom.CellAt(x, y);
    return c >= 0 ? grid.BlockAt(c) : -1;
  }

  void _UpdateHover(float x, float y)
  {
    const chaingrid::Grid grid = mGetGrid();
    mHoverPortNode = mHoverSrc = mHoverDst = -1;
    float best = 7.f;
    _ForEachPort(grid, [&](int node, bool isOut, float px, float py) {
      const float d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
      if (d < best)
      {
        best = d;
        mHoverPortNode = node;
        mHoverPortIsOut = isOut;
      }
    });
    if (mHoverPortNode < 0)
    {
      best = 5.f;
      _ForEachEdge(grid, [&](int src, int dst) {
        const auto pts = mGeom.Route(grid, src, dst);
        for (size_t i = 0; i + 1 < pts.size(); i++)
        {
          const float d =
            _DistToSegment(x, y, pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second);
          if (d < best)
          {
            best = d;
            mHoverSrc = src;
            mHoverDst = dst;
          }
        }
      });
    }
    const char* tip = mHoverPortNode >= 0 ? "Drag to a block to add a cable (or remove one that's there)"
                      : mHoverSrc >= 0    ? "Click to remove this cable"
                                          : "";
    if (mLastTip != tip)
    {
      SetTooltip(tip);
      mLastTip = tip;
    }
  }

  NAMChainGridGeometry mGeom;
  std::function<chaingrid::Grid()> mGetGrid;
  std::function<void(const chaingrid::Grid&)> mSetGrid;
  int mPreviewCell = -1;
  int mHoverPortNode = -1;
  bool mHoverPortIsOut = false;
  int mHoverSrc = -1, mHoverDst = -1;
  std::function<void()> mOutClick; // See SetOutClick
  // Where the pointer last hovered over the grid (see OnMouseDown: a cable is only cut when aimed at).
  float mOverX = 0.f, mOverY = 0.f;
  bool mOverValid = false;
  bool mDraggingCable = false;
  int mDragNode = -1;
  bool mDragFromOut = true;
  float mMouseX = 0.f, mMouseY = 0.f;
  const char* mLastTip = nullptr;
};

/// A vertical scrollbar for a list taller than its view (the AMP NEST pedal list): drag the thumb,
/// click the track to jump there, or use the mouse wheel over it. Scroll position is in pixels and
/// lives with the caller (get/set), so the list's contents can follow it live.
class NAMScrollBarControl : public IControl
{
public:
  NAMScrollBarControl(const IRECT& bounds, float contentH, float viewH, float wheelStep,
                      std::function<float()> getScroll, std::function<void(float)> setScroll)
  : IControl(bounds)
  , mContentH(contentH)
  , mViewH(viewH)
  , mWheelStep(wheelStep)
  , mGetScroll(getScroll)
  , mSetScroll(setScroll)
  {
  }

  float MaxScroll() const { return std::max(0.f, mContentH - mViewH); }

  void Draw(IGraphics& g) override
  {
    const float w = mRECT.W();
    g.FillRoundRect(PluginColors::NAM_INK.WithOpacity(0.07f), mRECT, w * 0.5f);
    const IRECT thumb = _Thumb();
    const float alpha = mDragging ? 1.f : (mMouseIsOver ? 0.9f : 0.7f);
    g.FillRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(alpha), thumb.GetHPadded(-1.f), (w - 2.f) * 0.5f);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const IRECT thumb = _Thumb();
    if (!thumb.Contains(x, y))
    {
      // Jump so the thumb is centred on the click, then drag from there.
      _SetFromThumbTop(y - thumb.H() * 0.5f);
    }
    mDragging = true;
    mDragStartY = y;
    mDragStartScroll = mGetScroll();
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    const float travel = mRECT.H() - _ThumbH();
    if (travel <= 0.f)
      return;
    _Set(mDragStartScroll + (y - mDragStartY) * MaxScroll() / travel);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    mDragging = false;
    SetDirty(false);
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override { Scroll(-d * mWheelStep); }

  // Scrolls by `delta` pixels (positive = down the list).
  void Scroll(float delta) { _Set(mGetScroll() + delta); }

private:
  float _ThumbH() const
  {
    const float h = mContentH > 0.f ? mRECT.H() * mViewH / mContentH : mRECT.H();
    return std::clamp(h, std::min(24.f, mRECT.H()), mRECT.H());
  }

  IRECT _Thumb() const
  {
    const float h = _ThumbH(), maxScroll = MaxScroll();
    const float t = mRECT.T + (maxScroll > 0.f ? (mRECT.H() - h) * mGetScroll() / maxScroll : 0.f);
    return IRECT(mRECT.L, t, mRECT.R, t + h);
  }

  void _SetFromThumbTop(float top)
  {
    const float travel = mRECT.H() - _ThumbH();
    if (travel > 0.f)
      _Set((top - mRECT.T) * MaxScroll() / travel);
  }

  void _Set(float scroll)
  {
    scroll = std::clamp(scroll, 0.f, MaxScroll());
    if (scroll != mGetScroll())
    {
      mSetScroll(scroll);
      SetDirty(false);
    }
  }

  float mContentH, mViewH, mWheelStep;
  std::function<float()> mGetScroll;
  std::function<void(float)> mSetScroll;
  bool mDragging = false;
  float mDragStartY = 0.f, mDragStartScroll = 0.f;
};

/// Invisible; passes the mouse wheel over an area (e.g. a list's empty rows) on to a scrollbar.
class NAMWheelForwardControl : public IControl
{
public:
  NAMWheelForwardControl(const IRECT& bounds, NAMScrollBarControl* bar)
  : IControl(bounds)
  , mBar(bar)
  {
  }
  void Draw(IGraphics& g) override {}
  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override { mBar->OnMouseWheel(x, y, mod, d); }

private:
  NAMScrollBarControl* mBar;
};

/// Full-window dim layer; click dismisses (used for Slim overlay).
class NAMSlimOverlayBackdropControl : public IControl
{
public:
  // iPlug2's corner resizer (IGraphics::AttachCornerResizer) lives outside the normal control
  // stack and is only ever consulted as a hit-test fallback -- if any regular control's rect
  // covers that corner (like this one, when it spans the whole window), the resizer never gets a
  // chance and dragging that corner just dismisses the overlay instead of resizing the window.
  // Insetting a small margin off the bottom-right corner keeps it clickable at all times, at the
  // cost of a sliver along those two edges not being dimmed -- a fair trade for the window always
  // staying resizable, popup or no popup.
  static constexpr float kCornerResizerMargin = 24.f;

  NAMSlimOverlayBackdropControl(const IRECT& bounds, IActionFunction dismiss, float dimOpacity = 0.45f)
  : IControl(bounds.GetReducedFromRight(kCornerResizerMargin).GetReducedFromBottom(kCornerResizerMargin), dismiss)
  , mDismiss(dismiss)
  , mDimOpacity(dimOpacity)
  , mCurrentOpacity(dimOpacity)
  {
  }

  void Draw(IGraphics& g) override { g.FillRect(COLOR_BLACK.WithOpacity(mCurrentOpacity), mRECT); }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    // A click on the popup itself (an empty bit of its card -- the card doesn't take clicks, so
    // they land here) mustn't close it; only a click outside it does.
    if (mKeepOpenArea && mKeepOpenArea().Contains(x, y))
      return;
    if (mDismiss)
      mDismiss(this);
  }

  // The area (the popup's card) where a click doesn't dismiss.
  void SetKeepOpenArea(std::function<IRECT()> f) { mKeepOpenArea = f; }

  // Adjusts the target dim strength -- a bigger, more modal-like popup (the Amp's) wants a
  // stronger dim than a small popover (a pedal's), so the background it's covering reads as
  // clearly "behind" rather than competing visually with the popup's own content.
  void SetDimOpacity(float dimOpacity)
  {
    mDimOpacity = dimOpacity;
    mCurrentOpacity = dimOpacity;
  }

  // Fades the dim in from transparent, in step with a popup card's AnimateGrowFrom, so the whole
  // window doesn't snap dark all at once (feels like a full-screen takeover) -- it settles in
  // alongside the card zooming into place instead.
  void AnimateFadeIn(int durationMs = 160)
  {
    mCurrentOpacity = 0.f;
    SetAnimation(
      [this](IControl* pCaller) {
        const double t = GetAnimationProgress();
        if (t > 1.)
        {
          mCurrentOpacity = mDimOpacity;
          pCaller->OnEndAnimation();
          return;
        }
        mCurrentOpacity = mDimOpacity * static_cast<float>(t);
        SetDirty(false);
      },
      durationMs);
  }

private:
  IActionFunction mDismiss;
  std::function<IRECT()> mKeepOpenArea;
  float mDimOpacity;
  float mCurrentOpacity;
};

class NAMKnobControl : public IVKnobControl, public IBitmapBase
{
public:
  NAMKnobControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, IBitmap bitmap)
  : IVKnobControl(bounds, paramIdx, label, style, true)
  , IBitmapBase(bitmap)
  {
    mInnerPointerFrac = 0.55;
  }

  void OnRescale() override { mBitmap = GetUI()->GetScaledBitmap(mBitmap); }

  // Side layout (1.38.1, the Tube Lab): the dial on the left, as tall as the whole cell, with the
  // name and value stacked to its right. The default (name above, value below) left a 47 px Tube Lab
  // cell a ~10 px dial, a dot, at every window size (the window scales as a whole).
  // Call before attaching (attaching runs OnResize). Re-laying out here before then crashed on launch:
  // SetDirty needs the UI.
  void SetSideText(bool sideText)
  {
    mSideText = sideText;
    if (GetUI())
      OnResize();
  }

  void OnResize() override
  {
    if (!mSideText)
    {
      IVKnobControl::OnResize();
      return;
    }
    const IRECT r = mRECT;
    const float side = std::min(r.H(), r.W() * 0.45f);
    mWidgetBounds = r.GetFromLeft(side).GetCentredInside(side, side);
    const IRECT text = r.GetReducedFromLeft(side + 4.f);
    mLabelBounds = text.GetFromTop(text.H() * 0.5f);
    mValueBounds = text.GetFromBottom(text.H() * 0.5f);
    SetTargetRECT(r);
    SetDirty(false);
  }

  void DrawWidget(IGraphics& g) override
  {
    float widgetRadius = GetRadius() * 0.73;
    // Square the knob off the SHORTER side of the widget bounds. Using W() alone made the knob
    // overflow vertically whenever its cell was wider than tall (the amp control grid's cells are),
    // painting over the label above it and the value below it.
    const float knobSide = std::min(mWidgetBounds.W(), mWidgetBounds.H());
    auto knobRect = mWidgetBounds.GetCentredInside(knobSide, knobSide);
    const float cx = knobRect.MW(), cy = knobRect.MH();
    const float angle = mAngle1 + (static_cast<float>(GetValue()) * (mAngle2 - mAngle1));
    DrawIndicatorTrack(g, angle, cx + 0.5, cy, widgetRadius);
    g.DrawFittedBitmap(mBitmap, knobRect);
    float data[2][2];
    RadialPoints(angle, cx, cy, mInnerPointerFrac * widgetRadius, mInnerPointerFrac * widgetRadius, 2, data);
    g.PathCircle(data[1][0], data[1][1], 3);
    g.PathFill(IPattern::CreateRadialGradient(data[1][0], data[1][1], 4.0f,
                                              {{GetColor(mMouseIsOver ? kX3 : kX1), 0.f},
                                               {GetColor(mMouseIsOver ? kX3 : kX1), 0.8f},
                                               {COLOR_TRANSPARENT, 1.0f}}),
               {}, &mBlend);
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.5f), data[1][0], data[1][1], 3, &mBlend);
  }

private:
  bool mSideText = false;
};

class NAMSwitchControl : public IVSlideSwitchControl, public IBitmapBase
{
public:
  NAMSwitchControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, IBitmap bitmap)
  : IVSlideSwitchControl(bounds, paramIdx, label,
                         style.WithRoundness(0.666f)
                           .WithShowValue(false)
                           .WithEmboss(true)
                           .WithShadowOffset(1.5f)
                           .WithDrawShadows(false)
                           .WithColor(kFR, COLOR_BLACK)
                           .WithFrameThickness(0.5f)
                           .WithWidgetFrac(0.5f)
                           .WithLabelOrientation(EOrientation::South))
  , IBitmapBase(bitmap)
  {
  }

  // Drawn by hand as a plain toggle: a rounded track with a round knob -- right and lit in the
  // theme color when on, left and grey when off -- and the label underneath, clear of it. (The
  // old drawing put the handle bitmap at a fixed size against the top of the track and let the
  // label run into the pill.)
  void Draw(IGraphics& g) override
  {
    const bool on = GetValue() > 0.5;
    const bool hasLabel = mLabelStr.GetLength() > 0;
    const float labelH = hasLabel ? std::clamp(mRECT.H() * 0.4f, 11.f, 16.f) : 0.f;
    const IRECT body = mRECT.GetReducedFromBottom(labelH);
    const float trackH = std::clamp(std::min(body.H() - 4.f, body.W() / 2.f), 10.f, 20.f);
    const IRECT track = body.GetCentredInside(trackH * 1.9f, trackH);
    const float r = trackH * 0.5f;

    g.FillRoundRect(on ? PluginColors::NAM_THEMECOLOR : PluginColors::NAM_SURFACE, track, r);
    g.DrawRoundRect(COLOR_WHITE.WithOpacity(on ? 0.25f : 0.15f), track, r, nullptr, 1.f);
    if (mMouseIsOver)
      g.FillRoundRect(COLOR_WHITE.WithOpacity(0.08f), track, r);

    const float knobR = r - 2.5f;
    const float cx = on ? track.R - r : track.L + r;
    g.FillCircle(COLOR_BLACK.WithOpacity(0.3f), cx, track.MH() + 1.f, knobR);
    g.FillCircle(on ? COLOR_WHITE : IColor(255, 150, 148, 145), cx, track.MH(), knobR);

    if (hasLabel)
    {
      const IText text(labelH * 0.85f, on ? PluginColors::NAM_THEMEFONTCOLOR
                                          : PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.55f));
      IVectorBase::DrawTextFitted(g, text, mLabelStr.Get(), IRECT(mRECT.L - 10.f, mRECT.B - labelH, mRECT.R + 10.f, mRECT.B), nullptr);
    }
  }
};

class NAMFileNameControl : public IVButtonControl
{
public:
  NAMFileNameControl(const IRECT& bounds, const char* label, const IVStyle& style)
  : IVButtonControl(bounds, DefaultClickActionFunc, label, style)
  {
  }

  void SetLabelAndTooltip(const char* str)
  {
    SetLabelStr(str);
    SetTooltip(str);
  }

  void SetLabelAndTooltipEllipsizing(const WDL_String& fileName)
  {
    auto EllipsizeFilePath = [](const char* filePath, size_t prefixLength, size_t suffixLength, size_t maxLength) {
      const std::string ellipses = "...";
      assert(maxLength <= (prefixLength + suffixLength + ellipses.size()));
      std::string str{filePath};

      if (str.length() <= maxLength)
      {
        return str;
      }
      else
      {
        return str.substr(0, prefixLength) + ellipses + str.substr(str.length() - suffixLength);
      }
    };

    // The whole name: IVectorBase::DrawLabel now fits it to the control's width (shrinking it, then
    // shortening the middle), so a fixed character cut-off would only hide text that fits.
    (void)EllipsizeFilePath;
    SetLabelStr(fileName.get_filepart());
    SetTooltip(fileName.get_filepart());
  }
};

// URL control for the "Get" models/irs links
class NAMGetButtonControl : public NAMSquareButtonControl
{
public:
  NAMGetButtonControl(const IRECT& bounds, const char* label, const char* url, const ISVG& globeSVG)
  : NAMSquareButtonControl(
      bounds,
      [url](IControl* pCaller) {
        WDL_String fullURL(url);
        auto* ui = pCaller->GetUI();
        ui->ReleaseMouseCapture();
        ui->OpenURL(fullURL.Get());
      },
      globeSVG)
  {
    SetTooltip(label);
  }
};

class NAMFileBrowserControl : public IDirBrowseControlBase
{
public:
  NAMFileBrowserControl(const IRECT& bounds, int clearMsgTag, const char* labelStr, const char* fileExtension,
                        IFileDialogCompletionHandlerFunc ch, const IVStyle& style, const ISVG& loadSVG,
                        const ISVG& clearSVG, const ISVG& leftSVG, const ISVG& rightSVG, const IBitmap& bitmap,
                        const ISVG& globeSVG, const char* getButtonLabel, const char* getButtonURL,
                        bool alwaysPromptDialog = false)
  : IDirBrowseControlBase(bounds, fileExtension, false, false)
  , mClearMsgTag(clearMsgTag)
  , mDefaultLabelStr(labelStr)
  , mCompletionHandlerFunc(ch)
  , mStyle(style.WithColor(kFG, COLOR_TRANSPARENT).WithDrawFrame(false))
  , mBitmap(bitmap)
  , mLoadSVG(loadSVG)
  , mClearSVG(clearSVG)
  , mLeftSVG(leftSVG)
  , mRightSVG(rightSVG)
  , mGlobeSVG(globeSVG)
  , mGetButtonLabel(getButtonLabel)
  , mGetButtonURL(getButtonURL)
  , mBrowserState(NAMBrowserState::Empty)
  , mAlwaysPromptDialog(alwaysPromptDialog)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override { g.DrawFittedBitmap(mBitmap, mRECT); }

  // IContainerBase::Hide() unconditionally cascades to every child, which would clobber
  // SetBrowserState's careful "only one of Clear/Get is ever visible" bookkeeping any time this
  // control (or an ancestor, e.g. a popup that hides/shows it) is un-hidden -- re-apply it
  // afterward so the two don't both end up visible at once.
  void Hide(bool hide) override
  {
    IDirBrowseControlBase::Hide(hide);
    if (!hide)
      SetBrowserState(mBrowserState);
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu)
    {
      IPopupMenu::Item* pItem = pSelectedMenu->GetChosenItem();

      if (pItem)
      {
        mSelectedItemIndex = mItems.Find(pItem);
        LoadFileAtCurrentIndex(NAMFileLoadSource::ExistingBrowserSelection);
      }
    }
  }

  void OnAttached() override
  {
    auto prevFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex--;

      if (mSelectedItemIndex < 0)
        mSelectedItemIndex = nItems - 1;

      LoadFileAtCurrentIndex(NAMFileLoadSource::ExistingBrowserSelection);
    };

    auto nextFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex++;

      if (mSelectedItemIndex >= nItems)
        mSelectedItemIndex = 0;

      LoadFileAtCurrentIndex(NAMFileLoadSource::ExistingBrowserSelection);
    };

    auto loadFileFunc = [&](IControl* pCaller) {
      WDL_String fileName;
      WDL_String path;
      GetSelectedFileDirectory(path);
#ifdef NAM_PICK_DIRECTORY
      pCaller->GetUI()->PromptForDirectory(path, [&](const WDL_String& fileName, const WDL_String& path) {
        if (path.GetLength())
        {
          ClearPathList();
          AddPath(path.Get(), "");
          SetupMenu();
          SelectFirstFile();
          LoadFileAtCurrentIndex(NAMFileLoadSource::ExistingBrowserSelection);
        }
      });
#else
      pCaller->GetUI()->PromptForFile(
        fileName, path, EFileAction::Open, mExtension.Get(), [&](const WDL_String& fileName, const WDL_String& path) {
          if (fileName.GetLength())
          {
            ClearPathList();
            AddPath(path.Get(), "");
            SetupMenu();
            SetSelectedFile(fileName.Get());
            LoadFileAtCurrentIndex(NAMFileLoadSource::FilePickerSelection, &fileName, &path);
          }
        });
#endif
    };

    auto clearFileFunc = [&](IControl* pCaller) {
      pCaller->GetDelegate()->SendArbitraryMsgFromUI(mClearMsgTag);
      mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
      SetBrowserState(NAMBrowserState::Empty);
      // FIXME disabling output mode...
      //      pCaller->GetUI()->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
    };

    auto chooseFileFunc = [&, loadFileFunc](IControl* pCaller) {
      if (mAlwaysPromptDialog
          || std::string_view(pCaller->As<IVButtonControl>()->GetLabelStr()) == mDefaultLabelStr.Get())
      {
        loadFileFunc(pCaller);
      }
      else
      {
        CheckSelectedItem();

        if (!mMainMenu.HasSubMenus())
        {
          mMainMenu.SetChosenItemIdx(mSelectedItemIndex);
        }
        pCaller->GetUI()->CreatePopupMenu(*this, mMainMenu, pCaller->GetRECT());
      }
    };

    IRECT padded = mRECT.GetPadded(-6.f).GetHPadded(-2.f);
    const auto buttonWidth = padded.H();
    const auto loadFileButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto clearAndGetButtonBounds = padded.ReduceFromRight(buttonWidth);
    const auto leftButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto rightButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto fileNameButtonBounds = padded;

    AddChildControl(new NAMSquareButtonControl(loadFileButtonBounds, DefaultClickActionFunc, mLoadSVG))
      ->SetAnimationEndActionFunction(loadFileFunc);
    AddChildControl(new NAMSquareButtonControl(leftButtonBounds, DefaultClickActionFunc, mLeftSVG))
      ->SetAnimationEndActionFunction(prevFileFunc);
    AddChildControl(new NAMSquareButtonControl(rightButtonBounds, DefaultClickActionFunc, mRightSVG))
      ->SetAnimationEndActionFunction(nextFileFunc);
    AddChildControl(mFileNameControl = new NAMFileNameControl(fileNameButtonBounds, mDefaultLabelStr.Get(), mStyle))
      ->SetAnimationEndActionFunction(chooseFileFunc);

    // creates both right-side controls but only show one based on state
    mClearButton = new NAMSquareButtonControl(clearAndGetButtonBounds, DefaultClickActionFunc, mClearSVG);
    mClearButton->SetAnimationEndActionFunction(clearFileFunc);
    AddChildControl(mClearButton);

    mGetButton = new NAMGetButtonControl(clearAndGetButtonBounds, mGetButtonLabel, mGetButtonURL, mGlobeSVG);
    AddChildControl(mGetButton);

    // initialize control visibility
    SetBrowserState(NAMBrowserState::Empty);
  }

  void LoadFileAtCurrentIndex(NAMFileLoadSource source, const WDL_String* filePickerFileName = nullptr,
                              const WDL_String* filePickerPath = nullptr)
  {
    if (source == NAMFileLoadSource::FilePickerSelection && mSelectedItemIndex == -1 && filePickerFileName != nullptr
        && filePickerPath != nullptr)
    {
      ReportDirectoryScanFailure(*filePickerFileName, *filePickerPath);
      return;
    }

    if (mSelectedItemIndex < 0 || mSelectedItemIndex >= NItems())
    {
      ReportUnexpectedLoadFailure(source, filePickerFileName);
      return;
    }

    WDL_String fileName, path;
    GetSelectedFile(fileName);
    mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
    mCompletionHandlerFunc(fileName, path);
  }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    switch (msgTag)
    {
      case kMsgTagLoadFailed:
        // Honestly, not sure why I made a big stink of it before. Why not just say it failed and move on? :)
        {
          std::string label(std::string("(FAILED) ") + std::string(mFileNameControl->GetLabelStr()));
          mFileNameControl->SetLabelAndTooltip(label.c_str());
          SetBrowserState(NAMBrowserState::Empty);
        }
        break;
      case kMsgTagLoadedModel:
      case kMsgTagLoadedIR:
      {
        WDL_String fileName, directory;
        fileName.Set(reinterpret_cast<const char*>(pData));
        directory.Set(reinterpret_cast<const char*>(pData));
        directory.remove_filepart(true);

        ClearPathList();
        AddPath(directory.Get(), "");
        SetupMenu();
        SetSelectedFile(fileName.Get());
        mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
        SetBrowserState(NAMBrowserState::Loaded);
      }
      break;
      case kMsgTagClearedDisplay:
        mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
        SetBrowserState(NAMBrowserState::Empty);
        break;
      default: break;
    }
  }

private:
  void ReportDirectoryScanFailure(const WDL_String& fileName, const WDL_String& path)
  {
    mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
    const std::string label = std::string("(FAILED) ") + mFileNameControl->GetLabelStr();

    std::stringstream message;
    message << "The selected file '" << fileName.Get() << "' was not found after scanning directory '" << path.Get()
            << "'. The host may not have granted permission to enumerate that directory.";

    mFileNameControl->SetLabelStr(label.c_str());
    mFileNameControl->SetTooltip(message.str().c_str());
    SetBrowserState(NAMBrowserState::Empty);
    std::fprintf(stderr, "NAM: File picker selection produced index -1. %s\n", message.str().c_str());
  }

  void ReportUnexpectedLoadFailure(NAMFileLoadSource source, const WDL_String* filePickerFileName)
  {
    if (filePickerFileName != nullptr)
      mFileNameControl->SetLabelAndTooltipEllipsizing(*filePickerFileName);

    const std::string label = std::string("(FAILED) ") + mFileNameControl->GetLabelStr();
    const std::string message =
      "The selected file could not be loaded because the file browser encountered an "
      "unexpected selection state. Please select the file again.";

    mFileNameControl->SetLabelStr(label.c_str());
    mFileNameControl->SetTooltip(message.c_str());
    SetBrowserState(NAMBrowserState::Empty);
    std::fprintf(stderr, "NAM: %s Source: %s; selected index: %d; item count: %d.\n", message.c_str(),
                 source == NAMFileLoadSource::FilePickerSelection ? "file picker" : "existing browser selection",
                 mSelectedItemIndex, NItems());
  }

  void SelectFirstFile() { mSelectedItemIndex = mFiles.GetSize() ? 0 : -1; }

  void GetSelectedFileDirectory(WDL_String& path)
  {
    GetSelectedFile(path);
    path.remove_filepart();
    return;
  }

  // set the state of the browser and the visibility of the "Get" vs. "Clear" buttons
  void SetBrowserState(NAMBrowserState newState)
  {
    mBrowserState = newState;

    // While the browser itself is hidden (e.g. the amp popup it lives in is closed), keep both
    // hidden: a model or IR loading then would otherwise show its button floating on the main
    // screen, over the AMP NEST panel. Hide(false) re-applies the state when it's shown.
    const bool shown = !IsHidden();
    switch (mBrowserState)
    {
      case NAMBrowserState::Empty:
        mClearButton->Hide(true);
        mGetButton->Hide(!shown);
        break;
      case NAMBrowserState::Loaded:
        mClearButton->Hide(!shown);
        mGetButton->Hide(true);
        break;
    }
  }

  WDL_String mDefaultLabelStr;
  IFileDialogCompletionHandlerFunc mCompletionHandlerFunc;
  NAMFileNameControl* mFileNameControl = nullptr;
  IVStyle mStyle;
  IBitmap mBitmap;
  ISVG mLoadSVG, mClearSVG, mLeftSVG, mRightSVG, mGlobeSVG;
  int mClearMsgTag;

  // new members for the "Get" button
  const char* mGetButtonLabel;
  const char* mGetButtonURL;
  NAMBrowserState mBrowserState;
  NAMSquareButtonControl* mClearButton = nullptr;
  NAMGetButtonControl* mGetButton = nullptr;
  bool mAlwaysPromptDialog;
};

/// A persistent library panel button: pre-populated from a fixed list of folders (e.g. a Tone3000 download
/// folder), opens a popup menu of matching files on click, and loads the selection directly -- no OS file
/// picker dialog required.
class NAMPresetBrowserControl : public IDirBrowseControlBase
{
public:
  NAMPresetBrowserControl(const IRECT& bounds, const char* labelStr, const char* fileExtension,
                          const std::vector<std::string>& sourcePaths, bool scanRecursively,
                          IFileDialogCompletionHandlerFunc ch, const IVStyle& style)
  : IDirBrowseControlBase(bounds, fileExtension, false, scanRecursively, false)
  , mLabelStr(labelStr)
  , mCompletionHandlerFunc(ch)
  , mStyle(style)
  , mSourcePaths(sourcePaths)
  {
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu)
    {
      IPopupMenu::Item* pItem = pSelectedMenu->GetChosenItem();
      if (pItem)
      {
        mSelectedItemIndex = mItems.Find(pItem);
        WDL_String fileName;
        GetSelectedFile(fileName);
        if (fileName.GetLength())
        {
          mButton->SetLabelStr((mLabelStr + ": " + fileName.get_filepart()).c_str());
          WDL_String path;
          mCompletionHandlerFunc(fileName, path);
        }
      }
    }
  }

  void OnAttached() override
  {
    for (const auto& p : mSourcePaths)
      AddPath(p.c_str(), "");
    SetupMenu();

    std::stringstream label;
    label << mLabelStr << " (" << NItems() << ")";

    auto browseFunc = [&](IControl* pCaller) {
      if (NItems() == 0)
        return;
      CheckSelectedItem();
      if (!mMainMenu.HasSubMenus())
        mMainMenu.SetChosenItemIdx(mSelectedItemIndex);
      pCaller->GetUI()->CreatePopupMenu(*this, mMainMenu, pCaller->GetRECT());
    };

    mButton = new IVButtonControl(mRECT, browseFunc, label.str().c_str(), mStyle, true, false);
    AddChildControl(mButton);
  }

private:
  std::string mLabelStr;
  IFileDialogCompletionHandlerFunc mCompletionHandlerFunc;
  IVStyle mStyle;
  std::vector<std::string> mSourcePaths;
  IVButtonControl* mButton = nullptr;
};

/// Chromatic tuner readout: note name + a +/-50 cents meter. Fed by kMsgTagTunerUpdate messages
/// from the DSP (a single double, the detected frequency in Hz, or 0.0 for "no pitch detected").
class NAMTunerDisplayControl : public IControl
{
public:
  NAMTunerDisplayControl(const IRECT& bounds, const IVStyle& style)
  : IControl(bounds)
  , mStyle(style)
  {
    mIgnoreMouse = true;
  }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    // 1.29.1: two doubles, the frequency (Hz, 0 = none) and the input RMS (linear).
    if (msgTag == kMsgTagTunerUpdate && dataSize == (int)(2 * sizeof(double)))
    {
      double data[2];
      memcpy(data, pData, sizeof(data));
      mFreqHz = data[0];
      mInputDB = data[1] > 1e-7 ? 20.0 * std::log10(data[1]) : -140.0;
      SetDirty(false);
    }
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.10f), mRECT, 12.f);

    if (mFreqHz <= 0.0)
    {
      const IText waitingText(22, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.6f), "Roboto-Regular");
      g.DrawText(waitingText, "Play a note...", mRECT);
      // Input level, so "no signal" and "too quiet" can be told apart from "can't find a pitch".
      char levelStr[48];
      if (mInputDB <= -100.0)
        snprintf(levelStr, sizeof(levelStr), "Input: no signal");
      else
        snprintf(levelStr, sizeof(levelStr), "Input: %.0f dB", mInputDB);
      const IText levelText(14, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.45f), "Roboto-Regular");
      g.DrawText(levelText, levelStr, mRECT.GetFromBottom(mRECT.H() * 0.3f));
      return;
    }

    static const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const double midiFloat = 69.0 + 12.0 * std::log2(mFreqHz / 440.0);
    const int midiNote = (int)std::round(midiFloat);
    const double cents = (midiFloat - midiNote) * 100.0;
    const int noteIdx = ((midiNote % 12) + 12) % 12;
    const int octave = midiNote / 12 - 1;

    std::stringstream noteStr;
    noteStr << kNoteNames[noteIdx] << octave;

    const bool inTune = std::abs(cents) < 5.0;
    const IColor noteColor = inTune ? IColor(255, 90, 220, 120) : PluginColors::NAM_THEMEFONTCOLOR;

    const auto noteArea = mRECT.GetFromTop(mRECT.H() * 0.55f);
    const IText noteText(48, noteColor, "Michroma-Regular");
    g.DrawText(noteText, noteStr.str().c_str(), noteArea);

    // Cents meter: a horizontal track with a moving indicator, +/-50 cents full-scale.
    const auto meterArea = mRECT.GetFromBottom(mRECT.H() * 0.3f).GetPadded(-20.f, 0.f, -20.f, -10.f);
    g.FillRect(COLOR_BLACK.WithOpacity(0.3f), meterArea);
    g.DrawLine(PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.5f), meterArea.MW(), meterArea.T, meterArea.MW(),
               meterArea.B);
    const double clampedCents = cents < -50.0 ? -50.0 : (cents > 50.0 ? 50.0 : cents);
    const float indicatorX = static_cast<float>(meterArea.MW() + (clampedCents / 50.0) * (meterArea.W() * 0.5));
    g.FillCircle(inTune ? IColor(255, 90, 220, 120) : PluginColors::NAM_THEMECOLOR, indicatorX, meterArea.MH(), 8.f);
  }

private:
  IVStyle mStyle;
  double mFreqHz = 0.0;
  double mInputDB = -140.0;
};

/// Simple toggleable overlay panel: background + close button, children added by the caller.
/// Used as the base look for the Tuner and Pedalboard panels.
class NAMOverlayPanelControl : public IContainerBase
{
public:
  NAMOverlayPanelControl(const IRECT& bounds, const char* title, const ISVG& closeSVG)
  : IContainerBase(bounds)
  , mTitle(title)
  , mCloseSVG(closeSVG)
  {
  }

  void OnAttached() override
  {
    const auto b = GetRECT();
    AddChildControl(new NAMPanelControl(b, PluginColors::NAM_PANEL));
    const IVStyle titleStyle =
      DEFAULT_STYLE.WithValueText(IText(26, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular")).WithDrawFrame(false);
    // Tall enough for the 26 px title (GetPadded(-20) on a 50 px strip left 10 px, which cut it off).
    AddChildControl(
      new IVLabelControl(b.GetFromTop(50.f).GetPadded(-20.f, -4.f, -20.f, -4.f), mTitle.c_str(), titleStyle));
    const auto closeArea = b.GetFromTRHC(50, 50).GetCentredInside(20, 20);
    AddChildControl(new NAMCircleButtonControl(
      closeArea, [this](IControl* pCaller) { Hide(true); }, mCloseSVG));
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      Hide(true);
      return true;
    }
    return false;
  }

  // While the page is open, the controls entirely underneath it are hidden, so they aren't drawn
  // at all. Drawn under an opaque page, they still bled through as faint lines along the edges of
  // redrawn regions at some window sizes (1.32.2). Only the controls this page hid are shown again.
  void Hide(bool hide) override
  {
    if (!hide && !mCovering)
      _HideCovered();
    IContainerBase::Hide(hide);
    if (hide && mCovering)
      _RestoreCovered();
  }

private:
  void _HideCovered()
  {
    IGraphics* pGraphics = GetUI();
    if (!pGraphics)
      return;
    mCovering = true;
    mCovered.clear();
    const IRECT bounds = GetRECT();
    // Control 0 is always drawn (the window background); everything from this page on is the page
    // itself or drawn on top of it.
    for (int i = 1; i < pGraphics->NControls(); i++)
    {
      IControl* pControl = pGraphics->GetControl(i);
      if (pControl == this)
        break;
      if (!pControl->IsHidden() && bounds.Contains(pControl->GetRECT()))
      {
        // The base Hide: a container's override would also hide (and later show) its children,
        // including ones that were already hidden.
        pControl->IControl::Hide(true);
        mCovered.push_back(pControl);
      }
    }
  }

  void _RestoreCovered()
  {
    for (IControl* pControl : mCovered)
      pControl->IControl::Hide(false);
    mCovered.clear();
    mCovering = false;
  }

  std::string mTitle;
  ISVG mCloseSVG;
  bool mCovering = false;
  std::vector<IControl*> mCovered; // Not owned; a relayout deletes them together with this page
};

/// Tuner overlay: title/close chrome (from NAMOverlayPanelControl) + the note/cents readout.
class NAMTunerPageControl : public NAMOverlayPanelControl
{
public:
  NAMTunerPageControl(const IRECT& bounds, const ISVG& closeSVG, const IVStyle& style)
  : NAMOverlayPanelControl(bounds, "TUNER", closeSVG)
  , mStyle(style)
  {
  }

  void OnAttached() override
  {
    NAMOverlayPanelControl::OnAttached();
    const auto displayArea = GetRECT().GetPadded(-40.f).GetReducedFromTop(60.f).GetMidVPadded(110.f);
    AddChildControl(new NAMTunerDisplayControl(displayArea, mStyle), kCtrlTagTunerDisplay);
  }

private:
  IVStyle mStyle;
};

/// A draggable pedal "chip" in the chain rack. Purely visual/interaction -- doesn't know about
/// param indices itself; the panel supplies an active-state getter, a position getter, a selected
/// getter, a drop callback, and a select callback. Self-syncs to the position getter every draw
/// (unless mid-drag), so it stays correct whether Active/Slot changed via dragging OR the separate
/// on/off switch. A plain click (mouse down+up with negligible movement) selects the pedal instead
/// of being treated as a drag, so its controls can be shown only while it's selected.
// Plots the Tube Lab's static transfer curve (input level -> output) for the current Drive/Bias,
// against a dashed straight line for reference. The curve is scaled to fill the plot so its shape
// stays readable at any Drive. Call SetDimmed()/SetDirty() when the params it depends on change.
class TubeCurveControl : public IControl
{
public:
  TubeCurveControl(const IRECT& bounds, std::function<double(double)> curve)
  : IControl(bounds)
  , mCurve(std::move(curve))
  {
    SetIgnoreMouse(true);
  }

  void Draw(IGraphics& g) override
  {
    const IRECT plot = mRECT.GetPadded(-6.f);
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.25f), mRECT, 4.f);
    const IColor gridColor = PluginColors::NAM_THEMECOLOR.WithOpacity(0.15f);
    g.DrawLine(gridColor, plot.L, plot.MH(), plot.R, plot.MH());
    g.DrawLine(gridColor, plot.MW(), plot.T, plot.MW(), plot.B);
    g.DrawDottedLine(PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), plot.L, plot.B, plot.R, plot.T);

    constexpr int kNumPoints = 128;
    double ys[kNumPoints];
    double peak = 1e-9;
    for (int i = 0; i < kNumPoints; i++)
    {
      ys[i] = mCurve(-1.0 + 2.0 * i / (kNumPoints - 1));
      peak = std::max(peak, std::abs(ys[i]));
    }
    const IColor curveColor = mDimmed ? PluginColors::NAM_3.WithOpacity(0.4f) : PluginColors::NAM_THEMECOLOR;
    float prevX = 0.f, prevY = 0.f;
    for (int i = 0; i < kNumPoints; i++)
    {
      const float x = plot.L + plot.W() * i / (kNumPoints - 1);
      const float y = plot.MH() - static_cast<float>(ys[i] / peak) * 0.5f * plot.H();
      if (i > 0)
        g.DrawLine(curveColor, prevX, prevY, x, y, nullptr, 2.f);
      prevX = x;
      prevY = y;
    }

    const IText labelText(11.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.6f), "Roboto-Regular", EAlign::Near,
                          EVAlign::Top);
    g.DrawText(labelText, "TRANSFER CURVE", plot);
  }

  // Greys the curve out while the stage is switched off.
  void SetDimmed(bool dimmed)
  {
    mDimmed = dimmed;
    SetDirty(false);
  }

private:
  std::function<double(double)> mCurve;
  bool mDimmed = false;
};

// A lab's card (1.60.0): an amp head in tolex with gold piping, its glyph (1 a glass preamp tube,
// 2 a speaker cone), its name, and a jewel lamp lit while it's on. Tall and narrow (a grid cell): the
// glyph on top, the name under it, the lamp at the bottom. Wide (the AMP NEST's slot): a gold
// faceplate with the glyph beside the name, and `subtitle` (if any) along the bottom.
inline void DrawLabCard(IGraphics& g, const IRECT& r, int kind, const std::string& name, bool active, bool selected,
                        bool dragging, const char* subtitle = nullptr, float dim = 1.f)
{
  const IColor gold(255, 214, 170, 90), amber(255, 255, 170, 60);
  const float corner = std::clamp(std::min(r.W(), r.H()) * 0.1f, 3.f, 8.f);
  g.FillRoundRect(dragging ? PluginColors::NAM_THEMECOLOR.WithOpacity(0.9f) : IColor(255, 36, 31, 28).WithOpacity(dim), r,
                  corner);
  for (float y = r.T + 3.f; y < r.B - 2.f; y += 3.f) // Tolex grain
    g.DrawLine(COLOR_WHITE.WithOpacity(0.025f * dim), r.L + 2.f, y, r.R - 2.f, y);
  g.DrawRoundRect(gold.WithOpacity((active ? 0.95f : 0.55f) * dim), r.GetPadded(-1.5f), corner, nullptr, 1.5f);
  if (selected)
    g.DrawRoundRect(COLOR_WHITE, r, corner, nullptr, 2.5f);
  const bool wide = r.W() > r.H() * 1.3f;
  const IRECT inner = r.GetPadded(-std::min(5.f, std::min(r.W(), r.H()) * 0.07f));
  auto glyph = [&](float cx, float cy, float rr) {
    if (kind == 1)
    {
      // Preamp tube: glass, base, plate, and the filament glowing when on.
      if (active)
        g.FillCircle(amber.WithOpacity(0.28f), cx, cy, rr * 1.25f);
      g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.65f * dim), IRECT(cx - rr * 0.55f, cy - rr, cx + rr * 0.55f, cy + rr * 0.8f),
                      rr * 0.5f, nullptr, 1.2f);
      g.FillRect(COLOR_BLACK.WithOpacity(0.6f), IRECT(cx - rr * 0.6f, cy + rr * 0.8f, cx + rr * 0.6f, cy + rr * 1.08f));
      g.DrawRect(COLOR_WHITE.WithOpacity(0.35f * dim), IRECT(cx - rr * 0.3f, cy - rr * 0.55f, cx + rr * 0.3f, cy + rr * 0.35f));
      g.DrawLine(active ? amber : COLOR_WHITE.WithOpacity(0.3f), cx, cy - rr * 0.45f, cx, cy + rr * 0.25f, nullptr,
                 active ? 2.f : 1.f);
    }
    else
    {
      // Speaker: basket, cone rings, dust cap glowing when on.
      if (active)
        g.FillCircle(amber.WithOpacity(0.18f), cx, cy, rr * 1.2f);
      g.FillCircle(COLOR_BLACK.WithOpacity(0.55f), cx, cy, rr);
      g.DrawCircle(gold.WithOpacity(0.75f * dim), cx, cy, rr, nullptr, 1.2f);
      g.DrawCircle(COLOR_WHITE.WithOpacity(0.2f * dim), cx, cy, rr * 0.72f);
      g.DrawCircle(COLOR_WHITE.WithOpacity(0.12f * dim), cx, cy, rr * 0.5f);
      g.FillCircle(active ? amber.WithOpacity(0.85f) : COLOR_WHITE.WithOpacity(0.35f * dim), cx, cy, rr * 0.26f);
    }
  };
  auto fit = [&](const std::string& s, float size, float width) {
    IText t(size, COLOR_WHITE, "Michroma-Regular");
    IRECT m;
    const float w = g.MeasureText(t, s.c_str(), m);
    if (w > width && w > 0.f)
      t.mSize = std::max(6.5f, size * width / w);
    return t;
  };
  const size_t space = name.find(' ');
  const std::string top = space == std::string::npos ? name : name.substr(0, space);
  const std::string bottom = space == std::string::npos ? std::string() : name.substr(space + 1);
  auto lamp = [&](float cx, float cy, float jr) {
    if (active)
      g.FillCircle(amber.WithOpacity(0.35f), cx, cy, jr * 1.9f);
    g.FillCircle(active ? amber : IColor(255, 70, 40, 30), cx, cy, jr);
    g.DrawCircle(gold.WithOpacity(0.6f * dim), cx, cy, jr);
  };
  if (!wide)
  {
    // Grid cell: glyph, name (two lines), lamp.
    const float gh = inner.H() * 0.38f;
    glyph(inner.MW(), inner.T + gh * 0.5f, std::min(gh, inner.W()) * 0.4f);
    const IRECT text(inner.L, inner.T + gh, inner.R, inner.B - inner.H() * 0.2f);
    const float size = std::clamp(text.H() * 0.36f, 6.5f, 12.f);
    IText t = fit(top.size() >= bottom.size() ? top : bottom, size, text.W());
    t.mFGColor = COLOR_WHITE.WithOpacity((active ? 1.f : 0.75f) * dim);
    IVectorBase::DrawTextFitted(g, t, top.c_str(), text.GetFromTop(text.H() * 0.5f), nullptr);
    t.mFGColor = gold.WithOpacity(dim);
    IVectorBase::DrawTextFitted(g, t, bottom.c_str(), text.GetFromBottom(text.H() * 0.5f), nullptr);
    lamp(inner.MW(), inner.B - inner.H() * 0.1f, std::clamp(inner.H() * 0.06f, 2.f, 5.f));
    return;
  }
  // AMP NEST slot: a faceplate with the glyph and name, and the subtitle (or the lamp) under it.
  const IRECT plate = inner.GetFromTop(inner.H() * 0.66f);
  g.FillRoundRect(gold.WithOpacity((active ? 0.3f : 0.15f) * dim), plate, 2.f);
  const float gs = std::min(plate.H(), plate.W() * 0.34f);
  glyph(plate.L + 2.f + gs * 0.5f, plate.MH(), gs * 0.38f);
  const IRECT text = plate.GetReducedFromLeft(gs + 6.f).GetPadded(-2.f);
  IText t = fit(top.size() >= bottom.size() ? top : bottom, std::clamp(text.H() * 0.38f, 8.f, 15.f), text.W());
  t.mFGColor = COLOR_WHITE.WithOpacity((active ? 1.f : 0.8f) * dim);
  IVectorBase::DrawTextFitted(g, t, top.c_str(), text.GetFromTop(text.H() * 0.5f), nullptr);
  t.mFGColor = gold.WithOpacity(dim);
  IVectorBase::DrawTextFitted(g, t, bottom.c_str(), text.GetFromBottom(text.H() * 0.5f), nullptr);
  const IRECT lower = inner.GetReducedFromTop(plate.H());
  const float jr = std::clamp(lower.H() * 0.22f, 2.5f, 5.f);
  lamp(lower.L + jr + 4.f, lower.MH(), jr);
  if (subtitle)
  {
    IText st(9.5f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.75f), "Roboto-Regular", EAlign::Near);
    IVectorBase::DrawTextFitted(g, st, subtitle, lower.GetReducedFromLeft(4.f * jr + 10.f).GetReducedFromRight(4.f), nullptr);
  }
}

class NAMPedalChipControl : public IControl
{
public:
  NAMPedalChipControl(const IRECT& bounds, const char* name, int pedalIdx, const IVStyle& style,
                      std::function<void(int pedalIdx, float dropX, float dropY)> onDropped)
  : IControl(bounds)
  , mName(name)
  , mPedalIdx(pedalIdx)
  , mStyle(style)
  , mOnDropped(onDropped)
  {
  }

  void SetActiveGetter(std::function<bool()> f) { mActiveGetter = f; }
  void SetPositionGetter(std::function<IRECT()> f) { mPositionGetter = f; }
  void SetSelectedGetter(std::function<bool()> f) { mSelectedGetter = f; }
  void SetSelectedFunc(std::function<void(int)> f) { mOnSelected = f; }
  // For pedal types whose identity comes from a loaded file (NAM pedals) rather than a fixed
  // algorithm name -- if set and non-empty, this replaces the static name in the chip's display
  // (and tooltip), so you can actually see which capture is in that slot instead of a generic
  // "CAPTURE 1"/"CAPTURE 2" regardless of what's loaded.
  void SetNameGetter(std::function<std::string()> f) { mNameGetter = f; }
  // Called while the chip is dragged (dragging = true, with the pointer position) and once when it's
  // let go (dragging = false), so the chain grid can show where it would land.
  void SetDragPreviewFunc(std::function<void(float x, float y, bool dragging)> f) { mDragPreview = f; }
  // For a chip in a scrolling list: returns true (with the visible area in `clip`) while the chip
  // should be clipped to it -- it draws only inside it and can't be clicked outside it. Not applied
  // while the chip is being dragged.
  void SetClipGetter(std::function<bool(IRECT& clip)> f) { mClipGetter = f; }
  // Mouse wheel over the chip (e.g. to scroll the list it sits in).
  void SetWheelFunc(std::function<void(float d)> f) { mWheelFunc = f; }
  // 1.60.0: the labs look like amp heads, not stompboxes: 1 TUBE LAB (a glass tube), 2 CAB LAB (a
  // speaker cone). 0 is a pedal.
  void SetLabKind(int kind) { mLabKind = kind; }
  int GetPedalIdx() const { return mPedalIdx; }

  // Moves the chip to where its position getter says, now. Draw does this too, but only runs for a
  // chip whose old position is on screen -- one scrolling into view from off screen needs this.
  void RefreshPosition()
  {
    if (!mDragging && mPositionGetter)
    {
      const IRECT wanted = mPositionGetter();
      if (wanted.L != mRECT.L || wanted.T != mRECT.T || wanted.W() != mRECT.W() || wanted.H() != mRECT.H())
      {
        SetTargetAndDrawRECTs(wanted);
        // Repaint everything: both where the chip was (to erase it) and where it is now.
        if (auto* ui = GetUI())
          ui->SetAllControlsDirty();
      }
    }
  }

  void Draw(IGraphics& g) override
  {
    RefreshPosition();
    IRECT clip;
    const bool clipped = !mDragging && mClipGetter && mClipGetter(clip);
    if (clipped)
    {
      if (clip.Intersect(mRECT).Empty())
        return;
      g.PathClipRegion(clip);
    }
    _DrawChip(g);
    if (clipped)
      g.PathClipRegion();
  }

  bool IsHit(float x, float y) const override
  {
    IRECT clip;
    if (!mDragging && mClipGetter && mClipGetter(clip) && !clip.Contains(x, y))
      return false;
    return IControl::IsHit(x, y);
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    if (mWheelFunc)
      mWheelFunc(d);
  }

private:
  void _DrawChip(IGraphics& g)
  {

    const bool active = mActiveGetter ? mActiveGetter() : false;
    const bool selected = mSelectedGetter ? mSelectedGetter() : false;
    const std::string displayName = _DisplayName();
    if (displayName != mLastTooltipName)
    {
      // Full (untruncated) name in the tooltip -- the on-chip text below gets cut short to fit.
      SetTooltip(displayName.c_str());
      mLastTooltipName = displayName;
    }
    if (mLabKind != 0)
    {
      DrawLabCard(g, mRECT, mLabKind, displayName, active, selected, mDragging);
      return;
    }

    // Stompbox enclosure, instead of a flat color blob.
    const IColor enclosure = mDragging ? PluginColors::NAM_THEMECOLOR.WithOpacity(0.95f)
                             : active   ? PluginColors::NAM_THEMECOLOR.WithOpacity(0.65f)
                                        : IColor(255, 58, 55, 53);
    const float corner = std::clamp(std::min(mRECT.W(), mRECT.H()) * 0.12f, 3.f, 9.f);
    g.FillRoundRect(enclosure, mRECT, corner);
    // Subtle top highlight / bottom shadow so it reads as a physical box, not a flat rectangle.
    g.FillRoundRect(COLOR_WHITE.WithOpacity(0.08f), mRECT.GetFromTop(mRECT.H() * 0.45f), corner, corner, 0.f, 0.f);
    g.DrawRoundRect(selected ? COLOR_WHITE : COLOR_BLACK.WithOpacity(0.6f), mRECT, corner, nullptr, selected ? 2.5f : 1.f);

    // Control-panel strip up top, with a couple of decorative mini-knobs -- purely cosmetic, not
    // interactive; the real controls live in the detail area once this pedal is selected.
    const auto topStrip = mRECT.GetFromTop(mRECT.H() * 0.32f).GetPadded(-std::min(3.f, corner * 0.5f));
    if (topStrip.W() > 10.f && topStrip.H() > 6.f)
    {
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.22f), topStrip, corner * 0.5f);
      const int miniKnobs = topStrip.W() > 64.f ? 3 : (topStrip.W() > 38.f ? 2 : 1);
      const float knobR = std::clamp(std::min(topStrip.H(), topStrip.W() / miniKnobs) * 0.28f, 1.5f, 7.f);
      for (int i = 0; i < miniKnobs; i++)
      {
        const float cx = topStrip.L + topStrip.W() * (i + 0.5f) / miniKnobs;
        const float cy = topStrip.MH();
        g.FillEllipse(COLOR_BLACK.WithOpacity(0.5f), cx, cy, knobR, knobR);
        g.DrawEllipse(COLOR_WHITE.WithOpacity(0.3f), cx, cy, knobR, knobR, 0.0, nullptr, 0.75f);
      }
    }

    // Footswitch near the bottom, lit up when the pedal is active -- the classic "is this on"
    // stompbox tell.
    const float swR = std::clamp(std::min(mRECT.W(), mRECT.H()) * 0.16f, 3.f, 11.f);
    const float swCx = mRECT.MW();
    const float swCy = mRECT.B - swR - std::min(4.f, mRECT.H() * 0.06f);
    if (swCy - swR > topStrip.B)
    {
      g.FillEllipse(IColor(255, 24, 23, 22), swCx, swCy, swR, swR);
      g.FillEllipse(active ? COLOR_WHITE.WithOpacity(0.55f) : COLOR_BLACK.WithOpacity(0.15f), swCx, swCy, swR * 0.62f,
                    swR * 0.62f);
      g.DrawEllipse(COLOR_BLACK.WithOpacity(0.7f), swCx, swCy, swR, swR, 0.0, nullptr, 1.f);

      // Name, between the control panel and the footswitch.
      const auto textArea = IRECT(mRECT.L + 2.f, topStrip.B + 1.f, mRECT.R - 2.f, swCy - swR - 1.f);
      if (textArea.H() > 6.f)
      {
        const std::string shown = displayName; // fitted (or shortened in the middle) when drawn
        const float wantSize = std::clamp(mRECT.H() * 0.17f, 9.f, 14.f);
        const IText oneLine = _FitText(g, shown, wantSize, textArea.W());
        // Two-word names ("TUBE LAB", "LOOP OUT") that would have to shrink to fit on one line go
        // on two lines instead, when the chip's tall enough -- much more readable on a small chip.
        const size_t space = shown.find(' ');
        if (oneLine.mSize < wantSize * 0.9f && space != std::string::npos && textArea.H() >= 2.f * 8.f)
        {
          const std::string a = shown.substr(0, space), b = shown.substr(space + 1);
          const float lineSize = std::min(wantSize, textArea.H() * 0.5f);
          IText t = _FitText(g, a.size() >= b.size() ? a : b, lineSize, textArea.W());
          IVectorBase::DrawTextFitted(g, t, a.c_str(), textArea.GetFromTop(textArea.H() * 0.5f).GetVShifted(1.f), nullptr);
          IVectorBase::DrawTextFitted(g, t, b.c_str(), textArea.GetFromBottom(textArea.H() * 0.5f).GetVShifted(-1.f), nullptr);
        }
        else
          IVectorBase::DrawTextFitted(g, oneLine, shown.c_str(), textArea, nullptr);
      }
    }
    else
    {
      // Too small for the full pedal chrome (e.g. a very cramped tray) -- just the name, centered.
      const std::string shown = displayName; // fitted (or shortened in the middle) when drawn
      IVectorBase::DrawTextFitted(g, _FitText(g, shown, std::clamp(mRECT.H() * 0.32f, 8.f, 14.f), mRECT.W() - 2.f),
                                  shown.c_str(), mRECT, nullptr);
    }
  }

public:
  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    NAMDragLog("down id=%d name=%s at %.1f,%.1f rect=%.1f,%.1f,%.1f,%.1f", mPedalIdx, _DisplayName().c_str(), x, y,
            mRECT.L, mRECT.T, mRECT.R, mRECT.B);
    mDragging = true;
    mTotalDrag = 0.f;
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    mTotalDrag += (dX < 0.f ? -dX : dX) + (dY < 0.f ? -dY : dY);
    SetTargetAndDrawRECTs(mRECT.GetTranslated(dX, dY));
    if (mDragPreview && mTotalDrag >= 4.f)
      mDragPreview(x, y, true);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    mDragging = false;
    if (mDragPreview)
      mDragPreview(x, y, false);
    const bool wasClick = mTotalDrag < 4.f;
    NAMDragLog("up id=%d name=%s at %.1f,%.1f drag=%.1f %s", mPedalIdx, _DisplayName().c_str(), x, y, mTotalDrag,
            wasClick ? "CLICK" : "DROP");
    if (wasClick)
    {
      // Snap back so a tiny jitter on a plain click doesn't leave the chip nudged.
      if (mPositionGetter)
        SetTargetAndDrawRECTs(mPositionGetter());
      if (mOnSelected)
        mOnSelected(mPedalIdx);
    }
    else if (mOnDropped)
    {
      mOnDropped(mPedalIdx, x, y);
    }
  }

private:
  // The name getter's result if set and non-empty, else the static mName.
  std::string _DisplayName() const
  {
    if (mNameGetter)
    {
      const std::string dynamicName = mNameGetter();
      if (!dynamicName.empty())
        return dynamicName;
    }
    return mName;
  }

  // The name's text style at `size`, shrunk (down to 6.5 px) if that's too wide for `width`.
  static IText _FitText(IGraphics& g, const std::string& s, float size, float width)
  {
    IText t(size, COLOR_WHITE, "Roboto-Regular");
    IRECT measured;
    const float w = g.MeasureText(t, s.c_str(), measured);
    if (w > width && w > 0.f)
      t.mSize = std::max(7.5f, size * width / w);
    return t;
  }

  static std::string _TruncateForChip(const std::string& s, size_t maxLen)
  {
    if (s.size() <= maxLen)
      return s;
    return s.substr(0, maxLen > 1 ? maxLen - 1 : maxLen) + "\xE2\x80\xA6"; // ellipsis
  }

  std::string mName;
  int mPedalIdx;
  IVStyle mStyle;
  std::function<void(int, float, float)> mOnDropped;
  std::function<bool()> mActiveGetter;
  std::function<bool()> mSelectedGetter;
  std::function<IRECT()> mPositionGetter;
  std::function<void(int)> mOnSelected;
  std::function<std::string()> mNameGetter;
  std::function<void(float, float, bool)> mDragPreview;
  std::function<bool(IRECT&)> mClipGetter;
  std::function<void(float)> mWheelFunc;
  std::string mLastTooltipName;
  bool mDragging = false;
  float mTotalDrag = 0.f;
  int mLabKind = 0; // See SetLabKind
};

/// Pedalboard overlay: a drag-and-drop chain rack (drag a pedal chip left/right of the fixed AMP
/// block to place it Pre-amp or in the effects loop, and to reorder it relative to the other
/// active pedals) plus each pedal's parameter knobs below.
class NAMPedalboardPageControl : public NAMOverlayPanelControl
{
public:
  NAMPedalboardPageControl(const IRECT& bounds, const ISVG& closeSVG, const IVStyle& style, const IBitmap& knobBitmap,
                           const IBitmap& switchBitmap)
  : NAMOverlayPanelControl(bounds, "PEDALBOARD", closeSVG)
  , mStyle(style)
  , mKnobBitmap(knobBitmap)
  , mSwitchBitmap(switchBitmap)
  , mPopupCloseSVG(closeSVG)
  {
  }

  void OnAttached() override
  {
    NAMOverlayPanelControl::OnAttached();

    const auto body = GetRECT().GetPadded(-20.f).GetReducedFromTop(60.f);
    const float chainAreaHeight = std::clamp(body.H() * 0.22f, 70.f, 110.f);
    mChainArea = body.GetFromTop(chainAreaHeight);
    // The tray (where inactive pedals sit) lives just below the chain rack -- knobsArea must start
    // after BOTH, or the tray visually overlaps (and steals clicks from) the top knob panels.
    const float trayGap = 8.f;
    mTrayHeight = chainAreaHeight * 0.7f;
    const auto knobsArea = body.GetReducedFromTop(chainAreaHeight + trayGap + mTrayHeight + 15.f);

    // 9 chain columns: slots -4..-1 (pre), 0 (fixed AMP block), 1..4 (loop).
    for (int i = 0; i < 9; i++)
      mSlotAreas[i] = mChainArea.GetGridCell(0, i, 1, 9).GetPadded(-5.f);

    AddChildControl(new NAMPanelControl(mSlotAreas[4], PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), 8.f));
    const IVStyle ampLabelStyle =
      DEFAULT_STYLE.WithValueText(IText(16, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular")).WithDrawFrame(false);
    AddChildControl(new IVLabelControl(mSlotAreas[4], "AMP", ampLabelStyle));

    const IVStyle hintStyle =
      DEFAULT_STYLE.WithValueText(IText(13, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.5f), "Roboto-Regular"))
        .WithDrawFrame(false);
    AddChildControl(new IVLabelControl(mChainArea.GetFromTop(16.f).GetFromLeft(mChainArea.W() * 0.44f), "PRE-AMP",
                                       hintStyle));
    AddChildControl(new IVLabelControl(mChainArea.GetFromTop(16.f).GetFromRight(mChainArea.W() * 0.44f),
                                       "EFFECTS LOOP", hintStyle));

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
      // NAM-file pedals: downloaded captures (e.g. Tone3000) instead of a coded algorithm -- the
      // file itself is picked from the "PEDAL 1"/"PEDAL 2" buttons in the AMP NEST panel, not here;
      // this chip just controls whether it's active and where it sits in the (pre-amp-only) chain.
      {"CAPTURE 1", kNamPedal0Active, kNamPedal0Engaged, kNamPedal0Slot,
       {{"Level", kNamPedal0Level}, {"Mix", kNamPedal0Mix}, {nullptr, -1}, {nullptr, -1}}},
      {"CAPTURE 2", kNamPedal1Active, kNamPedal1Engaged, kNamPedal1Slot,
       {{"Level", kNamPedal1Level}, {"Mix", kNamPedal1Mix}, {nullptr, -1}, {nullptr, -1}}},
    };

    const IVStyle rowLabelStyle =
      DEFAULT_STYLE.WithValueText(IText(17, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular")).WithDrawFrame(false);

    // Hint sits in the (otherwise empty when nothing's selected) space below the tray.
    const IVStyle hintStyle2 =
      DEFAULT_STYLE.WithValueText(IText(14, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.45f), "Roboto-Regular"))
        .WithDrawFrame(false);
    mHintLabel = new IVLabelControl(knobsArea, "Click a pedal above to edit it", hintStyle2);
    AddChildControl(mHintLabel);

    // Pass 1: all the chips, in the chain rack. Attached before the popup chrome below so the
    // popup (and its backdrop) always draws on top of every chip, never the other way around --
    // otherwise a chip positioned under the (centered) popup card would poke through it.
    for (int p = 0; p < kNumPedals; p++)
    {
      const int activeParam = kPedals[p].activeParam;
      const int engagedParam = kPedals[p].engagedParam;
      const int slotParam = kPedals[p].slotParam;

      auto* chip = new NAMPedalChipControl(
        _RectForPedal(p, activeParam, slotParam), kPedals[p].name, p, mStyle,
        [this, activeParam, slotParam](int pedalIdx, float dropX, float dropY) {
          _OnChipDropped(pedalIdx, dropX, dropY, activeParam, slotParam);
        });
      // "Lit" look reflects Engaged (is it actually processing), not Active (is it in the chain) --
      // so bypassing it via MIDI dims the chip in place rather than changing its position.
      chip->SetActiveGetter([this, engagedParam]() { return GetDelegate()->GetParam(engagedParam)->Value() > 0.5; });
      chip->SetPositionGetter([this, p, activeParam, slotParam]() { return _RectForPedal(p, activeParam, slotParam); });
      chip->SetSelectedGetter([this, p]() { return mSelectedPedal == p; });
      chip->SetSelectedFunc([this](int pedalIdx) { _SelectPedal(pedalIdx); });
      // NAM pedals' identity comes from the loaded .nam file, not a fixed algorithm -- show that
      // instead of the generic "CAPTURE 1"/"CAPTURE 2" label once one's actually loaded.
      if (p >= kNumBuiltInPedals)
      {
        const int namSlot = p - kNumBuiltInPedals;
        chip->SetNameGetter([this, namSlot, fallback = std::string(kPedals[p].name)]() -> std::string {
          const char* fileName = PLUG()->GetNamPedalFileName(namSlot);
          return fileName ? std::string(fileName) : fallback;
        });
      }
      mChips[p] = chip;
      AddChildControl(chip);
    }

    // Popup "window": a floating card over the rack/tray, shown only while a pedal is selected --
    // its own dedicated controls window, dismissed via its own close button or by clicking the
    // dimmed backdrop behind it (both independent of the rest of the pedalboard panel). Attached
    // after every chip above, so it (and its backdrop) always draws on top of the whole rack.
    const auto popupCardArea =
      body.GetCentredInside(std::min(body.W() * 0.68f, 420.f), std::min(body.H() * 0.55f, 280.f));
    const auto popupHeaderArea = popupCardArea.GetFromTop(32.f).GetPadded(-6.f);
    const auto popupCloseArea = popupHeaderArea.GetFromRight(22.f);
    const auto popupLabelArea = popupHeaderArea.GetReducedFromRight(28.f);
    const auto detailArea = popupCardArea.GetReducedFromTop(38.f).GetPadded(-10.f);

    mPopupBackdrop =
      AddChildControl(new NAMSlimOverlayBackdropControl(GetRECT(), [this](IControl*) { _SelectPedal(-1); }));
    mPopupBackdrop->Hide(true);
    mPopupCardBg = AddChildControl(
      new NAMBorderedPanelControl(popupCardArea, PluginColors::NAM_CARD, PluginColors::NAM_THEMECOLOR.WithOpacity(0.5f)));
    mPopupCardBg->Hide(true);
    mPopupCloseButton = AddChildControl(
      new NAMSquareButtonControl(popupCloseArea, [this](IControl*) { _SelectPedal(-1); }, mPopupCloseSVG));
    mPopupCloseButton->Hide(true);

    // Pass 2: each pedal's own label/switch/knobs, all sharing the same popup card position --
    // only the selected pedal's set is ever un-hidden (see _SelectPedal).
    for (int p = 0; p < kNumPedals; p++)
    {
      const float labelH = std::clamp(detailArea.H() * 0.16f, 18.f, 30.f);
      const auto labelArea = popupLabelArea.GetFromLeft(popupLabelArea.W() * 0.65f);
      const auto switchArea = popupLabelArea.GetFromRight(popupLabelArea.W() * 0.3f);
      const auto panelKnobsArea = detailArea.GetReducedFromTop(labelH + 10.f);

      const int engagedParam = kPedals[p].engagedParam;

      auto* label = new IVLabelControl(labelArea, kPedals[p].name, rowLabelStyle);
      AddChildControl(label);
      label->Hide(true);
      mDetailControls[p].push_back(label);

      // Real-time on/off (bypass) switch -- MIDI/automation-mappable. Deliberately separate from
      // "in the chain" (activeParam, set only by dragging): flipping this never moves the chip, so
      // a MIDI footswitch mapped here doesn't send it flying to the tray and back mid-performance.
      auto* onOffSwitch = new NAMSwitchControl(switchArea, engagedParam, "On", mStyle, mSwitchBitmap);
      AddChildControl(onOffSwitch);
      onOffSwitch->Hide(true);
      mDetailControls[p].push_back(onOffSwitch);

      int nKnobs = 0;
      for (int k = 0; k < 4; k++)
        if (kPedals[p].knobs[k].paramIdx >= 0)
          nKnobs++;
      // Fixed knob size regardless of how many a pedal has -- a 1-knob pedal (e.g. a NAM pedal's
      // Level) would otherwise stretch to fill the whole available cell and look huge next to a
      // 4-knob pedal's normally-sized ones. Knobs are laid out as a row, centered as a group.
      const float knobSize =
        std::clamp(std::min(panelKnobsArea.W() / 4.5f, panelKnobsArea.H() * 0.8f), 55.f, NAM_KNOB_HEIGHT);
      const float knobSpacing = knobSize * 1.3f;
      const float rowWidth = nKnobs * knobSpacing;
      const float rowLeft = panelKnobsArea.MW() - rowWidth * 0.5f;
      const float knobTop = panelKnobsArea.T + std::max(0.f, (panelKnobsArea.H() - knobSize) * 0.35f);
      for (int k = 0; k < nKnobs; k++)
      {
        const float cx = rowLeft + knobSpacing * (k + 0.5f);
        const IRECT knobArea(cx - knobSize * 0.5f, knobTop, cx + knobSize * 0.5f, knobTop + knobSize);
        auto* knob =
          new NAMKnobControl(knobArea, kPedals[p].knobs[k].paramIdx, kPedals[p].knobs[k].label, mStyle, mKnobBitmap);
        AddChildControl(knob);
        knob->Hide(true);
        mDetailControls[p].push_back(knob);
      }
    }
  }

  // IContainerBase::Hide(false) (run when the FX button opens this panel) unconditionally
  // un-hides every child control, which would blow away the per-pedal hidden state below and show
  // every pedal's knobs at once. Showing the panel needs to re-apply whichever pedal (if any) was
  // actually selected; hiding it can just cascade normally.
  void Hide(bool hide) override
  {
    NAMOverlayPanelControl::Hide(hide);
    if (!hide)
      _SelectPedal(mSelectedPedal);
  }

private:
  IRECT _ChainSlotRect(int slotValue) const { return mSlotAreas[slotValue + 4]; }

  // Shows pedal p's controls (label/switch/knobs) in the popup window and hides every other
  // pedal's, plus the "click a pedal" hint. Pass -1 to deselect (close the popup, show the hint).
  void _SelectPedal(int p)
  {
    mSelectedPedal = p;
    for (int i = 0; i < kNumPedals; i++)
      for (auto* c : mDetailControls[i])
        c->Hide(i != p);
    const bool popupOpen = p >= 0;
    if (mHintLabel)
      mHintLabel->Hide(popupOpen);
    if (mPopupBackdrop)
      mPopupBackdrop->Hide(!popupOpen);
    if (mPopupCardBg)
      mPopupCardBg->Hide(!popupOpen);
    if (mPopupCloseButton)
      mPopupCloseButton->Hide(!popupOpen);
    if (GetUI())
      GetUI()->SetAllControlsDirty();
  }

  IRECT _TrayRect(int pedalIdx) const
  {
    // Inactive pedals sit just below the chain rack, one fixed column per pedal. Must match the
    // reservation made for this in OnAttached (chainAreaHeight + trayGap + mTrayHeight) exactly,
    // or this visually collides with the knob panels below it.
    const auto trayArea = mChainArea.GetVShifted(mChainArea.H() + 8.f).GetFromTop(mTrayHeight);
    return trayArea.GetGridCell(0, pedalIdx, 1, kNumPedals).GetPadded(-4.f);
  }

  IRECT _RectForPedal(int pedalIdx, int activeParam, int slotParam)
  {
    const bool active = GetDelegate()->GetParam(activeParam)->Value() > 0.5;
    if (!active)
      return _TrayRect(pedalIdx);
    const int slotValue = (int)std::round(GetDelegate()->GetParam(slotParam)->Value());
    return _ChainSlotRect(slotValue);
  }

  void _OnChipDropped(int pedalIdx, float dropX, float dropY, int activeParam, int slotParam)
  {
    // Anything dropped below the chain rack goes back to the (inactive) tray.
    const bool droppedInTray = dropY > mChainArea.B;

    if (droppedInTray)
    {
      GetDelegate()->SendParameterValueFromUI(activeParam, 0.0);
    }
    else
    {
      int bestSlot = 0;
      float bestDist = 1e9f;
      for (int s = kPedalSlotMin; s <= kPedalSlotMax; s++)
      {
        if (s == 0)
          continue; // reserved for the fixed AMP block
        const IRECT& r = _ChainSlotRect(s);
        const float dx = r.MW() - dropX;
        const float dy = r.MH() - dropY;
        const float dist = dx * dx + dy * dy;
        if (dist < bestDist)
        {
          bestDist = dist;
          bestSlot = s;
        }
      }
      IParam* pSlot = GetDelegate()->GetParam(slotParam);
      GetDelegate()->SendParameterValueFromUI(slotParam, pSlot->ToNormalized((double)bestSlot));
      GetDelegate()->SendParameterValueFromUI(activeParam, 1.0);
    }

    // Snap the dropped chip to the position implied by its (now-updated) params. Other chips are
    // untouched by this drop -- their own params didn't change -- so they don't need repositioning.
    if (mChips[pedalIdx])
      mChips[pedalIdx]->SetTargetAndDrawRECTs(_RectForPedal(pedalIdx, activeParam, slotParam));
    GetUI()->SetAllControlsDirty();
  }

  IVStyle mStyle;
  IBitmap mKnobBitmap;
  IBitmap mSwitchBitmap;
  IRECT mChainArea;
  IRECT mSlotAreas[9];
  float mTrayHeight = 0.f;
  NAMPedalChipControl* mChips[kNumPedals] = {};

  // Which pedal's controls are currently shown in the popup window (-1 = none, showing the hint
  // instead). Each pedal's label/switch/knobs live in the same on-screen spot; only the selected
  // one's are ever un-hidden.
  int mSelectedPedal = -1;
  std::vector<IControl*> mDetailControls[kNumPedals];
  IVLabelControl* mHintLabel = nullptr;

  // The popup "window" chrome: dimming backdrop, card background, and its own close button.
  ISVG mPopupCloseSVG;
  IControl* mPopupBackdrop = nullptr;
  IControl* mPopupCardBg = nullptr;
  IControl* mPopupCloseButton = nullptr;
};

// Level meter, drawn by hand (the old one stretched the MeterBackground bitmap, redrew a black grid
// every frame and left a stray line in silence): one slim rounded bar per channel, filled blue /
// amber / red by level, with dB ticks and a falling peak-hold line. Horizontal when wider than
// tall (the main page's bottom strip), vertical otherwise (flanking the Amp popup). NC must match
// the NAMSender<NC> feeding it, since the message layout depends on it.
template <int NC = 1>
class NAMMeterControl : public IVPeakAvgMeterControl<NC>
{
  static constexpr float KMeterMin = -70.0f;
  static constexpr float KMeterMax = -0.01f;
  using Base = IVPeakAvgMeterControl<NC>;

public:
  NAMMeterControl(const IRECT& bounds, const IVStyle& style)
  : Base(bounds, "", style.WithShowValue(false).WithDrawFrame(false), EDirection::Vertical, {}, 0, KMeterMin,
         KMeterMax, {})
  {
    this->mPeakValues.fill(0.f); // the base class leaves these uninitialized
  }

  // 1.58.0: a click runs this (the OUT meter opens the OUTPUT page).
  void SetClickFunc(std::function<void()> f)
  {
    mClickFunc = std::move(f);
    this->SetIgnoreMouse(!mClickFunc);
  }
  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mClickFunc)
      mClickFunc();
    else
      Base::OnMouseDown(x, y, mod);
  }

  // A reading arrives every tick. Only repaint when it moved the bar or the peak by a visible amount
  // (0.07 dB), or while the bar is still gliding/holding -- a silent or steady meter used to repaint
  // every frame for nothing.
  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    const bool wasDirty = this->IsDirty();
    std::array<double, NC> value;
    std::array<float, NC> peak;
    for (int c = 0; c < NC; c++)
    {
      value[c] = this->GetValue(c);
      peak[c] = this->mPeakValues[c];
    }
    Base::OnMsgFromDelegate(msgTag, dataSize, pData);
    if (wasDirty || mMoving)
      return;
    constexpr double kVisible = 0.001;
    for (int c = 0; c < NC; c++)
      if (std::fabs(this->GetValue(c) - value[c]) > kVisible || std::fabs(this->mPeakValues[c] - peak[c]) > kVisible)
        return;
    this->SetClean();
  }

  void Draw(IGraphics& g) override
  {
    // Time since the last frame, so the motion below is the same at any frame rate.
    const auto now = Clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - mLastDraw).count(), 0.f, 0.1f);
    mLastDraw = now;
    constexpr float kRange = KMeterMax - KMeterMin; // dB across the whole bar
    bool moving = false;

    // Ballistics. The level glides toward each new reading instead of stepping to it: it rises
    // within ~10 ms (so peaks still show at their true height) and falls back over ~120 ms. The
    // peak line holds for 1 s, then falls at 20 dB/s; a clip stays flagged for 2 s.
    float loudestHold = 0.f;
    bool clipped = false;
    for (int c = 0; c < NC; c++)
    {
      const float target = std::clamp((float)this->GetValue(c), 0.f, 1.f);
      const float tau = target > mLevel[c] ? 0.010f : 0.120f;
      mLevel[c] += (target - mLevel[c]) * (1.f - std::exp(-dt / tau));
      if (std::fabs(target - mLevel[c]) > 0.0005f)
        moving = true;

      const float peak = this->mPeakValues[c];
      if (peak >= mHold[c])
      {
        mHold[c] = peak;
        mHoldSince[c] = now;
      }
      else if (now - mHoldSince[c] > std::chrono::seconds(1))
      {
        mHold[c] = std::max(peak, mHold[c] - (20.f / kRange) * dt);
        moving |= mHold[c] > 0.f;
      }
      else
        moving = true; // still holding: keep the timer running
      if (peak >= 1.f)
        mClipUntil[c] = now + std::chrono::seconds(2);
      if (now < mClipUntil[c])
      {
        clipped = true;
        moving = true;
      }
      loudestHold = std::max(loudestHold, mHold[c]);
    }

    const IRECT& rect = this->mRECT;
    const bool horiz = rect.W() > rect.H();
    const bool labels = horiz ? rect.H() >= 20.f : rect.W() >= 26.f;
    const bool chanNames = NC > 1 && horiz;
    const bool readout = horiz && rect.W() >= 120.f;
    // The bars' area: room left for the dB numbers (below / to the right), the L/R letters, and
    // (horizontal) the peak readout at the loud end.
    IRECT track = horiz ? rect.GetReducedFromBottom(labels ? 10.f : 0.f)
                            .GetReducedFromLeft(chanNames ? 10.f : 0.f)
                            .GetReducedFromRight(readout ? 40.f : 0.f)
                        : rect.GetReducedFromRight(labels ? 14.f : 0.f).GetVPadded(-6.f);
    if (horiz)
      track = track.GetMidVPadded(std::min(track.H() * 0.5f, NC * 8.f + (NC - 1) * 2.f));
    else
      track = track.GetMidHPadded(std::min(track.W() * 0.5f, NC * 5.f + (NC - 1) * 2.f));

    auto posOf = [](float dB) { return std::clamp((dB - KMeterMin) / (KMeterMax - KMeterMin), 0.f, 1.f); };
    // The part of r between positions a and b (0 = silent end, 1 = 0 dB end).
    auto span = [horiz](const IRECT& r, float a, float b) {
      return horiz ? IRECT(r.L + a * r.W(), r.T, r.L + b * r.W(), r.B) : IRECT(r.L, r.B - b * r.H(), r.R, r.B - a * r.H());
    };

    const IText tickText(8.f, PluginColors::NAM_INK.WithOpacity(0.5f), "Roboto-Regular");
    for (const float dB : {0.f, -6.f, -12.f, -24.f, -48.f})
    {
      const IRECT at = span(track, posOf(dB), posOf(dB));
      if (horiz)
      {
        g.DrawLine(PluginColors::NAM_INK.WithOpacity(0.2f), at.L, track.B + 1.f, at.L, track.B + 3.f);
        if (labels)
          g.DrawText(tickText, _DbStr(dB).c_str(), IRECT(at.L - 10.f, track.B + 2.f, at.L + 10.f, rect.B));
      }
      else
      {
        g.DrawLine(PluginColors::NAM_INK.WithOpacity(0.2f), track.R + 1.f, at.T, track.R + 4.f, at.T);
        if (labels)
          g.DrawText(IText(8.f, PluginColors::NAM_INK.WithOpacity(0.5f), "Roboto-Regular", EAlign::Near), _DbStr(dB).c_str(),
                     IRECT(track.R + 5.f, at.T - 5.f, rect.R, at.T + 5.f));
      }
    }

    const IColor green(255, 40, 235, 90), red(255, 255, 45, 45);
    const float redPos = posOf(-3.f);
    for (int c = 0; c < NC; c++)
    {
      const float gap = 2.f;
      const IRECT bar = horiz ? track.GetGridCell(c, 0, NC, 1).GetReducedFromBottom(c < NC - 1 ? gap * 0.5f : 0.f)
                                  .GetReducedFromTop(c > 0 ? gap * 0.5f : 0.f)
                              : track.GetGridCell(0, c, 1, NC).GetReducedFromRight(c < NC - 1 ? gap * 0.5f : 0.f)
                                  .GetReducedFromLeft(c > 0 ? gap * 0.5f : 0.f);
      g.FillRoundRect(IColor(255, 14, 14, 15), bar, 2.f);
      g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.12f), bar, 2.f, nullptr, 1.f);
      if (chanNames)
        g.DrawText(IText(8.f, COLOR_WHITE.WithOpacity(0.5f), "Roboto-Regular"), c == 0 ? "L" : "R",
                   IRECT(bar.L - 10.f, bar.T - 2.f, bar.L - 1.f, bar.B + 2.f));

      // Bright green, turning bright red near clipping (the top 3 dB), with a lighter strip along
      // one edge so the bar reads as lit. Drawn at sub-pixel precision -- no stepping.
      const IRECT inner = bar.GetPadded(-1.f);
      const float level = mLevel[c];
      auto fillBand = [&](float from, float to, const IColor& col) {
        to = std::min(to, level);
        if (to > from)
          g.FillRect(col, span(inner, from, to));
      };
      fillBand(0.f, redPos, green);
      fillBand(redPos, 1.f, red);
      if (level > 0.001f)
      {
        const IRECT lit = span(inner, 0.f, level);
        g.FillRect(COLOR_WHITE.WithOpacity(0.25f), horiz ? lit.GetFromTop(lit.H() * 0.35f) : lit.GetFromLeft(lit.W() * 0.35f));
      }

      // Peak line, red when it's been over the top; a clipped channel's whole bar is outlined red.
      if (now < mClipUntil[c])
        g.DrawRoundRect(red, bar, 2.f, nullptr, 1.5f);
      if (mHold[c] > 0.005f)
      {
        const float p = std::min(mHold[c], 1.f);
        const IRECT line = span(inner, p, p);
        g.FillRect(mHold[c] >= 1.f ? IColor(255, 255, 70, 60) : COLOR_WHITE.WithOpacity(0.85f),
                   horiz ? IRECT(line.L - 1.f, line.T, line.L + 1.f, line.B) : IRECT(line.L, line.T - 1.f, line.R, line.T + 1.f));
      }
    }

    // Peak readout (dB, loudest channel), red while a clip is flagged.
    if (readout)
    {
      char s[16];
      if (loudestHold <= 0.f)
        std::snprintf(s, sizeof(s), "-inf");
      else
        std::snprintf(s, sizeof(s), "%.1f", KMeterMin + std::min(loudestHold, 1.f) * kRange);
      g.DrawText(IText(11.f, clipped ? red : PluginColors::NAM_INK.WithOpacity(0.85f), "Roboto-Regular", EAlign::Far), s,
                 IRECT(track.R + 2.f, track.T - 4.f, rect.R, track.B + 4.f));
    }

    // Keep animating while anything is still gliding, holding or falling.
    mMoving = moving;
    if (moving)
      this->SetDirty(false);
  }

private:
  static std::string _DbStr(float dB) { return std::to_string((int)-dB); }

  using Clock = std::chrono::steady_clock;
  Clock::time_point mLastDraw = Clock::now();
  std::array<float, NC> mLevel{};
  std::array<float, NC> mHold{};
  std::array<Clock::time_point, NC> mHoldSince{};
  std::array<Clock::time_point, NC> mClipUntil{};
  bool mMoving = true;
  std::function<void()> mClickFunc; // See SetClickFunc
};

// MIDI learn overlay (1.43.0), Ableton-style. Attached last, over the whole window, and shown while
// MIDI learn is on: every visible knob or switch that a MIDI controller can drive gets a blue tint
// (green with its CC when it's mapped, orange when it's the one being learned). Clicking a tinted
// control selects it -- then moving or pressing a control on the MIDI controller maps it -- and a
// right-click clears it. Everything else (pedal chips, popups' X, the MIDI button) is clicked through
// as usual: the overlay only claims the points over a mappable control (see IsHit), so a pedal's
// popup can still be opened to reach its On switch. A strip along the bottom says what to do, shows
// the last MIDI message in, and lists the mappings.
class NAMMidiLearnOverlayControl : public IControl
{
public:
  struct Callbacks
  {
    std::function<int(IControl*)> targetOf; // A control's MIDI target, or kNoTarget if it has none
    std::function<int(int)> ccOf; // A target's CC, or -1
    std::function<int()> selected; // The target being learned, or kNoTarget
    std::function<void(int, bool)> select; // (target, add: Shift-click adds it to the CC's controls)
    std::function<void(int)> clear;
    std::function<std::string()> status; // The last MIDI message in, or what the selection is waiting for
    std::function<std::vector<std::string>()> mappings; // "CC 80: REVERB On, REVERB 2 On", ...
  };
  static constexpr int kNoTarget = INT_MIN;

  NAMMidiLearnOverlayControl(const IRECT& bounds, Callbacks callbacks)
  : IControl(bounds)
  , mCallbacks(std::move(callbacks))
  {
  }

  // Only over a mappable control: everywhere else, clicks go through to what's under the overlay.
  bool IsHit(float x, float y) const override { return _TargetAt(x, y) != kNoTarget; }

  // Redrawn every frame while shown, so the CC tags and status keep up with incoming MIDI.
  bool IsDirty() override { return !IsHidden() || IControl::IsDirty(); }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int target = _TargetAt(x, y);
    if (target == kNoTarget)
      return;
    if (mod.R)
      mCallbacks.clear(target);
    else
    {
      mCallbacks.select(target, mod.S);
      // A block on the grid (1.43.2): selecting it learns its On switch, and the click still opens
      // its popup as usual, for its knobs.
      if (auto* chip = dynamic_cast<NAMPedalChipControl*>(_TopmostAt(x, y)))
      {
        chip->OnMouseDown(x, y, mod);
        chip->OnMouseUp(x, y, mod);
      }
    }
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    const int selected = mCallbacks.selected();
    const IColor blue(255, 64, 156, 255), green(255, 70, 210, 110), orange(255, 255, 160, 40);
    const IText tagText(10.f, COLOR_WHITE, "Roboto-Regular");
    // The strip along the bottom (drawn last, over the tints).
    const IRECT strip = mRECT.GetFromBottom(kStripH);
    for (int i = 0; i < g.NControls(); i++)
    {
      IControl* c = g.GetControl(i);
      if (c == this || c->IsHidden())
        continue;
      const int target = mCallbacks.targetOf(c);
      if (target == kNoTarget)
        continue;
      const IRECT r = c->GetRECT();
      // Only where it's what the mouse would hit (not under a popup or a page). A slide switch only
      // takes clicks on its track, not its label, so try the middle of what it takes clicks on too.
      const IRECT t = c->GetTargetRECT();
      if (r.W() <= 0.f || strip.Contains(r.MW(), r.MH())
          || (_TopmostAt(r.MW(), r.MH()) != c && _TopmostAt(t.MW(), t.MH()) != c))
        continue;
      const int cc = mCallbacks.ccOf(target);
      const IColor& col = target == selected ? orange : (cc >= 0 ? green : blue);
      g.FillRoundRect(col.WithOpacity(target == selected ? 0.35f : 0.22f), r, 3.f);
      g.DrawRoundRect(col.WithOpacity(0.9f), r, 3.f, nullptr, target == selected ? 2.5f : 1.f);
      if (cc >= 0)
      {
        const std::string tag = "CC " + std::to_string(cc);
        const IRECT pill(r.L + 2.f, r.T + 2.f, r.L + 2.f + 8.f + 5.5f * (float)tag.size(), r.T + 15.f);
        g.FillRoundRect(COLOR_BLACK.WithOpacity(0.75f), pill, 3.f);
        g.DrawText(tagText, tag.c_str(), pill);
      }
    }

    g.FillRect(IColor(235, 14, 16, 22), strip);
    g.DrawLine(blue, strip.L, strip.T, strip.R, strip.T, nullptr, 2.f);
    const IRECT text = strip.GetPadded(-8.f);
    const IText head(12.f, COLOR_WHITE, "Roboto-Regular", EAlign::Near);
    const std::vector<std::string> lines = mCallbacks.mappings();
    const IRECT listArea = text.GetReducedFromTop(32.f);
    const int rows = std::max(1, (int)std::floor(listArea.H() / 12.f));
    const int cols = std::clamp(((int)lines.size() + rows - 1) / rows, 3, 6);
    const IText body(cols > 3 ? 10.f : 11.f, COLOR_WHITE.WithOpacity(0.8f), "Roboto-Regular", EAlign::Near);
    const IText status(12.f, orange, "Roboto-Regular", EAlign::Far);
    const IRECT line0 = text.GetFromTop(14.f);
    g.DrawText(head,
               "MIDI LEARN: click a pedal block (its on/off) or a knob/switch, then press or move one on your "
               "controller. Shift-click adds to a switch; right-click clears. Click MIDI when done.",
               line0);
    g.DrawText(status, mCallbacks.status().c_str(), text.GetFromTop(30.f).GetFromBottom(14.f));
    for (int k = 0; k < (int)lines.size() && k < rows * cols; k++)
    {
      const IRECT cell = listArea.GetGridCell(k % rows, k / rows, rows, cols);
      g.DrawText(body, lines[k].c_str(), cell.GetPadded(-1.f));
    }
  }

  static constexpr float kStripH = 86.f; // Clear of the Tube Lab popup's bottom row

private:
  // The control the mouse would hit at (x, y), not counting this overlay.
  IControl* _TopmostAt(float x, float y) const
  {
    IGraphics* g = const_cast<IGraphics*>(GetUI());
    if (!g)
      return nullptr;
    for (int i = g->NControls() - 1; i >= 0; i--)
    {
      IControl* c = g->GetControl(i);
      if (c == this || c->IsHidden() || c->GetIgnoreMouse())
        continue;
      if (c->IsHit(x, y))
        return c;
    }
    return nullptr;
  }

  int _TargetAt(float x, float y) const
  {
    if (IsHidden() || mRECT.GetFromBottom(kStripH).Contains(x, y))
      return kNoTarget;
    IControl* c = _TopmostAt(x, y);
    return c ? mCallbacks.targetOf(c) : kNoTarget;
  }

  Callbacks mCallbacks;
};

// Container where we can refer to children by names instead of indices
class IContainerBaseWithNamedChildren : public IContainerBase
{
public:
  IContainerBaseWithNamedChildren(const IRECT& bounds)
  : IContainerBase(bounds) {};
  ~IContainerBaseWithNamedChildren() = default;

protected:
  IControl* AddNamedChildControl(IControl* control, std::string name, int ctrlTag = kNoTag, const char* group = "")
  {
    // Make sure we haven't already used this name
    assert(mChildNameIndexMap.find(name) == mChildNameIndexMap.end());
    mChildNameIndexMap[name] = NChildren();
    return AddChildControl(control, ctrlTag, group);
  };

  IControl* GetNamedChild(std::string name)
  {
    const int index = mChildNameIndexMap[name];
    return GetChild(index);
  };


private:
  std::unordered_map<std::string, int> mChildNameIndexMap;
}; // class IContainerBaseWithNamedChildren


struct PossiblyKnownParameter
{
  bool known = false;
  double value = 0.0;
};

struct ModelInfo
{
  PossiblyKnownParameter sampleRate;
  PossiblyKnownParameter inputCalibrationLevel;
  PossiblyKnownParameter outputCalibrationLevel;
};

class ModelInfoControl : public IContainerBaseWithNamedChildren
{
public:
  ModelInfoControl(const IRECT& bounds, const IVStyle& style)
  : IContainerBaseWithNamedChildren(bounds)
  , mStyle(style) {};

  void ClearModelInfo()
  {
    static_cast<IVLabelControl*>(GetNamedChild(mControlNames.sampleRate))->SetStr("");
    mHasInfo = false;
  };

  void Hide(bool hide) override
  {
    // Don't show me unless I have info to show!
    IContainerBase::Hide(hide || (!mHasInfo));
  };

  void OnAttached() override
  {
    AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 0), "Model information:", mStyle));
    AddNamedChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 1), "", mStyle), mControlNames.sampleRate);
    // AddNamedChildControl(
    //   new IVLabelControl(GetRECT().SubRectVertical(4, 2), "", mStyle), mControlNames.inputCalibrationLevel);
    // AddNamedChildControl(
    //   new IVLabelControl(GetRECT().SubRectVertical(4, 3), "", mStyle), mControlNames.outputCalibrationLevel);
  };

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto SetControlStr = [&](const std::string& name, const PossiblyKnownParameter& p, const std::string& units,
                             const std::string& childName) {
      std::stringstream ss;
      ss << name << ": ";
      if (p.known)
      {
        ss << p.value << " " << units;
      }
      else
      {
        ss << "(Unknown)";
      }
      static_cast<IVLabelControl*>(GetNamedChild(childName))->SetStr(ss.str().c_str());
    };

    SetControlStr("Sample rate", modelInfo.sampleRate, "Hz", mControlNames.sampleRate);
    // SetControlStr(
    //   "Input calibration level", modelInfo.inputCalibrationLevel, "dBu", mControlNames.inputCalibrationLevel);
    // SetControlStr(
    //   "Output calibration level", modelInfo.outputCalibrationLevel, "dBu", mControlNames.outputCalibrationLevel);

    mHasInfo = true;
  };

private:
  const IVStyle mStyle;
  struct
  {
    const std::string sampleRate = "sampleRate";
    // const std::string inputCalibrationLevel = "inputCalibrationLevel";
    // const std::string outputCalibrationLevel = "outputCalibrationLevel";
  } mControlNames;
  // Do I have info?
  bool mHasInfo = false;
};

class OutputModeControl : public IVRadioButtonControl
{
public:
  OutputModeControl(const IRECT& bounds, int paramIdx, const IVStyle& style, float buttonSize)
  : IVRadioButtonControl(
      bounds, paramIdx, {}, "Output Mode", style, EVShape::Ellipse, EDirection::Vertical, buttonSize) {};

  void SetNormalizedDisable(const bool disable)
  {
    // HACK non-DRY string and hard-coded indices
    std::stringstream ss;
    ss << "Normalized";
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(1)->Set(ss.str().c_str());
  };
  void SetCalibratedDisable(const bool disable)
  {
    // HACK non-DRY string and hard-coded indices
    std::stringstream ss;
    ss << "Calibrated";
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(2)->Set(ss.str().c_str());
  };
};

// Settings page audio buffer row (1.31.0; more sizes and the interface's own panel in 1.51.0). In the
// standalone, a row of buffer sizes to click plus DRIVER PANEL (the audio interface's own control panel)
// and DEVICE (device, sample rate, channels, MIDI); in a DAW, which owns the buffer, it only shows the
// host's size and where to change it.
class NAMBufferSizeControl : public IControl
{
public:
  struct Callbacks
  {
    std::function<int()> getRequested; // the size asked for (highlighted)
    std::function<int()> getActual; // the size the driver actually runs (0 = unknown)
    std::function<void(int)> setSize;
    std::function<void()> openDriverPanel;
    std::function<void()> openDevicePrefs;
  };

  NAMBufferSizeControl(const IRECT& bounds, Callbacks callbacks, bool editable)
  : IControl(bounds)
  , mCallbacks(std::move(callbacks))
  , mEditable(editable)
  {
    mIgnoreMouse = !editable;
    if (editable)
      SetTooltip("Buffer size in samples. Your interface may only allow some sizes: if it runs a different "
                 "size, change it in DRIVER PANEL. 64 and below can drop out with the Tube Lab on.");
  }

  void Draw(IGraphics& g) override
  {
    const int requested = mCallbacks.getRequested ? mCallbacks.getRequested() : 0;
    const int actualRaw = mCallbacks.getActual ? mCallbacks.getActual() : 0;
    const int actual = actualRaw > 0 ? actualRaw : requested;
    const auto* pProcessor = dynamic_cast<const iplug::IPlugProcessor*>(GetDelegate());
    const double sr = pProcessor ? pProcessor->GetSampleRate() : 48000.0;
    const IText labelText(15.f, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular", EAlign::Near);
    const IText noteText(13.f, PluginColors::HELP_TEXT, "Roboto-Regular", EAlign::Near);
    const IRECT bottom = mRECT.GetFromBottom(mRECT.H() * 0.4f);
    g.DrawText(labelText, "AUDIO BUFFER", _Row().GetFromLeft(kLabelW));

    char note[240];
    const double ms = sr > 0.0 ? 1000.0 * actual / sr : 0.0;
    if (!mEditable)
      snprintf(note, sizeof(note), "%d samples = %.1f ms each way, set by your DAW: change it in the DAW's audio "
               "settings.", actual, ms);
    else if (actualRaw > 0 && actualRaw != requested)
      snprintf(note, sizeof(note), "Your interface is running %d samples = %.1f ms each way (asked for %d): it only "
               "allows the size set in its own panel. Change it with DRIVER PANEL.", actual, ms, requested);
    else
      snprintf(note, sizeof(note), "%d samples = %.1f ms each way. Bigger is safer from dropouts (Tube Lab), smaller "
               "feels tighter. DRIVER PANEL opens your interface's own settings.", actual, ms);
    g.DrawText(noteText, note, bottom);

    if (!mEditable)
      return;
    for (int i = 0; i < kNumSizes; i++)
    {
      const IRECT seg = _Segment(i).GetPadded(-2.f);
      const bool selected = kSizes[i] == requested;
      const bool hover = mHover == i;
      g.FillRoundRect(selected ? PluginColors::NAM_THEMECOLOR
                               : PluginColors::NAM_THEMECOLOR.WithOpacity(hover ? 0.3f : 0.15f),
                      seg, 4.f);
      const IText segText(13.f, selected ? PluginColors::NAM_ONACCENT : PluginColors::NAM_THEMEFONTCOLOR, "Roboto-Regular");
      g.DrawText(segText, std::to_string(kSizes[i]).c_str(), seg);
    }
    const char* buttonNames[kNumButtons] = {"DRIVER PANEL", "DEVICE..."};
    for (int b = 0; b < kNumButtons; b++)
    {
      const IRECT r = _Button(b).GetPadded(-2.f);
      const bool hover = mHover == kNumSizes + b;
      g.FillRoundRect(PluginColors::NAM_INK.WithOpacity(hover ? 0.18f : 0.08f), r, 4.f);
      g.DrawRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(hover ? 0.9f : 0.5f), r, 4.f, nullptr, 1.2f);
      g.DrawText(IText(12.f, PluginColors::NAM_THEMEFONTCOLOR, "Roboto-Regular"), buttonNames[b], r);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int hit = _HitTest(x, y);
    if (hit < 0)
      return;
    if (hit < kNumSizes)
    {
      if (mCallbacks.setSize)
        mCallbacks.setSize(kSizes[hit]);
    }
    else if (hit == kNumSizes && mCallbacks.openDriverPanel)
      mCallbacks.openDriverPanel();
    else if (hit == kNumSizes + 1 && mCallbacks.openDevicePrefs)
      mCallbacks.openDevicePrefs();
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const int hit = _HitTest(x, y);
    if (hit != mHover)
    {
      mHover = hit;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    mHover = -1;
    SetDirty(false);
  }

private:
  static constexpr int kNumSizes = 11;
  static constexpr int kSizes[kNumSizes] = {32, 48, 64, 96, 128, 192, 256, 384, 512, 1024, 2048};
  static constexpr int kNumButtons = 2;
  static constexpr float kLabelW = 150.f;
  static constexpr float kButtonW = 112.f;

  IRECT _Row() const { return mRECT.GetFromTop(mRECT.H() * 0.55f); }

  IRECT _Segment(int i) const
  {
    const IRECT row = _Row().GetReducedFromLeft(kLabelW).GetReducedFromRight(kNumButtons * kButtonW + 14.f);
    return row.SubRectHorizontal(kNumSizes, i);
  }

  IRECT _Button(int b) const
  {
    return _Row().GetFromRight(kNumButtons * kButtonW).SubRectHorizontal(kNumButtons, b);
  }

  int _HitTest(float x, float y) const
  {
    if (!mEditable)
      return -1;
    for (int i = 0; i < kNumSizes; i++)
      if (_Segment(i).Contains(x, y))
        return i;
    for (int b = 0; b < kNumButtons; b++)
      if (_Button(b).Contains(x, y))
        return kNumSizes + b;
    return -1;
  }

  Callbacks mCallbacks;
  bool mEditable;
  int mHover = -1;
};

// GATE / STUDIO COMP popup meters (1.52.0). Gate: the key's level against the threshold and close
// point, and whether the gate is open and how far it's turned down. Comp: input level against the
// threshold, gain reduction, output level. The DSP keeps the peak since the last read (see
// dynamics::StoreMax); the bars fall back at 30 dB/s so they're readable.
class NAMDynamicsMeterControl : public IControl
{
public:
  struct Callbacks
  {
    // Gate: key peak (dBFS), reduction (dB, >= 0), open?, threshold and close point (dBFS).
    std::function<float()> keyDB, closedDB;
    std::function<bool()> isOpen;
    // Comp: input and output peaks (dBFS), gain reduction (dB, >= 0).
    std::function<float()> inDB, grDB, outDB;
    std::function<float()> thresholdDB, closeDB;
  };

  NAMDynamicsMeterControl(const IRECT& bounds, bool gate, Callbacks callbacks)
  : IControl(bounds)
  , mGate(gate)
  , mCallbacks(std::move(callbacks))
  {
    mIgnoreMouse = true;
  }

  bool IsDirty() override { return !IsHidden() || IControl::IsDirty(); }

  void Draw(IGraphics& g) override
  {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - mLast).count(), 0.f, 0.2f);
    mLast = now;
    auto follow = [dt](float& shown, float fresh, float floor) {
      shown = std::max(fresh, std::max(floor, shown - 30.f * dt));
    };
    g.FillRoundRect(PluginColors::NAM_INK.WithOpacity(0.05f), mRECT, 4.f);
    const IRECT inner = mRECT.GetPadded(-6.f);
    const float thr = mCallbacks.thresholdDB ? mCallbacks.thresholdDB() : -200.f;
    if (mGate)
    {
      follow(mShown[0], mCallbacks.keyDB ? mCallbacks.keyDB() : -120.f, -90.f);
      // Reduction: shown as it is (not a falling peak), so the closing slope is visible.
      const float closed = mCallbacks.closedDB ? mCallbacks.closedDB() : 0.f;
      const bool open = mCallbacks.isOpen && mCallbacks.isOpen();
      _LevelRow(g, inner.SubRectVertical(2, 0), "KEY", mShown[0], -90.f, 0.f, thr,
                mCallbacks.closeDB ? mCallbacks.closeDB() : -200.f);
      const IRECT row = inner.SubRectVertical(2, 1).GetPadded(-2.f);
      const IRECT pill = row.GetFromLeft(row.H() * 2.6f).GetFromLeft(std::min(row.W(), 92.f));
      g.FillRoundRect(open ? PluginColors::NAM_THEMECOLOR : PluginColors::NAM_INK.WithOpacity(0.12f), pill, pill.H() * 0.5f);
      g.DrawText(IText(12.f, open ? PluginColors::NAM_ONACCENT : PluginColors::NAM_THEMEFONTCOLOR, "Roboto-Regular"),
                 open ? "OPEN" : "CLOSED", pill);
      char txt[48];
      if (closed < 0.1f)
        snprintf(txt, sizeof(txt), "passing");
      else if (closed >= 89.5f)
        snprintf(txt, sizeof(txt), "silent");
      else
        snprintf(txt, sizeof(txt), "-%.0f dB", closed);
      g.DrawText(IText(12.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.8f), "Roboto-Regular", EAlign::Near), txt,
                 row.GetReducedFromLeft(pill.W() + 10.f));
    }
    else
    {
      follow(mShown[0], mCallbacks.inDB ? mCallbacks.inDB() : -120.f, -60.f);
      follow(mShown[1], mCallbacks.grDB ? mCallbacks.grDB() : 0.f, 0.f);
      follow(mShown[2], mCallbacks.outDB ? mCallbacks.outDB() : -120.f, -60.f);
      _LevelRow(g, inner.SubRectVertical(3, 0), "IN", mShown[0], -60.f, 0.f, thr, -200.f);
      // Gain reduction grows from the left, 0 to 24 dB.
      const IRECT row = inner.SubRectVertical(3, 1);
      const IRECT bar = _Bar(row);
      g.DrawText(_LabelText(), "GR", row.GetFromLeft(kLabelW));
      g.FillRect(PluginColors::NAM_INK.WithOpacity(0.08f), bar);
      const float frac = std::clamp(mShown[1] / 24.f, 0.f, 1.f);
      g.FillRect(PluginColors::NAM_THEMECOLOR, bar.GetFromLeft(bar.W() * frac));
      for (float tick : {3.f, 6.f, 12.f, 18.f})
      {
        const float x = bar.L + bar.W() * tick / 24.f;
        g.DrawLine(PluginColors::NAM_INK.WithOpacity(0.25f), x, bar.T, x, bar.B);
      }
      char txt[24];
      snprintf(txt, sizeof(txt), "%.1f dB", mShown[1]);
      g.DrawText(_ValueText(), txt, row.GetFromRight(kValueW));
      _LevelRow(g, inner.SubRectVertical(3, 2), "OUT", mShown[2], -60.f, 0.f, -200.f, -200.f);
    }
  }

private:
  static constexpr float kLabelW = 40.f, kValueW = 62.f;
  static IText _LabelText() { return IText(12.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.8f), "Roboto-Regular", EAlign::Near); }
  static IText _ValueText() { return IText(12.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.8f), "Roboto-Regular", EAlign::Far); }
  static IRECT _Bar(const IRECT& row)
  {
    return row.GetReducedFromLeft(kLabelW).GetReducedFromRight(kValueW + 6.f).GetMidVPadded(std::min(7.f, row.H() * 0.3f));
  }

  // A level bar from lo to hi dBFS, with up to two markers (the threshold, and the gate's close point).
  void _LevelRow(IGraphics& g, const IRECT& row, const char* label, float dB, float lo, float hi, float mark1, float mark2)
  {
    const IRECT bar = _Bar(row);
    g.DrawText(_LabelText(), label, row.GetFromLeft(kLabelW));
    g.FillRect(PluginColors::NAM_INK.WithOpacity(0.08f), bar);
    auto xOf = [&](float v) { return bar.L + bar.W() * std::clamp((v - lo) / (hi - lo), 0.f, 1.f); };
    const bool over = mark1 > lo && dB > mark1;
    g.FillRect((over ? PluginColors::NAM_THEMECOLOR : PluginColors::NAM_INK.WithOpacity(0.45f)),
               IRECT(bar.L, bar.T, xOf(dB), bar.B));
    if (mark2 > lo && mark2 < hi)
      g.DrawLine(PluginColors::NAM_INK.WithOpacity(0.45f), xOf(mark2), bar.T - 4.f, xOf(mark2), bar.B + 4.f, nullptr, 1.5f);
    if (mark1 > lo && mark1 < hi)
      g.DrawLine(PluginColors::NAM_THEMECOLOR, xOf(mark1), bar.T - 5.f, xOf(mark1), bar.B + 5.f, nullptr, 2.f);
    char txt[24];
    if (dB <= lo + 0.05f)
      snprintf(txt, sizeof(txt), "-inf");
    else
      snprintf(txt, sizeof(txt), "%.0f dBFS", dB);
    g.DrawText(_ValueText(), txt, row.GetFromRight(kValueW));
  }

  bool mGate;
  Callbacks mCallbacks;
  float mShown[3] = {-120.f, 0.f, -120.f};
  std::chrono::steady_clock::time_point mLast = std::chrono::steady_clock::now();
};

/// A small flat button (1.48.0): draws an X when `text` is empty, a chevron for "#up" / "#down",
/// otherwise the text, and calls
/// `action` on click. Drawn in the theme's ink colour, so it shows in dark and light mode.
class NAMFlatButtonControl : public IControl
{
public:
  NAMFlatButtonControl(const IRECT& bounds, const char* text, std::function<void()> action)
  : IControl(bounds)
  , mText(text ? text : "")
  , mAction(std::move(action))
  {
  }

  void Draw(IGraphics& g) override
  {
    const IColor ink = PluginColors::NAM_INK;
    g.FillRoundRect(ink.WithOpacity(mMouseIsOver ? 0.16f : 0.06f), mRECT, 4.f);
    if (mText.empty())
    {
      const IRECT x = mRECT.GetCentredInside(std::min(mRECT.W(), mRECT.H()) * 0.4f);
      const IColor c = ink.WithOpacity(mMouseIsOver ? 0.95f : 0.6f);
      g.DrawLine(c, x.L, x.T, x.R, x.B, nullptr, 1.6f);
      g.DrawLine(c, x.L, x.B, x.R, x.T, nullptr, 1.6f);
    }
    else if (mText == "#pop")
    {
      // Pop out: a small window with an arrow leaving its corner.
      const IRECT x = mRECT.GetCentredInside(std::min(mRECT.W(), mRECT.H()) * 0.5f);
      const IColor c = ink.WithOpacity(mMouseIsOver ? 0.95f : 0.6f);
      const IRECT box(x.L, x.T + x.H() * 0.3f, x.R - x.W() * 0.3f, x.B);
      g.DrawRect(c, box, nullptr, 1.3f);
      g.DrawLine(c, x.MW(), x.MH(), x.R, x.T, nullptr, 1.3f);
      g.DrawLine(c, x.R - x.W() * 0.35f, x.T, x.R, x.T, nullptr, 1.3f);
      g.DrawLine(c, x.R, x.T, x.R, x.T + x.H() * 0.35f, nullptr, 1.3f);
    }
    else if (mText == "#up" || mText == "#down")
    {
      const IRECT x = mRECT.GetCentredInside(std::min(mRECT.W(), mRECT.H()) * 0.44f);
      const IColor c = ink.WithOpacity(mMouseIsOver ? 0.95f : 0.6f);
      const float tip = mText == "#up" ? x.T + x.H() * 0.2f : x.B - x.H() * 0.2f;
      const float base = mText == "#up" ? x.B - x.H() * 0.2f : x.T + x.H() * 0.2f;
      g.DrawLine(c, x.L, base, x.MW(), tip, nullptr, 1.6f);
      g.DrawLine(c, x.MW(), tip, x.R, base, nullptr, 1.6f);
    }
    else
      g.DrawText(IText(11.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(mMouseIsOver ? 0.95f : 0.65f), "Roboto-Regular"),
                 mText.c_str(), mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mAction)
      mAction();
  }

private:
  std::string mText;
  std::function<void()> mAction;
};

/// Tap tempo (1.71.0): the tempo, with a light that flashes on the beat. Drag up or down to change it
/// (Shift for fine steps), double-click to type it. TAP beside it (or a MIDI footswitch) sets it from
/// your taps.
class NAMTempoControl : public IControl
{
public:
  NAMTempoControl(const IRECT& bounds, int paramIdx, std::function<double()> beatPhase)
  : IControl(bounds, paramIdx)
  , mBeatPhase(std::move(beatPhase))
  {
  }

  void Draw(IGraphics& g) override
  {
    const IColor ink = PluginColors::NAM_INK;
    g.FillRoundRect(ink.WithOpacity(mMouseIsOver ? 0.16f : 0.06f), mRECT, 4.f);
    const double phase = mBeatPhase ? mBeatPhase() : -1.0;
    const IRECT led = mRECT.GetFromLeft(mRECT.H()).GetCentredInside(7.f);
    if (phase >= 0.0 && phase < 0.15)
      g.FillEllipse(PluginColors::NAM_THEMECOLOR, led);
    else
      g.DrawEllipse(ink.WithOpacity(0.45f), led, nullptr, 1.2f);
    char text[32];
    snprintf(text, sizeof(text), "%.1f BPM", GetParam() ? GetParam()->Value() : 120.0);
    g.DrawText(IText(12.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(mMouseIsOver ? 0.95f : 0.8f),
                     "Roboto-Regular"),
               text, mRECT.GetReducedFromLeft(mRECT.H() - 4.f));
  }

  // Redrawn every frame while shown, for the beat light.
  bool IsDirty() override { return !IsHidden() || IControl::IsDirty(); }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (!GetParam())
      return;
    // 0.5 BPM per pixel up, 0.1 with Shift.
    const double bpm = GetParam()->Value() - dY * (mod.S ? 0.1 : 0.5);
    SetValue(GetParam()->ToNormalized(bpm));
    SetDirty(true);
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    if (GetParam())
      GetUI()->PromptUserInput(*this, mRECT);
  }

private:
  std::function<double()> mBeatPhase;
};

/// Preferences (1.48.0): the colour theme (six palettes, dark or light) and the spectrum strip.
/// The changes themselves are made by the plug-in through the callbacks (they rebuild the UI).
class NAMThemePickerControl : public IControl
{
public:
  struct Callbacks
  {
    std::function<int()> getTheme;
    std::function<bool()> getLight;
    std::function<bool()> getStrip;
    std::function<void(int, bool)> setTheme;
    std::function<void(bool)> setStrip;
  };

  NAMThemePickerControl(const IRECT& bounds, Callbacks callbacks)
  : IControl(bounds)
  , mCallbacks(std::move(callbacks))
  {
  }

  void Draw(IGraphics& g) override
  {
    _Layout();
    const IText heading(15.f, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular", EAlign::Near);
    const IText name(11.f, PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.75f), "Roboto-Regular");
    g.DrawText(heading, "THEME", mThemeLabel);
    g.DrawText(heading, "SPECTRUM", mStripLabel);
    const int theme = mCallbacks.getTheme();
    const bool light = mCallbacks.getLight();
    for (int i = 0; i < PluginColors::kNumThemes; i++)
    {
      const IRECT& r = mSwatch[i];
      const IColor accent = PluginColors::ThemeAccent(i);
      // A tiny preview: the mode's background with the palette's accent bar across it.
      const IColor base = light ? PluginColors::ThemeTint(226, 224, 220, accent, 0.06f)
                                : PluginColors::ThemeTint(26, 24, 22, accent, i == 0 ? 0.f : 0.05f);
      g.FillRoundRect(base, r, 6.f);
      g.FillRoundRect(accent, r.GetPadded(-5.f).GetFromBottom(r.H() * 0.45f), 3.f);
      g.FillRoundRect(accent.WithOpacity(0.35f), r.GetPadded(-5.f).GetFromTop(r.H() * 0.22f), 2.f);
      const bool selected = i == theme;
      if (selected)
        g.DrawRoundRect(PluginColors::NAM_INK, r.GetPadded(3.f), 8.f, nullptr, 2.5f);
      else
        g.DrawRoundRect(PluginColors::NAM_INK.WithOpacity(mHover == i ? 0.6f : 0.2f), r, 6.f, nullptr, 1.f);
      g.DrawText(name, PluginColors::kThemes[i].name, IRECT(r.L - 20.f, r.B + 4.f, r.R + 20.f, r.B + 20.f));
    }
    _Segment(g, mDark, "DARK", !light, kHoverDark);
    _Segment(g, mLight, "LIGHT", light, kHoverLight);
    const bool strip = mCallbacks.getStrip();
    _Segment(g, mStripOn, "SHOW", strip, kHoverStripOn);
    _Segment(g, mStripOff, "HIDE", !strip, kHoverStripOff);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    _Layout();
    const int theme = mCallbacks.getTheme();
    const bool light = mCallbacks.getLight();
    for (int i = 0; i < PluginColors::kNumThemes; i++)
      if (mSwatch[i].Contains(x, y) && i != theme)
        mCallbacks.setTheme(i, light);
    if (mDark.Contains(x, y) && light)
      mCallbacks.setTheme(theme, false);
    if (mLight.Contains(x, y) && !light)
      mCallbacks.setTheme(theme, true);
    const bool strip = mCallbacks.getStrip();
    if (mStripOn.Contains(x, y) && !strip)
      mCallbacks.setStrip(true);
    if (mStripOff.Contains(x, y) && strip)
      mCallbacks.setStrip(false);
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    _Layout();
    int hover = -1;
    for (int i = 0; i < PluginColors::kNumThemes; i++)
      if (mSwatch[i].Contains(x, y))
        hover = i;
    if (mDark.Contains(x, y))
      hover = kHoverDark;
    if (mLight.Contains(x, y))
      hover = kHoverLight;
    if (mStripOn.Contains(x, y))
      hover = kHoverStripOn;
    if (mStripOff.Contains(x, y))
      hover = kHoverStripOff;
    if (hover != mHover)
    {
      mHover = hover;
      SetDirty(false);
    }
    IControl::OnMouseOver(x, y, mod);
  }

  void OnMouseOut() override
  {
    mHover = -1;
    IControl::OnMouseOut();
  }

private:
  static constexpr int kHoverDark = 100, kHoverLight = 101, kHoverStripOn = 102, kHoverStripOff = 103;

  void _Layout()
  {
    // One row: THEME, six swatches (names under them), DARK / LIGHT, then SPECTRUM SHOW / HIDE.
    const float natural = 112.f + 6.f * 58.f + 6.f + 120.f + 30.f + 112.f + 120.f;
    const float k = std::min(1.f, mRECT.W() / natural);
    const float labelW = 112.f * k, sw = 46.f * k, sh = 32.f, gap = 12.f * k, segW = 60.f * k, segH = 28.f;
    const float row = mRECT.T + 2.f;
    const float segT = row + (sh - segH) * 0.5f;
    mThemeLabel = IRECT(mRECT.L, segT, mRECT.L + labelW, segT + segH);
    float x = mRECT.L + labelW;
    for (int i = 0; i < PluginColors::kNumThemes; i++, x += sw + gap)
      mSwatch[i] = IRECT(x, row, x + sw, row + sh);
    x += 6.f * k;
    mDark = IRECT(x, segT, x + segW, segT + segH);
    mLight = IRECT(x + segW, segT, x + 2.f * segW, segT + segH);
    x += 2.f * segW + 30.f * k;
    mStripLabel = IRECT(x, segT, x + 112.f * k, segT + segH);
    x += 112.f * k;
    mStripOn = IRECT(x, segT, x + segW, segT + segH);
    mStripOff = IRECT(x + segW, segT, x + 2.f * segW, segT + segH);
  }

  void _Segment(IGraphics& g, const IRECT& r, const char* text, bool on, int hoverId)
  {
    const IColor fill = on ? PluginColors::NAM_THEMECOLOR
                           : PluginColors::NAM_SURFACE.WithContrast(mHover == hoverId ? 0.1f : 0.f);
    g.FillRect(fill, r);
    g.DrawRect(PluginColors::NAM_INK.WithOpacity(0.25f), r, nullptr, 1.f);
    g.DrawText(IText(13.f, on ? PluginColors::NAM_ONACCENT : PluginColors::NAM_THEMEFONTCOLOR, "Roboto-Regular"), text, r);
  }

  Callbacks mCallbacks;
  IRECT mThemeLabel, mStripLabel, mDark, mLight, mStripOn, mStripOff;
  IRECT mSwatch[PluginColors::kNumThemes];
  int mHover = -1;
};

class NAMSettingsPageControl : public IContainerBaseWithNamedChildren
{
public:
  NAMSettingsPageControl(const IRECT& bounds, const IBitmap& bitmap, const IBitmap& inputLevelBackgroundBitmap,
                         const IBitmap& switchBitmap, ISVG closeSVG, const IVStyle& style,
                         const IVStyle& radioButtonStyle,
                         NAMBufferSizeControl::Callbacks bufferCallbacks = NAMBufferSizeControl::Callbacks(),
                         bool bufferEditable = false, IControl* themePicker = nullptr)
  : IContainerBaseWithNamedChildren(bounds)
  , mThemePicker(themePicker)
  , mBufferCallbacks(std::move(bufferCallbacks))
  , mBufferEditable(bufferEditable)
  , mAnimationTime(0)
  , mBitmap(bitmap)
  , mInputLevelBackgroundBitmap(inputLevelBackgroundBitmap)
  , mSwitchBitmap(switchBitmap)
  , mStyle(style)
  , mRadioButtonStyle(radioButtonStyle)
  , mCloseSVG(closeSVG)
  {
    mIgnoreMouse = false;
  }

  void ClearModelInfo()
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->ClearModelInfo();
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      HideAnimated(true);
      return true;
    }

    return false;
  }

  void HideAnimated(bool hide)
  {
    mWillHide = hide;

    if (hide == false)
    {
      mHide = false;
    }
    else // hide subcontrols immediately
    {
      ForAllChildrenFunc([hide](int childIdx, IControl* pChild) { pChild->Hide(hide); });
    }

    SetAnimation(
      [&](IControl* pCaller) {
        auto progress = static_cast<float>(pCaller->GetAnimationProgress());

        if (mWillHide)
          SetBlend(IBlend(EBlend::Default, 1.0f - progress));
        else
          SetBlend(IBlend(EBlend::Default, progress));

        if (progress > 1.0f)
        {
          pCaller->OnEndAnimation();
          IContainerBase::Hide(mWillHide);
          GetUI()->SetAllControlsDirty();
          return;
        }
      },
      mAnimationTime);

    SetDirty(true);
  }

  void OnAttached() override
  {
    const float pad = 20.0f;
    const IVStyle titleStyle = DEFAULT_STYLE.WithValueText(IText(30, PluginColors::NAM_THEMEFONTCOLOR, "Michroma-Regular"))
                                 .WithDrawFrame(false)
                                 .WithShadowOffset(PluginColors::NAM_LIGHT ? 0.f : 2.f);
    const auto text = IText(DEFAULT_TEXT_SIZE, EAlign::Center, PluginColors::HELP_TEXT);
    const auto leftText = text.WithAlign(EAlign::Near);
    const auto style = mStyle.WithDrawFrame(false).WithValueText(text);
    const IVStyle leftStyle = style.WithValueText(leftText);

    // The backdrop art is dark; light mode uses a plain fill instead.
    if (PluginColors::NAM_LIGHT)
      AddNamedChildControl(new NAMPanelControl(GetRECT(), PluginColors::NAM_BG, 0.f), mControlNames.bitmap);
    else
      AddNamedChildControl(new IBitmapControl(GetRECT(), mBitmap), mControlNames.bitmap)->SetIgnoreMouse(true);
    const auto titleArea = GetRECT().GetPadded(-(pad + 10.0f)).GetFromTop(50.0f);
    AddNamedChildControl(new IVLabelControl(titleArea, "SETTINGS", titleStyle), mControlNames.title);

    // Attach input/output calibration controls
    {
      const float height = NAM_KNOB_HEIGHT + NAM_SWTICH_HEIGHT + 10.0f;
      const float width = titleArea.W();
      const auto inputOutputArea = titleArea.GetFromBottom(height).GetTranslated(0.0f, height);
      const auto inputArea = inputOutputArea.GetFromLeft(0.5f * width);
      const auto outputArea = inputOutputArea.GetFromRight(0.5f * width);

      const float knobWidth = 87.0f; // HACK based on looking at the main page knobs.
      const auto inputLevelArea =
        inputArea.GetFromTop(NAM_KNOB_HEIGHT).GetFromBottom(25.0f).GetMidHPadded(0.5f * knobWidth);
      const auto inputSwitchArea = inputArea.GetFromBottom(NAM_SWTICH_HEIGHT).GetMidHPadded(0.5f * knobWidth);

      auto* inputLevelControl = AddNamedChildControl(
        new InputLevelControl(inputLevelArea, kInputCalibrationLevel, mInputLevelBackgroundBitmap,
                              text.WithFGColor(COLOR_WHITE)),
        mControlNames.inputCalibrationLevel, kCtrlTagInputCalibrationLevel);
      inputLevelControl->SetTooltip(
        "The analog level, in dBu RMS, that corresponds to digital level of 0 dBFS peak in the host as its signal "
        "enters this plugin.");
      AddNamedChildControl(
        new NAMSwitchControl(inputSwitchArea, kCalibrateInput, "Calibrate Input", mStyle, mSwitchBitmap),
        mControlNames.calibrateInput, kCtrlTagCalibrateInput);

      // Same-ish height & width as input controls
      const auto outputRadioArea = outputArea.GetFromBottom(
        1.1f * (inputLevelArea.H() + inputSwitchArea.H())); // .GetMidHPadded(0.55f * knobWidth);
      const float buttonSize = 10.0f;
      auto* outputModeControl =
        AddNamedChildControl(new OutputModeControl(outputRadioArea, kOutputMode, mRadioButtonStyle, buttonSize),
                             mControlNames.outputMode, kCtrlTagOutputMode);
      outputModeControl->SetTooltip(
        "How to adjust the level of the output.\nRaw=No adjustment.\nNormalized=Adjust the level so that all models "
        "are about the same loudness.\nCalibrated=Match the input's digital-analog calibration.");
    }

    const float halfWidth = PLUG_WIDTH / 2.0f - pad;
    const auto bottomArea = GetRECT().GetPadded(-pad).GetFromBottom(78.0f);

    // Audio buffer row, centered in the space between the calibration controls and the info text.
    if (mBufferCallbacks.getRequested)
    {
      const float calibrationBottom = titleArea.B + NAM_KNOB_HEIGHT + NAM_SWTICH_HEIGHT + 10.0f;
      // With the theme row below it, the buffer row moves up to a third of the way down.
      // With the theme row above the info text, the buffer row centres in what's left.
      const float midY = 0.5f * (calibrationBottom + (mThemePicker ? bottomArea.T - kThemeRowH : bottomArea.T));
      const IRECT bufferArea(titleArea.L, midY - 30.f, titleArea.R, midY + 30.f);
      AddNamedChildControl(new NAMBufferSizeControl(bufferArea, mBufferCallbacks, mBufferEditable),
                           mControlNames.bufferSize);
    }
    const float lineHeight = 15.0f;
    const auto modelInfoArea = bottomArea.GetFromLeft(halfWidth).GetFromTop(4 * lineHeight);
    const auto aboutArea = bottomArea.GetFromRight(halfWidth).GetFromTop(5 * lineHeight);
    AddNamedChildControl(new ModelInfoControl(modelInfoArea, leftStyle), mControlNames.modelInfo);
    AddNamedChildControl(new AboutControl(aboutArea, leftStyle, leftText), mControlNames.about);
    if (mThemePicker)
    {
      const float top = bottomArea.T - kThemeRowH;
      mThemePicker->SetTargetAndDrawRECTs(IRECT(titleArea.L, top, titleArea.R, top + 54.f));
      AddNamedChildControl(mThemePicker, mControlNames.theme);
    }

    auto closeAction = [&](IControl* pCaller) {
      static_cast<NAMSettingsPageControl*>(pCaller->GetParent())->HideAnimated(true);
    };
    AddNamedChildControl(
      new NAMSquareButtonControl(CornerButtonArea(GetRECT()), closeAction, mCloseSVG), mControlNames.close);

    OnResize();
  }

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->SetModelInfo(modelInfo);
  };

private:
  NAMBufferSizeControl::Callbacks mBufferCallbacks;
  bool mBufferEditable = false;
  IControl* mThemePicker = nullptr;
  static constexpr float kThemeRowH = 62.f;
  IBitmap mBitmap;
  IBitmap mInputLevelBackgroundBitmap;
  IBitmap mSwitchBitmap;
  IVStyle mStyle;
  IVStyle mRadioButtonStyle;
  ISVG mCloseSVG;
  int mAnimationTime = 200;
  bool mWillHide = false;

  // Names for controls
  // Make sure that these are all unique and that you use them with AddNamedChildControl
  struct ControlNames
  {
    const std::string about = "About";
    const std::string bitmap = "Bitmap";
    const std::string bufferSize = "BufferSize";
    const std::string calibrateInput = "CalibrateInput";
    const std::string close = "Close";
    const std::string inputCalibrationLevel = "InputCalibrationLevel";
    const std::string modelInfo = "ModelInfo";
    const std::string outputMode = "OutputMode";
    const std::string theme = "Theme";
    const std::string title = "Title";
  } mControlNames;

  class InputLevelControl : public IEditableTextControl
  {
  public:
    InputLevelControl(const IRECT& bounds, int paramIdx, const IBitmap& bitmap, const IText& text = DEFAULT_TEXT,
                      const IColor& BGColor = DEFAULT_BGCOLOR)
    : IEditableTextControl(bounds, "", text, BGColor)
    , mBitmap(bitmap)
    {
      SetParamIdx(paramIdx);
    };

    void Draw(IGraphics& g) override
    {
      g.DrawFittedBitmap(mBitmap, mRECT);
      ITextControl::Draw(g);
    };

    void SetValueFromUserInput(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromUserInput(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      OnTextEntryCompletion(s.c_str(), valIdx);
    };

    void SetValueFromDelegate(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromDelegate(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      SetStr(s.c_str());
      SetDirty(false);
    };

  private:
    std::string ConvertToString(const double normalizedValue)
    {
      const double naturalValue = GetParam()->FromNormalized(normalizedValue);
      // And make the value to display
      std::stringstream ss;
      ss << naturalValue << " dBu";
      std::string s = ss.str();
      return s;
    };

    IBitmap mBitmap;
  };

  class AboutControl : public IContainerBase
  {
  public:
    AboutControl(const IRECT& bounds, const IVStyle& style, const IText& text)
    : IContainerBase(bounds)
    , mStyle(style)
    , mText(text) {};

    void OnAttached() override
    {
      WDL_String verStr, buildInfoStr;
      PLUG()->GetPluginVersionStr(verStr);

      buildInfoStr.SetFormatted(100, "Version %s %s %s", verStr.Get(), PLUG()->GetArchStr(), PLUG()->GetAPIStr());

      // 1.56.0: Griffin Amplification's own name and home first; the original authors are credited
      // (and linked) on the fourth line, as the MIT license asks.
      AddChildControl(new IURLControl(GetRECT().SubRectVertical(5, 0), "GRIFFIN AMPLIFICATION", PLUG_URL_STR, mText,
                                      COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(5, 1), "By Griffin Wojtowicz", mStyle));
      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(5, 2), buildInfoStr.Get(), mStyle));
      AddChildControl(new IURLControl(GetRECT().SubRectVertical(5, 3),
                                      "Built on open-source work by Steven Atkinson, Oli Larkin and others",
                                      "https://github.com/sdatkinson/NeuralAmpModelerPlugin/graphs/contributors", mText,
                                      COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      AddChildControl(new ThirdPartyNoticesControl(GetRECT().SubRectVertical(5, 4), mText));
    };

  private:
    class ThirdPartyNoticesControl : public IURLControl
    {
    public:
      ThirdPartyNoticesControl(const IRECT& bounds, const IText& text)
      : IURLControl(bounds, "Third party notices", "", text, COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO,
                    PluginColors::HELP_TEXT_CLICKED)
      {
      }

      void OnMouseDown(float x, float y, const IMouseMod& mod) override
      {
        WDL_String path;
        bool opened = false;

        if (ResolveNoticesPath(GetUI(), path))
          opened = OpenNoticesPath(GetUI(), path);

        if (!opened)
          ShowOpenError(GetUI());

        GetUI()->ReleaseMouseCapture();
        mClicked = true;
        SetDirty(false);
      }

    private:
      static bool FileExists(const WDL_String& path)
      {
        if (!CStringHasContents(path.Get()))
          return false;

        FILE* file = WDL_fopenA(path.Get(), "rb");
        if (file == nullptr)
          return false;

        fclose(file);
        return true;
      }

      static bool TryNoticePathInDirectory(WDL_String& result, const WDL_String& directory)
      {
        if (!CStringHasContents(directory.Get()))
          return false;

        WDL_String candidate(directory);
        const char lastChar = candidate.Get()[candidate.GetLength() - 1];

        if (!WDL_IS_DIRCHAR(lastChar))
          candidate.Append(WDL_DIRCHAR_STR);

        candidate.Append(kNoticesFileName);

        if (!FileExists(candidate))
          return false;

        result.Set(candidate.Get());
        return true;
      }

      // AAX (and similar) load the binary from Contents\x64 or Contents\Win32 while notices live in
      // Contents\Resources. Same layout as a VST3 bundle; this path is not covered by BundleResourcePath
      // when the plug-in is built as AAX_API only (no VST3_API).
      static bool TryNoticePathSiblingResources(WDL_String& result, const WDL_String& moduleDirectory)
      {
        if (!CStringHasContents(moduleDirectory.Get()))
          return false;

        WDL_String candidate(moduleDirectory);
        const char lastChar = candidate.Get()[candidate.GetLength() - 1];
        if (!WDL_IS_DIRCHAR(lastChar))
          candidate.Append(WDL_DIRCHAR_STR);

        candidate.Append("..");
        candidate.Append(WDL_DIRCHAR_STR);
        candidate.Append("Resources");
        candidate.Append(WDL_DIRCHAR_STR);
        candidate.Append(kNoticesFileName);

        if (!FileExists(candidate))
          return false;

        result.Set(candidate.Get());
        return true;
      }

      static bool ResolveNoticesPath(IGraphics* pGraphics, WDL_String& path)
      {
        path.Set("");

        if (pGraphics == nullptr)
          return false;

#ifdef OS_WIN
        WDL_String directory;
        const auto moduleHandle = static_cast<PluginIDType>(pGraphics->GetWinModuleHandle());

        BundleResourcePath(directory, moduleHandle);
        if (TryNoticePathInDirectory(path, directory))
          return true;

        directory.Set("");
        PluginPath(directory, moduleHandle);
        if (TryNoticePathInDirectory(path, directory))
          return true;

        if (TryNoticePathSiblingResources(path, directory))
          return true;
#endif

        const auto resourceLocation =
          LocateResource(kNoticesFileName, "txt", path, pGraphics->GetBundleID(), pGraphics->GetWinModuleHandle(),
                         pGraphics->GetSharedResourcesSubPath());

        return resourceLocation == EResourceLocation::kAbsolutePath && FileExists(path);
      }

      static bool OpenNoticesPath(IGraphics* pGraphics, const WDL_String& path)
      {
        if (pGraphics == nullptr || !CStringHasContents(path.Get()))
          return false;

#ifdef OS_WIN
        WCHAR pathWide[IPLUG_WIN_MAX_WIDE_PATH];
        UTF8ToUTF16(pathWide, path.Get(), IPLUG_WIN_MAX_WIDE_PATH);

        if (pathWide[0] == 0)
          return false;

        WCHAR canon[IPLUG_WIN_MAX_WIDE_PATH];
        const DWORD nCanon = GetFullPathNameW(pathWide, IPLUG_WIN_MAX_WIDE_PATH, canon, nullptr);
        const WCHAR* const launchPath = (nCanon > 0 && nCanon < IPLUG_WIN_MAX_WIDE_PATH) ? canon : pathWide;

        return ShellExecuteW(nullptr, L"open", launchPath, nullptr, nullptr, SW_SHOWNORMAL) > HINSTANCE(32);
#else
        return pGraphics->OpenURL(path.Get());
#endif
      }

      static void ShowOpenError(IGraphics* pGraphics)
      {
        if (pGraphics == nullptr)
          return;

        const char* const title = "Third party notices";
        const char* const message = "Could not open ThirdPartyNotices.txt.";

#ifdef OS_MAC
        pGraphics->ShowMessageBox(title, message, kMB_OK);
#else
        pGraphics->ShowMessageBox(message, title, kMB_OK);
#endif
      }

      static constexpr const char* kNoticesFileName = "ThirdPartyNotices.txt";
    };

    IVStyle mStyle;
    IText mText;
  };
};

/// A row of tabs (1.46.0, the Tube Lab's PREAMP / POWER AMP / FEEL). Not a parameter: the plugin
/// keeps which tab is open and shows that page's controls (see NeuralAmpModeler::_ApplyTubeTab).
class NAMTabBarControl : public IControl
{
public:
  NAMTabBarControl(const IRECT& bounds, std::vector<std::string> labels, std::function<int()> getTab,
                   std::function<void(int)> setTab)
  : IControl(bounds)
  , mLabels(std::move(labels))
  , mGetTab(std::move(getTab))
  , mSetTab(std::move(setTab))
  {
  }

  void Draw(IGraphics& g) override
  {
    const int n = (int)mLabels.size();
    const int sel = mGetTab ? mGetTab() : 0;
    g.DrawLine(PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f), mRECT.L, mRECT.B - 1.f, mRECT.R, mRECT.B - 1.f, nullptr, 1.5f);
    for (int i = 0; i < n; i++)
    {
      const IRECT r = mRECT.GetGridCell(0, i, 1, n).GetPadded(-3.f, 0.f, -3.f, -2.f);
      const bool on = i == sel, hover = i == mHover;
      g.FillRoundRect(on ? PluginColors::NAM_THEMECOLOR : PluginColors::NAM_BG.WithContrast((PluginColors::NAM_LIGHT ? -0.1f : 0.06f) + (hover ? 0.12f : 0.f)), r, 6.f, 6.f, 0.f, 0.f);
      const IText text(15.f, on ? PluginColors::NAM_ONACCENT : PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.75f), "Michroma-Regular");
      g.DrawText(text, mLabels[i].c_str(), r);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int i = _TabAt(x);
    if (i >= 0 && mSetTab)
      mSetTab(i);
    SetDirty(false);
  }
  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const int i = _TabAt(x);
    if (i != mHover)
    {
      mHover = i;
      SetDirty(false);
    }
  }
  void OnMouseOut() override
  {
    mHover = -1;
    SetDirty(false);
  }

private:
  int _TabAt(float x) const
  {
    const int n = (int)mLabels.size();
    if (n == 0 || mRECT.W() <= 0.f)
      return -1;
    return std::clamp((int)((x - mRECT.L) / mRECT.W() * n), 0, n - 1);
  }
  std::vector<std::string> mLabels;
  std::function<int()> mGetTab;
  std::function<void(int)> mSetTab;
  int mHover = -1;
};
