// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/engines.h"
#include <cmath>
#include <cstring>
#include <regex>

namespace fx {

namespace {

// ============================================================ delay

enum DelayP {
  D_TIME, D_FB, D_MIX, D_DRY, D_LP, D_HP, D_SAT, D_WOWR, D_WOWD, D_FLR, D_FLD, D_MODR, D_MODD, D_MODS,
  D_HEADS, D_R2, D_R3, D_R4, D_L1, D_L2, D_L3, D_L4, D_DIFF, D_DUCK, D_DUCKREL, D_BITS, D_SRATE, D_NOISE,
  D_PP, D_SPREAD, D_SMOOTH, D_WIDTH, D_COUNT
};

class DelayEngine : public FxEngine {
 public:
  void prepare(float sr, int) override {
    sr_ = sr;
    maxLen_ = (int)(2.7f * sr);
    for (int c = 0; c < 2; ++c) {
      d_[c].init(maxLen_ + 8);
      ap_[c].init((int)(0.0071f * sr) + c * 13);
      ts_[c].setup(sr, 120.0f);
      ts_[c].y = -1;
      hold_[c] = 0;
      holdPh_[c] = 0;
    }
    flutN_.setLP(sr, 12.0f);
    lastLp_ = lastHp_ = lastSmooth_ = lastDuckRel_ = -1;
  }
  void reset() override {
    for (int c = 0; c < 2; ++c) { d_[c].clear(); ap_[c].clear(); }
    lp_.clear();
    hp_.clear();
  }

  void process(const float* p, float* L, float* R, int n, bool st) override {
    const int nc = st ? 2 : 1;
    if (p[D_LP] != lastLp_) { lastLp_ = p[D_LP]; lp_.set(Biquad::LP, sr_, lastLp_, 0.707f); }
    if (p[D_HP] != lastHp_) { lastHp_ = p[D_HP]; hp_.setLP(sr_, lastHp_); }
    if (p[D_SMOOTH] != lastSmooth_) { lastSmooth_ = p[D_SMOOTH]; for (auto& s : ts_) s.setup(sr_, lastSmooth_); }
    if (p[D_DUCKREL] != lastDuckRel_) { lastDuckRel_ = p[D_DUCKREL]; duckEnv_.setup(sr_, 4.0f, lastDuckRel_); }
    const bool lpOn = p[D_LP] < 19000.0f, hpOn = p[D_HP] > 25.0f;
    const int heads = std::clamp((int)std::lround(p[D_HEADS]), 1, 4);
    const float ratio[4] = {1.0f, p[D_R2], p[D_R3], p[D_R4]};
    const float level[4] = {p[D_L1], p[D_L2], p[D_L3], p[D_L4]};
    const float fb = p[D_FB], mix = p[D_MIX], dry = p[D_DRY], sat = p[D_SAT], diff = p[D_DIFF];
    const float duck = p[D_DUCK], noise = p[D_NOISE] * 0.002f;
    const int bits = (int)std::lround(p[D_BITS]);
    const float q = bits > 0 ? (float)(1 << std::clamp(bits - 1, 1, 15)) : 0.0f;
    const float holdInc = p[D_SRATE] > 0 ? p[D_SRATE] / sr_ : 0.0f;
    const bool pp = p[D_PP] > 0.5f && st;
    const float ms = 0.001f * sr_;
    const float wInc = p[D_WOWR] / sr_, fInc = p[D_FLR] / sr_, mInc = p[D_MODR] / sr_;
    const float wowD = p[D_WOWD], flD = p[D_FLD], modD = p[D_MODD];
    const bool modTri = p[D_MODS] > 0.5f;
    const float satG = 1.0f + 3.0f * sat;
    const float width = p[D_WIDTH];
    const float spread = p[D_SPREAD];
    const float maxD = (float)maxLen_;
    if (ts_[0].y < 0) for (auto& s : ts_) s.reset(p[D_TIME]);

    for (int i = 0; i < n; ++i) {
      float m = 0.0f;
      if (wowD > 0) m += wowD * fastSin2pi(phW_);
      if (flD > 0) m += flD * (0.6f * fastSin2pi(phF_) + 2.0f * flutN_.lp(rng_.next()));
      if (modD > 0) m += modD * (modTri ? fastTri(phM_) : fastSin2pi(phM_));
      phW_ += wInc; phF_ += fInc; phM_ += mInc;
      if (phW_ >= 1) phW_ -= 1;
      if (phF_ >= 1) phF_ -= 1;
      if (phM_ >= 1) phM_ -= 1;
      const float xl = L[i], xr = st ? R[i] : L[i];
      float g = 1.0f;
      if (duck > 0) g = 1.0f - duck * std::min(1.0f, duckEnv_.tick(std::max(std::fabs(xl), std::fabs(xr))) * 6.0f);
      float wet[2] = {0, 0}, loop[2] = {0, 0};
      for (int c = 0; c < nc; ++c) {
        const float t = ts_[c].next(p[D_TIME]);
        const float off = (c == 1 ? spread : 0.0f) + m;
        float w = 0.0f;
        for (int k = 0; k < heads; ++k) {
          if (level[k] <= 0.0f) continue;
          const float ds = clampf((t * ratio[k] + off) * ms, 1.0f, maxD);
          w += level[k] * d_[c].read(ds);
        }
        if (holdInc > 0) {
          holdPh_[c] += holdInc;
          if (holdPh_[c] >= 1.0f) { holdPh_[c] -= 1.0f; hold_[c] = w; }
          w = hold_[c];
        }
        if (q > 0) w = std::round(w * q) / q;
        if (hpOn) w = hp_.hp(w, c);
        if (lpOn) w = lp_.tick(w, c);
        if (sat > 0) w = fastTanh(satG * w) / satG;
        float lo = diff > 0 ? ap_[c].tick(w, diff) : w;
        if (noise > 0) lo += noise * rng_.next();
        wet[c] = w;
        loop[c] = lo;
      }
      auto safe = [](float v) { return clampf(v, -4.0f, 4.0f); };
      if (pp) {
        d_[0].push(safe(0.5f * (xl + xr) + fb * loop[1]));
        d_[1].push(safe(fb * loop[0]));
      } else {
        d_[0].push(safe(xl + fb * loop[0]));
        if (st) d_[1].push(safe(xr + fb * loop[1]));
      }
      if (st) {
        const float mid = 0.5f * (wet[0] + wet[1]), side = 0.5f * (wet[0] - wet[1]) * width;
        L[i] = dry * xl + mix * g * (mid + side);
        R[i] = dry * xr + mix * g * (mid - side);
      } else {
        L[i] = dry * xl + mix * g * wet[0];
      }
    }
  }

 private:
  float sr_ = 48000;
  int maxLen_ = 0;
  DelayLine d_[2];
  Allpass ap_[2];
  Smoother ts_[2];
  Biquad lp_;
  OnePole hp_, flutN_;
  Envelope duckEnv_;
  Rng rng_;
  float phW_ = 0, phF_ = 0.3f, phM_ = 0;
  float hold_[2], holdPh_[2];
  float lastLp_ = -1, lastHp_ = -1, lastSmooth_ = -1, lastDuckRel_ = -1;
};

// ============================================================ modulation (chorus / flanger / vibrato)

enum ModP { M_VOICES, M_BASE, M_DEPTH, M_RATE, M_SHAPE, M_SPREAD, M_FB, M_MIX, M_DRY, M_LP, M_HP, M_SAT, M_DRIFT, M_COUNT };

class ModEngine : public FxEngine {
 public:
  void prepare(float sr, int) override {
    sr_ = sr;
    for (auto& d : d_) d.init((int)(0.065f * sr));
    for (auto& w : fbz_) w = 0;
    driftN_.setLP(sr, 0.3f);
    lastLp_ = lastHp_ = -1;
  }
  void reset() override { for (auto& d : d_) d.clear(); lp_.clear(); hp_.clear(); }
  void process(const float* p, float* L, float* R, int n, bool st) override {
    const int nc = st ? 2 : 1;
    if (p[M_LP] != lastLp_) { lastLp_ = p[M_LP]; lp_.set(Biquad::LP, sr_, lastLp_, 0.707f); }
    if (p[M_HP] != lastHp_) { lastHp_ = p[M_HP]; hp_.setLP(sr_, lastHp_); }
    const bool lpOn = p[M_LP] < 19000.0f, hpOn = p[M_HP] > 25.0f;
    const int voices = std::clamp((int)std::lround(p[M_VOICES]), 1, 3);
    const float inc = p[M_RATE] / sr_, base = p[M_BASE], depth = p[M_DEPTH], fb = p[M_FB];
    const float mix = p[M_MIX], dry = p[M_DRY], sat = p[M_SAT], drift = p[M_DRIFT], spread = p[M_SPREAD];
    const bool tri = p[M_SHAPE] > 0.5f;
    const float ms = 0.001f * sr_, maxD = 0.06f * sr_;
    const float satG = 1.0f + 3.0f * sat;
    const float vnorm = 1.0f / std::sqrt((float)voices);
    for (int i = 0; i < n; ++i) {
      const float dr = drift > 0 ? drift * 3.0f * driftN_.lp(rng_.next()) : 0.0f;
      for (int c = 0; c < nc; ++c) {
        float* x = c ? R : L;
        float acc = 0;
        for (int v = 0; v < voices; ++v) {
          const float ph = phase_ + (float)v / voices + (c ? spread : 0.0f);
          const float l = tri ? fastTri(ph) : fastSin2pi(ph);
          const float dm = base + depth * (0.5f + 0.5f * l) + (v ? dr * (v == 1 ? 1.0f : -1.0f) : 0.0f);
          acc += d_[c].read(clampf(dm * ms, 1.0f, maxD));
        }
        float w = acc * vnorm;
        d_[c].push(clampf(x[i] + fb * fbz_[c], -4.0f, 4.0f));
        fbz_[c] = acc / voices;
        if (hpOn) w = hp_.hp(w, c);
        if (lpOn) w = lp_.tick(w, c);
        if (sat > 0) w = fastTanh(satG * w) / satG;
        x[i] = dry * x[i] + mix * w;
      }
      phase_ += inc;
      if (phase_ >= 1) phase_ -= 1;
    }
  }

 private:
  float sr_ = 48000, phase_ = 0;
  DelayLine d_[2];
  float fbz_[2] = {0, 0};
  Biquad lp_;
  OnePole hp_, driftN_;
  Rng rng_;
  float lastLp_ = -1, lastHp_ = -1;
};

// ============================================================ phaser / vibe

enum PhP { P_STAGES, P_FMIN, P_FMAX, P_RATE, P_SHAPE, P_FB, P_MIX, P_DRY, P_SPREAD, P_STAGGER, P_COUNT };

class PhaserEngine : public FxEngine {
 public:
  void prepare(float sr, int) override { sr_ = sr; std::memset(z_, 0, sizeof(z_)); fbz_[0] = fbz_[1] = 0; lag_[0] = lag_[1] = 0; k_ = 0; }
  void reset() override { std::memset(z_, 0, sizeof(z_)); }
  void process(const float* p, float* L, float* R, int n, bool st) override {
    const int nc = st ? 2 : 1;
    const int stages = std::clamp((int)std::lround(p[P_STAGES]), 1, 12);
    const float fmin = p[P_FMIN], fmax = std::max(p[P_FMAX], fmin + 1.0f), fb = p[P_FB], mix = p[P_MIX], dry = p[P_DRY];
    const int shape = (int)std::lround(p[P_SHAPE]);
    const float inc = p[P_RATE] / sr_;
    const float lagUp = std::exp(-1.0f / (0.004f * sr_ / 16)), lagDn = std::exp(-1.0f / (0.045f * sr_ / 16));
    for (int i = 0; i < n; ++i) {
      if ((k_++ & 15) == 0) {  // coefficients at 1/16 of the sample rate
        for (int c = 0; c < nc; ++c) {
          const float ph = phase_ + (c ? p[P_SPREAD] : 0.0f);
          float m;
          if (shape == 1) m = 0.5f + 0.5f * fastTri(ph);
          else if (shape == 2) {  // vibe: lamp + photocell, fast on, slow off
            const float s = 0.5f + 0.5f * fastSin2pi(ph);
            const float t = s * s;
            lag_[c] = t + (t > lag_[c] ? lagUp : lagDn) * (lag_[c] - t);
            m = lag_[c];
          } else m = 0.5f + 0.5f * fastSin2pi(ph);
          const float f = fmin * std::pow(fmax / fmin, m);
          for (int s = 0; s < stages; ++s) {
            const float fs = std::min(f * (1.0f + p[P_STAGGER] * s * 0.55f), sr_ * 0.45f);
            const float t = std::tan(kPi * fs / sr_);
            a_[c][s] = (t - 1.0f) / (t + 1.0f);
          }
        }
      }
      for (int c = 0; c < nc; ++c) {
        float* x = c ? R : L;
        float y = x[i] + fbz_[c] * fb;
        for (int s = 0; s < stages; ++s) {
          const float o = a_[c][s] * y + z_[c][s];
          z_[c][s] = y - a_[c][s] * o;
          y = o;
        }
        fbz_[c] = clampf(y, -4.0f, 4.0f);
        x[i] = dry * x[i] + mix * y;
      }
      phase_ += inc;
      if (phase_ >= 1) phase_ -= 1;
    }
  }

 private:
  float sr_ = 48000, phase_ = 0;
  float a_[2][12] = {}, z_[2][12] = {}, fbz_[2] = {0, 0}, lag_[2] = {0, 0};
  unsigned k_ = 0;
};

// ============================================================ reverb (Freeverb topology, extended)

enum RvP { V_ROOM, V_DAMP, V_PRE, V_WIDTH, V_MIX, V_DRY, V_LOWCUT, V_HIGHCUT, V_SIZE, V_DIFF, V_COUNT };

class ReverbEngine : public FxEngine {
 public:
  void prepare(float sr, int) override {
    sr_ = sr;
    static const int combT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int apT[4] = {556, 441, 341, 225};
    const float k = sr / 44100.0f;
    for (int c = 0; c < 2; ++c) {
      for (int i = 0; i < 8; ++i) {
        base_[c][i] = (combT[i] + c * 23) * k;
        comb_[c][i].assign((size_t)(base_[c][i] * 1.65f) + 2, 0.0f);
        ci_[c][i] = 0;
        cf_[c][i] = 0;
        len_[c][i] = (int)base_[c][i];
      }
      for (int i = 0; i < 4; ++i) ap_[c][i].init((int)((apT[i] + c * 23) * k));
    }
    pre_.init((int)(0.26f * sr));
    lastSize_ = lastLc_ = lastHc_ = -1;
  }
  void reset() override {
    for (auto& ch : comb_) for (auto& b : ch) std::fill(b.begin(), b.end(), 0.0f);
    for (auto& ch : ap_) for (auto& a : ch) a.clear();
    pre_.clear();
  }
  void process(const float* p, float* L, float* R, int n, bool st) override {
    if (p[V_SIZE] != lastSize_) {
      lastSize_ = p[V_SIZE];
      for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 8; ++i) {
          len_[c][i] = std::clamp((int)(base_[c][i] * lastSize_), 16, (int)comb_[c][i].size() - 1);
          if (ci_[c][i] >= len_[c][i]) ci_[c][i] = 0;
        }
    }
    if (p[V_LOWCUT] != lastLc_) { lastLc_ = p[V_LOWCUT]; lc_.setLP(sr_, lastLc_); }
    if (p[V_HIGHCUT] != lastHc_) { lastHc_ = p[V_HIGHCUT]; hc_.setLP(sr_, lastHc_); }
    const bool lcOn = p[V_LOWCUT] > 25.0f, hcOn = p[V_HIGHCUT] < 19000.0f;
    const float room = p[V_ROOM], damp = p[V_DAMP], dry = p[V_DRY], g = p[V_DIFF];
    // long decays pile up energy: attenuate the wet signal above the classic Freeverb setting
    const float mix = p[V_MIX] * std::min(1.0f, std::sqrt(std::max(0.01f, 1.0f - room) / 0.16f));
    const float pd = p[V_PRE] * 0.001f * sr_;
    const float w1 = 0.5f + 0.5f * p[V_WIDTH], w2 = 0.5f - 0.5f * p[V_WIDTH];
    for (int i = 0; i < n; ++i) {
      const float xl = L[i], xr = st ? R[i] : L[i];
      float in = (xl + xr) * 0.015f;
      if (lcOn) in = lc_.hp(in);
      if (hcOn) in = hc_.lp(in);
      pre_.push(in);
      in = pd >= 1.0f ? pre_.read(pd) : in;
      float out[2];
      for (int c = 0; c < 2; ++c) {
        float acc = 0;
        for (int k = 0; k < 8; ++k) {
          float* b = comb_[c][k].data();
          int& idx = ci_[c][k];
          const float y = b[idx];
          cf_[c][k] = y * (1 - damp) + cf_[c][k] * damp;
          b[idx] = in + cf_[c][k] * room;
          if (++idx >= len_[c][k]) idx = 0;
          acc += y;
        }
        for (int k = 0; k < 4; ++k) {  // Freeverb-style all-pass
          Allpass& a = ap_[c][k];
          const float bo = a.buf[a.i];
          const float y = -acc + bo;
          a.buf[a.i] = acc + bo * g;
          if (++a.i >= (int)a.buf.size()) a.i = 0;
          acc = y;
        }
        out[c] = acc * 3.0f;
      }
      const float ol = out[0] * w1 + out[1] * w2, orr = out[1] * w1 + out[0] * w2;
      if (st) {
        L[i] = dry * xl + mix * ol;
        R[i] = dry * xr + mix * orr;
      } else {
        L[i] = dry * xl + mix * 0.5f * (ol + orr);
      }
    }
  }

 private:
  float sr_ = 48000;
  std::vector<float> comb_[2][8];
  float base_[2][8] = {}, cf_[2][8] = {};
  int ci_[2][8] = {}, len_[2][8] = {};
  Allpass ap_[2][4];
  DelayLine pre_;
  OnePole lc_, hc_;
  float lastSize_ = -1, lastLc_ = -1, lastHc_ = -1;
};

// ============================================================ drive

enum DrP { R_GAIN, R_PREHP, R_PRELP, R_MIDF, R_MIDG, R_CLIP, R_BIAS, R_CLEAN, R_TONE, R_POSTHP, R_LEVEL, R_MIDQ, R_COUNT };

class DriveEngine : public FxEngine {
 public:
  void prepare(float sr, int) override { sr_ = sr; dc_.setLP(sr, 18.0f); last_[0] = -1; }
  void reset() override { pre_hp_.clear(); pre_lp_.clear(); mid_.clear(); tone_.clear(); post_hp_.clear(); dc_.clear(); }
  static inline float clip(int mode, float v) {
    switch (mode) {
      case 1: return clampf(v, -0.8f, 0.8f) * 1.25f;                                // hard (op-amp + diodes to ground)
      case 2: return v > 0 ? fastTanh(v) : 0.75f * fastTanh(v * 1.33f);             // asymmetric diodes
      case 3: return v > 0 ? std::min(v, 0.6f) / 0.6f : std::max(v, -0.9f) / 0.9f;  // fuzz: hard, asymmetric
      case 4: return v > 0 ? fastTanh(v) : v / (1.0f - 0.6f * v) * 0.8f;            // tube-like (soft, asymmetric)
      default: return fastTanh(v);                                                  // soft
    }
  }
  void process(const float* p, float* L, float* R, int n, bool st) override {
    bool ch = false;
    for (int k : {R_PREHP, R_PRELP, R_MIDF, R_MIDG, R_TONE, R_POSTHP, R_MIDQ})
      if (p[k] != last_[k]) { last_[k] = p[k]; ch = true; }
    if (ch) {
      pre_hp_.setLP(sr_, p[R_PREHP]);  // first order, like the coupling caps in pedal circuits
      pre_lp_.set(Biquad::LP, sr_, p[R_PRELP], 0.707f);
      mid_.set(Biquad::PEAK, sr_, p[R_MIDF], p[R_MIDQ], p[R_MIDG]);
      tone_.set(Biquad::LP, sr_, p[R_TONE], 0.707f);
      post_hp_.set(Biquad::HP, sr_, p[R_POSTHP], 0.707f);
    }
    const bool hpOn = p[R_PREHP] > 25, lpOn = p[R_PRELP] < 19000, midOn = std::fabs(p[R_MIDG]) > 0.05f;
    const bool toneOn = p[R_TONE] < 19000, postOn = p[R_POSTHP] > 25;
    const float g = dbToLin(p[R_GAIN]), level = dbToLin(p[R_LEVEL]), clean = p[R_CLEAN], bias = p[R_BIAS];
    const int mode = std::clamp((int)std::lround(p[R_CLIP]), 0, 4);
    const float b0 = clip(mode, bias);
    for (int i = 0; i < n; ++i) {
      const float x = st ? 0.5f * (L[i] + R[i]) : L[i];
      float v = x;
      if (hpOn) v = pre_hp_.hp(v);
      if (midOn) v = mid_.tick(v);
      if (lpOn) v = pre_lp_.tick(v);
      float y = clip(mode, v * g + bias) - b0;
      y = dc_.hp(y);
      y += clean * x;
      if (toneOn) y = tone_.tick(y);
      if (postOn) y = post_hp_.tick(y);
      L[i] = y * level;
      if (st) R[i] = L[i];
    }
  }

 private:
  float sr_ = 48000;
  Biquad pre_lp_, mid_, tone_, post_hp_;
  OnePole pre_hp_, dc_;
  float last_[R_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

// ============================================================ compressor

enum CmP { C_THR, C_RATIO, C_ATT, C_REL, C_KNEE, C_MAKEUP, C_MIX, C_DETECT, C_OPTO, C_SCHP, C_COUNT };

class CompEngine : public FxEngine {
 public:
  void prepare(float sr, int) override { sr_ = sr; gr_ = 0; rms_ = 0; lastHp_ = -1; }
  void process(const float* p, float* L, float* R, int n, bool st) override {
    if (p[C_SCHP] != lastHp_) { lastHp_ = p[C_SCHP]; sc_.setLP(sr_, lastHp_); }
    const bool hpOn = p[C_SCHP] > 25;
    const float thr = p[C_THR], ratio = std::max(1.0f, p[C_RATIO]), knee = std::max(0.01f, p[C_KNEE]);
    const float att = std::exp(-1.0f / (0.001f * std::max(0.05f, p[C_ATT]) * sr_));
    const float relMs = std::max(1.0f, p[C_REL]);
    const float makeup = p[C_MAKEUP], mix = p[C_MIX], opto = p[C_OPTO];
    const bool rmsDet = p[C_DETECT] > 0.5f;
    const float rmsA = std::exp(-1.0f / (0.010f * sr_));
    const float slope = 1.0f - 1.0f / ratio;
    for (int i = 0; i < n; ++i) {
      float d = st ? std::max(std::fabs(L[i]), std::fabs(R[i])) : std::fabs(L[i]);
      if (hpOn) d = std::fabs(sc_.hp(st ? 0.5f * (L[i] + R[i]) : L[i]));
      if (rmsDet) { rms_ = d * d + rmsA * (rms_ - d * d); d = std::sqrt(rms_); }
      const float e = linToDb(d);
      const float over = e - thr;
      float target = 0;
      if (over > knee * 0.5f) target = over * slope;
      else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; target = slope * k * k / (2.0f * knee); }
      float c = att;
      if (target < gr_) {
        const float rms = relMs * (1.0f + opto * gr_ / 4.0f);  // opto: slower release after heavy reduction
        c = std::exp(-1.0f / (0.001f * rms * sr_));
      }
      gr_ = target + c * (gr_ - target);
      const float gl = dbToLin(makeup - gr_);
      const float g = 1.0f - mix + mix * gl;
      L[i] *= g;
      if (st) R[i] *= g;
    }
  }

 private:
  float sr_ = 48000, gr_ = 0, rms_ = 0, lastHp_ = -1;
  OnePole sc_;
};

template <class T>
std::unique_ptr<FxEngine> mk() { return std::make_unique<T>(); }

}  // namespace

// ============================================================ registry

const std::vector<EngineInfo>& engines() {
  static const std::vector<EngineInfo> list = {
      {"delay", "Delay line with up to 4 heads, filters, saturation, wow/flutter, modulation, diffusion, ducking, lo-fi and ping-pong", true,
       {{"time", 380, 1, 2500, "time of head 1 [ms]"},
        {"feedback", 0.35f, 0, 1.1f, "feedback amount (0-1.1)"},
        {"mix", 0.3f, 0, 2, "wet level"},
        {"dry", 1, 0, 1, "dry level"},
        {"lp", 20000, 200, 20000, "low-pass in the loop [Hz] (>=19000 off)"},
        {"hp", 20, 20, 2000, "high-pass in the loop [Hz] (<=25 off)"},
        {"sat", 0, 0, 1, "saturation in the loop"},
        {"wow_rate", 0.6f, 0.05f, 5, "wow rate [Hz]"},
        {"wow_depth", 0, 0, 5, "wow depth [ms]"},
        {"flutter_rate", 6, 1, 20, "flutter rate [Hz]"},
        {"flutter_depth", 0, 0, 1, "flutter depth [ms] (sine + random)"},
        {"mod_rate", 0.8f, 0.05f, 10, "modulation rate [Hz]"},
        {"mod_depth", 0, 0, 10, "modulation depth [ms]"},
        {"mod_shape", 0, 0, 1, "0 sine, 1 triangle"},
        {"heads", 1, 1, 4, "number of playback heads"},
        {"r2", 0.5f, 0.05f, 1, "head 2 time as a fraction of head 1"},
        {"r3", 0.75f, 0.05f, 1, "head 3 time fraction"},
        {"r4", 0.25f, 0.05f, 1, "head 4 time fraction"},
        {"l1", 1, 0, 1, "head 1 level"},
        {"l2", 0, 0, 1, "head 2 level"},
        {"l3", 0, 0, 1, "head 3 level"},
        {"l4", 0, 0, 1, "head 4 level"},
        {"diffuse", 0, 0, 0.8f, "all-pass diffusion in the loop"},
        {"duck", 0, 0, 1, "ducking amount (wet drops while playing)"},
        {"duck_release", 300, 20, 2000, "ducking release [ms]"},
        {"bits", 0, 0, 16, "bit depth reduction (0 off)"},
        {"srate", 0, 0, 48000, "sample-and-hold rate [Hz] (0 off)"},
        {"noise", 0, 0, 1, "hiss in the loop"},
        {"pingpong", 0, 0, 1, "ping-pong (stereo)"},
        {"spread", 0, 0, 50, "right channel time offset [ms]"},
        {"smooth", 120, 1, 2000, "time change glide [ms]"},
        {"width", 1, 0, 1, "stereo width of the repeats"}},
       mk<DelayEngine>},
      {"mod", "Modulated short delay: chorus, flanger, vibrato, ensemble", true,
       {{"voices", 1, 1, 3, "number of voices"},
        {"base", 8, 0.1f, 40, "base delay [ms]"},
        {"depth", 2, 0, 15, "modulation depth [ms]"},
        {"rate", 0.8f, 0.02f, 15, "LFO rate [Hz]"},
        {"shape", 0, 0, 1, "0 sine, 1 triangle"},
        {"spread", 0.25f, 0, 1, "right channel LFO phase offset (0-1)"},
        {"feedback", 0, -0.95f, 0.95f, "feedback (flanger)"},
        {"mix", 0.5f, 0, 1, "wet level"},
        {"dry", 1, 0, 1, "dry level (0 = vibrato)"},
        {"lp", 20000, 500, 20000, "low-pass on the wet signal [Hz]"},
        {"hp", 20, 20, 2000, "high-pass on the wet signal [Hz]"},
        {"sat", 0, 0, 1, "saturation of the wet signal (BBD)"},
        {"drift", 0, 0, 1, "random detune between voices"}},
       mk<ModEngine>},
      {"phaser", "All-pass phaser, 1-12 stages, vibe mode", true,
       {{"stages", 4, 1, 12, "number of all-pass stages"},
        {"fmin", 200, 20, 2000, "sweep start [Hz]"},
        {"fmax", 2000, 100, 10000, "sweep end [Hz]"},
        {"rate", 0.5f, 0.02f, 15, "LFO rate [Hz]"},
        {"shape", 0, 0, 2, "0 sine, 1 triangle, 2 vibe (lamp + photocell)"},
        {"feedback", 0.4f, -0.95f, 0.95f, "feedback"},
        {"mix", 0.5f, 0, 1, "wet level"},
        {"dry", 1, 0, 1, "dry level"},
        {"spread", 0, 0, 1, "right channel LFO phase offset"},
        {"stagger", 0, 0, 1, "spreads the stage frequencies (vibe)"}},
       mk<PhaserEngine>},
      {"reverb", "Comb/all-pass reverb (Freeverb topology) with size, pre-delay, width and filters", true,
       {{"room", 0.84f, 0, 0.99f, "decay (comb feedback)"},
        {"damp", 0.3f, 0, 1, "high-frequency damping"},
        {"predelay", 20, 0, 250, "pre-delay [ms]"},
        {"width", 1, 0, 1, "stereo width"},
        {"mix", 0.2f, 0, 2, "wet level"},
        {"dry", 1, 0, 1, "dry level"},
        {"lowcut", 20, 20, 1000, "input low cut [Hz]"},
        {"highcut", 20000, 1000, 20000, "input high cut [Hz]"},
        {"size", 1, 0.4f, 1.6f, "room size (comb length scale)"},
        {"diffusion", 0.5f, 0.1f, 0.75f, "all-pass diffusion"}},
       mk<ReverbEngine>},
      {"drive", "Overdrive / distortion / fuzz: pre-filters, mid boost, clipping, clean blend, tone", false,
       {{"gain", 20, 0, 60, "drive gain [dB]"},
        {"pre_hp", 20, 20, 2000, "high-pass before clipping [Hz]"},
        {"pre_lp", 20000, 500, 20000, "low-pass before clipping [Hz]"},
        {"mid_freq", 720, 100, 5000, "mid boost frequency [Hz]"},
        {"mid_gain", 0, -12, 18, "mid boost [dB]"},
        {"clip", 0, 0, 4, "0 soft, 1 hard, 2 asymmetric diodes, 3 fuzz, 4 tube"},
        {"bias", 0, -0.5f, 0.5f, "asymmetry"},
        {"clean", 0, 0, 1, "clean blend"},
        {"tone", 8000, 300, 20000, "output low-pass [Hz]"},
        {"post_hp", 20, 20, 1000, "output high-pass [Hz]"},
        {"level", 0, -30, 12, "output level [dB]"},
        {"mid_q", 0.7f, 0.3f, 4, "mid boost Q"}},
       mk<DriveEngine>},
      {"comp", "Compressor: peak/RMS detector, soft knee, opto release, side-chain high-pass, parallel mix", false,
       {{"thresh", -20, -60, 0, "threshold [dB]"},
        {"ratio", 4, 1, 30, "ratio"},
        {"attack", 10, 0.1f, 200, "attack [ms]"},
        {"release", 120, 5, 2000, "release [ms]"},
        {"knee", 6, 0, 24, "knee width [dB]"},
        {"makeup", 0, -12, 36, "makeup gain [dB]"},
        {"mix", 1, 0, 1, "compressed share (parallel compression)"},
        {"detect", 0, 0, 1, "0 peak, 1 RMS"},
        {"opto", 0, 0, 1, "program-dependent release"},
        {"sc_hp", 20, 20, 500, "side-chain high-pass [Hz]"}},
       mk<CompEngine>},
  };
  return list;
}

const EngineInfo* findEngine(const std::string& name) {
  for (auto& e : engines())
    if (name == e.name) return &e;
  return nullptr;
}

int engineParamIndex(const EngineInfo& e, const std::string& id) {
  for (size_t i = 0; i < e.params.size(); ++i)
    if (id == e.params[i].id) return (int)i;
  return -1;
}

json enginesJson() {
  json arr = json::array();
  for (auto& e : engines()) {
    json ps = json::array();
    for (auto& p : e.params) ps.push_back({{"id", p.id}, {"def", p.def}, {"min", p.min}, {"max", p.max}, {"doc", p.doc}});
    arr.push_back({{"engine", e.name}, {"desc", e.desc}, {"stereo", e.stereo}, {"params", ps}});
  }
  return arr;
}

// ============================================================ type files

static const char* kCats[] = {"dyn", "drive", "amp", "cab", "eq", "mod", "dly", "rev", "util"};

bool parseTypeDef(const json& j, BlockDef& d, std::string& err) {
  if (!j.is_object()) { err = "type is not an object"; return false; }
  d.type = j.value("type", "");
  static const std::regex idRe("^[a-z][a-z0-9_]{1,40}$");
  if (!std::regex_match(d.type, idRe)) { err = "invalid type id '" + d.type + "' (a-z, 0-9, _)"; return false; }
  const std::string eng = j.value("engine", "");
  const EngineInfo* e = findEngine(eng);
  if (!e) { err = d.type + ": unknown engine '" + eng + "'"; return false; }
  d.name = j.value("name", d.type);
  d.shortName = j.value("short", "");
  if (d.shortName.empty()) {
    for (char c : d.name) if (std::isalnum((unsigned char)c) && d.shortName.size() < 4) d.shortName += (char)std::toupper((unsigned char)c);
  }
  d.cat = j.value("cat", "");
  if (std::find_if(std::begin(kCats), std::end(kCats), [&](const char* c) { return d.cat == c; }) == std::end(kCats)) {
    err = d.type + ": invalid category '" + d.cat + "'";
    return false;
  }
  d.desc = j.value("desc", "");
  d.cpu = j.value("cpu", 2.0f);
  d.stereo = j.value("stereo", e->stereo) && e->stereo;
  d.author = j.value("author", d.author);
  d.license = j.value("license", d.license);
  d.source = "fx";
  if (j.contains("tr") && j["tr"].is_object()) d.tr = j["tr"];

  auto spec = std::make_shared<TypeSpec>();
  spec->engine = e;
  for (auto& p : e->params) spec->base.push_back(p.def);
  if (j.contains("set")) {
    if (!j["set"].is_object()) { err = d.type + ": 'set' must be an object"; return false; }
    for (auto it = j["set"].begin(); it != j["set"].end(); ++it) {
      const int k = engineParamIndex(*e, it.key());
      if (k < 0) { err = d.type + ": unknown internal parameter '" + it.key() + "'"; return false; }
      if (!it.value().is_number()) { err = d.type + ": set." + it.key() + " is not a number"; return false; }
      spec->base[k] = clampf(it.value().get<float>(), e->params[k].min, e->params[k].max);
    }
  }
  const json ps = j.value("params", json::array());
  if (!ps.is_array()) { err = d.type + ": 'params' must be an array"; return false; }
  for (auto& pj : ps) {
    ParamDef p;
    p.id = pj.value("id", "");
    if (p.id.empty()) { err = d.type + ": parameter without id"; return false; }
    p.name = pj.value("name", p.id);
    p.unit = pj.value("unit", "");
    const std::string kind = pj.value("kind", "float");
    p.kind = kind == "enum" ? PKind::Enum : kind == "bool" ? PKind::Bool : PKind::Float;
    if (p.kind == PKind::Enum) {
      for (auto& o : pj.value("options", json::array())) p.options.push_back(o.get<std::string>());
      if (p.options.size() < 2) { err = d.type + "." + p.id + ": enum needs at least 2 options"; return false; }
      p.min = 0;
      p.max = (float)p.options.size() - 1;
    } else if (p.kind == PKind::Bool) {
      p.min = 0;
      p.max = 1;
    } else {
      p.min = pj.value("min", 0.0f);
      p.max = pj.value("max", 1.0f);
      if (!(p.max > p.min)) { err = d.type + "." + p.id + ": max must be > min"; return false; }
    }
    p.def = clampf(pj.value("def", p.min), p.min, p.max);
    p.log = pj.value("log", false) && p.min > 0;
    p.step = pj.value("step", 0.0f);
    std::vector<MapTarget> maps;
    json mj = pj.value("map", json::array());
    if (mj.is_object()) mj = json::array({mj});
    for (auto& m : mj) {
      MapTarget t;
      const std::string to = m.value("to", "");
      t.target = engineParamIndex(*e, to);
      if (t.target < 0) { err = d.type + "." + p.id + ": map to unknown internal parameter '" + to + "'"; return false; }
      const std::string op = m.value("op", "set");
      t.op = op == "mul" ? MapTarget::Mul : op == "add" ? MapTarget::Add : MapTarget::Set;
      if (m.contains("values")) {
        t.mode = MapTarget::Values;
        for (auto& v : m["values"]) t.values.push_back(v.get<float>());
        const size_t need = p.kind == PKind::Enum ? p.options.size() : p.kind == PKind::Bool ? 2 : 0;
        if (need == 0 || t.values.size() != need) { err = d.type + "." + p.id + ": 'values' needs one value per option"; return false; }
      } else if (m.contains("range")) {
        t.mode = MapTarget::Range;
        if (!m["range"].is_array() || m["range"].size() != 2) { err = d.type + "." + p.id + ": 'range' must be [from, to]"; return false; }
        t.a = m["range"][0].get<float>();
        t.b = m["range"][1].get<float>();
        t.logCurve = m.value("curve", "lin") == "log" && t.a > 0 && t.b > 0;
        t.power = m.value("power", 1.0f);
      }
      maps.push_back(t);
    }
    d.params.push_back(p);
    spec->maps.push_back(std::move(maps));
  }
  d.spec = spec;
  return true;
}

// ============================================================ engine block

namespace {

class EngineBlock : public Block {
 public:
  explicit EngineBlock(const BlockDef* d) : Block(d), spec_(d->spec.get()) {
    eng_ = spec_->engine->make();
    vals_ = spec_->base;
    knobs_.assign(d->params.size(), std::nanf(""));
  }
  void prepare(float sr, int mb) override {
    Block::prepare(sr, mb);
    eng_->prepare(sr, mb);
    std::fill(knobs_.begin(), knobs_.end(), std::nanf(""));
  }
  void reset() override { eng_->reset(); }
  int latency() const override { return eng_->latency(); }

 protected:
  void process(float* L, float* R, int n, bool st) override {
    bool changed = false;
    for (size_t i = 0; i < knobs_.size(); ++i) {
      const float v = get((int)i);
      if (!(v == knobs_[i])) { knobs_[i] = v; changed = true; }
    }
    if (changed) recompute();
    eng_->process(vals_.data(), L, R, n, st);
  }

 private:
  float eval(const ParamDef& p, const MapTarget& t, float v) const {
    switch (t.mode) {
      case MapTarget::Values: {
        const int i = std::clamp((int)std::lround(v - p.min), 0, (int)t.values.size() - 1);
        return t.values[i];
      }
      case MapTarget::Range: {
        float x = (p.log && p.min > 0) ? std::log(v / p.min) / std::log(p.max / p.min) : (v - p.min) / (p.max - p.min);
        x = clampf(x, 0.0f, 1.0f);
        if (t.power != 1.0f) x = std::pow(x, t.power);
        return t.logCurve ? t.a * std::pow(t.b / t.a, x) : t.a + (t.b - t.a) * x;
      }
      default: return v;
    }
  }
  void recompute() {
    vals_ = spec_->base;
    for (int op : {MapTarget::Set, MapTarget::Mul, MapTarget::Add})
      for (size_t i = 0; i < spec_->maps.size(); ++i)
        for (auto& t : spec_->maps[i]) {
          if (t.op != op) continue;
          const float v = eval(def->params[i], t, knobs_[i]);
          float& o = vals_[t.target];
          o = op == MapTarget::Set ? v : op == MapTarget::Mul ? o * v : o + v;
        }
    const auto& ep = spec_->engine->params;
    for (size_t k = 0; k < vals_.size(); ++k) vals_[k] = clampf(vals_[k], ep[k].min, ep[k].max);
  }

  const TypeSpec* spec_;
  std::unique_ptr<FxEngine> eng_;
  std::vector<float> vals_, knobs_;
};

}  // namespace

std::unique_ptr<Block> makeEngineBlock(const BlockDef* d) { return std::make_unique<EngineBlock>(d); }

}  // namespace fx
