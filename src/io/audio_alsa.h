// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace fx {

class Engine;

struct AudioOptions {
  std::string capture = "hw:0,0";   // "" = no input (WAV only)
  std::string playback = "hw:0,0";
  int periods = 2;
  int inChannel = 0;                 // 0 = left, 1 = right, 2 = sum
  int cpu = 3;                       // CPU core for the audio thread (-1 = any)
  int priority = 80;                 // SCHED_FIFO
};

// Audio thread: ALSA capture -> Engine::process -> ALSA playback.
// Without ALSA (or with --null-audio) a clock-driven "null" driver runs.
class AudioDriver {
 public:
  AudioDriver(Engine& e, const AudioOptions& o) : eng_(e), opt_(o) {}
  ~AudioDriver() { stop(); }
  bool start(bool nullDriver, std::string& err);
  void stop();
  std::string description() const { return desc_; }

 private:
  void runAlsa();
  void runNull();
  void setupThread();
  Engine& eng_;
  AudioOptions opt_;
  std::thread th_;
  std::atomic<bool> run_{false};
  std::string desc_;
  void* cap_ = nullptr;
  void* pb_ = nullptr;
  int capCh_ = 2, pbCh_ = 2, capFmt_ = 0, pbFmt_ = 0;
};

}  // namespace fx
