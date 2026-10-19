#pragma once

// The EQ graph (1.46.0; panel, analyzer switch, freeze and colours 1.47.0): a live spectrum with an
// editable 8-band EQ on top -- the PRO EQ block's popup, the Output EQ page, and (compact, click to
// open the page) the strip on the main window.
//
// Drawn: a log frequency axis (20 Hz - 20 kHz) with the EQ's gain scale (+-18 dB) on the left and
// the spectrum's (0 to -90 dBFS) on the right; the two taps' spectra (A, usually the EQ's input, in
// slate; B, its output, shaded through the spectrum's colours by frequency, with a peak-hold line);
// each band's own shape tinted in its colour; the EQ's total response (the same biquads the audio
// runs, see proeq::ResponseDB); and a numbered node per band.
//
// Header chips: ANALYZER (the spectrum on/off: off, the audio thread stops feeding it and the view
// stops redrawing), FREEZE (holds the picture while you play), and the A / B spectra.
// Band panel (the selected band): band buttons 1-8, Type, Slope (cuts), On, Flip (boost <-> cut),
// Freq, Cut/Boost and Q dials (drag up/down or wheel; Shift = fine; double-click = default), and
// -1 dB / +1 dB.
// On the graph: drag a node (frequency and gain), wheel (Q), double-click (band on/off; on empty
// space, the nearest band jumps there), Alt-click (reset; a right-click on a parameter control is
// the host's menu in iPlug2).
// Every edit goes through the band's parameters, so it's automatable, MIDI-learnable and saved.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "IControl.h"
#include "ProEQ.h"
#include "Spectrum.h"

using namespace iplug;
using namespace igraphics;

class NAMEqGraphControl : public IControl
{
public:
  // The EQ's params: 8 x {On, Freq, Gain, Q} from firstBandParam, 8 x {Type, Slope} from
  // firstExtParam, Trim and Output. tapA / tapB may be null. view: the analyzer/freeze/selection
  // state, kept by the plugin so it survives a relayout. onClick (compact mode only) runs on a click
  // that isn't on a chip.
  NAMEqGraphControl(const IRECT& bounds, int firstBandParam, int firstExtParam, int trimParam, int outputParam,
                    spectrum::Tap* tapA, spectrum::Tap* tapB, const char* labelA, const char* labelB,
                    spectrum::EqViewState* view, bool compact = false, std::function<void()> onClick = nullptr)
  : IControl(bounds)
  , mTapA(tapA)
  , mTapB(tapB)
  , mLabelA(labelA ? labelA : "")
  , mLabelB(labelB ? labelB : "")
  , mView(view ? view : &mOwnView)
  , mCompact(compact)
  , mOnClick(std::move(onClick))
  {
    SetNVals(kExt0 + proeq::kNumBands * 2);
    for (int i = 0; i < proeq::kNumBands * 4; i++)
      SetParamIdx(firstBandParam + i, i);
    SetParamIdx(outputParam, kOutputVal);
    SetParamIdx(trimParam, kTrimVal);
    for (int i = 0; i < proeq::kNumBands * 2; i++)
      SetParamIdx(firstExtParam + i, kExt0 + i);
    mLast = std::chrono::steady_clock::now();
  }

  // Redraws at ~30 fps (the floating window: 60) only while it shows a live (on, not frozen) spectrum.
  void SetFrameRate(double fps) { mFps = fps; }

  bool IsDirty() override
  {
    if (IsHidden() || !_Live())
      return IControl::IsDirty();
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - mLastFrame).count() >= 1.0 / mFps)
      return true;
    return IControl::IsDirty();
  }

  void Draw(IGraphics& g) override
  {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::min(0.25, std::chrono::duration<double>(now - mLast).count());
    mLast = mLastFrame = now;
    if (_Live())
    {
      for (spectrum::Tap* t : {_ShownTap(0), _ShownTap(1)})
        if (t)
          t->KeepAlive();
      mAnalyzer.Update(_ShownTap(0), _ShownTap(1), dt);
    }

    const IRECT plot = _Plot();
    // Background: a dark navy-to-black fade.
    g.PathClear();
    g.PathRoundRect(mRECT, 8.f);
    g.PathFill(IPattern::CreateLinearGradient(mRECT.L, mRECT.T, mRECT.L, mRECT.B,
                                              {{IColor(255, 22, 26, 38), 0.f}, {IColor(255, 12, 12, 15), 1.f}}));
    _DrawGrid(g, plot);
    if (mView->analyzer)
      _DrawSpectrum(g, plot);
    _DrawBandShapes(g, plot);
    _DrawResponse(g, plot);
    if (!mCompact)
    {
      _DrawNodes(g, plot);
      _DrawReadout(g);
      _DrawPanel(g);
    }
    else
    {
      const IText t(11.f, IColor(170, 220, 225, 235), "Roboto-Regular", EAlign::Near, EVAlign::Top);
      g.DrawText(t, "OUTPUT  (click: Output EQ)", mRECT.GetPadded(-8.f, -4.f, -8.f, -4.f));
    }
    _DrawChips(g);
    g.DrawRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.45f), mRECT, 8.f, nullptr, 1.f);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    for (int c = 0; c < kNumChips; c++)
      if (_ChipShown(c) && _Chip(c).Contains(x, y))
      {
        _ToggleChip(c);
        SetDirty(false);
        return;
      }
    if (mCompact)
    {
      if (mOnClick)
        mOnClick();
      return;
    }
    mDrag = Drag::None;
    if (_Panel().Contains(x, y))
    {
      _PanelDown(x, y, mod);
      SetDirty(false);
      return;
    }
    const int band = _NodeAt(x, y);
    if (mod.A)
    {
      if (band >= 0)
        _ResetBand(band);
      return;
    }
    if (band >= 0)
    {
      mView->selected = band;
      if (!_On(band))
        _Set(band, 0, 1.0);
      mDrag = Drag::Node;
      mDragBand = band;
      mDragStartX = x;
      mDragStartY = y;
      mDragStartFreq = _Freq(band);
      mDragStartGain = _Gain(band);
    }
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mCompact || mDrag == Drag::None)
      return;
    const double fine = mod.S ? 0.1 : 1.0;
    if (mDrag == Drag::Node)
    {
      const IRECT plot = _Plot();
      const double octaves = (x - mDragStartX) / plot.W() * std::log2(proeq::kMaxHz / proeq::kMinHz) * fine;
      _SetReal(mDragBand, 1, std::clamp(mDragStartFreq * std::pow(2.0, octaves), proeq::kMinHz, proeq::kMaxHz));
      if (proeq::HasGain(_Type(mDragBand)))
      {
        const double gain = mDragStartGain - (y - mDragStartY) / plot.H() * 2.0 * proeq::kMaxGainDB * fine;
        _SetReal(mDragBand, 2, std::clamp(gain, -proeq::kMaxGainDB, proeq::kMaxGainDB));
      }
    }
    else
      _DialMove(mDrag, -dY * (float)fine);
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override { mDrag = Drag::None; }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    if (mCompact)
      return;
    const float fine = mod.S ? 0.25f : 1.f;
    for (int k = 0; k < 3; k++)
      if (_Dial(k).Contains(x, y))
      {
        _DialMove(_DialDrag(k), d * 6.f * fine);
        SetDirty(false);
        return;
      }
    int band = _NodeAt(x, y);
    if (band < 0)
      band = mView->selected;
    if (band < 0)
      return;
    mView->selected = band;
    _SetReal(band, 3, std::clamp(_Q(band) * std::pow(1.12, d * fine), proeq::kMinQ, proeq::kMaxQ));
    SetDirty(false);
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    if (mCompact)
      return;
    const int sel = mView->selected;
    for (int k = 0; k < 3; k++)
      if (sel >= 0 && _Dial(k).Contains(x, y))
      {
        // A dial back to the band's default.
        const proeq::Band d = proeq::DefaultBand(sel);
        if (k == 0)
          _SetReal(sel, 1, d.freq);
        else if (k == 1)
          _SetReal(sel, 2, 0.0);
        else
          _SetReal(sel, 3, _Type(sel) == proeq::Type::Bell || _Type(sel) == proeq::Type::Notch ? 1.0 : 0.707);
        return;
      }
    if (_Panel().Contains(x, y))
      return;
    const int band = _NodeAt(x, y);
    if (band >= 0)
    {
      _Set(band, 0, _On(band) ? 0.0 : 1.0);
      mView->selected = band;
      return;
    }
    // Empty space: the band whose frequency is nearest (in octaves) jumps here, switched on.
    const IRECT plot = _Plot();
    if (!plot.Contains(x, y))
      return;
    const double hz = _XToHz(plot, x);
    int best = 0;
    double bestDist = 1e9;
    for (int b = 0; b < proeq::kNumBands; b++)
    {
      const double dist = std::fabs(std::log2(_Freq(b) / hz));
      if (dist < bestDist)
      {
        bestDist = dist;
        best = b;
      }
    }
    _SetReal(best, 1, hz);
    if (proeq::HasGain(_Type(best)))
      _SetReal(best, 2, std::clamp(_YToGain(plot, y), -proeq::kMaxGainDB, proeq::kMaxGainDB));
    _Set(best, 0, 1.0);
    mView->selected = best;
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const int band = mCompact || _Panel().Contains(x, y) ? -1 : _NodeAt(x, y);
    if (band != mHover)
    {
      mHover = band;
      SetDirty(false);
    }
  }
  void OnMouseOut() override
  {
    mHover = -1;
    SetDirty(false);
  }

private:
  static constexpr int kOutputVal = proeq::kNumBands * 4, kTrimVal = kOutputVal + 1, kExt0 = kTrimVal + 1;
  enum class Drag
  {
    None,
    Node,
    Freq,
    Gain,
    Q
  };
  enum
  {
    kChipAnalyzer = 0,
    kChipFreeze,
    kChipA,
    kChipB,
    kNumChips
  };

  bool _Live() const { return mView->analyzer && !mView->frozen; }
  spectrum::Tap* _ShownTap(int i) const
  {
    if (i == 0)
      return mView->showA ? mTapA : nullptr;
    return mView->showB ? mTapB : nullptr;
  }

  // --- parameters (real values via the plugin's params; edits go through SetValue + SetDirty)
  double _Real(int valIdx) const { return GetParam(valIdx) ? GetParam(valIdx)->Value() : 0.0; }
  bool _On(int b) const { return _Real(b * 4) > 0.5; }
  double _Freq(int b) const { return std::max(proeq::kMinHz, _Real(b * 4 + 1)); }
  double _Gain(int b) const { return _Real(b * 4 + 2); }
  double _Q(int b) const { return std::max(proeq::kMinQ, _Real(b * 4 + 3)); }
  proeq::Type _Type(int b) const
  {
    return (proeq::Type)std::clamp((int)std::lround(_Real(kExt0 + b * 2)), 0, proeq::kNumTypes - 1);
  }
  int _Slope(int b) const { return std::clamp((int)std::lround(_Real(kExt0 + b * 2 + 1)), 0, proeq::kNumSlopes - 1); }
  void _SetVal(int valIdx, double normalized)
  {
    SetValue(normalized, valIdx);
    SetDirty(true, valIdx);
  }
  void _Set(int b, int field, double normalized) { _SetVal(b * 4 + field, normalized); }
  void _SetRealVal(int valIdx, double real)
  {
    if (const IParam* p = GetParam(valIdx))
      _SetVal(valIdx, p->ToNormalized(real));
  }
  void _SetReal(int b, int field, double real) { _SetRealVal(b * 4 + field, real); }
  void _ResetBand(int b)
  {
    const proeq::Band d = proeq::DefaultBand(b);
    _SetReal(b, 1, d.freq);
    _SetReal(b, 2, d.gainDB);
    _SetReal(b, 3, d.q);
    _SetRealVal(kExt0 + b * 2, (double)d.type);
    _SetRealVal(kExt0 + b * 2 + 1, 0.0);
    _Set(b, 0, 0.0);
  }
  proeq::Band _BandOf(int b) const
  {
    proeq::Band x;
    x.on = _On(b);
    x.type = _Type(b);
    x.slope = _Slope(b);
    x.freq = _Freq(b);
    x.gainDB = _Gain(b);
    x.q = _Q(b);
    return x;
  }
  proeq::Params _Params() const
  {
    proeq::Params p;
    for (int b = 0; b < proeq::kNumBands; b++)
      p.bands[b] = _BandOf(b);
    p.outputDB = _Real(kOutputVal);
    p.trimDB = _Real(kTrimVal);
    return p;
  }
  double _SampleRate() const
  {
    const spectrum::Tap* t = mTapB ? mTapB : mTapA;
    return t ? std::max(8000.0, t->SampleRate()) : 48000.0;
  }

  // --- geometry
  IRECT _Panel() const { return mCompact ? IRECT() : mRECT.GetFromBottom(96.f).GetPadded(-8.f, 0.f, -8.f, -6.f); }
  IRECT _Plot() const
  {
    return mCompact ? mRECT.GetPadded(-6.f, -18.f, -6.f, -4.f) : mRECT.GetPadded(-44.f, -54.f, -48.f, -24.f - 96.f);
  }
  static float _HzToX(const IRECT& r, double hz)
  {
    return r.L + r.W() * (float)(std::log(hz / proeq::kMinHz) / std::log(proeq::kMaxHz / proeq::kMinHz));
  }
  static double _XToHz(const IRECT& r, float x)
  {
    return proeq::kMinHz * std::pow(proeq::kMaxHz / proeq::kMinHz, std::clamp((x - r.L) / r.W(), 0.f, 1.f));
  }
  float _GainToY(const IRECT& r, double db) const
  {
    const double range = mCompact ? 12.0 : proeq::kMaxGainDB;
    return r.MH() - r.H() * 0.5f * (float)(std::clamp(db, -range, range) / range);
  }
  double _YToGain(const IRECT& r, float y) const { return (r.MH() - y) / (r.H() * 0.5f) * proeq::kMaxGainDB; }
  static float _SpecToY(const IRECT& r, double dbfs)
  {
    constexpr double kTop = 0.0, kBottom = -90.0;
    return r.T + r.H() * (float)((kTop - std::clamp(dbfs, kBottom, kTop)) / (kTop - kBottom));
  }
  float _NodeY(const IRECT& plot, int b) const
  {
    return proeq::HasGain(_Type(b)) ? _GainToY(plot, _Gain(b)) : _GainToY(plot, 0.0);
  }
  int _NodeAt(float x, float y) const
  {
    const IRECT plot = _Plot();
    int best = -1;
    float bestD = 12.f * 12.f;
    for (int b = 0; b < proeq::kNumBands; b++)
    {
      const float dx = x - _HzToX(plot, _Freq(b)), dy = y - _NodeY(plot, b);
      const float d = dx * dx + dy * dy;
      if (d < bestD)
      {
        bestD = d;
        best = b;
      }
    }
    return best;
  }
  bool _ChipShown(int c) const
  {
    if (c == kChipA)
      return mTapA && !mCompact;
    if (c == kChipB)
      return mTapB && !mCompact;
    return true;
  }
  IRECT _Chip(int c) const
  {
    if (mCompact)
    {
      const IRECT top = mRECT.GetFromTop(18.f).GetFromRight(170.f).GetPadded(-2.f);
      return top.GetGridCell(0, c, 1, 2).GetPadded(-2.f);
    }
    const IRECT top = mRECT.GetFromTop(28.f).GetFromRight(390.f).GetPadded(-4.f);
    return top.GetGridCell(0, c, 1, kNumChips).GetPadded(-2.f);
  }
  void _ToggleChip(int c)
  {
    switch (c)
    {
      case kChipAnalyzer:
        mView->analyzer = !mView->analyzer;
        if (mView->onAnalyzerChanged)
          mView->onAnalyzerChanged();
        break;
      case kChipFreeze: mView->frozen = !mView->frozen; break;
      case kChipA: mView->showA = !mView->showA; break;
      default: mView->showB = !mView->showB; break;
    }
  }

  // Panel layout: row 1 = band buttons 1-8, Type, Slope, On, Flip; row 2 = Freq, Cut/Boost, Q dials,
  // -1 dB, +1 dB.
  IRECT _Row(int r) const
  {
    const IRECT p = _Panel();
    return r == 0 ? p.GetFromTop(30.f) : p.GetReducedFromTop(36.f);
  }
  IRECT _BandButton(int b) const
  {
    const IRECT row = _Row(0);
    return row.GetFromLeft(row.W() * 0.30f).GetGridCell(0, b, 1, proeq::kNumBands).GetPadded(-2.f);
  }
  IRECT _RowCell(int row, int i, int n, float from = 0.30f) const
  {
    const IRECT r = _Row(row);
    const IRECT rest = r.GetReducedFromLeft(r.W() * from);
    return rest.GetGridCell(0, i, 1, n).GetPadded(-4.f, -1.f, -4.f, -1.f);
  }
  IRECT _TypeBox() const { return _RowCell(0, 0, 4); }
  IRECT _SlopeBox() const { return _RowCell(0, 1, 4); }
  IRECT _OnBox() const { return _RowCell(0, 2, 4); }
  IRECT _FlipBox() const { return _RowCell(0, 3, 4); }
  IRECT _Dial(int k) const
  {
    const IRECT r = _Row(1);
    return r.GetFromLeft(r.W() * 0.66f).GetGridCell(0, k, 1, 3).GetPadded(-4.f, 0.f, -4.f, 0.f);
  }
  IRECT _StepButton(int i) const
  {
    const IRECT r = _Row(1);
    return r.GetReducedFromLeft(r.W() * 0.68f).GetGridCell(0, i, 1, 2).GetCentredInside(r.W() * 0.14f, 30.f);
  }
  static Drag _DialDrag(int k) { return k == 0 ? Drag::Freq : k == 1 ? Drag::Gain : Drag::Q; }

  void _PanelDown(float x, float y, const IMouseMod& mod)
  {
    for (int b = 0; b < proeq::kNumBands; b++)
      if (_BandButton(b).Contains(x, y))
      {
        mView->selected = b;
        return;
      }
    const int sel = mView->selected;
    if (sel < 0)
      return;
    if (_TypeBox().Contains(x, y))
    {
      // Click cycles forward (Shift: back).
      const int t = ((int)_Type(sel) + (mod.S ? proeq::kNumTypes - 1 : 1)) % proeq::kNumTypes;
      _SetRealVal(kExt0 + sel * 2, (double)t);
      return;
    }
    if (_SlopeBox().Contains(x, y) && proeq::IsCut(_Type(sel)))
    {
      const int s = (_Slope(sel) + (mod.S ? proeq::kNumSlopes - 1 : 1)) % proeq::kNumSlopes;
      _SetRealVal(kExt0 + sel * 2 + 1, (double)s);
      return;
    }
    if (_OnBox().Contains(x, y))
    {
      _Set(sel, 0, _On(sel) ? 0.0 : 1.0);
      return;
    }
    if (_FlipBox().Contains(x, y) && proeq::HasGain(_Type(sel)))
    {
      _SetReal(sel, 2, -_Gain(sel));
      return;
    }
    for (int i = 0; i < 2; i++)
      if (_StepButton(i).Contains(x, y) && proeq::HasGain(_Type(sel)))
      {
        _SetReal(sel, 2, std::clamp(_Gain(sel) + (i == 0 ? -1.0 : 1.0), -proeq::kMaxGainDB, proeq::kMaxGainDB));
        if (!_On(sel))
          _Set(sel, 0, 1.0);
        return;
      }
    for (int k = 0; k < 3; k++)
      if (_Dial(k).Contains(x, y))
      {
        mDrag = _DialDrag(k);
        return;
      }
  }

  // A dial moved by `steps` (pixels of drag, or wheel units x 6).
  void _DialMove(Drag which, float steps)
  {
    const int sel = mView->selected;
    if (sel < 0)
      return;
    if (which == Drag::Freq)
      _SetReal(sel, 1, std::clamp(_Freq(sel) * std::pow(2.0, steps / 60.0), proeq::kMinHz, proeq::kMaxHz));
    else if (which == Drag::Gain && proeq::HasGain(_Type(sel)))
      _SetReal(sel, 2, std::clamp(_Gain(sel) + steps * 0.1, -proeq::kMaxGainDB, proeq::kMaxGainDB));
    else if (which == Drag::Q)
      _SetReal(sel, 3, std::clamp(_Q(sel) * std::pow(2.0, steps / 80.0), proeq::kMinQ, proeq::kMaxQ));
    else
      return;
    if (!_On(sel))
      _Set(sel, 0, 1.0);
  }

  // --- colours
  // The spectrum's colour at a frequency: violet lows through blue, teal and green to amber and rose highs.
  static IColor _SpectrumColor(double hz)
  {
    struct Stop
    {
      double hz;
      int r, g, b;
    };
    static const Stop kStops[] = {{20.0, 150, 80, 230},   {80.0, 90, 110, 245},  {250.0, 50, 180, 235},
                                  {800.0, 60, 210, 160},  {2500.0, 200, 210, 70}, {6000.0, 245, 150, 60},
                                  {20000.0, 240, 80, 120}};
    const double lh = std::log(std::clamp(hz, 20.0, 20000.0));
    for (size_t i = 0; i + 1 < sizeof(kStops) / sizeof(kStops[0]); i++)
    {
      const double a = std::log(kStops[i].hz), b = std::log(kStops[i + 1].hz);
      if (lh <= b)
      {
        const double t = (lh - a) / (b - a);
        auto mix = [t](int x, int y) { return (int)std::lround(x + t * (y - x)); };
        return IColor(255, mix(kStops[i].r, kStops[i + 1].r), mix(kStops[i].g, kStops[i + 1].g),
                      mix(kStops[i].b, kStops[i + 1].b));
      }
    }
    return IColor(255, 240, 80, 120);
  }
  static IColor _BandColor(int b)
  {
    static const IColor kColors[proeq::kNumBands] = {
      IColor(255, 235, 90, 95),  IColor(255, 240, 150, 70), IColor(255, 230, 205, 80), IColor(255, 120, 215, 110),
      IColor(255, 70, 205, 200), IColor(255, 90, 150, 245), IColor(255, 165, 120, 245), IColor(255, 225, 110, 205)};
    return kColors[b];
  }

  // --- drawing
  void _DrawGrid(IGraphics& g, const IRECT& plot)
  {
    const IColor line(255, 34, 38, 50), strong(255, 54, 60, 78);
    const IText label(mCompact ? 9.f : 11.f, IColor(255, 120, 128, 145), "Roboto-Regular");
    for (double hz : {30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 300.0, 400.0, 600.0, 700.0, 800.0, 900.0, 3000.0, 4000.0,
                      6000.0, 7000.0, 8000.0, 9000.0})
    {
      const float x = _HzToX(plot, hz);
      g.DrawLine(line, x, plot.T, x, plot.B, nullptr, 1.f);
    }
    for (double hz : {100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
      const float x = _HzToX(plot, hz);
      g.DrawLine(strong, x, plot.T, x, plot.B, nullptr, 1.f);
      if (!mCompact)
      {
        char s[16];
        if (hz >= 1000.0)
          std::snprintf(s, sizeof(s), "%gk", hz / 1000.0);
        else
          std::snprintf(s, sizeof(s), "%g", hz);
        g.DrawText(label, s, IRECT(x - 20.f, plot.B + 2.f, x + 20.f, plot.B + 20.f));
      }
    }
    if (mCompact)
    {
      g.DrawLine(strong, plot.L, _GainToY(plot, 0.0), plot.R, _GainToY(plot, 0.0), nullptr, 1.f);
      return;
    }
    for (int db = -18; db <= 18; db += 6)
    {
      const float y = _GainToY(plot, db);
      g.DrawLine(db == 0 ? strong : line, plot.L, y, plot.R, y, nullptr, 1.f);
      char s[16];
      std::snprintf(s, sizeof(s), "%+d", db);
      g.DrawText(label.WithAlign(EAlign::Far), db == 0 ? "0" : s, IRECT(plot.L - 40.f, y - 8.f, plot.L - 6.f, y + 8.f));
    }
    if (mView->analyzer)
    {
      for (int dbfs = 0; dbfs >= -90; dbfs -= 15)
      {
        const float y = _SpecToY(plot, dbfs);
        char s[16];
        std::snprintf(s, sizeof(s), "%d", dbfs);
        g.DrawText(label.WithAlign(EAlign::Near).WithFGColor(IColor(255, 110, 160, 220)), s,
                   IRECT(plot.R + 6.f, y - 8.f, plot.R + 46.f, y + 8.f));
      }
      g.DrawText(label.WithFGColor(IColor(255, 110, 160, 220)).WithAlign(EAlign::Near), "dBFS",
                 IRECT(plot.R + 6.f, plot.T - 20.f, plot.R + 46.f, plot.T - 4.f));
    }
    g.DrawText(label.WithAlign(EAlign::Far), "EQ dB", IRECT(plot.L - 44.f, plot.T - 20.f, plot.L - 4.f, plot.T - 4.f));
  }

  void _DrawSpectrum(IGraphics& g, const IRECT& plot)
  {
    const int n = mAnalyzer.NumPoints();
    g.PathClipRegion(plot);
    for (int tap = 0; tap < 2; tap++)
    {
      if (!_ShownTap(tap) || !mAnalyzer.HasData(tap))
        continue;
      mXs.resize(n);
      mYs.resize(n);
      for (int i = 0; i < n; i++)
      {
        mXs[i] = _HzToX(plot, spectrum::Analyzer::PointHz(i));
        mYs[i] = _SpecToY(plot, mAnalyzer.Level(tap, i));
      }
      if (tap == 0)
      {
        // A (the input): a quiet slate fill and line.
        const IColor c(255, 130, 140, 165);
        g.PathClear();
        g.PathMoveTo(mXs[0], plot.B);
        for (int i = 0; i < n; i++)
          g.PathLineTo(mXs[i], mYs[i]);
        g.PathLineTo(mXs[n - 1], plot.B);
        g.PathClose();
        g.PathFill(IPattern(c.WithOpacity(0.16f)));
        g.PathClear();
        g.PathMoveTo(mXs[0], mYs[0]);
        for (int i = 1; i < n; i++)
          g.PathLineTo(mXs[i], mYs[i]);
        g.PathStroke(IPattern(c.WithOpacity(0.5f)), 1.f);
        continue;
      }
      // B (the output): slices coloured by frequency, each fading down to the floor, and the line.
      const int step = mCompact ? 2 : 1;
      for (int i = 0; i + step < n; i += step)
      {
        const IColor c = _SpectrumColor(spectrum::Analyzer::PointHz(i));
        const float top = std::min(mYs[i], mYs[i + step]);
        g.PathClear();
        g.PathMoveTo(mXs[i], plot.B);
        g.PathLineTo(mXs[i], mYs[i]);
        g.PathLineTo(mXs[i + step], mYs[i + step]);
        g.PathLineTo(mXs[i + step], plot.B);
        g.PathClose();
        g.PathFill(IPattern::CreateLinearGradient(0.f, top, 0.f, plot.B,
                                                  {{c.WithOpacity(0.55f), 0.f}, {c.WithOpacity(0.04f), 1.f}}));
      }
      for (int i = 0; i + step < n; i += step)
        g.DrawLine(_SpectrumColor(spectrum::Analyzer::PointHz(i)), mXs[i], mYs[i], mXs[i + step], mYs[i + step], nullptr,
                   1.6f);
      if (!mCompact)
      {
        g.PathClear();
        for (int i = 0; i < n; i++)
        {
          const float y = _SpecToY(plot, mAnalyzer.Peak(tap, i));
          if (i == 0)
            g.PathMoveTo(mXs[i], y);
          else
            g.PathLineTo(mXs[i], y);
        }
        g.PathStroke(IPattern(IColor(90, 255, 255, 255)), 1.f);
      }
    }
    g.PathClipRegion();
    if (mView->frozen)
    {
      const IText t(mCompact ? 10.f : 13.f, IColor(220, 120, 200, 255), "Roboto-Regular", EAlign::Far, EVAlign::Bottom);
      g.DrawText(t, "FROZEN", plot.GetPadded(-6.f));
    }
  }

  // Each band's own shape, tinted in its colour (stronger for the selected one).
  void _DrawBandShapes(IGraphics& g, const IRECT& plot)
  {
    if (mCompact)
      return;
    const double sr = _SampleRate();
    constexpr int kN = 160;
    const float y0 = _GainToY(plot, 0.0);
    g.PathClipRegion(plot);
    for (int b = 0; b < proeq::kNumBands; b++)
    {
      if (!_On(b))
        continue;
      const proeq::Band band = _BandOf(b);
      g.PathClear();
      g.PathMoveTo(plot.L, y0);
      for (int i = 0; i < kN; i++)
      {
        const float x = plot.L + plot.W() * i / (kN - 1);
        g.PathLineTo(x, _GainToY(plot, proeq::BandResponseDB(band, _XToHz(plot, x), sr)));
      }
      g.PathLineTo(plot.R, y0);
      g.PathClose();
      const float tint = proeq::IsCut(band.type) ? 0.05f : 0.13f;
      g.PathFill(IPattern(_BandColor(b).WithOpacity(b == mView->selected ? tint * 2.2f : tint)));
    }
    g.PathClipRegion();
  }

  void _ResponsePath(IGraphics& g, const IRECT& plot, const proeq::Params& p, double sr)
  {
    constexpr int kN = 240;
    g.PathClear();
    for (int i = 0; i < kN; i++)
    {
      const float x = plot.L + plot.W() * i / (kN - 1);
      const float y = _GainToY(plot, proeq::ResponseDB(p, _XToHz(plot, x), sr));
      if (i == 0)
        g.PathMoveTo(x, y);
      else
        g.PathLineTo(x, y);
    }
  }

  void _DrawResponse(IGraphics& g, const IRECT& plot)
  {
    const proeq::Params p = _Params();
    const double sr = _SampleRate();
    g.PathClipRegion(plot);
    // A soft glow under a bright line.
    _ResponsePath(g, plot, p, sr);
    g.PathStroke(IPattern(IColor(40, 255, 255, 255)), mCompact ? 4.f : 7.f);
    _ResponsePath(g, plot, p, sr);
    g.PathStroke(IPattern(IColor(255, 245, 246, 250)), mCompact ? 1.5f : 2.f);
    g.PathClipRegion();
  }

  void _DrawNodes(IGraphics& g, const IRECT& plot)
  {
    const IText num(11.f, COLOR_BLACK, "Roboto-Regular");
    for (int b = 0; b < proeq::kNumBands; b++)
    {
      const float x = _HzToX(plot, _Freq(b)), y = _NodeY(plot, b);
      const IColor c = _BandColor(b);
      const float r = (b == mView->selected || b == mHover) ? 10.f : 8.f;
      if (_On(b))
      {
        g.FillCircle(c.WithOpacity(0.25f), x, y, r + 5.f);
        g.FillCircle(c, x, y, r);
        if (b == mView->selected)
          g.DrawCircle(COLOR_WHITE, x, y, r + 2.f, nullptr, 1.5f);
      }
      else
      {
        g.FillCircle(IColor(255, 30, 31, 36), x, y, r);
        g.DrawCircle(c.WithOpacity(0.7f), x, y, r, nullptr, 1.5f);
      }
      char s[4];
      std::snprintf(s, sizeof(s), "%d", b + 1);
      g.DrawText(_On(b) ? num : num.WithFGColor(c), s, IRECT(x - r, y - r, x + r, y + r));
    }
  }

  static std::string _FreqText(double hz)
  {
    char f[24];
    if (hz >= 1000.0)
      std::snprintf(f, sizeof(f), "%.2f kHz", hz / 1000.0);
    else
      std::snprintf(f, sizeof(f), "%.0f Hz", hz);
    return f;
  }

  void _DrawReadout(IGraphics& g)
  {
    const int b = mHover >= 0 ? mHover : mView->selected;
    const IText t(13.f, IColor(255, 225, 228, 235), "Roboto-Regular", EAlign::Near);
    const IRECT r = mRECT.GetFromTop(28.f).GetPadded(-12.f, -4.f, -400.f, -2.f);
    char buf[200];
    if (b < 0)
      std::snprintf(buf, sizeof(buf), "Drag a node: freq / gain.  Wheel: Q.  Double-click: on/off.  Alt-click: reset.");
    else
    {
      const proeq::Type type = _Type(b);
      char shape[40];
      if (proeq::HasGain(type))
        std::snprintf(shape, sizeof(shape), "%+.1f dB   Q %.2f", _Gain(b), _Q(b));
      else if (proeq::IsCut(type))
        std::snprintf(shape, sizeof(shape), "%d dB/oct   Q %.2f", proeq::SlopeDBPerOct(_Slope(b)), _Q(b));
      else
        std::snprintf(shape, sizeof(shape), "-40 dB   Q %.2f", _Q(b));
      std::snprintf(buf, sizeof(buf), "%d  %s   %s   %s%s", b + 1, proeq::TypeName(type), _FreqText(_Freq(b)).c_str(), shape,
                    _On(b) ? "" : "   (off)");
    }
    g.DrawText(t, buf, r);
  }

  void _DrawButton(IGraphics& g, const IRECT& r, const char* text, bool lit, const IColor& accent, bool enabled = true)
  {
    g.FillRoundRect(lit ? accent.WithOpacity(0.45f) : IColor(255, 30, 32, 40), r, 5.f);
    g.DrawRoundRect(enabled ? accent.WithOpacity(0.9f) : IColor(255, 60, 62, 70), r, 5.f, nullptr, 1.f);
    g.DrawText(IText(12.f, enabled ? (lit ? COLOR_WHITE : IColor(255, 210, 214, 222)) : IColor(255, 90, 92, 100),
                     "Roboto-Regular"),
               text, r);
  }

  void _DrawDial(IGraphics& g, const IRECT& area, const char* label, const std::string& value, double norm, bool bipolar,
                 const IColor& c, bool enabled)
  {
    const float d = std::min(area.H() - 4.f, 44.f);
    const IRECT knob = area.GetFromLeft(d).GetCentredInside(d, d);
    const float cx = knob.MW(), cy = knob.MH(), rad = d * 0.5f - 3.f;
    g.FillCircle(IColor(255, 28, 30, 38), cx, cy, rad);
    g.DrawArc(IColor(255, 60, 64, 76), cx, cy, rad, -135.f, 135.f, nullptr, 3.f);
    const float a0 = bipolar ? 0.f : -135.f, a1 = -135.f + 270.f * (float)std::clamp(norm, 0.0, 1.0);
    if (enabled)
      g.DrawArc(c, cx, cy, rad, std::min(a0, a1), std::max(a0, a1), nullptr, 3.f);
    const float rads = (a1 - 90.f) * 3.14159265f / 180.f;
    g.DrawLine(enabled ? COLOR_WHITE : IColor(255, 90, 92, 100), cx, cy, cx + std::cos(rads) * rad * 0.8f,
               cy + std::sin(rads) * rad * 0.8f, nullptr, 2.f);
    const IRECT text = area.GetReducedFromLeft(d + 6.f);
    const IColor tc = enabled ? IColor(255, 220, 224, 232) : IColor(255, 90, 92, 100);
    g.DrawText(IText(11.f, tc.WithOpacity(0.75f), "Roboto-Regular", EAlign::Near), label, text.GetFromTop(text.H() * 0.5f));
    g.DrawText(IText(14.f, tc, "Roboto-Regular", EAlign::Near), value.c_str(), text.GetFromBottom(text.H() * 0.5f));
  }

  void _DrawPanel(IGraphics& g)
  {
    const IRECT p = _Panel();
    g.FillRoundRect(IColor(255, 18, 20, 28), p, 6.f);
    for (int b = 0; b < proeq::kNumBands; b++)
    {
      char s[4];
      std::snprintf(s, sizeof(s), "%d", b + 1);
      const IRECT r = _BandButton(b);
      const IColor c = _BandColor(b);
      g.FillRoundRect(b == mView->selected ? c : (_On(b) ? c.WithOpacity(0.35f) : IColor(255, 30, 32, 40)), r, 5.f);
      g.DrawRoundRect(c, r, 5.f, nullptr, 1.f);
      g.DrawText(IText(12.f, b == mView->selected ? COLOR_BLACK : COLOR_WHITE, "Roboto-Regular"), s, r);
    }
    const int sel = mView->selected;
    if (sel < 0)
    {
      g.DrawText(IText(13.f, IColor(200, 200, 205, 215), "Roboto-Regular", EAlign::Near),
                 "Pick a band (a number, or a node on the graph) to edit it here.",
                 _Row(0).GetReducedFromLeft(_Row(0).W() * 0.31f));
      return;
    }
    const IColor c = _BandColor(sel);
    const proeq::Type type = _Type(sel);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s  >", proeq::TypeName(type));
    _DrawButton(g, _TypeBox(), buf, false, c);
    std::snprintf(buf, sizeof(buf), "%d dB/oct", proeq::SlopeDBPerOct(_Slope(sel)));
    _DrawButton(g, _SlopeBox(), buf, false, c, proeq::IsCut(type));
    _DrawButton(g, _OnBox(), _On(sel) ? "ON" : "OFF", _On(sel), c);
    _DrawButton(g, _FlipBox(), "FLIP", false, c, proeq::HasGain(type));
    const double fNorm = std::log(_Freq(sel) / proeq::kMinHz) / std::log(proeq::kMaxHz / proeq::kMinHz);
    _DrawDial(g, _Dial(0), "FREQ", _FreqText(_Freq(sel)), fNorm, false, c, true);
    std::snprintf(buf, sizeof(buf), "%+.1f dB", proeq::HasGain(type) ? _Gain(sel) : 0.0);
    _DrawDial(g, _Dial(1), _Gain(sel) < 0.0 ? "CUT" : "BOOST", proeq::HasGain(type) ? buf : "--",
              (_Gain(sel) + proeq::kMaxGainDB) / (2.0 * proeq::kMaxGainDB), true, c, proeq::HasGain(type));
    std::snprintf(buf, sizeof(buf), "%.2f", _Q(sel));
    const double qNorm = std::log(_Q(sel) / proeq::kMinQ) / std::log(proeq::kMaxQ / proeq::kMinQ);
    _DrawDial(g, _Dial(2), proeq::IsCut(type) ? "RESONANCE" : "Q", buf, qNorm, false, c, true);
    _DrawButton(g, _StepButton(0), "-1 dB", false, c, proeq::HasGain(type));
    _DrawButton(g, _StepButton(1), "+1 dB", false, c, proeq::HasGain(type));
  }

  void _DrawChips(IGraphics& g)
  {
    for (int i = 0; i < kNumChips; i++)
    {
      if (!_ChipShown(i))
        continue;
      bool on;
      const char* label;
      IColor c;
      switch (i)
      {
        case kChipAnalyzer:
          on = mView->analyzer;
          label = "ANALYZER";
          c = IColor(255, 90, 200, 160);
          break;
        case kChipFreeze:
          on = mView->frozen;
          label = "FREEZE";
          c = IColor(255, 120, 200, 255);
          break;
        case kChipA:
          on = mView->showA;
          label = mLabelA.c_str();
          c = IColor(255, 140, 150, 175);
          break;
        default:
          on = mView->showB;
          label = mLabelB.c_str();
          c = IColor(255, 240, 150, 70);
          break;
      }
      const bool enabled = i == kChipAnalyzer || mView->analyzer;
      const IRECT r = _Chip(i);
      g.FillRoundRect(on && enabled ? c.WithOpacity(0.35f) : IColor(255, 26, 27, 32), r, 5.f);
      g.DrawRoundRect(enabled ? c : IColor(255, 60, 62, 70), r, 5.f, nullptr, 1.f);
      g.DrawText(IText(mCompact ? 9.f : 11.f, on && enabled ? COLOR_WHITE : (enabled ? c : IColor(255, 80, 82, 90)),
                       "Roboto-Regular"),
                 label, r);
    }
  }

  spectrum::Tap* mTapA;
  spectrum::Tap* mTapB;
  std::string mLabelA, mLabelB;
  spectrum::EqViewState mOwnView;
  spectrum::EqViewState* mView;
  bool mCompact = false;
  std::function<void()> mOnClick;
  spectrum::Analyzer mAnalyzer;
  std::chrono::steady_clock::time_point mLast, mLastFrame;
  double mFps = 30.0;
  int mHover = -1, mDragBand = -1;
  Drag mDrag = Drag::None;
  float mDragStartX = 0.f, mDragStartY = 0.f;
  double mDragStartFreq = 1000.0, mDragStartGain = 0.0;
  std::vector<float> mXs, mYs;
};

/// The spectrum as a floating window (1.50.0): drag its title bar to move it, its bottom-right corner
/// to resize it. It's attached above every block popup (the Amp's and the Tube Lab's included), so
/// it stays in view while you play with any of them; the full pages (Settings, Output EQ, Tuner)
/// still cover it. DOCK puts it back in the strip above the meters; X hides it.
class NAMSpectrumWindowControl : public IContainerBase
{
public:
  struct Callbacks
  {
    std::function<void()> dock, close;
    std::function<void(const IRECT&)> moved; // after a drag or resize ends
  };

  NAMSpectrumWindowControl(const IRECT& bounds, NAMEqGraphControl* graph, Callbacks callbacks)
  : IContainerBase(bounds)
  , mGraph(graph)
  , mCallbacks(std::move(callbacks))
  {
    mIgnoreMouse = false;
  }

  static constexpr float kTitleH = 24.f, kMinW = 280.f, kMinH = 140.f;
  // Kept this far from the right and bottom edges, so its resize grip never sits on the window's own
  // resize corner (a drag there resized the whole app instead).
  static constexpr float kEdge = 28.f;
  static IRECT Usable(const IRECT& win) { return win.GetReducedFromRight(kEdge).GetReducedFromBottom(kEdge); }

  void OnAttached() override
  {
    AddChildControl(mGraph);
    _Place();
  }

  void Draw(IGraphics& g) override
  {
    const IColor accent = PluginColors::NAM_THEMECOLOR;
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.35f), mRECT.GetTranslated(3.f, 5.f), 10.f);
    g.FillRoundRect(IColor(255, 16, 18, 26), mRECT, 10.f);
    g.DrawRoundRect(accent.WithOpacity(mMode != Mode::None ? 1.f : 0.7f), mRECT, 10.f, nullptr, 1.5f);
    const IRECT title = _Title();
    g.DrawText(IText(12.f, IColor(255, 210, 214, 225), "Roboto-Regular", EAlign::Near),
               "OUTPUT SPECTRUM   (drag to move)", title.GetReducedFromLeft(10.f));
    _Button(g, _DockRect(), "DOCK", mHover == 1);
    _Button(g, _CloseRect(), "", mHover == 2);
    // Resize grip: three short diagonals.
    const IRECT grip = _GripRect();
    const IColor gc = IColor(255, 150, 156, 175).WithOpacity(mHover == 3 ? 1.f : 0.6f);
    for (int k = 1; k <= 3; k++)
    {
      const float d = grip.W() * k / 3.f;
      g.DrawLine(gc, grip.R - d, grip.B - 1.f, grip.R - 1.f, grip.B - d, nullptr, 1.2f);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (_CloseRect().Contains(x, y))
    {
      if (mCallbacks.close)
        mCallbacks.close();
      return;
    }
    if (_DockRect().Contains(x, y))
    {
      if (mCallbacks.dock)
        mCallbacks.dock();
      return;
    }
    mMode = _GripRect().GetPadded(4.f).Contains(x, y) ? Mode::Resize : _Title().Contains(x, y) ? Mode::Move : Mode::None;
    mStartRect = mRECT;
    mStartX = x;
    mStartY = y;
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mMode == Mode::None)
      return;
    const IRECT win = Usable(GetUI()->GetBounds());
    const float dx = x - mStartX, dy = y - mStartY;
    IRECT r = mStartRect;
    if (mMode == Mode::Move)
    {
      r = r.GetTranslated(dx, dy);
      r = r.GetTranslated(std::clamp(r.L, win.L, std::max(win.L, win.R - r.W())) - r.L,
                          std::clamp(r.T, win.T, std::max(win.T, win.B - r.H())) - r.T);
    }
    else
    {
      r.R = std::clamp(r.R + dx, r.L + kMinW, std::max(r.L + kMinW, win.R));
      r.B = std::clamp(r.B + dy, r.T + kMinH, std::max(r.T + kMinH, win.B));
    }
    mRECT = mTargetRECT = r;
    _Place();
    GetUI()->SetAllControlsDirty(); // repaint where it was, too
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (mMode != Mode::None && mCallbacks.moved)
      mCallbacks.moved(mRECT);
    mMode = Mode::None;
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    const int hover = _DockRect().Contains(x, y)                    ? 1
                      : _CloseRect().Contains(x, y)                 ? 2
                      : _GripRect().GetPadded(4.f).Contains(x, y) ? 3
                                                                    : 0;
    if (hover != mHover)
    {
      mHover = hover;
      SetDirty(false);
    }
    IControl::OnMouseOver(x, y, mod);
  }

  void OnMouseOut() override
  {
    mHover = 0;
    IControl::OnMouseOut();
  }

private:
  enum class Mode
  {
    None,
    Move,
    Resize
  };

  IRECT _Title() const { return mRECT.GetFromTop(kTitleH); }
  IRECT _CloseRect() const { return _Title().GetFromRight(kTitleH).GetPadded(-4.f).GetTranslated(-4.f, 0.f); }
  IRECT _DockRect() const
  {
    const IRECT c = _CloseRect();
    return IRECT(c.L - 56.f, c.T, c.L - 6.f, c.B);
  }
  IRECT _GripRect() const { return mRECT.GetFromBottom(14.f).GetFromRight(14.f).GetTranslated(-3.f, -3.f); }

  void _Place()
  {
    if (mGraph)
      mGraph->SetTargetAndDrawRECTs(mRECT.GetReducedFromTop(kTitleH).GetPadded(-6.f).GetReducedFromBottom(10.f));
  }

  void _Button(IGraphics& g, const IRECT& r, const char* text, bool hover)
  {
    g.FillRoundRect(COLOR_WHITE.WithOpacity(hover ? 0.18f : 0.07f), r, 4.f);
    const IColor c = COLOR_WHITE.WithOpacity(hover ? 0.95f : 0.65f);
    if (*text)
      g.DrawText(IText(11.f, c, "Roboto-Regular"), text, r);
    else
    {
      const IRECT x = r.GetCentredInside(std::min(r.W(), r.H()) * 0.45f);
      g.DrawLine(c, x.L, x.T, x.R, x.B, nullptr, 1.5f);
      g.DrawLine(c, x.L, x.B, x.R, x.T, nullptr, 1.5f);
    }
  }

  NAMEqGraphControl* mGraph;
  Callbacks mCallbacks;
  Mode mMode = Mode::None;
  IRECT mStartRect;
  float mStartX = 0.f, mStartY = 0.f;
  int mHover = 0;
};
