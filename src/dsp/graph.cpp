// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/graph.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

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

static float blockLoad(const BlockPtr& b) {
  if (!b->on.load(std::memory_order_relaxed)) return 0.0f;
  const float l = b->load.load(std::memory_order_relaxed);
  return l > 0.001f ? l : b->def->cpu * 0.01f;  // measured while running, else the type's estimate
}

static float laneLoad(const std::vector<BlockPtr>& lane) {
  float s = 0;
  for (auto& b : lane) s += blockLoad(b);
  return s;
}

static float nodeLoad(const Node& nd) { return blockLoad(nd.block) + (nd.isSplit() ? laneLoad(nd.a) + laneLoad(nd.b) : 0.0f); }

float Graph::planWall(const std::vector<Node>& nodes, const GraphPlan& p) {
  float total = 0;
  for (auto& nd : nodes) total += nodeLoad(nd);
  if (p.mode == GraphPlan::Pipeline) {
    float s0 = 0;
    for (int i = 0; i < p.cut && i < (int)nodes.size(); ++i) s0 += nodeLoad(nodes[i]);
    return std::max(s0, total - s0);
  }
  if (p.mode == GraphPlan::Fork) {
    float saved = 0;
    for (size_t i = 0; i < nodes.size() && i < p.fork.size(); ++i)
      if (p.fork[i]) saved += std::min(laneLoad(nodes[i].a), laneLoad(nodes[i].b));
    return total - saved;
  }
  return total;
}

GraphPlan Graph::makePlan(const std::vector<Node>& nodes, bool haveWorker) {
  GraphPlan p;
  float total = 0;
  for (auto& nd : nodes) total += nodeLoad(nd);
  p.serial = p.wall = total;
  p.fork.assign(nodes.size(), 0);
  if (!haveWorker || nodes.empty() || total < 0.15f) return p;  // too little to be worth a second thread

  // Fork: split lanes that both carry real load
  GraphPlan fk = p;
  float saved = 0;
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (!nodes[i].isSplit()) continue;
    const float m = std::min(laneLoad(nodes[i].a), laneLoad(nodes[i].b));
    if (m >= 0.05f) { fk.fork[i] = 1; saved += m; }
  }
  if (saved > 0) { fk.mode = GraphPlan::Fork; fk.wall = total - saved; }

  // Pipeline: the cut that balances the two stages best
  GraphPlan pl = p;
  if (nodes.size() >= 2) {
    float pre = 0, best = total;
    int cut = 0;
    for (size_t i = 0; i + 1 < nodes.size(); ++i) {
      pre += nodeLoad(nodes[i]);
      const float wall = std::max(pre, total - pre);
      if (wall < best) { best = wall; cut = (int)i + 1; }
    }
    if (cut > 0) { pl.mode = GraphPlan::Pipeline; pl.cut = cut; pl.wall = best; }
  }

  // Fork has no extra latency: take it unless the pipeline is clearly better. Require a real gain (15 %) over serial.
  GraphPlan chosen = p;
  if (fk.mode == GraphPlan::Fork && (pl.mode != GraphPlan::Pipeline || fk.wall <= pl.wall + 0.03f)) chosen = fk;
  else if (pl.mode == GraphPlan::Pipeline) chosen = pl;
  if (chosen.mode != GraphPlan::Serial && chosen.wall > 0.85f * total) return p;
  return chosen;
}

Graph::Graph(std::vector<Node> nodes, int maxBlock, float sampleRate, Worker* worker)
    : nodes_(std::move(nodes)), w_(worker && worker->running() ? worker : nullptr), sr_(sampleRate) {
  for (Scratch* s : {&s0_, &s1_}) {
    s->aL.assign(maxBlock, 0.0f);
    s->aR.assign(maxBlock, 0.0f);
    s->bL.assign(maxBlock, 0.0f);
    s->bR.assign(maxBlock, 0.0f);
  }
  for (auto* v : {&pipeL_, &pipeR_, &outL_, &outR_}) v->assign(maxBlock, 0.0f);
  plan_ = makePlan(nodes_, w_ != nullptr);
  if (plan_.mode == GraphPlan::Serial) w_ = nullptr;
  const char* name = plan_.mode == GraphPlan::Pipeline ? "pipeline" : plan_.mode == GraphPlan::Fork ? "fork" : "serial";
  std::fprintf(stderr, "[graph] %s%s: estimated %.0f %% of a core in total, longest thread %.0f %%\n", name,
               plan_.mode == GraphPlan::Pipeline ? (" (cut at node " + std::to_string(plan_.cut) + ")").c_str() : "", plan_.serial * 100.0f,
               plan_.wall * 100.0f);
}

Graph::~Graph() {
  if (w_) w_->wait();  // the worker may still be inside a job of this graph (it is freed on the control thread)
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

void Graph::laneFn(void* ctx) {
  auto* j = static_cast<LaneJob*>(ctx);
  for (auto& b : *j->lane) j->g->timed(*b, j->L, j->R, j->n, false);
}

void Graph::stageFn(void* ctx) {  // pipeline stage 1 on the worker, in place on pipeL_/pipeR_
  auto* j = static_cast<StageJob*>(ctx);
  Graph* g = j->g;
  bool stereo = j->stereoIn;
  g->runRange(g->plan_.cut, (int)g->nodes_.size(), g->pipeL_.data(), g->pipeR_.data(), j->n, stereo, g->s1_, false);
  if (!stereo) std::memcpy(g->pipeR_.data(), g->pipeL_.data(), j->n * sizeof(float));
  j->stereoOut = true;  // pipeR_ is always filled
}

void Graph::runRange(int first, int last, float* L, float* R, int n, bool& stereo, Scratch& s, bool allowFork) {
  for (int idx = first; idx < last; ++idx) {
    Node& node = nodes_[idx];
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
    sp->split(L, s.aL.data(), s.bL.data(), n);
    if (allowFork && w_ && idx < (int)plan_.fork.size() && plan_.fork[idx]) {
      // the lighter lane goes to the worker, the audio thread runs the other one meanwhile
      const bool bLighter = laneLoad(node.b) <= laneLoad(node.a);
      const std::vector<BlockPtr>& mine = bLighter ? node.a : node.b;
      lane_ = {this, bLighter ? &node.b : &node.a, bLighter ? s.bL.data() : s.aL.data(), bLighter ? s.bR.data() : s.aR.data(), n};
      w_->submit(&Graph::laneFn, &lane_);
      for (auto& b : mine) timed(*b, bLighter ? s.aL.data() : s.bL.data(), bLighter ? s.aR.data() : s.bR.data(), n, false);
      if (!w_->wait()) std::fprintf(stderr, "[audio] worker lane timed out\n");
    } else {
      for (auto& b : node.a) timed(*b, s.aL.data(), s.aR.data(), n, false);
      for (auto& b : node.b) timed(*b, s.bL.data(), s.bR.data(), n, false);
    }
    stereo = sp->merge(s.aL.data(), s.bL.data(), laneLatency(node.a), laneLatency(node.b), L, R, n);
  }
}

void Graph::process(float* L, float* R, int n) {
  if (w_ && firstCall_) {  // the previous graph's last job may still be running on the worker (it shares our blocks)
    w_->wait();
    firstCall_ = false;
  }
  bool stereo = false;
  const int N = (int)nodes_.size();
  if (!w_ || plan_.mode == GraphPlan::Serial) {
    runRange(0, N, L, R, n, stereo, s0_, false);
  } else if (plan_.mode == GraphPlan::Fork) {
    runRange(0, N, L, R, n, stereo, s0_, true);
  } else {  // Pipeline: stage 0 here, stage 1 on the worker one block behind
    runRange(0, plan_.cut, L, R, n, stereo, s0_, false);
    bool have = false;
    if (pipeStarted_) {
      if (w_->wait()) {  // stage 1 of the previous block is done
        std::memcpy(outL_.data(), pipeL_.data(), n * sizeof(float));
        std::memcpy(outR_.data(), pipeR_.data(), n * sizeof(float));
        have = true;
      } else {
        std::fprintf(stderr, "[audio] worker stage timed out\n");
        std::memset(L, 0, n * sizeof(float));  // the worker still owns pipeL_/pipeR_: do not hand over again
        std::memset(R, 0, n * sizeof(float));
        return;
      }
    }
    std::memcpy(pipeL_.data(), L, n * sizeof(float));
    std::memcpy(pipeR_.data(), R, n * sizeof(float));
    stage_ = {this, n, stereo, true};
    w_->submit(&Graph::stageFn, &stage_);
    pipeStarted_ = true;
    if (have) {
      std::memcpy(L, outL_.data(), n * sizeof(float));
      std::memcpy(R, outR_.data(), n * sizeof(float));
    } else {
      std::memset(L, 0, n * sizeof(float));
      std::memset(R, 0, n * sizeof(float));
    }
    return;
  }
  if (!stereo) std::memcpy(R, L, n * sizeof(float));
}

}  // namespace fx
