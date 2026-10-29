// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/graph.h"
#include <chrono>
#include <cstring>

namespace fx {

void SplitBlock::prepare(float sr, int mb) {
  Block::prepare(sr, mb);
  env_.setup(sr, 5.0f, 150.0f);
  for (Smoother* s : {&dyn_, &gA_, &gB_, &pA_, &pB_}) s->setup(sr, 20.0f);
  dyn_.reset(0.5f);
  gA_.reset(dbToLin(get(4)));
  gB_.reset(dbToLin(get(5)));
  pA_.reset(get(6) * 0.01f);
  pB_.reset(get(7) * 0.01f);
  dA_.init((int)(0.05f * sr));
  dB_.init((int)(0.05f * sr));
  lastFreq_ = -1;
}

void SplitBlock::reset() {
  for (auto& f : lp_) f.clear();
  for (auto& f : hp_) f.clear();
  dA_.clear();
  dB_.clear();
}

void SplitBlock::split(const float* in, float* a, float* b, int n) {
  const int mode = (int)get(0);
  if (mode == 2) {  // Linkwitz-Riley 24 dB/oct crossover: B = lows, A = highs
    const float f = get(1);
    if (f != lastFreq_) {
      lastFreq_ = f;
      for (int i = 0; i < 2; ++i) {
        lp_[i].set(Biquad::LP, sr_, f, 0.70710678f);
        hp_[i].set(Biquad::HP, sr_, f, 0.70710678f);
      }
    }
    for (int i = 0; i < n; ++i) {
      b[i] = lp_[1].tick(lp_[0].tick(in[i]));
      a[i] = hp_[1].tick(hp_[0].tick(in[i]));
    }
  } else if (mode == 1) {  // A/B balance (equal power)
    const float bal = get(2) * 0.01f;
    const float ga = std::sin(bal * kPi * 0.5f), gb = std::cos(bal * kPi * 0.5f);
    for (int i = 0; i < n; ++i) { a[i] = in[i] * ga; b[i] = in[i] * gb; }
  } else if (mode == 3) {  // Dynamic: hard picking -> A, soft -> B
    const float thr = get(3);
    for (int i = 0; i < n; ++i) {
      const float e = linToDb(env_.tick(in[i]));
      const float x = dyn_.next(clampf((e - thr) / 6.0f + 0.5f, 0.0f, 1.0f));
      a[i] = in[i] * x;
      b[i] = in[i] * (1.0f - x);
    }
  } else {  // Y
    std::memcpy(a, in, n * sizeof(float));
    std::memcpy(b, in, n * sizeof(float));
  }
}

bool SplitBlock::merge(float* aL, float* bL, int latA, int latB, float* L, float* R, int n) {
  // latency alignment: the shorter path gets delayed
  const int d = latA - latB;
  if (d > 0) for (int i = 0; i < n; ++i) { dB_.push(bL[i]); bL[i] = dB_.readInt(d); }
  if (d < 0) for (int i = 0; i < n; ++i) { dA_.push(aL[i]); aL[i] = dA_.readInt(-d); }
  const float ta = (get(4) <= -59.5f ? 0.0f : dbToLin(get(4))) * (get(8) > 0.5f ? -1.0f : 1.0f);
  const float tb = (get(5) <= -59.5f ? 0.0f : dbToLin(get(5))) * (get(9) > 0.5f ? -1.0f : 1.0f);
  const bool stereo = get(10) > 0.5f;
  const float tpa = get(6) * 0.01f, tpb = get(7) * 0.01f;
  for (int i = 0; i < n; ++i) {
    const float ga = gA_.next(ta), gb = gB_.next(tb);
    const float va = aL[i] * ga, vb = bL[i] * gb;
    if (stereo) {
      const float qa = (pA_.next(tpa) + 1.0f) * kPi * 0.25f, qb = (pB_.next(tpb) + 1.0f) * kPi * 0.25f;
      L[i] = va * std::cos(qa) + vb * std::cos(qb);
      R[i] = va * std::sin(qa) + vb * std::sin(qb);
    } else {
      L[i] = va + vb;
    }
  }
  return stereo;
}

std::unique_ptr<Block> makeSplitBlock(const BlockDef* d) { return std::make_unique<SplitBlock>(d); }

Graph::Graph(std::vector<Node> nodes, int maxBlock, float sampleRate) : nodes_(std::move(nodes)), sr_(sampleRate) {
  aL_.assign(maxBlock, 0.0f);
  aR_.assign(maxBlock, 0.0f);
  bL_.assign(maxBlock, 0.0f);
  bR_.assign(maxBlock, 0.0f);
}

static int laneLatency(const std::vector<BlockPtr>& lane) {
  int l = 0;
  for (auto& b : lane)
    if (b->on.load(std::memory_order_relaxed)) l += b->latency();
  return l;
}

void Graph::timed(Block& b, float* L, float* R, int n, bool st) {
  const auto t0 = std::chrono::steady_clock::now();
  b.run(L, R, n, st);
  const float dt = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
  const float frac = dt * sr_ / (float)n;
  b.load.store(b.load.load(std::memory_order_relaxed) * 0.97f + frac * 0.03f, std::memory_order_relaxed);
}

void Graph::process(float* L, float* R, int n) {
  bool stereo = false;
  for (auto& node : nodes_) {
    if (!node.isSplit()) {
      if (!stereo && node.block->def->stereo) {
        std::memcpy(R, L, n * sizeof(float));
        stereo = true;
      }
      timed(*node.block, L, R, n, stereo);
      continue;
    }
    auto* sp = static_cast<SplitBlock*>(node.block.get());
    if (stereo) for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);
    sp->split(L, aL_.data(), bL_.data(), n);
    for (auto& b : node.a) timed(*b, aL_.data(), aR_.data(), n, false);
    for (auto& b : node.b) timed(*b, bL_.data(), bR_.data(), n, false);
    stereo = sp->merge(aL_.data(), bL_.data(), laneLatency(node.a), laneLatency(node.b), L, R, n);
  }
  if (!stereo) std::memcpy(R, L, n * sizeof(float));
}

}  // namespace fx
