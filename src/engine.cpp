// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "engine.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include "i18n.h"
#include "dsp/engines.h"
#include "dsp/lv2_host.h"
#include "io/wav.h"

namespace fs = std::filesystem;

namespace fx {

// "Čistý Lead 2" -> "Cisty_Lead_2" (strips Czech diacritics, keeps [A-Za-z0-9_-])
static std::string fileNameFor(const std::string& name) {
  static const char* from[] = {"á", "č", "ď", "é", "ě", "í", "ň", "ó", "ř", "š", "ť", "ú", "ů", "ý", "ž",
                               "Á", "Č", "Ď", "É", "Ě", "Í", "Ň", "Ó", "Ř", "Š", "Ť", "Ú", "Ů", "Ý", "Ž"};
  static const char* to = "acdeeinorstuuyzACDEEINORSTUUYZ";
  std::string out;
  for (size_t i = 0; i < name.size();) {
    bool done = false;
    for (int k = 0; k < 30 && !done; ++k) {
      const size_t n = std::strlen(from[k]);
      if (name.compare(i, n, from[k]) == 0) { out += to[k]; i += n; done = true; }
    }
    if (done) continue;
    const unsigned char ch = (unsigned char)name[i++];
    if (std::isalnum(ch) || ch == '-') out += (char)ch;
    else if (ch < 0x80 || (ch & 0xC0) == 0xC0) out += '_';
  }
  if (out.empty()) out = "preset";
  return out;
}

static bool safeName(const std::string& s) {
  return !s.empty() && s.find("..") == std::string::npos && s.find('/') == std::string::npos && s.find('\\') == std::string::npos;
}

Engine::Engine(const Config& cfg) : cfg_(cfg) {
  L_.assign(cfg.block, 0.0f);
  R_.assign(cfg.block, 0.0f);
  inSm_.setup(cfg.sampleRate, 20.0f);
  outSm_.setup(cfg.sampleRate, 20.0f);
  inSm_.reset(1.0f);
  outSm_.reset(1.0f);
  tuner_.prepare(cfg.sampleRate);
  for (const char* d : {"nam", "ir", "presets", "wav", "fx", "lv2"}) fs::create_directories(dataPath(d));
  {
    std::ifstream sf(dataPath("settings.json"));
    json st = json::parse(sf, nullptr, false);
    if (!st.is_discarded() && st.is_object()) settings_ = st;
  }
  CatalogPaths cp;
  cp.fxDir = cfg.fxDir;
  cp.userFxDir = dataPath("fx");
  cp.userLv2Dir = dataPath("lv2");
  cp.lv2 = cfg.lv2;
  catalogInit(cp);
  {
    std::ifstream cf(dataPath("cpu_stats.json"));
    json cs = json::parse(cf, nullptr, false);
    if (!cs.is_discarded() && cs.is_object())
      for (auto it = cs.begin(); it != cs.end(); ++it)
        if (it.value().is_number()) { measured_[it.key()] = it.value().get<float>(); catalogSetMeasured(it.key(), measured_[it.key()]); }
  }
  rebuild();
}

json Engine::settings() {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  return settings_;
}

void Engine::saveSettings() { std::ofstream(dataPath("settings.json")) << settings_.dump(2); }

// measured load per type (on the R36S these are real A35 numbers) -> catalog + data/cpu_stats.json
void Engine::updateMeasured() {
  auto take = [&](const BlockPtr& b) {
    if (!b->on.load() || b->def->source == "missing" || b->def->type == "split") return;
    const float pct = b->load.load() * 100.0f;
    if (pct <= 0.01f) return;
    float& m = measured_[b->def->type];
    const float nv = m > 0 ? m * 0.8f + pct * 0.2f : pct;
    if (std::fabs(nv - m) > 0.05f) { m = nv; measuredDirty_ = true; catalogSetMeasured(b->def->type, m); }
  };
  for (auto& nd : nodes_) {
    take(nd.block);
    for (auto& b : nd.a) take(b);
    for (auto& b : nd.b) take(b);
  }
}

// ------------------------------------------------------------ audio thread

void Engine::process(const float* in, float* outL, float* outR, int n) {
  Graph* g = graph_.acquire();
  WavBuf* wb = wav_.acquire();
  float* L = L_.data();
  float* R = R_.data();
  if (useWav_.load(std::memory_order_relaxed) && wb && !wb->data.empty()) {
    size_t pos = wavPos_.load(std::memory_order_relaxed);
    const size_t len = wb->data.size();
    const bool play = playing_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
      if (play && pos >= len) {
        if (loop_.load(std::memory_order_relaxed)) pos = 0;
        else { playing_.store(false); }
      }
      L[i] = (play && pos < len) ? wb->data[pos++] : 0.0f;
    }
    wavPos_.store(pos, std::memory_order_relaxed);
  } else if (in) {
    std::memcpy(L, in, n * sizeof(float));
  } else {
    std::memset(L, 0, n * sizeof(float));
  }
  const float gi = dbToLin(inGain_.load(std::memory_order_relaxed));
  float pin = 0;
  for (int i = 0; i < n; ++i) {
    L[i] *= inSm_.next(gi);
    pin = std::max(pin, std::fabs(L[i]));
  }
  peakIn_.store(std::max(pin, peakIn_.load(std::memory_order_relaxed) * 0.94f), std::memory_order_relaxed);
  const bool tuner = tunerOn_.load(std::memory_order_relaxed);
  if (tuner) tuner_.push(L, n);
  if (g) g->process(L, R, n);
  else std::memcpy(R, L, n * sizeof(float));
  const bool silent = (tuner && tunerMute_.load(std::memory_order_relaxed)) || muted_.load(std::memory_order_relaxed);
  const float go = silent ? 0.0f : dbToLin(outGain_.load(std::memory_order_relaxed));
  float pout = 0;
  for (int i = 0; i < n; ++i) {
    const float g2 = outSm_.next(go);
    float l = L[i] * g2, r = R[i] * g2;
    if (!std::isfinite(l)) l = 0;
    if (!std::isfinite(r)) r = 0;
    pout = std::max(pout, std::max(std::fabs(l), std::fabs(r)));
    outL[i] = clampf(l, -1.0f, 1.0f);
    outR[i] = clampf(r, -1.0f, 1.0f);
  }
  if (metOn_.load(std::memory_order_relaxed) && !muted_.load(std::memory_order_relaxed)) {  // metronome click, mixed after the effects
    const double spb = cfg_.sampleRate * 60.0 / metBpm_.load(std::memory_order_relaxed);  // samples per beat
    const float vol = dbToLin(metVol_.load(std::memory_order_relaxed));
    const int clickLen = (int)(cfg_.sampleRate * 0.03);
    for (int i = 0; i < n; ++i) {
      if (metPhase_ <= 0.0) {
        metPhase_ += spb;
        metBeat_ = (metBeat_ + 1) % 4;
        clickPos_ = 0;
      }
      metPhase_ -= 1.0;
      if (clickPos_ < clickLen) {
        const float f = metBeat_ == 0 ? 1600.0f : 1000.0f;
        const float env = 1.0f - (float)clickPos_ / clickLen;
        const float s = fastSin2pi(f * clickPos_ / cfg_.sampleRate) * env * env * vol;
        outL[i] = clampf(outL[i] + s, -1.0f, 1.0f);
        outR[i] = clampf(outR[i] + s, -1.0f, 1.0f);
        ++clickPos_;
      }
    }
  } else {
    metPhase_ = 0.0;
    metBeat_ = 3;
  }
  peakOut_.store(std::max(pout, peakOut_.load(std::memory_order_relaxed) * 0.94f), std::memory_order_relaxed);
}

// ------------------------------------------------------------ chain model

BlockPtr Engine::newBlock(const std::string& type, std::string& err) {
  const BlockDef* d = findDef(type);
  if (!d) { err = "unknown block type: " + type; return nullptr; }
  auto b = makeBlock(d);
  if (!b) { err = "block type not supported in this build: " + type; return nullptr; }
  b->id = "b" + std::to_string(nextId_++);
  b->prepare(cfg_.sampleRate, cfg_.block);
  return BlockPtr(std::move(b));
}

BlockPtr Engine::findBlock(const std::string& id, Location* loc) {
  for (int i = 0; i < (int)nodes_.size(); ++i) {
    auto& nd = nodes_[i];
    if (nd.block->id == id) { if (loc) *loc = {i, 0, -1}; return nd.block; }
    for (int l = 1; l <= 2; ++l) {
      auto& lane = l == 1 ? nd.a : nd.b;
      for (int k = 0; k < (int)lane.size(); ++k)
        if (lane[k]->id == id) { if (loc) *loc = {i, l, k}; return lane[k]; }
    }
  }
  return nullptr;
}

std::vector<BlockPtr>* Engine::laneOf(const Location& loc) {
  if (loc.node < 0 || loc.lane == 0) return nullptr;
  return loc.lane == 1 ? &nodes_[loc.node].a : &nodes_[loc.node].b;
}

void Engine::rebuild() {
  graph_.collect();
  graph_.set(new Graph(nodes_, cfg_.block, cfg_.sampleRate));
  changed();
}

json Engine::chainJson() const {
  json arr = json::array();
  for (auto& nd : nodes_) {
    json j = nd.block->toJson();
    if (nd.isSplit()) {
      json a = json::array(), b = json::array();
      for (auto& x : nd.a) a.push_back(x->toJson());
      for (auto& x : nd.b) b.push_back(x->toJson());
      j["a"] = a;
      j["b"] = b;
    }
    arr.push_back(j);
  }
  return arr;
}

bool Engine::chainFromJson(const json& chain, std::string& err) {
  std::vector<Node> nodes;
  int maxId = 0;
  auto mk = [&](const json& j) -> BlockPtr {
    std::string e;
    const std::string type = j.value("type", "");
    BlockPtr b;
    if (findDef(type)) b = newBlock(type, e);
    if (!b && !type.empty()) {  // type not available here (missing type file / LV2 plugin): keep a placeholder
      auto mb = makeBlock(missingDef(type));
      mb->id = "b" + std::to_string(nextId_++);
      mb->prepare(cfg_.sampleRate, cfg_.block);
      mb->setRaw(j);
      mb->fileError = "type not available: " + type;
      b = BlockPtr(std::move(mb));
    }
    if (!b) { err = e.empty() ? "block without type" : e; return nullptr; }
    const std::string id = j.value("id", "");
    if (!id.empty()) {
      b->id = id;
      if (id.size() > 1 && id[0] == 'b') maxId = std::max(maxId, std::atoi(id.c_str() + 1));
    }
    b->fromJson(j);
    const std::string file = j.value("file", "");
    if (!file.empty() && !b->def->fileKind.empty()) {
      b->file = file;
      std::string fe;
      if (!b->loadFile(dataPath(b->def->fileKind) + "/" + file, fe)) b->fileError = fe;
    }
    return b;
  };
  for (auto& j : chain) {
    Node nd;
    nd.block = mk(j);
    if (!nd.block) return false;
    if (nd.isSplit()) {
      for (auto& x : j.value("a", json::array())) { auto b = mk(x); if (!b) return false; if (b->def->type != "split") nd.a.push_back(b); }
      for (auto& x : j.value("b", json::array())) { auto b = mk(x); if (!b) return false; if (b->def->type != "split") nd.b.push_back(b); }
    }
    nodes.push_back(std::move(nd));
  }
  nodes_ = std::move(nodes);
  nextId_ = std::max(nextId_, maxId + 1);
  rebuild();
  return true;
}

float Engine::cpuEstimate() const {
  float s = 0;
  auto add = [&](const BlockPtr& b) {
    if (!b->on.load()) return;
    const float meas = b->load.load() * 100.0f;
    if (meas > 0.01f) { s += meas; return; }  // measured while running
    float c = b->def->cpu;
    if (b->def->type == "nam") {
      // A35 estimate from R36S measurements: A2 Lite ~22 %, A2 Full ~100 % (does not fit), other WaveNets ~60 %+
      const json inf = b->info();
      if (b->file.empty()) c = 0;
      else if (inf.value("slimmable", false)) c = b->get(2) > 0.5f ? 100.0f : 22.0f;
      else c = 60.0f;
    }
    if (b->def->type == "ir") c *= std::max(0.2f, b->get(0) / 100.0f);
    s += c;
  };
  for (auto& nd : nodes_) {
    add(nd.block);
    for (auto& b : nd.a) add(b);
    for (auto& b : nd.b) add(b);
  }
  return s;
}

json Engine::filesJson() const {
  json out = json::object();
  auto list = [&](const char* sub, std::vector<std::string> exts) {
    json arr = json::array();
    std::vector<std::string> names;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dataPath(sub), ec)) {
      if (!e.is_regular_file()) continue;
      std::string ext = e.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (std::find(exts.begin(), exts.end(), ext) != exts.end()) names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    for (auto& n : names) arr.push_back(n);
    out[sub] = arr;
  };
  list("nam", {".nam"});
  list("ir", {".wav"});
  list("presets", {".json"});
  list("wav", {".wav"});
  list("fx", {".json"});
  return out;
}

void Engine::applyScene(int idx) {
  if (idx < 0 || idx >= (int)scenes_.size()) return;
  const json& on = scenes_[idx]["on"];
  for (auto it = on.begin(); it != on.end(); ++it) {
    BlockPtr b = findBlock(it.key());
    if (b) b->on.store(it.value().get<bool>());
  }
  scene_ = idx;
}

// ------------------------------------------------------------ control

json Engine::state() {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  return {{"t", "state"},
          {"version", version_.load()},
          {"preset", {{"name", presetName_}, {"file", presetFile_}}},
          {"chain", chainJson()},
          {"scenes", scenes_},
          {"scene", scene_},
          {"globals", {{"input_gain", inGain_.load()}, {"output_gain", outGain_.load()}, {"tuner_mute", tunerMute_.load()}, {"a4", a4_}}},
          {"input", {{"source", useWav_.load() ? "wav" : "live"}, {"file", wavFile_}, {"loop", loop_.load()}}},
          {"cpu_est", cpuEstimate()},
          {"lang", i18n().code()},
          {"langs", I18n::available(cfg_.langDir)},
          {"ui", {{"theme", settings_.value("theme", "dark")}, {"view", settings_.value("view", "tiles")}}},
          {"catalog_version", catalogVersion()},
          {"hotspot_cfg", settings_.value("hotspot", json::object())},
          {"network", extState_ ? extState_() : json::object()},
          {"audio_dev", audioState_ ? audioState_() : json::object()},
          {"muted", muted_.load()},
          {"metronome", {{"on", metOn_.load()}, {"bpm", metBpm_.load()}, {"volume", metVol_.load()}}},
          {"can_undo", !undo_.empty()},
          {"can_redo", !redo_.empty()},
          {"lv2", lv2Available()},
          {"files", filesJson()},
          {"audio", {{"rate", cfg_.sampleRate}, {"block", cfg_.block}}}};
}

json Engine::status() {
  const float pi = peakIn_.load(), po = peakOut_.load();
  json s = {{"t", "status"},
            {"cpu", cpu_.load() * 100.0f},
            {"in_db", linToDb(pi)},
            {"out_db", linToDb(po)},
            {"in_l_db", linToDb(rawL_.load())},
            {"in_r_db", linToDb(rawR_.load())},
            {"muted", muted_.load()},
            {"xruns", xruns_.load()},
            {"playing", playing_.load()},
            {"wav_pos", wavPos_.load() / cfg_.sampleRate},
            {"wav_len", wavLen_.load() / cfg_.sampleRate},
            {"tuner_on", tunerOn_.load()}};
  {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    json loads = json::object();
    auto put = [&](const BlockPtr& b) { if (b->def->type != "split") loads[b->id] = std::round(b->load.load() * 1000.0) / 10.0; };
    for (auto& nd : nodes_) {
      put(nd.block);
      for (auto& b : nd.a) put(b);
      for (auto& b : nd.b) put(b);
    }
    s["loads"] = loads;
  }
  if (tunerOn_.load()) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (tunerRes_.valid)
      s["tuner"] = {{"note", tunerRes_.note}, {"octave", tunerRes_.octave}, {"cents", tunerRes_.cents}, {"freq", tunerRes_.freq}};
    else
      s["tuner"] = nullptr;
  }
  return s;
}

void Engine::controlTick() {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  graph_.collect();
  wav_.collect();
  for (auto& nd : nodes_) {
    nd.block->control();
    for (auto& b : nd.a) b->control();
    for (auto& b : nd.b) b->control();
  }
  if (tunerOn_.load()) tunerRes_ = tuner_.analyze(a4_);
  if (++ticks_ % 40 == 0) updateMeasured();
  if (ticks_ % 1200 == 0 && measuredDirty_) {
    json cs = json::object();
    for (auto& [k, v] : measured_) cs[k] = std::round(v * 10.0f) / 10.0f;
    std::ofstream(dataPath("cpu_stats.json")) << cs.dump(1);
    measuredDirty_ = false;
  }
}

bool Engine::loadPresetFile(const std::string& file, std::string& err) {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  if (!safeName(file)) { err = "invalid file name"; return false; }
  std::ifstream f(dataPath("presets") + "/" + file);
  if (!f) { err = "preset not found"; return false; }
  json j = json::parse(f, nullptr, false);
  if (j.is_discarded()) { err = "invalid JSON"; return false; }
  if (!chainFromJson(j.value("chain", json::array()), err)) return false;
  presetName_ = j.value("name", file);
  presetFile_ = file;
  std::ofstream(cfg_.dataDir + "/last_preset.txt") << file;
  inGain_.store(j.value("input_gain", 0.0f));
  outGain_.store(j.value("output_gain", 0.0f));
  scenes_ = j.value("scenes", json::array());
  scene_ = -1;
  const int sc = j.value("scene", -1);
  if (sc >= 0) applyScene(sc);
  changed();
  return true;
}

bool Engine::setWavInput(const std::string& file, bool loop, std::string& err) {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  if (!safeName(file)) { err = "invalid file name"; return false; }
  WavData w;
  if (!readWav(dataPath("wav") + "/" + file, w, err)) return false;
  auto* wb = new WavBuf();
  wb->data = resample(toMono(w), w.sampleRate, (int)cfg_.sampleRate);
  wavLen_.store(wb->data.size());
  wavPos_.store(0);
  wav_.set(wb);
  wavFile_ = file;
  loop_.store(loop);
  useWav_.store(true);
  changed();
  return true;
}

json Engine::handle(const json& cmd) {
  std::lock_guard<std::recursive_mutex> lk(mtx_);
  try {
    const std::string name = cmd.value("cmd", "");
    if (name == "undo" || name == "redo") {
      auto& from = name == "undo" ? undo_ : redo_;
      auto& to = name == "undo" ? redo_ : undo_;
      if (from.empty()) return {{"ok", false}, {"code", "err.nothing_to_undo"}, {"error", "Nothing to undo"}};
      const json now = chainJson();
      const json target = from.back();
      std::string err;
      if (!chainFromJson(target, err)) return {{"ok", false}, {"code", "err.file"}, {"error", err}};
      from.pop_back();
      to.push_back(now);
      changed();
      return {{"ok", true}};
    }
    // structural edits can be undone (parameter tweaks are not recorded: too frequent)
    static const std::set<std::string> kUndoable = {"add_block", "add_split", "remove", "move", "move_to_lane", "set_on", "set_file"};
    const bool undoable = kUndoable.count(name) > 0;
    const json before = undoable ? chainJson() : json();
    json r = handleLocked(cmd);
    if (undoable && r.value("ok", false)) {
      undo_.push_back(before);
      if (undo_.size() > 30) undo_.erase(undo_.begin());
      redo_.clear();
    } else if (name == "preset_load" || name == "preset_new") {
      if (r.value("ok", false)) { undo_.clear(); redo_.clear(); }
    }
    return r;
  } catch (const std::exception& e) {
    return {{"ok", false}, {"error", e.what()}};
  }
}

json Engine::handleLocked(const json& c) {
  const std::string cmd = c.value("cmd", "");
  auto ok = [](json extra = json::object()) { extra["ok"] = true; return extra; };
  // errors carry a translatable code; "error" is the English text (+ detail)
  auto fail = [](const std::string& code, const std::string& detail = "") {
    static const std::map<std::string, std::string> en = {
        {"err.block_not_found", "Block not found"}, {"err.unknown_param", "Unknown parameter"},
        {"err.invalid_name", "Invalid file name"}, {"err.nothing_to_undo", "Nothing to undo"}, {"err.split_not_found", "Split not found"},
        {"err.position_not_found", "Position not found"}, {"err.cannot_move", "Cannot move further"},
        {"err.preset_write", "Cannot write preset"}, {"err.scene_not_found", "Scene not found"},
        {"err.unknown_key", "Unknown setting"}, {"err.unknown_command", "Unknown command"},
        {"err.lang", "Language not available"}, {"err.file", "File error"},
        {"err.unknown_type", "Unknown block type"}, {"err.net_mode", "Invalid hotspot mode"},
        {"err.net_ssid", "Network name must have 1-32 characters"}, {"err.net_password", "Password must have 8-63 characters (letters, digits, symbols)"},
        {"err.net_unavailable", "Network control not available"},
        {"err.audio_device", "Invalid audio device"}, {"err.audio_channel", "Invalid input channel"}};
    auto it = en.find(code);
    std::string msg = it != en.end() ? it->second : code;
    if (!detail.empty()) msg += ": " + detail;
    json j = {{"ok", false}, {"code", code}, {"error", msg}};
    if (!detail.empty()) j["detail"] = detail;
    return j;
  };

  if (cmd == "state") return state();
  if (cmd == "status") return status();
  if (cmd == "catalog") return {{"t", "catalog"}, {"ok", true}, {"catalog", catalogJson()}, {"catalog_version", catalogVersion()}};
  if (cmd == "rescan") { changed(); return ok(); }
  if (cmd == "fx_rescan") { catalogRescan(); changed(); return ok({{"catalog_version", catalogVersion()}}); }
  if (cmd == "catalog_log") return ok({{"log", catalogLog()}, {"lv2_path", lv2Path(dataPath("lv2"))}, {"lv2", lv2Available()}});
  if (cmd == "engines") return ok({{"engines", enginesJson()}});
  if (cmd == "net_set") {  // hotspot settings
    json h = settings_.value("hotspot", json::object());
    if (c.contains("mode")) {
      const std::string m = c.value("mode", "");
      if (m != "auto" && m != "always" && m != "off") return fail("err.net_mode", m);
      h["mode"] = m;
    }
    auto printable = [](const std::string& v) {
      for (unsigned char ch : v) if (ch < 0x20 || ch == 0x7F) return false;
      return true;
    };
    if (c.contains("ssid")) {
      const std::string v = c.value("ssid", "");
      if (v.empty() || v.size() > 32 || !printable(v)) return fail("err.net_ssid");
      h["ssid"] = v;
    }
    if (c.contains("password")) {
      const std::string v = c.value("password", "");
      bool ascii = true;
      for (unsigned char ch : v) if (ch < 0x20 || ch > 0x7E) ascii = false;
      if (v.size() < 8 || v.size() > 63 || !ascii) return fail("err.net_password");
      h["password"] = v;
    }
    settings_["hotspot"] = h;
    saveSettings();
    changed();
    return ok();
  }
  if (cmd == "audio_set") {  // which sound card / input channel to use (applied by the main loop)
    json a = settings_.value("audio", json::object());
    if (c.contains("device")) {
      const std::string d = c["device"].is_string() ? c["device"].get<std::string>() : "";
      bool good = d == "auto" || d == "internal";
      if (!good && d.rfind("card:", 0) == 0 && d.size() > 5 && d.size() <= 37) {
        good = true;
        for (size_t i = 5; i < d.size(); ++i)
          if (!std::isalnum((unsigned char)d[i]) && d[i] != '_' && d[i] != '-') good = false;
      }
      if (!good) return fail("err.audio_device", d);
      a["device"] = d;
    }
    if (c.contains("channel")) {
      const std::string ch = c["channel"].is_string() ? c["channel"].get<std::string>() : "";
      if (ch != "auto" && ch != "left" && ch != "right" && ch != "sum") return fail("err.audio_channel", ch);
      a["channel"] = ch;
    }
    settings_["audio"] = a;
    saveSettings();
    if (extCmd_) extCmd_({{"cmd", "audio_apply"}});
    changed();
    return ok();
  }
  if ((cmd.rfind("net_", 0) == 0 || cmd.rfind("audio_", 0) == 0) && extCmd_) return extCmd_(c);
  if (cmd == "set_ui") {
    for (const char* k : {"theme", "view"})
      if (c.contains(k) && c[k].is_string() && safeName(c[k].get<std::string>())) settings_[k] = c[k];
    saveSettings();
    changed();
    return ok();
  }
  if (cmd == "set_lang") {
    const std::string code = c.value("lang", "en");
    if (!safeName(code) || !i18n().load(cfg_.langDir, code)) return fail("err.lang", code);
    settings_["lang"] = code;
    saveSettings();
    changed();
    return ok();
  }

  if (cmd == "set_param") {
    BlockPtr b = findBlock(c.value("id", ""));
    if (!b) return fail("err.block_not_found");
    if (!b->setByName(c.value("param", ""), c.value("value", 0.0f))) return fail("err.unknown_param");
    changed();
    return ok();
  }
  if (cmd == "set_on") {
    BlockPtr b = findBlock(c.value("id", ""));
    if (!b) return fail("err.block_not_found");
    b->on.store(c.value("on", true));
    changed();
    return ok();
  }
  if (cmd == "set_file") {
    BlockPtr b = findBlock(c.value("id", ""));
    if (!b || b->def->fileKind.empty()) return fail("err.block_not_found");
    const std::string file = c.value("file", "");
    if (!safeName(file)) return fail("err.invalid_name");
    std::string err;
    if (!b->loadFile(dataPath(b->def->fileKind) + "/" + file, err)) {
      b->fileError = err;
      changed();
      return fail("err.file", err);
    }
    b->file = file;
    b->fileError.clear();
    changed();
    return ok();
  }
  if (cmd == "add_block" || cmd == "add_split") {
    const std::string type = cmd == "add_split" ? "split" : c.value("type", "");
    std::string err;
    BlockPtr b = newBlock(type, err);
    if (!b) return fail("err.unknown_type", type);
    const bool isSplit = type == "split";
    Node nd;
    nd.block = b;
    const std::string after = c.value("after", "");
    if (c.contains("lane") && c["lane"].is_object() && !isSplit) {  // to the start of a path
      Location sl;
      BlockPtr s = findBlock(c["lane"].value("split", ""), &sl);
      if (!s || sl.lane != 0 || !nodes_[sl.node].isSplit()) return fail("err.split_not_found");
      auto& lane = c["lane"].value("lane", "a") == "b" ? nodes_[sl.node].b : nodes_[sl.node].a;
      lane.insert(lane.begin(), b);
    } else if (!after.empty()) {
      Location loc;
      if (!findBlock(after, &loc)) return fail("err.position_not_found");
      if (loc.lane != 0 && !isSplit) {
        auto* lane = laneOf(loc);
        lane->insert(lane->begin() + loc.index + 1, b);
      } else {
        nodes_.insert(nodes_.begin() + loc.node + 1, nd);
      }
    } else if (c.value("start", false)) {
      nodes_.insert(nodes_.begin(), nd);
    } else {
      nodes_.push_back(nd);
    }
    rebuild();
    return ok({{"id", b->id}});
  }
  if (cmd == "remove") {
    Location loc;
    if (!findBlock(c.value("id", ""), &loc)) return fail("err.block_not_found");
    if (loc.lane != 0) {
      auto* lane = laneOf(loc);
      lane->erase(lane->begin() + loc.index);
    } else {
      Node nd = nodes_[loc.node];
      nodes_.erase(nodes_.begin() + loc.node);
      if (nd.isSplit())  // blocks of path A stay in the main chain
        for (int k = (int)nd.a.size() - 1; k >= 0; --k) nodes_.insert(nodes_.begin() + loc.node, Node{nd.a[k], {}, {}});
    }
    rebuild();
    return ok();
  }
  if (cmd == "move") {
    Location loc;
    if (!findBlock(c.value("id", ""), &loc)) return fail("err.block_not_found");
    const int dir = c.value("dir", 1) > 0 ? 1 : -1;
    if (loc.lane != 0) {
      auto* lane = laneOf(loc);
      const int j = loc.index + dir;
      if (j < 0 || j >= (int)lane->size()) return fail("err.cannot_move");
      std::swap((*lane)[loc.index], (*lane)[j]);
    } else {
      const int j = loc.node + dir;
      if (j < 0 || j >= (int)nodes_.size()) return fail("err.cannot_move");
      std::swap(nodes_[loc.node], nodes_[j]);
    }
    rebuild();
    return ok();
  }
  if (cmd == "move_to_lane") {  // move a block into a split path / back to the main chain
    Location loc;
    BlockPtr b = findBlock(c.value("id", ""), &loc);
    if (!b || b->def->type == "split") return fail("err.block_not_found");
    const std::string sid = c.value("split", "");
    const std::string ln = c.value("lane", "main");
    Location sl;
    if (ln != "main" && (!findBlock(sid, &sl) || !nodes_[sl.node].isSplit())) return fail("err.split_not_found");
    if (loc.lane != 0) { auto* lane = laneOf(loc); lane->erase(lane->begin() + loc.index); }
    else nodes_.erase(nodes_.begin() + loc.node);
    if (ln == "main") {
      Location s2;
      if (!sid.empty() && findBlock(sid, &s2)) nodes_.insert(nodes_.begin() + s2.node + 1, Node{b, {}, {}});
      else nodes_.push_back(Node{b, {}, {}});
    } else {
      findBlock(sid, &sl);
      auto& lane = ln == "b" ? nodes_[sl.node].b : nodes_[sl.node].a;
      lane.push_back(b);
    }
    rebuild();
    return ok();
  }
  if (cmd == "preset_load") {
    std::string err;
    if (!loadPresetFile(c.value("file", ""), err)) return fail("err.file", err);
    return ok();
  }
  if (cmd == "preset_save") {
    std::string file = c.value("file", presetFile_);
    const std::string name = c.value("name", presetName_);
    if (file.empty()) file = fileNameFor(name) + ".json";
    if (file.size() < 5 || file.substr(file.size() - 5) != ".json") file += ".json";
    if (!safeName(file)) return fail("err.invalid_name");
    json j = {{"name", name}, {"input_gain", inGain_.load()}, {"output_gain", outGain_.load()},
              {"chain", chainJson()}, {"scenes", scenes_}, {"scene", scene_}};
    for (auto& nd : j["chain"]) {  // runtime info does not belong in a preset
      nd.erase("info"); nd.erase("error");
      for (const char* l : {"a", "b"}) if (nd.contains(l)) for (auto& x : nd[l]) { x.erase("info"); x.erase("error"); }
    }
    std::ofstream f(dataPath("presets") + "/" + file);
    if (!f) return fail("err.preset_write");
    f << j.dump(2);
    presetFile_ = file;
    presetName_ = name;
    std::ofstream(cfg_.dataDir + "/last_preset.txt") << file;
    changed();
    return ok({{"file", file}});
  }
  if (cmd == "preset_new") {
    nodes_.clear();
    scenes_ = json::array();
    scene_ = -1;
    presetName_ = c.value("name", T("ui.new_preset", "New preset"));
    presetFile_.clear();
    rebuild();
    return ok();
  }
  if (cmd == "preset_rename") {
    presetName_ = c.value("name", presetName_);
    changed();
    return ok();
  }
  if (cmd == "scene_store") {
    int idx = c.value("index", -1);
    json on = json::object();
    for (auto& nd : nodes_) {
      on[nd.block->id] = nd.block->on.load();
      for (auto& b : nd.a) on[b->id] = b->on.load();
      for (auto& b : nd.b) on[b->id] = b->on.load();
    }
    if (idx < 0 || idx >= (int)scenes_.size()) {
      scenes_.push_back({{"name", c.value("name", fmtArgs(T("ui.scene_n", "Scene {0}"), {std::to_string(scenes_.size() + 1)}))}, {"on", on}});
      idx = (int)scenes_.size() - 1;
    } else {
      scenes_[idx]["on"] = on;
      if (c.contains("name")) scenes_[idx]["name"] = c["name"];
    }
    scene_ = idx;
    changed();
    return ok({{"index", idx}});
  }
  if (cmd == "scene_select") {
    applyScene(c.value("index", -1));
    changed();
    return ok();
  }
  if (cmd == "scene_remove") {
    const int idx = c.value("index", -1);
    if (idx < 0 || idx >= (int)scenes_.size()) return fail("err.scene_not_found");
    scenes_.erase(scenes_.begin() + idx);
    scene_ = -1;
    changed();
    return ok();
  }
  if (cmd == "set_global") {
    const std::string k = c.value("key", "");
    if (k == "input_gain") inGain_.store(clampf(c.value("value", 0.0f), -24, 24));
    else if (k == "output_gain") outGain_.store(clampf(c.value("value", 0.0f), -60, 12));
    else if (k == "tuner_mute") tunerMute_.store(c.value("value", true));
    else if (k == "a4") a4_ = clampf(c.value("value", 440.0f), 430, 450);
    else return fail("err.unknown_key");
    changed();
    return ok();
  }
  if (cmd == "mute") {  // panic mute: the output fades to silence (outSm_), "on" omitted = toggle
    muted_.store(c.contains("on") ? c.value("on", true) : !muted_.load());
    changed();
    return ok();
  }
  if (cmd == "metronome") {  // {on?, bpm 30..300, volume dB -40..0}
    if (c.contains("bpm")) metBpm_.store(clampf(c.value("bpm", 100.0f), 30.0f, 300.0f));
    if (c.contains("volume")) metVol_.store(clampf(c.value("volume", -12.0f), -40.0f, 0.0f));
    if (c.contains("on")) metOn_.store(c.value("on", false));
    changed();
    return ok();
  }
  if (cmd == "tuner") {
    tunerOn_.store(c.value("on", true));
    changed();
    return ok();
  }
  if (cmd == "input") {
    const std::string src = c.value("source", "live");
    if (src == "wav") {
      std::string err;
      const std::string file = c.value("file", wavFile_);
      if (file != wavFile_ || !useWav_.load()) {
        if (!setWavInput(file, c.value("loop", true), err)) return fail("err.file", err);
      }
      loop_.store(c.value("loop", loop_.load()));
      useWav_.store(true);
      if (c.contains("play")) playing_.store(c["play"].get<bool>());
    } else {
      useWav_.store(false);
      playing_.store(false);
    }
    changed();
    return ok();
  }
  if (cmd == "transport") {
    if (c.contains("play")) playing_.store(c["play"].get<bool>());
    if (c.value("rewind", false)) wavPos_.store(0);
    return ok();
  }
  return fail("err.unknown_command", cmd);
}

}  // namespace fx
