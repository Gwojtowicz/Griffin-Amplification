#pragma once

// The signal-chain grid (1.17.0): Fractal-style placement. Blocks sit anywhere on a kRows x kCols
// grid and are wired with cables. Signal flows left to right: a cable always runs from a block to
// one in a later column, so the column order is the processing order. Blocks in the same column
// run in parallel (a split); a block fed by several cables gets their average (a merge).
//
// This header is plain C++ (no iPlug2), so the editing rules can be tested on their own. The
// plugin stores a Grid in params (see kGridCell0 / kGridIn0 / kGridOut) and rebuilds it from them
// on both the audio thread (to process) and the UI thread (to draw and edit).

#include <cstdint>
#include <cstdlib>

namespace chaingrid
{
constexpr int kRows = 4;
constexpr int kCols = 14;
constexpr int kNumCells = kRows * kCols;

// Block ids. 0..kNumPedalBlocks-1 are the pedals (same indices as kPedals in the plugin), then the
// fixed blocks. CAB / SEND / RETURN deliberately match the plugin's kCabPedalIdx /
// kLoopSendPedalIdx / kLoopReturnPedalIdx, so a block id can go straight into applyPedal.
constexpr int kNumPedalBlocks = 11;
constexpr int kCab = kNumPedalBlocks;
constexpr int kLoopSend = kNumPedalBlocks + 1;
constexpr int kLoopReturn = kNumPedalBlocks + 2;
constexpr int kAmp = kNumPedalBlocks + 3;
// Cable endpoints that aren't blocks: Input is a source (bit kInput of a block's input mask),
// Output is a destination (its own mask, Grid::out). Input's id sits between the original blocks
// and the extra pedals, where 1.17 put it, so saved masks keep their meaning.
constexpr int kInput = kNumPedalBlocks + 4;
// Extra pedals (1.19.0): more copies of the effect pedals (the first kNumMultiPedalTypes in
// kPedals: Comp, OD, Chorus, Delay, Reverb, EQ, Tremolo), so the AMP NEST can hand out another
// Reverb after one is in the chain. Copy k (1..kExtraCopiesPerType) of type t has id
// ExtraPedalId(t, k).
constexpr int kNumMultiPedalTypes = 7;
constexpr int kExtraCopiesPerType = 2;
constexpr int kFirstExtraPedal = kInput + 1;
constexpr int kNumExtraPedals = kNumMultiPedalTypes * kExtraCopiesPerType;
// Pedals added after the extra copies (1.27.0: Flanger, 1.28.0: Phaser, 1.34.0: Real Feel, 1.36.0:
// CAB 2 and CAB 3), one
// copy each. Their ids come after every older id, so the ids (and the GridCell/GridIn names) in
// saved states keep their meaning. Each is its own pedal type, numbered by its id.
constexpr int kFirstLatePedal = kFirstExtraPedal + kNumExtraPedals;
constexpr int kFlanger = kFirstLatePedal;
constexpr int kPhaser = kFirstLatePedal + 1;
constexpr int kRealFeel = kFirstLatePedal + 2; // The Tube Lab's Real Feel, as a block (RealFeel.h)
constexpr int kCab2 = kFirstLatePedal + 3; // 1.36.0: extra cab IR blocks
constexpr int kCab3 = kFirstLatePedal + 4;
// 1.44.0: SKY VERB, FLANGE DBL, WAH, PREC DRIVE, APEX PRE (NewPedals.h).
constexpr int kSkyVerb = kFirstLatePedal + 5;
constexpr int kFlangerDoubler = kFirstLatePedal + 6;
constexpr int kWah = kFirstLatePedal + 7;
constexpr int kPrecisionDrive = kFirstLatePedal + 8;
constexpr int kApexPreamp = kFirstLatePedal + 9;
// 1.46.0: PRO EQ (ProEQ.h), a parametric EQ with a live spectrum.
constexpr int kProEq = kFirstLatePedal + 10;
// 1.52.0: GATE and PRO COMP (Dynamics.h).
constexpr int kGate = kFirstLatePedal + 11;
constexpr int kStudioComp = kFirstLatePedal + 12;
constexpr int kNumLatePedals = 13;
// Every id: blocks 0..kInput-1, Input, the extra pedals, then the late pedals.
constexpr int kNumIds = kFirstLatePedal + kNumLatePedals;
constexpr int kOutput = kNumIds;
using Mask = uint64_t;
// One bit per source id (Output is never a source). 64 bits since 1.34.0 (32 ids filled the old
// 32-bit masks); the mask params are stepped doubles, exact for whole numbers up to 2^53, so they
// hold any mask of up to 53 ids, and states saved with 32-bit masks load unchanged.
static_assert(kNumIds <= 53, "mask params are doubles: exact up to 2^53");
constexpr Mask Bit(int id) { return (Mask)1 << id; }

constexpr bool IsExtraPedal(int id) { return id >= kFirstExtraPedal && id < kFirstLatePedal; }
constexpr bool IsLatePedal(int id) { return id >= kFirstLatePedal && id < kNumIds; }
constexpr bool IsPedal(int id)
{
  return (id >= 0 && id < kNumPedalBlocks) || IsExtraPedal(id) || IsLatePedal(id);
}
// A pedal type (a row in the AMP NEST): an original pedal, or a late pedal (its own type).
constexpr bool IsPedalType(int t) { return (t >= 0 && t < kNumPedalBlocks) || IsLatePedal(t); }
// Which kPedals entry a pedal is a copy of, and which copy (0 = the original).
constexpr int PedalType(int id) { return IsExtraPedal(id) ? (id - kFirstExtraPedal) / kExtraCopiesPerType : id; }
constexpr int PedalCopy(int id) { return IsExtraPedal(id) ? 1 + (id - kFirstExtraPedal) % kExtraCopiesPerType : 0; }
constexpr int PedalCopyId(int type, int copy)
{
  return copy == 0 ? type : kFirstExtraPedal + type * kExtraCopiesPerType + copy - 1;
}
constexpr int NumCopies(int type) { return type < kNumMultiPedalTypes ? 1 + kExtraCopiesPerType : 1; }
// Grid params exist for every id but Input: slot = id, less one past Input.
constexpr int ParamSlot(int id) { return id < kInput ? id : id - 1; }
constexpr int kNumParamSlots = kNumIds - 1;

constexpr int CellRow(int cell) { return cell / kCols; }
constexpr int CellCol(int cell) { return cell % kCols; }
constexpr int MakeCell(int row, int col) { return row * kCols + col; }

struct Grid
{
  int cell[kNumIds]; // -1 = not on the grid (always, for kInput)
  Mask in[kNumIds]; // bit s set = cable from source s (a block id, or kInput)
  Mask out = 0; // sources cabled to Output

  Grid()
  {
    for (int b = 0; b < kNumIds; b++)
    {
      cell[b] = -1;
      in[b] = 0;
    }
  }

  bool Placed(int b) const { return b >= 0 && b < kNumIds && b != kInput && cell[b] >= 0; }

  // Column of a cable endpoint: Input sits just left of the grid, Output just right of it.
  int ColOf(int node) const
  {
    if (node == kInput)
      return -1;
    if (node == kOutput)
      return kCols;
    return Placed(node) ? CellCol(cell[node]) : -2;
  }
  // Input and Output are drawn level with the top row.
  int RowOf(int node) const
  {
    if (node == kInput || node == kOutput)
      return 0;
    return Placed(node) ? CellRow(cell[node]) : 0;
  }

  int BlockAt(int c) const
  {
    for (int b = 0; b < kNumIds; b++)
      if (b != kInput && cell[b] == c)
        return b;
    return -1;
  }

  Mask InputsOf(int dst) const { return dst == kOutput ? out : in[dst]; }
  void SetInputs(int dst, Mask mask)
  {
    if (dst == kOutput)
      out = mask;
    else
      in[dst] = mask;
  }
  bool HasEdge(int src, int dst) const { return (InputsOf(dst) >> src) & 1; }

  // A cable can only run rightwards, between endpoints that exist.
  bool EdgeValid(int src, int dst) const
  {
    if (src == kOutput || dst == kInput || src == dst)
      return false;
    if (src != kInput && !Placed(src))
      return false;
    if (dst != kOutput && !Placed(dst))
      return false;
    return ColOf(src) < ColOf(dst);
  }

  // Adds the cable if it's missing, removes it if it's there. Returns false for an invalid cable.
  bool ToggleEdge(int src, int dst)
  {
    if (!EdgeValid(src, dst))
      return false;
    SetInputs(dst, InputsOf(dst) ^ Bit(src));
    return true;
  }

  void RemoveEdge(int src, int dst) { SetInputs(dst, InputsOf(dst) & (Mask)~Bit(src)); }

  // Takes a block off the grid. Anything left with no input at all (it was fed only by this
  // block, i.e. it was in series) is fed from this block's own sources instead, so the chain
  // closes up behind it; a merge that still has other inputs (it was in parallel) is left alone.
  void Remove(int b)
  {
    if (!Placed(b))
      return;
    const Mask bBit = Bit(b);
    const Mask sources = in[b];
    for (int d = 0; d <= kOutput; d++)
    {
      if (d == kInput || (d < kNumIds && !Placed(d)) || d == b)
        continue;
      const Mask m = InputsOf(d);
      if (!(m & bBit))
        continue;
      Mask next = m & (Mask)~bBit;
      if (next == 0)
        next = sources;
      SetInputs(d, next);
    }
    in[b] = 0;
    cell[b] = -1;
  }

  // Puts block b (not currently placed) at an empty cell and wires it in:
  //  - a column that already holds blocks: b goes in parallel with the nearest one (same inputs,
  //    same outputs);
  //  - an empty column: b is inserted in series into the cable crossing that column (the one
  //    whose ends are nearest b's row);
  //  - no cable crosses it (a broken chain): b is fed from the nearest block to its left and
  //    feeds the nearest block to its right.
  void Insert(int b, int c)
  {
    if (b < 0 || b >= kNumIds || b == kInput || c < 0 || c >= kNumCells || BlockAt(c) >= 0)
      return;
    Remove(b);
    const int row = CellRow(c), col = CellCol(c);

    int sibling = -1;
    for (int o = 0; o < kNumIds; o++)
      if (Placed(o) && CellCol(cell[o]) == col
          && (sibling < 0 || std::abs(CellRow(cell[o]) - row) < std::abs(CellRow(cell[sibling]) - row)))
        sibling = o;

    cell[b] = c;
    const Mask bBit = Bit(b);
    if (sibling >= 0)
    {
      in[b] = in[sibling];
      for (int d = 0; d <= kOutput; d++)
        if (d != kInput && d != b && (d == kOutput || Placed(d)) && HasEdge(sibling, d))
          SetInputs(d, InputsOf(d) | bBit);
      return;
    }

    int bestSrc = -1, bestDst = -1, bestScore = 1 << 30;
    for (int d = 0; d <= kOutput; d++)
    {
      if (d == kInput || d == b || (d < kNumIds && !Placed(d)))
        continue;
      const Mask m = InputsOf(d);
      for (int s = 0; s < kNumIds; s++)
      {
        if (!((m >> s) & 1) || s == b || (s != kInput && !Placed(s)))
          continue;
        if (!(ColOf(s) < col && col < ColOf(d)))
          continue;
        const int ds = std::abs(RowOf(s) - row), dd = std::abs(RowOf(d) - row);
        const int score = (ds < dd ? ds : dd) * 64 + (ColOf(d) - ColOf(s));
        if (score < bestScore)
        {
          bestScore = score;
          bestSrc = s;
          bestDst = d;
        }
      }
    }
    if (bestSrc >= 0)
    {
      RemoveEdge(bestSrc, bestDst);
      in[b] = Bit(bestSrc);
      SetInputs(bestDst, InputsOf(bestDst) | bBit);
      return;
    }

    int src = kInput, dst = kOutput;
    int srcDist = 1 << 30, dstDist = 1 << 30;
    for (int o = 0; o < kNumIds; o++)
    {
      if (o == b || !Placed(o))
        continue;
      const int oc = CellCol(cell[o]);
      const int dist = std::abs(oc - col) * 8 + std::abs(CellRow(cell[o]) - row);
      if (oc < col && dist < srcDist)
      {
        srcDist = dist;
        src = o;
      }
      if (oc > col && dist < dstDist)
      {
        dstDist = dist;
        dst = o;
      }
    }
    in[b] = Bit(src);
    SetInputs(dst, InputsOf(dst) | bBit);
  }

  // Drag-and-drop: re-plugs b at cell c (re-wired by Insert's rules from wherever it lands).
  void Move(int b, int c)
  {
    if (cell[b] == c)
      return;
    Remove(b);
    Insert(b, c);
  }

  // The empty cell nearest `c` (c itself if it's empty), or -1 if the grid is full. `ignore` is
  // treated as empty (the block being dragged).
  int NearestFreeCell(int c, int ignore = -1) const
  {
    int best = -1, bestDist = 1 << 30;
    for (int k = 0; k < kNumCells; k++)
    {
      const int occupant = BlockAt(k);
      if (occupant >= 0 && occupant != ignore)
        continue;
      const int dr = CellRow(k) - CellRow(c), dc = CellCol(k) - CellCol(c);
      const int dist = dr * dr * 2 + dc * dc * 3; // cells are a bit taller than wide
      if (dist < bestDist)
      {
        bestDist = dist;
        best = k;
      }
    }
    return best;
  }
};

// Lays out a pre-1.17 chain (one serial order) on the top row. The FX Loop Send is a tap, so it
// goes on the row below, fed from the block before it, rather than taking a column of its own --
// that way even the longest possible legacy chain (every pedal, AMP, CAB and Loop Return = 14)
// fits in kCols.
inline Grid FromSerialOrder(const int* order, int n)
{
  Grid g;
  int col = 0, prev = kInput;
  bool sendPending = false;
  int sendSource = kInput;
  for (int i = 0; i < n; i++)
  {
    const int b = order[i];
    if (b == kLoopSend)
    {
      sendPending = true;
      sendSource = prev;
      continue;
    }
    if (col >= kCols)
      break;
    g.cell[b] = MakeCell(0, col);
    g.in[b] = Bit(prev);
    if (sendPending)
    {
      g.cell[kLoopSend] = MakeCell(1, col);
      g.in[kLoopSend] = Bit(sendSource);
      sendPending = false;
    }
    prev = b;
    col++;
  }
  g.out = Bit(prev);
  if (sendPending)
  {
    // Send was last in the chain. With the grid full, tap one block earlier instead.
    const int sendCol = col < kCols ? col : kCols - 1;
    g.cell[kLoopSend] = MakeCell(1, sendCol);
    int src = sendSource;
    if (g.ColOf(src) >= sendCol)
    {
      src = kInput;
      for (int b = 0; b < kNumIds; b++)
        if (g.Placed(b) && b != kLoopSend && g.ColOf(b) == sendCol - 1)
          src = b;
    }
    g.in[kLoopSend] = Bit(src);
  }
  return g;
}
} // namespace chaingrid
