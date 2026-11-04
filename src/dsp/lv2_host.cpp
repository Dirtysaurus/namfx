// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "dsp/lv2_host.h"
#include <cstdlib>

#ifdef NAMFX_HAVE_LILV

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/port-props/port-props.h>
#include <lv2/units/units.h>
#include <lv2/urid/urid.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace fx {

// ------------------------------------------------------------ URID map (thread-safe)

namespace {

struct UridMap {
  std::mutex mtx;
  std::unordered_map<std::string, LV2_URID> ids;
  std::vector<std::string> uris{""};
  LV2_URID map(const char* uri) {
    std::lock_guard<std::mutex> lk(mtx);
    auto it = ids.find(uri);
    if (it != ids.end()) return it->second;
    uris.push_back(uri);
    return ids[uri] = (LV2_URID)(uris.size() - 1);
  }
  const char* unmap(LV2_URID id) {
    std::lock_guard<std::mutex> lk(mtx);
    return id < uris.size() ? uris[id].c_str() : nullptr;
  }
};

UridMap& urids() {
  static UridMap m;
  return m;
}
LV2_URID mapCb(LV2_URID_Map_Handle, const char* uri) { return urids().map(uri); }
const char* unmapCb(LV2_URID_Unmap_Handle, LV2_URID id) { return urids().unmap(id); }

LV2_URID_Map gMap = {nullptr, mapCb};
LV2_URID_Unmap gUnmap = {nullptr, unmapCb};

struct World {
  LilvWorld* w = nullptr;
  ~World() { if (w) lilv_world_free(w); }
};
World& world() {
  static World w;
  return w;
}

enum class PortKind { AudioIn, AudioOut, CtrlIn, CtrlOut, AtomIn, AtomOut, Unused };

}  // namespace

struct Lv2Spec {
  const LilvPlugin* plugin = nullptr;
  std::string uri;
  struct Port {
    PortKind kind = PortKind::Unused;
    int param = -1;      // CtrlIn: index into BlockDef::params (-1 = fixed value)
    float fixed = 0;     // CtrlIn without parameter
    bool latency = false;
  };
  std::vector<Port> ports;
  int nIn = 0, nOut = 0;
};

static std::string sanitizeShort(const std::string& name) {
  std::string s;
  bool word = true;
  for (char c : name) {
    if (std::isalnum((unsigned char)c)) {
      if (word || std::isupper((unsigned char)c) || std::isdigit((unsigned char)c)) s += (char)std::toupper((unsigned char)c);
      word = false;
    } else word = true;
    if (s.size() >= 4) break;
  }
  if (s.size() < 2) {
    s.clear();
    for (char c : name) if (std::isalnum((unsigned char)c) && s.size() < 4) s += (char)std::toupper((unsigned char)c);
  }
  return s.empty() ? "LV2" : s;
}

static std::string catFor(const std::string& cls) {
  static const std::vector<std::pair<const char*, const char*>> m = {
      {"DelayPlugin", "dly"},      {"ReverbPlugin", "rev"},     {"ChorusPlugin", "mod"},    {"FlangerPlugin", "mod"},
      {"PhaserPlugin", "mod"},     {"ModulatorPlugin", "mod"},  {"DistortionPlugin", "drive"}, {"WaveshaperPlugin", "drive"},
      {"SimulatorPlugin", "amp"},  {"DynamicsPlugin", "dyn"},   {"CompressorPlugin", "dyn"}, {"ExpanderPlugin", "dyn"},
      {"GatePlugin", "dyn"},       {"LimiterPlugin", "dyn"},    {"EQPlugin", "eq"},          {"ParaEQPlugin", "eq"},
      {"MultiEQPlugin", "eq"},     {"FilterPlugin", "eq"},      {"LowpassPlugin", "eq"},     {"HighpassPlugin", "eq"},
      {"BandpassPlugin", "eq"},    {"CombPlugin", "eq"},        {"AllpassPlugin", "eq"},     {"SpatialPlugin", "rev"}};
  for (auto& [k, v] : m)
    if (cls == k) return v;
  return "util";
}

std::string lv2Path(const std::string& userDir) {
  std::string p = userDir;
  const char* env = std::getenv("LV2_PATH");
  std::string sys;
  if (env && *env) sys = env;
  else {
    const char* home = std::getenv("HOME");
    sys = std::string(home ? home : "/root") + "/.lv2:/usr/local/lib/lv2:/usr/lib/lv2:/usr/lib/aarch64-linux-gnu/lv2:/usr/lib/x86_64-linux-gnu/lv2";
  }
  return p.empty() ? sys : p + ":" + sys;
}

bool lv2Available() { return true; }

void lv2Scan(const std::string& userDir, std::vector<std::unique_ptr<BlockDef>>& out, json& log) {
  World& W = world();
  if (!W.w) {
    W.w = lilv_world_new();
    LilvNode* path = lilv_new_string(W.w, lv2Path(userDir).c_str());
    lilv_world_set_option(W.w, LILV_OPTION_LV2_PATH, path);
    lilv_node_free(path);
    lilv_world_load_all(W.w);
  }
  LilvWorld* w = W.w;
  auto N = [&](const char* uri) { return lilv_new_uri(w, uri); };
  LilvNode *nInput = N(LV2_CORE__InputPort), *nOutput = N(LV2_CORE__OutputPort), *nAudio = N(LV2_CORE__AudioPort),
           *nControl = N(LV2_CORE__ControlPort), *nAtom = N(LV2_ATOM__AtomPort), *nCV = N(LV2_CORE__CVPort),
           *nOptional = N(LV2_CORE__connectionOptional), *nToggled = N(LV2_CORE__toggled), *nInteger = N(LV2_CORE__integer),
           *nEnum = N(LV2_CORE__enumeration), *nLog = N(LV2_PORT_PROPS__logarithmic), *nLatency = N(LV2_CORE__latency),
           *nReportsLat = N(LV2_CORE__reportsLatency), *nDesignation = N(LV2_CORE__designation), *nEnabled = N(LV2_CORE__enabled),
           *nFreewheel = N(LV2_CORE__freeWheeling), *nNotOnGui = N(LV2_PORT_PROPS__notOnGUI), *nUnit = N(LV2_UNITS__unit),
           *nSampleRate = N(LV2_CORE__sampleRate), *nInstrument = N(LV2_CORE__InstrumentPlugin),
           *nRdfType = N("http://www.w3.org/1999/02/22-rdf-syntax-ns#type");
  static const char* supported[] = {LV2_URID__map, LV2_URID__unmap, LV2_OPTIONS__options, LV2_BUF_SIZE__boundedBlockLength,
                                    LV2_CORE__isLive, LV2_CORE__inPlaceBroken, LV2_CORE__hardRTCapable};

  const LilvPlugins* plugins = lilv_world_get_all_plugins(w);
  LILV_FOREACH(plugins, it, plugins) {
    const LilvPlugin* p = lilv_plugins_get(plugins, it);
    const std::string uri = lilv_node_as_uri(lilv_plugin_get_uri(p));
    auto skip = [&](const std::string& why) { log.push_back({{"lv2", uri}, {"skipped", why}}); };

    // required features
    std::string missingFeat;
    LilvNodes* req = lilv_plugin_get_required_features(p);
    LILV_FOREACH(nodes, f, req) {
      const char* fu = lilv_node_as_uri(lilv_nodes_get(req, f));
      if (std::none_of(std::begin(supported), std::end(supported), [&](const char* s) { return std::strcmp(s, fu) == 0; })) missingFeat = fu;
    }
    lilv_nodes_free(req);
    if (!missingFeat.empty()) { skip("needs feature " + missingFeat); continue; }
    // plugins that crashed (SIGSEGV) in `namfx --bench` on the R36S (guitarix 0.46.0, aarch64); a crash would end the whole app
    static const char* crashing[] = {"http://guitarix.sourceforge.net/plugins/gxtape_st#tape"};
    if (std::any_of(std::begin(crashing), std::end(crashing), [&](const char* s) { return uri == s; })) { skip("crashes in the host (segfault), disabled"); continue; }
    // most specific known class among all rdf:type values
    std::string cat = "util";
    bool instrument = false;
    LilvNodes* types = lilv_plugin_get_value(p, nRdfType);
    LILV_FOREACH(nodes, t, types) {
      const LilvNode* tn = lilv_nodes_get(types, t);
      if (!lilv_node_is_uri(tn)) continue;
      if (lilv_node_equals(tn, nInstrument)) instrument = true;
      const std::string tu = lilv_node_as_uri(tn);
      const std::string c = catFor(tu.substr(tu.find('#') + 1));
      if (c != "util") cat = c;
    }
    lilv_nodes_free(types);
    if (instrument) { skip("instrument"); continue; }

    auto d = std::make_unique<BlockDef>();
    auto spec = std::make_shared<Lv2Spec>();
    spec->plugin = p;
    spec->uri = uri;
    const uint32_t np = lilv_plugin_get_num_ports(p);
    spec->ports.resize(np);
    std::vector<float> mins(np), maxs(np), defs(np);
    lilv_plugin_get_port_ranges_float(p, mins.data(), maxs.data(), defs.data());
    std::string bad;
    for (uint32_t i = 0; i < np && bad.empty(); ++i) {
      const LilvPort* port = lilv_plugin_get_port_by_index(p, i);
      auto& P = spec->ports[i];
      const bool in = lilv_port_is_a(p, port, nInput), optional = lilv_port_has_property(p, port, nOptional);
      if (lilv_port_is_a(p, port, nAudio)) {
        P.kind = in ? PortKind::AudioIn : PortKind::AudioOut;
        (in ? spec->nIn : spec->nOut)++;
      } else if (lilv_port_is_a(p, port, nControl)) {
        if (!in) {
          P.kind = PortKind::CtrlOut;
          P.latency = lilv_port_has_property(p, port, nReportsLat);
          LilvNodes* des = lilv_port_get_value(p, port, nDesignation);
          if (des && lilv_nodes_contains(des, nLatency)) P.latency = true;
          lilv_nodes_free(des);
          continue;
        }
        P.kind = PortKind::CtrlIn;
        float mn = std::isnan(mins[i]) ? 0.0f : mins[i], mx = std::isnan(maxs[i]) ? 1.0f : maxs[i];
        if (!(mx > mn)) mx = mn + 1.0f;
        float df = std::isnan(defs[i]) ? mn : clampf(defs[i], mn, mx);
        LilvNodes* des = lilv_port_get_value(p, port, nDesignation);
        const bool enabled = des && lilv_nodes_contains(des, nEnabled);
        const bool freewheel = des && lilv_nodes_contains(des, nFreewheel);
        lilv_nodes_free(des);
        if (enabled || freewheel) { P.fixed = enabled ? 1.0f : 0.0f; continue; }  // host-managed ports
        ParamDef pd;
        pd.id = lilv_node_as_string(lilv_port_get_symbol(p, port));
        LilvNode* nm = lilv_port_get_name(p, port);
        pd.name = nm ? lilv_node_as_string(nm) : pd.id;
        lilv_node_free(nm);
        pd.min = mn;
        pd.max = mx;
        pd.def = df;
        if (lilv_port_has_property(p, port, nToggled)) {
          pd.kind = PKind::Bool;
          pd.optionValues = {mn, mx};
          pd.min = 0;
          pd.max = 1;
          pd.def = df > 0.5f * (mn + mx) ? 1.0f : 0.0f;
        } else if (lilv_port_has_property(p, port, nEnum)) {
          LilvScalePoints* sp = lilv_port_get_scale_points(p, port);
          std::vector<std::pair<float, std::string>> pts;
          if (sp) {
            LILV_FOREACH(scale_points, s, sp) {
              const LilvScalePoint* pt = lilv_scale_points_get(sp, s);
              const LilvNode *v = lilv_scale_point_get_value(pt), *l = lilv_scale_point_get_label(pt);
              if (v && l) pts.push_back({lilv_node_as_float(v), lilv_node_as_string(l)});
            }
            lilv_scale_points_free(sp);
          }
          std::sort(pts.begin(), pts.end());
          if (pts.size() >= 2) {
            pd.kind = PKind::Enum;
            int defIdx = 0;
            for (size_t k = 0; k < pts.size(); ++k) {
              pd.options.push_back(pts[k].second);
              pd.optionValues.push_back(pts[k].first);
              if (std::fabs(pts[k].first - df) < std::fabs(pts[defIdx].first - df)) defIdx = (int)k;
            }
            pd.min = 0;
            pd.max = (float)pts.size() - 1;
            pd.def = (float)defIdx;
          }
        }
        if (pd.kind == PKind::Float) {
          if (lilv_port_has_property(p, port, nInteger)) pd.step = 1;
          pd.log = lilv_port_has_property(p, port, nLog) && mn > 0;
          LilvNodes* u = lilv_port_get_value(p, port, nUnit);
          if (u && lilv_nodes_size(u) > 0) {
            const LilvNode* un = lilv_nodes_get_first(u);
            const std::string us = lilv_node_is_uri(un) ? lilv_node_as_uri(un) : "";
            if (us == LV2_UNITS__hz) pd.unit = "Hz";
            else if (us == LV2_UNITS__ms) pd.unit = "ms";
            else if (us == LV2_UNITS__db) pd.unit = "dB";
            else if (us == LV2_UNITS__pc) pd.unit = "%";
            else if (us == LV2_UNITS__s) pd.unit = "s";
          }
          lilv_nodes_free(u);
          if (lilv_port_has_property(p, port, nSampleRate)) pd.hidden = true;  // relative to the sample rate - rare, keep default
        }
        if (lilv_port_has_property(p, port, nNotOnGui)) pd.hidden = true;
        P.param = (int)d->params.size();
        d->params.push_back(pd);
      } else if (lilv_port_is_a(p, port, nAtom)) {
        P.kind = in ? PortKind::AtomIn : PortKind::AtomOut;
      } else if (optional) {
        P.kind = PortKind::Unused;
      } else {
        bad = lilv_port_is_a(p, port, nCV) ? "CV port" : "unsupported port type";
      }
    }
    if (!bad.empty()) { skip(bad); continue; }
    if (spec->nIn < 1 || spec->nIn > 2 || spec->nOut < 1 || spec->nOut > 2) {
      skip("audio ports " + std::to_string(spec->nIn) + " in / " + std::to_string(spec->nOut) + " out");
      continue;
    }
    LilvNode* name = lilv_plugin_get_name(p);
    d->name = name ? lilv_node_as_string(name) : uri;
    lilv_node_free(name);
    d->type = "lv2:" + uri;
    d->shortName = sanitizeShort(d->name);
    d->cat = cat;
    LilvNode* an = lilv_plugin_get_author_name(p);
    d->author = an ? lilv_node_as_string(an) : "";
    lilv_node_free(an);
    std::string bundle = lilv_node_as_uri(lilv_plugin_get_bundle_uri(p));
    while (!bundle.empty() && bundle.back() == '/') bundle.pop_back();
    d->pack = bundle.substr(bundle.find_last_of('/') + 1);
    d->desc = "LV2" + std::string(d->author.empty() ? "" : " · " + d->author) + " · " + d->pack;
    d->source = "lv2";
    d->bundled = false;
    d->stereo = spec->nOut == 2;
    d->cpu = 5;
    d->lv2 = spec;
    out.push_back(std::move(d));
  }
  for (LilvNode* n : {nInput, nOutput, nAudio, nControl, nAtom, nCV, nOptional, nToggled, nInteger, nEnum, nLog, nLatency,
                      nReportsLat, nDesignation, nEnabled, nFreewheel, nNotOnGui, nUnit, nSampleRate, nInstrument, nRdfType})
    lilv_node_free(n);
}

// ------------------------------------------------------------ block

namespace {

constexpr size_t kAtomBuf = 8192;

class Lv2Block : public Block {
 public:
  explicit Lv2Block(const BlockDef* d) : Block(d), spec_(d->lv2.get()) {}
  ~Lv2Block() override { release(); }

  void prepare(float sr, int mb) override {
    Block::prepare(sr, mb);
    release();
    const size_t np = spec_->ports.size();
    ctrl_.assign(np, 0.0f);
    audio_.assign(np, std::vector<float>(mb, 0.0f));
    atoms_.assign(np, std::vector<uint64_t>());
    rate_ = sr;
    minBlock_ = 1;
    maxBlockI_ = mb;
    nominal_ = mb;
    LV2_URID tInt = gMap.map(nullptr, LV2_ATOM__Int), tFloat = gMap.map(nullptr, LV2_ATOM__Float);
    opts_ = {{LV2_OPTIONS_INSTANCE, 0, gMap.map(nullptr, LV2_BUF_SIZE__minBlockLength), sizeof(int32_t), tInt, &minBlock_},
             {LV2_OPTIONS_INSTANCE, 0, gMap.map(nullptr, LV2_BUF_SIZE__maxBlockLength), sizeof(int32_t), tInt, &maxBlockI_},
             {LV2_OPTIONS_INSTANCE, 0, gMap.map(nullptr, LV2_BUF_SIZE__nominalBlockLength), sizeof(int32_t), tInt, &nominal_},
             {LV2_OPTIONS_INSTANCE, 0, gMap.map(nullptr, LV2_PARAMETERS__sampleRate), sizeof(float), tFloat, &rate_},
             {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr}};
    fMap_ = {LV2_URID__map, &gMap};
    fUnmap_ = {LV2_URID__unmap, &gUnmap};
    fOpts_ = {LV2_OPTIONS__options, opts_.data()};
    fBounded_ = {LV2_BUF_SIZE__boundedBlockLength, nullptr};
    features_ = {&fMap_, &fUnmap_, &fOpts_, &fBounded_, nullptr};
    inst_ = lilv_plugin_instantiate(spec_->plugin, sr, features_.data());
    if (!inst_) { fileError = "LV2 plugin could not be instantiated"; return; }
    seqType_ = gMap.map(nullptr, LV2_ATOM__Sequence);
    chunkType_ = gMap.map(nullptr, LV2_ATOM__Chunk);
    for (size_t i = 0; i < np; ++i) {
      const auto& P = spec_->ports[i];
      void* buf = nullptr;
      switch (P.kind) {
        case PortKind::AudioIn:
        case PortKind::AudioOut: buf = audio_[i].data(); break;
        case PortKind::CtrlIn: ctrl_[i] = P.param >= 0 ? raw(P.param) : P.fixed; buf = &ctrl_[i]; break;
        case PortKind::CtrlOut: buf = &ctrl_[i]; break;
        case PortKind::AtomIn:
        case PortKind::AtomOut: atoms_[i].assign(kAtomBuf / 8, 0); buf = atoms_[i].data(); break;
        default: break;
      }
      lilv_instance_connect_port(inst_, (uint32_t)i, buf);
      if (P.kind == PortKind::AudioIn) ins_.push_back((int)i);
      if (P.kind == PortKind::AudioOut) outs_.push_back((int)i);
      if (P.kind == PortKind::CtrlOut && P.latency) latPort_ = (int)i;
    }
    lilv_instance_activate(inst_);
    active_ = true;
  }

  int latency() const override { return latPort_ >= 0 && inst_ ? std::max(0, (int)ctrl_[latPort_]) : 0; }

 protected:
  void process(float* L, float* R, int n, bool st) override {
    if (!inst_) return;
    n = std::min(n, maxBlock_);
    for (size_t i = 0; i < spec_->ports.size(); ++i) {
      const auto& P = spec_->ports[i];
      if (P.kind == PortKind::CtrlIn && P.param >= 0) ctrl_[i] = raw(P.param);
      else if (P.kind == PortKind::AtomIn) {
        auto* seq = (LV2_Atom_Sequence*)atoms_[i].data();
        seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
        seq->atom.type = seqType_;
        seq->body.unit = 0;
        seq->body.pad = 0;
      } else if (P.kind == PortKind::AtomOut) {
        auto* a = (LV2_Atom*)atoms_[i].data();
        a->size = (uint32_t)(kAtomBuf - sizeof(LV2_Atom));
        a->type = chunkType_;
      }
    }
    float* a0 = audio_[ins_[0]].data();
    if (ins_.size() == 2) {
      std::memcpy(a0, L, n * sizeof(float));
      std::memcpy(audio_[ins_[1]].data(), st ? R : L, n * sizeof(float));
    } else if (st) {
      for (int i = 0; i < n; ++i) a0[i] = 0.5f * (L[i] + R[i]);
    } else {
      std::memcpy(a0, L, n * sizeof(float));
    }
    lilv_instance_run(inst_, (uint32_t)n);
    const float* o0 = audio_[outs_[0]].data();
    if (outs_.size() == 2 && st) {
      std::memcpy(L, o0, n * sizeof(float));
      std::memcpy(R, audio_[outs_[1]].data(), n * sizeof(float));
    } else if (outs_.size() == 2) {
      const float* o1 = audio_[outs_[1]].data();
      for (int i = 0; i < n; ++i) L[i] = 0.5f * (o0[i] + o1[i]);
    } else {
      std::memcpy(L, o0, n * sizeof(float));
      if (st) std::memcpy(R, o0, n * sizeof(float));
    }
  }

 private:
  float raw(int pi) const {  // block value -> LV2 port value
    const ParamDef& pd = def->params[pi];
    const float v = get(pi);
    if (!pd.optionValues.empty()) {
      const int k = std::clamp((int)std::lround(v), 0, (int)pd.optionValues.size() - 1);
      return pd.optionValues[k];
    }
    return v;
  }
  void release() {
    if (inst_) {
      if (active_) lilv_instance_deactivate(inst_);
      lilv_instance_free(inst_);
    }
    inst_ = nullptr;
    active_ = false;
    ins_.clear();
    outs_.clear();
    latPort_ = -1;
  }

  const Lv2Spec* spec_;
  LilvInstance* inst_ = nullptr;
  bool active_ = false;
  std::vector<float> ctrl_;
  std::vector<std::vector<float>> audio_;
  std::vector<std::vector<uint64_t>> atoms_;
  std::vector<int> ins_, outs_;
  int latPort_ = -1;
  float rate_ = 48000;
  int32_t minBlock_ = 1, maxBlockI_ = 128, nominal_ = 128;
  LV2_URID seqType_ = 0, chunkType_ = 0;
  std::vector<LV2_Options_Option> opts_;
  LV2_Feature fMap_{}, fUnmap_{}, fOpts_{}, fBounded_{};
  std::vector<const LV2_Feature*> features_;
};

}  // namespace

std::unique_ptr<Block> makeLv2Block(const BlockDef* d) { return std::make_unique<Lv2Block>(d); }

}  // namespace fx

#else  // without lilv

namespace fx {
struct Lv2Spec {};
void lv2Scan(const std::string&, std::vector<std::unique_ptr<BlockDef>>&, json& log) {
  log.push_back({{"lv2", ""}, {"skipped", "built without LV2 support (liblilv-dev missing)"}});
}
bool lv2Available() { return false; }
std::string lv2Path(const std::string& userDir) { return userDir; }
std::unique_ptr<Block> makeLv2Block(const BlockDef*) { return nullptr; }
}  // namespace fx

#endif
