#pragma once

// The MODULAR chassis (1.72.0): the Tube Lab's chassis with its preamp on swappable modules. The same steel
// chassis, power section, Variac and camera as the TUBE LAB chassis (NAMTubeChassisControl, whose drawing
// pieces this reuses), but in place of the turret board and the row of preamp tubes, a backplane with two
// bays, A and B, each holding a preamp module (PreampModules.h) like a memory stick in its slot: a card
// with the module's tubes along its top edge (one bottle for every two triodes, as twin triodes are), its
// name and family colour, the chips and caps of its circuit, and gold contacts in the slot.
// * The bay that's playing glows: its tubes pulse with their own stages' signal, and the signal wire runs
//   from the input jack through its slot to the phase inverter and the power tubes. The other bay's card
//   sits cold.
// * Plug another module into a bay (the side panel's dropdown) and the card lifts out of its slot and the
//   new one drops in.
// * Click a card to see it and set it in the side panel; double-click it to make it the bay that plays.
// Included from NeuralAmpModeler.cpp after TubeChassisControls.h.

#include <chrono>

#include "PreampModules.h"

namespace chassis
{
// What the MODULAR scene needs besides the TUBE LAB chassis's View.
struct ModView
{
  View base;
  int module[2] = {tube::module::kBritMV, tube::module::kCalModern}; // Each bay's (module::ModuleId)
  int playing = 0; // The bay that plays
  double level[tube::module::kMaxStages] = {}; // The playing module's stages' signal (plate swing / 100 V)
};

// The bays side by side along the deck (x), each card standing across it, its face to the front panel.
constexpr double kBayX[2] = {-0.255, -0.125};
constexpr double kBayZ = 0.012, kCardHalfL = 0.058;
constexpr double kPiX = -0.035; // The phase inverter, V4
constexpr double kSwapSeconds = 0.5; // A card out, the new one in

inline IColor FamilyColor(const char* family)
{
  const std::string f = family ? family : "";
  if (f == "HG")
    return IColor(255, 222, 74, 60);
  if (f == "CAL")
    return IColor(255, 80, 150, 232);
  if (f == "US")
    return IColor(255, 234, 216, 172);
  return IColor(255, 214, 170, 90); // BRIT
}

// A module's triodes (gain stages and cathode followers), and the bottles they take.
inline int ModuleTriodes(int id)
{
  const tube::module::ModuleDef& d = tube::module::GetModule(id);
  int n = d.numStages;
  for (int s = 0; s < d.numStages; s++)
    n += d.stage[s].follower ? 1 : 0;
  return n;
}
inline int ModuleBottles(int id) { return (ModuleTriodes(id) + 1) / 2; }
} // namespace chassis

class NAMModularChassisControl : public NAMTubeChassisControl
{
public:
  struct ModCallbacks
  {
    std::function<chassis::ModView()> view;
    std::function<void(int bay)> play; // Double-click a card: it's the bay that plays
  };

  NAMModularChassisControl(const IRECT& bounds, Callbacks cb, ModCallbacks mcb)
  : NAMTubeChassisControl(bounds, std::move(cb))
  , mMod(std::move(mcb))
  {
  }

  // A card swap is still moving (the plugin keeps redrawing while it is).
  bool Animating() const
  {
    for (int b = 0; b < 2; b++)
      if (mSwapFrom[b] >= 0)
        return true;
    return false;
  }

  void Draw(IGraphics& g) override
  {
    using namespace chassis;
    const ModView mv = mMod.view();
    const View& v = mv.base;
    _UpdateSwaps(mv);
    _BeginFrame();
    g.PathRect(mRECT);
    g.PathFill(IPattern::CreateLinearGradient(mRECT.L, mRECT.T, mRECT.L, mRECT.B,
                                              {{IColor(255, 34, 32, 30), 0.f}, {IColor(255, 14, 13, 12), 1.f}}));
    g.PathClipRegion(mRECT);
    for (auto& b : mBox)
      b = ItemBox();
    mCur = -1;
    _DrawFloor(g);
    _DrawChassis(g, v);
    _DrawBackplane(g);
    _DrawModWires(g, mv);
    std::vector<std::pair<double, std::function<void()>>> ops;
    auto add = [&](const V3& at, std::function<void()> fn) { ops.push_back({_Depth(at), std::move(fn)}); };
    for (int bay = 0; bay < 2; bay++)
      add({kBayX[bay], 0.04, kBayZ}, [&g, &mv, this, bay]() { _Card(g, mv, bay); });
    add({kPiX, 0.03, kPreampZ}, [&g, &v, this]() { _PhaseInverter(g, v); });
    for (int k = 0; k < kNumPower; k++)
      add({kPowerX[k], 0.05, kPowerZ}, [&g, &v, this, k]() { _PowerTube(g, v, k); });
    add({0.27, 0.03, -0.07}, [&g, this]() { _Transformer(g, kOutputTransformer, {0.27, 0.0, -0.07}, {0.038, 0.03, 0.032}); });
    add({0.265, 0.04, 0.065}, [&g, this]() { _Transformer(g, kPowerSupply, {0.265, 0.0, 0.065}, {0.045, 0.038, 0.04}); });
    for (int k = 0; k < 2; k++)
      add({0.115 + 0.055 * k, 0.025, 0.075}, [&g, this, k]() { _FilterCap(g, 0.115 + 0.055 * k); });
    add({0.47, -0.03, 0.03}, [&g, &v, this]() { _Variac(g, v); });
    std::stable_sort(ops.begin(), ops.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (auto& op : ops)
      op.second();
    mCur = -1;
    _DrawModLabels(g, mv);
    _DrawSelection(g);
    _DrawHud(g, v);
    g.PathClipRegion();
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    const int item = _HitItem(x, y);
    if (chassis::IsBay(item))
    {
      _Select(item);
      if (mMod.play)
        mMod.play(item - chassis::kBayA);
      return;
    }
    NAMTubeChassisControl::OnMouseDblClick(x, y, mod);
  }

private:
  // A bay whose module changed: the old card lifts out over the first half of kSwapSeconds, the new one
  // drops in over the second.
  void _UpdateSwaps(const chassis::ModView& mv)
  {
    const auto now = std::chrono::steady_clock::now();
    for (int b = 0; b < 2; b++)
    {
      if (mShown[b] < 0)
        mShown[b] = mv.module[b]; // First draw: already in
      if (mv.module[b] != mShown[b] && mSwapFrom[b] < 0)
      {
        mSwapFrom[b] = mShown[b];
        mSwapStart[b] = now;
      }
      mShown[b] = mv.module[b];
      if (mSwapFrom[b] >= 0 && std::chrono::duration<double>(now - mSwapStart[b]).count() >= chassis::kSwapSeconds)
        mSwapFrom[b] = -1;
    }
  }
  // The card in bay b now: which module, and how far out of its slot (metres).
  int _CardModule(int b, double& lift) const
  {
    lift = 0.0;
    if (mSwapFrom[b] < 0)
      return mShown[b];
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - mSwapStart[b]).count()
                     / chassis::kSwapSeconds;
    auto ease = [](double u) { return u * u * (3.0 - 2.0 * u); };
    const double out = 0.16;
    if (t < 0.5)
    {
      lift = out * ease(std::clamp(t * 2.0, 0.0, 1.0));
      return mSwapFrom[b];
    }
    lift = out * (1.0 - ease(std::clamp((t - 0.5) * 2.0, 0.0, 1.0)));
    return mShown[b];
  }

  // The backplane: a black board along the deck's left half, the bays' slots on it.
  void _DrawBackplane(IGraphics& g)
  {
    using namespace chassis;
    _Box(g, {-0.19, 0.003, 0.005}, {0.128, 0.0016, 0.1}, IColor(255, 24, 46, 34), 0.4f);
    // Traces from each slot back toward the phase inverter.
    for (int b = 0; b < 2; b++)
      for (int t = 0; t < 4; t++)
      {
        const double x = kBayX[b] - 0.03 + 0.02 * t;
        _Line(g, IColor(255, 190, 150, 70).WithOpacity(0.35f), {x, 0.0048, kBayZ - 0.012}, {x, 0.0048, -0.08}, 1.f);
      }
    _Text(g, {-0.19, 0.005, 0.088}, "PREAMP BAYS", 7.f, IColor(255, 214, 170, 90), "Michroma-Regular");
  }

  // A bay: its slot (with the latches at each end) and the card standing in it.
  void _Card(IGraphics& g, const chassis::ModView& mv, int bay)
  {
    using namespace chassis;
    mCur = kBayA + bay;
    const double x = kBayX[bay];
    const bool playing = mv.playing == bay;
    // The slot (across the deck), with its latches at each end.
    _Box(g, {x, 0.0085, kBayZ}, {kCardHalfL + 0.006, 0.004, 0.0055}, IColor(255, 20, 20, 22), 0.4f);
    for (int s = -1; s <= 1; s += 2)
      _Box(g, {x + s * (kCardHalfL + 0.009), 0.013, kBayZ}, {0.003, 0.009, 0.0045},
           playing ? IColor(255, 214, 170, 90) : IColor(255, 200, 200, 196), 0.3f);
    double lift = 0.0;
    const int id = _CardModule(bay, lift);
    if (id <= tube::module::kModuleLab || id >= tube::module::kNumModules)
    {
      mCur = -1;
      return;
    }
    const tube::module::ModuleDef& def = tube::module::GetModule(id);
    const IColor fam = FamilyColor(def.family);
    const double yb = 0.011 + lift, hh = 0.028; // The card's bottom edge, its half height
    const double zf = kBayZ + 0.0017; // Its front face
    // The board, its family stripe along the top, the gold contacts along the bottom.
    _Box(g, {x, yb + hh, kBayZ}, {kCardHalfL, hh, 0.0016}, IColor(255, 18, 62, 40), 0.35f);
    _Box(g, {x, yb + 2.0 * hh - 0.0035, kBayZ}, {kCardHalfL, 0.0035, 0.0019}, fam, 0.2f);
    const int fingers = 14;
    for (int i = 0; i < fingers; i++)
      _Box(g, {x - kCardHalfL + 0.006 + i * (2.0 * kCardHalfL - 0.012) / (fingers - 1), yb + 0.0035, kBayZ},
           {0.0024, 0.0032, 0.0019}, IColor(255, 220, 178, 70), 0.f);
    // Its circuit on the front face: a chip per stage, a cap beside each, and its name.
    for (int st = 0; st < def.numStages; st++)
    {
      const double cx = x - kCardHalfL + 0.012 + st * (2.0 * kCardHalfL - 0.024) / std::max(1, def.numStages - 1);
      _Box(g, {cx, yb + 0.016, zf + 0.001}, {0.0055, 0.004, 0.001}, IColor(255, 26, 26, 28), 0.3f);
      _Cyl(g, {cx, yb + 0.025, zf + 0.0025}, 2, 0.0025, 0.004, IColor(255, 228, 108, 32), IColor(255, 238, 128, 52), 8);
    }
    if (_Faces({x, yb + hh, zf}, {0, 0, 1}))
    {
      _Text(g, {x, yb + 0.039, zf + 0.0005}, def.name, 8.f, COLOR_WHITE.WithOpacity(0.92f), "Michroma-Regular");
      _Text(g, {x, yb + 0.0305, zf + 0.0005}, def.family, 6.f, fam);
    }
    // The tubes along the top edge, on their sockets: lit by the stages they hold when this bay plays.
    const int bottles = ModuleBottles(id), triodes = ModuleTriodes(id);
    const double top = yb + 2.0 * hh;
    for (int k = 0; k < bottles; k++)
    {
      const double bx = bottles == 1 ? x : x - kCardHalfL + 0.014 + k * (2.0 * kCardHalfL - 0.028) / (bottles - 1);
      _Cyl(g, {bx, top + 0.003, kBayZ}, 1, 0.0095, 0.006, IColor(255, 30, 30, 32), IColor(255, 52, 50, 50), 12);
      double sig = 0.0;
      if (playing && lift == 0.0)
        for (int t = 2 * k; t < 2 * k + 2 && t < triodes; t++)
        {
          const int stage = _TriodeStage(t, def);
          sig = std::max(sig, std::clamp((20.0 * std::log10(std::max(mv.level[stage], 1e-6)) + 40.0) / 40.0, 0.0, 1.0));
        }
      const bool lit = playing && lift == 0.0 && mv.base.circuit && !mv.base.capture;
      const double glow = lit ? std::pow(std::clamp(mv.base.heat, 0.5, 1.3), 4.0) * (0.7 + 0.4 * sig) : 0.0;
      _Bottle(g, {bx, top + 0.006, kBayZ}, 0.0078, 0.042, glow, mv.base.heat, 0.0, false, false);
    }
    mCur = -1;
  }
  // The stage a triode belongs to: the gain stages in order, a follower sharing its stage's level.
  static int _TriodeStage(int triode, const tube::module::ModuleDef& def)
  {
    int t = 0;
    for (int s = 0; s < def.numStages; s++)
    {
      if (t++ == triode)
        return s;
      if (def.stage[s].follower && t++ == triode)
        return s;
    }
    return std::max(0, def.numStages - 1);
  }

  // V4, the phase inverter, between the bays and the power tubes.
  void _PhaseInverter(IGraphics& g, const chassis::View& v)
  {
    using namespace chassis;
    mCur = kTubeV4;
    const double x = kPiX, z = kPreampZ;
    _Cyl(g, {x, 0.0035, z}, 1, 0.0105 * 1.5, 0.007, IColor(255, 30, 30, 32), IColor(255, 52, 50, 50), 14);
    const bool used = v.powerAmp && v.circuit;
    const double sig = used ? std::clamp((20.0 * std::log10(std::max(v.level[3], 1e-6)) + 40.0) / 40.0, 0.0, 1.0) : 0.0;
    const double glow = used ? std::pow(std::clamp(v.heat, 0.5, 1.3), 4.0) * (0.7 + 0.4 * sig) : 0.0;
    _Bottle(g, {x, 0.0075, z}, 0.0105, 0.058, glow, v.heat, 0.0, false, false);
    mCur = -1;
  }

  // The signal: the input jack to the playing bay's slot, out of it to V4, then the power section.
  void _DrawModWires(IGraphics& g, const chassis::ModView& mv)
  {
    using namespace chassis;
    const View& v = mv.base;
    const double y = 0.0052;
    auto lit = [](double level) { return std::clamp((20.0 * std::log10(std::max(level, 1e-6)) + 40.0) / 40.0, 0.0, 1.0); };
    auto wire = [&](const V3& a, const V3& b, double glow, bool on) {
      const IColor base = on ? IColor(255, 214, 170, 90) : IColor(255, 110, 100, 85);
      const float w = std::max(1.2f, 0.0025f * _Scale(a));
      _Line(g, COLOR_BLACK.WithOpacity(0.5f), a, b, w + 1.5f);
      _Line(g, base.WithOpacity(on ? 0.55f + 0.45f * (float)glow : 0.35f), a, b, w);
      if (on && glow > 0.05)
        _Line(g, IColor(255, 255, 235, 170).WithOpacity((float)glow * 0.6f), a, b, std::max(1.f, w * 0.45f));
    };
    const double in = lit(v.level[0]), pre = lit(v.level[2]), pi = lit(v.level[3]);
    const double front = 0.06, back = -0.035;
    wire({-0.29, 0.0012, kHalfD - 0.004}, {-0.29, y, front}, in, v.engaged);
    for (int b = 0; b < 2; b++)
    {
      const bool on = v.engaged && mv.playing == b;
      wire({-0.29, y, front}, {kBayX[b], y, front}, in, on);
      wire({kBayX[b], y, front}, {kBayX[b], y, kBayZ + 0.008}, in, on);
      wire({kBayX[b], y, kBayZ - 0.008}, {kBayX[b], y, back}, pre, on);
      wire({kBayX[b], y, back}, {kPiX, y, back}, pre, on);
    }
    wire({kPiX, y, back}, {kPiX, y, kPreampZ + 0.012}, pre, v.engaged);
    wire({kPiX, 0.0012, kWireZ}, {kPowerX[0], 0.0012, kWireZ}, pi, v.powerAmp);
    for (int k = 1; k < kNumPower; k++)
      wire({kPowerX[k - 1], 0.0012, kWireZ}, {kPowerX[k], 0.0012, kWireZ}, pi, v.powerAmp && (v.quad || k < 2));
    wire({kPowerX[kNumPower - 1], 0.0012, kWireZ}, {0.27, 0.0012, kWireZ}, pi, v.powerAmp);
    wire({0.27, 0.0012, kWireZ}, {0.31, 0.0012, kWireZ}, pi, v.engaged);
    wire({0.31, 0.0012, kWireZ}, {0.31, 0.0012, -kHalfD + 0.004}, pi, v.engaged);
  }

  // The bays' tags (the module, and which bay plays), V4's and the power tubes'.
  void _DrawModLabels(IGraphics& g, const chassis::ModView& mv)
  {
    using namespace chassis;
    const int sel = mCb.selected ? mCb.selected() : -1;
    for (int b = 0; b < 2; b++)
    {
      double lift = 0.0;
      const int id = _CardModule(b, lift);
      char name[24], job[48];
      snprintf(name, sizeof(name), "BAY %c", 'A' + b);
      snprintf(job, sizeof(job), "%s%s", tube::module::GetModule(id).name, mv.playing == b ? "  (PLAYING)" : "");
      _Label(g, {kBayX[b], 0.13 + lift, kBayZ}, name, job, mv.playing == b, sel == kBayA + b, true);
    }
    _Label(g, {kPiX, 0.0, kPreampZ + 0.02}, "V4", "PHASE INV", mv.base.powerAmp, sel == kTubeV4, mOver == kTubeV4);
    static const char* powerNames[kNumPower] = {"V9", "V10", "V11", "V12"};
    const View& v = mv.base;
    for (int k = 0; k < kNumPower; k++)
    {
      const bool fitted = k < 2 || v.quad;
      const char* state = !fitted        ? "EMPTY"
                          : !v.powerAmp  ? "OFF"
                          : v.dead[k]    ? "FAILED"
                          : v.dissipation[k] > 1.0 ? "RED PLATING"
                                         : v.powerName;
      _Label(g, {kPowerX[k], 0.0, kPowerZ + 0.028}, powerNames[k], state, fitted && v.powerAmp && !v.dead[k],
             sel == kPowerTube1 + k, mOver == kPowerTube1 + k);
    }
  }

  ModCallbacks mMod;
  int mShown[2] = {-1, -1}, mSwapFrom[2] = {-1, -1};
  std::chrono::steady_clock::time_point mSwapStart[2];
};
