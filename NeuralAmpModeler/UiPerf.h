#pragma once

#include <chrono>

// LOW GPU (1.79.0), Settings > Display. Each repaint composites the whole window, so what costs
// graphics power is how many frames a second something asks for, not how much of the window it
// covers. With it on, the controls that animate by themselves (the live spectrum analyzers and the
// IN/OUT meters) ask for at most kLowGpuFps frames a second, all on the same clock so they share
// frames, and the analyzers stop once the signal has been quiet for a moment. Knobs, clicks and
// popups still repaint at the full frame rate. UI thread only.
namespace namui
{
inline bool gLowGpu = false;
constexpr int kLowGpuFps = 12;

// The current slot on the shared low-GPU clock: a control due for an animation frame draws at most
// once per slot.
inline long long LowGpuSlot()
{
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() / (1000 / kLowGpuFps);
}
} // namespace namui
