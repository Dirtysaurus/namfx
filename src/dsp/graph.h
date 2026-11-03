// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <memory>
#include <vector>
#include "dsp/blocks.h"
#include "dsp/worker.h"

namespace fx {

using BlockPtr = std::shared_ptr<Block>;

// Split state (filters, buffers). Parameters live in the "split" block.
class SplitBlock : public Block {
 public:
  using Block::Block;
  void prepare(float sr, int mb) override;
  // splits mono input into a, b
  void split(const float* in, float* a, float* b, int n);
  // merges the paths into L/R, returns true if the output is stereo
  bool merge(float* aL, float* bL, int latA, int latB, float* L, float* R, int n);
  void reset() override;

 protected:
  void process(float*, float*, int, bool) override {}

 private:
  Biquad lp_[2], hp_[2];
  float lastFreq_ = -1;
  Envelope env_;
  Smoother dyn_, gA_, gB_, pA_, pB_;
  DelayLine dA_, dB_;
};

struct Node {
  BlockPtr block;                // regular block or SplitBlock
  std::vector<BlockPtr> a, b;    // split only
  bool isSplit() const { return block && block->def->type == "split"; }
};

// How the graph is spread over the audio thread and the worker thread (see Graph::makePlan).
//  Serial:   everything on the audio thread.
//  Fork:     the lighter lane of each marked split runs on the worker at the same time as the other lane (no extra latency).
//  Pipeline: nodes [0, cut) on the audio thread, nodes [cut, N) on the worker, one block behind (one block of extra latency).
struct GraphPlan {
  enum Mode { Serial, Fork, Pipeline } mode = Serial;
  int cut = 0;                 // Pipeline: first node of the second stage
  std::vector<char> fork;      // Fork: per node, the lanes of this split run in parallel
  float serial = 0, wall = 0;  // estimated load [fraction of a period] on one core / with this plan (the longest thread)
  bool sameShape(const GraphPlan& o) const { return mode == o.mode && cut == o.cut && fork == o.fork; }
};

// Immutable graph built by the control thread, handed to the audio thread via Swap.
class Graph {
 public:
  // worker: the second audio thread (nullptr = single threaded). The plan decides what runs on it.
  Graph(std::vector<Node> nodes, int maxBlock, float sampleRate, Worker* worker = nullptr);
  ~Graph();
  // estimated load of the nodes (measured where the block has run already, else the type's estimate) and the best plan
  static GraphPlan makePlan(const std::vector<Node>& nodes, bool haveWorker);
  // wall load of a given plan with the current loads
  static float planWall(const std::vector<Node>& nodes, const GraphPlan& plan);
  const GraphPlan& plan() const { return plan_; }
  // mono input in L; output L/R (always fills both). The main chain turns stereo after
  // the first block whose type produces stereo (delay, reverb, ...) or after a stereo merge.
  // Split paths are mono. Every block's processing time is measured (Block::load).
  void process(float* L, float* R, int n);
  const std::vector<Node>& nodes() const { return nodes_; }

 private:
  struct Scratch { std::vector<float> aL, aR, bL, bR; };
  struct LaneJob { Graph* g; const std::vector<BlockPtr>* lane; float* L; float* R; int n; };
  struct StageJob { Graph* g; int n; bool stereoIn, stereoOut; };
  void runRange(int first, int last, float* L, float* R, int n, bool& stereo, Scratch& s, bool allowFork);
  void timed(Block& b, float* L, float* R, int n, bool st);
  static void laneFn(void* ctx);
  static void stageFn(void* ctx);

  std::vector<Node> nodes_;
  GraphPlan plan_;
  Worker* w_ = nullptr;
  bool firstCall_ = true, pipeStarted_ = false;
  Scratch s0_, s1_;                   // stage 0 / fork lanes use s0_, the worker's pipeline stage s1_
  std::vector<float> pipeL_, pipeR_, outL_, outR_;
  LaneJob lane_{};
  StageJob stage_{};
  float sr_ = 48000;
};

}  // namespace fx
