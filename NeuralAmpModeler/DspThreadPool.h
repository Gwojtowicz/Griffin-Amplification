#pragma once

// EXTENDED DSP's worker threads (1.81.0): blocks stacked in one grid column don't depend on each other,
// so the audio thread can hand some of them to other cores and run the rest itself.
//
// Real-time rules: Run() never allocates, locks or waits on the OS. The audio thread publishes a batch
// of tasks and works through them alongside the workers; tasks are claimed with a compare-and-swap on a
// counter tagged with the batch's generation, so a worker still finishing an older batch can never claim
// a task of the next one. The audio thread then spins (briefly: it only waits for tasks already being run)
// until every task is done. Workers run at the "Pro Audio" scheduling priority (as the audio thread
// does), spin for a short while after each batch (the next column's batch usually follows within
// microseconds), then sleep on the generation counter until woken.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif
#include <immintrin.h>

namespace dsppool
{
class ThreadPool
{
public:
  ThreadPool() = default;
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ~ThreadPool() { Stop(); }

  // Starts up to numWorkers threads (call off the audio thread). Threads already running are kept.
  void Start(int numWorkers)
  {
    while ((int)mThreads.size() < numWorkers)
    {
      const int idx = (int)mThreads.size();
      mThreads.emplace_back([this, idx]() { _Worker(idx); });
      mNumStarted.store((int)mThreads.size(), std::memory_order_release);
    }
  }

  void Stop()
  {
    if (mThreads.empty())
      return;
    mQuit.store(true, std::memory_order_release);
    mGen.fetch_add(1, std::memory_order_acq_rel);
    mGen.notify_all();
    for (auto& t : mThreads)
      if (t.joinable())
        t.join();
    mThreads.clear();
    mNumStarted.store(0, std::memory_order_release);
    mQuit.store(false, std::memory_order_release);
  }

  int NumThreads() const { return mNumStarted.load(std::memory_order_acquire); }

  // How many of the started workers join in (the rest stay asleep). Any thread may set it.
  void SetActiveWorkers(int n) { mActive.store(n, std::memory_order_release); }
  int ActiveWorkers() const
  {
    const int n = mActive.load(std::memory_order_acquire);
    const int started = NumThreads();
    return n < started ? n : started;
  }

  // Runs f(task, slot) for task = 0..numTasks-1, on the calling thread (slot 0) and the active workers
  // (slots 1..), and returns once all are done. Audio thread only; not re-entrant.
  template <typename F>
  void Run(int numTasks, F& f)
  {
    if (numTasks <= 0)
      return;
    if (ActiveWorkers() == 0 || numTasks == 1)
    {
      for (int t = 0; t < numTasks; t++)
        f(t, 0);
      return;
    }
    mCtx = &f;
    mFn = [](void* ctx, int task, int slot) { (*static_cast<F*>(ctx))(task, slot); };
    mNumTasks.store(numTasks, std::memory_order_relaxed);
    // Only as many workers as there are tasks beyond the caller's own: the rest stay asleep (a spinning
    // worker can share a physical core with the audio thread and slow it down).
    mWanted.store(std::min(numTasks - 1, ActiveWorkers()), std::memory_order_relaxed);
    mDone.store(0, std::memory_order_relaxed);
    const uint32_t gen = mGen.load(std::memory_order_relaxed) + 1;
    mWork.store((uint64_t)gen << 32, std::memory_order_release);
    mGen.store(gen, std::memory_order_release);
    if (mSleepers.load(std::memory_order_acquire) > 0)
      mGen.notify_all();
    // Work alongside the workers.
    _Drain(gen, 0);
    // Wait for tasks claimed by workers that are still running.
    while (mDone.load(std::memory_order_acquire) < numTasks)
      _mm_pause();
  }

private:
  // Claims and runs tasks of batch `gen` until none are left.
  void _Drain(uint32_t gen, int slot)
  {
    for (;;)
    {
      uint64_t w = mWork.load(std::memory_order_acquire);
      for (;;)
      {
        if ((uint32_t)(w >> 32) != gen || (int)(uint32_t)w >= mNumTasks.load(std::memory_order_relaxed))
          return;
        if (mWork.compare_exchange_weak(w, w + 1, std::memory_order_acq_rel, std::memory_order_acquire))
          break;
      }
      const int task = (int)(uint32_t)w;
      mFn(mCtx, task, slot);
      mDone.fetch_add(1, std::memory_order_acq_rel);
    }
  }

  void _Worker(int idx)
  {
#ifdef _WIN32
    // The audio thread's scheduling class, so a worker holding a task isn't pre-empted by ordinary work.
    bool mmcss = false;
    if (HMODULE avrt = LoadLibraryA("avrt.dll"))
    {
      using AvSetFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
      if (auto avSet = (AvSetFn)(void*)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"))
      {
        DWORD taskIndex = 0;
        mmcss = avSet(L"Pro Audio", &taskIndex) != nullptr;
      }
    }
    if (!mmcss)
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
    // Denormals flushed to zero (FTZ + DAZ), as on the audio thread: the control register is per thread.
    _mm_setcsr(_mm_getcsr() | 0x8040);

    uint32_t seen = mGen.load(std::memory_order_acquire);
    bool worked = false; // spin for the next batch only after taking part in one
    for (;;)
    {
      uint32_t g;
      int spins = worked ? 0 : kSpinIters;
      while ((g = mGen.load(std::memory_order_acquire)) == seen)
      {
        if (++spins < kSpinIters)
          _mm_pause();
        else
        {
          mSleepers.fetch_add(1, std::memory_order_acq_rel);
          // Re-checked after registering as a sleeper, so a batch published in between isn't missed.
          if (mGen.load(std::memory_order_acquire) == seen)
            mGen.wait(seen, std::memory_order_acquire);
          mSleepers.fetch_sub(1, std::memory_order_acq_rel);
          spins = 0;
        }
      }
      seen = g;
      if (mQuit.load(std::memory_order_acquire))
        return;
      worked = idx < mWanted.load(std::memory_order_relaxed);
      if (worked)
        _Drain(g, idx + 1);
    }
  }

  // About 100-200 microseconds of spinning before a worker sleeps: long enough to catch the next
  // column's batch in the same audio block, short enough not to keep the cores busy between blocks.
  static constexpr int kSpinIters = 3000;

  std::vector<std::thread> mThreads;
  std::atomic<uint32_t> mGen{0};
  std::atomic<uint64_t> mWork{0}; // batch generation << 32 | next task
  std::atomic<int> mDone{0};
  std::atomic<int> mSleepers{0};
  std::atomic<int> mActive{0};
  std::atomic<int> mWanted{0}; // workers 0..mWanted-1 join the current batch
  std::atomic<bool> mQuit{false};
  std::atomic<int> mNumStarted{0}; // mThreads.size(), readable from the audio thread
  void* mCtx = nullptr;
  void (*mFn)(void*, int, int) = nullptr;
  std::atomic<int> mNumTasks{0};
};
} // namespace dsppool
