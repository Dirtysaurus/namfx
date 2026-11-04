# AGENTS.md – namfx (handoff / context dump, 2026-10-29)

> For Codex or another coding agent. Put this file in the repo root (`~/namfx/AGENTS.md`).
> The owner writes in **Czech**: answer in Czech, factually and briefly. Do nothing without an
> instruction, offer no further steps, and make no design proposals until asked.

## 1. What namfx is

A guitar multi-effect for the **R36S** handheld (Rockchip RK3326, 4× Cortex-A35, 1 GB RAM, 640×480)
running **dArkOS** (Debian trixie, GCC 14, glibc).
- Amp models are **NAM** (Neural Amp Modeler, A1/A2 `.nam` files) and cabinets are IRs, run through a convolver.
- Built-in DSP effects plus data-defined effect types (JSON in `fx/`) and an optional **LV2** host (lilv).
- The on-screen UI uses SDL2 (KMSDRM) and is driven by the gamepad. A web UI (HTTP + WebSocket) is served from the console for phones.
- Started from EmulationStation → Ports (`ports/NAMFX.sh`). It runs only when the user launches it; it is not a system service.
- **Version:** v0.4.0 (tag pushed 2026-11-04; the repo `Dirtysaurus/namfx` is now **public** and the `release` workflow publishes the aarch64 tarball). The main console runs v0.4.0. Sound testing with the GO Twin is next.

### License (decided)
- The project's own code is **GPL-3.0-only** with a **commercial license** available from the author (dual licensing).
  - Files: `LICENSE`, `COMMERCIAL.md`, `CLA.md` (template for contributors), `AUTHORS`, `THIRD_PARTY.md`.
  - `AUTHORS` = "Peter Hudak <peter.hudak88@gmail.com>". The name was guessed from the e-mail; the owner should confirm it.
- **Only the owner's own code is distributed.** NAM, LV2 plugins and Airwindows (via LV2) are *supported but not shipped*.
- Every source file starts with:
  ```
  // SPDX-License-Identifier: GPL-3.0-only
  // Copyright (C) 2026 Peter Hudak
  ```
  The exceptions are the third-party headers `stb_*.h`.

## 2. Hardware and system facts (console)

- **Access:** user `ark`, SSH by key (the owner's key `Hudy`, passphrase held by the Windows `ssh-agent`; the agent must not be asked for passwords). `sudo` works without a password. The app lives in `/home/ark/namfx` (ext4, so files can be executable).
  - **Main console** (RTL8723BU, AP-capable): `192.168.0.80` at the last check (DHCP, may change).
  - **Test console** (MT7601U, no AP): `192.168.0.72` at the last check, and a serial shell on the PC (`COM3`, 115200 baud, CP210x; the number changes between PCs).
  - A PC-side HTTP server is not needed any more: install on a console with `curl -L <release url> | tar -xzf - -C ~` (the tarball has no `data/`).
- **Memory:** the root filesystem is **btrfs**, so a swap file is impossible ("swapfile has holes").
  - For builds: enlarge zram to 1G, stop EmulationStation, use `JOBS=1`.
  - The C++ compiler (`cc1plus`) gets OOM-killed otherwise.
- **Audio:** ALSA `hw:0,0`, 48 kHz, period 128 (2.7 ms). The RK817 mic path is set with `amixer sset 'Capture MIC Path' 'Main Mic'`.
  - **Jack input is right channel only.** Measured on the second (test) console with headphones as a microphone: the left channel is digital silence, the right one goes from about −60 to −6 dBFS when tapped. namfx therefore picks the channel itself (right for the internal jack, left for USB, see System → Audio input); `--in-channel` forces one. Not yet checked on the main console. The jack is a mic input (bias, no instrument buffer), not a guitar input; the GO Twin is the intended input.
  - The kernel log prints `ASoC: Failed to add route Mic Jack -> direct -> MIC_IN` (and the HPOL/HPOR routes) at boot. This is harmless: the BSP codec has no such DAPM widgets and the path is selected with the `Capture MIC Path` control.
- **Measured CPU** (one A35 core):
  - Plexi preset: 35.8 % (2.8× realtime).
  - Dual Amp preset: 68.4 % (1.5× realtime).
  - The A35 is about 13× slower than the dev PC for NAM; engine costs are estimated as PC load × 20.
- **WiFi:** main console: internal **RTL8723BU** (WiFi + Bluetooth, USB `0bda:b720`) with the vendor driver `8723bu.ko` (`drivers/net/wireless/ew-7611ulb`). Its `iw list` reports `IBSS, managed, AP, P2P-client, P2P-GO` (checked 2026-10-30), so the hotspot can work. NetworkManager (`nmcli`) is present. The internal WiFi shares the OTG USB D+/D− lines (conflict with USB audio).
- **Second (test) console:** has a different WiFi, an **MT7601U** USB dongle (`148f:7601`, `mt7601u.ko`) that offers only `managed` and `monitor`, **no AP**: the hotspot cannot run there. It also has an extra SPI flash chip; that work is a separate project (repo `Dirtysaurus/r36s-rescue`) and not part of namfx.
- **Bluetooth (owner's own scripts, backup in `C:\Hudy\R36S\backup`, not in this repo):** the RTL8723BU also carries Bluetooth (`rtk_btusb mp_drv_mode=1`, `8723bu` loaded with `rtw_btcoex_enable=1`, USB device `1-1`). `bluealsa.service` runs `bluealsad -p a2dp-source -p a2dp-sink`. `bt-vol.service` (`/opt/bt_fix/bt_volume_monitor.sh`) waits for a connected headphone, loads `snd-aloop`, writes `/etc/asound.conf` (`default` -> `plug` -> `hw:Loopback,0,0`, `pcm.blueout` -> bluealsa with the MAC) and starts `alsaloop -C hw:Loopback,1,0 -P blueout -t 20000 --resample`; it removes the file when the headphones go. `/opt/system/Absolute.sh` (R36S Manager v71) is the menu: WiFi+BT / WiFi only / BT only / all off, pairing, WiFi manager. BT-only mode stops NetworkManager, so the namfx hotspot (nmcli) needs the *WiFi* or *Both* mode. `headphones/fake_headphone.py` injects the headphone-inserted switch event (mutes the speaker).
- **USB audio:** the owner has a **TC Helicon GO Twin**.
  - UAC1 class-compliant, Hi-Z 470 kΩ, 44.1/48 kHz, 24-bit.
  - Use `plughw:CARD=…`, because S24_3LE is not supported natively.
  - Its connector is a special 10-pin mini USB; use the stock Mini-USB→USB-A cable plus a USB-C OTG adapter.
- **Update on a console:** `tools/update.sh` (latest release; `--check`, or a tag). It refuses while namfx runs, keeps `build/namfx.old` and writes `.release`. Verified on the test console.
- **LV2 on the R36S (verified 2026-11-04, test console):** `apt install guitarix-lv2 x42-plugins` gives 120 loadable plugins (67 from guitarix); loads from `--bench 1`: 72 under 5 %, 36 at 5–15 %, 12 above 15 % (`gx_jcm800pre` mono/stereo ~100 %, Tuna ~41 %, Gx Studio Preamp Stereo 27 %). All 47 built-in and bundled types are at most 4.8 %.
- **Disk:** the test console's root (btrfs) is 89 % full. The `~/.wine_*` prefixes there belong to the owner's separate game project (Gooka, LBA2 and others; they hold the games and saves). Never delete them; the owner decides.
- **Prebuilt ARM binary:** each `v*` tag builds `namfx-<tag>-aarch64.tar.gz` (GitHub release, ~1.5 min). Installing it over `~/namfx` was verified on the main console (`namfx.old` kept as a backup). Compiling on the console with `tools/build.sh` still works, but a full rebuild takes 20+ minutes.
- **Pasting over SSH:** paste multi-line commands one line at a time; bracketed paste broke commands before (`^[[200~`).

## 3. Build, run, test

```sh
# console (or Debian/Ubuntu PC)
./tools/build.sh              # apt deps (g++, cmake, libasound2-dev, libsdl2-dev, liblilv-dev, lv2-dev) + fetch_deps + cmake
JOBS=1 ./tools/build.sh       # low RAM
# tools/fetch_deps.sh: NeuralAmpModelerCore -> third_party/; if the Eigen submodule (GitLab) fails, it symlinks libeigen3-dev

# CMake options
-DNAMFX_TARGET_A35=ON         # set automatically on aarch64
-DNAMFX_WITH_LV2=OFF          # build without lilv (stubs)
# ALSA / SDL2 missing -> offline/WAV only / no on-screen UI (web + snapshots still work)
```

Useful flags (`./build/namfx --help`):

```
--data DIR --web DIR --fonts DIR --fx DIR   --no-lv2  --no-hotspot  --lang en|cs  --port N (80 -> 8080)
--rate 48000 --block 128  --capture hw:0,0 --playback hw:0,0  --in-channel 0|1|2  --cpu 3
--null-audio  --no-display  --preset FILE  --wav FILE
--offline IN.wav OUT.wav [--preset X.json]   # render + prints x-realtime and core load
--snapshots DIR      # PNG of every on-screen page (themes, pedals) and exit
--bench [SEC]        # load of every block type (each in its own process, a crash prints CRASH); table on stdout, JSON on stderr
--print-state / --print-catalog / --print-engines / --print-log
```

PC test recipes used so far:
- **UI without hardware:** `./build/namfx --null-audio --no-display --port 8097`, then drive the WebSocket `/ws` with a small Python client (send `{"cmd":…,"rid":N}` and wait for the reply with the same `rid`). The browser UI is at `http://localhost:8097/` (screenshots with Playwright).
- **Offline render check of every effect type:** levels finite, no clipping at default settings.
- **Fuzzing:** the WebSocket API under ASan and TSan. ASan shows only a lilv internal leak; TSan shows 0 warnings.
- **Hotspot:** put a fake `nmcli` bash script on `PATH` that keeps its state in a file (`state:connection`). Cover auto after 20 s, manual stop/start, mode off, always, and renaming while running.
- **Sound card choice:** `python3 tests/audio_test.py` (needs `pip install websocket-client` and a built `build/namfx`; runs namfx with `--null-audio` against a fake `/proc/asound` via `NAMFX_ASOUND_ROOT`). CI (`.github/workflows/ci.yml`) builds on Ubuntu and runs it together with the i18n check.
- **i18n:** `python3 tools/i18n.py --check` regenerates `web/lang/en.json` from the sources and lists the keys missing in `cs.json` (currently 602/602).

## 4. Architecture (src/)

| Path | Role |
|---|---|
| `main.cpp` | Options, audio and UI loop, HTTP/WS server, snapshots, `--bench`/`--offline`. Wires up the Hotspot (created **before** `HttpServer.start`, via `eng.setExtension`) and refreshes the footer net info every 2 s. |
| `engine.h/.cpp` | Owns state, presets, scenes and settings (`data/settings.json`: lang, theme, view, hotspot) and handles every WS command. Bumps `version()` on change; `state()` and `status()` return JSON. Errors are `{"ok":false,"code":"err.…","error":"English text"}`. |
| `dsp/blocks.h/.cpp` | `ParamDef` and `BlockDef` (owned strings; `source` = builtin \| fx \| lv2 \| missing). Built-in blocks: gate, nam, ir, eq, tremolo, wah, octaver, volume, split. `MissingBlock` passes audio through and keeps its raw JSON. Factory: `makeBlock(type)` / `makeBlock(def)`. |
| `dsp/catalog.cpp` | Registry: a deque that is never freed, guarded by a mutex. Loads `fx/` (bundled) and `data/fx/` (user; id collisions with bundled types are refused), plus the LV2 scan. Provides `catalogVersion()`, the catalog JSON and the load log. |
| `dsp/engines.h/.cpp` | Six generic engines: delay, mod, phaser, reverb, drive, comp. `parseTypeDef` reads JSON types (`set`, `params`, `map` → `to`, `range`, `curve` log/power, `values`, `op` set/mul/add; order is set → mul → add, then clamp). `EngineBlock` recomputes internal values when a knob changes. |
| `dsp/lv2_host.*` | lilv host. Features: urid map/unmap, options, boundedBlockLength. Control ports become params (toggled, enumeration, integer, log, units). `lv2:enabled` and freeWheeling ports are hidden; atom ports get empty sequences. Plugins needing worker/CV features, and instruments, are skipped. The category comes from all `rdf:type` values. |
| `dsp/graph.*` | Chain with an A/B split. Becomes stereo after the first block whose `def->stereo` is set. Per-block load is an EMA, reported as `loads`. |
| `dsp/dsp_util.h` | `enableFlushToZero()` (aarch64 FPCR FZ, x86 FTZ/DAZ; call it in every audio thread – denormals cost a lot), Biquad, OnePole, Allpass, DelayLine, `fastSin2pi`/`fastTri`/`fastTanh`, Rng, lock-free `Swap<T>`. |
| `dsp/nam_block.cpp`, `convolver`, `fft`, `tuner` | NAM wrapper, IR convolution, tuner. |
| `io/audio_alsa.*`, `io/wav.*` | ALSA I/O (SCHED_FIFO 80 on `--cpu`), WAV read/write (16/24/32-bit). `AudioDriver::restart()` switches cards. |
| `io/audio_devices.*` | Finds sound cards in `/proc/asound` (or `$NAMFX_ASOUND_ROOT`, used by the tests) and resolves the choice. `auto` = first USB duplex card (`plughw:CARD=<id>,DEV=0`), else the internal one (`hw:N,0`); input and output always from one card; channel auto = left for USB, right for the internal jack. ALSA loopback is never picked. |
| `net/http_server.*` | Static files, `/ws`, `GET /api/state`, `POST /api/upload?kind=nam\|ir\|wav\|presets\|fx&name=…`, `POST /api/delete`. Uploading kind `fx` triggers `fx_rescan`. |
| `net/hotspot.*` | WiFi AP fallback via `nmcli`, started with `posix_spawnp` (no shell). Details in §6. |
| `ui/screens.*` | On-screen UI: rig (tiles/pedals), edit, add (source filter on Y), tuner, menu (pages: preset, block, input and sound, settings; `menuEntries()` gives label + action id). Theme palette via `applyTheme`. `status_` is initialised in the constructor – a null value crashed it before (`type_error.306`). |
| `ui/canvas.*`, `display.cpp`, `input_evdev.cpp` | Software canvas using stb_truetype, SDL2/KMSDRM output, evdev gamepad. |
| `i18n.*` | `T("key","English fallback")`, shared with the web UI through `web/lang/*.json`. |

Web UI (`web/`):
- `index.html`, `app.js` (vanilla JS), `style.css`
- Themes via `[data-theme]`: dark, light, contrast, retro
- Tabs: Rig, Files, Tuner, System
- System cards: Appearance, Effects, Network, …
- Translation: `data-i18n` attributes and `tr()`

Effect types (`fx/*.json`, 41 types, documented in `docs/fx-types.md`):
- **dynamics:** comp, comp_opto, comp_studio, comp_squash, limiter
- **drive:** drive, drive_green, drive_transparent, drive_dist, drive_fuzz_vintage, drive_fuzz_big, drive_boost, drive_treble
- **modulation:** chorus, chorus_analog, chorus_ensemble, chorus_dimension, vibrato, flanger, flanger_jet, phaser, phaser_script, phaser_deep, univibe
- **delay:** delay, delay_digital, delay_analog, delay_tape, delay_space, delay_ducking, delay_lofi, delay_mod, delay_slap, delay_pingpong, delay_ambient
- **reverb:** reverb, reverb_room, reverb_hall, reverb_plate, reverb_dark, reverb_ambient
- The plain ids (`delay`, `chorus`, …) keep their old parameter ids for preset compatibility.
- Per-type `cpu` values are calibrated.
- Users add their own packs to `data/fx/`. LV2 is documented in `docs/lv2.md`.

## 5. WebSocket API (summary)

- **State and catalog:** `state`, `catalog` (has `catalog_version`), `fx_rescan`, `catalog_log`, `engines`.
- **Blocks:** `set_param {id,param,value}`, `set_on`, `set_file`, `add_block {type, after | lane:{split,lane}}`, `add_split`, `remove`, `move`, `move_to_lane`.
- **Presets and scenes:** `preset_load/save/new/rename`, `scene_store/select/remove`.
- **Settings and input:**
  - `set_global {key,value}`, `tuner {on}`, `input {source:live|wav,file,loop,play}`, `transport`
  - `set_lang {lang}`, `set_ui {theme,view}`
- **Audio input:**
  - `audio_set {device: auto|internal|card:<id>, channel: auto|left|right|sum}`: validated and saved in `data/settings.json` (`audio`), applied by the **main loop** (not under the engine lock) through `AudioDriver::restart()`; a failed start falls back to the internal card, then to the null driver
  - `audio_scan`: rescans the cards
  - `audio_set` also takes `output: same|bluetooth|pcm:<ALSA name>`. `bluetooth` plays to ALSA `default` while `/etc/asound.conf` mentions Loopback (the console's bt-vol setup, see §2 *Bluetooth*), otherwise to the card; the main loop re-checks every 2 s
  - Also: `setlist_set {files}`, `preset_index`/`preset_step`, `expression {value}`, `transport {speed, loop_a, loop_b, seek, clear_loop}`, `looper {action}`. `io/midi_in.*` reads USB MIDI (PC = Nth setlist preset, CC11 expression, CC64/65 next/prev); the gamepad's stick axis `settings.expression_axis` (default 4) sends `expression` (unverified on the console); `net/mdns.*` answers `<hostname>.local` (default `namfx`); QR codes: `src/ui/qr/` (console, Menu -> Settings -> Phone QR codes) and `web/qrcode.min.js`; `sk.json` is generated by `tools/make_sk.py`. Not done on purpose: preset-change crossfade (needs two graphs), Bluetooth HID pedals, Tone3000 download (no API access), PWA (needs HTTPS).
  - Other new commands: `mute {on?}`, `undo`/`redo`, `metronome {on,bpm,volume}`, `gain_wizard {on}`. Optional `web_password` in `data/settings.json` turns on HTTP Basic auth (user `namfx`). The hotspot is open until the owner sets a password. `.github/workflows/release.yml` builds an aarch64 tarball on `v*` tags (needs a public repo for the free arm runner)
  - State: `state.audio_dev` = `{cards[], active{device,label,usb,channel,capture,playback,description,missing?}, choice{device,channel}, manual, error?}` (`state.audio` is the older `{rate,block}` object)
  - Errors: `err.audio_device`, `err.audio_channel`
  - `audio_set {hw_out: system|speaker|headphones|both}` (default `system` = leave the mixer alone): sets the internal codec's `Playback Path` (RK817 enum `OFF`, `SPK`, `HP`, `SPK_HP`) at start and on every audio change; ignored for a USB card. Error `err.audio_hwout`. Console menu: Input and sound → Console output; web: System → Audio input. Checked on the test console with `amixer`; the speaker muting itself is not heard yet (that console has no speaker).
  - `--capture` / `--playback` / `--in-channel` win over the choice (`manual: true`)
- **Network:**
  - `net_set {mode:auto|always|off, ssid (1–32 printable), password (8–63 ASCII)}`
  - `net_hotspot {on}`
  - State fields: `state.network`, `state.hotspot_cfg`
  - Errors: `err.net_mode`, `err.net_ssid`, `err.net_password`, `err.net_unavailable`
- **Server push:** `state` on every change; `status` 10× per second (CPU, levels, `loads`, tuner).

## 6. Hotspot (new in v0.3.1)

- **Settings:** `data/settings.json` → `"hotspot": {"mode":"auto","ssid":"namfx","password":""}`. An empty password = **open AP** (decided by the owner: the first connection must work without knowing anything; the web warns and the password is set afterwards). An open AP is built with `nmcli connection add … 802-11-wireless.mode ap ipv4.method shared` + `connection up`, because `nmcli device wifi hotspot` always adds WPA.
- **Modes:**
  - **auto:** no WiFi connection for 20 s → start the AP.
  - **always:** keep the AP up while namfx runs.
  - **off:** never start it.
- **Polling:** every 2 s it parses `nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device` and `nmcli -g IP4.ADDRESS device show <dev>`.
- **Starting:** `nmcli connection delete namfx-hotspot`, then `nmcli device wifi hotspot ifname <dev> con-name namfx-hotspot ssid … password …`. The IP is usually `10.42.0.1`.
- **Stopping:** connection down + delete, then `device connect` for a manual stop. On exit it does only down + delete, and NetworkManager reconnects to known networks.
- **Manual stop** blocks auto mode until the console is connected to a normal WiFi again.
- **Rename while running:** a new ssid or password restarts the AP.
- **Leftover AP:** an AP left from a previous run is adopted, with the name and password taken from the config.
- **Failed start:** retries after about 80 s (`noLinkSecs_ = -60`).
- **UI:** console menu "Own WiFi: Auto/Always/Off" (cycles); web System → Network card. The footer shows `AP ssid / pw · ip`.
- **Verified** on the main console's RTL8723BU (2026-11-04, namfx v0.4.0, mode *always*, open AP): `namfx-hotspot` came up on `10.42.0.1`, a phone loaded the web UI on port 80, and after exiting namfx the console reconnected to its WiFi (`PS5`, `192.168.0.80`) within 45 s. The state machine was also checked against a fake `nmcli` on PC. A chip without AP just makes `nmcli … hotspot` fail: the error appears under System → Network and it is retried after about 80 s. No capability probe exists on purpose.

## 7. Conventions

- C++17, 2-space indent, `namespace fx`.
- No allocation or locks in the audio thread. Hand objects over with `Swap<T>`.
- Every user-visible string goes through i18n:
  - C++ uses `T("key","English")`; web uses `data-i18n` / `tr()`.
  - Add the Czech translation to `web/lang/cs.json`.
  - Run `tools/i18n.py --check`. The key count must be N/N.
- New error replies take a `code` (`err.*`) and an English `error`.
- Run external programs with `posix_spawnp` and an argv vector, never through a shell.
- Gitignored: `build/`, `third_party/`, `data/cpu_stats.json`, `data/settings.json`.
- Keep `README.md` (EN) and `README.cs.md` (CS) in sync: Options, Buttons, API and Status sections.

## 8. History of notable fixes (don't reintroduce)

- **Drive (TS) too quiet:** a 2nd-order pre-highpass caused phase cancellation. It is now a 1st-order OnePole HP, with levels retuned.
- **Hall/ambient reverb clipping:** wet gain is normalised by `sqrt((1-room)/0.16)`.
- **flanger_jet clipping:** its defaults were changed.
- **Denormals:** without FTZ, Dual Amp offline load on PC went from 3.5 % to 9.2 %.
- **lilv assert:** a TTL without the `rdfs` prefix triggered it. There is now a NULL guard for scale points.
- **First-frame UI crash:** `status_` was null on the first frame; it is now set in the constructor.
- **Hotspot race:** the extension must be installed before the server starts.

## 9. Artifacts and deliverables

- **UI mockups** (published page): "R36S NAM efekt – UI mockupy",
  https://claude.ai/artifact/UXMqwLyTiH1g4F6Jrj343h (owner's claude.ai account, private).
- **Files delivered in the previous chat** (the owner has them; they are not hosted anywhere):
  - `namfx_v0.3.zip`: full source tree, including `data/` (sample presets, test NAM, IR, WAV).
  - `namfx_v0.3.1.zip`: update. It has no `data/`, so the user's presets and settings stay. Unpack with `unzip -o` over `~/namfx`.
  - `namfx_dock_lite_v0.1.zip`: KiCad project for the dock/input buffer (schematic rev 0.1, `gen_sch.py`, `bom.csv`, `netlist.txt`, README).
- **Screenshots in the repo:** `docs/screen/*.png` (on-screen UI, all themes and the pedal view) and `docs/web/*.png` (web UI).

## 10. Open / not done (only on the owner's instruction)

- **Sound test on the console** with the GO Twin: the owner is testing it now.
- **Gx Tape Stereo** (`gxtape_st#tape`, guitarix 0.46.0) segfaults in the host under `--bench` on the R36S; the cause is not known (no gdb on the console), so it is on a skip list in `lv2_host.cpp`. Gx Tape (mono) works.
- **UI rework:**
  - The owner finds the control and menu "clunky".
  - The pedalboard view has one pedal look, told apart only by colour – to be developed later.
  - The tiles view still exists and is the default.
- **Ideas the owner asked about but did not order:**
  - running on other OSes/handhelds (ROCKNIX, ArkOS, Anbernic etc., PortMaster)
  - downloading presets from Tone3000
  - a prebuilt aarch64 binary (CI)
- **Hardware dock (separate project, `namfx-dock-lite`):** KiCad rev 0.1, input buffer for PCBWay; needs verification.
