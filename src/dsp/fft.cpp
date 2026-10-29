// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/fft.h"
#include <cmath>
#include <utility>

namespace fx {

void FFT::init(int n) {
  n_ = n;
  if (n <= 0) return;
  int bits = 0;
  while ((1 << bits) < n) ++bits;
  rev_.resize(n);
  for (int i = 0; i < n; ++i) {
    int r = 0;
    for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
    rev_[i] = r;
  }
  cos_.resize(n / 2);
  sin_.resize(n / 2);
  for (int i = 0; i < n / 2; ++i) {
    const double a = -2.0 * M_PI * i / n;
    cos_[i] = (float)std::cos(a);
    sin_[i] = (float)std::sin(a);
  }
}

void FFT::run(float* re, float* im, bool inv) const {
  const int n = n_;
  for (int i = 0; i < n; ++i) {
    const int j = rev_[i];
    if (j > i) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  const float sgn = inv ? -1.0f : 1.0f;
  for (int len = 2; len <= n; len <<= 1) {
    const int half = len >> 1, step = n / len;
    for (int i = 0; i < n; i += len) {
      for (int k = 0; k < half; ++k) {
        const float wr = cos_[k * step], wi = sgn * sin_[k * step];
        const int a = i + k, b = a + half;
        const float tr = re[b] * wr - im[b] * wi;
        const float ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
}

}  // namespace fx
