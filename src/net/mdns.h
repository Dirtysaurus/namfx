// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
//
// Minimal mDNS responder: answers A queries for "<name>.local" with this device's IPv4 addresses,
// so the web UI is reachable as http://namfx.local from phones and PCs (iOS, macOS, Windows 10+, Linux with nss-mdns).
// UDP port 5353, or $NAMFX_MDNS_PORT (tests). Not a full implementation: no IPv6, no probing, no other record types.
#pragma once
#include <atomic>
#include <string>
#include <thread>

namespace fx {

class Mdns {
 public:
  explicit Mdns(std::string name) : name_(std::move(name)) {}
  ~Mdns() { stop(); }
  bool start();
  void stop();
  const std::string& name() const { return name_; }

 private:
  void loop();
  std::string name_;
  int fd_ = -1;
  std::thread th_;
  std::atomic<bool> run_{false};
};

}  // namespace fx
