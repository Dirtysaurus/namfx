// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
//
// USB MIDI controller / footswitch via raw MIDI devices (/dev/snd/midiC*D*). Program change = Nth preset of the setlist,
// CC 11 = expression, CC 64 = next preset, CC 65 = previous preset. $NAMFX_MIDI_DEV names one file to read instead (tests).
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include "json.hpp"

namespace fx {

class MidiInput {
 public:
  using Send = std::function<void(const nlohmann::json&)>;
  explicit MidiInput(Send send) : send_(std::move(send)) {}
  ~MidiInput() { stop(); }
  void start();
  void stop();
  // feeds raw bytes through the parser (public for the tests)
  void feed(const unsigned char* data, int n);

 private:
  void loop();
  Send send_;
  std::thread th_;
  std::atomic<bool> run_{false};
  int status_ = 0, need_ = 0, n_ = 0;
  int data_[2] = {0, 0};
};

}  // namespace fx
