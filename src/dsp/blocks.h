// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "json.hpp"
#include "dsp/dsp_util.h"

namespace fx {

using json = nlohmann::json;

enum class PKind { Float, Enum, Bool };

struct ParamDef {
  std::string id;
  std::string name;
  float min = 0, max = 1, def = 0;
  std::string unit;
  PKind kind = PKind::Float;
  std::vector<std::string> options = {};
  bool log = false;
  float step = 0;                    // > 0: values snap to this step (integer LV2 ports)
  std::vector<float> optionValues;   // enum index -> raw value (LV2 enumerations)
  bool hidden = false;               // not shown in the UIs (e.g. LV2 "enabled" ports)
};

struct TypeSpec;  // effect-type mapping (engines.h)
struct Lv2Spec;   // LV2 plugin description (lv2_host.h)

// Block type. Built-in types, effect types loaded from fx/*.json, LV2 plugins and
// placeholders for missing types all share this description.
struct BlockDef {
  std::string type;       // unique id ("delay", "delay_tape", "lv2:http://...")
  std::string name;       // display name (English; translated via lang files / tr)
  std::string shortName;  // tile abbreviation
  std::string cat;        // dyn|drive|amp|cab|eq|mod|dly|rev|util|route
  std::string desc;
  std::string fileKind;   // "nam" | "ir" | ""
  float cpu = 1;          // estimated load of one Cortex-A35 core in %
  std::vector<ParamDef> params;
  std::string source = "builtin";  // builtin | fx | lv2 | missing
  std::string pack;                // fx pack name / LV2 bundle
  std::string author, license;
  bool bundled = true;    // shipped with namfx (translated in web/lang)
  bool stereo = false;    // produces stereo: the chain is stereo after this block
  json tr = json::object();  // translations from the type file {"cs": {"name":..,"desc":..,"params":{id:{name,options}}}}
  std::shared_ptr<const TypeSpec> spec;  // source == "fx"
  std::shared_ptr<const Lv2Spec> lv2;    // source == "lv2"
};

// ---- catalog (thread-safe; definitions are never freed, pointers stay valid)
struct CatalogPaths {
  std::string fxDir = "fx";       // bundled effect types
  std::string userFxDir;          // data/fx
  std::string userLv2Dir;         // data/lv2
  bool lv2 = true;
};
void catalogInit(const CatalogPaths& p);
void catalogRescan();                         // effect types + LV2 again (new files)
const BlockDef* findDef(const std::string& type);
const BlockDef* missingDef(const std::string& type);
std::vector<const BlockDef*> catalogList();   // visible definitions (no placeholders)
json catalogJson();                           // cached
unsigned catalogVersion();
json catalogLog();                            // load messages (bad type files, skipped plugins)
void catalogSetMeasured(const std::string& type, float pct);  // measured A35 load
json defJson(const BlockDef& d);

class Block {
 public:
  Block(const BlockDef* d);
  virtual ~Block() = default;

  // control thread
  virtual void prepare(float sr, int maxBlock);
  virtual bool loadFile(const std::string& path, std::string& err) { (void)path; err = "block does not use files"; return false; }
  virtual void control() {}  // periodically from the control thread (freeing, rebuilds)
  virtual json info() const { return json::object(); }
  virtual int latency() const { return 0; }
  virtual json extraJson() const { return json(); }  // missing blocks: original JSON
  virtual void setRaw(const json&) {}

  // audio thread
  void run(float* L, float* R, int n, bool stereo);  // including bypass crossfade
  virtual void reset() {}

  int paramIndex(const std::string& pid) const;
  float get(int i) const { return p_[i].load(std::memory_order_relaxed); }
  void set(int i, float v);
  bool setByName(const std::string& pid, float v);
  json toJson() const;
  void fromJson(const json& j);

  std::string id;
  const BlockDef* def;
  std::atomic<bool> on{true};
  std::string file;  // control thread only
  std::string fileError;
  std::atomic<float> load{0.0f};  // measured share of the period (0..1), EMA

 protected:
  virtual void process(float* L, float* R, int n, bool stereo) = 0;
  float sr_ = 48000.0f;
  int maxBlock_ = 128;

 private:
  std::unique_ptr<std::atomic<float>[]> p_;
  std::vector<float> dryL_, dryR_;
  float mix_ = 1.0f;
  float mixStep_ = 0.0f;
};

std::unique_ptr<Block> makeBlock(const std::string& type);
std::unique_ptr<Block> makeBlock(const BlockDef* d);

}  // namespace fx
