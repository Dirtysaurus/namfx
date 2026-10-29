// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <string>
#include <vector>

namespace fx {

struct WavData {
  int sampleRate = 0;
  int channels = 0;
  std::vector<float> data;  // interleaved
  size_t frames() const { return channels ? data.size() / channels : 0; }
};

bool readWav(const std::string& path, WavData& out, std::string& err);
bool writeWav(const std::string& path, const float* interleaved, size_t frames, int channels, int sampleRate);
std::vector<float> toMono(const WavData& w);
// Resampling (windowed sinc) for IRs and input WAVs.
std::vector<float> resample(const std::vector<float>& in, int srIn, int srOut);

}  // namespace fx
