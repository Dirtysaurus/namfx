// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <vector>
#include "dsp/fft.h"

namespace fx {

// Uniformly partitioned convolution (overlap-save) with no added latency:
// partition length = audio block size.
class Convolver {
 public:
  // ir: impulse response; block: block size process() will be called with.
  bool init(const std::vector<float>& ir, int block);
  void process(const float* in, float* out);  // exactly `block` samples
  int block() const { return B_; }
  int partitions() const { return P_; }
  void reset();

 private:
  int B_ = 0, N_ = 0, P_ = 0, bins_ = 0, pos_ = 0;
  FFT fft_;
  std::vector<float> hre_, him_;   // P x bins
  std::vector<float> xre_, xim_;   // P x bins (frequency-domain delay line)
  std::vector<float> inbuf_;       // 2B
  std::vector<float> wre_, wim_;   // N work buffers
  std::vector<float> are_, aim_;   // bins accumulator
};

}  // namespace fx
