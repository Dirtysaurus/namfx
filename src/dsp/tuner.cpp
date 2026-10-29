// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/tuner.h"
#include <cmath>

namespace fx {

void Tuner::prepare(float sr) {
  sr_ = sr;
  dsr_ = sr / kDecim;
  aa_.set(Biquad::LP, sr, 1500.0f, 0.707f);
  win_.assign(kWin, 0.0f);
  d_.assign(kWin / 2, 0.0f);
}

void Tuner::push(const float* x, int n) {
  unsigned w = w_.load(std::memory_order_relaxed);
  for (int i = 0; i < n; ++i) {
    const float y = aa_.tick(x[i]);
    if (++phase_ >= kDecim) {
      phase_ = 0;
      ring_[w % kRing] = y;
      ++w;
    }
  }
  w_.store(w, std::memory_order_release);
}

TunerResult Tuner::analyze(float a4) {
  TunerResult r;
  const unsigned w = w_.load(std::memory_order_acquire);
  if (w < (unsigned)kWin) return r;
  float peak = 0;
  for (int i = 0; i < kWin; ++i) {
    win_[i] = ring_[(w - kWin + i) % kRing];
    peak = std::max(peak, std::fabs(win_[i]));
  }
  if (peak < 0.003f) return r;  // silence
  // YIN: cumulative mean normalised difference function
  const int maxTau = kWin / 2;
  const int minTau = (int)(dsr_ / 1400.0f);
  d_[0] = 1.0f;
  float running = 0;
  for (int tau = 1; tau < maxTau; ++tau) {
    float s = 0;
    for (int j = 0; j < maxTau; ++j) {
      const float df = win_[j] - win_[j + tau];
      s += df * df;
    }
    running += s;
    d_[tau] = running > 0 ? s * tau / running : 1.0f;
  }
  int tau = -1;
  for (int t = std::max(2, minTau); t < maxTau - 1; ++t) {
    if (d_[t] < 0.15f) {
      while (t + 1 < maxTau - 1 && d_[t + 1] < d_[t]) ++t;
      tau = t;
      break;
    }
  }
  if (tau < 0) return r;
  // parabolic interpolation
  const float a = d_[tau - 1], b = d_[tau], c = d_[tau + 1];
  const float den = a - 2 * b + c;
  const float t = tau + (std::fabs(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f);
  r.freq = dsr_ / t;
  const float midiF = 69.0f + 12.0f * std::log2(r.freq / a4);
  r.midi = (int)std::lround(midiF);
  r.cents = (midiF - r.midi) * 100.0f;
  static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  r.note = names[((r.midi % 12) + 12) % 12];
  r.octave = r.midi / 12 - 1;
  r.valid = true;
  return r;
}

}  // namespace fx
