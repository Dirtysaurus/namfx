// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cmath>
#include <cstring>
#include "ui/platform.h"

namespace ui {

// GO-Super Gamepad button codes (R36S, dArkOS)
static Btn mapCode(int code) {
  switch (code) {
    case 304: return Btn::A;
    case 305: return Btn::B;
    case 307: return Btn::X;
    case 308: return Btn::Y;
    case 310: return Btn::L1;
    case 311: return Btn::R1;
    case 312: return Btn::L2;
    case 313: return Btn::R2;
    case 544: return Btn::Up;
    case 545: return Btn::Down;
    case 546: return Btn::Left;
    case 547: return Btn::Right;
    case 704: return Btn::Select;
    case 705: return Btn::Start;
    default: return Btn::None;
  }
}

bool GamepadInput::open(const std::string& hint) {
  DIR* d = opendir("/dev/input");
  if (!d) return false;
  while (dirent* e = readdir(d)) {
    if (std::strncmp(e->d_name, "event", 5) != 0) continue;
    const std::string path = std::string("/dev/input/") + e->d_name;
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;
    char name[256] = {0};
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    if (std::strstr(name, hint.c_str())) {
      fd_ = fd;
      dev_ = path + " (" + name + ")";
      break;
    }
    ::close(fd);
  }
  closedir(d);
  return fd_ >= 0;
}

void GamepadInput::poll(const BtnFn& fn, const AxisFn& axis) {
  if (fd_ < 0) return;
  if (!axisInit_) { for (float& v : lastAxis_) v = -1.0f; axisInit_ = true; }
  input_event ev;
  const auto now = std::chrono::steady_clock::now();
  while (read(fd_, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
    if (ev.type == EV_ABS && axis && ev.code < 64) {
      input_absinfo ai;
      if (ioctl(fd_, EVIOCGABS(ev.code), &ai) == 0 && ai.maximum > ai.minimum) {
        const float v = (float)(ev.value - ai.minimum) / (float)(ai.maximum - ai.minimum);
        if (std::fabs(v - lastAxis_[ev.code]) >= 0.02f) { lastAxis_[ev.code] = v; axis(ev.code, v); }
      }
      continue;
    }
    if (ev.type != EV_KEY) continue;
    const Btn b = mapCode(ev.code);
    if (b == Btn::None) continue;
    const int i = (int)b;
    if (ev.value == 1) {
      held_[i] = true;
      next_[i] = now + std::chrono::milliseconds(380);
      fn(b, false);
    } else if (ev.value == 0) {
      held_[i] = false;
    }
  }
  // own autorepeat for the D-pad and L1/R1
  for (Btn b : {Btn::Up, Btn::Down, Btn::Left, Btn::Right, Btn::L1, Btn::R1}) {
    const int i = (int)b;
    if (held_[i] && now >= next_[i]) {
      next_[i] = now + std::chrono::milliseconds(70);
      fn(b, true);
    }
  }
}

void GamepadInput::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

}  // namespace ui
