// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "net/http_server.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

namespace fx {

// ------------------------------------------------------------ SHA-1 + base64 (WebSocket handshake)

static std::string sha1(const std::string& s) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::string m = s;
  const uint64_t bits = (uint64_t)s.size() * 8;
  m.push_back((char)0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; --i) m.push_back((char)((bits >> (i * 8)) & 0xFF));
  auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = ((uint8_t)m[off + 4 * i] << 24) | ((uint8_t)m[off + 4 * i + 1] << 16) | ((uint8_t)m[off + 4 * i + 2] << 8) | (uint8_t)m[off + 4 * i + 3];
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  std::string out;
  for (uint32_t v : h)
    for (int i = 3; i >= 0; --i) out.push_back((char)((v >> (i * 8)) & 0xFF));
  return out;
}

static std::string base64(const std::string& s) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  size_t i = 0;
  for (; i + 2 < s.size(); i += 3) {
    const uint32_t v = ((uint8_t)s[i] << 16) | ((uint8_t)s[i + 1] << 8) | (uint8_t)s[i + 2];
    o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
  }
  if (i + 1 == s.size()) {
    const uint32_t v = (uint8_t)s[i] << 16;
    o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
  } else if (i + 2 == s.size()) {
    const uint32_t v = ((uint8_t)s[i] << 16) | ((uint8_t)s[i + 1] << 8);
    o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
  }
  return o;
}

static std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
    else if (s[i] == '+') o += ' ';
    else o += s[i];
  }
  return o;
}

std::string HttpRequest::param(const std::string& k) const {
  std::stringstream ss(query);
  std::string kv;
  while (std::getline(ss, kv, '&')) {
    const auto eq = kv.find('=');
    if (eq != std::string::npos && urlDecode(kv.substr(0, eq)) == k) return urlDecode(kv.substr(eq + 1));
  }
  return "";
}

// ------------------------------------------------------------ server

bool HttpServer::start(std::string& err) {
  lfd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (lfd_ < 0) { err = "socket"; return false; }
  int one = 1;
  setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port_);
  a.sin_addr.s_addr = INADDR_ANY;
  if (bind(lfd_, (sockaddr*)&a, sizeof(a)) < 0) { err = "port " + std::to_string(port_) + ": " + std::strerror(errno); close(lfd_); lfd_ = -1; return false; }
  listen(lfd_, 16);
  fcntl(lfd_, F_SETFL, O_NONBLOCK);
  int p[2];
  if (pipe(p) < 0) { err = "pipe"; return false; }
  wakeR_ = p[0];
  wakeW_ = p[1];
  fcntl(wakeR_, F_SETFL, O_NONBLOCK);
  run_.store(true);
  th_ = std::thread([this] { loop(); });
  return true;
}

void HttpServer::stop() {
  if (!run_.exchange(false)) return;
  if (wakeW_ >= 0) { char c = 1; (void)!write(wakeW_, &c, 1); }
  if (th_.joinable()) th_.join();
  for (auto& c : conns_) close(c.fd);
  conns_.clear();
  if (lfd_ >= 0) close(lfd_);
  if (wakeR_ >= 0) close(wakeR_);
  if (wakeW_ >= 0) close(wakeW_);
  lfd_ = wakeR_ = wakeW_ = -1;
}

void HttpServer::broadcast(const std::string& text) {
  {
    std::lock_guard<std::mutex> lk(qm_);
    queue_.push_back(text);
  }
  char c = 1;
  (void)!write(wakeW_, &c, 1);
}

std::string HttpServer::wsFrame(const std::string& p, int opcode) {
  std::string f;
  f.push_back((char)(0x80 | opcode));
  if (p.size() < 126) f.push_back((char)p.size());
  else if (p.size() < 65536) { f.push_back(126); f.push_back((char)(p.size() >> 8)); f.push_back((char)(p.size() & 0xFF)); }
  else { f.push_back(127); for (int i = 7; i >= 0; --i) f.push_back((char)(((uint64_t)p.size() >> (i * 8)) & 0xFF)); }
  return f + p;
}

void HttpServer::loop() {
  while (run_.load()) {
    std::vector<pollfd> pf;
    pf.push_back({lfd_, POLLIN, 0});
    pf.push_back({wakeR_, POLLIN, 0});
    for (auto& c : conns_) pf.push_back({c.fd, (short)(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
    if (poll(pf.data(), pf.size(), 500) < 0) continue;
    if (pf[1].revents & POLLIN) {
      char tmp[256];
      while (read(wakeR_, tmp, sizeof(tmp)) > 0) {}
      std::vector<std::string> q;
      {
        std::lock_guard<std::mutex> lk(qm_);
        q.swap(queue_);
      }
      for (auto& msg : q) {
        const std::string fr = wsFrame(msg);
        for (auto& c : conns_)
          if (c.ws && c.out.size() < 4 * 1024 * 1024) c.out += fr;  // slow client: drop
      }
    }
    for (size_t i = 0; i < conns_.size(); ++i) {
      auto& c = conns_[i];
      const auto& p = pf[i + 2];
      if (p.fd != c.fd) continue;
      if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { c.closeAfter = true; c.out.clear(); continue; }
      if (p.revents & POLLIN) onReadable(c);
      if ((p.revents & POLLOUT) && !c.out.empty()) {
        const ssize_t w = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (w > 0) c.out.erase(0, (size_t)w);
        else if (w < 0 && errno != EAGAIN) { c.closeAfter = true; c.out.clear(); }
      }
    }
    // flush freshly queued replies right away
    for (auto& c : conns_) {
      if (!c.out.empty()) {
        const ssize_t w = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w > 0) c.out.erase(0, (size_t)w);
      }
    }
    conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                                [&](Conn& c) {
                                  if (c.closeAfter && c.out.empty()) {
                                    if (c.ws) wsCount_.fetch_sub(1);
                                    close(c.fd);
                                    return true;
                                  }
                                  return false;
                                }),
                 conns_.end());
    if (pf[0].revents & POLLIN) {
      for (;;) {
        const int fd = accept(lfd_, nullptr, nullptr);
        if (fd < 0) break;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        conns_.push_back({fd, "", "", false, false});
      }
    }
  }
}

void HttpServer::onReadable(Conn& c) {
  char buf[65536];
  for (;;) {
    const ssize_t r = recv(c.fd, buf, sizeof(buf), 0);
    if (r > 0) { c.in.append(buf, (size_t)r); if (c.in.size() > 200u * 1024 * 1024) { c.closeAfter = true; return; } continue; }
    if (r == 0) { c.closeAfter = true; }
    break;
  }
  if (c.ws) handleWsData(c);
  else while (!c.ws && !c.closeAfter && handleHttp(c)) {}
}

static const char* mimeOf(const std::string& p) {
  auto ends = [&](const char* e) { const size_t n = std::strlen(e); return p.size() >= n && p.compare(p.size() - n, n, e) == 0; };
  if (ends(".html")) return "text/html; charset=utf-8";
  if (ends(".js")) return "application/javascript; charset=utf-8";
  if (ends(".css")) return "text/css; charset=utf-8";
  if (ends(".svg")) return "image/svg+xml";
  if (ends(".png")) return "image/png";
  if (ends(".json")) return "application/json";
  if (ends(".ttf")) return "font/ttf";
  return "application/octet-stream";
}

static const unsigned long long kMaxBody = 128ull * 1024 * 1024;  // largest upload (a NAM model / IR is a few MB)

void HttpServer::setPassword(const std::string& pw) { authToken_ = pw.empty() ? std::string() : base64("namfx:" + pw); }

void HttpServer::serveStatic(const HttpRequest& r, HttpResponse& resp) {
  std::string p = r.path == "/" ? "/index.html" : r.path;
  if (p.find("..") != std::string::npos) { resp.status = 403; resp.body = "forbidden"; return; }
  std::ifstream f(root_ + p, std::ios::binary);
  if (!f) { resp.status = 404; resp.type = "text/plain"; resp.body = "not found"; return; }
  std::stringstream ss;
  ss << f.rdbuf();
  resp.body = ss.str();
  resp.type = mimeOf(p);
}

bool HttpServer::handleHttp(Conn& c) {
  const auto he = c.in.find("\r\n\r\n");
  if (he == std::string::npos) return false;
  HttpRequest r;
  std::stringstream hs(c.in.substr(0, he));
  std::string line;
  std::getline(hs, line);
  {
    std::stringstream ls(line);
    std::string target;
    ls >> r.method >> target;
    const auto q = target.find('?');
    r.path = urlDecode(target.substr(0, q));
    if (q != std::string::npos) r.query = target.substr(q + 1);
  }
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto col = line.find(':');
    if (col == std::string::npos) continue;
    std::string k = line.substr(0, col), v = line.substr(col + 1);
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
    while (!v.empty() && v[0] == ' ') v.erase(0, 1);
    r.headers[k] = v;
  }
  size_t len = 0;
  if (r.headers.count("content-length")) {
    const std::string& cl = r.headers["content-length"];
    if (cl.empty() || cl.size() > 12 || cl.find_first_not_of("0123456789") != std::string::npos || std::stoull(cl) > kMaxBody) {
      const std::string body = "payload too large or invalid";
      c.out += "HTTP/1.1 413 Payload Too Large\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body;
      c.closeAfter = true;
      c.in.clear();
      return false;
    }
    len = std::stoull(cl);
  }
  if (c.in.size() < he + 4 + len) return false;
  r.body = c.in.substr(he + 4, len);
  c.in.erase(0, he + 4 + len);

  if (!authToken_.empty()) {
    const std::string want = "Basic " + authToken_;
    const std::string got = r.headers.count("authorization") ? r.headers["authorization"] : std::string();
    unsigned diff = (unsigned)(want.size() ^ got.size());  // constant-time compare
    for (size_t i = 0; i < want.size() && i < got.size(); ++i) diff |= (unsigned)(want[i] ^ got[i]);
    if (diff != 0) {
      const std::string body = "authentication required";
      c.out += "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"namfx\"\r\nContent-Type: text/plain\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\nConnection: keep-alive\r\n\r\n" + body;
      return true;
    }
  }
  if (r.path == "/ws" && r.headers.count("sec-websocket-key")) {
    const std::string acc = base64(sha1(r.headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
    c.out += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + acc + "\r\n\r\n";
    c.ws = true;
    wsCount_.fetch_add(1);
    if (ws_) {
      const std::string hello = ws_("{\"cmd\":\"hello\"}");
      if (!hello.empty()) c.out += wsFrame(hello);
    }
    if (!c.in.empty()) handleWsData(c);
    return false;
  }
  HttpResponse resp;
  if (!(api_ && api_(r, resp))) serveStatic(r, resp);
  const char* st = resp.status == 200 ? "OK" : resp.status == 404 ? "Not Found" : resp.status == 400 ? "Bad Request" : "Error";
  c.out += "HTTP/1.1 " + std::to_string(resp.status) + " " + st + "\r\nContent-Type: " + resp.type +
           "\r\nContent-Length: " + std::to_string(resp.body.size()) + "\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n" + resp.body;
  return true;
}

void HttpServer::handleWsData(Conn& c) {
  for (;;) {
    if (c.in.size() < 2) return;
    const uint8_t b0 = (uint8_t)c.in[0], b1 = (uint8_t)c.in[1];
    const int op = b0 & 0x0F;
    const bool masked = b1 & 0x80;
    uint64_t len = b1 & 0x7F;
    size_t pos = 2;
    if (len == 126) { if (c.in.size() < 4) return; len = ((uint8_t)c.in[2] << 8) | (uint8_t)c.in[3]; pos = 4; }
    else if (len == 127) { if (c.in.size() < 10) return; len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | (uint8_t)c.in[2 + i]; pos = 10; }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) { if (c.in.size() < pos + 4) return; std::memcpy(mask, c.in.data() + pos, 4); pos += 4; }
    if (c.in.size() < pos + len) return;
    std::string payload = c.in.substr(pos, len);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i & 3];
    c.in.erase(0, pos + len);
    if (op == 8) { c.out += wsFrame("", 8); c.closeAfter = true; return; }
    if (op == 9) { c.out += wsFrame(payload, 10); continue; }
    if (op == 1 && ws_) {
      const std::string reply = ws_(payload);
      if (!reply.empty()) c.out += wsFrame(reply);
    }
  }
}

}  // namespace fx
