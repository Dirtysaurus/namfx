// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "ui/canvas.h"
#include <algorithm>
#include <cmath>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "ui/stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ui/stb_image_write.h"

namespace ui {

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

Color mix(Color a, Color b, float t) {
  t = clamp01(t);
  auto ch = [&](int s) { return (uint32_t)std::lround(((a >> s) & 0xFF) * (1 - t) + ((b >> s) & 0xFF) * t) << s; };
  return ch(24) | ch(16) | ch(8) | ch(0);
}

std::vector<uint32_t> utf8(const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    const uint8_t c = (uint8_t)s[i];
    uint32_t cp = c;
    int n = 0;
    if (c >= 0xF0) { cp = c & 0x07; n = 3; }
    else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
    else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
    ++i;
    for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | ((uint8_t)s[i] & 0x3F);
    out.push_back(cp);
  }
  return out;
}

// ------------------------------------------------------------ font

struct Font::Impl { stbtt_fontinfo info; };

bool Font::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  data_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  impl_ = std::make_shared<Impl>();
  return stbtt_InitFont(&impl_->info, data_.data(), 0) != 0;
}

float Font::ascent(int px) const {
  int a, d, l;
  stbtt_GetFontVMetrics(&impl_->info, &a, &d, &l);
  return a * stbtt_ScaleForPixelHeight(&impl_->info, (float)px);
}

float Font::kern(uint32_t a, uint32_t b, int px) const {
  return stbtt_GetCodepointKernAdvance(&impl_->info, (int)a, (int)b) * stbtt_ScaleForPixelHeight(&impl_->info, (float)px);
}

const Font::Glyph& Font::glyph(uint32_t cp, int px) {
  const uint64_t key = ((uint64_t)px << 32) | cp;
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  Glyph g;
  if (impl_) {
    const float sc = stbtt_ScaleForPixelHeight(&impl_->info, (float)px);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&impl_->info, (int)cp, &adv, &lsb);
    g.adv = adv * sc;
    unsigned char* b = stbtt_GetCodepointBitmap(&impl_->info, 0, sc, (int)cp, &g.w, &g.h, &g.xoff, &g.yoff);
    if (b) {
      g.bmp.assign(b, b + g.w * g.h);
      stbtt_FreeBitmap(b, nullptr);
    }
  }
  return cache_.emplace(key, std::move(g)).first->second;
}

// ------------------------------------------------------------ drawing

inline void Canvas::blend(int x, int y, Color c, float cov) {
  if ((unsigned)x >= (unsigned)w_ || (unsigned)y >= (unsigned)h_) return;
  const float a = ((c >> 24) & 0xFF) / 255.0f * cov;
  if (a <= 0.001f) return;
  uint32_t& d = px_[(size_t)y * w_ + x];
  if (a >= 0.999f) { d = 0xFF000000u | (c & 0xFFFFFF); return; }
  const int ia = (int)(a * 256);
  auto ch = [&](int s) { const int dv = (d >> s) & 0xFF, sv = (c >> s) & 0xFF; return (uint32_t)((dv + (((sv - dv) * ia) >> 8)) & 0xFF) << s; };
  d = 0xFF000000u | ch(16) | ch(8) | ch(0);
}

void Canvas::clear(Color c) { std::fill(px_.begin(), px_.end(), c); }

void Canvas::rect(float x, float y, float w, float h, Color c) {
  const int x0 = std::max(0, (int)std::floor(x)), y0 = std::max(0, (int)std::floor(y));
  const int x1 = std::min(w_, (int)std::ceil(x + w)), y1 = std::min(h_, (int)std::ceil(y + h));
  for (int j = y0; j < y1; ++j) {
    const float cy = clamp01(std::min((float)j + 1, y + h) - std::max((float)j, y));
    for (int i = x0; i < x1; ++i) {
      const float cx = clamp01(std::min((float)i + 1, x + w) - std::max((float)i, x));
      blend(i, j, c, cx * cy);
    }
  }
}

static inline float sdRound(float px, float py, float cx, float cy, float hw, float hh, float r) {
  const float qx = std::fabs(px - cx) - hw + r, qy = std::fabs(py - cy) - hh + r;
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

void Canvas::roundRectV(float x, float y, float w, float h, float r, Color top, Color bottom) {
  r = std::min(r, std::min(w, h) * 0.5f);
  const float cx = x + w * 0.5f, cy = y + h * 0.5f;
  const int x0 = std::max(0, (int)std::floor(x)), y0 = std::max(0, (int)std::floor(y));
  const int x1 = std::min(w_, (int)std::ceil(x + w)), y1 = std::min(h_, (int)std::ceil(y + h));
  for (int j = y0; j < y1; ++j) {
    const Color c = top == bottom ? top : mix(top, bottom, (j + 0.5f - y) / h);
    for (int i = x0; i < x1; ++i) {
      const float d = sdRound(i + 0.5f, j + 0.5f, cx, cy, w * 0.5f, h * 0.5f, r);
      if (d < 0.5f) blend(i, j, c, clamp01(0.5f - d));
    }
  }
}

void Canvas::roundRect(float x, float y, float w, float h, float r, Color c) { roundRectV(x, y, w, h, r, c, c); }

void Canvas::roundStroke(float x, float y, float w, float h, float r, float t, Color c, float dash) {
  r = std::min(r, std::min(w, h) * 0.5f);
  const float cx = x + w * 0.5f, cy = y + h * 0.5f;
  const int x0 = std::max(0, (int)std::floor(x - 1)), y0 = std::max(0, (int)std::floor(y - 1));
  const int x1 = std::min(w_, (int)std::ceil(x + w + 1)), y1 = std::min(h_, (int)std::ceil(y + h + 1));
  for (int j = y0; j < y1; ++j)
    for (int i = x0; i < x1; ++i) {
      const float d = sdRound(i + 0.5f, j + 0.5f, cx, cy, w * 0.5f, h * 0.5f, r);
      const float e = std::fabs(d + t * 0.5f) - t * 0.5f;  // band inside the edge
      if (e >= 0.5f) continue;
      if (dash > 0) {
        const float u = (std::fabs(i + 0.5f - cx) / w > std::fabs(j + 0.5f - cy) / h) ? (j + 0.5f) : (i + 0.5f);
        if (std::fmod(u, dash * 2) > dash) continue;
      }
      blend(i, j, c, clamp01(0.5f - e));
    }
}

void Canvas::radial(float cx, float cy, float rx, float ry, Color inner, Color outer) {
  for (int j = 0; j < h_; ++j)
    for (int i = 0; i < w_; ++i) {
      const float dx = (i - cx) / rx, dy = (j - cy) / ry;
      px_[(size_t)j * w_ + i] = mix(inner, outer, std::sqrt(dx * dx + dy * dy));
    }
}

void Canvas::circle(float cx, float cy, float r, Color c) {
  const int x0 = std::max(0, (int)(cx - r - 1)), x1 = std::min(w_, (int)(cx + r + 2));
  const int y0 = std::max(0, (int)(cy - r - 1)), y1 = std::min(h_, (int)(cy + r + 2));
  for (int j = y0; j < y1; ++j)
    for (int i = x0; i < x1; ++i) {
      const float d = std::hypot(i + 0.5f - cx, j + 0.5f - cy) - r;
      if (d < 0.5f) blend(i, j, c, clamp01(0.5f - d));
    }
}

void Canvas::ring(float cx, float cy, float r, float t, Color c) { arc(cx, cy, r, t, 0, 2 * (float)M_PI + 0.01f, c); }

void Canvas::arc(float cx, float cy, float r, float t, float a0, float a1, Color c) {
  const float ro = r + t * 0.5f;
  const int x0 = std::max(0, (int)(cx - ro - 1)), x1 = std::min(w_, (int)(cx + ro + 2));
  const int y0 = std::max(0, (int)(cy - ro - 1)), y1 = std::min(h_, (int)(cy + ro + 2));
  const float twoPi = 2 * (float)M_PI;
  for (int j = y0; j < y1; ++j)
    for (int i = x0; i < x1; ++i) {
      const float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
      const float d = std::fabs(std::hypot(dx, dy) - r) - t * 0.5f;
      if (d >= 0.5f) continue;
      float a = std::atan2(dy, dx);
      while (a < a0) a += twoPi;
      while (a > a0 + twoPi) a -= twoPi;
      if (a > a1) {
        // rounded caps
        const float ex0 = cx + r * std::cos(a0), ey0 = cy + r * std::sin(a0);
        const float ex1 = cx + r * std::cos(a1), ey1 = cy + r * std::sin(a1);
        const float dc = std::min(std::hypot(i + 0.5f - ex0, j + 0.5f - ey0), std::hypot(i + 0.5f - ex1, j + 0.5f - ey1)) - t * 0.5f;
        if (dc < 0.5f) blend(i, j, c, clamp01(0.5f - dc));
        continue;
      }
      blend(i, j, c, clamp01(0.5f - d));
    }
}

void Canvas::line(float x0, float y0, float x1, float y1, float t, Color c) {
  const float minx = std::min(x0, x1) - t, maxx = std::max(x0, x1) + t, miny = std::min(y0, y1) - t, maxy = std::max(y0, y1) + t;
  const float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
  for (int j = std::max(0, (int)miny); j < std::min(h_, (int)maxy + 1); ++j)
    for (int i = std::max(0, (int)minx); i < std::min(w_, (int)maxx + 1); ++i) {
      const float px = i + 0.5f, py = j + 0.5f;
      float u = l2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
      u = clamp01(u);
      const float d = std::hypot(px - (x0 + u * dx), py - (y0 + u * dy)) - t * 0.5f;
      if (d < 0.5f) blend(i, j, c, clamp01(0.5f - d));
    }
}

void Canvas::glow(float x, float y, float w, float h, float r, float spread, Color c) {
  const float cx = x + w * 0.5f, cy = y + h * 0.5f;
  const int x0 = std::max(0, (int)(x - spread)), x1 = std::min(w_, (int)(x + w + spread));
  const int y0 = std::max(0, (int)(y - spread)), y1 = std::min(h_, (int)(y + h + spread));
  for (int j = y0; j < y1; ++j)
    for (int i = x0; i < x1; ++i) {
      const float d = sdRound(i + 0.5f, j + 0.5f, cx, cy, w * 0.5f, h * 0.5f, r);
      if (d <= 0 || d > spread) continue;
      const float k = 1.0f - d / spread;
      blend(i, j, c, k * k * 0.6f);
    }
}

float Canvas::measure(const std::string& s, Font& f, int px) {
  float w = 0;
  auto cps = utf8(s);
  for (size_t i = 0; i < cps.size(); ++i) {
    w += f.glyph(cps[i], px).adv;
    if (i + 1 < cps.size()) w += f.kern(cps[i], cps[i + 1], px);
  }
  return w;
}

std::string Canvas::ellipsize(const std::string& s, Font& f, int px, float maxW) {
  if (measure(s, f, px) <= maxW) return s;
  auto cps = utf8(s);
  while (!cps.empty()) {
    cps.pop_back();
    std::string t;
    for (uint32_t cp : cps) {
      if (cp < 0x80) t += (char)cp;
      else if (cp < 0x800) { t += (char)(0xC0 | (cp >> 6)); t += (char)(0x80 | (cp & 0x3F)); }
      else { t += (char)(0xE0 | (cp >> 12)); t += (char)(0x80 | ((cp >> 6) & 0x3F)); t += (char)(0x80 | (cp & 0x3F)); }
    }
    t += "…";
    if (measure(t, f, px) <= maxW) return t;
  }
  return "…";
}

float Canvas::text(float x, float baseline, const std::string& s, Font& f, int px, Color c, Align a) {
  const float w = measure(s, f, px);
  if (a == Align::Center) x -= w * 0.5f;
  else if (a == Align::Right) x -= w;
  auto cps = utf8(s);
  float pen = x;
  for (size_t i = 0; i < cps.size(); ++i) {
    const auto& g = f.glyph(cps[i], px);
    const int gx = (int)std::lround(pen) + g.xoff, gy = (int)std::lround(baseline) + g.yoff;
    for (int j = 0; j < g.h; ++j)
      for (int k = 0; k < g.w; ++k) {
        const uint8_t v = g.bmp[j * g.w + k];
        if (v) blend(gx + k, gy + j, c, v / 255.0f);
      }
    pen += g.adv;
    if (i + 1 < cps.size()) pen += f.kern(cps[i], cps[i + 1], px);
  }
  return w;
}

bool Canvas::savePng(const std::string& path) const {
  std::vector<uint8_t> rgb((size_t)w_ * h_ * 3);
  for (size_t i = 0; i < px_.size(); ++i) {
    rgb[i * 3] = (px_[i] >> 16) & 0xFF;
    rgb[i * 3 + 1] = (px_[i] >> 8) & 0xFF;
    rgb[i * 3 + 2] = px_[i] & 0xFF;
  }
  return stbi_write_png(path.c_str(), w_, h_, 3, rgb.data(), w_ * 3) != 0;
}

}  // namespace ui
