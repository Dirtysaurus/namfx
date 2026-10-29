// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <memory>
#include <vector>
#include "dsp/blocks.h"

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

// Immutable graph built by the control thread, handed to the audio thread via Swap.
class Graph {
 public:
  Graph(std::vector<Node> nodes, int maxBlock, float sampleRate);
  // mono input in L; output L/R (always fills both). The main chain turns stereo after
  // the first block whose type produces stereo (delay, reverb, ...) or after a stereo merge.
  // Split paths are mono. Every block's processing time is measured (Block::load).
  void process(float* L, float* R, int n);
  const std::vector<Node>& nodes() const { return nodes_; }

 private:
  std::vector<Node> nodes_;
  void timed(Block& b, float* L, float* R, int n, bool st);
  std::vector<float> aL_, aR_, bL_, bR_;
  float sr_ = 48000;
};

}  // namespace fx
