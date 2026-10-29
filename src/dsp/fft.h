// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <vector>

namespace fx {

// Complex radix-2 FFT (in place), split re/im arrays.
class FFT {
 public:
  explicit FFT(int n = 0) { init(n); }
  void init(int n);
  int size() const { return n_; }
  void forward(float* re, float* im) const { run(re, im, false); }
  void inverse(float* re, float* im) const { run(re, im, true); }  // no 1/N normalisation

 private:
  void run(float* re, float* im, bool inv) const;
  int n_ = 0;
  std::vector<int> rev_;
  std::vector<float> cos_, sin_;
};

}  // namespace fx
