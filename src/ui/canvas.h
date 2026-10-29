// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ui {

using Color = uint32_t;  // 0xAARRGGBB
constexpr Color rgb(uint32_t v) { return 0xFF000000u | v; }
constexpr Color rgba(uint32_t v, uint8_t a) { return ((uint32_t)a << 24) | (v & 0xFFFFFF); }
Color mix(Color a, Color b, float t);  // a -> b

enum class Align { Left, Center, Right };

class Font {
 public:
  bool load(const std::string& path);
  struct Glyph { int w = 0, h = 0, xoff = 0, yoff = 0; float adv = 0; std::vector<uint8_t> bmp; };
  const Glyph& glyph(uint32_t cp, int px);
  float ascent(int px) const;
  float kern(uint32_t a, uint32_t b, int px) const;

 private:
  std::vector<uint8_t> data_;
  struct Impl;
  std::shared_ptr<Impl> impl_;
  std::map<uint64_t, Glyph> cache_;
};

class Canvas {
 public:
  Canvas(int w, int h) : w_(w), h_(h), px_((size_t)w * h, 0xFF000000u) {}
  int width() const { return w_; }
  int height() const { return h_; }
  const uint32_t* pixels() const { return px_.data(); }

  void clear(Color c);
  void rect(float x, float y, float w, float h, Color c);
  void roundRect(float x, float y, float w, float h, float r, Color c);
  void roundRectV(float x, float y, float w, float h, float r, Color top, Color bottom);  // vertical gradient
  void roundStroke(float x, float y, float w, float h, float r, float t, Color c, float dash = 0);
  void radial(float cx, float cy, float rx, float ry, Color inner, Color outer);  // whole canvas
  void circle(float cx, float cy, float r, Color c);
  void ring(float cx, float cy, float r, float t, Color c);
  void arc(float cx, float cy, float r, float t, float a0, float a1, Color c);  // angles in radians, 0 = right, clockwise
  void line(float x0, float y0, float x1, float y1, float t, Color c);
  void glow(float x, float y, float w, float h, float r, float spread, Color c);

  float text(float x, float baseline, const std::string& s, Font& f, int px, Color c, Align a = Align::Left);
  float measure(const std::string& s, Font& f, int px);
  std::string ellipsize(const std::string& s, Font& f, int px, float maxW);

  bool savePng(const std::string& path) const;

 private:
  inline void blend(int x, int y, Color c, float cov);
  int w_, h_;
  std::vector<uint32_t> px_;
};

std::vector<uint32_t> utf8(const std::string& s);

}  // namespace ui
