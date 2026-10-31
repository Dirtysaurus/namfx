// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
//
// WiFi hotspot fallback via NetworkManager (nmcli). When the console has no WiFi
// connection, namfx opens its own access point so the web UI stays reachable.
// Settings live in data/settings.json ("hotspot": {mode, ssid, password}).
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "json.hpp"

namespace fx {

using json = nlohmann::json;

struct HotspotConfig {
  std::string mode = "auto";  // auto | always | off
  std::string ssid = "namfx";
  std::string password;  // empty = open network (set one after the first connection)
  static HotspotConfig fromJson(const json& j);
  json toJson() const;
};

class Hotspot {
 public:
  // getConfig: current settings (read every cycle); onChange: status changed (bump state version)
  Hotspot(std::function<HotspotConfig()> getConfig, std::function<void()> onChange);
  ~Hotspot() { stop(); }
  void start();
  void stop();  // also takes our hotspot down so the normal WiFi comes back
  void request(bool on);  // manual start/stop from the UIs
  json status() const;
  std::string shortInfo() const;  // for the on-screen footer

  static constexpr const char* kConn = "namfx-hotspot";

 private:
  void loop();
  void poll();
  bool startAp(const HotspotConfig& c);
  void stopAp(bool reconnect = true);

  std::function<HotspotConfig()> getConfig_;
  std::function<void()> onChange_;
  std::thread th_;
  std::atomic<bool> run_{false};
  std::atomic<int> manual_{0};  // 1 start, -1 stop
  std::atomic<bool> repoll_{false};
  mutable std::mutex mtx_;
  // status (mtx_)
  bool available_ = false, active_ = false, startedByUs_ = false, manualOff_ = false;
  std::string device_, wifiState_, connection_, ip_, error_, apSsid_, apPassword_;
  int noLinkSecs_ = 0;
};

// Runs a program (no shell) and returns its exit code; stdout+stderr in out. -1 = not found.
int runProgram(const std::vector<std::string>& args, std::string& out, int timeoutSec = 20);

}  // namespace fx
