// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#include "dsp/blocks.h"

namespace fx {

namespace {

struct NamModel {
  std::unique_ptr<nam::DSP> dsp;
  float normGain = 1.0f;
};

class NamBlock : public Block {
 public:
  using Block::Block;

  void prepare(float sr, int mb) override {
    Block::prepare(sr, mb);
    in_.assign(mb, 0.0f);
    out_.assign(mb, 0.0f);
    gin_.setup(sr, 20.0f);
    gout_.setup(sr, 20.0f);
    gin_.reset(dbToLin(get(0)));
    gout_.reset(dbToLin(get(1)));
    if (!file.empty() && !path_.empty()) {
      std::string err;
      loadFile(path_, err);
    }
  }

  bool loadFile(const std::string& path, std::string& err) override {
    try {
      auto m = std::make_unique<NamModel>();
      m->dsp = nam::get_dsp(std::filesystem::path(path));
      if (!m->dsp) { err = "model could not be loaded"; return false; }
      if (m->dsp->NumInputChannels() != 1 || m->dsp->NumOutputChannels() != 1) { err = "model is not mono"; return false; }
      auto* slim = dynamic_cast<nam::SlimmableModel*>(m->dsp.get());
      if (slim) slim->SetSlimmableSize(get(2));
      m->dsp->Reset(sr_, maxBlock_);
      const bool hasLoud = m->dsp->HasLoudness();
      m->normGain = hasLoud ? dbToLin(-18.0f - (float)m->dsp->GetLoudness()) : 1.0f;
      meta_ = readMeta(path);
      meta_["slimmable"] = slim != nullptr;
      if (slim) {
        json bp = json::array();
        for (double v : slim->GetSlimmableSizeBreakpoints()) bp.push_back(v);
        meta_["breakpoints"] = bp;
      }
      meta_["sample_rate"] = m->dsp->GetExpectedSampleRate();
      if (hasLoud) meta_["loudness"] = m->dsp->GetLoudness();
      const double esr = m->dsp->GetExpectedSampleRate();
      if (esr > 0 && std::fabs(esr - sr_) > 1.0) meta_["warning"] = "model was trained at " + std::to_string((int)esr) + " Hz";
      lastSize_ = get(2);
      latest_ = m.get();
      slim_ = slim;
      model_.set(m.release());
      path_ = path;
      return true;
    } catch (const std::exception& e) {
      err = std::string("model error: ") + e.what();
      return false;
    }
  }

  void control() override {
    model_.collect();
    const float s = get(2);
    if (slim_ && s != lastSize_) {
      lastSize_ = s;
      slim_->SetSlimmableSize(s);
    }
  }

  json info() const override { return meta_; }

  void process(float* L, float* R, int n, bool st) override {
    NamModel* m = model_.acquire();
    if (!m) return;  // pass-through without a model
    if (st) for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);
    const float gi = dbToLin(get(0));
    const float go = dbToLin(get(1)) * (get(3) > 0.5f ? m->normGain : 1.0f);
    const float mix = get(4) * 0.01f;
    for (int i = 0; i < n; ++i) in_[i] = L[i] * gin_.next(gi);
    float* ip = in_.data();
    float* op = out_.data();
    m->dsp->process(&ip, &op, n);
    for (int i = 0; i < n; ++i) {
      const float y = op[i] * gout_.next(go);
      L[i] = L[i] * (1.0f - mix) + y * mix;
      if (st) R[i] = L[i];
    }
  }

 private:
  static json readMeta(const std::string& path) {
    json out = json::object();
    try {
      std::ifstream f(path);
      json j = json::parse(f, nullptr, false);
      if (j.is_discarded()) return out;
      out["architecture"] = j.value("architecture", "");
      const json* md = nullptr;
      if (j.contains("metadata")) md = &j["metadata"];
      else if (j.contains("config") && j["config"].contains("submodels") && !j["config"]["submodels"].empty() &&
               j["config"]["submodels"][0].contains("model") && j["config"]["submodels"][0]["model"].contains("metadata"))
        md = &j["config"]["submodels"][0]["model"]["metadata"];
      if (md && md->is_object())
        for (const char* k : {"name", "modeled_by", "gear_make", "gear_model", "gear_type", "tone_type"})
          if (md->contains(k) && (*md)[k].is_string()) out[k] = (*md)[k];
    } catch (...) {
    }
    return out;
  }

  Swap<NamModel> model_;
  NamModel* latest_ = nullptr;
  nam::SlimmableModel* slim_ = nullptr;
  float lastSize_ = -1;
  std::string path_;
  json meta_ = json::object();
  std::vector<float> in_, out_;
  Smoother gin_, gout_;
};

}  // namespace

std::unique_ptr<Block> makeNamBlock(const BlockDef* d) { return std::make_unique<NamBlock>(d); }

}  // namespace fx
