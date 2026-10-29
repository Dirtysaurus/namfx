// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <atomic>
#include <string>
#include <vector>
#include "dsp/dsp_util.h"

namespace fx {

struct TunerResult {
  bool valid = false;
  float freq = 0, cents = 0;
  int midi = 0;
  std::string note;
  int octave = 0;
};

// The audio thread fills a decimated ring buffer, the control thread runs YIN.
class Tuner {
 public:
  void prepare(float sr);
  void push(const float* x, int n);              // audio thread
  TunerResult analyze(float a4 = 440.0f);        // control thread

 private:
  static constexpr int kRing = 8192, kDecim = 4, kWin = 1536;
  float sr_ = 48000, dsr_ = 12000;
  Biquad aa_;
  int phase_ = 0;
  std::vector<float> ring_ = std::vector<float>(kRing, 0.0f);
  std::atomic<unsigned> w_{0};
  std::vector<float> win_, d_;
};

}  // namespace fx
