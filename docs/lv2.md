# LV2 plugins

namfx can host LV2 plugins. It **does not ship any**: you install them yourself, and their own
licenses apply. Plugins appear in the add-block list under the source filter **LV2**, sorted into
categories by their LV2 class.

## Installing plugins on the R36S (dArkOS)

```sh
sudo apt install guitarix-lv2        # many guitar effects
sudo apt install x42-plugins         # EQ, delays, utilities
sudo apt install calf-plugins zam-plugins dragonfly-reverb-lv2
```

Then restart namfx. Debian packages are built for ARM64 and work as they are. Bundles you copy
by hand (`*.lv2` folders) go into `data/lv2/` and **must be ARM64 builds**.

Search path: `data/lv2`, then `$LV2_PATH` or the defaults `~/.lv2`, `/usr/local/lib/lv2`,
`/usr/lib/lv2` and `/usr/lib/aarch64-linux-gnu/lv2`. *System → Load log* in the web UI shows
the path and every plugin that was skipped, with the reason.

## What is supported

- 1–2 audio inputs and 1–2 audio outputs (mono and stereo). A plugin with stereo outputs makes
  the chain stereo after it.
- Control ports become parameters:
  - toggles become on/off switches
  - enumerations become option lists
  - integer ports snap to whole numbers
  - logarithmic ports get a log knob
  - units `Hz`, `ms`, `dB`, `%`, `s` are shown
- `lv2:enabled` and `lv2:freeWheeling` ports are handled by namfx and hidden. namfx has its own
  bypass with a 10 ms crossfade.
- Features provided: `urid:map`, `urid:unmap`, `options:options` (block length, sample rate),
  `buf-size:boundedBlockLength`.
- Atom ports get empty event buffers (no MIDI). The reported latency is used to align
  split paths.

Skipped plugins:
- plugins that require other features (e.g. `worker:schedule`, used by convolution and sample
  players)
- plugins with CV ports
- instruments
- plugins with more than 2 audio channels

Plugin presets, state saving and plugin GUIs are not supported. namfx shows a generic list of
parameters.

## Tested on the R36S

`guitarix-lv2` 0.46 and `x42-plugins` (Debian trixie, arm64) give 120 usable plugins; the rest is skipped by the rules above. Measured with `--bench 1` on one Cortex-A35 core (128-sample blocks): 72 plugins under 5 %, 36 at 5–15 %, 12 above 15 %. Too heavy for a live chain: the two Gx JCM800 preamps (~100 %), Tuna (~41 %), the x42 spectrum and meter displays (~17–25 %). **Gx Tape Stereo** crashes in the host, so it is skipped and listed in the load log as "crashes in the host".

## Performance

LV2 plugins run in the audio thread like built-in blocks. Their load varies a lot, and some are
too heavy for the Cortex-A35. Each block shows its measured load (web: in the block editor;
console: in the editor header). The value is also remembered per type in `data/cpu_stats.json`
and shown in the add list.

## Presets

Presets store LV2 blocks as `lv2:<plugin URI>` with parameters by port symbol. If a plugin is
missing on another system, the block becomes a placeholder. It passes the signal through and
keeps its settings, so saving the preset again loses nothing.
