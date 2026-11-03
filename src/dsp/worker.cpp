// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/worker.h"
#include <pthread.h>
#include <sched.h>
#include <chrono>
#include <cstdio>
#include "dsp/dsp_util.h"

#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#define NAMFX_FUTEX 1
#endif

namespace fx {

static inline void cpuRelax() {
#if defined(__aarch64__) || defined(__arm__)
  asm volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
  asm volatile("pause" ::: "memory");
#endif
}

static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t), "futex needs a plain 32 bit atomic");

static void futexWait(std::atomic<uint32_t>* a, uint32_t expected) {
#ifdef NAMFX_FUTEX
  syscall(SYS_futex, reinterpret_cast<uint32_t*>(a), FUTEX_WAIT_PRIVATE, expected, nullptr, nullptr, 0);
#else
  (void)a; (void)expected;
#endif
}

static void futexWake(std::atomic<uint32_t>* a) {
#ifdef NAMFX_FUTEX
  syscall(SYS_futex, reinterpret_cast<uint32_t*>(a), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
#else
  (void)a;
#endif
}

void Worker::start(int cpu, int priority) {
  if (run_.load()) return;
  run_.store(true);
  th_ = std::thread([this, cpu, priority] { loop(cpu, priority); });
}

void Worker::stop() {
  if (!run_.exchange(false)) return;
  seq_.fetch_add(1, std::memory_order_seq_cst);
  futexWake(&seq_);
  if (th_.joinable()) th_.join();
}

void Worker::submit(Fn fn, void* ctx) {
  fn_ = fn;
  ctx_ = ctx;
  seq_.fetch_add(1, std::memory_order_seq_cst);
  if (sleeping_.load(std::memory_order_seq_cst)) futexWake(&seq_);
}

bool Worker::wait(int timeoutUs) {
  const uint32_t target = seq_.load(std::memory_order_relaxed);
  if (done_.load(std::memory_order_acquire) == target) return true;
  const auto t0 = std::chrono::steady_clock::now();
  unsigned spins = 0;
  while (done_.load(std::memory_order_acquire) != target) {
    cpuRelax();
    if ((++spins & 255u) == 0 && std::chrono::steady_clock::now() - t0 > std::chrono::microseconds(timeoutUs)) return false;
  }
  return true;
}

void Worker::loop(int cpu, int priority) {
  enableFlushToZero();  // denormals are expensive here too
  if (cpu >= 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  }
  if (priority > 0) {
    sched_param sp{};
    sp.sched_priority = priority;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) std::fprintf(stderr, "[audio] worker: cannot set SCHED_FIFO\n");
  }
  uint32_t last = seq_.load(std::memory_order_acquire);
  for (;;) {
    uint32_t s;
    unsigned spins = 0;
    while ((s = seq_.load(std::memory_order_acquire)) == last) {
      if (!run_.load(std::memory_order_relaxed)) return;
      if (++spins < 4000) {
        cpuRelax();
      } else {  // nothing for a while: sleep until the next period's job
        sleeping_.store(1, std::memory_order_seq_cst);
        if (seq_.load(std::memory_order_seq_cst) == last && run_.load(std::memory_order_relaxed)) futexWait(&seq_, last);
        sleeping_.store(0, std::memory_order_seq_cst);
        spins = 0;
      }
    }
    if (!run_.load(std::memory_order_relaxed)) return;
    if (fn_) fn_(ctx_);
    last = s;
    done_.store(s, std::memory_order_release);
  }
}

}  // namespace fx
