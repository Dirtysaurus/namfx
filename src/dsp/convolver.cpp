// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/convolver.h"
#include <algorithm>
#include <cstring>

namespace fx {

bool Convolver::init(const std::vector<float>& ir, int block) {
  if (block <= 0 || ir.empty()) return false;
  B_ = block;
  N_ = 2 * block;
  bins_ = N_ / 2 + 1;
  P_ = (int)((ir.size() + B_ - 1) / B_);
  fft_.init(N_);
  hre_.assign((size_t)P_ * bins_, 0.0f);
  him_.assign((size_t)P_ * bins_, 0.0f);
  xre_.assign((size_t)P_ * bins_, 0.0f);
  xim_.assign((size_t)P_ * bins_, 0.0f);
  inbuf_.assign(N_, 0.0f);
  wre_.assign(N_, 0.0f);
  wim_.assign(N_, 0.0f);
  are_.assign(bins_, 0.0f);
  aim_.assign(bins_, 0.0f);
  for (int p = 0; p < P_; ++p) {
    std::fill(wre_.begin(), wre_.end(), 0.0f);
    std::fill(wim_.begin(), wim_.end(), 0.0f);
    for (int i = 0; i < B_; ++i) {
      const size_t k = (size_t)p * B_ + i;
      if (k < ir.size()) wre_[i] = ir[k];
    }
    fft_.forward(wre_.data(), wim_.data());
    std::memcpy(&hre_[(size_t)p * bins_], wre_.data(), bins_ * sizeof(float));
    std::memcpy(&him_[(size_t)p * bins_], wim_.data(), bins_ * sizeof(float));
  }
  pos_ = 0;
  return true;
}

void Convolver::reset() {
  std::fill(xre_.begin(), xre_.end(), 0.0f);
  std::fill(xim_.begin(), xim_.end(), 0.0f);
  std::fill(inbuf_.begin(), inbuf_.end(), 0.0f);
}

void Convolver::process(const float* in, float* out) {
  // shift input: [previous block | current block]
  std::memmove(inbuf_.data(), inbuf_.data() + B_, B_ * sizeof(float));
  std::memcpy(inbuf_.data() + B_, in, B_ * sizeof(float));
  std::memcpy(wre_.data(), inbuf_.data(), N_ * sizeof(float));
  std::fill(wim_.begin(), wim_.end(), 0.0f);
  fft_.forward(wre_.data(), wim_.data());
  pos_ = (pos_ + P_ - 1) % P_;
  std::memcpy(&xre_[(size_t)pos_ * bins_], wre_.data(), bins_ * sizeof(float));
  std::memcpy(&xim_[(size_t)pos_ * bins_], wim_.data(), bins_ * sizeof(float));

  std::fill(are_.begin(), are_.end(), 0.0f);
  std::fill(aim_.begin(), aim_.end(), 0.0f);
  for (int p = 0; p < P_; ++p) {
    const int xi = (pos_ + p) % P_;
    const float* xr = &xre_[(size_t)xi * bins_];
    const float* xm = &xim_[(size_t)xi * bins_];
    const float* hr = &hre_[(size_t)p * bins_];
    const float* hm = &him_[(size_t)p * bins_];
    float* ar = are_.data();
    float* am = aim_.data();
    for (int k = 0; k < bins_; ++k) {
      ar[k] += xr[k] * hr[k] - xm[k] * hm[k];
      am[k] += xr[k] * hm[k] + xm[k] * hr[k];
    }
  }
  // Hermitian symmetry -> full spectrum
  for (int k = 0; k < bins_; ++k) {
    wre_[k] = are_[k];
    wim_[k] = aim_[k];
  }
  for (int k = bins_; k < N_; ++k) {
    wre_[k] = are_[N_ - k];
    wim_[k] = -aim_[N_ - k];
  }
  fft_.inverse(wre_.data(), wim_.data());
  const float norm = 1.0f / (float)N_;
  for (int i = 0; i < B_; ++i) out[i] = wre_[B_ + i] * norm;
}

}  // namespace fx
