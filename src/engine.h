// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "dsp/graph.h"
#include "dsp/tuner.h"

namespace fx {

struct Config {
  std::string dataDir = "data";
  std::string langDir = "web/lang";
  std::string fxDir = "fx";   // bundled effect types (user types: data/fx)
  bool lv2 = true;            // scan LV2 plugins (system + data/lv2)
  float sampleRate = 48000.0f;
  int block = 128;
};

struct Location {
  int node = -1;   // index in the main chain
  int lane = 0;    // 0 = main chain, 1 = path A, 2 = path B
  int index = -1;  // index within the path (lane 1/2)
};

class Engine {
 public:
  explicit Engine(const Config& cfg);

  // --- control interface (thread-safe, used by the web server and the on-screen UI)
  json handle(const json& cmd);
  json state();
  json status();
  void controlTick();  // ~20 Hz: freeing, tuner, rebuilds
  unsigned version() const { return version_.load(); }
  bool loadPresetFile(const std::string& file, std::string& err);
  bool setWavInput(const std::string& file, bool loop, std::string& err);
  void setPlaying(bool p) { playing_.store(p); }

  // --- audio thread
  void process(const float* in, float* outL, float* outR, int n);
  void reportCpu(float load) { cpu_.store(cpu_.load() * 0.9f + load * 0.1f); }
  void reportXrun() { xruns_.fetch_add(1); }

  const Config& config() const { return cfg_; }
  std::string dataPath(const std::string& sub) const { return cfg_.dataDir + "/" + sub; }
  json settings();  // persisted UI settings (lang, theme, view, hotspot)
  // extensions living outside the engine (network): commands "net_*" not handled here + state part
  void setExtension(std::function<json(const json&)> cmd, std::function<json()> state) { extCmd_ = std::move(cmd); extState_ = std::move(state); }
  // state part "audio_dev" (devices, active input); the commands audio_apply / audio_scan go to the extension
  void setAudioState(std::function<json()> state) { audioState_ = std::move(state); }
  void touch() { changed(); }  // something outside the engine changed (state version bump)

 private:
  json handleLocked(const json& cmd);
  BlockPtr newBlock(const std::string& type, std::string& err);
  BlockPtr findBlock(const std::string& id, Location* loc = nullptr);
  std::vector<BlockPtr>* laneOf(const Location& loc);
  void rebuild();
  void changed() { version_.fetch_add(1); }
  json chainJson() const;
  bool chainFromJson(const json& chain, std::string& err);
  json filesJson() const;
  float cpuEstimate() const;
  void applyScene(int idx);
  void saveSettings();
  void updateMeasured();

  Config cfg_;
  std::recursive_mutex mtx_;
  std::vector<Node> nodes_;
  int nextId_ = 1;
  std::string presetName_ = "New preset", presetFile_;
  json scenes_ = json::array();
  int scene_ = -1;
  json settings_ = json::object();
  std::function<json(const json&)> extCmd_;
  std::function<json()> extState_;
  std::function<json()> audioState_;
  std::map<std::string, float> measured_;  // type -> measured load [% of a core], persisted in data/cpu_stats.json
  bool measuredDirty_ = false;
  unsigned ticks_ = 0;

  Swap<Graph> graph_;
  std::vector<float> L_, R_;

  // global parameters
  std::atomic<float> inGain_{0.0f}, outGain_{0.0f};
  std::atomic<bool> tunerOn_{false}, tunerMute_{true};
  float a4_ = 440.0f;
  Smoother inSm_, outSm_;
  Tuner tuner_;
  TunerResult tunerRes_;

  // WAV file input
  struct WavBuf { std::vector<float> data; };
  Swap<WavBuf> wav_;
  std::string wavFile_;
  std::atomic<bool> useWav_{false}, playing_{false}, loop_{true};
  std::atomic<size_t> wavPos_{0}, wavLen_{0};

  // metering
  std::atomic<float> peakIn_{0}, peakOut_{0}, cpu_{0};
  std::atomic<unsigned> xruns_{0};
  std::atomic<unsigned> version_{1};
};

}  // namespace fx
