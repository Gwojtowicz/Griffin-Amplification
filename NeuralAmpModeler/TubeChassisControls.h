#pragma once

// The CHASSIS page (1.62.0; 3D since 1.68.0): the Tube Lab as a whole amp chassis you can turn around.
// * A steel chassis in perspective, drawn as real 3D shapes (boxes and cylinders, lit from above and
//   sorted back to front): drag to turn it, the wheel zooms, RESET VIEW puts it back.
// * Along the back, the six preamp tubes in the order the signal passes them: V1 the input stage, V2
//   the second gain stage, the two sockets V5, V6 (TubeStage.h SocketParams: empty, or a gain stage,
//   cathode follower or boost, on a 12AX7, 12AT7, 12AU7 or 5751), V3 the cathode follower, V4 the
//   phase inverter. Then the four power tube sockets (V9..V12, EL34 or 6L6: a pair uses V9 and V10, a
//   quad all four), the output transformer and the speaker jack.
// * A signal wire runs from the input jack through every working tube to the power tubes and the
//   output transformer; it lights with what each stage passes on. Each tube glows with its heater
//   (the Variac) and pulses with its own stage's signal; a tube that isn't working sits cold. Power
//   tubes red-plate past their rated dissipation, and a failed one goes dark with a milky getter.
// * The turret board in front: the parts the preamp's knobs really are (orange-drop coupling caps,
//   electrolytic bypass caps, silver-mica plate caps, carbon-comp resistors with their colour bands,
//   the Gain 2 pot). The power transformer and filter caps, the front panel's knobs, the input and
//   speaker jacks, and the Variac on the bench beside it.
// * Click anything to see what it is and set it (the side panel); double-click to go to its page: the
//   Tube Lab's PREAMP / POWER AMP / FEEL tab, the debug scope on its node, the SPEAKER LAB, or the AMP
//   capture (NAMChassisLinksControl has the same links as buttons).
// It only redraws when something changes (at most ~25 times a second while you play) or while dragged.
// Included from NeuralAmpModeler.cpp after LibraryControls.h.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace iplug;
using namespace igraphics;

namespace chassis
{
// What the page selects: a part of the preamp (kParts), a tube, the Variac, ...
enum Item
{
  kInputCap = 0, // Bloom
  kRk1, // V1 cathode resistor (Bias)
  kCk1, // V1 bypass cap
  kRa, // Plate resistors (both stages)
  kCp1, // V1 plate cap
  kCouple, // Interstage coupling cap
  kGain2Pot,
  kRk2,
  kCk2,
  kCp2,
  kNumParts,
  kTubeV1 = kNumParts,
  kTubeV2,
  kTubeV3,
  kTubeV4,
  kVariac,
  // 1.68.0
  kSocket5,
  kSocket6,
  kPowerTube1, // V9 (side 1)
  kPowerTube2, // V10 (side 2)
  kPowerTube3, // V11 (side 1, a quad's)
  kPowerTube4, // V12 (side 2, a quad's)
  kOutputTransformer,
  kPowerSupply, // Power transformer and filter caps
  kInputJack,
  kSpeakerJack,
  kFrontPanel,
  kNumItems
};
constexpr int kNumSockets = 2, kNumPreamp = 4 + kNumSockets, kNumPower = 4;
inline bool IsPart(int i) { return i >= 0 && i < kNumParts; }
inline bool IsSocket(int i) { return i >= kSocket5 && i <= kSocket6; }
inline bool IsPowerTube(int i) { return i >= kPowerTube1 && i <= kPowerTube4; }
// Index into View::level / inUse (the same as TubeStage::StageLevel): V1, V2, V3, V4, V5, V6.
inline int TubeSlot(int item)
{
  return item >= kTubeV1 && item <= kTubeV4 ? item - kTubeV1 : IsSocket(item) ? 4 + item - kSocket5 : -1;
}

// Where the chassis leads (the links under the side panel's text, and a double-click).
enum Link
{
  kLinkPreamp = 0,
  kLinkPowerAmp,
  kLinkFeel,
  kLinkDebug,
  kLinkSpeakerLab,
  kLinkCapture,
  kNumLinks
};
inline const char* LinkName(int link)
{
  static const char* names[kNumLinks] = {"PREAMP TAB", "POWER AMP TAB", "FEEL TAB", "DEBUG SCOPE", "SPEAKER LAB",
                                         "AMP CAPTURE"};
  return names[std::clamp(link, 0, kNumLinks - 1)];
}
// An item's links, the first being where a double-click goes. Returns how many (up to 2).
inline int LinksOf(int item, int out[2])
{
  int n = 0;
  auto add = [&](int l) { out[n++] = l; };
  if (IsPart(item) || item == kTubeV1 || item == kTubeV2 || item == kTubeV3 || IsSocket(item))
  {
    add(kLinkPreamp);
    add(kLinkDebug);
  }
  else if (item == kTubeV4 || IsPowerTube(item))
  {
    add(kLinkPowerAmp);
    add(kLinkDebug);
  }
  else if (item == kOutputTransformer)
  {
    add(kLinkPowerAmp);
    add(kLinkSpeakerLab);
  }
  else if (item == kPowerSupply || item == kVariac)
  {
    add(kLinkFeel);
    add(kLinkDebug);
  }
  else if (item == kInputJack)
  {
    add(kLinkCapture);
    add(kLinkPreamp);
  }
  else if (item == kSpeakerJack)
  {
    add(kLinkSpeakerLab);
    add(kLinkPowerAmp);
  }
  else if (item == kFrontPanel)
  {
    add(kLinkPreamp);
    add(kLinkPowerAmp);
  }
  return n;
}
// The debug scope channel (tube::ProbeChannel) that shows an item best.
inline int ProbeOf(int item)
{
  switch (item)
  {
    case kInputCap: return tube::kProbeV1Block;
    case kRk1:
    case kCk1: return tube::kProbeV1Cathode;
    case kRa:
    case kCp1:
    case kTubeV1: return tube::kProbeV1Plate;
    case kCouple: return tube::kProbeV2Block;
    case kGain2Pot:
    case kRk2:
    case kCk2:
    case kCp2:
    case kTubeV2: return tube::kProbeV2Plate;
    case kTubeV3: return tube::kProbeStackOut;
    case kTubeV4: return tube::kProbePAGrid1;
    case kPowerTube1:
    case kPowerTube3: return tube::kProbePAPlate1;
    case kPowerTube2:
    case kPowerTube4: return tube::kProbePAPlate2;
    case kPowerSupply: return tube::kProbePARail;
    case kVariac: return tube::kProbePreRail;
    case kInputJack: return tube::kProbeInput;
    default:
      return IsSocket(item) ? tube::kProbeSocketPlate0 + item - kSocket5 : tube::kProbeOutput;
  }
}

// What the scene needs each time it draws.
struct View
{
  double heat = 1.0; // Heater voltage / rated
  double level[kNumPreamp] = {}; // Each preamp tube's signal (see TubeStage::StageLevel)
  bool inUse[kNumPreamp] = {true, false, false, false}; // Working (glowing) or cold
  bool circuit = true; // Circuit engine (the Classic engine has no parts to show)
  bool capture = false; // The capture is the preamp (its tubes sit cold)
  double variacV = 120.0;
  double rk[2] = {1500.0, 1500.0}, raKOhm = 100.0, gain2 = 0.5;
  std::string value[kNumParts]; // Each part's value, as its control shows it
  int socketRole[kNumSockets] = {}, socketTube[kNumSockets] = {}; // tube::SocketRole / TubeType
  bool engaged = true, powerAmp = false, sixL6 = false, quad = false;
  double life[kNumPower] = {1.0, 1.0, 1.0, 1.0}, dissipation[kNumPower] = {};
  bool dead[kNumPower] = {};
  double knob[6] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5}; // Front panel: Drive, Bass, Mid, Treble, Master, Presence (0..1)
  const char* title = "GRIFFIN  TUBE LAB";
};

inline IColor Glow(double heat)
{
  // Dull red cold-ish, orange at rated, toward yellow-white over-run.
  const double t = std::clamp((heat - 0.75) / 0.375, 0.0, 1.0);
  return IColor(255, 255, (int)(70 + 150 * t), (int)(20 + 120 * t * t));
}

// Standard resistor colour bands for a value in ohms: two digits, multiplier (and gold tolerance).
inline void ResistorBands(double ohms, IColor out[4])
{
  static const IColor c[10] = {IColor(255, 20, 20, 20),   IColor(255, 120, 70, 30),  IColor(255, 210, 40, 30),
                               IColor(255, 240, 130, 30), IColor(255, 240, 220, 40), IColor(255, 50, 160, 60),
                               IColor(255, 50, 90, 210),  IColor(255, 140, 60, 180), IColor(255, 140, 140, 140),
                               IColor(255, 245, 245, 245)};
  ohms = std::max(ohms, 10.0);
  int exp = (int)std::floor(std::log10(ohms)) - 1;
  int digits = (int)std::lround(ohms / std::pow(10.0, exp));
  if (digits >= 100)
  {
    digits /= 10;
    exp++;
  }
  out[0] = c[std::clamp(digits / 10, 0, 9)];
  out[1] = c[std::clamp(digits % 10, 0, 9)];
  out[2] = c[std::clamp(exp, 0, 9)];
  out[3] = IColor(255, 200, 160, 50);
}

inline const char* SocketRoleName(int role)
{
  static const char* n[4] = {"EMPTY", "GAIN", "FOLLOWER", "BOOST"};
  return n[std::clamp(role, 0, 3)];
}
inline const char* TubeTypeName(int type)
{
  static const char* n[4] = {"12AX7", "12AT7", "12AU7", "5751"};
  return n[std::clamp(type, 0, 3)];
}

// A point or direction in the scene (metres): x right along the chassis, y up, z out of the front panel.
struct V3
{
  double x, y, z;
};
inline V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(const V3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }

// The layout (metres). The chassis top is y = 0; the bench it stands on y = kFloorY.
constexpr double kHalfW = 0.33, kHalfD = 0.13, kFloorY = -0.075;
constexpr double kPreampZ = -0.075, kPowerZ = -0.068, kWireZ = -0.047;
// Each preamp tube's x along the back, by TubeSlot (V1, V2, V3, V4, V5, V6): signal order is
// V1 V2 V5 V6 V3 V4.
inline double TubeX(int slot)
{
  static const double x[kNumPreamp] = {-0.295, -0.24, -0.075, -0.02, -0.185, -0.13};
  return x[std::clamp(slot, 0, kNumPreamp - 1)];
}
// The power tubes V9..V12 (V9, V11 on side 1; V10, V12 on side 2).
constexpr double kPowerX[kNumPower] = {0.045, 0.098, 0.151, 0.204};
} // namespace chassis

class NAMTubeChassisControl : public IControl
{
public:
  struct Callbacks
  {
    std::function<chassis::View()> view;
    std::function<int()> selected;
    std::function<void(int item)> select;
    std::function<void(double deltaNormalized)> nudgeVariac; // Dragging its dial
    std::function<void(int link)> go; // Double-click: the item's first link
    float* yaw = nullptr; // The camera (kept by the plugin across relayouts)
    float* pitch = nullptr;
  };

  NAMTubeChassisControl(const IRECT& bounds, Callbacks cb)
  : IControl(bounds)
  , mCb(std::move(cb))
  {
  }

  void Draw(IGraphics& g) override
  {
    using namespace chassis;
    const View v = mCb.view();
    _BeginFrame();
    // The bench: dark, lit from above.
    g.PathRect(mRECT);
    g.PathFill(IPattern::CreateLinearGradient(mRECT.L, mRECT.T, mRECT.L, mRECT.B,
                                              {{IColor(255, 34, 32, 30), 0.f}, {IColor(255, 14, 13, 12), 1.f}}));
    g.PathClipRegion(mRECT);
    for (auto& b : mBox)
      b = ItemBox();
    mCur = -1;
    _DrawFloor(g);
    _DrawChassis(g, v);
    _DrawBoard(g, v);
    _DrawWires(g, v);
    // Everything standing on the chassis or the bench, back to front.
    std::vector<std::pair<double, std::function<void()>>> ops;
    auto add = [&](const V3& at, std::function<void()> fn) { ops.push_back({_Depth(at), std::move(fn)}); };
    for (int slot = 0; slot < kNumPreamp; slot++)
    {
      const int item = slot < 4 ? kTubeV1 + slot : kSocket5 + slot - 4;
      add({TubeX(slot), 0.03, kPreampZ}, [&g, &v, this, slot, item]() { _PreampTube(g, v, slot, item); });
    }
    for (int k = 0; k < kNumPower; k++)
      add({kPowerX[k], 0.05, kPowerZ}, [&g, &v, this, k]() { _PowerTube(g, v, k); });
    add({0.27, 0.03, -0.07}, [&g, this]() { _Transformer(g, kOutputTransformer, {0.27, 0.0, -0.07}, {0.038, 0.03, 0.032}); });
    add({0.265, 0.04, 0.065}, [&g, this]() { _Transformer(g, kPowerSupply, {0.265, 0.0, 0.065}, {0.045, 0.038, 0.04}); });
    for (int k = 0; k < 2; k++)
      add({0.115 + 0.055 * k, 0.025, 0.075}, [&g, this, k]() { _FilterCap(g, 0.115 + 0.055 * k); });
    _AddParts(add, g, v);
    add({0.47, -0.03, 0.03}, [&g, &v, this]() { _Variac(g, v); });
    std::stable_sort(ops.begin(), ops.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (auto& op : ops)
      op.second();
    mCur = -1;
    _DrawLabels(g, v);
    _DrawSelection(g);
    _DrawHud(g, v);
    g.PathClipRegion();
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    mMoved = false;
    mDragVariac = false;
    if (mResetRect.Contains(x, y))
    {
      _ResetView();
      mDownItem = -2;
      return;
    }
    mDownItem = _HitItem(x, y);
    mDragVariac = mDownItem == chassis::kVariac;
    if (mDragVariac)
      _Select(mDownItem);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mDownItem == -2)
      return;
    if (mDragVariac && mCb.nudgeVariac)
    {
      mCb.nudgeVariac((dX - dY) / (mod.S ? 1000.0 : 200.0));
      return;
    }
    mMoved = true;
    if (mCb.yaw && mCb.pitch)
    {
      float yaw = *mCb.yaw + 0.008f * dX;
      if (yaw > 3.14159265f)
        yaw -= 6.2831853f;
      if (yaw < -3.14159265f)
        yaw += 6.2831853f;
      *mCb.yaw = yaw;
      *mCb.pitch = std::clamp(*mCb.pitch + 0.008f * dY, 0.18f, 1.4f);
    }
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (!mMoved && mDownItem >= 0 && !mDragVariac)
      _Select(mDownItem);
    mDownItem = -1;
    mDragVariac = false;
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    const int item = _HitItem(x, y);
    if (item < 0)
    {
      _ResetView();
      return;
    }
    _Select(item);
    int links[2];
    if (chassis::LinksOf(item, links) > 0 && mCb.go)
      mCb.go(links[0]);
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    if (_HitItem(x, y) == chassis::kVariac && mCb.nudgeVariac)
    {
      mCb.nudgeVariac((d > 0 ? 1.0 : -1.0) / (mod.S ? 450.0 : 90.0));
      return;
    }
    mZoom = std::clamp(mZoom * (d > 0 ? 1.1 : 1.0 / 1.1), 0.7, 2.6);
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const int over = _HitItem(x, y);
    if (over != mOver)
    {
      mOver = over;
      SetDirty(false);
    }
  }
  void OnMouseOut() override
  {
    mOver = -1;
    SetDirty(false);
  }

private:
  using V3 = chassis::V3;

  // Where each item landed on screen last frame (for clicks), and how near it is.
  struct ItemBox
  {
    float l = 1e9f, t = 1e9f, r = -1e9f, b = -1e9f;
    double depth = 1e9;
    bool Valid() const { return r >= l; }
    IRECT Rect() const { return IRECT(l, t, r, b); }
  };

  // Camera ======================================================================================
  void _BeginFrame()
  {
    const double yaw = mCb.yaw ? *mCb.yaw : -0.35, pitch = mCb.pitch ? *mCb.pitch : 0.7;
    mCy = std::cos(yaw);
    mSy = std::sin(yaw);
    mCp = std::cos(pitch);
    mSp = std::sin(pitch);
    mDist = 1.5 / mZoom;
    const IRECT a = mRECT;
    mFocal = std::min(a.W() * 1.55f, a.H() * 2.5f); // (Zoom moves the camera in: mDist)
    mCX = a.MW();
    mCY = a.MH() - a.H() * 0.02f;
  }
  // World to view: about the scene's middle, turned by yaw (about y), then tilted by pitch (about x).
  V3 _Rotate(const V3& w) const
  {
    const double x1 = w.x * mCy + w.z * mSy, z1 = -w.x * mSy + w.z * mCy;
    return {x1, w.y * mCp - z1 * mSp, w.y * mSp + z1 * mCp};
  }
  V3 _ToView(const V3& w) const { return _Rotate(w - V3{0.07, -0.025, 0.0}); }
  double _Depth(const V3& w) const { return mDist - _ToView(w).z; }
  bool _P(const V3& w, float& sx, float& sy)
  {
    const V3 v = _ToView(w);
    const double d = mDist - v.z;
    if (d < 0.05)
      return false;
    sx = mCX + (float)(mFocal * v.x / d);
    sy = mCY - (float)(mFocal * v.y / d);
    if (mCur >= 0)
    {
      ItemBox& b = mBox[mCur];
      b.l = std::min(b.l, sx);
      b.r = std::max(b.r, sx);
      b.t = std::min(b.t, sy);
      b.b = std::max(b.b, sy);
      b.depth = std::min(b.depth, d);
    }
    return true;
  }
  // Pixels per metre at a point (for sizes that should shrink with distance).
  float _Scale(const V3& w) const { return (float)(mFocal / std::max(0.05, _Depth(w))); }
  // Whether a face at `c` with outward normal `n` faces the camera.
  bool _Faces(const V3& c, const V3& n) const
  {
    const V3 cv = _ToView(c), nv = _Rotate(n);
    return nv.x * -cv.x + nv.y * -cv.y + nv.z * (mDist - cv.z) > 0.0;
  }
  // Lambert shading from a light above, a little to the left and in front.
  static IColor _Shade(const IColor& c, const V3& n)
  {
    constexpr double lx = -0.36, ly = 0.83, lz = 0.43;
    const double k = 0.38 + 0.62 * std::max(0.0, n.x * lx + n.y * ly + n.z * lz);
    return IColor(c.A, std::min(255, (int)(c.R * k)), std::min(255, (int)(c.G * k)), std::min(255, (int)(c.B * k)));
  }
  void _ResetView()
  {
    if (mCb.yaw && mCb.pitch)
    {
      *mCb.yaw = -0.35f;
      *mCb.pitch = 0.7f;
    }
    mZoom = 1.0;
    SetDirty(false);
  }
  void _Select(int item)
  {
    if (mCb.select)
      mCb.select(item);
    SetDirty(false);
  }
  int _HitItem(float x, float y) const
  {
    int best = -1;
    double bestDepth = 1e9;
    for (int i = 0; i < chassis::kNumItems; i++)
      if (mBox[i].Valid() && mBox[i].Rect().GetPadded(3.f).Contains(x, y) && mBox[i].depth < bestDepth)
      {
        best = i;
        bestDepth = mBox[i].depth;
      }
    return best;
  }

  // Primitives ==================================================================================
  void _Line(IGraphics& g, const IColor& c, const V3& a, const V3& b, float thick = 1.f)
  {
    float x1, y1, x2, y2;
    if (_P(a, x1, y1) && _P(b, x2, y2))
      g.DrawLine(c, x1, y1, x2, y2, nullptr, thick);
  }
  // A flat convex polygon (world points), shaded by its normal if it faces the camera.
  void _Poly(IGraphics& g, const V3* p, int n, const V3& normal, const IColor& c, float edge = 0.f, bool cull = true)
  {
    V3 mid{0, 0, 0};
    for (int i = 0; i < n; i++)
      mid = mid + p[i];
    mid = mid * (1.0 / n);
    if (cull && !_Faces(mid, normal))
      return;
    float xs[40], ys[40];
    for (int i = 0; i < n && i < 40; i++)
      if (!_P(p[i], xs[i], ys[i]))
        return;
    g.FillConvexPolygon(_Shade(c, normal), xs, ys, std::min(n, 40));
    if (edge > 0.f)
      for (int i = 0; i < n; i++)
        g.DrawLine(COLOR_BLACK.WithOpacity(edge), xs[i], ys[i], xs[(i + 1) % n], ys[(i + 1) % n]);
  }
  // An axis-aligned box: centre, half sizes.
  void _Box(IGraphics& g, const V3& c, const V3& h, const IColor& col, float edge = 0.25f)
  {
    const double x0 = c.x - h.x, x1 = c.x + h.x, y0 = c.y - h.y, y1 = c.y + h.y, z0 = c.z - h.z, z1 = c.z + h.z;
    const V3 f[6][4] = {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, // top
                        {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, // front
                        {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}, // back
                        {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}, // left
                        {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}, // right
                        {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}; // bottom
    const V3 n[6] = {{0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}};
    for (int i = 0; i < 6; i++)
      _Poly(g, f[i], 4, n[i], col, edge);
  }
  // A cylinder: centre, axis (0 x, 1 y, 2 z), radius, length; its ends filled with `cap` (alpha 0: open).
  void _Cyl(IGraphics& g, const V3& c, int axis, double r, double len, const IColor& side, const IColor& cap,
            int sides = 14)
  {
    sides = std::clamp(sides, 3, 32);
    auto at = [&](double t, double a) {
      const double u = r * std::cos(a), w = r * std::sin(a);
      return axis == 0 ? V3{c.x + t, c.y + u, c.z + w} : axis == 1 ? V3{c.x + u, c.y + t, c.z + w} : V3{c.x + u, c.y + w, c.z + t};
    };
    auto radial = [&](double a) {
      const double u = std::cos(a), w = std::sin(a);
      return axis == 0 ? V3{0, u, w} : axis == 1 ? V3{u, 0, w} : V3{u, w, 0};
    };
    const double h = len * 0.5, step = 2.0 * 3.14159265358979 / sides;
    for (int i = 0; i < sides; i++)
    {
      const double a0 = i * step, a1 = (i + 1) * step;
      const V3 q[4] = {at(-h, a0), at(-h, a1), at(h, a1), at(h, a0)};
      _Poly(g, q, 4, radial(0.5 * (a0 + a1)), side);
    }
    if (cap.A == 0)
      return;
    V3 ring[32];
    for (int s = -1; s <= 1; s += 2)
    {
      for (int i = 0; i < sides; i++)
        ring[i] = at(s * h, i * step);
      const V3 n = axis == 0 ? V3{(double)s, 0, 0} : axis == 1 ? V3{0, (double)s, 0} : V3{0, 0, (double)s};
      _Poly(g, ring, sides, n, cap);
    }
  }
  // A flat disc facing +axis (sign > 0) or -axis.
  void _Disc(IGraphics& g, const V3& c, int axis, int sign, double r, const IColor& col, int sides = 20)
  {
    V3 ring[32];
    sides = std::clamp(sides, 3, 32);
    for (int i = 0; i < sides; i++)
    {
      const double a = 2.0 * 3.14159265358979 * i / sides, u = r * std::cos(a), w = r * std::sin(a);
      ring[i] = axis == 0 ? V3{c.x, c.y + u, c.z + w} : axis == 1 ? V3{c.x + u, c.y, c.z + w} : V3{c.x + u, c.y + w, c.z};
    }
    const V3 n = axis == 0 ? V3{(double)sign, 0, 0} : axis == 1 ? V3{0, (double)sign, 0} : V3{0, 0, (double)sign};
    _Poly(g, ring, sides, n, col);
  }
  void _Text(IGraphics& g, const V3& at, const char* s, float size, const IColor& c, const char* font = "Roboto-Regular",
             float minSize = 6.5f)
  {
    float x, y;
    if (!_P(at, x, y))
      return;
    const float px = std::clamp(size * _Scale(at) / 600.f, minSize, size * 1.6f);
    g.DrawText(IText(px, c, font), s, IRECT(x - 80.f, y - px, x + 80.f, y + px));
  }

  // The scene ===================================================================================
  void _DrawFloor(IGraphics& g)
  {
    using namespace chassis;
    const V3 floor[4] = {{-0.6, kFloorY, 0.45}, {0.75, kFloorY, 0.45}, {0.75, kFloorY, -0.45}, {-0.6, kFloorY, -0.45}};
    _Poly(g, floor, 4, {0, 1, 0}, IColor(255, 52, 44, 38), 0.f, false);
    for (int i = -6; i <= 7; i++)
      _Line(g, COLOR_BLACK.WithOpacity(0.18f), {i * 0.1, kFloorY, -0.45}, {i * 0.1, kFloorY, 0.45});
    // Contact shadows under the chassis and the Variac.
    const V3 s1[4] = {{-kHalfW - 0.02, kFloorY + 0.0005, kHalfD + 0.025}, {kHalfW + 0.02, kFloorY + 0.0005, kHalfD + 0.025},
                      {kHalfW + 0.02, kFloorY + 0.0005, -kHalfD - 0.02}, {-kHalfW - 0.02, kFloorY + 0.0005, -kHalfD - 0.02}};
    _Poly(g, s1, 4, {0, 1, 0}, IColor(150, 0, 0, 0), 0.f, false);
    const V3 s2[4] = {{0.40, kFloorY + 0.0005, 0.1}, {0.54, kFloorY + 0.0005, 0.1}, {0.54, kFloorY + 0.0005, -0.04},
                      {0.40, kFloorY + 0.0005, -0.04}};
    _Poly(g, s2, 4, {0, 1, 0}, IColor(130, 0, 0, 0), 0.f, false);
  }

  void _DrawChassis(IGraphics& g, const chassis::View& v)
  {
    using namespace chassis;
    const IColor steel(255, 168, 172, 178), plate(255, 186, 150, 84), back(255, 92, 95, 100);
    const double y0 = kFloorY, y1 = 0.0;
    // The sides and back first, the deck, then the front panel and what's on it.
    const V3 left[4] = {{-kHalfW, y0, -kHalfD}, {-kHalfW, y0, kHalfD}, {-kHalfW, y1, kHalfD}, {-kHalfW, y1, -kHalfD}};
    const V3 right[4] = {{kHalfW, y0, kHalfD}, {kHalfW, y0, -kHalfD}, {kHalfW, y1, -kHalfD}, {kHalfW, y1, kHalfD}};
    const V3 rear[4] = {{kHalfW, y0, -kHalfD}, {-kHalfW, y0, -kHalfD}, {-kHalfW, y1, -kHalfD}, {kHalfW, y1, -kHalfD}};
    _Poly(g, left, 4, {-1, 0, 0}, steel, 0.4f);
    _Poly(g, right, 4, {1, 0, 0}, steel, 0.4f);
    if (_Faces({0, -0.04, -kHalfD}, {0, 0, -1}))
    {
      _Poly(g, rear, 4, {0, 0, -1}, back, 0.4f);
      _Text(g, {0.0, -0.02, -kHalfD - 0.001}, "8 / 16 OHM     FUSE 2A     MAINS 120V", 10.f, IColor(200, 230, 230, 230));
      mCur = kSpeakerJack;
      _Jack(g, {0.27, -0.035, -kHalfD}, -1);
      _Text(g, {0.27, -0.06, -kHalfD - 0.001}, "SPEAKER", 9.f, IColor(230, 255, 255, 255));
      mCur = -1;
    }
    const V3 deck[4] = {{-kHalfW, y1, kHalfD}, {kHalfW, y1, kHalfD}, {kHalfW, y1, -kHalfD}, {-kHalfW, y1, -kHalfD}};
    _Poly(g, deck, 4, {0, 1, 0}, steel, 0.5f, false);
    // Brushed steel: fine streaks along the deck.
    for (int i = 1; i < 26; i++)
    {
      const double z = -kHalfD + 2.0 * kHalfD * i / 26.0;
      _Line(g, COLOR_WHITE.WithOpacity(0.04f + 0.04f * (float)((i * 7) % 5) / 5.f), {-kHalfW, y1, z}, {kHalfW, y1, z});
    }
    _Line(g, COLOR_WHITE.WithOpacity(0.55f), {-kHalfW, y1, kHalfD}, {kHalfW, y1, kHalfD}, 1.5f);
    if (_Faces({0, -0.04, kHalfD}, {0, 0, 1}))
    {
      const V3 front[4] = {{-kHalfW, y0, kHalfD}, {kHalfW, y0, kHalfD}, {kHalfW, y1, kHalfD}, {-kHalfW, y1, kHalfD}};
      _Poly(g, front, 4, {0, 0, 1}, plate, 0.5f);
      // Silkscreen, the six knobs, the input jack, the pilot light.
      const double fz = kHalfD + 0.0005;
      _Text(g, {-0.175, -0.024, fz}, v.title, 9.f, IColor(255, 40, 30, 20), "Michroma-Regular");
      _Text(g, {-0.175, -0.05, fz}, "HAND WIRED  -  CIRCUIT MODEL", 6.f, IColor(220, 60, 45, 30));
      static const char* names[6] = {"DRIVE", "BASS", "MIDDLE", "TREBLE", "MASTER", "PRESENCE"};
      mCur = kFrontPanel;
      for (int k = 0; k < 6; k++)
      {
        const double x = -0.09 + 0.062 * k, y = -0.032;
        _Cyl(g, {x, y, kHalfD + 0.008}, 2, 0.0115, 0.016, IColor(255, 24, 22, 20), IColor(255, 36, 34, 32), 16);
        const double a = (-135.0 + 270.0 * std::clamp(v.knob[k], 0.0, 1.0)) * 3.14159265 / 180.0;
        _Line(g, COLOR_WHITE, {x, y, kHalfD + 0.0165}, {x + 0.0095 * std::sin(a), y + 0.0095 * std::cos(a), kHalfD + 0.0165},
              2.f);
        _Text(g, {x, -0.057, fz}, names[k], 6.5f, IColor(255, 40, 30, 20));
      }
      mCur = kInputJack;
      _Jack(g, {-0.29, -0.035, kHalfD}, 1);
      _Text(g, {-0.29, -0.058, fz}, "INPUT", 6.5f, IColor(255, 40, 30, 20));
      mCur = -1;
      // Pilot light: lit while the lab is on.
      _Disc(g, {0.29, -0.032, kHalfD + 0.002}, 2, 1, 0.009, v.engaged ? IColor(255, 255, 60, 40) : IColor(255, 90, 30, 25));
      if (v.engaged)
      {
        float x, y;
        if (_P({0.29, -0.032, kHalfD + 0.003}, x, y))
          for (int k = 3; k >= 1; k--)
            g.FillCircle(IColor(255, 255, 70, 40).WithOpacity(0.1f), x, y, 0.009f * _Scale({0.29, -0.032, kHalfD}) * (1.f + 0.5f * k));
      }
    }
    // The cables: the guitar into the input jack, the speaker out of the back.
    _Cable(g, {{-0.29, -0.035, kHalfD + 0.03}, {-0.29, -0.04, kHalfD + 0.09}, {-0.31, -0.06, kHalfD + 0.17},
               {-0.36, kFloorY + 0.006, kHalfD + 0.28}},
           IColor(255, 20, 20, 22), v.capture ? IColor(255, 120, 200, 255) : IColor(255, 214, 170, 90));
    _Cable(g, {{0.27, -0.035, -kHalfD - 0.03}, {0.27, -0.045, -kHalfD - 0.09}, {0.29, -0.065, -kHalfD - 0.16},
               {0.33, kFloorY + 0.006, -kHalfD - 0.3}},
           IColor(255, 20, 20, 22), IColor(255, 214, 170, 90));
  }

  // A 1/4" jack in a panel facing `sign` (+1 front, -1 back), with a plug in it.
  void _Jack(IGraphics& g, const V3& c, int sign)
  {
    _Disc(g, {c.x, c.y, c.z + sign * 0.0006}, 2, sign, 0.0085, IColor(255, 200, 202, 208));
    _Disc(g, {c.x, c.y, c.z + sign * 0.001}, 2, sign, 0.0045, IColor(255, 10, 10, 10));
    _Cyl(g, {c.x, c.y, c.z + sign * 0.016}, 2, 0.0065, 0.03, IColor(255, 190, 192, 198), IColor(255, 120, 122, 128), 12);
  }
  void _Cable(IGraphics& g, const std::vector<V3>& pts, const IColor& jacket, const IColor& tint)
  {
    for (size_t i = 0; i + 1 < pts.size(); i++)
    {
      const float w = std::max(1.5f, 0.006f * _Scale(pts[i]));
      _Line(g, jacket, pts[i], pts[i + 1], w);
      _Line(g, tint.WithOpacity(0.25f), pts[i], pts[i + 1], 1.f);
    }
  }

  void _DrawBoard(IGraphics& g, const chassis::View& v)
  {
    using namespace chassis;
    // The turret board: brown fibreglass on standoffs, a row of brass turrets along each edge.
    const double y = 0.006;
    _Box(g, {-0.125, y, 0.06}, {0.19, 0.0018, 0.055}, IColor(255, 120, 82, 40), 0.4f);
    for (int i = 0; i < 18; i++)
      for (int row = 0; row < 2; row++)
      {
        const double x = -0.305 + i * 0.0212, z = row == 0 ? 0.015 : 0.105;
        _Cyl(g, {x, y + 0.004, z}, 1, 0.0016, 0.005, IColor(255, 200, 165, 90), IColor(255, 230, 200, 120), 6);
      }
  }

  // The signal's path along the deck, lit by what each stage passes on.
  void _DrawWires(IGraphics& g, const chassis::View& v)
  {
    using namespace chassis;
    const double y = 0.0012;
    auto lit = [](double level) {
      return std::clamp((20.0 * std::log10(std::max(level, 1e-6)) + 40.0) / 40.0, 0.0, 1.0);
    };
    auto wire = [&](const V3& a, const V3& b, double glow, bool on) {
      const IColor base = on ? IColor(255, 214, 170, 90) : IColor(255, 110, 100, 85);
      const float w = std::max(1.2f, 0.0025f * _Scale(a));
      _Line(g, COLOR_BLACK.WithOpacity(0.5f), a, b, w + 1.5f);
      _Line(g, base.WithOpacity(on ? 0.55f + 0.45f * (float)glow : 0.35f), a, b, w);
      if (on && glow > 0.05)
        _Line(g, IColor(255, 255, 235, 170).WithOpacity((float)glow * 0.6f), a, b, std::max(1.f, w * 0.45f));
    };
    // In from the input jack, up to V1.
    const bool live = v.circuit && !v.capture;
    wire({-0.29, y, kHalfD - 0.004}, {-0.315, y, 0.11}, lit(v.level[0]), v.engaged);
    wire({-0.315, y, 0.11}, {-0.315, y, kWireZ}, lit(v.level[0]), v.engaged);
    wire({-0.315, y, kWireZ}, {TubeX(0), y, kWireZ}, lit(v.level[0]), v.engaged);
    // V1 > V2 > V5 > V6 > V3 > V4, each leg lit by the stage it leaves.
    static const int order[kNumPreamp] = {0, 1, 4, 5, 2, 3};
    int from = 0;
    for (int i = 1; i < kNumPreamp; i++)
    {
      const int slot = order[i];
      wire({TubeX(order[i - 1]), y, kWireZ}, {TubeX(slot), y, kWireZ}, lit(v.level[from]), live && v.engaged);
      if (v.inUse[slot])
        from = slot;
    }
    // The phase inverter's two outputs to the power tubes, their plates to the output transformer, and
    // out to the speaker jack.
    const double pi = lit(v.level[3]);
    wire({TubeX(3), y, kWireZ}, {kPowerX[0], y, kWireZ}, pi, v.powerAmp);
    for (int k = 1; k < kNumPower; k++) // (A pair: V11 and V12's sockets are wired, dark)
      wire({kPowerX[k - 1], y, kWireZ}, {kPowerX[k], y, kWireZ}, pi, v.powerAmp && (v.quad || k < 2));
    wire({kPowerX[kNumPower - 1], y, kWireZ}, {0.27, y, kWireZ}, pi, v.powerAmp);
    wire({0.27, y, kWireZ}, {0.31, y, kWireZ}, pi, v.engaged);
    wire({0.31, y, kWireZ}, {0.31, y, -kHalfD + 0.004}, pi, v.engaged);
    // Hook-up wire from the board's two groups to V1's and V2's sockets.
    for (int i = 0; i < 2; i++)
      _Line(g, IColor(255, 220, 200, 60).WithOpacity(i == 1 && !v.inUse[1] ? 0.25f : 0.75f),
            {i == 0 ? -0.25 : -0.10, 0.008, 0.012}, {TubeX(i), 0.002, kPreampZ + 0.012}, 1.4f);
  }

  // A preamp tube (or an empty socket) at TubeSlot `slot`.
  void _PreampTube(IGraphics& g, const chassis::View& v, int slot, int item)
  {
    using namespace chassis;
    mCur = item;
    const double x = TubeX(slot), z = kPreampZ, r = 0.0105, h = 0.058, y0 = 0.0075;
    _Cyl(g, {x, 0.0035, z}, 1, r * 1.5, 0.007, IColor(255, 30, 30, 32), IColor(255, 52, 50, 50), 14);
    const bool empty = IsSocket(item) && v.socketRole[item - kSocket5] == 0;
    if (empty)
    {
      // An empty socket: its nine pin holes.
      for (int p = 0; p < 9; p++)
      {
        const double a = 2.0 * 3.14159265 * p / 10.0;
        float sx, sy;
        if (_P({x + 0.0075 * std::cos(a), 0.0072, z + 0.0075 * std::sin(a)}, sx, sy))
          g.FillCircle(COLOR_BLACK, sx, sy, std::max(0.8f, 0.0012f * _Scale({x, 0, z})));
      }
      // So the empty socket can still be clicked: its footprint counts as the item.
      float sx, sy;
      _P({x, 0.03, z}, sx, sy);
      mCur = -1;
      return;
    }
    const bool used = v.inUse[slot] && v.circuit && !v.capture;
    const double sig = used ? std::clamp((20.0 * std::log10(std::max(v.level[slot], 1e-6)) + 40.0) / 40.0, 0.0, 1.0) : 0.0;
    const double glow = used ? std::pow(std::clamp(v.heat, 0.5, 1.3), 4.0) * (0.7 + 0.4 * sig) : 0.0;
    _Bottle(g, {x, y0, z}, r, h, glow, v.heat, 0.0, false, false);
    mCur = -1;
  }

  // A tube's bottle standing at `base`: plates, heater glow, glass, getter. redPlate 0..1 colours the
  // plates; dead: dark, its getter gone milky (air got in).
  void _Bottle(IGraphics& g, const V3& base, double r, double h, double glow, double heat, double redPlate, bool dead,
               bool power)
  {
    const IColor gc = chassis::Glow(heat);
    const double x = base.x, z = base.z, y0 = base.y;
    // Mica spacers, the plates (two for a twin triode, one big one for a power tube), the heater.
    const IColor plateCol = IColor(255, (int)(70 + 185 * redPlate), (int)(70 - 20 * redPlate), (int)(74 - 44 * redPlate));
    _Box(g, {x, y0 + h * 0.74, z}, {r * 0.82, 0.0006, r * 0.62}, IColor(200, 200, 200, 205), 0.f);
    if (power)
      _Box(g, {x, y0 + h * 0.46, z}, {r * 0.52, h * 0.24, r * 0.4}, plateCol, 0.35f);
    else
      for (int s = -1; s <= 1; s += 2)
        _Box(g, {x + s * r * 0.4, y0 + h * 0.46, z}, {r * 0.27, h * 0.24, r * 0.36}, plateCol, 0.35f);
    _Box(g, {x, y0 + h * 0.18, z}, {r * 0.82, 0.0006, r * 0.62}, IColor(200, 200, 200, 205), 0.f);
    float mx, my;
    const bool onScreen = _P({x, y0 + h * 0.46, z}, mx, my);
    const float px = _Scale({x, y0, z});
    if (onScreen && redPlate > 0.0)
      for (int k = 4; k >= 1; k--)
        g.FillCircle(IColor(255, 255, 70, 30).WithOpacity((float)std::min(0.5, 0.08 * redPlate)), mx, my,
                     (float)(r * px * (0.8 + 0.4 * k)));
    if (onScreen && glow > 0.0)
    {
      for (int k = 4; k >= 1; k--)
        g.FillCircle(gc.WithOpacity((float)std::min(0.5, 0.045 * glow)), mx, my, (float)(r * px * (1.0 + 0.5 * k)));
      const float a = (float)std::min(1.0, 0.35 + 0.5 * glow);
      _Line(g, gc.WithOpacity(a), {x, y0 + h * (power ? 0.12 : 0.24), z}, {x, y0 + h * 0.68, z}, std::max(1.5f, (float)(r * px * 0.22)));
    }
    // Glass: a faint envelope, the getter's silver (or milky, failed) flash at the top, the tip.
    const IColor glass = IColor(255, 205, 220, 232).WithOpacity(0.11f);
    _Cyl(g, {x, y0 + h * 0.5, z}, 1, r, h, glass, IColor(0, 0, 0, 0), power ? 18 : 14);
    _Cyl(g, {x, y0 + h * 0.915, z}, 1, r * 1.002, h * 0.17,
         dead ? IColor(255, 235, 235, 230).WithOpacity(0.8f) : IColor(255, 175, 180, 190).WithOpacity(0.6f),
         dead ? IColor(255, 240, 240, 236) : IColor(255, 150, 155, 165), power ? 18 : 14);
    _Cyl(g, {x, y0 + h + 0.0025, z}, 1, r * 0.22, 0.005, IColor(255, 160, 165, 175).WithOpacity(0.7f),
         IColor(255, 190, 195, 205), 6);
    // Highlights down the lit side.
    float tx, ty, bx, by;
    if (_P({x, y0 + h * 0.85, z}, tx, ty) && _P({x, y0 + h * 0.1, z}, bx, by))
    {
      const float rr = (float)(r * px);
      g.DrawLine(COLOR_WHITE.WithOpacity(0.3f), tx - rr * 0.55f, ty, bx - rr * 0.55f, by, nullptr, std::max(1.f, rr * 0.18f));
      g.DrawLine(COLOR_WHITE.WithOpacity(0.12f), tx + rr * 0.6f, ty, bx + rr * 0.6f, by, nullptr, 1.f);
    }
  }

  void _PowerTube(IGraphics& g, const chassis::View& v, int k)
  {
    using namespace chassis;
    mCur = kPowerTube1 + k;
    const double x = kPowerX[k], z = kPowerZ, r = v.sixL6 ? 0.0165 : 0.0175, h = v.sixL6 ? 0.1 : 0.094;
    // Octal socket and the brown bakelite base.
    _Cyl(g, {x, 0.0035, z}, 1, r * 1.45, 0.007, IColor(255, 30, 30, 32), IColor(255, 52, 50, 50), 16);
    if (k >= 2 && !v.quad)
    {
      // A pair: V11 and V12 are empty octal sockets (eight pin holes and the key).
      for (int p = 0; p < 8; p++)
      {
        const double ang = 2.0 * 3.14159265 * p / 9.0;
        float sx, sy;
        if (_P({x + 0.011 * std::cos(ang), 0.0072, z + 0.011 * std::sin(ang)}, sx, sy))
          g.FillCircle(COLOR_BLACK, sx, sy, std::max(0.9f, 0.0016f * _Scale({x, 0, z})));
      }
      float sx, sy;
      _P({x, 0.035, z}, sx, sy); // (So the empty socket can be clicked)
      mCur = -1;
      return;
    }
    _Cyl(g, {x, 0.017, z}, 1, r * 0.95, 0.02, IColor(255, 92, 58, 36), IColor(255, 110, 72, 46), 16);
    const bool working = v.powerAmp && v.circuit && !v.dead[k];
    const double glow = working ? std::pow(std::clamp(v.heat, 0.5, 1.3), 4.0) * 0.8 : 0.0;
    const double red = working ? std::clamp((v.dissipation[k] - 0.85) / 0.4, 0.0, 1.0) : 0.0;
    _Bottle(g, {x, 0.027, z}, r, h, glow, v.heat, red, v.dead[k], true);
    mCur = -1;
  }

  // A transformer: a laminated core between two black end bells.
  void _Transformer(IGraphics& g, int item, const V3& at, const V3& half)
  {
    mCur = item;
    const V3 c{at.x, half.y, at.z};
    _Box(g, c, {half.x * 0.62, half.y, half.z}, IColor(255, 70, 72, 76), 0.4f);
    for (int s = -1; s <= 1; s += 2)
      _Box(g, {c.x + s * half.x * 0.81, c.y, c.z}, {half.x * 0.19, half.y * 0.92, half.z * 1.04}, IColor(255, 26, 26, 28), 0.5f);
    // The core's laminations on the faces you can see.
    for (int i = 1; i < 10; i++)
    {
      const double y = half.y * 2.0 * i / 10.0;
      if (_Faces({c.x, c.y, c.z + half.z}, {0, 0, 1}))
        _Line(g, COLOR_BLACK.WithOpacity(0.35f), {c.x - half.x * 0.62, y, c.z + half.z + 0.0003}, {c.x + half.x * 0.62, y, c.z + half.z + 0.0003});
      if (_Faces({c.x, c.y, c.z - half.z}, {0, 0, -1}))
        _Line(g, COLOR_BLACK.WithOpacity(0.35f), {c.x - half.x * 0.62, y, c.z - half.z - 0.0003}, {c.x + half.x * 0.62, y, c.z - half.z - 0.0003});
    }
    mCur = -1;
  }

  void _FilterCap(IGraphics& g, double x)
  {
    mCur = chassis::kPowerSupply;
    _Cyl(g, {x, 0.026, 0.075}, 1, 0.016, 0.052, IColor(255, 60, 80, 150), IColor(255, 190, 195, 205), 18);
    _Cyl(g, {x, 0.053, 0.075}, 1, 0.0165, 0.003, IColor(255, 200, 205, 215), IColor(255, 210, 214, 222), 18);
    mCur = -1;
  }

  template <typename Add>
  void _AddParts(Add& add, IGraphics& g, const chassis::View& v)
  {
    using namespace chassis;
    struct P
    {
      int item, kind; // kind: 0 orange drop, 1 electrolytic, 2 silver mica, 3 resistor, 4 pot
      double x, z;
      bool live;
      double ohms, pos;
    };
    const bool v2 = v.inUse[1];
    const P parts[kNumParts] = {
      {kInputCap, 0, -0.29, 0.085, true, 0, 0},  {kRk1, 3, -0.29, 0.035, true, v.rk[0], 0},
      {kCk1, 1, -0.235, 0.035, true, 0, 0},      {kRa, 3, -0.235, 0.085, true, v.raKOhm * 1000.0, 0},
      {kCp1, 2, -0.18, 0.085, true, 0, 0},       {kCouple, 0, -0.125, 0.085, v2, 0, 0},
      {kGain2Pot, 4, -0.125, 0.035, v2, 0, v.gain2}, {kRk2, 3, -0.07, 0.035, v2, v.rk[1], 0},
      {kCk2, 1, -0.015, 0.035, v2, 0, 0},        {kCp2, 2, -0.07, 0.085, v2, 0, 0}};
    for (const P& p : parts)
      add({p.x, 0.012, p.z}, [&g, this, p]() { _Part(g, p.item, p.kind, p.x, p.z, p.live, p.ohms, p.pos); });
  }

  void _Part(IGraphics& g, int item, int kind, double x, double z, bool live, double ohms, double pos)
  {
    mCur = item;
    const double y = 0.0115;
    auto dim = [live](const IColor& c) { return live ? c : IColor(c.A, c.R / 2 + 30, c.G / 2 + 30, c.B / 2 + 30); };
    // Leads out to the turrets.
    _Line(g, IColor(255, 190, 190, 195), {x - 0.022, 0.009, z}, {x + 0.022, 0.009, z}, 1.2f);
    switch (kind)
    {
      case 0: // Orange drop: a dipped slab standing on its leads
        _Box(g, {x, y + 0.004, z}, {0.012, 0.008, 0.0045}, dim(IColor(255, 228, 108, 32)), 0.3f);
        break;
      case 1: // Electrolytic: a sleeved can lying down, its silver end
        _Cyl(g, {x - 0.002, y, z}, 0, 0.0048, 0.02, dim(IColor(255, 40, 70, 165)), dim(IColor(255, 60, 90, 180)), 12);
        _Cyl(g, {x + 0.0095, y, z}, 0, 0.0049, 0.003, dim(IColor(255, 195, 200, 210)), dim(IColor(255, 205, 210, 220)), 12);
        break;
      case 2: // Silver mica: a small caramel-dipped chip
        _Box(g, {x, y + 0.001, z}, {0.006, 0.005, 0.0022}, dim(IColor(255, 205, 150, 85)), 0.3f);
        break;
      case 3: // Carbon comp resistor: a tan body and its colour bands
      {
        IColor bands[4];
        chassis::ResistorBands(ohms, bands);
        const double seg[8] = {-0.012, -0.007, -0.0045, -0.002, 0.0005, 0.005, 0.0075, 0.012};
        const IColor col[7] = {IColor(255, 205, 180, 140), bands[0], bands[1], bands[2],
                               IColor(255, 205, 180, 140), bands[3], IColor(255, 205, 180, 140)};
        for (int i = 0; i < 7; i++)
          _Cyl(g, {x + 0.5 * (seg[i] + seg[i + 1]), y - 0.002, z}, 0, 0.0036, seg[i + 1] - seg[i], dim(col[i]),
               dim(IColor(255, 190, 165, 125)), 10);
        break;
      }
      default: // Pot: its can, the shaft, a knob with its pointer at the setting
      {
        _Cyl(g, {x, y - 0.001, z}, 1, 0.0095, 0.007, dim(IColor(255, 120, 122, 128)), dim(IColor(255, 150, 152, 158)), 16);
        _Cyl(g, {x, y + 0.008, z}, 1, 0.006, 0.01, dim(IColor(255, 28, 28, 28)), dim(IColor(255, 40, 40, 40)), 14);
        const double a = (-135.0 + 270.0 * std::clamp(pos, 0.0, 1.0)) * 3.14159265 / 180.0;
        _Line(g, COLOR_WHITE, {x, y + 0.0131, z}, {x + 0.0055 * std::sin(a), y + 0.0131, z - 0.0055 * std::cos(a)}, 1.5f);
        break;
      }
    }
    mCur = -1;
  }

  void _Variac(IGraphics& g, const chassis::View& v)
  {
    using namespace chassis;
    mCur = kVariac;
    const double x = 0.47, z = 0.03, top = kFloorY + 0.09;
    _Box(g, {x, kFloorY + 0.045, z}, {0.06, 0.045, 0.06}, IColor(255, 70, 74, 80), 0.5f);
    const V3 dial{x, top + 0.0006, z};
    _Disc(g, dial, 1, 1, 0.046, IColor(255, 232, 224, 200), 36);
    auto dir = [](double volts) {
      const double a = (-130.0 + 260.0 * (volts - 90.0) / 45.0) * 3.14159265 / 180.0;
      return V3{std::sin(a), 0.0, -std::cos(a)};
    };
    for (int volts = 90; volts <= 135; volts += 5)
    {
      const V3 d = dir(volts);
      const bool red = volts > 125;
      _Line(g, red ? IColor(255, 200, 40, 30) : COLOR_BLACK, dial + d * 0.036, dial + d * 0.044, volts % 15 == 0 ? 2.f : 1.f);
      if (volts % 15 == 0)
        _Text(g, dial + d * 0.026, std::to_string(volts).c_str(), 7.f, red ? IColor(255, 200, 40, 30) : COLOR_BLACK);
    }
    _Cyl(g, {x, top + 0.007, z}, 1, 0.013, 0.013, IColor(255, 26, 26, 26), IColor(255, 36, 36, 36), 16);
    const V3 d = dir(v.variacV);
    _Line(g, COLOR_WHITE, {x, top + 0.0136, z}, V3{x, top + 0.0136, z} + d * 0.012, 2.f);
    _Line(g, IColor(255, 200, 40, 30), {x, top + 0.0012, z}, dial + d * 0.045, 1.2f);
    if (_Faces({x, kFloorY + 0.045, z + 0.06}, {0, 0, 1}))
    {
      _Text(g, {x, kFloorY + 0.06, z + 0.0605}, "VARIAC", 9.f, IColor(255, 214, 170, 90), "Michroma-Regular");
      char t[24];
      snprintf(t, sizeof(t), "%.1f V", v.variacV);
      _Text(g, {x, kFloorY + 0.03, z + 0.0605}, t, 10.f, v.variacV > 125.0 ? IColor(255, 255, 120, 90) : COLOR_WHITE);
    }
    mCur = -1;
    // Its cord into the chassis.
    _Cable(g, {{x - 0.06, kFloorY + 0.02, z - 0.02}, {0.37, kFloorY + 0.004, z - 0.05}, {kHalfW + 0.005, -0.05, -0.1}},
           IColor(255, 16, 16, 16), IColor(255, 90, 90, 90));
  }

  // Each tube's name and job under it, on top of everything so it stays legible.
  void _DrawLabels(IGraphics& g, const chassis::View& v)
  {
    using namespace chassis;
    static const char* jobs[4] = {"INPUT", "GAIN 2", "FOLLOWER", "PHASE INV"};
    for (int slot = 0; slot < kNumPreamp; slot++)
    {
      const int item = slot < 4 ? kTubeV1 + slot : kSocket5 + slot - 4;
      const bool sock = slot >= 4;
      const int k = slot - 4;
      char name[8], job[32];
      snprintf(name, sizeof(name), "V%d", slot + 1);
      if (sock)
        snprintf(job, sizeof(job), "%s%s%s", SocketRoleName(v.socketRole[k]), v.socketRole[k] ? " " : "",
                 v.socketRole[k] ? TubeTypeName(v.socketTube[k]) : "");
      else
        snprintf(job, sizeof(job), "%s", jobs[slot]);
      _Label(g, {TubeX(slot), 0.0, kPreampZ + 0.02}, name, job, v.inUse[slot] && v.circuit && !v.capture,
             mCb.selected && mCb.selected() == item, mOver == item);
    }
    static const char* powerNames[kNumPower] = {"V9", "V10", "V11", "V12"};
    for (int k = 0; k < kNumPower; k++)
    {
      const bool fitted = k < 2 || v.quad;
      const char* state = !fitted        ? "EMPTY"
                          : !v.powerAmp  ? "OFF"
                          : v.dead[k]    ? "FAILED"
                          : v.dissipation[k] > 1.0 ? "RED PLATING"
                          : v.sixL6      ? "6L6"
                                         : "EL34";
      _Label(g, {kPowerX[k], 0.0, kPowerZ + 0.028}, powerNames[k], state, fitted && v.powerAmp && !v.dead[k],
             mCb.selected && mCb.selected() == kPowerTube1 + k, mOver == kPowerTube1 + k);
    }
    // The selected (or hovered) part's value.
    const int show = mOver >= 0 && IsPart(mOver) ? mOver : mCb.selected && IsPart(mCb.selected()) ? mCb.selected() : -1;
    if (show >= 0 && mBox[show].Valid())
    {
      const IRECT r = mBox[show].Rect();
      const IRECT t(r.MW() - 50.f, r.B + 3.f, r.MW() + 50.f, r.B + 16.f);
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.65f), t, 3.f);
      g.DrawText(IText(9.5f, COLOR_WHITE, "Roboto-Regular"), v.value[show].c_str(), t);
    }
  }
  // A tube's name tag; with its job too while it's selected or under the mouse (or `showJob`).
  void _Label(IGraphics& g, const V3& at, const char* name, const char* job, bool on, bool selected, bool showJob)
  {
    float x, y;
    if (!_P(at, x, y))
      return;
    const float s = std::clamp(_Scale(at) / 650.f, 0.7f, 1.3f);
    const bool full = selected || showJob;
    const float w = full ? 36.f * s : 12.f * s;
    const IRECT r(x - w, y + 1.f, x + w, y + 1.f + (full ? 20.f : 10.f) * s);
    g.FillRoundRect(COLOR_BLACK.WithOpacity(selected ? 0.8f : 0.55f), r, 3.f);
    if (selected)
      g.DrawRoundRect(IColor(255, 214, 170, 90), r, 3.f);
    const IColor nameCol = on ? IColor(255, 255, 214, 150) : COLOR_WHITE.WithOpacity(0.45f);
    if (!full)
    {
      g.DrawText(IText(7.5f * s, nameCol, "Michroma-Regular"), name, r);
      return;
    }
    g.DrawText(IText(8.5f * s, nameCol, "Michroma-Regular"), name, r.GetFromTop(r.H() * 0.5f));
    g.DrawText(IText(7.5f * s, COLOR_WHITE.WithOpacity(on ? 0.85f : 0.45f), "Roboto-Regular"), job, r.GetFromBottom(r.H() * 0.5f));
  }

  void _DrawSelection(IGraphics& g)
  {
    const int sel = mCb.selected ? mCb.selected() : -1;
    if (mOver >= 0 && mOver != sel && mBox[mOver].Valid())
      g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.45f), mBox[mOver].Rect().GetPadded(4.f), 4.f);
    if (sel >= 0 && sel < chassis::kNumItems && mBox[sel].Valid())
      g.DrawRoundRect(IColor(255, 214, 170, 90), mBox[sel].Rect().GetPadded(5.f), 5.f, nullptr, 2.f);
  }

  void _DrawHud(IGraphics& g, const chassis::View& v)
  {
    const IRECT a = mRECT.GetPadded(-8.f);
    g.DrawText(IText(9.5f, COLOR_WHITE.WithOpacity(0.5f), "Roboto-Regular", EAlign::Near, EVAlign::Top),
               "Drag to turn  -  wheel to zoom  -  click a part  -  double-click to go to its page", a);
    mResetRect = IRECT(a.R - 84.f, a.T, a.R, a.T + 20.f);
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.45f), mResetRect, 4.f);
    g.DrawRoundRect(IColor(255, 214, 170, 90).WithOpacity(0.6f), mResetRect, 4.f);
    g.DrawText(IText(9.5f, COLOR_WHITE.WithOpacity(0.85f), "Roboto-Regular"), "RESET VIEW", mResetRect);
    if (!v.circuit || v.capture)
    {
      const IRECT b = IRECT(a.L, a.B - 24.f, a.R, a.B);
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.6f), b, 4.f);
      g.DrawText(IText(10.f, IColor(255, 255, 185, 60), "Roboto-Regular"),
                 !v.circuit ? "The preamp tubes are the Circuit engine's: switch the Tube Lab to CIRCUIT."
                            : "The AMP capture is the preamp: its tubes sit cold (the power amp still runs).",
                 b);
    }
  }

  Callbacks mCb;
  ItemBox mBox[chassis::kNumItems];
  int mCur = -1; // The item being drawn (its screen box grows with every point projected)
  double mCy = 1.0, mSy = 0.0, mCp = 1.0, mSp = 0.0, mDist = 1.5, mZoom = 1.0;
  float mFocal = 500.f, mCX = 0.f, mCY = 0.f;
  IRECT mResetRect;
  int mDownItem = -1, mOver = -1;
  bool mMoved = false, mDragVariac = false;
};

// The links from the selected item to the other pages (chassis::LinksOf), as buttons.
class NAMChassisLinksControl : public IControl
{
public:
  NAMChassisLinksControl(const IRECT& bounds, std::function<int()> selected, std::function<void(int)> go)
  : IControl(bounds)
  , mSelected(std::move(selected))
  , mGo(std::move(go))
  {
  }
  void Draw(IGraphics& g) override
  {
    int links[2];
    mN = chassis::LinksOf(mSelected(), links);
    for (int i = 0; i < mN; i++)
    {
      mLink[i] = links[i];
      mRect[i] = mRECT.SubRectHorizontal(2, i).GetPadded(-2.f);
      const bool over = mMouseIsOver && mOverIdx == i;
      g.FillRoundRect(IColor(255, 214, 170, 90).WithOpacity(over ? 0.45f : 0.22f), mRect[i], 4.f);
      g.DrawRoundRect(IColor(255, 214, 170, 90).WithOpacity(0.8f), mRect[i], 4.f);
      std::string t = std::string("GO TO ") + chassis::LinkName(links[i]);
      g.DrawText(IText(9.5f, COLOR_WHITE, "Roboto-Regular"), t.c_str(), mRect[i]);
    }
  }
  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    for (int i = 0; i < mN; i++)
      if (mRect[i].Contains(x, y) && mGo)
        mGo(mLink[i]);
  }
  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    int idx = -1;
    for (int i = 0; i < mN; i++)
      if (mRect[i].Contains(x, y))
        idx = i;
    if (idx != mOverIdx)
    {
      mOverIdx = idx;
      SetDirty(false);
    }
    IControl::OnMouseOver(x, y, mod);
  }

private:
  std::function<int()> mSelected;
  std::function<void(int)> mGo;
  int mN = 0, mLink[2] = {}, mOverIdx = -1;
  IRECT mRect[2];
};

// The power tubes' health: life, plate dissipation against the rating, what that means, and REPLACE.
class NAMPowerTubeStatusControl : public IControl
{
public:
  struct Reading
  {
    double life[4] = {1.0, 1.0, 1.0, 1.0}, dissipation[4] = {};
    bool dead[4] = {}, powerAmp = false, sixL6 = false, canFail = false, quad = false;
  };
  NAMPowerTubeStatusControl(const IRECT& bounds, std::function<Reading()> read, std::function<void(int)> replace)
  : IControl(bounds)
  , mRead(std::move(read))
  , mReplace(std::move(replace))
  {
  }

  void Draw(IGraphics& g) override
  {
    const Reading r = mRead();
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.28f), mRECT, 6.f);
    const IRECT a = mRECT.GetPadded(-8.f);
    g.DrawText(IText(10.f, PluginColors::NAM_THEMECOLOR, "Michroma-Regular", EAlign::Near, EVAlign::Top),
               r.quad ? (r.sixL6 ? "POWER TUBES (4 x 6L6)" : "POWER TUBES (4 x EL34)")
                      : (r.sixL6 ? "POWER TUBES (6L6)" : "POWER TUBES (EL34)"),
               a);
    if (!r.powerAmp)
    {
      g.DrawText(IText(10.f, COLOR_WHITE.WithOpacity(0.5f), "Roboto-Regular"), "Power amp off", a);
      return;
    }
    const int n = r.quad ? 4 : 2;
    static const char* names[4] = {"V9", "V10", "V11", "V12"};
    const float rowH = std::min(26.f, (a.H() - 30.f) / n);
    for (int k = 0; k < 4; k++)
      mButton[k] = IRECT();
    for (int k = 0; k < n; k++)
    {
      const IRECT row(a.L, a.T + 16.f + k * rowH, a.R, a.T + 16.f + (k + 1) * rowH - 4.f);
      const IRECT bar = row.GetFromLeft(row.W() * 0.36f).GetReducedFromLeft(28.f).GetCentredInside(row.W() * 0.36f - 28.f, 8.f);
      g.DrawText(IText(10.f, COLOR_WHITE.WithOpacity(0.8f), "Roboto-Regular", EAlign::Near), names[k],
                 row.GetFromLeft(26.f));
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.4f), bar, 2.f);
      const IColor lifeCol = r.life[k] > 0.5 ? IColor(255, 80, 200, 110) : r.life[k] > 0.2 ? IColor(255, 255, 185, 60)
                                                                                         : IColor(255, 235, 60, 50);
      g.FillRoundRect(lifeCol, IRECT(bar.L, bar.T, bar.L + bar.W() * (float)std::clamp(r.life[k], 0.0, 1.0), bar.B), 2.f);
      const char* state = r.dead[k]                    ? "FAILED"
                          : r.dissipation[k] > 1.0     ? "RED PLATING"
                          : r.dissipation[k] > 0.75    ? "hot"
                                                       : "ok";
      char t[64];
      snprintf(t, sizeof(t), "%3.0f%%  %3.0f%% diss.  %s", 100.0 * r.life[k], 100.0 * r.dissipation[k], state);
      IText tx(10.f, r.dead[k] || r.dissipation[k] > 1.0 ? IColor(255, 255, 110, 80) : COLOR_WHITE.WithOpacity(0.8f),
               "Roboto-Regular", EAlign::Near);
      g.DrawText(tx, t, IRECT(bar.R + 8.f, row.T, row.R - 64.f, row.B));
      mButton[k] = row.GetFromRight(60.f).GetPadded(-1.f);
      const bool can = r.life[k] < 0.999;
      g.FillRoundRect(can ? PluginColors::NAM_THEMECOLOR.WithOpacity(0.5f) : COLOR_BLACK.WithOpacity(0.3f), mButton[k], 3.f);
      g.DrawText(IText(9.5f, COLOR_WHITE.WithOpacity(can ? 1.f : 0.35f), "Roboto-Regular"), "REPLACE", mButton[k]);
    }
    if (!r.canFail)
      g.DrawText(IText(9.5f, COLOR_WHITE.WithOpacity(0.5f), "Roboto-Regular", EAlign::Near, EVAlign::Bottom),
                 "Failing is off: they never wear out.", a);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    for (int k = 0; k < 4; k++)
      if (mButton[k].Contains(x, y) && mReplace)
        mReplace(k);
    SetDirty(false);
  }

private:
  std::function<Reading()> mRead;
  std::function<void(int)> mReplace;
  IRECT mButton[4];
};

// A few lines of text from a function (the selected part's name and what it does).
class NAMDynamicTextControl : public IControl
{
public:
  NAMDynamicTextControl(const IRECT& bounds, std::function<std::pair<std::string, std::string>()> text)
  : IControl(bounds)
  , mText(std::move(text))
  {
    mIgnoreMouse = true;
  }
  void Draw(IGraphics& g) override
  {
    const auto t = mText();
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.28f), mRECT, 6.f);
    const IRECT a = mRECT.GetPadded(-8.f);
    g.DrawText(IText(11.f, IColor(255, 214, 170, 90), "Michroma-Regular", EAlign::Near, EVAlign::Top), t.first.c_str(), a);
    const IText body(10.f, COLOR_WHITE.WithOpacity(0.75f), "Roboto-Regular", EAlign::Near, EVAlign::Top);
    float y = a.T + 18.f;
    for (const std::string& line : tubedebug::Wrap(g, body, t.second, a.W()))
    {
      if (y + 12.f > a.B)
        break;
      g.DrawText(body, line.c_str(), IRECT(a.L, y, a.R, y + 12.f));
      y += 12.5f;
    }
  }

private:
  std::function<std::pair<std::string, std::string>()> mText;
};
