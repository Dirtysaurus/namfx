// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
// namfx - NAM guitar multi-effect for the R36S (dArkOS)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>
#include <atomic>
#include <csignal>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <algorithm>
#include "dsp/engines.h"
#include "engine.h"
#include "i18n.h"
#include "io/audio_alsa.h"
#include "io/audio_devices.h"
#include "io/wav.h"
#include "net/hotspot.h"
#include "net/http_server.h"
#include "ui/platform.h"

namespace fs = std::filesystem;
using fx::json;

static volatile std::sig_atomic_t g_quit = 0;
static void onSignal(int) { g_quit = 1; }

static std::string localIp() {
  ifaddrs* ifs = nullptr;
  std::string best;
  if (getifaddrs(&ifs) != 0) return "";
  for (ifaddrs* i = ifs; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK)) continue;
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, buf, sizeof(buf));
    if (best.empty() || std::string(i->ifa_name).rfind("wl", 0) == 0) best = buf;
  }
  freeifaddrs(ifs);
  return best;
}

static void usage() {
  std::puts(
      "namfx [options]\n"
      "  --data DIR          data (nam/, ir/, presets/, wav/, fx/, lv2/), default ./data\n"
      "  --fx DIR            bundled effect types, default ./fx\n"
      "  --no-lv2            do not scan LV2 plugins\n"
      "  --no-hotspot        never open the WiFi hotspot (see System -> Network)\n"
      "  --web DIR           web UI (incl. lang/), default ./web\n"
      "  --fonts DIR         fonts, default ./fonts\n"
      "  --lang CODE         UI language (en, cs, ...), default from data/settings.json or en\n"
      "  --port N            web server (80, falls back to 8080)\n"
      "  --rate N --block N  sample rate (48000) and period (128)\n"
      "  --capture DEV       ALSA input, '' = no input (default: the card chosen under System -> Audio)\n"
      "  --playback DEV      ALSA output (default: the same card)\n"
      "  --in-channel N      0 left, 1 right, 2 sum (default: right for the internal jack, left for USB)\n"
      "  --cpu N             CPU core for the audio thread (3)\n"
      "  --null-audio        no sound card (timer driven) - UI testing\n"
      "  --no-display        no on-screen UI (buttons still work)\n"
      "  --preset FILE       load a preset from data/presets\n"
      "  --wav FILE          loop data/wav/FILE as input (no guitar needed)\n"
      "  --offline IN OUT    process a WAV file and exit\n"
      "  --snapshots DIR     save PNGs of all screens and exit\n"
      "  --print-state       print state (JSON) and exit\n"
      "  --print-catalog     print the block catalog (JSON, English) and exit\n"
      "  --print-engines     print the engines and their internal parameters and exit\n"
      "  --print-log         print the catalog log (type files, LV2 plugins) and exit\n"
      "  --bench [SEC]       measure every block type (noise input) and exit");
}

// Measures every catalog type on this machine. Load = share of one core at the configured
// sample rate / period. On a PC the A35 estimate uses the measured ratio PC -> R36S.
static int runBench(fx::Engine& eng, double seconds) {
  fx::enableFlushToZero();
  const int n = eng.config().block;
  const float sr = eng.config().sampleRate;
  std::vector<float> L(n), R(n);
  fx::Rng rng;
  std::printf("%-34s %-6s %9s\n", "type", "source", "load %");
  json out = json::object();
  for (const fx::BlockDef* d : fx::catalogList()) {
    if (d->type == "split" || !d->fileKind.empty()) continue;
    auto b = fx::makeBlock(d);
    if (!b) continue;
    b->prepare(sr, n);
    const size_t blocks = (size_t)(seconds * sr / n);
    double t = 0;
    for (size_t k = 0; k < blocks; ++k) {
      for (int i = 0; i < n; ++i) { L[i] = 0.2f * rng.next(); R[i] = L[i]; }
      const auto t0 = std::chrono::steady_clock::now();
      b->run(L.data(), R.data(), n, d->stereo);
      t += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    const double pct = 100.0 * t / (blocks * (double)n / sr);
    out[d->type] = std::round(pct * 100) / 100;
    std::printf("%-34.34s %-6s %9.2f\n", d->type.c_str(), d->source.c_str(), pct);
  }
  std::fprintf(stderr, "%s\n", out.dump().c_str());
  return 0;
}

static int runOffline(fx::Engine& eng, const std::string& in, const std::string& out) {
  fx::enableFlushToZero();
  fx::WavData w;
  std::string err;
  if (!fx::readWav(in, w, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
  auto mono = fx::resample(fx::toMono(w), w.sampleRate, (int)eng.config().sampleRate);
  const int n = eng.config().block;
  const size_t frames = mono.size();
  std::vector<float> outI((frames + n) * 2), l(n), r(n), blk(n);
  const auto t0 = std::chrono::steady_clock::now();
  for (size_t pos = 0; pos < frames; pos += n) {
    for (int i = 0; i < n; ++i) blk[i] = pos + i < frames ? mono[pos + i] : 0.0f;
    eng.process(blk.data(), l.data(), r.data(), n);
    for (int i = 0; i < n; ++i) { outI[(pos + i) * 2] = l[i]; outI[(pos + i) * 2 + 1] = r[i]; }
    if ((pos / n) % 64 == 0) eng.controlTick();
  }
  const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const double audioSec = frames / eng.config().sampleRate;
  fx::writeWav(out, outI.data(), frames, 2, (int)eng.config().sampleRate);
  std::printf("offline: %.2f s of audio in %.2f s (%.1fx realtime, core load %.1f %%)\n", audioSec, sec, audioSec / sec, 100.0 * sec / audioSec);
  return 0;
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  fx::Config cfg;
  fx::AudioOptions ao;
  std::string webDir = "web", fontDir = "fonts", lang, preset, wav, offIn, offOut, snapDir;
  int port = 80;
  bool nullAudio = false, noDisplay = false, printState = false, printCatalog = false, printLog = false, noHotspot = false;
  bool captureGiven = false, playbackGiven = false, inChannelGiven = false;  // explicit options beat System -> Audio
  double bench = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--data") cfg.dataDir = next();
    else if (a == "--fx") cfg.fxDir = next();
    else if (a == "--no-lv2") cfg.lv2 = false;
    else if (a == "--no-hotspot") noHotspot = true;
    else if (a == "--print-engines") { std::puts(fx::enginesJson().dump(1).c_str()); return 0; }
    else if (a == "--print-log") printLog = true;
    else if (a == "--bench") { bench = 3; if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0])) bench = std::stod(next()); }
    else if (a == "--web") webDir = next();
    else if (a == "--fonts") fontDir = next();
    else if (a == "--lang") lang = next();
    else if (a == "--port") port = std::stoi(next());
    else if (a == "--rate") cfg.sampleRate = std::stof(next());
    else if (a == "--block") cfg.block = std::stoi(next());
    else if (a == "--capture") { ao.capture = next(); captureGiven = true; }
    else if (a == "--playback") { ao.playback = next(); playbackGiven = true; }
    else if (a == "--in-channel") { ao.inChannel = std::stoi(next()); inChannelGiven = true; }
    else if (a == "--cpu") ao.cpu = std::stoi(next());
    else if (a == "--null-audio") nullAudio = true;
    else if (a == "--no-display") noDisplay = true;
    else if (a == "--preset") preset = next();
    else if (a == "--wav") wav = next();
    else if (a == "--offline") { offIn = next(); offOut = next(); }
    else if (a == "--snapshots") snapDir = next();
    else if (a == "--print-state") printState = true;
    else if (a == "--print-catalog") printCatalog = true;
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
  }

  cfg.langDir = webDir + "/lang";
  if (lang.empty()) {  // persisted language choice
    std::ifstream sf(cfg.dataDir + "/settings.json");
    json st = json::parse(sf, nullptr, false);
    if (!st.is_discarded() && st.is_object()) lang = st.value("lang", "");
  }
  if (lang.empty()) lang = "en";
  if (!fx::i18n().load(cfg.langDir, lang)) std::fprintf(stderr, "[i18n] language %s not found, using en\n", lang.c_str());
  fx::Engine eng(cfg);
  std::string err;
  if (printCatalog) { std::puts(fx::catalogJson().dump(1).c_str()); return 0; }
  if (printLog) { std::puts(fx::catalogLog().dump(1).c_str()); return 0; }
  if (bench > 0) return runBench(eng, bench);
  if (preset.empty()) {  // last used preset
    std::ifstream lp(cfg.dataDir + "/last_preset.txt");
    std::getline(lp, preset);
    if (preset.empty() || !fs::exists(eng.dataPath("presets") + "/" + preset)) {  // otherwise the first preset
      preset.clear();
      std::vector<std::string> all;
      std::error_code ec;
      for (auto& e : fs::directory_iterator(eng.dataPath("presets"), ec))
        if (e.path().extension() == ".json") all.push_back(e.path().filename().string());
      std::sort(all.begin(), all.end());
      if (!all.empty()) preset = all.front();
    }
  }
  if (!preset.empty() && !eng.loadPresetFile(preset, err)) std::fprintf(stderr, "[preset] %s: %s\n", preset.c_str(), err.c_str());
  if (printState) { auto st = eng.state(); st.erase("files"); std::puts(st.dump(2).c_str()); return 0; }
  if (!offIn.empty()) return runOffline(eng, offIn, offOut);
  if (!wav.empty()) {
    if (!eng.setWavInput(wav, true, err)) std::fprintf(stderr, "[wav] %s\n", err.c_str());
    else eng.setPlaying(true);
  }

  ui::Screens screens(eng, fontDir);
  const std::string ip = localIp();
  screens.setNetInfo(ip.empty() ? fx::T("ui.no_network", "no network") : "web " + ip + (port == 80 ? "" : ":" + std::to_string(port)));

  if (!snapDir.empty()) {
    fs::create_directories(snapDir);
    ui::Canvas cv(640, 480);
    auto shot = [&](const std::string& name) {
      for (int k = 0; k < 3; ++k) screens.update();
      screens.render(cv);
      cv.savePng(snapDir + "/" + name + ".png");
    };
    shot("1_rig");
    for (int k = 0; k < 4; ++k) screens.press(ui::Btn::Right);
    shot("2_rig_lane");
    screens.press(ui::Btn::A);
    shot("3_edit");
    screens.press(ui::Btn::A);
    shot("4_files");
    screens.press(ui::Btn::B);
    screens.press(ui::Btn::Select);  // back from the editor
    screens.press(ui::Btn::Y);
    shot("4b_split");
    screens.press(ui::Btn::B);
    screens.press(ui::Btn::X);
    screens.press(ui::Btn::Down);
    screens.press(ui::Btn::Down);
    screens.press(ui::Btn::Down);
    screens.press(ui::Btn::Down);
    screens.press(ui::Btn::Down);
    shot("5_add");
    screens.press(ui::Btn::B);
    screens.press(ui::Btn::Select);
    shot("6_tuner");
    screens.press(ui::Btn::B);
    screens.press(ui::Btn::Start);
    shot("7_menu");
    screens.press(ui::Btn::B);
    screens.press(ui::Btn::X);  // add screen, list focused, delay category
    for (int k = 0; k < 6; ++k) screens.press(ui::Btn::Down);
    screens.press(ui::Btn::Right);
    screens.press(ui::Btn::Down);
    screens.press(ui::Btn::Down);
    shot("5b_add_list");
    screens.press(ui::Btn::B);
    screens.press(ui::Btn::B);
    const json ui0 = eng.settings();
    eng.handle({{"cmd", "set_ui"}, {"view", "pedals"}, {"theme", "dark"}});
    shot("8_pedals");
    for (int k = 0; k < 4; ++k) screens.press(ui::Btn::Right);
    shot("8b_pedals_lane");
    for (const char* th : {"light", "contrast", "retro"}) {
      eng.handle({{"cmd", "set_ui"}, {"theme", th}, {"view", "tiles"}});
      shot(std::string("9_theme_") + th);
    }
    eng.handle({{"cmd", "set_ui"}, {"theme", "light"}, {"view", "pedals"}});
    shot("9_theme_light_pedals");
    eng.handle({{"cmd", "set_ui"}, {"theme", ui0.value("theme", "dark")}, {"view", ui0.value("view", "tiles")}});
    return 0;
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // Sound card: --capture / --playback / --in-channel win; otherwise the choice from System -> Audio
  // (data/settings.json "audio"): auto = a USB interface if one is plugged in, else the internal codec.
  auto audioChoice = [&]() {
    const json a = eng.settings().value("audio", json::object());
    fx::AudioChoice ch;
    ch.device = a.value("device", "auto");
    ch.channel = a.value("channel", "auto");
    ch.output = a.value("output", "same");
    return ch;
  };
  auto audioOptions = [&](const fx::AudioResolved& r) {
    fx::AudioOptions o = ao;
    if (!captureGiven) o.capture = r.capture;
    if (!playbackGiven) o.playback = r.playback;
    if (!inChannelGiven) o.inChannel = r.channel;
    return o;
  };
  std::mutex audioMtx;  // guards audioActive, audioError (state() reads them from other threads)
  fx::AudioResolved audioActive = fx::resolveAudio(fx::scanAudioCards(), audioChoice());
  std::string audioError;
  std::atomic<bool> audioApply{false};  // audio_set -> the main loop restarts the driver outside the engine lock

  fx::AudioDriver audio(eng, audioOptions(audioActive));
  if (!audio.start(nullAudio, err)) {
    std::fprintf(stderr, "[audio] %s - falling back to the null driver\n", err.c_str());
    audioError = err;
    audio.start(true, err);
  }
  std::printf("[audio] %s\n", audio.description().c_str());
  std::string audioDesc = audio.description();  // copy: the driver's own string changes on restart
  eng.setAudioState([&]() -> json {
    json cards = json::array();
    for (const auto& c : fx::scanAudioCards())
      cards.push_back({{"id", c.id}, {"name", c.name}, {"index", c.index}, {"usb", c.usb}, {"usbid", c.usbid},
                       {"capture", c.capture}, {"playback", c.playback}});
    const json a = eng.settings().value("audio", json::object());
    std::lock_guard<std::mutex> lk(audioMtx);
    json act = {{"device", audioActive.device}, {"label", audioActive.label}, {"usb", audioActive.usb}, {"channel", audioActive.channel},
                {"capture", captureGiven ? ao.capture : audioActive.capture}, {"playback", playbackGiven ? ao.playback : audioActive.playback},
                {"description", audioDesc}};
    if (!audioActive.missing.empty()) act["missing"] = audioActive.missing;
    act["bt_route"] = audioActive.btRoute;
    json st = {{"cards", cards}, {"active", act},
               {"choice", {{"device", a.value("device", "auto")}, {"channel", a.value("channel", "auto")}, {"output", a.value("output", "same")}}},
               {"manual", captureGiven || playbackGiven || inChannelGiven}};
    if (!audioError.empty()) st["error"] = audioError;
    return st;
  });

  // WiFi hotspot fallback (NetworkManager); the network commands are routed through the engine
  fx::Hotspot hotspot([&] { return fx::HotspotConfig::fromJson(eng.settings().value("hotspot", json::object())); },
                      [&] { eng.touch(); });
  eng.setExtension(
      [&](const json& c) -> json {
        const std::string name = c.value("cmd", "");
        if (name == "audio_apply") { audioApply = true; return {{"ok", true}}; }  // done by the main loop
        if (name == "audio_scan") { eng.touch(); return {{"ok", true}}; }        // state() rescans the cards
        if (name == "net_hotspot") {
          if (noHotspot) return {{"ok", false}, {"code", "err.net_unavailable"}, {"error", "Network control not available"}};
          hotspot.request(c.value("on", true));
          return {{"ok", true}};
        }
        return {{"ok", false}, {"code", "err.unknown_command"}, {"error", "Unknown command"}};
      },
      [&]() -> json {
        json st = noHotspot ? json{{"available", false}, {"error", "disabled (--no-hotspot)"}} : hotspot.status();
        st["web_port"] = port;
        return st;
      });
  if (!noHotspot) hotspot.start();
  auto netInfo = [&]() -> std::string {
    const std::string ap = noHotspot ? "" : hotspot.shortInfo();
    if (!ap.empty()) return ap;
    const std::string cur = localIp();
    return cur.empty() ? fx::T("ui.no_network", "no network") : "web " + cur + (port == 80 ? "" : ":" + std::to_string(port));
  };

  fx::HttpServer server(port, webDir);
  {  // optional: "web_password" in data/settings.json protects the web UI and uploads (user "namfx")
    const json ws = eng.settings();
    if (ws.contains("web_password") && ws["web_password"].is_string()) server.setPassword(ws["web_password"].get<std::string>());
  }
  server.onWs([&](const std::string& text) -> std::string {
    json c = json::parse(text, nullptr, false);
    if (c.is_discarded()) return R"({"ok":false,"code":"err.json","error":"invalid JSON"})";
    if (c.value("cmd", "") == "hello") {
      json h = eng.state();
      h["t"] = "hello";
      h["catalog"] = fx::catalogJson();
      h["catalog_version"] = fx::catalogVersion();
      h["net"] = ip;
      return h.dump();
    }
    json r = eng.handle(c);
    if (c.contains("rid")) r["rid"] = c["rid"];
    if (!r.contains("t")) r["t"] = "reply";
    return r.dump();
  });
  server.onApi([&](const fx::HttpRequest& r, fx::HttpResponse& resp) -> bool {
    if (r.path == "/api/state") { resp.body = eng.state().dump(); return true; }
    if (r.path == "/api/upload" || r.path == "/api/delete") {
      const std::string kind = r.param("kind"), name = fs::path(r.param("name")).filename().string();
      if ((kind != "nam" && kind != "ir" && kind != "wav" && kind != "presets" && kind != "fx") || name.empty() || name[0] == '.') {
        resp.status = 400;
        resp.body = R"({"ok":false,"code":"err.request","error":"invalid request"})";
        return true;
      }
      const std::string path = eng.dataPath(kind) + "/" + name;
      bool ok;
      if (r.path == "/api/upload") {
        std::ofstream f(path, std::ios::binary);
        f.write(r.body.data(), (std::streamsize)r.body.size());
        ok = (bool)f;
      } else {
        std::error_code ec;
        ok = fs::remove(path, ec);
      }
      eng.handle({{"cmd", kind == "fx" ? "fx_rescan" : "rescan"}});
      resp.body = json{{"ok", ok}}.dump();
      return true;
    }
    return false;
  });
  bool webOk = server.start(err);
  if (!webOk && port == 80) {  // no permission for port 80 -> 8080
    std::fprintf(stderr, "[web] %s, trying 8080\n", err.c_str());
    port = 8080;
    server.setPort(port);
    webOk = server.start(err);
  }
  if (!webOk) std::fprintf(stderr, "[web] %s\n", err.c_str());
  else std::printf("[web] http://%s%s\n", ip.empty() ? "localhost" : ip.c_str(), port == 80 ? "" : (":" + std::to_string(port)).c_str());
  screens.setNetInfo(ip.empty() ? fx::T("ui.no_network", "no network") : "web " + ip + (port == 80 ? "" : ":" + std::to_string(port)));

  ui::Display display;
  bool haveDisplay = false;
  if (!noDisplay) {
    haveDisplay = display.open(640, 480, err);
    if (!haveDisplay) std::fprintf(stderr, "[ui] display unavailable: %s\n", err.c_str());
  }
  ui::GamepadInput pad;
  if (pad.open()) std::printf("[ui] gamepad %s\n", pad.device().c_str());
  else std::printf("[ui] gamepad not found\n");

  ui::Canvas canvas(640, 480);
  unsigned sentVersion = 0;
  auto lastStatus = std::chrono::steady_clock::now();
  auto lastState = lastStatus;
  auto lastNet = lastStatus;
  int tick = 0;
  auto lastBt = std::chrono::steady_clock::now();
  auto onBtn = [&](ui::Btn b, bool rep) {
    screens.setButtonsHeld(pad.selectHeld(), pad.startHeld());
    screens.press(b, rep);
  };
  while (!g_quit && !screens.quit()) {
    const auto now = std::chrono::steady_clock::now();
    if (++tick % 2 == 0) eng.controlTick();
    if (now - lastNet > std::chrono::seconds(2)) { lastNet = now; screens.setNetInfo(netInfo()); }
    if (now - lastBt > std::chrono::seconds(2)) {  // Bluetooth headphones (dis)connected: move the output
      lastBt = now;
      bool used;
      { std::lock_guard<std::mutex> lk(audioMtx); used = audioActive.btRoute; }
      if (audioChoice().output == "bluetooth" && fx::bluetoothRouteActive() != used) audioApply = true;
    }
    if (audio.lost()) {  // USB interface unplugged while running: re-resolve (the missing card falls back to the internal one)
      std::fprintf(stderr, "[audio] sound card lost - re-resolving\n");
      audioApply = true;
    }
    if (audioApply.exchange(false)) {  // new sound card / channel chosen
      fx::AudioResolved r = fx::resolveAudio(fx::scanAudioCards(), audioChoice());
      std::string e, failure;
      if (!audio.restart(audioOptions(r), nullAudio, e)) {  // back to the internal codec, then to the null driver
        failure = e;
        std::fprintf(stderr, "[audio] %s - trying the internal input\n", e.c_str());
        r = fx::resolveAudio(fx::scanAudioCards(), {"internal", "auto"});
        if (!audio.restart(audioOptions(r), nullAudio, e)) {
          std::fprintf(stderr, "[audio] %s - falling back to the null driver\n", e.c_str());
          audio.restart(audioOptions(r), true, e);
        }
      }
      std::printf("[audio] %s\n", audio.description().c_str());
      {
        std::lock_guard<std::mutex> lk(audioMtx);
        audioActive = r;
        audioError = failure;
        audioDesc = audio.description();
      }
      eng.touch();
    }
    pad.poll(onBtn);
    if (haveDisplay && !display.pollKeys(onBtn)) break;
    if (screens.update() && haveDisplay) {
      screens.render(canvas);
      display.present(canvas);
    }
    if (server.clients() > 0) {
      if (eng.version() != sentVersion && now - lastState > std::chrono::milliseconds(80)) {
        sentVersion = eng.version();
        lastState = now;
        server.broadcast(eng.state().dump());
      }
      if (now - lastStatus > std::chrono::milliseconds(100)) {
        lastStatus = now;
        server.broadcast(eng.status().dump());
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  std::printf("[namfx] exit\n");
  server.stop();
  audio.stop();
  display.close();
  hotspot.stop();  // our access point down -> NetworkManager returns to the known WiFi
  return 0;
}
