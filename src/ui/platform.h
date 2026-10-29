// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include "ui/canvas.h"
#include "ui/screens.h"

namespace ui {

using BtnFn = std::function<void(Btn, bool repeat)>;

// Display output via SDL2 (KMSDRM on the R36S, a window on a PC).
class Display {
 public:
  bool open(int w, int h, std::string& err);
  void present(const Canvas& c);
  bool pollKeys(const BtnFn& fn);  // keyboard (PC); false = close requested
  void close();
  ~Display() { close(); }

 private:
  void* win_ = nullptr;
  void* ren_ = nullptr;
  void* tex_ = nullptr;
};

// R36S gamepad via evdev (works without a display too).
class GamepadInput {
 public:
  bool open(const std::string& nameHint = "GO-Super Gamepad");
  void poll(const BtnFn& fn);
  void close();
  ~GamepadInput() { close(); }
  bool selectHeld() const { return held_[(int)Btn::Select]; }
  bool startHeld() const { return held_[(int)Btn::Start]; }
  std::string device() const { return dev_; }

 private:
  int fd_ = -1;
  std::string dev_;
  bool held_[16] = {};
  std::chrono::steady_clock::time_point next_[16];
};

}  // namespace ui
