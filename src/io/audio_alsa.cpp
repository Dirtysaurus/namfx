// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "io/audio_alsa.h"
#include <pthread.h>
#include <sched.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>
#include "engine.h"

#ifdef NAMFX_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

namespace fx {

void AudioDriver::setupThread() {
  enableFlushToZero();
  if (opt_.cpu >= 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(opt_.cpu, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  }
  sched_param sp{};
  sp.sched_priority = opt_.priority;
  if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
    std::fprintf(stderr, "[audio] cannot set SCHED_FIFO (run as root or set rtprio limits)\n");
}

void AudioDriver::stop() {
  run_.store(false);
  if (th_.joinable()) th_.join();
#ifdef NAMFX_HAVE_ALSA
  if (cap_) { snd_pcm_close((snd_pcm_t*)cap_); cap_ = nullptr; }
  if (pb_) { snd_pcm_close((snd_pcm_t*)pb_); pb_ = nullptr; }
#endif
}

#ifdef NAMFX_HAVE_ALSA
// 0 = S16, 1 = S32, 2 = S24_LE (in 32 bits)
static int setupPcm(snd_pcm_t* pcm, unsigned rate, snd_pcm_uframes_t period, unsigned periods, int& channels, int& fmt, std::string& err) {
  snd_pcm_hw_params_t* hw;
  snd_pcm_hw_params_alloca(&hw);
  snd_pcm_hw_params_any(pcm, hw);
  int e;
  if ((e = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) { err = "access"; return e; }
  if (snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S32_LE) == 0) fmt = 1;
  else if (snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S24_LE) == 0) fmt = 2;
  else if (snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE) == 0) fmt = 0;
  else { err = "format"; return -1; }
  unsigned ch = 2;
  if (snd_pcm_hw_params_set_channels_near(pcm, hw, &ch) < 0) { err = "channels"; return -1; }
  channels = (int)ch;
  unsigned r = rate;
  if ((e = snd_pcm_hw_params_set_rate(pcm, hw, r, 0)) < 0) { err = "sample rate " + std::to_string(rate); return e; }
  snd_pcm_uframes_t p = period;
  if ((e = snd_pcm_hw_params_set_period_size(pcm, hw, p, 0)) < 0) {
    if ((e = snd_pcm_hw_params_set_period_size_near(pcm, hw, &p, 0)) < 0 || p != period) { err = "period size " + std::to_string(period); return -1; }
  }
  unsigned np = periods;
  snd_pcm_hw_params_set_periods_near(pcm, hw, &np, 0);
  if ((e = snd_pcm_hw_params(pcm, hw)) < 0) { err = snd_strerror(e); return e; }
  snd_pcm_sw_params_t* sw;
  snd_pcm_sw_params_alloca(&sw);
  snd_pcm_sw_params_current(pcm, sw);
  snd_pcm_sw_params_set_start_threshold(pcm, sw, period);
  snd_pcm_sw_params_set_avail_min(pcm, sw, period);
  snd_pcm_sw_params(pcm, sw);
  return 0;
}
#endif

bool AudioDriver::start(bool nullDriver, std::string& err) {
  lost_.store(false);
#ifdef NAMFX_HAVE_ALSA
  if (!nullDriver) {
    const unsigned rate = (unsigned)eng_.config().sampleRate;
    const int period = eng_.config().block;
    snd_pcm_t *cap = nullptr, *pb = nullptr;
    int e;
    if ((e = snd_pcm_open(&pb, opt_.playback.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
      err = "playback " + opt_.playback + ": " + snd_strerror(e);
      return false;
    }
    if (setupPcm(pb, rate, period, opt_.periods, pbCh_, pbFmt_, err) < 0) { err = "playback: " + err; snd_pcm_close(pb); return false; }
    if (!opt_.capture.empty()) {
      if ((e = snd_pcm_open(&cap, opt_.capture.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        std::fprintf(stderr, "[audio] capture %s unavailable (%s) - WAV input only\n", opt_.capture.c_str(), snd_strerror(e));
        cap = nullptr;
      } else if (setupPcm(cap, rate, period, opt_.periods, capCh_, capFmt_, err) < 0) {
        std::fprintf(stderr, "[audio] capture: %s - WAV input only\n", err.c_str());
        snd_pcm_close(cap);
        cap = nullptr;
      }
    }
    if (cap && snd_pcm_link(cap, pb) < 0) std::fprintf(stderr, "[audio] snd_pcm_link failed, running unlinked\n");
    cap_ = cap;
    pb_ = pb;
    desc_ = "ALSA " + opt_.playback + (cap ? " / input " + opt_.capture : " / no input") + ", " + std::to_string(rate) + " Hz, period " +
            std::to_string(period);
    run_.store(true);
    th_ = std::thread([this] { runAlsa(); });
    return true;
  }
#endif
  desc_ = "null (timer), " + std::to_string((int)eng_.config().sampleRate) + " Hz";
  run_.store(true);
  th_ = std::thread([this] { runNull(); });
  (void)err;
  return true;
}

void AudioDriver::runNull() {
  setupThread();
  const int n = eng_.config().block;
  std::vector<float> l(n), r(n);
  const auto period = std::chrono::duration<double>(n / eng_.config().sampleRate);
  auto next = std::chrono::steady_clock::now();
  while (run_.load()) {
    const auto t0 = std::chrono::steady_clock::now();
    eng_.process(nullptr, l.data(), r.data(), n);
    const auto t1 = std::chrono::steady_clock::now();
    eng_.reportCpu((float)(std::chrono::duration<double>(t1 - t0).count() / period.count()));
    next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
    std::this_thread::sleep_until(next);
  }
}

void AudioDriver::runAlsa() {
#ifdef NAMFX_HAVE_ALSA
  setupThread();
  auto* cap = (snd_pcm_t*)cap_;
  auto* pb = (snd_pcm_t*)pb_;
  const int n = eng_.config().block;
  std::vector<int32_t> cbuf(n * capCh_ * 2), pbuf(n * pbCh_ * 2);
  std::vector<float> in(n), l(n), r(n);
  const double periodSec = n / eng_.config().sampleRate;
  auto toF = [](const void* b, int i, int fmt) -> float {
    if (fmt == 0) return ((const int16_t*)b)[i] / 32768.0f;
    if (fmt == 2) return (((const int32_t*)b)[i] << 8) / 2147483648.0f;
    return ((const int32_t*)b)[i] / 2147483648.0f;
  };
  auto prefill = [&]() {
    std::memset(pbuf.data(), 0, pbuf.size() * sizeof(int32_t));
    for (int k = 0; k < opt_.periods; ++k) snd_pcm_writei(pb, pbuf.data(), n);
  };
  snd_pcm_prepare(pb);
  if (cap) snd_pcm_prepare(cap);
  prefill();
  if (cap) snd_pcm_start(cap);
  while (run_.load()) {
    if (cap) {
      snd_pcm_sframes_t got = snd_pcm_readi(cap, cbuf.data(), n);
      if (got < 0) {
        if (got == -ENODEV || got == -EIO || got == -ENOTTY || got == -EBADFD) { lost_.store(true); break; }  // unplugged
        eng_.reportXrun();
        snd_pcm_recover(cap, (int)got, 1);
        if (snd_pcm_state(pb) != SND_PCM_STATE_RUNNING) { snd_pcm_prepare(pb); prefill(); }
        snd_pcm_start(cap);
        continue;
      }
      float pl = 0, pr = 0;
      for (int i = 0; i < n; ++i) {
        pl = std::max(pl, std::fabs(toF(cbuf.data(), i * capCh_, capFmt_)));
        if (capCh_ > 1) pr = std::max(pr, std::fabs(toF(cbuf.data(), i * capCh_ + 1, capFmt_)));
      }
      eng_.reportInputPeaks(pl, capCh_ > 1 ? pr : pl);
      for (int i = 0; i < n; ++i) {
        if (opt_.inChannel == 2 && capCh_ > 1) in[i] = 0.5f * (toF(cbuf.data(), i * capCh_, capFmt_) + toF(cbuf.data(), i * capCh_ + 1, capFmt_));
        else in[i] = toF(cbuf.data(), i * capCh_ + std::min(opt_.inChannel, capCh_ - 1), capFmt_);
      }
    }
    const auto t0 = std::chrono::steady_clock::now();
    eng_.process(cap ? in.data() : nullptr, l.data(), r.data(), n);
    const auto t1 = std::chrono::steady_clock::now();
    eng_.reportCpu((float)(std::chrono::duration<double>(t1 - t0).count() / periodSec));
    for (int i = 0; i < n; ++i) {
      for (int c = 0; c < pbCh_; ++c) {
        const float v = c == 1 ? r[i] : l[i];
        if (pbFmt_ == 0) ((int16_t*)pbuf.data())[i * pbCh_ + c] = (int16_t)std::lrintf(v * 32767.0f);
        else if (pbFmt_ == 2) pbuf[i * pbCh_ + c] = (int32_t)std::lrintf(v * 8388607.0f);
        else pbuf[i * pbCh_ + c] = (int32_t)std::lrint((double)v * 2147483647.0);
      }
    }
    snd_pcm_sframes_t w = snd_pcm_writei(pb, pbuf.data(), n);
    if (w < 0) {
      if (w == -ENODEV || w == -EIO || w == -ENOTTY || w == -EBADFD) { lost_.store(true); break; }
      eng_.reportXrun();
      snd_pcm_recover(pb, (int)w, 1);
      prefill();
    }
  }
  snd_pcm_drop(pb);
  if (cap) snd_pcm_drop(cap);
#endif
}

}  // namespace fx
