// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "net/mdns.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace fx {

// IPv4 addresses (network order) of the non-loopback interfaces that are up
static std::vector<uint32_t> localIps() {
  std::vector<uint32_t> out;
  ifaddrs* ifa = nullptr;
  if (getifaddrs(&ifa) != 0) return out;
  for (ifaddrs* p = ifa; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET || !(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
    out.push_back(((sockaddr_in*)p->ifa_addr)->sin_addr.s_addr);
  }
  freeifaddrs(ifa);
  return out;
}

// "namfx.local" as DNS labels
static std::string labels(const std::string& host) {
  std::string out;
  out += (char)host.size();
  out += host;
  out += (char)5;
  out += "local";
  out += (char)0;
  return out;
}

static void joinGroups(int fd) {  // join the mDNS group on every interface (failures, e.g. already a member, are ignored)
  for (uint32_t ip : localIps()) {
    ip_mreq m{};
    inet_pton(AF_INET, "224.0.0.251", &m.imr_multiaddr);
    m.imr_interface.s_addr = ip;
    setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m));
  }
}

bool Mdns::start() {
  int port = 5353;
  if (const char* e = std::getenv("NAMFX_MDNS_PORT"); e && *e) port = std::atoi(e);
  fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) return false;
  const int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
  setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));  // may share the port with avahi
#endif
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port);
  if (bind(fd_, (sockaddr*)&a, sizeof(a)) != 0) { ::close(fd_); fd_ = -1; return false; }
  joinGroups(fd_);
  run_.store(true);
  th_ = std::thread([this] { loop(); });
  return true;
}

void Mdns::stop() {
  run_.store(false);
  if (th_.joinable()) th_.join();
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

void Mdns::loop() {
  const std::string mine = lower(labels(name_)), myName = labels(name_);
  unsigned char buf[1500];
  int idle = 0;
  while (run_.load()) {
    pollfd pf{fd_, POLLIN, 0};
    if (poll(&pf, 1, 200) <= 0) {
      if (++idle >= 50) { idle = 0; joinGroups(fd_); }  // every ~10 s: pick up interfaces that came up later (hotspot, WiFi)
      continue;
    }
    sockaddr_in from{};
    socklen_t fl = sizeof(from);
    const ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
    if (n < 12 || (buf[2] & 0x80)) continue;  // too short, or a response
    const int qd = (buf[4] << 8) | buf[5];
    size_t pos = 12;
    bool match = false, unicast = false;
    for (int q = 0; q < qd && pos < (size_t)n; ++q) {
      const size_t start = pos;
      while (pos < (size_t)n && buf[pos] != 0) pos += (size_t)buf[pos] + 1;
      ++pos;  // the terminating zero
      if (pos + 4 > (size_t)n) break;
      const std::string qname = lower(std::string((const char*)buf + start, pos - start));
      const int type = (buf[pos] << 8) | buf[pos + 1], cls = (buf[pos + 2] << 8) | buf[pos + 3];
      pos += 4;
      if (qname == mine && (type == 1 || type == 255) && (cls & 0x7FFF) == 1) {
        match = true;
        unicast = (cls & 0x8000) != 0;
      }
    }
    if (!match) continue;
    const std::vector<uint32_t> ips = localIps();
    if (ips.empty()) continue;
    const bool legacy = ntohs(from.sin_port) != 5353;  // a plain resolver, not an mDNS querier: unicast reply, echo the id
    std::vector<unsigned char> r;
    auto put16 = [&](int v) { r.push_back((unsigned char)(v >> 8)); r.push_back((unsigned char)v); };
    put16(legacy ? ((buf[0] << 8) | buf[1]) : 0);
    put16(0x8400);
    put16(0);
    put16((int)ips.size());
    put16(0);
    put16(0);
    for (uint32_t ip : ips) {
      r.insert(r.end(), myName.begin(), myName.end());
      put16(1);
      put16(legacy ? 1 : 0x8001);  // IN (+ cache flush)
      put16(0);
      put16(120);
      put16(4);
      const unsigned char* b = (const unsigned char*)&ip;
      r.insert(r.end(), b, b + 4);
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    if (legacy || unicast) {
      to = from;
    } else {
      inet_pton(AF_INET, "224.0.0.251", &to.sin_addr);
      to.sin_port = htons(5353);
    }
    sendto(fd_, r.data(), r.size(), 0, (sockaddr*)&to, sizeof(to));
  }
}

}  // namespace fx
