// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"

namespace fx {

// Translations. Source language is English: every call site carries the English
// text as fallback, lang/<code>.json files map keys to translated text.
// Lang files live in web/lang/ so the web UI can load the same files.
class I18n {
 public:
  bool load(const std::string& dir, const std::string& code);
  std::string tr(const std::string& key, const std::string& fallback) const;
  std::string code() const;
  // [{code, name}] of all lang/*.json files
  static nlohmann::json available(const std::string& dir);

 private:
  mutable std::mutex m_;
  nlohmann::json dict_ = nlohmann::json::object();
  std::string code_ = "en";
};

I18n& i18n();

inline std::string T(const std::string& key, const std::string& fallback) { return i18n().tr(key, fallback); }

// "Scene {0}" + {"3"} -> "Scene 3"
std::string fmtArgs(std::string s, const std::vector<std::string>& args);

}  // namespace fx
