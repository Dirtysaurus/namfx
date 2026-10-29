// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "i18n.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace fx {

I18n& i18n() {
  static I18n inst;
  return inst;
}

bool I18n::load(const std::string& dir, const std::string& code) {
  nlohmann::json d = nlohmann::json::object();
  if (code != "en") {
    std::ifstream f(dir + "/" + code + ".json");
    if (!f) return false;
    d = nlohmann::json::parse(f, nullptr, false);
    if (d.is_discarded() || !d.is_object()) return false;
  }
  std::lock_guard<std::mutex> lk(m_);
  dict_ = std::move(d);
  code_ = code;
  return true;
}

std::string I18n::tr(const std::string& key, const std::string& fallback) const {
  std::lock_guard<std::mutex> lk(m_);
  auto it = dict_.find(key);
  if (it != dict_.end() && it->is_string() && !it->get<std::string>().empty()) return it->get<std::string>();
  return fallback;
}

std::string I18n::code() const {
  std::lock_guard<std::mutex> lk(m_);
  return code_;
}

nlohmann::json I18n::available(const std::string& dir) {
  nlohmann::json out = nlohmann::json::array();
  std::vector<std::pair<std::string, std::string>> langs;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    if (e.path().extension() != ".json") continue;
    std::ifstream f(e.path());
    auto j = nlohmann::json::parse(f, nullptr, false);
    const std::string code = e.path().stem().string();
    std::string name = code;
    if (!j.is_discarded() && j.contains("_language")) name = j["_language"].get<std::string>();
    langs.push_back({code, name});
  }
  if (std::none_of(langs.begin(), langs.end(), [](auto& p) { return p.first == "en"; })) langs.push_back({"en", "English"});
  std::sort(langs.begin(), langs.end());
  for (auto& [c, n] : langs) out.push_back({{"code", c}, {"name", n}});
  return out;
}

std::string fmtArgs(std::string s, const std::vector<std::string>& args) {
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string tag = "{" + std::to_string(i) + "}";
    for (size_t p = s.find(tag); p != std::string::npos; p = s.find(tag, p + args[i].size())) s.replace(p, tag.size(), args[i]);
  }
  return s;
}

}  // namespace fx
