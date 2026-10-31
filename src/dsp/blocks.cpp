// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/blocks.h"
#include <cmath>
#include <cstring>
#include "dsp/convolver.h"
#include "io/wav.h"

namespace fx {

// ---------------------------------------------------------------- built-in types
// Everything else (comp, drive, chorus, flanger, phaser, delay, reverb, ...) is an
// effect type in fx/*.json running on an engine (engines.cpp).

std::vector<BlockDef> builtinDefs() {
  auto P = [](const char* id, const char* name, float mn, float mx, float def, const char* unit, PKind k = PKind::Float,
              std::vector<std::string> opts = {}, bool log = false) {
    ParamDef p;
    p.id = id; p.name = name; p.min = mn; p.max = mx; p.def = def; p.unit = unit; p.kind = k; p.options = std::move(opts); p.log = log;
    return p;
  };
  auto D = [](const char* type, const char* name, const char* sh, const char* cat, const char* desc, const char* file, float cpu,
              std::vector<ParamDef> ps) {
    BlockDef d;
    d.type = type; d.name = name; d.shortName = sh; d.cat = cat; d.desc = desc; d.fileKind = file; d.cpu = cpu; d.params = std::move(ps);
    return d;
  };
  return {
    D("gate", "Noise gate", "GATE", "dyn", "Mutes noise between notes", "", 0.6f,
      {P("thresh", "Threshold", -90, -20, -60, "dB"), P("release", "Release", 5, 500, 80, "ms", PKind::Float, {}, true)}),
    D("nam", "NAM amp", "NAM", "amp", "Neural Amp Modeler (A2 Lite recommended)", "nam", 18,
      {P("input", "Input", -24, 24, 0, "dB"), P("output", "Output", -24, 24, 0, "dB"),
       P("size", "Size", 0, 1, 0, "", PKind::Enum, {"Lite", "Full"}), P("normalize", "Normalize", 0, 1, 1, "", PKind::Bool),
       P("mix", "Mix", 0, 100, 100, "%")}),
    D("ir", "IR cabinet", "IR", "cab", "Cabinet impulse response", "ir", 5,
      {P("length", "Length", 10, 500, 100, "ms", PKind::Float, {}, true), P("lowcut", "Low cut", 20, 400, 80, "Hz", PKind::Float, {}, true),
       P("highcut", "High cut", 2000, 20000, 9000, "Hz", PKind::Float, {}, true), P("level", "Level", -24, 12, 0, "dB"),
       P("mix", "Mix", 0, 100, 100, "%")}),
    D("eq", "Parametric EQ", "EQ", "eq", "Low/high cut, 2 shelves, 2 mids", "", 1,
      {P("lowcut", "Low cut", 20, 400, 20, "Hz", PKind::Float, {}, true), P("low", "Bass", -12, 12, 0, "dB"),
       P("lmfreq", "Low mid freq", 150, 1500, 400, "Hz", PKind::Float, {}, true), P("lmgain", "Low mid", -12, 12, 0, "dB"),
       P("hmfreq", "High mid freq", 1000, 6000, 2500, "Hz", PKind::Float, {}, true), P("hmgain", "High mid", -12, 12, 0, "dB"),
       P("high", "Treble", -12, 12, 0, "dB"), P("highcut", "High cut", 2000, 20000, 20000, "Hz", PKind::Float, {}, true),
       P("level", "Level", -12, 12, 0, "dB")}),
    D("tremolo", "Tremolo", "TRM", "mod", "Volume modulation", "", 0.6f,
      {P("rate", "Rate", 0.5f, 15, 5, "Hz", PKind::Float, {}, true), P("depth", "Depth", 0, 100, 50, "%"),
       P("shape", "Shape", 0, 2, 0, "", PKind::Enum, {"Sine", "Triangle", "Square"})}),
    D("wah", "Wah", "WAH", "mod", "Wah pedal (manual position, or an auto-wah that follows your picking)", "", 1.2f,
      {P("mode", "Mode", 0, 1, 0, "", PKind::Enum, {"Pedal", "Auto"}), P("pos", "Pedal position", 0, 100, 50, "%"),
       P("lo", "Heel frequency", 200, 800, 380, "Hz", PKind::Float, {}, true), P("hi", "Toe frequency", 1000, 4000, 2200, "Hz", PKind::Float, {}, true),
       P("q", "Resonance", 2, 12, 6, ""), P("sens", "Auto sensitivity", 0, 100, 50, "%"),
       P("release", "Auto release", 20, 500, 120, "ms", PKind::Float, {}, true), P("level", "Level", -12, 12, 0, "dB")}),
    D("octaver", "Octaver", "OCT", "mod", "Analog-style octave down (single notes work best)", "", 1.0f,
      {P("sub1", "-1 octave", 0, 100, 70, "%"), P("sub2", "-2 octaves", 0, 100, 0, "%"), P("dry", "Dry", 0, 100, 100, "%"),
       P("tone", "Sub tone", 100, 3000, 800, "Hz", PKind::Float, {}, true), P("level", "Level", -12, 12, 0, "dB")}),
    D("volume", "Volume", "VOL", "util", "Volume / expression", "", 0.4f,
      {P("level", "Level", -60, 12, 0, "dB")}),
    // Splits the signal into two paths; not added like a regular block.
    D("split", "Split / merge", "SPLIT", "route", "Two parallel paths A/B and their merge", "", 1,
      {P("mode", "Split type", 0, 3, 0, "", PKind::Enum, {"Y", "A/B", "Crossover", "Dynamic"}),
       P("freq", "Crossover frequency", 80, 5000, 650, "Hz", PKind::Float, {}, true),
       P("balance", "A/B balance", 0, 100, 50, "%"),
       P("thresh", "Threshold (dynamic)", -60, 0, -30, "dB"),
       P("levelA", "Level A", -60, 12, 0, "dB"), P("levelB", "Level B", -60, 12, 0, "dB"),
       P("panA", "Pan A", -100, 100, 0, "pan"), P("panB", "Pan B", -100, 100, 0, "pan"),
       P("invA", "Phase A \u00d8", 0, 1, 0, "", PKind::Bool), P("invB", "Phase B \u00d8", 0, 1, 0, "", PKind::Bool),
       P("stereo", "Stereo output", 0, 1, 1, "", PKind::Bool)}),
  };
}

// ---------------------------------------------------------------- base

Block::Block(const BlockDef* d) : def(d) {
  p_.reset(new std::atomic<float>[std::max<size_t>(1, d->params.size())]);
  for (size_t i = 0; i < d->params.size(); ++i) p_[i].store(d->params[i].def);
}

void Block::prepare(float sr, int maxBlock) {
  sr_ = sr;
  maxBlock_ = maxBlock;
  dryL_.assign(maxBlock, 0.0f);
  dryR_.assign(maxBlock, 0.0f);
  mix_ = on.load() ? 1.0f : 0.0f;
  mixStep_ = 1.0f / (0.010f * sr);  // 10 ms bypass crossfade
}

int Block::paramIndex(const std::string& pid) const {
  for (size_t i = 0; i < def->params.size(); ++i)
    if (pid == def->params[i].id) return (int)i;
  return -1;
}

void Block::set(int i, float v) {
  const auto& pd = def->params[i];
  v = clampf(v, pd.min, pd.max);
  if (pd.kind != PKind::Float) v = std::round(v);
  else if (pd.step > 0) v = pd.min + std::round((v - pd.min) / pd.step) * pd.step;
  p_[i].store(v, std::memory_order_relaxed);
}

bool Block::setByName(const std::string& pid, float v) {
  const int i = paramIndex(pid);
  if (i < 0) return false;
  set(i, v);
  return true;
}

json Block::toJson() const {
  json ex = extraJson();
  if (!ex.is_null()) {  // placeholder for a missing type: keep the original JSON
    ex["id"] = id;
    ex["on"] = on.load();
    if (!fileError.empty()) ex["error"] = fileError;
    return ex;
  }
  json ps = json::object();
  for (size_t i = 0; i < def->params.size(); ++i) ps[def->params[i].id] = get((int)i);
  json j = {{"id", id}, {"type", def->type}, {"on", on.load()}, {"params", ps}};
  if (!def->fileKind.empty()) j["file"] = file;
  if (!fileError.empty()) j["error"] = fileError;
  json inf = info();
  if (!inf.empty()) j["info"] = inf;
  return j;
}

void Block::fromJson(const json& j) {
  if (j.contains("on")) on.store(j["on"].get<bool>());
  if (j.contains("params"))
    for (auto it = j["params"].begin(); it != j["params"].end(); ++it)
      if (it.value().is_number()) setByName(it.key(), it.value().get<float>());
}

void Block::run(float* L, float* R, int n, bool stereo) {
  const float target = on.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
  if (mix_ == target) {
    if (target == 0.0f) return;
    process(L, R, n, stereo);
    return;
  }
  std::memcpy(dryL_.data(), L, n * sizeof(float));
  if (stereo) std::memcpy(dryR_.data(), R, n * sizeof(float));
  process(L, R, n, stereo);
  for (int i = 0; i < n; ++i) {
    mix_ += target > mix_ ? mixStep_ : -mixStep_;
    mix_ = clampf(mix_, 0.0f, 1.0f);
    if (std::fabs(mix_ - target) < mixStep_) mix_ = target;
    L[i] = dryL_[i] + mix_ * (L[i] - dryL_[i]);
    if (stereo) R[i] = dryR_[i] + mix_ * (R[i] - dryR_[i]);
  }
}

// ---------------------------------------------------------------- dynamics

class GateBlock : public Block {
 public:
  using Block::Block;
  void process(float* L, float* R, int n, bool st) override {
    const float thr = dbToLin(get(0));
    det_.setup(sr_, 0.5f, 20.0f);
    const float rel = std::exp(-1.0f / (0.001f * get(1) * sr_));
    const float att = std::exp(-1.0f / (0.001f * 0.5f * sr_));
    for (int i = 0; i < n; ++i) {
      const float e = det_.tick(st ? 0.5f * (L[i] + R[i]) : L[i]);
      if (e > thr) open_ = true;
      else if (e < thr * 0.5f) open_ = false;  // 6 dB hysteresis
      const float t = open_ ? 1.0f : 0.0f;
      const float c = t > g_ ? att : rel;
      g_ = t + c * (g_ - t);
      L[i] *= g_;
      if (st) R[i] *= g_;
    }
  }

 private:
  Envelope det_;
  float g_ = 0.0f;
  bool open_ = false;
};

// ---------------------------------------------------------------- IR

class IrBlock : public Block {
 public:
  using Block::Block;
  void prepare(float sr, int mb) override {
    Block::prepare(sr, mb);
    tmp_.assign(mb, 0.0f);
    out_.assign(mb, 0.0f);
    lastLow_ = lastHigh_ = -1;
    builtLen_ = -1;
    if (!ir_.empty()) rebuild();
  }
  bool loadFile(const std::string& path, std::string& err) override {
    WavData w;
    if (!readWav(path, w, err)) return false;
    std::vector<float> m = toMono(w);
    m = resample(m, w.sampleRate, (int)sr_);
    if (m.empty()) { err = "empty IR"; return false; }
    // energy normalisation
    double e = 0;
    for (float v : m) e += (double)v * v;
    const float g = e > 0 ? (float)(1.0 / std::sqrt(e)) : 1.0f;
    for (float& v : m) v *= g;
    ir_ = std::move(m);
    srcRate_ = w.sampleRate;
    rebuild();
    return true;
  }
  void control() override {
    conv_.collect();
    const int len = (int)get(0);
    if (!ir_.empty() && len != builtLen_) rebuild();
  }
  json info() const override {
    if (ir_.empty()) return json::object();
    return {{"samples", ir_.size()}, {"src_rate", srcRate_}, {"ms", 1000.0 * ir_.size() / sr_}};
  }
  void process(float* L, float* R, int n, bool st) override {
    Convolver* c = conv_.acquire();
    const float low = get(1), high = get(2);
    if (low != lastLow_) { lastLow_ = low; lc_.set(Biquad::HP, sr_, low, 0.707f); }
    if (high != lastHigh_) { lastHigh_ = high; hc_.set(Biquad::LP, sr_, high, 0.707f); }
    const float level = dbToLin(get(3)), mix = get(4) * 0.01f;
    if (st) for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);
    if (c && c->block() == n) {
      c->process(L, out_.data());
    } else {
      std::memcpy(out_.data(), L, n * sizeof(float));
    }
    for (int i = 0; i < n; ++i) {
      const float y = hc_.tick(lc_.tick(out_[i])) * level;
      L[i] = L[i] * (1.0f - mix) + y * mix;
      if (st) R[i] = L[i];
    }
  }

 private:
  void rebuild() {
    const int len = (int)get(0);
    builtLen_ = len;
    size_t taps = std::min(ir_.size(), (size_t)(len * 0.001f * sr_));
    if (taps < 16) taps = std::min<size_t>(16, ir_.size());
    std::vector<float> h(ir_.begin(), ir_.begin() + taps);
    const size_t fade = std::max<size_t>(1, taps / 20);  // 5 % fade-out
    for (size_t i = 0; i < fade; ++i) h[taps - 1 - i] *= (float)i / fade;
    auto* c = new Convolver();
    c->init(h, maxBlock_);
    conv_.set(c);
  }
  Swap<Convolver> conv_;
  std::vector<float> ir_, tmp_, out_;
  Biquad lc_, hc_;
  float lastLow_ = -1, lastHigh_ = -1;
  int builtLen_ = -1, srcRate_ = 0;
};

// ---------------------------------------------------------------- EQ

class EqBlock : public Block {
 public:
  using Block::Block;
  void process(float* L, float* R, int n, bool st) override {
    float cur[9];
    bool changed = false;
    for (int i = 0; i < 9; ++i) { cur[i] = get(i); if (cur[i] != last_[i]) changed = true; }
    if (changed) {
      std::memcpy(last_, cur, sizeof(cur));
      f_[0].set(Biquad::HP, sr_, cur[0], 0.707f);
      f_[1].set(Biquad::LSHELF, sr_, 100.0f, 0.707f, cur[1]);
      f_[2].set(Biquad::PEAK, sr_, cur[2], 1.0f, cur[3]);
      f_[3].set(Biquad::PEAK, sr_, cur[4], 1.0f, cur[5]);
      f_[4].set(Biquad::HSHELF, sr_, 6000.0f, 0.707f, cur[6]);
      f_[5].set(Biquad::LP, sr_, std::min(cur[7], sr_ * 0.45f), 0.707f);
    }
    const float level = dbToLin(cur[8]);
    const bool lc = cur[0] > 21.0f, hc = cur[7] < 19500.0f;
    for (int c = 0; c < (st ? 2 : 1); ++c) {
      float* x = c ? R : L;
      for (int i = 0; i < n; ++i) {
        float y = x[i];
        if (lc) y = f_[0].tick(y, c);
        y = f_[1].tick(y, c);
        y = f_[2].tick(y, c);
        y = f_[3].tick(y, c);
        y = f_[4].tick(y, c);
        if (hc) y = f_[5].tick(y, c);
        x[i] = y * level;
      }
    }
  }

 private:
  Biquad f_[6];
  float last_[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
};

// ---------------------------------------------------------------- modulation

class TremoloBlock : public Block {
 public:
  using Block::Block;
  void process(float* L, float* R, int n, bool st) override {
    lfo_.setRate(sr_, get(0));
    const float depth = get(1) * 0.01f;
    const int shape = (int)get(2);
    for (int i = 0; i < n; ++i) {
      float m = shape == 0 ? lfo_.sine() : shape == 1 ? lfo_.tri() : lfo_.square();
      if (shape == 2) { sq_ += 0.02f * (m - sq_); m = sq_; }  // rounded edge
      const float g = 1.0f - depth * (0.5f + 0.5f * m);
      L[i] *= g;
      if (st) R[i] *= g;
    }
  }

 private:
  Lfo lfo_;
  float sq_ = 0;
};

// Wah: band-pass state variable filter (TPT) swept between the heel and toe frequency.
// The position comes from the "pos" parameter (an expression pedal or stick can drive it) or, in Auto mode, from the picking envelope.
class WahBlock : public Block {
 public:
  using Block::Block;
  void prepare(float sr, int mb) override {
    Block::prepare(sr, mb);
    posSm_.setup(sr, 15.0f);
    posSm_.reset(get(1) * 0.01f);
    ic1_[0] = ic1_[1] = ic2_[0] = ic2_[1] = 0;
  }
  void process(float* L, float* R, int n, bool st) override {
    const bool autoMode = get(0) > 0.5f;
    const float lo = get(2), hi = std::max(get(3), lo * 1.5f), q = get(4), k = 1.0f / q;
    const float sens = get(5) * 0.1f, level = dbToLin(get(7));
    env_.setup(sr_, 4.0f, get(6));
    const float manual = get(1) * 0.01f;
    float g = 0, a1 = 1, a2 = 0, a3 = 0;
    for (int i = 0; i < n; ++i) {
      const float in = st ? 0.5f * (L[i] + R[i]) : L[i];
      const float target = autoMode ? clampf(env_.tick(in) * sens, 0.0f, 1.0f) : manual;
      const float pos = posSm_.next(target);
      if ((i & 7) == 0) {  // coefficients every 8 samples
        const float fc = lo * std::pow(hi / lo, pos);
        g = std::tan(kPi * clampf(fc, 20.0f, sr_ * 0.45f) / sr_);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
      }
      for (int c = 0; c < (st ? 2 : 1); ++c) {
        float* x = c ? R : L;
        const float v3 = x[i] - ic2_[c];
        const float v1 = a1 * ic1_[c] + a2 * v3;
        const float v2 = ic2_[c] + a2 * ic1_[c] + a3 * v3;
        ic1_[c] = 2.0f * v1 - ic1_[c];
        ic2_[c] = 2.0f * v2 - ic2_[c];
        x[i] = v1 * k * 2.0f * level;
      }
    }
  }

 private:
  Smoother posSm_;
  Envelope env_;
  float ic1_[2] = {0, 0}, ic2_[2] = {0, 0};
};

// Octaver: a flip-flop driven by the zero crossings of the low-passed input gives a square wave one octave down
// (a second flip-flop two octaves down); its level follows the input envelope and it is low-passed for a rounder tone.
class OctaverBlock : public Block {
 public:
  using Block::Block;
  void prepare(float sr, int mb) override {
    Block::prepare(sr, mb);
    in_.setLP(sr, 1000.0f);
    env_.setup(sr, 2.0f, 60.0f);
  }
  void process(float* L, float* R, int n, bool st) override {
    const float s1 = get(0) * 0.01f, s2 = get(1) * 0.01f, dry = get(2) * 0.01f, level = dbToLin(get(4));
    tone_.setLP(sr_, get(3));
    for (int i = 0; i < n; ++i) {
      const float x = st ? 0.5f * (L[i] + R[i]) : L[i];
      const float env = env_.tick(x);
      const float lp = in_.lp(x);
      const float h = std::max(0.2f * env, 1e-4f);  // hysteresis follows the level
      if (!high_ && lp > h) {
        high_ = true;
        ff1_ = !ff1_;
        if (ff1_) ff2_ = !ff2_;
      } else if (high_ && lp < -h) {
        high_ = false;
      }
      float sub = ((ff1_ ? 1.0f : -1.0f) * s1 + (ff2_ ? 1.0f : -1.0f) * s2) * env * 1.5f;
      sub = tone_.lp(tone_.lp(sub, 1), 0);
      L[i] = (L[i] * dry + sub) * level;
      if (st) R[i] = (R[i] * dry + sub) * level;
    }
  }

 private:
  OnePole in_, tone_;
  Envelope env_;
  bool high_ = false, ff1_ = false, ff2_ = false;
};

class VolumeBlock : public Block {
 public:
  using Block::Block;
  void prepare(float sr, int mb) override { Block::prepare(sr, mb); sm_.setup(sr, 20.0f); sm_.reset(dbToLin(get(0))); }
  void process(float* L, float* R, int n, bool st) override {
    const float t = get(0) <= -59.5f ? 0.0f : dbToLin(get(0));
    for (int i = 0; i < n; ++i) {
      const float g = sm_.next(t);
      L[i] *= g;
      if (st) R[i] *= g;
    }
  }

 private:
  Smoother sm_;
};

std::unique_ptr<Block> makeNamBlock(const BlockDef* d);     // nam_block.cpp
std::unique_ptr<Block> makeSplitBlock(const BlockDef* d);   // graph.cpp
std::unique_ptr<Block> makeEngineBlock(const BlockDef* d);  // engines.cpp
std::unique_ptr<Block> makeLv2Block(const BlockDef* d);     // lv2_host.cpp

// Placeholder for a type that is not available (missing type file or LV2 plugin):
// passes the signal through and keeps its JSON so a saved preset loses nothing.
class MissingBlock : public Block {
 public:
  using Block::Block;
  json extraJson() const override { return raw_; }
  void setRaw(const json& j) override { raw_ = j; raw_.erase("info"); raw_.erase("error"); }
 protected:
  void process(float*, float*, int, bool) override {}
 private:
  json raw_ = json::object();
};

std::unique_ptr<Block> makeBlock(const BlockDef* d) {
  if (!d) return nullptr;
  if (d->source == "fx") return makeEngineBlock(d);
  if (d->source == "lv2") return makeLv2Block(d);
  if (d->source == "missing") return std::make_unique<MissingBlock>(d);
  const std::string& type = d->type;
  if (type == "gate") return std::make_unique<GateBlock>(d);
  if (type == "nam") return makeNamBlock(d);
  if (type == "ir") return std::make_unique<IrBlock>(d);
  if (type == "eq") return std::make_unique<EqBlock>(d);
  if (type == "tremolo") return std::make_unique<TremoloBlock>(d);
  if (type == "wah") return std::make_unique<WahBlock>(d);
  if (type == "octaver") return std::make_unique<OctaverBlock>(d);
  if (type == "volume") return std::make_unique<VolumeBlock>(d);
  if (type == "split") return makeSplitBlock(d);
  return nullptr;
}

std::unique_ptr<Block> makeBlock(const std::string& type) { return makeBlock(findDef(type)); }

}  // namespace fx
