// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "ui/screens.h"
#include <algorithm>
#include <cmath>
#include "ui/qr/qrcodegen.hpp"
#include <cstdio>
#include <fstream>
#include "i18n.h"

namespace ui {

using fx::json;
using fx::T;
using fx::fmtArgs;

// Theme palette (set by applyTheme). Category colours stay the same in every theme.
namespace col {
Color bg0, bg1, bar, line, panel, text, text2, dim, faint, dark, knobBg, tile, card, ring, tuneBg, tuneGlow, toastBg;
Color laneA = rgb(0xFF7A1A), laneB = rgb(0x5CC8FF), ok = rgb(0x22E0A0), warn = rgb(0xFFE14D), err = rgb(0xFF3B5C);
}  // namespace col

static const char* kThemes[] = {"dark", "light", "contrast", "retro"};

static void applyTheme(const std::string& name) {
  using namespace col;
  dark = rgb(0x0B0C0F);  // text on coloured tiles
  if (name == "light") {
    bg0 = rgb(0xF4F5F7); bg1 = rgb(0xDDE1E7); bar = rgb(0xFFFFFF); line = rgb(0xC9CED6); panel = rgb(0xFFFFFF);
    text = rgb(0x14171C); text2 = rgb(0x2B313A); dim = rgb(0x5B6470); faint = rgb(0x8A929C); knobBg = rgb(0xE6E9EE);
    tile = rgb(0xE9ECF0); card = rgb(0xF0F2F5); ring = rgb(0xB8BEC7); tuneBg = rgb(0xE8EBEF); tuneGlow = rgb(0xD9F3E6); toastBg = rgb(0xFFE4E8);
    warn = rgb(0xB88A00); ok = rgb(0x0FA872);
  } else if (name == "contrast") {
    bg0 = rgb(0x000000); bg1 = rgb(0x000000); bar = rgb(0x000000); line = rgb(0x8A8A8A); panel = rgb(0x0A0A0A);
    text = rgb(0xFFFFFF); text2 = rgb(0xFFFFFF); dim = rgb(0xE6E6E6); faint = rgb(0xBDBDBD); knobBg = rgb(0x1A1A1A);
    tile = rgb(0x050505); card = rgb(0x111111); ring = rgb(0xFFFFFF); tuneBg = rgb(0x000000); tuneGlow = rgb(0x002A14); toastBg = rgb(0x3A0008);
    warn = rgb(0xFFE14D); ok = rgb(0x22E0A0);
  } else if (name == "retro") {
    bg0 = rgb(0x2A1D0A); bg1 = rgb(0x0E0904); bar = rgb(0x1A1206); line = rgb(0x4A3512); panel = rgb(0x1E1508);
    text = rgb(0xFFC46B); text2 = rgb(0xF0B050); dim = rgb(0xC08A3A); faint = rgb(0x8C6428); knobBg = rgb(0x2A1E0C);
    tile = rgb(0x160F05); card = rgb(0x241A0B); ring = rgb(0x6A4C1C); tuneBg = rgb(0x120C04); tuneGlow = rgb(0x2E2408); toastBg = rgb(0x3A1408);
    warn = rgb(0xFFE14D); ok = rgb(0x9BE36A);
  } else {
    bg0 = rgb(0x1C1F27); bg1 = rgb(0x0B0C0F); bar = rgb(0x13151A); line = rgb(0x2A2F37); panel = rgb(0x181B20);
    text = rgb(0xF2F4F7); text2 = col::text2; dim = rgb(0x9AA3AE); faint = rgb(0x6B737D); knobBg = rgb(0x22262D);
    tile = col::tile; card = col::card; ring = col::ring; tuneBg = col::tuneBg; tuneGlow = col::tuneGlow; toastBg = col::toastBg;
    warn = rgb(0xFFE14D); ok = rgb(0x22E0A0);
  }
}

static Color catColor(const std::string& cat) {
  if (cat == "dyn") return rgb(0x22E0A0);
  if (cat == "drive") return rgb(0xFF3B5C);
  if (cat == "amp") return rgb(0xFFB300);
  if (cat == "cab") return rgb(0xB8F542);
  if (cat == "eq") return rgb(0x2F9BFF);
  if (cat == "mod") return rgb(0x9B5CFF);
  if (cat == "dly") return rgb(0x00E5FF);
  if (cat == "rev") return rgb(0xFF4FD8);
  if (cat == "route") return rgb(0xFF7A1A);
  return rgb(0xA0AEC0);
}

// category keys + English names (translated via cat.<key>)
static const std::vector<std::pair<std::string, std::string>> kCats = {
    {"dyn", "Dynamics"}, {"drive", "Drive"}, {"amp", "Amp (NAM)"}, {"cab", "Cab (IR)"}, {"eq", "EQ / filter"},
    {"mod", "Modulation"}, {"dly", "Delay"}, {"rev", "Reverb"}, {"util", "Utility"}, {"route", "Routing"}};
static std::string catName(size_t k) { return T("cat." + kCats[k].first, kCats[k].second); }

// Catalog with names, descriptions, parameter names and options translated
// (keys fx.<type>, fx.<type>.desc, p.<type>.<param>, o.<type>.<param>.<i>).
// Type files may carry their own translations: "tr": {"cs": {"name", "desc", "params": {id: {"name", "options"}}}}.
static json localizedCatalog(const std::string& lang) {
  json cat = fx::catalogJson();
  for (auto& d : cat) {
    const std::string t = d["type"];
    const json tr = d.contains("tr") && d["tr"].contains(lang) ? d["tr"][lang] : json::object();
    d["name"] = T("fx." + t, tr.value("name", d["name"].get<std::string>()));
    d["desc"] = T("fx." + t + ".desc", tr.value("desc", d["desc"].get<std::string>()));
    const json trp = tr.value("params", json::object());
    for (auto& p : d["params"]) {
      const std::string pid = p["id"];
      const json tp = trp.value(pid, json::object());
      p["name"] = T("p." + t + "." + pid, tp.value("name", p["name"].get<std::string>()));
      if (p.contains("options"))
        for (size_t i = 0; i < p["options"].size(); ++i) {
          std::string en = p["options"][i];
          if (tp.contains("options") && i < tp["options"].size()) en = tp["options"][i];
          p["options"][i] = T("o." + t + "." + pid + "." + std::to_string(i), en);
        }
    }
  }
  return cat;
}

Screens::Screens(fx::Engine& e, const std::string& fontDir) : eng_(e) {
  applyTheme("dark");
  sans_.load(fontDir + "/DejaVuSans.ttf");
  bold_.load(fontDir + "/DejaVuSans-Bold.ttf");
  mono_.load(fontDir + "/DejaVuSansMono-Bold.ttf");
  status_ = eng_.status();  // the first frame is drawn before the first status refresh
  refresh();
}

void Screens::refresh() {
  state_ = eng_.state();
  seenVersion_ = state_.value("version", 0u);
  const unsigned cv = state_.value("catalog_version", 0u);
  if (catalog_.is_null() || state_.value("lang", "") != lang_ || cv != catalogVersion_) {  // language or catalog changed
    lang_ = state_.value("lang", "");
    catalogVersion_ = cv;
    catalog_ = localizedCatalog(lang_);
  }
  const json ui = state_.value("ui", json::object());
  applyTheme(ui.value("theme", "dark"));
  pedals_ = ui.value("view", "tiles") == "pedals";
  layout();
}

static const char* kSrcNames[] = {"All", "Built-in", "My types", "LV2"};

std::vector<const json*> Screens::addList() const {
  std::vector<const json*> out;
  for (auto& d : catalog_) {
    if (d["cat"] != kCats[addCat_].first) continue;
    const std::string src = d.value("source", "builtin");
    if (src == "missing") continue;
    const bool user = d.value("user", false);
    if (addSrc_ == 1 && (src == "lv2" || user)) continue;
    if (addSrc_ == 2 && !(src == "fx" && user)) continue;
    if (addSrc_ == 3 && src != "lv2") continue;
    out.push_back(&d);
  }
  return out;
}

std::vector<int> Screens::visibleParams(const json& d) const {
  std::vector<int> v;
  const auto& ps = d["params"];
  for (int i = 0; i < (int)ps.size(); ++i)
    if (!ps[i].value("hidden", false)) v.push_back(i);
  return v;
}

void Screens::cmd(const json& c) {
  json r = eng_.handle(c);
  if (!r.value("ok", false)) {
    std::string m = T(r.value("code", std::string("err.generic")), r.value("error", std::string("Error")));
    if (r.contains("detail") && r.value("code", "") != "") m += ": " + r["detail"].get<std::string>();
    toast(m);
  }
  refresh();
  dirty_ = true;
}

void Screens::toast(const std::string& s) {
  toast_ = s;
  toastTicks_ = 30;
  dirty_ = true;
}

const json* Screens::defOf(const std::string& type) const {
  for (auto& d : catalog_)
    if (d["type"] == type) return &d;
  return nullptr;
}

const json* Screens::blockJson(const std::string& id) const {
  for (auto& n : state_["chain"]) {
    if (n["id"] == id) return &n;
    for (const char* l : {"a", "b"})
      if (n.contains(l))
        for (auto& b : n[l])
          if (b["id"] == id) return &b;
  }
  return nullptr;
}

void Screens::selectId(const std::string& id) {
  for (size_t i = 0; i < cells_.size(); ++i)
    if (cells_[i].id == id && cells_[i].kind != 'm') { cur_ = (int)i; curId_ = id; }
  dirty_ = true;
}

void Screens::layout() {
  const std::string keepId = curId_;
  const int keepRow = cur_ < (int)cells_.size() ? cells_[cur_].row : 0;
  const char keepKind = cur_ < (int)cells_.size() ? cells_[cur_].kind : 'b';
  const std::string keepSplit = cur_ < (int)cells_.size() ? cells_[cur_].split : "";
  cells_.clear();
  int c = 0;
  for (auto& n : state_["chain"]) {
    const std::string id = n["id"];
    if (n["type"] != "split") { cells_.push_back({c++, 0, id, 'b', ""}); continue; }
    cells_.push_back({c++, 0, id, 's', id});
    const auto& a = n["a"];
    const auto& b = n["b"];
    for (size_t i = 0; i < a.size(); ++i) cells_.push_back({c + (int)i, -1, a[i]["id"], 'b', id});
    cells_.push_back({c + (int)a.size(), -1, "", '+', id});
    for (size_t i = 0; i < b.size(); ++i) cells_.push_back({c + (int)i, 1, b[i]["id"], 'b', id});
    cells_.push_back({c + (int)b.size(), 1, "", '+', id});
    c += (int)std::max(a.size(), b.size()) + 1;
    cells_.push_back({c++, 0, id, 'm', id});
  }
  if (cells_.empty()) cells_.push_back({0, 0, "", '+', ""});
  cur_ = 0;
  for (size_t i = 0; i < cells_.size(); ++i) {
    const auto& x = cells_[i];
    if (!keepId.empty() && x.id == keepId && x.kind == keepKind) { cur_ = (int)i; break; }
    if (keepId.empty() && x.kind == '+' && x.split == keepSplit && x.row == keepRow) { cur_ = (int)i; break; }
  }
  curId_ = cells_[cur_].id;
}

bool Screens::update() {
  ++frame_;
  if (eng_.version() != seenVersion_) { refresh(); dirty_ = true; }
  if (frame_ % 2 == 0) { status_ = eng_.status(); dirty_ = true; }
  if (toastTicks_ > 0 && --toastTicks_ == 0) dirty_ = true;
  const bool d = dirty_;
  dirty_ = false;
  return d;
}

std::string Screens::fmtParam(const json& p, float v) const {
  char buf[64];
  const std::string kind = p.value("kind", "float"), unit = p.value("unit", "");
  if (kind == "bool") return v > 0.5f ? T("ui.on", "On") : T("ui.off", "Off");
  if (kind == "enum") {
    const auto& o = p["options"];
    const int i = std::clamp((int)std::lround(v), 0, (int)o.size() - 1);
    return o[i].get<std::string>();
  }
  if (unit == "pan") {
    if (std::fabs(v) < 0.5f) return "C";
    std::snprintf(buf, sizeof(buf), "%c%.0f", v < 0 ? 'L' : 'R', std::fabs(v));
    return buf;
  }
  if (unit == "Hz" && v >= 1000) std::snprintf(buf, sizeof(buf), "%.1f kHz", v / 1000.0f);
  else if (unit == "dB") std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
  else if (unit == "ms") std::snprintf(buf, sizeof(buf), v < 10 ? "%.1f ms" : "%.0f ms", v);
  else if (unit == "%") std::snprintf(buf, sizeof(buf), "%.0f %%", v);
  else if (unit == ":1") std::snprintf(buf, sizeof(buf), "%.1f:1", v);
  else if (unit.empty()) std::snprintf(buf, sizeof(buf), "%.2f", v);
  else std::snprintf(buf, sizeof(buf), "%.0f %s", v, unit.c_str());
  std::string s = buf;
  const std::string dec = T("fmt.decimal", ".");
  if (dec != ".") for (char& ch : s) if (ch == '.') ch = dec[0];
  return s;
}

static float paramFrac(const json& p, float v) {
  const float mn = p["min"], mx = p["max"];
  if (p.value("log", false) && mn > 0) return std::log(v / mn) / std::log(mx / mn);
  return (v - mn) / (mx - mn);
}

// ------------------------------------------------------------ input

void Screens::adjustParam(int dir, bool fast) {
  const auto& cell = cells_[cur_];
  const json* b = blockJson(cell.id);
  if (!b) return;
  const json* d = defOf((*b)["type"]);
  if (!d) return;
  int idx = editSel_;
  if (!(*d)["file"].get<std::string>().empty()) {
    if (idx == 0) return;
    --idx;
  }
  const auto vis = visibleParams(*d);
  if (idx < 0 || idx >= (int)vis.size()) return;
  const auto& p = (*d)["params"][vis[idx]];
  const std::string pid = p["id"];
  float v = (*b)["params"][pid];
  const float mn = p["min"], mx = p["max"];
  const std::string kind = p["kind"];
  if (kind == "bool") v = v > 0.5f ? 0 : 1;
  else if (kind == "enum") { v += dir; if (v > mx) v = mn; if (v < mn) v = mx; }
  else if (p.value("step", 0.0f) > 0) v = std::clamp(v + dir * p.value("step", 1.0f) * (fast ? 5.0f : 1.0f), mn, mx);
  else if (p.value("log", false) && mn > 0) v = std::clamp(v * std::pow(mx / mn, dir * (fast ? 0.03f : 0.005f)), mn, mx);
  else v = std::clamp(v + dir * (mx - mn) * (fast ? 0.04f : 0.01f), mn, mx);
  cmd({{"cmd", "set_param"}, {"id", cell.id}, {"param", pid}, {"value", v}});
}

void Screens::press(Btn btn, bool repeat) {
  dirty_ = true;
  if ((btn == Btn::Start && selHeld_) || (btn == Btn::Select && startHeld_)) { quit_ = true; return; }
  const json& files = state_["files"];
  switch (mode_) {
    case Mode::Rig: {
      if (cells_.empty()) return;
      const Cell cc = cells_[cur_];
      auto pick = [&](auto pred, auto score) {
        int best = -1;
        float bs = 1e9f;
        for (size_t i = 0; i < cells_.size(); ++i)
          if (pred(cells_[i])) { const float s = score(cells_[i]); if (s < bs) { bs = s; best = (int)i; } }
        if (best >= 0) { cur_ = best; curId_ = cells_[cur_].id; }
      };
      if (btn == Btn::Right || btn == Btn::Left) {
        const int d = btn == Btn::Right ? 1 : -1;
        if (cc.kind == 's' && d > 0) pick([&](const Cell& x) { return x.split == cc.split && x.row == -1; }, [&](const Cell& x) { return (float)x.col; });
        else if (cc.kind == 'm' && d < 0) pick([&](const Cell& x) { return x.split == cc.split && x.row == -1; }, [&](const Cell& x) { return (float)-x.col; });
        else if (cc.row != 0) {
          const int before = cur_;
          pick([&](const Cell& x) { return x.row == cc.row && x.split == cc.split && (x.col - cc.col) * d > 0; },
               [&](const Cell& x) { return (float)std::abs(x.col - cc.col); });
          if (cur_ == before) pick([&](const Cell& x) { return x.split == cc.split && x.kind == (d > 0 ? 'm' : 's'); }, [](const Cell&) { return 0.0f; });
        } else {
          pick([&](const Cell& x) { return x.row == 0 && (x.col - cc.col) * d > 0; }, [&](const Cell& x) { return (float)std::abs(x.col - cc.col); });
        }
      } else if (btn == Btn::Up || btn == Btn::Down) {
        const int target = btn == Btn::Up ? -1 : 1;
        if (!cc.split.empty() && cc.row != target)
          pick([&](const Cell& x) { return x.split == cc.split && x.row == target; }, [&](const Cell& x) { return (float)std::abs(x.col - cc.col); });
      } else if (btn == Btn::A) {
        if (cc.kind == '+') { addAfter_.clear(); mode_ = Mode::Add; addFx_ = 0; addFocusList_ = false; return; }
        editSel_ = 0;
        mode_ = Mode::Edit;
      } else if (btn == Btn::B) {
        if (cc.kind == 'b') {
          const json* b = blockJson(cc.id);
          if (b) cmd({{"cmd", "set_on"}, {"id", cc.id}, {"on", !(*b)["on"].get<bool>()}});
        }
      } else if (btn == Btn::X) {
        mode_ = Mode::Add;
        addFx_ = 0;
        addFocusList_ = false;
      } else if (btn == Btn::Y) {
        if (!cc.split.empty()) { selectId(cc.split); editSel_ = 0; mode_ = Mode::Edit; }
      } else if (btn == Btn::L1 || btn == Btn::R1) {
        const auto& pl = files["presets"];
        if (pl.empty()) { toast(T("ui.no_presets", "No presets")); return; }
        int i = 0;
        const std::string curf = state_["preset"]["file"];
        for (size_t k = 0; k < pl.size(); ++k) if (pl[k] == curf) i = (int)k;
        i = (i + (btn == Btn::R1 ? 1 : -1) + (int)pl.size()) % (int)pl.size();
        cmd({{"cmd", "preset_load"}, {"file", pl[i]}});
      } else if (btn == Btn::L2 || btn == Btn::R2) {
        const auto& sc = state_["scenes"];
        if (sc.empty()) { toast(T("ui.no_scenes", "Preset has no scenes")); return; }
        int i = state_.value("scene", -1);
        i = (i + (btn == Btn::R2 ? 1 : -1) + (int)sc.size()) % (int)sc.size();
        cmd({{"cmd", "scene_select"}, {"index", i}});
      } else if (btn == Btn::Select) {
        cmd({{"cmd", "tuner"}, {"on", true}});
        mode_ = Mode::Tuner;
      } else if (btn == Btn::Start) {
        menuSel_ = 0;
        menuPage_ = 0;
        mode_ = Mode::Menu;
      }
      break;
    }
    case Mode::Edit: {
      const json* b = blockJson(cells_[cur_].id);
      if (!b) { mode_ = Mode::Rig; return; }
      const json* d = defOf((*b)["type"]);
      const bool hasFile = !(*d)["file"].get<std::string>().empty();
      const int rows = std::max(1, (int)visibleParams(*d).size() + (hasFile ? 1 : 0));
      if (btn == Btn::Up) editSel_ = (editSel_ + rows - 1) % rows;
      else if (btn == Btn::Down) editSel_ = (editSel_ + 1) % rows;
      else if (btn == Btn::Left || btn == Btn::Right) adjustParam(btn == Btn::Right ? 1 : -1, repeat);
      else if (btn == Btn::L1 || btn == Btn::R1) adjustParam(btn == Btn::R1 ? 1 : -1, true);
      else if (btn == Btn::A && hasFile && editSel_ == 0) {
        fileKind_ = (*d)["file"];
        fileBlock_ = cells_[cur_].id;
        fileSel_ = 0;
        const auto& fl = files[fileKind_];
        for (size_t i = 0; i < fl.size(); ++i) if (fl[i] == b->value("file", "")) fileSel_ = (int)i;
        mode_ = Mode::Files;
      } else if (btn == Btn::X && (*b)["type"] != "split") cmd({{"cmd", "set_on"}, {"id", cells_[cur_].id}, {"on", !(*b)["on"].get<bool>()}});
      else if (btn == Btn::B || btn == Btn::Select) mode_ = Mode::Rig;
      break;
    }
    case Mode::Add: {
      const int nc = (int)kCats.size();
      const auto fx = addList();
      const int n = (int)fx.size();
      if (btn == Btn::B) { if (addFocusList_) addFocusList_ = false; else mode_ = Mode::Rig; return; }
      if (btn == Btn::Y) { addSrc_ = (addSrc_ + 1) % 4; addFx_ = 0; return; }
      if (btn == Btn::Left) { addFocusList_ = false; return; }
      if (btn == Btn::Right) { if (n) addFocusList_ = true; return; }
      if (!addFocusList_) {
        if (btn == Btn::Up) { addCat_ = (addCat_ + nc - 1) % nc; addFx_ = 0; }
        else if (btn == Btn::Down) { addCat_ = (addCat_ + 1) % nc; addFx_ = 0; }
        else if (btn == Btn::A && n) addFocusList_ = true;
        return;
      }
      if (!n) { addFocusList_ = false; return; }
      if (btn == Btn::Up) addFx_ = (addFx_ + n - 1) % n;
      else if (btn == Btn::Down) addFx_ = (addFx_ + 1) % n;
      else if (btn == Btn::L1) addFx_ = std::max(0, addFx_ - 8);
      else if (btn == Btn::R1) addFx_ = std::min(n - 1, addFx_ + 8);
      else if (btn == Btn::A) {
        const std::string type = (*fx[std::min(addFx_, n - 1)])["type"];
        const Cell cc = cells_[cur_];
        json c = {{"cmd", type == "split" ? "add_split" : "add_block"}, {"type", type}};
        if (cc.kind == 'b' || cc.kind == 'm') c["after"] = cc.id;
        else if (cc.kind == 's') { if (type == "split") c["after"] = cc.id; else c["lane"] = {{"split", cc.split}, {"lane", "a"}}; }
        else if (cc.kind == '+' && !cc.split.empty()) {
          // after the last block of the path, else at its start
          std::string last;
          for (auto& x : cells_) if (x.split == cc.split && x.row == cc.row && x.kind == 'b') last = x.id;
          if (type == "split") c["after"] = cc.split;
          else if (!last.empty()) c["after"] = last;
          else c["lane"] = {{"split", cc.split}, {"lane", cc.row < 0 ? "a" : "b"}};
        }
        json r = eng_.handle(c);
        if (!r.value("ok", false)) { toast(T(r.value("code", std::string("err.generic")), r.value("error", std::string("Error")))); return; }
        refresh();
        selectId(r.value("id", ""));
        addFocusList_ = false;
        mode_ = Mode::Rig;
      }
      break;
    }
    case Mode::Files: {
      const auto& fl = files[fileKind_];
      const int n = (int)fl.size();
      if (btn == Btn::B) { mode_ = Mode::Edit; return; }
      if (n == 0) return;
      if (btn == Btn::Up) fileSel_ = (fileSel_ + n - 1) % n;
      else if (btn == Btn::Down) fileSel_ = (fileSel_ + 1) % n;
      else if (btn == Btn::L1) fileSel_ = std::max(0, fileSel_ - 8);
      else if (btn == Btn::R1) fileSel_ = std::min(n - 1, fileSel_ + 8);
      else if (btn == Btn::A || btn == Btn::Right) {
        cmd({{"cmd", "set_file"}, {"id", fileBlock_}, {"file", fl[fileSel_]}});
        if (btn == Btn::A) mode_ = Mode::Edit;
      }
      break;
    }
    case Mode::Tuner: {
      if (btn == Btn::B || btn == Btn::Select) { cmd({{"cmd", "tuner"}, {"on", false}}); mode_ = Mode::Rig; }
      else if (btn == Btn::A) cmd({{"cmd", "set_global"}, {"key", "tuner_mute"}, {"value", !state_["globals"]["tuner_mute"].get<bool>()}});
      else if (btn == Btn::Left || btn == Btn::Right)
        cmd({{"cmd", "set_global"}, {"key", "a4"}, {"value", state_["globals"]["a4"].get<float>() + (btn == Btn::Right ? 1 : -1)}});
      break;
    }
    case Mode::Presets: {
      const auto& pl = files["presets"];
      const int n = (int)pl.size();
      if (btn == Btn::B) { mode_ = Mode::Menu; return; }
      if (n == 0) return;
      if (btn == Btn::Up) presetSel_ = (presetSel_ + n - 1) % n;
      else if (btn == Btn::Down) presetSel_ = (presetSel_ + 1) % n;
      else if (btn == Btn::A) { cmd({{"cmd", "preset_load"}, {"file", pl[presetSel_]}}); mode_ = Mode::Rig; }
      break;
    }
    case Mode::Qr:
      if (btn == Btn::B || btn == Btn::A || btn == Btn::Start) mode_ = Mode::Menu;
      break;
    case Mode::Menu: {
      const auto entries = menuEntries();
      const int n = (int)entries.size();
      if (menuSel_ >= n) menuSel_ = 0;
      if (btn == Btn::Up) menuSel_ = (menuSel_ + n - 1) % n;
      else if (btn == Btn::Down) menuSel_ = (menuSel_ + 1) % n;
      else if (btn == Btn::Start) mode_ = Mode::Rig;
      else if (btn == Btn::B) {
        if (menuPage_ != 0) { menuPage_ = 0; menuSel_ = 0; }
        else mode_ = Mode::Rig;
      } else if (btn == Btn::A) {
        const Cell cc = cells_[cur_];
        const bool inp = state_["input"]["source"] == "wav";
        const int id = entries[menuSel_].id;
        if (id >= 100) { menuPage_ = id - 100; menuSel_ = 0; break; }
        if (id == 99) { menuPage_ = 0; menuSel_ = 0; break; }
        switch (id) {
          case 0: cmd({{"cmd", "preset_save"}}); toast(T("ui.preset_saved", "Preset saved")); mode_ = Mode::Rig; break;
          case 1: presetSel_ = 0; mode_ = Mode::Presets; break;
          case 2: if (cc.kind == 'b' || cc.kind == 's') cmd({{"cmd", "move"}, {"id", cc.id}, {"dir", -1}}); mode_ = Mode::Rig; break;
          case 3: if (cc.kind == 'b' || cc.kind == 's') cmd({{"cmd", "move"}, {"id", cc.id}, {"dir", 1}}); mode_ = Mode::Rig; break;
          case 4: if (cc.kind == 'b' || cc.kind == 's') cmd({{"cmd", "remove"}, {"id", cc.id}}); mode_ = Mode::Rig; break;
          case 5: {
            const auto& wl = files["wav"];
            if (wl.empty()) { toast(T("ui.no_wav", "No WAV files in data/wav")); break; }
            std::string cur = state_["input"]["file"];
            int i = -1;
            for (size_t k = 0; k < wl.size(); ++k) if (wl[k] == cur) i = (int)k;
            i = inp ? (i + 1) % (int)wl.size() : std::max(i, 0);
            cmd({{"cmd", "input"}, {"source", "wav"}, {"file", wl[i]}, {"loop", true}, {"play", true}});
            break;
          }
          case 6: cmd({{"cmd", "transport"}, {"play", !status_.value("playing", false)}}); break;
          case 7: cmd({{"cmd", "input"}, {"source", "live"}}); break;
          case 8: {
            const auto& ls = state_["langs"];
            if (ls.empty()) break;
            size_t i = 0;
            for (size_t k = 0; k < ls.size(); ++k) if (ls[k]["code"] == state_.value("lang", "en")) i = k;
            cmd({{"cmd", "set_lang"}, {"lang", ls[(i + 1) % ls.size()]["code"]}});
            break;
          }
          case 9: {
            const std::string cur = state_.value("ui", json::object()).value("theme", "dark");
            int i = 0;
            for (int k = 0; k < 4; ++k) if (cur == kThemes[k]) i = k;
            cmd({{"cmd", "set_ui"}, {"theme", kThemes[(i + 1) % 4]}});
            break;
          }
          case 10: cmd({{"cmd", "set_ui"}, {"view", pedals_ ? "tiles" : "pedals"}}); break;
          case 11: cycleAudio(); break;
          case 12: {
            static const char* modes[] = {"auto", "always", "off"};
            const std::string cur = state_.value("hotspot_cfg", json::object()).value("mode", "auto");
            int i = 0;
            for (int k = 0; k < 3; ++k) if (cur == modes[k]) i = k;
            cmd({{"cmd", "net_set"}, {"mode", modes[(i + 1) % 3]}});
            break;
          }
          case 13: cmd({{"cmd", "mute"}}); mode_ = Mode::Rig; break;
          case 14: quit_ = true; break;
          case 16: mode_ = Mode::Qr; break;
          case 15: cmd({{"cmd", "metronome"}, {"on", !state_.value("metronome", json::object()).value("on", false)}}); break;
        }
      }
      break;
    }
  }
}

// ------------------------------------------------------------ drawing

void Screens::render(Canvas& c) {
  c.radial(320, 0, 760, 400, col::bg0, col::bg1);
  drawTop(c);
  switch (mode_) {
    case Mode::Rig: drawRig(c); break;
    case Mode::Edit: drawEdit(c); break;
    case Mode::Add: drawAdd(c); break;
    case Mode::Files: drawFiles(c); break;
    case Mode::Tuner: drawTuner(c); break;
    case Mode::Menu: drawMenu(c); break;
    case Mode::Presets: drawPresets(c); break;
    case Mode::Qr: drawQr(c); break;
  }
  if (toastTicks_ > 0 && !toast_.empty()) {
    const float w = c.measure(toast_, bold_, 14) + 32;
    c.roundRect(320 - w / 2, 400, w, 34, 8, col::toastBg);
    c.roundStroke(320 - w / 2, 400, w, 34, 8, 1.5f, col::err);
    c.text(320, 422, toast_, bold_, 14, col::text, Align::Center);
  }
}

void Screens::drawTop(Canvas& c) {
  c.rect(0, 0, 640, 40, col::bar);
  c.rect(0, 39, 640, 1, col::line);
  const auto& pl = state_["files"]["presets"];
  const std::string pf = state_["preset"]["file"];
  int idx = 0;
  for (size_t k = 0; k < pl.size(); ++k) if (pl[k] == pf) idx = (int)k + 1;
  char num[8];
  if (idx) std::snprintf(num, sizeof(num), "%02d", idx); else std::snprintf(num, sizeof(num), "--");
  c.roundRect(12, 10, 28, 20, 4, rgb(0xFFB300));
  c.text(26, 25, num, mono_, 12, col::dark, Align::Center);
  float x = 48;
  x += c.text(x, 26, c.ellipsize(state_["preset"]["name"], bold_, 16, 230), bold_, 16, col::text);
  const int sc = state_.value("scene", -1);
  if (sc >= 0 && sc < (int)state_["scenes"].size())
    c.text(x + 10, 25, "● " + state_["scenes"][sc]["name"].get<std::string>(), bold_, 12, rgb(0xFF4FD8));
  const float cpu = status_.value("cpu", 0.0f);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "CPU %.0f%%", cpu);
  c.text(430, 25, buf, mono_, 12, cpu < 60 ? col::ok : cpu < 85 ? col::warn : col::err, Align::Right);
  auto meter = [&](float x0, const char* lbl, float db) {
    c.text(x0, 25, lbl, mono_, 10, col::dim);
    const float fx = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    c.roundRect(x0 + 26, 17, 44, 6, 3, col::line);
    if (fx > 0) c.roundRectV(x0 + 26, 17, 44 * fx, 6, 3, db > -3 ? col::err : col::ok, db > -3 ? col::err : col::warn);
  };
  meter(440, "IN", status_.value("in_db", -100.0f));
  meter(520, "OUT", status_.value("out_db", -100.0f));
  if (state_["input"]["source"] == "wav") c.text(628, 25, status_.value("playing", false) ? "▶ WAV" : "■ WAV", mono_, 10, col::laneB, Align::Right);
}

void Screens::drawFooter(Canvas& c, const std::vector<std::pair<std::string, std::string>>& hints) {
  c.rect(0, 446, 640, 34, col::bar);
  c.rect(0, 446, 640, 1, col::line);
  float x = 14;
  const Color keyCols[] = {rgb(0xFFB300), rgb(0xFFB300), col::ok, col::err, rgb(0x2F9BFF), rgb(0x9B5CFF), rgb(0xFF7A1A)};
  int i = 0;
  for (auto& h : hints) {
    x += c.text(x, 467, h.first, bold_, 11, keyCols[std::min(i, 6)]) + 4;
    x += c.text(x, 467, h.second, sans_, 11, col::dim) + 12;
    ++i;
  }
  if (!net_.empty()) c.text(626, 467, net_, mono_, 10, col::dim, Align::Right);
}

void Screens::drawKnob(Canvas& c, float cx, float cy, float r, float frac, Color colr, bool active) {
  const float a0 = 0.75f * (float)M_PI, a1 = 2.25f * (float)M_PI;
  c.arc(cx, cy, r, 6, a0, a1, col::line);
  if (frac > 0.001f) c.arc(cx, cy, r, 6, a0, a0 + (a1 - a0) * std::clamp(frac, 0.0f, 1.0f), colr);
  c.circle(cx, cy, r - 11, col::knobBg);
  c.ring(cx, cy, r - 11, 1, col::ring);
  const float a = a0 + (a1 - a0) * std::clamp(frac, 0.0f, 1.0f);
  c.line(cx + std::cos(a) * (r - 18), cy + std::sin(a) * (r - 18), cx + std::cos(a) * (r - 12), cy + std::sin(a) * (r - 12), 2.5f,
         active ? colr : col::text);
}

void Screens::drawPedal(Canvas& c, float cx, float cy, const json& b, const json& d, bool sel, const std::string& label) {
  const Color cl = catColor(d["cat"]);
  const bool on = b["on"];
  const float w = sel ? 54 : 50, h = sel ? 72 : 68, x = cx - w / 2, y = cy - h / 2;
  if (sel && on) c.glow(x, y, w, h, 8, 14, cl);
  if (on) c.roundRectV(x, y, w, h, 8, mix(cl, rgb(0xFFFFFF), 0.08f), mix(cl, rgb(0x000000), 0.35f));
  else c.roundRectV(x, y, w, h, 8, mix(cl, col::tile, 0.72f), mix(cl, col::tile, 0.85f));
  c.roundStroke(x, y, w, h, 8, 1.5f, mix(cl, rgb(0x000000), 0.45f));
  if (!b.value("error", std::string()).empty()) c.roundStroke(x, y, w, h, 8, 2, col::err);
  if (sel) c.roundStroke(x - 3, y - 3, w + 6, h + 6, 10, 2, col::text);
  // LED + number
  if (on) c.glow(cx + 13, y + 9, 1, 1, 2, 5, rgb(0xFF3030));
  c.circle(cx + 13, y + 9, 3, on ? rgb(0xFF3030) : rgb(0x401010));
  c.text(x + 5, y + 12, label, mono_, 8, on ? rgba(0x0B0C0F, 170) : cl);
  // two mini knobs = first two visible parameters
  const auto vis = visibleParams(d);
  for (int k = 0; k < 2; ++k) {
    const float kx = cx + (k ? 11 : -11), ky = y + 27;
    c.circle(kx, ky, 7, rgb(0x16181C));
    c.ring(kx, ky, 7, 1, rgb(0x4A4F57));
    if (k < (int)vis.size()) {
      const auto& p = d["params"][vis[k]];
      const float v = b["params"].value(p["id"].get<std::string>(), 0.0f);
      const float mn = p["min"], mx = p["max"];
      float fr = p.value("log", false) && mn > 0 ? std::log(v / mn) / std::log(mx / mn) : (v - mn) / (mx - mn);
      fr = std::clamp(fr, 0.0f, 1.0f);
      const float a = 0.75f * (float)M_PI + fr * 1.5f * (float)M_PI;
      c.line(kx, ky, kx + std::cos(a) * 5.5f, ky + std::sin(a) * 5.5f, 1.6f, rgb(0xF2F4F7));
    }
  }
  c.text(cx, y + 48, c.ellipsize(d["short"].get<std::string>(), mono_, 10, w - 4), mono_, 10, on ? col::dark : cl, Align::Center);
  // footswitch
  c.circle(cx, y + h - 11, 7, rgb(0x5A5A5A));
  c.circle(cx, y + h - 11, 5.5f, on ? rgb(0xE6E6E6) : rgb(0xA8A8A8));
}

void Screens::drawRig(Canvas& c) {
  const float pitch = pedals_ ? 60 : 51, tile = pedals_ ? 50 : 42, x0 = 40;
  const float yMain = pedals_ ? 150 : 140, yA = pedals_ ? 82 : 86, yB = pedals_ ? 218 : 194;
  int maxCol = 0;
  for (auto& x : cells_) maxCol = std::max(maxCol, x.col);
  const int vis = (int)((604 - x0) / pitch);
  const int cc = cells_[cur_].col;
  if (cc < scroll_) scroll_ = cc;
  if (cc >= scroll_ + vis) scroll_ = cc - vis + 1;
  scroll_ = std::clamp(scroll_, 0, std::max(0, maxCol - vis + 1));
  auto X = [&](int col) { return x0 + (col - scroll_) * pitch + tile / 2; };
  auto Y = [&](int row) { return row < 0 ? yA : row > 0 ? yB : yMain; };
  auto colorOf = [&](const std::string& id) -> Color {
    const json* b = blockJson(id);
    if (!b) return col::text;
    const json* d = defOf((*b)["type"]);
    return d ? catColor((*d)["cat"]) : col::text;
  };
  auto wire = [&](float xa, float ya, float xb, float yb, Color cl) { c.line(xa, ya, xb, yb, 3, cl); };

  // wires
  Color prev = col::text;
  float px = 22;
  for (size_t i = 0; i < cells_.size(); ++i) {
    const auto& ce = cells_[i];
    if (ce.row != 0) continue;
    wire(px, yMain, X(ce.col), yMain, prev);
    if (ce.kind == 's') {
      const Color in = prev;
      for (int row : {-1, 1}) {
        Color lp = in;
        float lx = X(ce.col);
        wire(X(ce.col), yMain, X(ce.col), Y(row), in);
        int mcol = ce.col;
        for (auto& m : cells_) if (m.split == ce.split && m.kind == 'm') mcol = m.col;
        for (auto& l : cells_) {
          if (l.split != ce.split || l.row != row) continue;
          wire(lx, Y(row), X(l.col), Y(row), lp);
          lx = X(l.col);
          if (l.kind == 'b') { const json* b = blockJson(l.id); if (b && (*b)["on"].get<bool>()) lp = colorOf(l.id); }
        }
        wire(lx, Y(row), X(mcol), Y(row), lp);
        wire(X(mcol), Y(row), X(mcol), yMain, row < 0 ? col::laneA : col::laneB);
      }
      px = X(ce.col);
      // jump to the merge
      for (auto& m : cells_) if (m.split == ce.split && m.kind == 'm') { px = X(m.col); }
      prev = col::text;
      continue;
    }
    if (ce.kind == 'm') { px = X(ce.col); prev = col::text; continue; }
    px = X(ce.col);
    if (ce.kind == 'b') { const json* b = blockJson(ce.id); if (b && (*b)["on"].get<bool>()) prev = colorOf(ce.id); }
  }
  wire(px, yMain, 614, yMain, prev);
  c.rect(0, yMain - 12, 26, 24, col::tile);
  c.roundRect(4, yMain - 11, 22, 22, 11, col::text);
  c.text(15, yMain + 3, "IN", mono_, 8, col::dark, Align::Center);
  c.roundRect(612, yMain - 11, 22, 22, 11, col::text);
  c.text(623, yMain + 3, "OUT", mono_, 7, col::dark, Align::Center);

  // lane labels
  for (auto& ce : cells_)
    if (ce.kind == 's') {
      if (!pedals_) c.text(X(ce.col) + 30, yA - 30, "A", bold_, 11, col::laneA);
      else c.text(X(ce.col) - 22, yA + 4, "A", bold_, 11, col::laneA);
      if (!pedals_) c.text(X(ce.col) + 30, yB + 40, "B", bold_, 11, col::laneB);
      else c.text(X(ce.col) - 22, yB + 4, "B", bold_, 11, col::laneB);
    }

  // tiles
  int laneIdx[2] = {0, 0};
  int mainIdx = 0;
  for (size_t i = 0; i < cells_.size(); ++i) {
    const auto& ce = cells_[i];
    if (ce.col < scroll_ || ce.col >= scroll_ + vis) continue;
    const bool sel = (int)i == cur_ && mode_ == Mode::Rig;
    const float cx = X(ce.col), cy = Y(ce.row);
    if (ce.kind == 's' || ce.kind == 'm') {
      const Color rc = ce.kind == 's' ? col::laneA : col::laneB;
      if (sel) c.glow(cx - 14, cy - 14, 28, 28, 14, 12, rc);
      c.circle(cx, cy, 14, col::dark);
      c.ring(cx, cy, 12.5f, 3, rc);
      if (sel) c.ring(cx, cy, 17, 2, col::text);
      c.text(cx, cy + 5, ce.kind == 's' ? "X" : "Σ", bold_, 13, col::text, Align::Center);
      continue;
    }
    const float sz = sel ? 48 : tile;
    const float tx = cx - sz / 2, ty = cy - sz / 2;
    if (ce.kind == '+') {
      const Color lc = ce.row < 0 ? col::laneA : ce.row > 0 ? col::laneB : col::dim;
      const float sh = pedals_ ? sz * 1.36f : sz, sy = cy - sh / 2;
      c.roundRect(tx, sy, sz, sh, 8, mix(col::bg1, rgb(0x000000), 0.2f));
      c.roundStroke(tx, sy, sz, sh, 8, 2, lc, 4);
      if (sel) c.roundStroke(tx - 3, sy - 3, sz + 6, sh + 6, 10, 2, col::text);
      c.text(cx, cy + 7, "+", bold_, 20, lc, Align::Center);
      continue;
    }
    const json* b = blockJson(ce.id);
    if (!b) continue;
    const json* d = defOf((*b)["type"]);
    if (!d) continue;
    const Color cl = catColor((*d)["cat"]);
    const bool on = (*b)["on"];
    char num[16];
    if (ce.row == 0) std::snprintf(num, sizeof(num), "%d", ++mainIdx);
    else std::snprintf(num, sizeof(num), "%c%d", ce.row < 0 ? 'A' : 'B', ++laneIdx[ce.row < 0 ? 0 : 1]);
    if (pedals_) { drawPedal(c, cx, cy, *b, *d, sel, num); continue; }
    if (on) {
      if (sel) c.glow(tx, ty, sz, sz, 8, 16, cl);
      c.roundRectV(tx, ty, sz, sz, 8, cl, mix(cl, rgb(0x000000), 0.18f));
      if (!(*b).value("error", std::string()).empty()) c.roundStroke(tx, ty, sz, sz, 8, 2, col::err);
    } else {
      c.roundRect(tx, ty, sz, sz, 8, col::tile);
      c.roundStroke(tx, ty, sz, sz, 8, 2, cl, 4);
    }
    if (sel) c.roundStroke(tx - 3, ty - 3, sz + 6, sz + 6, 10, 2, col::text);
    c.text(tx + 5, ty + 11, num, mono_, 8, on ? rgba(0x0B0C0F, 160) : cl);
    c.text(cx, cy + 4, (*d)["short"].get<std::string>(), mono_, 12, on ? col::dark : cl, Align::Center);
  }

  // scroll arrows
  if (scroll_ > 0) c.text(30, yMain - 20, "‹", bold_, 20, col::text, Align::Center);
  if (maxCol >= scroll_ + vis) c.text(606, yMain - 20, "›", bold_, 20, col::text, Align::Center);

  // legend (tiles view only - pedals use the full height)
  float lx = 16;
  for (size_t k = 0; k + 2 < kCats.size() && !pedals_; ++k) {
    c.roundRect(lx, 252, 9, 9, 2, catColor(kCats[k].first));
    lx += 13 + c.text(lx + 13, 261, T("cat." + kCats[k].first + ".short", kCats[k].first == "amp" ? "Amp" : kCats[k].first == "cab" ? "Cab" : kCats[k].second), sans_, 10, col::dim) + 10;
  }

  // selected block panel
  const auto& ce = cells_[cur_];
  const float py = 272, ph = 166;
  if (ce.kind == '+') {
    c.roundRect(14, py, 612, ph, 8, col::panel);
    c.roundStroke(14, py, 612, ph, 8, 1, col::line, 5);
    c.text(320, py + 78, ce.split.empty() ? T("ui.empty_chain", "Empty chain - A / X adds the first block") : T("ui.empty_slot", "Empty path slot - A / X adds a block"), bold_, 15, col::dim, Align::Center);
  } else if (const json* b = blockJson(ce.id)) {
    const json* d = defOf((*b)["type"]);
    const Color cl = (*b)["type"] == "split" ? (ce.kind == 'm' ? col::laneB : col::laneA) : catColor((*d)["cat"]);
    c.roundRectV(14, py, 612, ph, 8, mix(col::panel, cl, 0.2f), col::panel);
    c.rect(14, py, 612, 4, cl);
    const std::string pill = std::string(ce.kind == 'm' ? "MERGE" : (*d)["short"].get<std::string>()) + " · " + (*d)["name"].get<std::string>();
    const float pw = c.measure(pill, mono_, 11) + 16;
    c.roundRect(28, py + 14, pw, 20, 4, cl);
    c.text(36, py + 28, pill, mono_, 11, col::dark);
    std::string title = (*b).value("file", std::string());
    if (title.empty() && (*b)["type"] == "split") title = T("ui.type", "Type") + ": " + fmtParam((*d)["params"][0], (*b)["params"]["mode"]);
    if ((*b).contains("info") && (*b)["info"].contains("name")) title = (*b)["info"]["name"].get<std::string>();
    c.text(36 + pw, py + 29, c.ellipsize(title, bold_, 15, 560 - pw), bold_, 15, col::text);
    if (!(*b)["on"].get<bool>()) c.text(612, py + 29, T("ui.bypass_caps", "BYPASS"), mono_, 11, col::err, Align::Right);
    // 4 knobs
    std::vector<int> show;
    const auto& ps = (*d)["params"];
    if ((*b)["type"] == "split") show = ce.kind == 'm' ? std::vector<int>{4, 5, 6, 7} : std::vector<int>{0, 1, 2, 3};
    else for (int k : visibleParams(*d)) if (show.size() < 4) show.push_back(k);
    for (size_t k = 0; k < show.size(); ++k) {
      const auto& p = ps[show[k]];
      const float v = (*b)["params"][p["id"].get<std::string>()];
      const float bx = 30 + k * 148;
      c.roundRect(bx, py + 46, 136, 106, 8, col::card);
      if (p["kind"] == "float") drawKnob(c, bx + 40, py + 98, 26, paramFrac(p, v), cl, false);
      else {
        c.roundRect(bx + 14, py + 82, 52, 30, 6, p["kind"] == "bool" && v > 0.5f ? cl : col::knobBg);
        c.text(bx + 40, py + 102, p["kind"] == "bool" ? (v > 0.5f ? T("ui.on_caps", "ON") : T("ui.off_caps", "OFF")) : "◀▶", mono_, 11, p["kind"] == "bool" && v > 0.5f ? col::dark : col::text, Align::Center);
      }
      c.text(bx + 76, py + 92, c.ellipsize(fmtParam(p, v), mono_, 12, 58), mono_, 12, col::text);
      c.text(bx + 76, py + 110, c.ellipsize(p["name"], sans_, 10, 58), sans_, 10, col::dim);
    }
  }
  drawFooter(c, {{"◀▶▲▼", T("hint.block", "block")}, {"A", T("hint.edit", "edit")}, {"B", T("hint.bypass", "bypass")}, {"X", T("hint.add", "add")}, {"Y", T("hint.split", "split")}, {"L1/R1", T("hint.preset", "preset")}, {"Start", T("hint.menu", "menu")}});
}

void Screens::drawEdit(Canvas& c) {
  const json* b = blockJson(cells_[cur_].id);
  if (!b) return;
  const json* d = defOf((*b)["type"]);
  const Color cl = (*b)["type"] == "split" ? col::laneA : catColor((*d)["cat"]);
  c.roundRectV(14, 52, 612, 384, 8, mix(col::panel, cl, 0.16f), col::panel);
  c.rect(14, 52, 612, 4, cl);
  const std::string pill = (*d)["short"].get<std::string>() + " · " + (*d)["name"].get<std::string>();
  const float pw = c.measure(pill, mono_, 12) + 16;
  c.roundRect(28, 66, pw, 22, 4, cl);
  c.text(36, 82, pill, mono_, 12, col::dark);
  if ((*b)["type"] != "split")
    c.text(612, 82, (*b)["on"].get<bool>() ? T("ui.enabled_caps", "ENABLED") : T("ui.bypass_caps", "BYPASS"), mono_, 12, (*b)["on"].get<bool>() ? col::ok : col::err, Align::Right);
  const bool hasFile = !(*d)["file"].get<std::string>().empty();
  const auto& ps = (*d)["params"];
  const auto vp = visibleParams(*d);
  const int rows = (int)vp.size() + (hasFile ? 1 : 0);
  {  // measured load of this block
    const json loads = status_.value("loads", json::object());
    const std::string bid = cells_[cur_].id;
    if (loads.contains(bid) && (*b)["type"] != "split") {
      char lb[32];
      std::snprintf(lb, sizeof(lb), "CPU %.1f %%", loads[bid].get<float>());
      c.text(612 - c.measure((*b)["on"].get<bool>() ? T("ui.enabled_caps", "ENABLED") : T("ui.bypass_caps", "BYPASS"), mono_, 12) - 16, 82, lb, mono_, 11, col::dim, Align::Right);
    }
  }
  const int vis = 9;
  const int first = std::clamp(editSel_ - vis + 2, 0, std::max(0, rows - vis));
  for (int r = first; r < std::min(rows, first + vis); ++r) {
    const float y = 100 + (r - first) * 37;
    const bool sel = r == editSel_;
    if (sel) { c.roundRect(24, y, 592, 32, 6, mix(col::panel, cl, 0.35f)); c.roundStroke(24, y, 592, 32, 6, 1.5f, cl); }
    if (hasFile && r == 0) {
      c.text(38, y + 21, (*d)["file"] == "nam" ? T("ui.model", "Model") : T("ui.impulse_response", "Impulse response"), bold_, 13, col::text);
      std::string f = (*b).value("file", std::string());
      if (f.empty()) f = T("ui.pick_file", "- pick a file (A) -");
      c.text(600, y + 21, c.ellipsize(f, mono_, 12, 380), mono_, 12, (*b).contains("error") ? col::err : cl, Align::Right);
      continue;
    }
    const auto& p = ps[vp[r - (hasFile ? 1 : 0)]];
    const float v = (*b)["params"][p["id"].get<std::string>()];
    c.text(38, y + 21, p["name"].get<std::string>(), sans_, 13, col::text);
    if (p["kind"] == "float") {
      c.roundRect(250, y + 13, 230, 6, 3, col::line);
      c.roundRect(250, y + 13, 230 * std::clamp(paramFrac(p, v), 0.0f, 1.0f), 6, 3, cl);
      c.circle(250 + 230 * std::clamp(paramFrac(p, v), 0.0f, 1.0f), y + 16, 6, col::text);
    }
    c.text(600, y + 21, fmtParam(p, v), mono_, 13, sel ? col::text : col::dim, Align::Right);
  }
  if ((*b).contains("info") && (*b)["info"].contains("warning")) c.text(30, 428, (*b)["info"]["warning"].get<std::string>(), sans_, 11, col::warn);
  drawFooter(c, {{"▲▼", T("hint.param", "parameter")}, {"◀▶", T("hint.value", "value")}, {"L1/R1", T("hint.coarse", "coarse")}, {"A", T("hint.file", "file")}, {"X", T("hint.bypass", "bypass")}, {"B", T("hint.back", "back")}});
}

void Screens::drawAdd(Canvas& c) {
  c.text(14, 70, T("ui.add_block", "Add block"), bold_, 16, col::text);
  const auto& cc = cells_[cur_];
  std::string where = cc.kind == '+' ? (cc.split.empty() ? T("ui.at_start", "at the start") : (cc.row < 0 ? T("ui.into_path_a", "into path A") : T("ui.into_path_b", "into path B"))) : T("ui.after_selected", "after the selected block");
  c.text(24 + c.measure(T("ui.add_block", "Add block"), bold_, 16), 70, where, sans_, 12, col::dim);
  const std::string src = "Y · " + T("ui.source", "Source") + ": " + T(std::string("src.") + std::to_string(addSrc_), kSrcNames[addSrc_]);
  const float sw = c.measure(src, mono_, 11) + 16;
  c.roundRect(626 - sw, 54, sw, 22, 11, addSrc_ ? rgb(0xFFB300) : col::knobBg);
  c.text(626 - sw / 2, 69, src, mono_, 11, addSrc_ ? col::dark : col::text, Align::Center);
  // categories (counts follow the source filter)
  const int keepCat = addCat_;
  for (size_t k = 0; k < kCats.size(); ++k) {
    const float y = 84 + k * 35;
    const Color cl = catColor(kCats[k].first);
    const bool sel = (int)k == addCat_;
    c.roundRect(14, y, 180, 31, 6, sel ? mix(col::panel, cl, addFocusList_ ? 0.18f : 0.32f) : col::panel);
    c.roundStroke(14, y, 180, 31, 6, sel && !addFocusList_ ? 2 : 1, sel ? cl : col::line);
    c.roundRect(22, y + 6, 10, 19, 3, cl);
    c.text(40, y + 20, c.ellipsize(catName(k), sans_, 13, 120), sans_, 13, col::text);
    addCat_ = (int)k;
    const size_t n = addList().size();
    addCat_ = keepCat;
    c.text(186, y + 20, std::to_string(n), mono_, 11, n ? col::dim : col::faint, Align::Right);
  }
  const auto fx = addList();
  const Color cl = catColor(kCats[addCat_].first);
  if (fx.empty()) c.text(416, 200, T("ui.no_effects", "No effects here"), bold_, 14, col::dim, Align::Center);
  const int vis = 8, n = (int)fx.size();
  const int sel = std::min(addFx_, std::max(0, n - 1));
  const int first = std::clamp(sel - vis / 2, 0, std::max(0, n - vis));
  for (int k = first; k < std::min(n, first + vis); ++k) {
    const json& d = *fx[k];
    const float x = 206, y = 84 + (k - first) * 36;
    const bool s = k == sel;
    const bool focus = s && addFocusList_;
    if (focus) c.glow(x, y, 420, 32, 6, 8, cl);
    c.roundRect(x, y, 420, 32, 6, s ? mix(col::panel, cl, focus ? 0.4f : 0.18f) : col::panel);
    c.roundStroke(x, y, 420, 32, 6, focus ? 2 : 1, s ? cl : col::line);
    c.roundRect(x + 6, y + 6, 46, 20, 4, cl);
    c.text(x + 29, y + 20, c.ellipsize(d["short"].get<std::string>(), mono_, 10, 42), mono_, 10, col::dark, Align::Center);
    c.text(x + 60, y + 21, c.ellipsize(d["name"], bold_, 13, 250), bold_, 13, col::text);
    const std::string srcS = d.value("source", "") == "lv2" ? "LV2" : d.value("user", false) ? T("ui.user_short", "USER") : "";
    char cpu[32];
    if (d.contains("cpu_measured")) std::snprintf(cpu, sizeof(cpu), "%.1f %%", d["cpu_measured"].get<float>());
    else std::snprintf(cpu, sizeof(cpu), "~%.0f %%", d["cpu"].get<float>());
    c.text(x + 412, y + 21, cpu, mono_, 10, d.contains("cpu_measured") ? col::ok : cl, Align::Right);
    if (!srcS.empty()) c.text(x + 352, y + 21, srcS, mono_, 9, col::laneB, Align::Right);
  }
  if (n > vis) {
    char pg[32];
    std::snprintf(pg, sizeof(pg), "%d / %d", sel + 1, n);
    c.text(626, 382, pg, mono_, 10, col::dim, Align::Right);
  }
  if (n) {  // description of the selected effect
    const json& d = *fx[sel];
    c.roundRect(206, 390, 420, 48, 6, col::card);
    std::string meta = d.value("pack", "");
    if (d.contains("author") && !d["author"].get<std::string>().empty()) meta += (meta.empty() ? "" : " · ") + d["author"].get<std::string>();
    c.text(216, 409, c.ellipsize(d["desc"], sans_, 12, 400), sans_, 12, col::text2);
    c.text(216, 428, c.ellipsize(meta, mono_, 10, 400), mono_, 10, col::faint);
  }
  drawFooter(c, {{"▲▼", addFocusList_ ? T("hint.effect", "effect") : T("hint.category", "category")}, {"◀▶", T("hint.focus", "column")}, {"A", addFocusList_ ? T("hint.insert", "insert") : T("hint.open", "open")}, {"Y", T("hint.source", "source")}, {"L1/R1", T("hint.page", "page")}, {"B", T("hint.back", "back")}});
}

void Screens::drawFiles(Canvas& c) {
  const auto& fl = state_["files"][fileKind_];
  const Color cl = fileKind_ == "nam" ? rgb(0xFFB300) : rgb(0xB8F542);
  c.roundRect(14, 52, 120, 22, 4, cl);
  c.text(74, 68, fileKind_ == "nam" ? T("ui.nam_models_caps", "NAM MODELS") : T("ui.ir_files_caps", "IR FILES"), mono_, 11, col::dark, Align::Center);
  c.text(146, 68, "data/" + fileKind_ + " · " + fmtArgs(T("ui.n_files", "{0} files"), {std::to_string(fl.size())}), mono_, 11, col::dim);
  if (fl.empty()) c.text(320, 240, T("ui.upload_hint", "Upload files via the web UI or to data/") + fileKind_, bold_, 14, col::dim, Align::Center);
  const int vis = 9;
  const int first = std::clamp(fileSel_ - vis / 2, 0, std::max(0, (int)fl.size() - vis));
  const json* b = blockJson(fileBlock_);
  const std::string curFile = b ? b->value("file", std::string()) : "";
  for (int i = first; i < std::min((int)fl.size(), first + vis); ++i) {
    const float y = 84 + (i - first) * 39;
    const bool sel = i == fileSel_;
    c.roundRect(14, y, 400, 34, 6, sel ? mix(col::panel, cl, 0.35f) : col::panel);
    c.roundStroke(14, y, 400, 34, 6, sel ? 2 : 1, sel ? cl : col::line);
    c.roundRect(20, y + 7, 4, 20, 2, cl);
    c.text(32, y + 22, c.ellipsize(fl[i], sans_, 13, 350), sans_, 13, col::text);
    if (fl[i] == curFile) c.text(406, y + 22, "●", bold_, 12, col::ok, Align::Right);
  }
  c.roundRectV(424, 84, 202, 350, 8, mix(col::panel, cl, 0.25f), col::panel);
  c.roundStroke(424, 84, 202, 350, 8, 1, cl);
  float y = 108;
  if (b && b->contains("info")) {
    const auto& inf = (*b)["info"];
    auto row = [&](const std::string& k, const std::string& v) {
      c.text(436, y, k, sans_, 11, col::dim);
      c.text(436, y + 16, c.ellipsize(v, bold_, 12, 180), bold_, 12, col::text);
      y += 38;
    };
    if (inf.contains("name")) row(T("ui.name", "Name"), inf["name"]);
    if (inf.contains("gear_make")) row(T("ui.maker", "Maker"), inf["gear_make"].get<std::string>() + " " + inf.value("gear_model", ""));
    if (inf.contains("architecture")) row(T("ui.architecture", "Architecture"), inf["architecture"]);
    if (inf.contains("slimmable")) row("Lite/Full", inf["slimmable"].get<bool>() ? T("ui.slim_yes", "yes (Size parameter)") : T("ui.no", "no"));
    if (inf.contains("sample_rate")) row(T("ui.sample_rate", "Sample rate"), std::to_string((int)inf["sample_rate"].get<double>()) + " Hz");
    if (inf.contains("ms")) row(T("ui.ir_length", "IR length"), std::to_string((int)inf["ms"].get<double>()) + " ms");
  } else {
    c.text(436, y, T("ui.file_info_1", "Details of the loaded"), sans_, 11, col::dim);
    c.text(436, y + 16, T("ui.file_info_2", "file appear here."), sans_, 11, col::dim);
  }
  drawFooter(c, {{"▲▼", T("hint.file", "file")}, {"→", T("hint.preview", "preview")}, {"A", T("hint.load", "load")}, {"L1/R1", T("hint.page", "page")}, {"B", T("hint.back", "back")}});
}

void Screens::drawTuner(Canvas& c) {
  c.radial(320, 240, 420, 300, col::tuneGlow, col::tuneBg);
  drawTop(c);
  const auto& t = status_.contains("tuner") ? status_["tuner"] : json(nullptr);
  const bool valid = !t.is_null();
  const float cents = valid ? t["cents"].get<float>() : 0.0f;
  const std::string rawNote = valid ? t["note"].get<std::string>() : "";
  const std::string note = valid ? T("note." + rawNote, rawNote) : "";  // e.g. B -> H in Czech
  const char* strings[] = {"E", "A", "D", "G", "B", "E"};
  for (int i = 0; i < 6; ++i) {
    const bool hit = valid && rawNote == strings[i];
    const std::string lbl = i == 5 ? "e" : T(std::string("note.") + strings[i], strings[i]);
    c.roundRect(214 + i * 36, 70, 30, 28, 6, hit ? col::ok : col::knobBg);
    c.text(229 + i * 36, 89, lbl, mono_, 13, hit ? col::dark : col::dim, Align::Center);
  }
  const Color nc = !valid ? col::faint : std::fabs(cents) < 3 ? col::ok : std::fabs(cents) < 15 ? col::warn : col::err;
  c.text(320, 230, note, bold_, 110, nc, Align::Center);
  if (valid) c.text(320 + c.measure(note, bold_, 110) / 2 + 8, 230, std::to_string(t["octave"].get<int>()), mono_, 26, col::dim);
  for (int i = 0; i < 21; ++i) {
    const int dd = std::abs(i - 10);
    const Color sc = dd <= 1 ? col::ok : dd <= 4 ? col::warn : dd <= 7 ? rgb(0xFFB300) : col::err;
    const int pos = valid ? std::clamp((int)std::lround(cents / 5.0f) + 10, 0, 20) : -1;
    const bool lit = i == pos || i == 10;
    if (i == pos) c.glow(60 + i * 25, 262, 21, 36, 3, 10, sc);
    c.roundRect(60 + i * 25, 262, 21, 36, 3, lit ? sc : mix(col::tuneBg, sc, 0.18f));
  }
  c.text(60, 316, "−50 c", mono_, 11, col::faint);
  c.text(320, 316, "0", mono_, 11, col::faint, Align::Center);
  c.text(580, 316, "+50 c", mono_, 11, col::faint, Align::Right);
  char buf[64];
  std::string s;
  if (valid) {
    std::snprintf(buf, sizeof(buf), "%.1f Hz · %+.0f ", t["freq"].get<float>(), cents);
    s = std::string(buf) + T("ui.cents", "cents");
    const std::string dec = T("fmt.decimal", ".");
    if (dec != ".") for (char& ch : s) if (ch == '.') ch = dec[0];
  } else {
    s = T("ui.play_string", "play a string");
  }
  c.text(320, 360, s, mono_, 16, col::text2, Align::Center);
  std::snprintf(buf, sizeof(buf), "A4 = %.0f Hz", state_["globals"]["a4"].get<float>());
  c.text(320, 392, std::string(buf) + (state_["globals"]["tuner_mute"].get<bool>() ? " · " + T("ui.output_muted", "output muted") : " · " + T("ui.output_live", "output live")), sans_, 12, col::dim,
         Align::Center);
  drawFooter(c, {{"Select/B", T("hint.back", "back")}, {"◀▶", "A4"}, {"A", T("hint.mute", "mute/unmute")}});
}

// menu text for the audio input: the choice and, for "auto", what it resolved to
std::string Screens::audioLabel() const {
  const json au = state_.value("audio_dev", json::object());
  const std::string dev = au.value("choice", json::object()).value("device", std::string("auto"));
  if (dev == "internal") return T("aud.internal", "Internal");
  if (dev == "auto") {
    const std::string act = au.value("active", json::object()).value("label", std::string());
    return T("aud.auto", "Auto") + (act.empty() ? "" : " (" + act + ")");
  }
  const std::string id = dev.substr(dev.rfind(':') == std::string::npos ? 0 : dev.rfind(':') + 1);
  for (const auto& c : au.value("cards", json::array()))
    if (c.value("id", std::string()) == id) return "USB " + c.value("name", id);
  return id + " (" + T("aud.unplugged", "not connected") + ")";
}

// Start -> Audio input: auto -> internal -> each USB interface -> auto ...
void Screens::cycleAudio() {
  const json au = state_.value("audio_dev", json::object());
  std::vector<std::string> devs = {"auto", "internal"};
  for (const auto& c : au.value("cards", json::array()))
    if (c.value("usb", false) && c.value("capture", false) && c.value("playback", false)) devs.push_back("card:" + c.value("id", std::string()));
  const std::string cur = au.value("choice", json::object()).value("device", std::string("auto"));
  size_t i = 0;
  for (size_t k = 0; k < devs.size(); ++k) if (devs[k] == cur) i = k;
  cmd({{"cmd", "audio_set"}, {"device", devs[(i + 1) % devs.size()]}});
}

std::vector<Screens::MenuEntry> Screens::menuEntries() {
  const bool wav = state_["input"]["source"] == "wav";
  const std::string wavf = state_["input"]["file"];
  std::string langName = state_.value("lang", "en");
  for (auto& l : state_["langs"]) if (l["code"] == langName) langName = l["name"];
  const std::string theme = state_.value("ui", json::object()).value("theme", std::string("dark"));
  const std::string hotspot = state_.value("hotspot_cfg", json::object()).value("mode", std::string("auto"));
  const json met = state_.value("metronome", json::object());
  const std::string back = "< " + T("menu.back", "Back");
  switch (menuPage_) {
    case 1:
      return {{T("menu.save", "Save preset"), 0}, {T("menu.load", "Load preset..."), 1}, {back, 99}};
    case 2:
      return {{T("menu.move_left", "Move block left"), 2}, {T("menu.move_right", "Move block right"), 3}, {T("menu.delete", "Delete block"), 4}, {back, 99}};
    case 3:
      return {{wav ? fmtArgs(T("menu.wav_next", "WAV input: next file ({0})"), {wavf}) : T("menu.wav_input", "Input from WAV file"), 5},
              {status_.value("playing", false) ? T("menu.wav_stop", "Stop WAV") : T("menu.wav_play", "Play WAV"), 6},
              {T("menu.live", "Live input (guitar)"), 7},
              {T("menu.audio", "Audio input") + ": " + audioLabel(), 11},
              {T("menu.metronome", "Metronome") + ": " + std::to_string((int)std::lround(met.value("bpm", 100.0))) + " BPM " +
                   (met.value("on", false) ? T("menu.on", "on") : T("menu.off", "off")), 15},
              {back, 99}};
    case 4:
      return {{T("menu.language", "Language") + ": " + langName, 8},
              {T("menu.theme", "Theme") + ": " + T("theme." + theme, theme), 9},
              {T("menu.view", "Rig view") + ": " + (pedals_ ? T("view.pedals", "Pedalboard") : T("view.tiles", "Tiles")), 10},
              {T("menu.hotspot", "Own WiFi") + ": " + T("net." + hotspot, hotspot), 12},
              {T("menu.qr", "Phone QR codes"), 16},
              {back, 99}};
    default:
      return {{T("menu.sub_preset", "Preset") + "  >", 101},
              {T("menu.sub_block", "Block") + "  >", 102},
              {T("menu.sub_input", "Input and sound") + "  >", 103},
              {T("menu.sub_settings", "Settings") + "  >", 104},
              {state_.value("muted", false) ? T("menu.unmute", "Unmute output") : T("menu.mute", "Mute output"), 13},
              {T("menu.quit", "Quit and return to ES"), 14}};
  }
}

void Screens::drawMenu(Canvas& c) {
  drawRig(c);
  c.rect(0, 40, 640, 406, rgba(0x000000, 170));
  const auto items = menuEntries();
  c.roundRect(150, 46, 340, 398, 10, col::panel);
  c.roundStroke(150, 46, 340, 398, 10, 1.5f, rgb(0xFFB300));
  c.text(170, 70, T("menu.title", "Menu"), bold_, 16, col::text);
  for (size_t i = 0; i < items.size(); ++i) {
    const float y = 82 + i * 32.0f;
    const bool sel = (int)i == menuSel_;
    if (sel) c.roundRect(162, y, 316, 28, 6, mix(col::panel, rgb(0xFFB300), 0.3f));
    c.text(176, y + 19, c.ellipsize(items[i].label, sans_, 14, 290), sans_, 14, items[i].id == 14 ? col::err : col::text);
  }
}

// Draws a QR code of `text` into a square of about `size` px (white quiet zone included).
static void drawQrCode(Canvas& c, const std::string& text, float x, float y, float size) {
  using qrcodegen::QrCode;
  const QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::MEDIUM);
  const int n = qr.getSize(), cells = n + 4;
  const float m = std::floor(size / cells);
  c.rect(x, y, m * cells, m * cells, rgb(0xFFFFFF));
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i)
      if (qr.getModule(i, j)) c.rect(x + (i + 2) * m, y + (j + 2) * m, m, m, rgb(0x000000));
}

// WIFI:T:WPA;S:name;P:password;; (special characters escaped), as phone cameras expect it
static std::string wifiQrText(const std::string& ssid, const std::string& pw) {
  auto esc = [](const std::string& s) {
    std::string o;
    for (char ch : s) { if (std::string("\\;,:\"").find(ch) != std::string::npos) o += '\\'; o += ch; }
    return o;
  };
  return pw.empty() ? "WIFI:T:nopass;S:" + esc(ssid) + ";;" : "WIFI:T:WPA;S:" + esc(ssid) + ";P:" + esc(pw) + ";;";
}

void Screens::drawQr(Canvas& c) {
  const json net = state_.value("network", json::object());
  const bool ap = net.value("hotspot", false);
  const json apj = net.value("ap", json::object());
  std::string ip = net.value("ip", "");
  if (ip.empty() && ap) ip = "10.42.0.1";
  const int port = net.value("web_port", 80);
  const std::string url = ip.empty() ? "" : "http://" + ip + (port != 80 ? ":" + std::to_string(port) : "");
  c.text(14, 70, T("qr.title", "Connect your phone"), bold_, 16, col::text);
  if (ap) {
    const std::string ssid = apj.value("ssid", "namfx"), pw = apj.value("password", "");
    drawQrCode(c, wifiQrText(ssid, pw), 50, 100, 230);
    c.text(165, 350, T("qr.join", "1. Scan to join the WiFi"), sans_, 14, col::text, Align::Center);
    c.text(165, 372, ssid + (pw.empty() ? " (" + T("qr.open", "open") + ")" : ""), mono_, 12, col::dim, Align::Center);
    if (!url.empty()) {
      drawQrCode(c, url, 360, 100, 230);
      c.text(475, 350, T("qr.open_page", "2. Then scan to open namfx"), sans_, 14, col::text, Align::Center);
      c.text(475, 372, url, mono_, 12, col::dim, Align::Center);
    }
  } else if (!url.empty()) {
    drawQrCode(c, url, 205, 100, 230);
    c.text(320, 350, T("qr.open_page2", "Scan to open namfx (same WiFi)"), sans_, 14, col::text, Align::Center);
    c.text(320, 372, url, mono_, 12, col::dim, Align::Center);
  } else {
    c.text(320, 240, T("qr.none", "No network: turn on WiFi or the own WiFi (hotspot)"), sans_, 14, col::text, Align::Center);
  }
}

void Screens::drawPresets(Canvas& c) {
  const auto& pl = state_["files"]["presets"];
  c.text(14, 70, T("ui.presets", "Presets"), bold_, 16, col::text);
  const int vis = 9;
  const int first = std::clamp(presetSel_ - vis / 2, 0, std::max(0, (int)pl.size() - vis));
  for (int i = first; i < std::min((int)pl.size(), first + vis); ++i) {
    const float y = 84 + (i - first) * 39;
    const bool sel = i == presetSel_;
    c.roundRect(14, y, 612, 34, 6, sel ? mix(col::panel, rgb(0xFFB300), 0.3f) : col::panel);
    c.roundStroke(14, y, 612, 34, 6, sel ? 2 : 1, sel ? rgb(0xFFB300) : col::line);
    char num[16];
    std::snprintf(num, sizeof(num), "%02d", i + 1);
    c.text(28, y + 22, num, mono_, 12, rgb(0xFFB300));
    c.text(60, y + 22, pl[i].get<std::string>(), sans_, 13, col::text);
  }
  drawFooter(c, {{"▲▼", T("hint.preset", "preset")}, {"A", T("hint.load", "load")}, {"B", T("hint.back", "back")}});
}

}  // namespace ui
