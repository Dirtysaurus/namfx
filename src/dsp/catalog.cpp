// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
//
// Block catalog: built-in types + effect types (fx/*.json, data/fx/*.json) + LV2 plugins.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include "dsp/blocks.h"
#include "dsp/engines.h"
#include "dsp/lv2_host.h"

namespace fs = std::filesystem;

namespace fx {

std::vector<BlockDef> builtinDefs();  // blocks.cpp

namespace {

struct Registry {
  std::mutex mtx;
  std::vector<std::unique_ptr<BlockDef>> all;          // never shrinks: pointers stay valid
  std::map<std::string, const BlockDef*> current;     // type -> newest definition
  std::map<std::string, const BlockDef*> missing;
  std::map<std::string, float> measured;
  std::map<std::string, std::string> fileStamp;       // type file -> mtime+size (rescan)
  CatalogPaths paths;
  json log = json::array();
  unsigned version = 1;
  std::string cache;
  unsigned cacheVersion = 0;
  bool lv2Scanned = false;
};

Registry& reg() {
  static Registry r;
  return r;
}

void addDef(Registry& r, std::unique_ptr<BlockDef> d) {
  r.current[d->type] = d.get();
  r.missing.erase(d->type);
  r.all.push_back(std::move(d));
}

std::string stampOf(const fs::path& p) {
  std::error_code ec;
  const auto t = fs::last_write_time(p, ec).time_since_epoch().count();
  return std::to_string(t) + ":" + std::to_string(fs::file_size(p, ec));
}

// Loads all *.json in dir. Bundled types are translated via web/lang.
void loadTypeDir(Registry& r, const std::string& dir, bool bundled) {
  std::error_code ec;
  if (dir.empty() || !fs::is_directory(dir, ec)) return;
  std::vector<fs::path> files;
  for (auto& e : fs::directory_iterator(dir, ec))
    if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    const std::string st = stampOf(f);
    if (r.fileStamp[f.string()] == st) continue;  // unchanged since the last scan
    r.fileStamp[f.string()] = st;
    std::ifstream in(f);
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded()) { r.log.push_back({{"file", f.filename().string()}, {"error", "invalid JSON"}}); continue; }
    json types = j.is_array() ? j : j.contains("types") ? j["types"] : json::array({j});
    const std::string pack = j.is_object() ? j.value("pack", f.stem().string()) : f.stem().string();
    const std::string author = j.is_object() ? j.value("author", "") : "";
    const std::string license = j.is_object() ? j.value("license", "") : "";
    int ok = 0;
    for (auto& t : types) {
      auto d = std::make_unique<BlockDef>();
      d->pack = pack;
      d->author = author;
      d->license = license;
      d->bundled = bundled;
      std::string err;
      if (!parseTypeDef(t, *d, err)) { r.log.push_back({{"file", f.filename().string()}, {"error", err}}); continue; }
      auto it = r.current.find(d->type);
      if (it != r.current.end() && (it->second->source != "fx" || (it->second->bundled && !bundled))) {
        r.log.push_back({{"file", f.filename().string()}, {"error", d->type + ": id already used by " + it->second->source + " type"}});
        continue;
      }
      addDef(r, std::move(d));
      ++ok;
    }
    r.log.push_back({{"file", f.filename().string()}, {"types", ok}});
  }
}

}  // namespace

void catalogInit(const CatalogPaths& p) {
  auto& r = reg();
  std::lock_guard<std::mutex> lk(r.mtx);
  r.paths = p;
  if (r.all.empty())
    for (auto& d : builtinDefs()) addDef(r, std::make_unique<BlockDef>(std::move(d)));
  loadTypeDir(r, p.fxDir, true);
  loadTypeDir(r, p.userFxDir, false);
  if (p.lv2 && !r.lv2Scanned) {
    r.lv2Scanned = true;
    std::vector<std::unique_ptr<BlockDef>> found;
    lv2Scan(p.userLv2Dir, found, r.log);
    for (auto& d : found)
      if (!r.current.count(d->type)) addDef(r, std::move(d));
  }
  ++r.version;
}

void catalogRescan() {
  auto& r = reg();
  std::lock_guard<std::mutex> lk(r.mtx);
  loadTypeDir(r, r.paths.fxDir, true);
  loadTypeDir(r, r.paths.userFxDir, false);
  ++r.version;
}

const BlockDef* findDef(const std::string& type) {
  auto& r = reg();
  std::lock_guard<std::mutex> lk(r.mtx);
  if (r.all.empty()) {  // used without catalogInit (tools, tests): built-ins only
    for (auto& d : builtinDefs()) addDef(r, std::make_unique<BlockDef>(std::move(d)));
  }
  auto it = r.current.find(type);
  return it != r.current.end() ? it->second : nullptr;
}

const BlockDef* missingDef(const std::string& type) {
  auto& r = reg();
  std::lock_guard<std::mutex> lk(r.mtx);
  auto it = r.missing.find(type);
  if (it != r.missing.end()) return it->second;
  auto d = std::make_unique<BlockDef>();
  d->type = type;
  d->name = "Missing: " + (type.rfind("lv2:", 0) == 0 ? type.substr(type.find_last_of("/#:") + 1) : type);
  d->shortName = "???";
  d->cat = "util";
  d->desc = "Type not available on this system - the block passes the signal through";
  d->source = "missing";
  d->bundled = false;
  d->cpu = 0;
  const BlockDef* p = d.get();
  r.missing[type] = p;
  r.all.push_back(std::move(d));
  ++r.version;
  return p;
}

std::vector<const BlockDef*> catalogList() {
  auto& r = reg();
  std::lock_guard<std::mutex> lk(r.mtx);
  std::vector<const BlockDef*> out;
  for (auto& [k, d] : r.current) out.push_back(d);
  // stable, friendly order: built-ins, bundled types, user types, LV2
  auto rank = [](const BlockDef* d) { return d->source == "builtin" ? 0 : d->source == "fx" ? (d->bundled ? 1 : 2) : 3; };
  std::stable_sort(out.begin(), out.end(), [&](const BlockDef* a, const BlockDef* b) {
    if (rank(a) != rank(b)) return rank(a) < rank(b);
    return a->name < b->name;
  });
  return out;
}

json defJson(const BlockDef& d) {
  json ps = json::array();
  for (auto& p : d.params) {
    json pj = {{"id", p.id}, {"name", p.name}, {"min", p.min}, {"max", p.max}, {"def", p.def}, {"unit", p.unit},
               {"kind", p.kind == PKind::Float ? "float" : p.kind == PKind::Enum ? "enum" : "bool"}, {"log", p.log}};
    if (!p.options.empty()) pj["options"] = p.options;
    if (p.step > 0) pj["step"] = p.step;
    if (p.hidden) pj["hidden"] = true;
    ps.push_back(pj);
  }
  json j = {{"type", d.type}, {"name", d.name}, {"short", d.shortName}, {"cat", d.cat}, {"desc", d.desc},
            {"file", d.fileKind}, {"cpu", d.cpu}, {"params", ps}, {"source", d.source}, {"stereo", d.stereo}};
  if (!d.pack.empty()) j["pack"] = d.pack;
  if (!d.author.empty()) j["author"] = d.author;
  if (!d.license.empty()) j["license"] = d.license;
  if (!d.bundled) j["user"] = true;
  if (!d.tr.empty()) j["tr"] = d.tr;
  return j;
}

json catalogJson() {
  auto& r = reg();
  std::vector<const BlockDef*> list = catalogList();
  std::lock_guard<std::mutex> lk(r.mtx);
  json arr = json::array();
  for (auto* d : list) {
    json j = defJson(*d);
    auto m = r.measured.find(d->type);
    if (m != r.measured.end()) j["cpu_measured"] = m->second;
    arr.push_back(j);
  }
  for (auto& [k, d] : r.missing) arr.push_back(defJson(*d));
  return arr;
}

unsigned catalogVersion() { return reg().version; }

json catalogLog() {
  auto& r = reg();
  std::lock_guard<std::mutex> lk(r.mtx);
  return r.log;
}

void catalogSetMeasured(const std::string& type, float pct) {
  auto& r = reg();
  std::lock_guard<std::mutex> lk(r.mtx);
  r.measured[type] = pct;
}

}  // namespace fx
