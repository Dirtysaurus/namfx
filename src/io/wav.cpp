// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "io/wav.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace fx {

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool readWav(const std::string& path, WavData& out, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { err = "cannot open " + path; return false; }
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) || std::memcmp(buf.data() + 8, "WAVE", 4)) {
    err = "not a WAV file";
    return false;
  }
  int fmt = 0, ch = 0, bits = 0;
  uint32_t sr = 0;
  const uint8_t* data = nullptr;
  size_t dataLen = 0;
  size_t pos = 12;
  while (pos + 8 <= buf.size()) {
    const uint8_t* c = buf.data() + pos;
    uint32_t len = rd32(c + 4);
    const size_t body = pos + 8;
    if (body + len > buf.size()) len = (uint32_t)(buf.size() - body);
    if (!std::memcmp(c, "fmt ", 4) && len >= 16) {
      fmt = rd16(c + 8);
      ch = rd16(c + 10);
      sr = rd32(c + 12);
      bits = rd16(c + 22);
      if (fmt == 0xFFFE && len >= 40) fmt = rd16(c + 8 + 24);  // WAVE_FORMAT_EXTENSIBLE -> sub-format
    } else if (!std::memcmp(c, "data", 4)) {
      data = c + 8;
      dataLen = len;
    }
    pos = body + len + (len & 1);
  }
  if (!data || ch <= 0 || sr == 0) { err = "missing fmt/data chunk"; return false; }
  const int bps = bits / 8;
  if (bps <= 0) { err = "invalid bit depth"; return false; }
  const size_t n = dataLen / bps;
  out.sampleRate = (int)sr;
  out.channels = ch;
  out.data.resize(n - n % ch);
  for (size_t i = 0; i < out.data.size(); ++i) {
    const uint8_t* s = data + i * bps;
    float v = 0;
    if (fmt == 1) {
      if (bits == 16) v = (int16_t)rd16(s) / 32768.0f;
      else if (bits == 24) v = (int32_t)((s[0] << 8) | (s[1] << 16) | ((uint32_t)s[2] << 24)) / 2147483648.0f;
      else if (bits == 32) v = (int32_t)rd32(s) / 2147483648.0f;
      else if (bits == 8) v = (s[0] - 128) / 128.0f;
      else { err = "unsupported bit depth"; return false; }
    } else if (fmt == 3) {
      if (bits == 32) { float x; std::memcpy(&x, s, 4); v = x; }
      else if (bits == 64) { double x; std::memcpy(&x, s, 8); v = (float)x; }
      else { err = "unsupported float format"; return false; }
    } else {
      err = "unsupported WAV format (" + std::to_string(fmt) + ")";
      return false;
    }
    out.data[i] = v;
  }
  return true;
}

bool writeWav(const std::string& path, const float* x, size_t frames, int ch, int sr) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  auto w32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
  auto w16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
  const uint32_t dataLen = (uint32_t)(frames * ch * 4);
  f.write("RIFF", 4); w32(36 + dataLen); f.write("WAVE", 4);
  f.write("fmt ", 4); w32(16); w16(3); w16((uint16_t)ch); w32((uint32_t)sr); w32((uint32_t)sr * ch * 4); w16((uint16_t)(ch * 4)); w16(32);
  f.write("data", 4); w32(dataLen);
  f.write((const char*)x, dataLen);
  return (bool)f;
}

std::vector<float> toMono(const WavData& w) {
  std::vector<float> m(w.frames());
  for (size_t i = 0; i < m.size(); ++i) {
    float s = 0;
    for (int c = 0; c < w.channels; ++c) s += w.data[i * w.channels + c];
    m[i] = s / (float)w.channels;
  }
  return m;
}

std::vector<float> resample(const std::vector<float>& in, int srIn, int srOut) {
  if (srIn == srOut || in.empty()) return in;
  const double ratio = (double)srOut / srIn;
  const size_t outN = (size_t)std::ceil(in.size() * ratio);
  std::vector<float> out(outN);
  const int half = 32;
  const double cutoff = std::min(1.0, ratio) * 0.95;
  for (size_t i = 0; i < outN; ++i) {
    const double t = i / ratio;
    const long c = (long)std::floor(t);
    double acc = 0, wsum = 0;
    for (long k = c - half + 1; k <= c + half; ++k) {
      if (k < 0 || k >= (long)in.size()) continue;
      const double x = t - k;
      const double sx = x * cutoff;
      const double sinc = std::fabs(sx) < 1e-9 ? 1.0 : std::sin(M_PI * sx) / (M_PI * sx);
      const double wn = 0.42 + 0.5 * std::cos(M_PI * x / half) + 0.08 * std::cos(2 * M_PI * x / half);  // Blackman
      const double wgt = sinc * wn * cutoff;
      acc += in[k] * wgt;
      wsum += wgt;
    }
    out[i] = (float)acc;
    (void)wsum;
  }
  return out;
}

}  // namespace fx
