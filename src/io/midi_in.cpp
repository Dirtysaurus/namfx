// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "io/midi_in.h"
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace fx {

void MidiInput::feed(const unsigned char* data, int n) {
  for (int i = 0; i < n; ++i) {
    const int b = data[i];
    if (b >= 0xF8) continue;  // realtime bytes may appear anywhere
    if (b & 0x80) {
      status_ = b;
      n_ = 0;
      const int t = b & 0xF0;
      need_ = (t == 0xC0 || t == 0xD0) ? 1 : (t < 0xF0 ? 2 : 0);
      if (need_ == 0) status_ = 0;  // system messages (sysex ...) are ignored
      continue;
    }
    if (!status_) continue;
    data_[n_++] = b;
    if (n_ < need_) continue;
    n_ = 0;  // running status: the next data bytes belong to the same status
    const int t = status_ & 0xF0;
    if (t == 0xC0) {
      send_({{"cmd", "preset_index"}, {"index", data_[0]}});
    } else if (t == 0xB0) {
      const int cc = data_[0], v = data_[1];
      if (cc == 11) send_({{"cmd", "expression"}, {"value", v / 127.0}});
      else if (cc == 64 && v >= 64) send_({{"cmd", "preset_step"}, {"dir", 1}});
      else if (cc == 65 && v >= 64) send_({{"cmd", "preset_step"}, {"dir", -1}});
    }
  }
}

void MidiInput::start() {
  run_.store(true);
  th_ = std::thread([this] { loop(); });
}

void MidiInput::stop() {
  run_.store(false);
  if (th_.joinable()) th_.join();
}

void MidiInput::loop() {
  std::vector<int> fds;
  auto lastScan = std::chrono::steady_clock::time_point{};
  while (run_.load()) {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastScan > std::chrono::seconds(3)) {  // (re)scan: controllers come and go
      lastScan = now;
      for (int fd : fds) ::close(fd);
      fds.clear();
      if (const char* env = std::getenv("NAMFX_MIDI_DEV"); env && *env) {
        const int fd = ::open(env, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) fds.push_back(fd);
      } else if (DIR* d = opendir("/dev/snd")) {
        while (dirent* e = readdir(d))
          if (std::strncmp(e->d_name, "midiC", 5) == 0) {
            const int fd = ::open((std::string("/dev/snd/") + e->d_name).c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0) fds.push_back(fd);
          }
        closedir(d);
      }
    }
    bool any = false;
    unsigned char buf[256];
    for (size_t i = 0; i < fds.size(); ++i) {
      const ssize_t r = ::read(fds[i], buf, sizeof(buf));
      if (r > 0) { feed(buf, (int)r); any = true; }
    }
    if (!any) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  for (int fd : fds) ::close(fd);
}

}  // namespace fx
