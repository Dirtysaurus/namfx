// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
//
// A second real-time thread for the audio graph: the audio thread hands it a job each period (a pipeline stage or one lane of
// a split) and waits for the result. No locks and no allocation per job: sequence numbers in atomics, a short spin, then a futex
// sleep between periods so an idle worker does not burn a core.
#pragma once
#include <atomic>
#include <cstdint>
#include <thread>

namespace fx {

class Worker {
 public:
  using Fn = void (*)(void*);
  ~Worker() { stop(); }
  // cpu < 0: do not pin. priority: SCHED_FIFO priority (0 = leave alone).
  void start(int cpu, int priority);
  void stop();
  bool running() const { return run_.load(); }
  // audio thread: runs fn(ctx) on the worker. ctx must stay valid until wait() returned true.
  void submit(Fn fn, void* ctx);
  // audio thread: blocks (spinning) until the last submitted job is done. false = it took longer than timeoutUs.
  bool wait(int timeoutUs = 20000);
  bool idle() const { return done_.load(std::memory_order_acquire) == seq_.load(std::memory_order_relaxed); }

 private:
  void loop(int cpu, int priority);
  std::atomic<uint32_t> seq_{0}, done_{0};
  std::atomic<uint32_t> sleeping_{0};
  Fn fn_ = nullptr;
  void* ctx_ = nullptr;
  std::atomic<bool> run_{false};
  std::thread th_;
};

}  // namespace fx
