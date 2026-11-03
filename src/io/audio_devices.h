// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
//
// Finds the sound cards (internal codec, USB interfaces) and picks the one namfx should use.
// Reads /proc/asound (or $NAMFX_ASOUND_ROOT, used by the tests), so it needs no ALSA library.
#pragma once
#include <string>
#include <vector>

namespace fx {

struct AudioCard {
  int index = 0;
  std::string id;      // "Twin" (the name between the brackets in /proc/asound/cards)
  std::string name;    // "GO Twin"
  std::string driver;  // "USB-Audio", "simple-card", ...
  std::string usbid;   // "0644:8010", USB cards only
  bool usb = false, capture = false, playback = false;
};

// What the user picked (data/settings.json -> "audio").
struct AudioChoice {
  std::string device = "auto";   // auto | internal | card:<id>
  std::string channel = "auto";  // auto | left | right | sum
  int hwGain = 40;               // internal codec only: capture sensitivity in percent (mixer control "Record")
  std::string output = "same";   // same (as the input card) | bluetooth | pcm:<ALSA name>
};

struct AudioResolved {
  std::string device = "internal";  // "internal" or "card:<id>": what the choice came to
  std::string capture = "hw:0,0", playback = "hw:0,0";
  std::string label;                // for the UIs
  std::string cardId;
  std::string output;               // custom playback device ("" = the input card)
  std::string missing;              // the chosen card isn't there (unplugged): we fell back
  int cardIndex = -1;               // ALSA card number of the picked card
  bool btRoute = false;             // output=bluetooth and the system routes "default" to Bluetooth right now
  bool usb = false, found = false;  // found = a card was picked from the list
  int channel = 1;                  // 0 left, 1 right, 2 sum
};

std::vector<AudioCard> scanAudioCards();
// The R36S Bluetooth setup (bt-vol service, bluealsa + alsaloop) writes /etc/asound.conf with "default" -> hw:Loopback,0,0
// while Bluetooth headphones are connected; alsaloop forwards that to the headphones. $NAMFX_ASOUND_CONF overrides the path (tests).
bool bluetoothRouteActive();
// auto = first USB interface with input and output, otherwise the internal codec.
// Input and output always come from the same card: two cards would have separate clocks and drift.
// auto channel: the internal jack only carries signal on the right channel, USB interfaces use the left.
AudioResolved resolveAudio(const std::vector<AudioCard>& cards, const AudioChoice& choice);

}  // namespace fx
