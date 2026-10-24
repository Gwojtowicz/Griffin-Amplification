#pragma once

// The SPEAKER LAB page (1.59.0; CAB LAB's 3D mic placement, CabLab.h):
// * FRONT: the cab face on, drawn to scale (its tolex, grille cloth and the speakers behind it). Drag a
//   mic to move it across the baffle (X/Y); the wheel moves it in and out (Z); Shift is fine control.
// * SIDE: the cab from the side, the grille on the left. Drag a mic out from the grille (Z, on a
//   square-root scale so the first few centimetres get room) and up and down (Y).
// * 3D: a perspective view. Drag a mic to move it across the baffle at its distance; drag anywhere
//   else to orbit; the wheel moves the selected mic's Z. Lines from each mic to each cone show how
//   much of that cone it hears.
// * RESPONSE: the close mics' frequency response, each mic and their sum, from the same geometry and
//   filters the audio uses (the comb filtering between cones and mics shows up here).
// * Mic tabs A-D (select; the panel under them shows that mic's controls) and a readout of the
//   selected mic's geometry, cone by cone.
// Nothing here animates: each view redraws only when a placement or setting changes, and the plot
// is recomputed only then.
// Included from NeuralAmpModeler.cpp after OutputPageControls.h.

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "CabLab.h"

using namespace iplug;
using namespace igraphics;

namespace speakerlab
{
inline IColor MicColor(int m)
{
  static const IColor colors[4] = {IColor(255, 255, 160, 60), IColor(255, 80, 200, 255), IColor(255, 120, 230, 120),
                                   IColor(255, 235, 110, 220)};
  return colors[std::clamp(m, 0, 3)];
}
inline IColor Ink(float a = 0.85f) { return PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(a); }
// 1.61.0: dual cabs are told apart by colour: cab 1 gold, cab 2 teal (their piping, tabs, badges).
inline IColor CabColor(int cab) { return cab == 0 ? IColor(255, 214, 170, 90) : IColor(255, 70, 205, 190); }

// Each format's look: tolex, piping and grille cloth.
struct CabLook
{
  IColor tolex, piping, grille, weave;
  const char* cloth;
};
inline CabLook LookFor(int format)
{
  switch (format)
  {
    case 0: return {IColor(255, 26, 26, 26), IColor(255, 200, 190, 160), IColor(255, 52, 48, 42), IColor(255, 150, 135, 100), "basketweave"};
    case 1: return {IColor(255, 22, 22, 24), IColor(255, 210, 210, 210), IColor(255, 78, 80, 84), IColor(255, 170, 175, 182), "silver"};
    case 2: return {IColor(255, 22, 22, 24), IColor(255, 210, 210, 210), IColor(255, 78, 80, 84), IColor(255, 170, 175, 182), "silver"};
    default: return {IColor(255, 176, 146, 96), IColor(255, 90, 55, 30), IColor(255, 92, 40, 34), IColor(255, 150, 95, 70), "oxblood"};
  }
}

constexpr double kRangeM = 0.5; // Mic X/Y limits
constexpr double kMaxZ = 1.5;

inline double Clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
} // namespace speakerlab

class NAMSpeakerLabViewControl : public IControl
{
public:
  enum class View
  {
    Front,
    Side,
    Persp
  };

  NAMSpeakerLabViewControl(const IRECT& bounds, View view, speakerlab::Model* model)
  : IControl(bounds)
  , mView(view)
  , mModel(model)
  {
  }

  void Draw(IGraphics& g) override
  {
    using namespace speakerlab;
    const cab::CabLabParams p = mModel->params();
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.28f), mRECT, 6.f);
    const IText title(10.f, PluginColors::NAM_THEMECOLOR, "Michroma-Regular", EAlign::Near, EVAlign::Top);
    const char* names[3] = {"FRONT  (drag: X / Y, wheel: Z)", "SIDE  (drag: Z / Y)", "3D  (drag a mic: X / Y, drag: orbit)"};
    g.DrawText(title, names[(int)mView], mRECT.GetPadded(-8.f));
    g.PathClipRegion(_Area());
    if (mView == View::Front)
      _DrawFront(g, p);
    else if (mView == View::Side)
      _DrawSide(g, p);
    else
      _DrawPersp(g, p);
    g.PathClipRegion();
    if (!p.lab3D)
    {
      // Older presets load in classic mode: say so (wrapped to the view's width).
      const IRECT banner = _Area().GetFromBottom(34.f);
      g.FillRect(COLOR_BLACK.WithOpacity(0.7f), banner);
      const IText t(10.f, IColor(255, 255, 185, 60), "Roboto-Regular");
      const auto lines = tubedebug::Wrap(g, t, "Classic mode (an older preset): switch 3D on to hear these placements.",
                                         banner.W() - 12.f);
      for (size_t i = 0; i < lines.size() && i < 2; i++)
        g.DrawText(t, lines[i].c_str(), banner.SubRectVertical(2, (int)i));
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const cab::CabLabParams p = mModel->params();
    mDragMic = _MicAt(p, x, y);
    mOrbiting = mDragMic < 0 && mView == View::Persp;
    mLastX = x;
    mLastY = y;
    if (mDragMic >= 0)
    {
      mModel->select(mDragMic);
      const cab::LabMic& m = p.lab[mDragMic];
      mGrab[0] = m.x;
      mGrab[1] = m.y;
      mGrab[2] = m.z;
    }
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    using namespace speakerlab;
    const float fine = mod.S ? 0.2f : 1.f;
    if (mOrbiting)
    {
      *mModel->yaw = (float)Clamp(*mModel->yaw + 0.008f * dX, -1.5, 1.5);
      *mModel->pitch = (float)Clamp(*mModel->pitch + 0.008f * dY, -1.2, 1.2);
      mModel->redrawViews();
      return;
    }
    if (mDragMic < 0)
      return;
    const cab::CabLabParams p = mModel->params();
    const cab::LabMic& m = p.lab[mDragMic];
    double nx = m.x, ny = m.y, nz = m.z;
    if (mView == View::Front)
    {
      const float s = _FrontScale(p);
      nx = m.x + fine * dX / s;
      ny = m.y - fine * dY / s;
    }
    else if (mView == View::Side)
    {
      // Z on the square-root axis: move in that axis' space.
      const float s = _SideScaleY(p);
      const double u = _SideU(m.z) + fine * dX / _SideZSpan();
      nz = _SideZ(u);
      ny = m.y - fine * dY / s;
    }
    else
    {
      // Across the baffle, at the mic's own distance: where the pointer's ray meets that plane.
      double wx, wy;
      if (_RayToPlane(p, x, y, m.z, wx, wy))
      {
        double px, py;
        _RayToPlane(p, mLastX, mLastY, m.z, px, py);
        nx = m.x + fine * (wx - px);
        ny = m.y + fine * (wy - py);
      }
    }
    mLastX = x;
    mLastY = y;
    mModel->setMic(mDragMic, Clamp(nx, -kRangeM, kRangeM), Clamp(ny, -kRangeM, kRangeM), Clamp(nz, 0.0, kMaxZ));
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    mDragMic = -1;
    mOrbiting = false;
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    using namespace speakerlab;
    const cab::CabLabParams p = mModel->params();
    const int m = *mModel->selected;
    const cab::LabMic& lm = p.lab[m];
    // Z in steps that scale with it: 1 mm up close, centimetres far back.
    const double step = (mod.S ? 0.2 : 1.0) * std::max(0.001, 0.08 * lm.z);
    mModel->setMic(m, lm.x, lm.y, Clamp(lm.z + (d > 0 ? step : -step), 0.0, kMaxZ));
  }

private:
  IRECT _Area() const { return mRECT.GetPadded(-8.f).GetReducedFromTop(16.f); }
  // 1.61.0: the cab FRONT and SIDE show (the selected one when dual), and whether a mic is on it.
  int _ViewCab(const cab::CabLabParams& p) const { return p.dual ? std::clamp(*mModel->selectedCab, 0, 1) : 0; }
  bool _OnViewCab(const cab::CabLabParams& p, int m) const { return mView == View::Persp || p.MicCab(m) == _ViewCab(p); }
  static speakerlab::CabLook _Look(const cab::CabLabParams& p, int cab)
  {
    speakerlab::CabLook look = speakerlab::LookFor(p.CabFormatOf(cab));
    if (p.dual)
      look.piping = speakerlab::CabColor(cab);
    return look;
  }

  // FRONT ========================================================================================
  float _FrontScale(const cab::CabLabParams& p) const
  {
    const IRECT a = _Area();
    return (float)(std::min(a.W(), a.H()) / (2.0 * speakerlab::kRangeM + 0.04));
  }
  void _FrontPos(const cab::CabLabParams& p, double x, double y, float& sx, float& sy) const
  {
    const IRECT a = _Area();
    const float s = _FrontScale(p);
    sx = a.MW() + (float)x * s;
    sy = a.MH() - (float)y * s;
  }

  void _DrawCabFace(IGraphics& g, const cab::CabFormat& fmt, const speakerlab::CabLook& look, int format, float cx,
                    float cy, float s)
  {
    using namespace speakerlab;
    const float w = (float)fmt.width * s, h = (float)fmt.height * s;
    const IRECT cabR(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    g.FillRoundRect(look.tolex, cabR, 0.025f * s);
    g.DrawRoundRect(look.piping, cabR.GetPadded(-0.012f * s), 0.02f * s, nullptr, 1.5f);
    const IRECT grille = cabR.GetPadded(-0.035f * s);
    g.FillRoundRect(look.grille, grille, 0.01f * s);
    // The cloth's weave (diagonal for basketweave, horizontal threads otherwise).
    g.PathClipRegion(grille);
    const float step = std::max(4.f, 0.012f * s);
    if (format == 0)
      for (float t = -grille.H(); t < grille.W(); t += step)
      {
        g.DrawLine(look.weave.WithOpacity(0.18f), grille.L + t, grille.T, grille.L + t + grille.H(), grille.B);
        g.DrawLine(look.weave.WithOpacity(0.12f), grille.L + t + grille.H(), grille.T, grille.L + t, grille.B);
      }
    else
      for (float t = grille.T; t < grille.B; t += step * 0.6f)
        g.DrawLine(look.weave.WithOpacity(0.15f), grille.L, t, grille.R, t);
    // The speakers behind the cloth.
    for (int k = 0; k < fmt.numCones; k++)
    {
      const float x = cx + (float)fmt.cx[k] * s, y = cy - (float)fmt.cy[k] * s;
      const float a = (float)fmt.coneRadius * s;
      g.FillCircle(COLOR_BLACK.WithOpacity(0.35f), x, y, a * 1.18f); // Basket and surround
      g.DrawCircle(look.weave.WithOpacity(0.35f), x, y, a * 1.1f, nullptr, 1.f);
      g.FillCircle(IColor(255, 40, 38, 36).WithOpacity(0.55f), x, y, a); // Cone
      for (float r = 0.45f; r < 1.f; r += 0.18f)
        g.DrawCircle(look.weave.WithOpacity(0.10f), x, y, a * r, nullptr, 1.f);
      g.FillCircle(IColor(255, 70, 66, 60).WithOpacity(0.75f), x, y, a * 0.3f); // Dust cap
      g.DrawText(IText(std::max(9.f, a * 0.22f), Ink(0.45f), "Roboto-Regular"), std::to_string(k + 1).c_str(),
                 IRECT(x - a, y + a * 0.55f, x + a, y + a * 0.95f));
    }
    g.PathClipRegion(_Area());
  }

  void _DrawFront(IGraphics& g, const cab::CabLabParams& p)
  {
    using namespace speakerlab;
    const IRECT a = _Area();
    const float s = _FrontScale(p);
    const int viewCab = _ViewCab(p);
    const cab::CabFormat& fmt = cab::GetCabFormat(p.CabFormatOf(viewCab));
    // Rulers: every 10 cm.
    for (int i = -5; i <= 5; i++)
    {
      float x, y;
      _FrontPos(p, i * 0.1, i * 0.1, x, y);
      g.DrawLine(Ink(i == 0 ? 0.25f : 0.08f), x, a.T, x, a.B);
      g.DrawLine(Ink(i == 0 ? 0.25f : 0.08f), a.L, y, a.R, y);
    }
    _DrawCabFace(g, fmt, _Look(p, viewCab), p.CabFormatOf(viewCab), a.MW(), a.MH(), s);
    if (p.dual)
    {
      g.DrawText(IText(11.f, CabColor(viewCab), "Michroma-Regular", EAlign::Far, EVAlign::Top),
                 viewCab == 0 ? "CAB 1" : "CAB 2", a.GetPadded(-4.f));
      // A cab nothing is listening to is silent: say so.
      bool heard = false;
      for (int m = 0; m < cab::kNumLabMics; m++)
        heard |= p.lab[m].on && p.MicCab(m) == viewCab;
      if (!heard)
      {
        const IRECT banner = a.GetFromBottom(34.f);
        g.FillRect(COLOR_BLACK.WithOpacity(0.7f), banner);
        const IText t(10.f, IColor(255, 255, 185, 60), "Roboto-Regular");
        const auto lines = tubedebug::Wrap(
          g, t, "No mic on this cab is on, so it's silent: turn one on, or set a mic's Cab to it.", banner.W() - 12.f);
        for (size_t i = 0; i < lines.size() && i < 2; i++)
          g.DrawText(t, lines[i].c_str(), banner.SubRectVertical(2, (int)i));
      }
    }
    // The selected mic's line to its nearest cone.
    const int sel = *mModel->selected;
    if (_OnViewCab(p, sel))
    {
      const cab::LabMic& m = p.lab[sel];
      int nearest = 0;
      cab::LabProximityDB(m, fmt, &nearest);
      float mx, my, kx, ky;
      _FrontPos(p, m.x, m.y, mx, my);
      _FrontPos(p, fmt.cx[nearest], fmt.cy[nearest], kx, ky);
      g.DrawDottedLine(MicColor(sel).WithOpacity(0.8f), mx, my, kx, ky, nullptr, 1.f, 3.f);
    }
    for (int m = cab::kNumLabMics - 1; m >= 0; m--)
    {
      const int idx = (m + sel + 1) % cab::kNumLabMics; // Selected last (on top)
      if (!_OnViewCab(p, idx))
        continue;
      const cab::LabMic& lm = p.lab[idx];
      float x, y;
      _FrontPos(p, lm.x, lm.y, x, y);
      // The capsule's size, and a halo that shrinks as it moves out (nearer = bigger).
      const float r = std::max(7.f, 0.022f * s);
      const IColor c = MicColor(idx);
      if (lm.on)
      {
        g.FillCircle(c.WithOpacity(0.18f), x, y, r * (1.f + 1.5f / (1.f + 20.f * (float)lm.z)));
        g.FillCircle(IColor(255, 30, 30, 30), x, y, r);
        g.DrawCircle(c, x, y, r, nullptr, 2.f);
        g.FillCircle(c.WithOpacity(0.5f), x, y, r * 0.45f);
      }
      else
        g.DrawCircle(c.WithOpacity(0.5f), x, y, r, nullptr, 1.f);
      if (idx == sel)
        g.DrawCircle(Ink(0.9f), x, y, r + 4.f, nullptr, 1.f);
      g.DrawText(IText(11.f, c, "Michroma-Regular"), std::string(1, char('A' + idx)).c_str(),
                 IRECT(x + r, y - r - 12.f, x + r + 16.f, y - r + 2.f));
    }
  }

  // SIDE =========================================================================================
  // Z axis: the grille at 26% of the width, then sqrt(z / 1.5 m) across the rest.
  float _SideZSpan() const { return _Area().W() * 0.7f; }
  float _SideX0() const { return _Area().L + _Area().W() * 0.26f; }
  static double _SideU(double z) { return std::sqrt(std::max(0.0, z) / speakerlab::kMaxZ); }
  static double _SideZ(double u)
  {
    u = speakerlab::Clamp(u, 0.0, 1.0);
    return u * u * speakerlab::kMaxZ;
  }
  float _SideScaleY(const cab::CabLabParams& p) const
  {
    return (float)(_Area().H() / (2.0 * speakerlab::kRangeM + 0.04));
  }
  void _SidePos(const cab::CabLabParams& p, double z, double y, float& sx, float& sy) const
  {
    sx = _SideX0() + (float)_SideU(z) * _SideZSpan();
    sy = _Area().MH() - (float)y * _SideScaleY(p);
  }

  void _DrawSide(IGraphics& g, const cab::CabLabParams& p)
  {
    using namespace speakerlab;
    const IRECT a = _Area();
    const int viewCab = _ViewCab(p);
    const cab::CabFormat& fmt = cab::GetCabFormat(p.CabFormatOf(viewCab));
    const CabLook look = _Look(p, viewCab);
    const float sy = _SideScaleY(p), x0 = _SideX0();
    // Z ticks.
    for (double cm : {2.5, 10.0, 25.0, 50.0, 100.0, 150.0})
    {
      float x, y;
      _SidePos(p, cm / 100.0, 0.0, x, y);
      g.DrawLine(Ink(0.08f), x, a.T, x, a.B);
      char t[16];
      snprintf(t, sizeof(t), cm < 10 ? "%.1f" : "%.0f", cm);
      g.DrawText(IText(9.f, Ink(0.4f), "Roboto-Regular"), t, IRECT(x - 14.f, a.B - 12.f, x + 14.f, a.B));
    }
    g.DrawText(IText(9.f, Ink(0.4f), "Roboto-Regular", EAlign::Far), "cm from the grille",
               IRECT(a.R - 120.f, a.T, a.R - 2.f, a.T + 12.f));
    // The cab from the side: its depth behind the grille (drawn at the Y scale), the cones' profiles.
    const float depth = std::min((float)fmt.depth * sy, x0 - a.L - 6.f);
    const float h = (float)fmt.height * sy;
    const IRECT body(x0 - depth, a.MH() - h / 2, x0, a.MH() + h / 2);
    g.FillRect(look.tolex, body);
    g.DrawRect(look.piping.WithOpacity(0.6f), body);
    g.FillRect(look.grille, IRECT(x0 - 3.f, body.T + 4.f, x0, body.B - 4.f));
    std::vector<double> rows;
    for (int k = 0; k < fmt.numCones; k++)
      if (std::find(rows.begin(), rows.end(), fmt.cy[k]) == rows.end())
        rows.push_back(fmt.cy[k]);
    for (double cy : rows)
    {
      const float yc = a.MH() - (float)cy * sy, r = (float)fmt.coneRadius * sy;
      const float back = std::min(depth * 0.6f, 0.11f * sy);
      float px[4] = {x0 - 4.f, x0 - 4.f, x0 - 4.f - back, x0 - 4.f - back};
      float py[4] = {yc - r, yc + r, yc + r * 0.25f, yc - r * 0.25f};
      g.FillConvexPolygon(IColor(255, 60, 56, 52), px, py, 4);
      g.FillRect(IColor(255, 90, 90, 90), IRECT(x0 - 4.f - back - 0.05f * sy, yc - r * 0.35f, x0 - 4.f - back, yc + r * 0.35f));
      g.DrawLine(Ink(0.15f), x0, yc, a.R, yc); // The cone row's axis
    }
    const int sel = *mModel->selected;
    for (int i = 0; i < cab::kNumLabMics; i++)
    {
      const int m = (i + sel + 1) % cab::kNumLabMics;
      if (!_OnViewCab(p, m))
        continue;
      const cab::LabMic& lm = p.lab[m];
      float x, y;
      _SidePos(p, lm.z, lm.y, x, y);
      const IColor c = MicColor(m);
      // Body pointing back from the capsule, away from the cab.
      const float len = 46.f, rad = 6.f;
      const IRECT bodyR(x, y - rad, x + len, y + rad);
      g.FillRoundRect(lm.on ? IColor(255, 40, 40, 42) : COLOR_TRANSPARENT, bodyR, 3.f);
      g.DrawRoundRect(lm.on ? c : c.WithOpacity(0.4f), bodyR, 3.f, nullptr, lm.on ? 1.5f : 1.f);
      g.FillCircle(lm.on ? c : c.WithOpacity(0.3f), x, y, rad * 0.9f);
      if (m == sel)
        g.DrawRoundRect(Ink(0.9f), bodyR.GetPadded(4.f), 4.f);
      g.DrawText(IText(10.f, c, "Michroma-Regular"), std::string(1, char('A' + m)).c_str(),
                 IRECT(x + len + 3.f, y - 7.f, x + len + 18.f, y + 7.f));
    }
  }

  // 3D ===========================================================================================
  struct V3
  {
    double x, y, z;
  };
  // World: x right, y up, z out of the grille. The camera orbits a point just in front of the cab.
  V3 _ToView(const V3& w) const
  {
    const double yaw = *mModel->yaw, pitch = *mModel->pitch;
    const double x = w.x, y = w.y, z = w.z - 0.12;
    const double x1 = x * std::cos(yaw) + z * std::sin(yaw), z1 = -x * std::sin(yaw) + z * std::cos(yaw);
    const double y2 = y * std::cos(pitch) - z1 * std::sin(pitch), z2 = y * std::sin(pitch) + z1 * std::cos(pitch);
    return {x1, y2, z2};
  }
  static constexpr double kCamDist = 2.4;
  // 1.61.0: where each cab sits in the 3D view: side by side (cab 1 left) when dual, standing on the
  // same floor; one cab, centred.
  void _CabOffset(const cab::CabLabParams& p, int cab, double& ox, double& oy) const
  {
    const cab::CabFormat& f1 = cab::GetCabFormat(p.CabFormatOf(0));
    const cab::CabFormat& f2 = cab::GetCabFormat(p.CabFormatOf(1));
    if (!p.dual)
    {
      ox = oy = 0.0;
      return;
    }
    const double gap = 0.14, total = f1.width + gap + f2.width;
    ox = cab == 0 ? -total / 2 + f1.width / 2 : total / 2 - f2.width / 2;
    const double floorY = -std::max(f1.height, f2.height) / 2;
    oy = floorY + (cab == 0 ? f1.height : f2.height) / 2;
  }
  double _CamDist(const cab::CabLabParams& p) const { return p.dual ? 3.1 : kCamDist; }
  float _Focal() const { return std::min(_Area().W(), _Area().H()) * 1.9f; }
  bool _Project(const V3& w, float& sx, float& sy) const
  {
    const V3 v = _ToView(w);
    const double depth = mCamDist - v.z;
    if (depth < 0.1)
      return false;
    const IRECT a = _Area();
    sx = a.MW() + (float)(_Focal() * v.x / depth);
    sy = a.MH() - (float)(_Focal() * v.y / depth);
    return true;
  }
  // Where the pointer's ray meets the plane z = planeZ (world), for dragging a mic at its distance.
  bool _RayToPlane(const cab::CabLabParams& p, float sx, float sy, double planeZ, double& wx, double& wy) const
  {
    const IRECT a = _Area();
    const double f = _Focal();
    // View-space ray from the camera at (0, 0, kCamDist).
    const double dx = (sx - a.MW()) / f, dy = -(sy - a.MH()) / f, dz = -1.0;
    const double yaw = *mModel->yaw, pitch = *mModel->pitch;
    // Back to world: undo pitch, then yaw (both rotations' transposes).
    auto toWorld = [&](double x, double y, double z, double& ox, double& oy, double& oz) {
      const double y1 = y * std::cos(pitch) + z * std::sin(pitch), z1 = -y * std::sin(pitch) + z * std::cos(pitch);
      ox = x * std::cos(yaw) - z1 * std::sin(yaw);
      oz = x * std::sin(yaw) + z1 * std::cos(yaw);
      oy = y1;
    };
    double ox, oy, oz, rx, ry, rz;
    toWorld(0.0, 0.0, mCamDist, ox, oy, oz);
    toWorld(dx, dy, dz, rx, ry, rz);
    oz += 0.12;
    if (std::fabs(rz) < 1e-9)
      return false;
    const double t = (planeZ - oz) / rz;
    if (t < 0.0)
      return false;
    wx = ox + t * rx;
    wy = oy + t * ry;
    return true;
  }

  void _Line3(IGraphics& g, const IColor& c, const V3& a, const V3& b, float thick = 1.f)
  {
    float x1, y1, x2, y2;
    if (_Project(a, x1, y1) && _Project(b, x2, y2))
      g.DrawLine(c, x1, y1, x2, y2, nullptr, thick);
  }
  void _Circle3(IGraphics& g, const IColor& c, double cx, double cy, double z, double r, bool fill)
  {
    constexpr int n = 28;
    float xs[n], ys[n];
    for (int i = 0; i < n; i++)
    {
      const double t = 2.0 * cab::kPi * i / n;
      if (!_Project({cx + r * std::cos(t), cy + r * std::sin(t), z}, xs[i], ys[i]))
        return;
    }
    if (fill)
      g.FillConvexPolygon(c, xs, ys, n);
    else
      for (int i = 0; i < n; i++)
        g.DrawLine(c, xs[i], ys[i], xs[(i + 1) % n], ys[(i + 1) % n]);
  }

  void _DrawPersp(IGraphics& g, const cab::CabLabParams& p)
  {
    using namespace speakerlab;
    mCamDist = _CamDist(p);
    const int numCabs = p.dual ? 2 : 1;
    double floorY = 0.0;
    for (int cab = 0; cab < numCabs; cab++)
    {
      double ox, oy;
      _CabOffset(p, cab, ox, oy);
      floorY = std::min(floorY, oy - cab::GetCabFormat(p.CabFormatOf(cab)).height / 2);
    }
    // Floor grid under the cabs.
    const double span = p.dual ? 1.0 : 0.5;
    for (int i = -10; i <= 10; i++)
    {
      if (std::fabs(i * 0.1) <= span)
        _Line3(g, Ink(0.06f), {i * 0.1, floorY, -0.4}, {i * 0.1, floorY, 1.0});
      if (i >= -4)
        _Line3(g, Ink(0.06f), {-span, floorY, i * 0.1 + 0.6}, {span, floorY, i * 0.1 + 0.6});
    }
    for (int cab = 0; cab < numCabs; cab++)
    {
      const cab::CabFormat& fmt = cab::GetCabFormat(p.CabFormatOf(cab));
      const CabLook look = _Look(p, cab);
      double ox, oy;
      _CabOffset(p, cab, ox, oy);
      const double w = fmt.width / 2, h = fmt.height / 2, d = fmt.depth;
      // The box: faces as filled quads, back to front (the front face last).
      const V3 c[8] = {{ox - w, oy - h, 0},  {ox + w, oy - h, 0},  {ox + w, oy + h, 0},  {ox - w, oy + h, 0},
                       {ox - w, oy - h, -d}, {ox + w, oy - h, -d}, {ox + w, oy + h, -d}, {ox - w, oy + h, -d}};
      const int faces[5][4] = {{4, 5, 6, 7}, {0, 4, 7, 3}, {1, 5, 6, 2}, {3, 2, 6, 7}, {0, 1, 5, 4}};
      auto quad = [&](const int* idx, const IColor& col, float edge) {
        float xs[4], ys[4];
        for (int i = 0; i < 4; i++)
          if (!_Project(c[idx[i]], xs[i], ys[i]))
            return;
        g.FillConvexPolygon(col, xs, ys, 4);
        for (int i = 0; i < 4; i++)
          g.DrawLine(look.piping.WithOpacity(edge), xs[i], ys[i], xs[(i + 1) % 4], ys[(i + 1) % 4]);
      };
      for (const auto& f : faces)
        quad(f, look.tolex, 0.35f);
      const int front[4] = {0, 1, 2, 3};
      quad(front, look.grille, p.dual ? 0.9f : 0.35f);
      for (int k = 0; k < fmt.numCones; k++)
      {
        _Circle3(g, COLOR_BLACK.WithOpacity(0.3f), ox + fmt.cx[k], oy + fmt.cy[k], 0.001, fmt.coneRadius * 1.15, true);
        _Circle3(g, look.weave.WithOpacity(0.5f), ox + fmt.cx[k], oy + fmt.cy[k], 0.002, fmt.coneRadius, false);
        _Circle3(g, look.weave.WithOpacity(0.35f), ox + fmt.cx[k], oy + fmt.cy[k], 0.002, fmt.coneRadius * 0.3, false);
      }
      if (p.dual)
      {
        float lx, ly;
        if (_Project({ox, oy + h + 0.06, 0.0}, lx, ly))
          g.DrawText(IText(10.f, CabColor(cab), "Michroma-Regular"), cab == 0 ? "CAB 1" : "CAB 2",
                     IRECT(lx - 40.f, ly - 7.f, lx + 40.f, ly + 7.f));
      }
    }
    // Each on mic's paths to the cones of its cab, brighter for the ones it hears more of.
    for (int m = 0; m < cab::kNumLabMics; m++)
    {
      const cab::LabMic& lm = p.lab[m];
      if (!lm.on)
        continue;
      const int cab = p.MicCab(m);
      const cab::CabFormat& fmt = cab::GetCabFormat(p.CabFormatOf(cab));
      double ox, oy;
      _CabOffset(p, cab, ox, oy);
      double maxGain = 1e-9;
      cab::ConePath paths[4];
      for (int k = 0; k < fmt.numCones; k++)
      {
        paths[k] = cab::ComputeConePath(lm, fmt, k);
        maxGain = std::max(maxGain, paths[k].gain);
      }
      for (int k = 0; k < fmt.numCones; k++)
        _Line3(g, MicColor(m).WithOpacity((float)(0.08 + 0.6 * paths[k].gain / maxGain)), {ox + lm.x, oy + lm.y, lm.z},
               {ox + fmt.cx[k], oy + fmt.cy[k], -fmt.recess});
    }
    // Mics: a body pointing away from the cab, the capsule at its position.
    const int sel = *mModel->selected;
    for (int i = 0; i < cab::kNumLabMics; i++)
    {
      const int m = (i + sel + 1) % cab::kNumLabMics;
      const cab::LabMic& lm0 = p.lab[m];
      double ox, oy;
      _CabOffset(p, p.MicCab(m), ox, oy);
      cab::LabMic lm = lm0;
      lm.x += ox;
      lm.y += oy;
      const IColor col = MicColor(m);
      _Line3(g, lm.on ? col : col.WithOpacity(0.35f), {lm.x, lm.y, lm.z}, {lm.x, lm.y, lm.z + 0.16}, lm.on ? 5.f : 2.f);
      float x, y;
      if (_Project({lm.x, lm.y, lm.z}, x, y))
      {
        g.FillCircle(lm.on ? col : col.WithOpacity(0.35f), x, y, 5.f);
        if (m == sel)
          g.DrawCircle(Ink(0.9f), x, y, 9.f);
        g.DrawText(IText(10.f, col, "Michroma-Regular"), std::string(1, char('A' + m)).c_str(),
                   IRECT(x - 18.f, y - 18.f, x - 4.f, y - 6.f));
      }
    }
  }

  // Hit test: the mic nearest the pointer within 14 px in this view (the selected one wins ties).
  int _MicAt(const cab::CabLabParams& p, float x, float y) const
  {
    int best = -1;
    float bestD = 14.f * 14.f;
    for (int m = 0; m < cab::kNumLabMics; m++)
    {
      const cab::LabMic& lm = p.lab[m];
      float sx = 0.f, sy = 0.f;
      if (mView != View::Persp && !_OnViewCab(p, m))
        continue;
      if (mView == View::Front)
        _FrontPos(p, lm.x, lm.y, sx, sy);
      else if (mView == View::Side)
      {
        _SidePos(p, lm.z, lm.y, sx, sy);
        // The body extends 46 px to the right of the capsule: anywhere on it counts.
        if (x > sx && x < sx + 46.f && std::fabs(y - sy) < 8.f)
          sx = x;
      }
      else
      {
        if (!_OnViewCab(p, m))
          continue;
        double ox, oy;
        _CabOffset(p, p.MicCab(m), ox, oy);
        if (!_Project({lm.x + ox, lm.y + oy, lm.z}, sx, sy))
          continue;
      }
      const float d2 = (sx - x) * (sx - x) + (sy - y) * (sy - y) - (m == *mModel->selected ? 4.f : 0.f);
      if (d2 < bestD)
      {
        bestD = d2;
        best = m;
      }
    }
    return best;
  }

  View mView;
  speakerlab::Model* mModel;
  int mDragMic = -1;
  bool mOrbiting = false;
  double mCamDist = kCamDist; // (Set when drawn: farther back with two cabs)
  float mLastX = 0.f, mLastY = 0.f;
  double mGrab[3] = {};
};

// The close mics' frequency response: each on mic (its colour) and their sum (white), recomputed only
// when marked stale (a setting changed).
class NAMSpeakerLabResponseControl : public IControl
{
public:
  NAMSpeakerLabResponseControl(const IRECT& bounds, speakerlab::Model* model)
  : IControl(bounds)
  , mModel(model)
  {
    mIgnoreMouse = true;
    for (double f = 40.0; f < 20000.0; f *= 1.025)
      mFreqs.push_back(f);
  }

  void MarkStale()
  {
    mStale = true;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    using namespace speakerlab;
    if (mStale)
      _Compute();
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.28f), mRECT, 6.f);
    // 1.61.0: with two cabs, the selected cab's mics and their sum, and both cabs together (dashed).
    if (mDual)
    {
      char title[64];
      snprintf(title, sizeof(title), "RESPONSE  CAB %d  (dashed: both cabs)", mCab + 1);
      g.DrawText(IText(10.f, CabColor(mCab), "Michroma-Regular", EAlign::Near, EVAlign::Top), title,
                 mRECT.GetPadded(-8.f));
    }
    else
      g.DrawText(IText(10.f, PluginColors::NAM_THEMECOLOR, "Michroma-Regular", EAlign::Near, EVAlign::Top),
                 "RESPONSE  (close mics, before the room)", mRECT.GetPadded(-8.f));
    const IRECT a = mRECT.GetPadded(-8.f).GetReducedFromTop(16.f).GetReducedFromBottom(12.f).GetReducedFromLeft(26.f);
    const float lo = mTop - 42.f, hi = mTop + 6.f;
    auto fx = [&](double f) { return a.L + a.W() * (float)(std::log(f / 40.0) / std::log(20000.0 / 40.0)); };
    auto dy = [&](float db) { return a.B - a.H() * (std::clamp(db, lo, hi) - lo) / (hi - lo); };
    const IText axis(9.f, Ink(0.4f), "Roboto-Regular");
    for (double f : {100.0, 1000.0, 10000.0})
    {
      g.DrawLine(Ink(0.1f), fx(f), a.T, fx(f), a.B);
      g.DrawText(axis, f >= 1000 ? (std::to_string((int)(f / 1000)) + "k").c_str() : "100",
                 IRECT(fx(f) - 14.f, a.B, fx(f) + 14.f, a.B + 12.f));
    }
    for (float db = std::ceil(lo / 12.f) * 12.f; db <= hi; db += 12.f)
    {
      g.DrawLine(Ink(0.08f), a.L, dy(db), a.R, dy(db));
      char t[8];
      snprintf(t, sizeof(t), "%.0f", db - mTop);
      g.DrawText(IText(9.f, Ink(0.4f), "Roboto-Regular", EAlign::Far), t, IRECT(a.L - 26.f, dy(db) - 6.f, a.L - 3.f, dy(db) + 6.f));
    }
    auto curve = [&](const std::vector<double>& db, const IColor& c, float thick) {
      for (size_t i = 1; i < db.size(); i++)
        g.DrawLine(c, fx(mFreqs[i - 1]), dy((float)db[i - 1]), fx(mFreqs[i]), dy((float)db[i]), nullptr, thick);
    };
    if (mDual && !mTotal.empty())
      for (size_t i = 1; i < mTotal.size(); i++)
        g.DrawDottedLine(Ink(0.45f), fx(mFreqs[i - 1]), dy((float)mTotal[i - 1]), fx(mFreqs[i]), dy((float)mTotal[i]),
                         nullptr, 1.f, 3.f);
    for (int m = 0; m < cab::kNumLabMics; m++)
      if (!mMic[m].empty())
        curve(mMic[m], MicColor(m).WithOpacity(m == *mModel->selected ? 0.9f : 0.45f), 1.f);
    if (mSilent)
      g.DrawText(IText(11.f, IColor(255, 255, 185, 60), "Roboto-Regular"), "No mic on this cab is on", a);
    else if (!mSum.empty())
      curve(mSum, Ink(0.95f), 2.f);
  }

private:
  void _Compute()
  {
    mStale = false;
    const cab::CabLabParams p = mModel->params();
    const int n = (int)mFreqs.size();
    mDual = p.dual;
    mCab = p.dual ? std::clamp(*mModel->selectedCab, 0, 1) : 0;
    const int cabFilter = p.dual ? mCab : -1;
    mSilent = true;
    for (int m = 0; m < cab::kNumLabMics; m++)
      mSilent &= !(p.lab[m].on && (cabFilter < 0 || p.MicCab(m) == cabFilter));
    mSum.assign(n, 0.0);
    cab::CabLab::LabResponse(p, 48000.0, mFreqs.data(), n, mSum.data(), -1, cabFilter);
    mTotal.clear();
    if (p.dual)
    {
      mTotal.assign(n, 0.0);
      cab::CabLab::LabResponse(p, 48000.0, mFreqs.data(), n, mTotal.data());
    }
    mTop = -1e9f;
    for (double v : (p.dual ? mTotal : mSum))
      mTop = std::max(mTop, (float)v);
    for (int m = 0; m < cab::kNumLabMics; m++)
    {
      mMic[m].clear();
      if (!p.lab[m].on || (cabFilter >= 0 && p.MicCab(m) != cabFilter))
        continue;
      mMic[m].assign(n, 0.0);
      cab::CabLab::LabResponse(p, 48000.0, mFreqs.data(), n, mMic[m].data(), m);
    }
    if (mTop < -200.f)
      mTop = 0.f;
  }

  speakerlab::Model* mModel;
  std::vector<double> mFreqs, mSum, mTotal, mMic[cab::kNumLabMics];
  float mTop = 0.f;
  bool mStale = true;
  bool mDual = false, mSilent = false; // 1.61.0
  int mCab = 0;
};

// Tabs A-D: click to select a mic (its controls show below). Each says its type, lit when on.
class NAMSpeakerLabTabsControl : public IControl
{
public:
  NAMSpeakerLabTabsControl(const IRECT& bounds, speakerlab::Model* model)
  : IControl(bounds)
  , mModel(model)
  {
  }

  void Draw(IGraphics& g) override
  {
    using namespace speakerlab;
    const cab::CabLabParams p = mModel->params();
    for (int m = 0; m < cab::kNumLabMics; m++)
    {
      const IRECT r = mRECT.SubRectHorizontal(cab::kNumLabMics, m).GetPadded(-3.f);
      const bool sel = m == *mModel->selected;
      const IColor c = MicColor(m);
      g.FillRoundRect(sel ? c.WithOpacity(0.22f) : COLOR_BLACK.WithOpacity(0.25f), r, 5.f);
      g.DrawRoundRect(c.WithOpacity(sel ? 0.95f : 0.4f), r, 5.f, nullptr, sel ? 2.f : 1.f);
      g.FillCircle(p.lab[m].on ? c : c.WithOpacity(0.2f), r.L + 12.f, r.MH(), 4.f);
      const std::string label = std::string("MIC ") + char('A' + m) + "  " + cab::GetMic(p.lab[m].type).name;
      IRECT text = r.GetReducedFromLeft(22.f);
      if (p.dual)
      {
        // Which cab it's on, in that cab's colour.
        const IRECT badge = r.GetFromRight(22.f).GetCentredInside(16.f, 14.f);
        const int cab = p.MicCab(m);
        g.FillRoundRect(CabColor(cab).WithOpacity(0.3f), badge, 3.f);
        g.DrawRoundRect(CabColor(cab), badge, 3.f);
        g.DrawText(IText(9.5f, CabColor(cab), "Roboto-Regular"), cab == 0 ? "1" : "2", badge);
        text = text.GetReducedFromRight(22.f);
      }
      IText lt(11.f, Ink(p.lab[m].on ? 0.9f : 0.45f), "Roboto-Regular", EAlign::Near);
      IVectorBase::DrawTextFitted(g, lt, label.c_str(), text, nullptr); // (Shrinks to fit beside the badge)
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int m = std::clamp((int)((x - mRECT.L) / (mRECT.W() / cab::kNumLabMics)), 0, cab::kNumLabMics - 1);
    mModel->select(m);
  }

private:
  speakerlab::Model* mModel;
};

// 1.61.0: CAB 1 / CAB 2 tabs, each in its colour with its size; the page's cab row and the FRONT and
// SIDE views show the selected one. With a single cab, CAB 2 is greyed out.
class NAMSpeakerLabCabTabsControl : public IControl
{
public:
  NAMSpeakerLabCabTabsControl(const IRECT& bounds, speakerlab::Model* model)
  : IControl(bounds)
  , mModel(model)
  {
  }

  void Draw(IGraphics& g) override
  {
    using namespace speakerlab;
    const cab::CabLabParams p = mModel->params();
    for (int c = 0; c < 2; c++)
    {
      const IRECT r = mRECT.SubRectHorizontal(2, c).GetPadded(-2.f);
      const bool avail = c == 0 || p.dual;
      const bool sel = avail && (p.dual ? *mModel->selectedCab : 0) == c;
      const IColor col = CabColor(c).WithOpacity(avail ? 1.f : 0.3f);
      g.FillRoundRect(sel ? col.WithOpacity(0.28f) : COLOR_BLACK.WithOpacity(0.25f), r, 4.f);
      g.DrawRoundRect(col.WithOpacity(sel ? 0.95f : 0.45f), r, 4.f, nullptr, sel ? 2.f : 1.f);
      char label[32];
      snprintf(label, sizeof(label), "CAB %d  %s", c + 1, cab::GetCabFormat(p.CabFormatOf(c)).name);
      g.DrawText(IText(10.5f, col, "Michroma-Regular"), label, r);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int c = x < mRECT.MW() ? 0 : 1;
    if (c == 1 && !mModel->params().dual)
      return;
    mModel->selectCab(c);
  }

private:
  speakerlab::Model* mModel;
};

// A compact param field (the page is dense): Segments shows every choice of an enum side by side;
// Stepper is "< value >" (click a side); Toggle is an on/off chip; Drag is a number you drag sideways
// (Shift: fine), or nudge with the wheel. Double-click resets to the default.
class NAMLabFieldControl : public IControl
{
public:
  enum class Mode
  {
    Segments,
    Stepper,
    Toggle,
    Drag
  };

  NAMLabFieldControl(const IRECT& bounds, int paramIdx, const char* label, Mode mode, IColor accent = IColor())
  : IControl(bounds, paramIdx)
  , mLabel(label)
  , mMode(mode)
  , mAccent(accent.A == 0 ? PluginColors::NAM_THEMECOLOR : accent)
  {
  }

  void Draw(IGraphics& g) override
  {
    using namespace speakerlab;
    const IParam* p = GetParam();
    if (!p)
      return;
    const float dim = IsDisabled() ? 0.4f : 1.f;
    const IText labelText(10.f, Ink(0.55f * dim), "Roboto-Regular", EAlign::Near);
    const IText valueText(11.f, Ink(0.95f * dim), "Roboto-Regular");
    IRECT r = mRECT;
    if (!mLabel.empty() && mMode != Mode::Toggle)
    {
      const float lw = std::min(r.W() * 0.4f, 7.f * mLabel.size() + 6.f);
      g.DrawText(labelText, mLabel.c_str(), r.GetFromLeft(lw));
      r = r.GetReducedFromLeft(lw);
    }
    r = r.GetPadded(-1.f);
    if (mMode == Mode::Segments)
    {
      const int n = p->NDisplayTexts();
      const int cur = p->Int() - (int)p->GetMin();
      for (int i = 0; i < n; i++)
      {
        const IRECT s = r.SubRectHorizontal(n, i).GetPadded(-1.f);
        g.FillRoundRect(i == cur ? mAccent.WithOpacity(0.5f * dim) : COLOR_BLACK.WithOpacity(0.3f), s, 3.f);
        g.DrawRoundRect(mAccent.WithOpacity((i == cur ? 1.f : 0.3f) * dim), s, 3.f, nullptr, i == cur ? 2.f : 1.f);
        IText t = valueText;
        t.mFGColor = Ink((i == cur ? 1.f : 0.55f) * dim);
        g.DrawText(t, p->GetDisplayText(i), s);
      }
      return;
    }
    if (mMode == Mode::Toggle)
    {
      const bool on = p->Bool();
      g.FillRoundRect(on ? mAccent.WithOpacity(0.35f * dim) : COLOR_BLACK.WithOpacity(0.3f), r, 3.f);
      g.DrawRoundRect(mAccent.WithOpacity((on ? 0.9f : 0.35f) * dim), r, 3.f);
      g.DrawText(valueText, mLabel.c_str(), r);
      return;
    }
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.3f), r, 3.f);
    g.DrawRoundRect(mAccent.WithOpacity((mMouseIsOver ? 0.7f : 0.3f) * dim), r, 3.f);
    WDL_String s;
    p->GetDisplayWithLabel(s);
    if (mMode == Mode::Stepper)
    {
      g.DrawText(valueText, "<", r.GetFromLeft(14.f));
      g.DrawText(valueText, ">", r.GetFromRight(14.f));
      g.DrawText(valueText, s.Get(), r.GetPadded(-14.f, 0.f, -14.f, 0.f));
    }
    else
      g.DrawText(valueText, s.Get(), r);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const IParam* p = GetParam();
    if (!p || IsDisabled())
      return;
    if (mMode == Mode::Drag)
    {
      _Begin();
      return;
    }
    double v = p->Value();
    if (mMode == Mode::Toggle)
      v = p->Bool() ? 0.0 : 1.0;
    else if (mMode == Mode::Stepper)
    {
      v += x < mRECT.MW() ? -1.0 : 1.0;
      if (v > p->GetMax())
        v = p->GetMin(); // Wraps round
      if (v < p->GetMin())
        v = p->GetMax();
    }
    else
    {
      // Segments: which one was clicked.
      IRECT r = mRECT;
      if (!mLabel.empty())
        r = r.GetReducedFromLeft(std::min(r.W() * 0.4f, 7.f * mLabel.size() + 6.f));
      const int n = p->NDisplayTexts();
      v = p->GetMin() + std::clamp((int)((x - r.L) / (r.W() / n)), 0, n - 1);
    }
    _Begin();
    SetValue(p->ToNormalized(v));
    SetDirty(true);
    _End();
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mMode != Mode::Drag || IsDisabled())
      return;
    SetValue(std::clamp(GetValue() + (dX - dY) / (mod.S ? 1500.0 : 300.0), 0.0, 1.0));
    SetDirty(true);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (mMode == Mode::Drag)
      _End();
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    if (mMode != Mode::Drag || IsDisabled())
      return;
    _Begin();
    SetValue(std::clamp(GetValue() + (d > 0 ? 1.0 : -1.0) / (mod.S ? 1000.0 : 200.0), 0.0, 1.0));
    SetDirty(true);
    _End();
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    if (mMode != Mode::Drag || IsDisabled())
      return;
    _Begin();
    SetValueToDefault();
    SetDirty(true);
    _End();
  }

private:
  void _Begin()
  {
    if (auto* d = GetDelegate())
      d->BeginInformHostOfParamChangeFromUI(GetParamIdx());
  }
  void _End()
  {
    if (auto* d = GetDelegate())
      d->EndInformHostOfParamChangeFromUI(GetParamIdx());
  }

  std::string mLabel;
  Mode mMode;
  IColor mAccent;
};

// The selected mic's geometry, cone by cone.
class NAMSpeakerLabReadoutControl : public IControl
{
public:
  NAMSpeakerLabReadoutControl(const IRECT& bounds, speakerlab::Model* model)
  : IControl(bounds)
  , mModel(model)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    using namespace speakerlab;
    const cab::CabLabParams p = mModel->params();
    const int m = *mModel->selected;
    const cab::LabMic& lm = p.lab[m];
    const cab::CabFormat& fmt = cab::GetCabFormat(p.CabFormatOf(p.MicCab(m)));
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.28f), mRECT, 6.f);
    const IRECT a = mRECT.GetPadded(-8.f);
    const IText head(11.f, MicColor(m), "Roboto-Regular", EAlign::Near, EVAlign::Top);
    const IText body(9.5f, Ink(0.8f), "Roboto-Regular", EAlign::Near, EVAlign::Top);
    char buf[200];
    char where[24] = "";
    if (p.dual)
      snprintf(where, sizeof(where), "  on CAB %d (%s)", p.MicCab(m) + 1, fmt.name);
    snprintf(buf, sizeof(buf), "MIC %c  %s (%s)%s%s   X %+.1f  Y %+.1f  Z %.1f cm", 'A' + m, cab::GetMic(lm.type).name,
             cab::GetMicPolar(lm.type).pattern, where, lm.on ? "" : "  - off", 100.0 * lm.x, 100.0 * lm.y, 100.0 * lm.z);
    g.DrawText(head, buf, a);
    int nearest = 0;
    const double prox = cab::LabProximityDB(lm, fmt, &nearest);
    constexpr float lineH = 11.5f;
    float y = a.T + 15.f;
    for (int k = 0; k < fmt.numCones; k++)
    {
      const cab::ConePath cp = cab::ComputeConePath(lm, fmt, k);
      snprintf(buf, sizeof(buf), "Cone %d%s: %.1f cm, %.0f deg off axis, %.3f ms, %+.1f dB, treble %+.1f dB", k + 1,
               k == nearest ? "*" : " ", 100.0 * cp.distanceM, cp.offAxisDeg, 1e3 * cp.delayS,
               20.0 * std::log10(std::max(1e-9, cp.gain)), 20.0 * std::log10(cp.shelf));
      g.DrawText(body, buf, IRECT(a.L, y, a.R, y + lineH));
      y += lineH;
    }
    snprintf(buf, sizeof(buf), "Proximity effect from cone %d: %+.1f dB around 120 Hz", nearest + 1, prox);
    g.DrawText(body, buf, IRECT(a.L, y, a.R, y + lineH));
  }

private:
  speakerlab::Model* mModel;
};
