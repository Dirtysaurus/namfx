// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fx {

struct HttpRequest {
  std::string method, path, query, body;
  std::map<std::string, std::string> headers;  // lower-case keys
  std::string param(const std::string& k) const;
};

struct HttpResponse {
  int status = 200;
  std::string type = "application/json";
  std::string body;
};

// Minimal HTTP/1.1 + WebSocket server (single thread, poll()).
class HttpServer {
 public:
  using ApiHandler = std::function<bool(const HttpRequest&, HttpResponse&)>;  // true = handled
  using WsHandler = std::function<std::string(const std::string&)>;          // returns the reply (or "")

  HttpServer(int port, std::string webRoot) : port_(port), root_(std::move(webRoot)) {}
  ~HttpServer() { stop(); }
  void setPort(int p) { port_ = p; }
  void onApi(ApiHandler h) { api_ = std::move(h); }
  void onWs(WsHandler h) { ws_ = std::move(h); }
  // HTTP Basic auth for everything (pages, /ws, uploads): user "namfx" + this password. Empty = open. Set before start().
  void setPassword(const std::string& pw);
  bool start(std::string& err);
  void stop();
  void broadcast(const std::string& text);  // thread-safe
  int clients() const { return wsCount_.load(); }

 private:
  struct Conn {
    int fd;
    std::string in, out;
    bool ws = false, closeAfter = false;
  };
  void loop();
  void onReadable(Conn& c);
  bool handleHttp(Conn& c);
  void handleWsData(Conn& c);
  void serveStatic(const HttpRequest& r, HttpResponse& resp);
  static std::string wsFrame(const std::string& payload, int opcode = 1);

  int port_;
  std::string root_;
  int lfd_ = -1, wakeR_ = -1, wakeW_ = -1;
  std::thread th_;
  std::atomic<bool> run_{false};
  std::vector<Conn> conns_;
  std::mutex qm_;
  std::vector<std::string> queue_;
  std::atomic<int> wsCount_{0};
  ApiHandler api_;
  WsHandler ws_;
  std::string authToken_;  // base64("namfx:" + password), empty = no auth
};

}  // namespace fx
