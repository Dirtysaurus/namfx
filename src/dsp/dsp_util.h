// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace fx {

constexpr float kPi = 3.14159265358979f;

// Denormal numbers (decaying reverb/delay tails) are very slow on many CPUs.
// Call once at the start of every thread that processes audio.
inline void enableFlushToZero() {
#if defined(__aarch64__)
  uint64_t fpcr;
  __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
  __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));  // FZ
#elif defined(__SSE__)
  _mm_setcsr(_mm_getcsr() | 0x8040);  // FTZ + DAZ
#endif
}

inline float dbToLin(float db) { return std::pow(10.0f, db * 0.05f); }
inline float linToDb(float v) { return v > 1e-9f ? 20.0f * std::log10(v) : -180.0f; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// One-pole parameter smoothing (avoids zipper noise).
struct Smoother {
  float y = 0.0f, a = 0.0f;
  void setup(float sr, float ms) { a = std::exp(-1.0f / (0.001f * ms * sr)); }
  void reset(float v) { y = v; }
  inline float next(float target) { y = target + a * (y - target); return y; }
};

// RBJ biquad, transposed direct form II.
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1[2] = {0, 0}, z2[2] = {0, 0};
  enum Type { LP, HP, BP, PEAK, LSHELF, HSHELF, AP };
  void set(Type t, float sr, float f, float q, float gainDb = 0.0f) {
    f = clampf(f, 10.0f, sr * 0.49f);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w = 2.0f * kPi * f / sr, cw = std::cos(w), sw = std::sin(w);
    const float alpha = sw / (2.0f * q);
    float B0, B1, B2, A0, A1, A2;
    switch (t) {
      case LP: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
      case HP: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
      case BP: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
      case AP: B0 = 1 - alpha; B1 = -2 * cw; B2 = 1 + alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
      case PEAK: B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
      case LSHELF: {
        const float s = 2 * std::sqrt(A) * alpha;
        B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
        A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s; break;
      }
      default: {
        const float s = 2 * std::sqrt(A) * alpha;
        B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
        A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s; break;
      }
    }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
  }
  inline float tick(float x, int ch = 0) {
    const float y = b0 * x + z1[ch];
    z1[ch] = b1 * x - a1 * y + z2[ch];
    z2[ch] = b2 * x - a2 * y;
    return y;
  }
  void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

// Delay line with linear interpolation.
struct DelayLine {
  std::vector<float> buf;
  int w = 0, mask = 0;
  void init(int maxSamples) {
    int n = 1;
    while (n < maxSamples + 4) n <<= 1;
    buf.assign(n, 0.0f);
    mask = n - 1;
    w = 0;
  }
  inline void push(float x) { buf[w] = x; w = (w + 1) & mask; }
  inline float read(float delay) const {
    const float rp = (float)w - 1.0f - delay;
    int i = (int)std::floor(rp);
    const float fr = rp - (float)i;
    const float a = buf[i & mask], b = buf[(i + 1) & mask];
    return a + (b - a) * fr;
  }
  inline float readInt(int delay) const { return buf[(w - 1 - delay) & mask]; }
  void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
};

// Envelope (peak follower) with attack/release.
struct Envelope {
  float env = 0, att = 0, rel = 0;
  void setup(float sr, float attMs, float relMs) {
    att = std::exp(-1.0f / (0.001f * std::max(attMs, 0.01f) * sr));
    rel = std::exp(-1.0f / (0.001f * std::max(relMs, 0.01f) * sr));
  }
  inline float tick(float x) {
    x = std::fabs(x);
    const float c = x > env ? att : rel;
    env = x + c * (env - x);
    return env;
  }
};

struct Lfo {
  float phase = 0, inc = 0;
  void setRate(float sr, float hz) { inc = hz / sr; }
  inline float sine() { const float v = std::sin(2 * kPi * phase); phase += inc; if (phase >= 1) phase -= 1; return v; }
  inline float tri() { const float v = phase < 0.5f ? 4 * phase - 1 : 3 - 4 * phase; phase += inc; if (phase >= 1) phase -= 1; return v; }
  inline float square() { const float v = phase < 0.5f ? 1.0f : -1.0f; phase += inc; if (phase >= 1) phase -= 1; return v; }
};

// ---- cheap approximations for per-sample use on the Cortex-A35

// sin(2*pi*ph) for any ph (wrapped), max error ~0.001
inline float fastSin2pi(float ph) {
  ph -= std::floor(ph);
  float x = ph < 0.5f ? ph : ph - 1.0f;  // [-0.5, 0.5)
  float y = 8.0f * x - 16.0f * x * std::fabs(x);
  return 0.225f * (y * std::fabs(y) - y) + y;
}
// triangle -1..1 for any ph
inline float fastTri(float ph) {
  ph -= std::floor(ph);
  return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
}
// tanh, error < 0.003, exact limits
inline float fastTanh(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// one-pole low/high pass
struct OnePole {
  float a = 0, z[2] = {0, 0};
  void setLP(float sr, float hz) { a = std::exp(-2.0f * kPi * clampf(hz, 1.0f, sr * 0.49f) / sr); }
  inline float lp(float x, int c = 0) { z[c] = x + a * (z[c] - x); return z[c]; }
  inline float hp(float x, int c = 0) { return x - lp(x, c); }
  void clear() { z[0] = z[1] = 0; }
};

// Schroeder all-pass with a fixed delay
struct Allpass {
  std::vector<float> buf;
  int i = 0;
  void init(int len) { buf.assign(std::max(1, len), 0.0f); i = 0; }
  inline float tick(float x, float g) {
    const float d = buf[i];
    const float v = x + g * d;
    buf[i] = v;
    if (++i >= (int)buf.size()) i = 0;
    return d - g * v;
  }
  void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
};

// fast white noise -1..1
struct Rng {
  uint32_t s = 0x12345678u;
  inline float next() { s = s * 1664525u + 1013904223u; return (float)(int32_t)s * (1.0f / 2147483648.0f); }
};

// Lock-free hand-over of objects between the control and audio threads.
// Control thread: set() publishes a new object, collect() frees the old one.
// Audio thread: acquire() at block start takes over the pending object.
template <typename T>
struct Swap {
  std::atomic<T*> pending{nullptr};
  std::atomic<T*> retired{nullptr};
  T* active = nullptr;
  ~Swap() { delete pending.load(); delete retired.load(); delete active; }
  void set(T* p) {  // control thread
    collect();
    T* old = pending.exchange(p);
    delete old;  // never became active
  }
  void collect() {  // control thread
    T* r = retired.exchange(nullptr);
    delete r;
  }
  T* acquire() {  // audio thread
    if (pending.load(std::memory_order_relaxed) && retired.load(std::memory_order_acquire) == nullptr) {
      T* p = pending.exchange(nullptr, std::memory_order_acq_rel);
      if (p) {
        retired.store(active, std::memory_order_release);
        active = p;
      }
    }
    return active;
  }
};

}  // namespace fx
