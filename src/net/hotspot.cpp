// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "net/hotspot.h"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <vector>

extern char** environ;

namespace fx {

HotspotConfig HotspotConfig::fromJson(const json& j) {
  HotspotConfig c;
  if (!j.is_object()) return c;
  c.mode = j.value("mode", c.mode);
  c.ssid = j.value("ssid", c.ssid);
  c.password = j.value("password", c.password);
  return c;
}

json HotspotConfig::toJson() const { return {{"mode", mode}, {"ssid", ssid}, {"password", password}}; }

int runProgram(const std::vector<std::string>& args, std::string& out, int timeoutSec) {
  out.clear();
  int fds[2];
  if (pipe(fds) != 0) return -1;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
  posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
  posix_spawn_file_actions_addclose(&fa, fds[0]);
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  close(fds[1]);
  if (rc != 0) { close(fds[0]); return -1; }
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
  char buf[512];
  while (true) {
    const int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count();
    if (left <= 0) { kill(pid, SIGKILL); break; }
    pollfd p{fds[0], POLLIN, 0};
    if (::poll(&p, 1, left) <= 0) continue;
    const ssize_t n = read(fds[0], buf, sizeof(buf));
    if (n <= 0) break;
    if (out.size() < 65536) out.append(buf, (size_t)n);
  }
  close(fds[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  if (WIFEXITED(st)) return WEXITSTATUS(st) == 127 ? -1 : WEXITSTATUS(st);
  return 255;
}

// "a:b\:c" -> {"a", "b:c"} (nmcli terse output escapes ':' and '\')
static std::vector<std::string> splitTerse(const std::string& line) {
  std::vector<std::string> f(1);
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\\' && i + 1 < line.size()) f.back() += line[++i];
    else if (line[i] == ':') f.emplace_back();
    else f.back() += line[i];
  }
  return f;
}

static std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

Hotspot::Hotspot(std::function<HotspotConfig()> getConfig, std::function<void()> onChange)
    : getConfig_(std::move(getConfig)), onChange_(std::move(onChange)) {}

void Hotspot::start() {
  if (run_.exchange(true)) return;
  th_ = std::thread([this] { loop(); });
}

void Hotspot::stop() {
  if (run_.exchange(false) && th_.joinable()) th_.join();
  bool ours;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    ours = startedByUs_;
  }
  if (ours) stopAp(false);  // NetworkManager reconnects to known networks on its own
}

void Hotspot::request(bool on) { manual_.store(on ? 1 : -1); }

json Hotspot::status() const {
  std::lock_guard<std::mutex> lk(mtx_);
  json j = {{"available", available_}, {"device", device_}, {"wifi_state", wifiState_}, {"connection", connection_},
            {"ip", ip_}, {"hotspot", active_}};
  if (active_) j["ap"] = {{"ssid", apSsid_}, {"password", apPassword_}};
  if (!error_.empty()) j["error"] = error_;
  return j;
}

std::string Hotspot::shortInfo() const {
  std::lock_guard<std::mutex> lk(mtx_);
  if (active_) return "AP " + apSsid_ + (apPassword_.empty() ? " (open)" : " / " + apPassword_) + " · " + (ip_.empty() ? "10.42.0.1" : ip_);
  return "";
}

void Hotspot::loop() {
  using namespace std::chrono;
  auto next = steady_clock::now();
  while (run_.load()) {
    if (steady_clock::now() >= next || manual_.load() != 0 || repoll_.exchange(false)) {
      poll();
      next = steady_clock::now() + seconds(2);
    }
    std::this_thread::sleep_for(milliseconds(100));
  }
}

void Hotspot::poll() {
  const HotspotConfig cfg = getConfig_();
  std::string out;
  const int rc = runProgram({"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device"}, out, 10);
  std::string dev, state, conn;
  bool avail = rc == 0;
  std::string err;
  if (rc == -1) err = "nmcli not found (NetworkManager missing)";
  else if (rc != 0) err = trim(out);
  else {
    std::istringstream is(out);
    std::string line;
    while (std::getline(is, line)) {
      auto f = splitTerse(trim(line));
      if (f.size() >= 4 && f[1] == "wifi") { dev = f[0]; state = f[2]; conn = f[3]; break; }
    }
    if (dev.empty()) { avail = false; err = "no WiFi device"; }
  }
  const bool connected = state.rfind("connected", 0) == 0;  // "connected", "connected (externally)"
  const bool apActive = connected && conn == kConn;
  std::string ip;
  if (connected) {
    std::string o2;
    if (runProgram({"nmcli", "-g", "IP4.ADDRESS", "device", "show", dev}, o2, 10) == 0) {
      ip = trim(o2);
      ip = ip.substr(0, ip.find_first_of("/|\n"));
    }
  }
  bool changed;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    changed = avail != available_ || dev != device_ || state != wifiState_ || conn != connection_ || ip != ip_ || apActive != active_;
    available_ = avail;
    device_ = dev;
    wifiState_ = state;
    connection_ = conn;
    ip_ = ip;
    active_ = apActive;
    if (apActive && apSsid_.empty()) { apSsid_ = cfg.ssid; apPassword_ = cfg.password; startedByUs_ = true; }  // left over from a previous run
    if (!err.empty() && err != error_) { error_ = err; changed = true; }
    if (err.empty() && avail && !error_.empty() && error_.rfind("hotspot:", 0) != 0) { error_.clear(); changed = true; }
    if (connected && !apActive) { noLinkSecs_ = 0; manualOff_ = false; }
  }
  if (changed && onChange_) onChange_();
  if (!avail) { manual_.store(0); return; }

  const int man = manual_.exchange(0);
  if (man > 0 && !apActive) { startAp(cfg); return; }
  if (man < 0) {
    { std::lock_guard<std::mutex> lk(mtx_); manualOff_ = true; noLinkSecs_ = 0; }
    if (apActive) stopAp();
    return;
  }
  bool manualOff, restart = false;
  int noLink;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    // new name/password while our network runs -> restart it
    if (apActive && startedByUs_ && cfg.mode != "off" && (cfg.ssid != apSsid_ || cfg.password != apPassword_)) restart = true;
    if (!connected) noLinkSecs_ += 2;
    manualOff = manualOff_;
    noLink = noLinkSecs_;
  }
  if (restart) {
    startAp(cfg);
  } else if (cfg.mode == "off") {
    bool ours;
    { std::lock_guard<std::mutex> lk(mtx_); ours = startedByUs_; }
    if (apActive && ours) stopAp();
  } else if (cfg.mode == "always") {
    if (!apActive && !manualOff) startAp(cfg);
  } else {  // auto: no WiFi connection for 20 s -> own network
    if (!connected && !apActive && !manualOff && noLink >= 20) startAp(cfg);
  }
}

bool Hotspot::startAp(const HotspotConfig& c) {
  std::string dev;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    dev = device_;
  }
  std::string out;
  runProgram({"nmcli", "connection", "delete", kConn}, out, 10);  // old settings (ssid/password) out
  int rc;
  if (c.password.empty()) {  // open network: nmcli's "wifi hotspot" always adds WPA, so build the AP connection by hand
    std::string o2;
    rc = runProgram({"nmcli", "connection", "add", "type", "wifi", "ifname", dev, "con-name", kConn, "autoconnect", "no", "ssid", c.ssid,
                     "802-11-wireless.mode", "ap", "802-11-wireless.band", "bg", "ipv4.method", "shared"}, out, 20);
    if (rc == 0) rc = runProgram({"nmcli", "connection", "up", kConn}, o2, 30);
    if (rc != 0 && !o2.empty()) out = o2;
  } else {
    rc = runProgram({"nmcli", "device", "wifi", "hotspot", "ifname", dev, "con-name", kConn, "ssid", c.ssid, "password", c.password}, out, 30);
  }
  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (rc == 0) {
      startedByUs_ = true;
      apSsid_ = c.ssid;
      apPassword_ = c.password;
      error_.clear();
      std::fprintf(stderr, "[net] hotspot %s started on %s\n", c.ssid.c_str(), dev.c_str());
    } else {
      error_ = "hotspot: " + trim(out);
      noLinkSecs_ = -60;  // retry after a pause
      std::fprintf(stderr, "[net] hotspot failed: %s\n", error_.c_str());
    }
  }
  repoll_ = true;
  if (onChange_) onChange_();
  return rc == 0;
}

void Hotspot::stopAp(bool reconnect) {
  std::string out, dev;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    dev = device_;
  }
  runProgram({"nmcli", "connection", "down", kConn}, out, 15);
  runProgram({"nmcli", "connection", "delete", kConn}, out, 10);
  if (reconnect && !dev.empty()) runProgram({"nmcli", "device", "connect", dev}, out, 30);  // back to a known network
  {
    std::lock_guard<std::mutex> lk(mtx_);
    startedByUs_ = false;
    active_ = false;
  }
  repoll_ = true;
  std::fprintf(stderr, "[net] hotspot stopped\n");
  if (onChange_) onChange_();
}

}  // namespace fx
