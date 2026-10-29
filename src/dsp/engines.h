// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
//
// Effect engines (layer 1). An engine is a configurable algorithm with a set of
// internal parameters. An effect type (fx/*.json) picks an engine, fixes some
// internal values and maps its user-facing knobs onto the internal parameters.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "dsp/blocks.h"

namespace fx {

struct EngineParam {
  const char* id;
  float def, min, max;
  const char* doc;
};

class FxEngine {
 public:
  virtual ~FxEngine() = default;
  virtual void prepare(float sr, int maxBlock) = 0;
  // p = internal parameter values (same order as EngineInfo::params)
  virtual void process(const float* p, float* L, float* R, int n, bool stereo) = 0;
  virtual void reset() {}
  virtual int latency() const { return 0; }
};

struct EngineInfo {
  const char* name;
  const char* desc;
  bool stereo;  // engine can produce stereo output
  std::vector<EngineParam> params;
  std::unique_ptr<FxEngine> (*make)();
};

const std::vector<EngineInfo>& engines();
const EngineInfo* findEngine(const std::string& name);
int engineParamIndex(const EngineInfo& e, const std::string& id);
json enginesJson();  // documentation of all engines (for type authors)

// Knob -> internal parameter mapping, compiled from the type file.
struct MapTarget {
  int target = -1;  // internal parameter index
  enum Op { Set, Mul, Add } op = Set;
  enum Mode { Identity, Range, Values } mode = Identity;
  float a = 0, b = 1;            // Range: output range
  bool logCurve = false;         // Range: a * (b/a)^t
  float power = 1;               // Range: t^power
  std::vector<float> values;     // Values: per enum/bool index
};

struct TypeSpec {
  const EngineInfo* engine = nullptr;
  std::vector<float> base;                    // engine defaults + "set" values
  std::vector<std::vector<MapTarget>> maps;   // per knob (BlockDef::params index)
};

// Parses one type object; returns false with an error message.
bool parseTypeDef(const json& j, BlockDef& out, std::string& err);

std::unique_ptr<Block> makeEngineBlock(const BlockDef* d);

}  // namespace fx
