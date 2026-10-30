// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "io/audio_devices.h"
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace fx {

static std::string trim(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

// 'c' = capture, 'p' = playback: /proc/asound/cardN/pcm0c, pcm0p, ...
static bool hasPcm(const fs::path& dir, char dirChar) {
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    const std::string n = e.path().filename().string();
    if (n.size() >= 5 && n.compare(0, 3, "pcm") == 0 && std::isdigit((unsigned char)n[3]) && n.back() == dirChar) return true;
  }
  return false;
}

std::vector<AudioCard> scanAudioCards() {
  std::vector<AudioCard> out;
  const char* env = std::getenv("NAMFX_ASOUND_ROOT");
  const fs::path root = env && *env ? env : "/proc/asound";
  std::ifstream f(root / "cards");
  std::string line;
  while (std::getline(f, line)) {
    // " 1 [Twin           ]: USB-Audio - GO Twin"   (the next line is a long description)
    const size_t first = line.find_first_not_of(' ');
    if (first == std::string::npos || !std::isdigit((unsigned char)line[first])) continue;
    const size_t lb = line.find('['), rb = line.find(']');
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) continue;
    AudioCard c;
    c.index = std::atoi(line.c_str() + first);
    c.id = trim(line.substr(lb + 1, rb - lb - 1));
    const size_t colon = line.find(':', rb);
    const std::string rest = colon == std::string::npos ? "" : trim(line.substr(colon + 1));
    const size_t sep = rest.find(" - ");
    c.driver = sep == std::string::npos ? rest : trim(rest.substr(0, sep));
    c.name = sep == std::string::npos ? c.id : trim(rest.substr(sep + 3));
    const fs::path dir = root / ("card" + std::to_string(c.index));
    std::ifstream u(dir / "usbid");
    if (u) {
      std::getline(u, c.usbid);
      c.usbid = trim(c.usbid);
      c.usb = true;
    }
    if (c.driver == "USB-Audio") c.usb = true;
    c.capture = hasPcm(dir, 'c');
    c.playback = hasPcm(dir, 'p');
    out.push_back(c);
  }
  return out;
}

AudioResolved resolveAudio(const std::vector<AudioCard>& cards, const AudioChoice& choice) {
  auto duplex = [](const AudioCard& c) { return c.capture && c.playback; };
  auto internal = [&](const AudioCard& c) { return !c.usb && c.driver != "Loopback" && duplex(c); };
  AudioResolved r;
  const AudioCard* pick = nullptr;
  if (choice.device.rfind("card:", 0) == 0) {
    const std::string id = choice.device.substr(5);
    for (const auto& c : cards)
      if (c.id == id && duplex(c)) pick = &c;
    if (!pick) r.missing = id;  // unplugged or not usable: the same as auto
  }
  if (!pick && choice.device == "internal") {
    for (const auto& c : cards)
      if (internal(c)) { pick = &c; break; }
  }
  if (!pick && choice.device != "internal") {
    for (const auto& c : cards)
      if (c.usb && duplex(c)) { pick = &c; break; }
  }
  if (!pick) {
    for (const auto& c : cards)
      if (internal(c)) { pick = &c; break; }
  }
  if (pick) {
    r.found = true;
    r.usb = pick->usb;
    r.cardId = pick->id;
    r.label = pick->name;
    r.device = pick->usb ? "card:" + pick->id : "internal";
    // plughw converts the sample format (USB interfaces are often 24-bit packed)
    r.capture = r.playback = pick->usb ? "plughw:CARD=" + pick->id + ",DEV=0" : "hw:" + std::to_string(pick->index) + ",0";
  }
  r.channel = r.usb ? 0 : 1;
  if (choice.channel == "left") r.channel = 0;
  else if (choice.channel == "right") r.channel = 1;
  else if (choice.channel == "sum") r.channel = 2;
  return r;
}

}  // namespace fx
