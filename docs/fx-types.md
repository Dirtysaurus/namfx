# Effect types (fx/*.json, data/fx/*.json)

Most namfx effects are **types**: JSON descriptions that run on one of the built-in **engines**
(delay, mod, phaser, reverb, drive, comp). A type sets some of the engine's internal parameters
to fixed values and maps its own knobs onto the rest. Adding a new delay, chorus or overdrive
only takes a JSON file. Nothing needs to be compiled.

- Bundled types live in `fx/`. They ship with namfx and are translated in `web/lang/*.json`.
- Your own types go in `data/fx/`. Upload them in the web UI (Files → Effect types), or copy them
  there and press *System → Reload effect types*.
- A bundled type id can't be overridden. A user type with an existing id is rejected and
  reported in *System → Load log*.

Engine documentation as JSON: `./build/namfx --print-engines`.

## File format

A file contains one type, a list of types, or a pack:

```json
{
  "pack": "My delays", "author": "Me", "license": "CC-BY-4.0",
  "types": [
    {
      "type": "my_tape",                      // id: a-z, 0-9, _ (kept in presets)
      "engine": "delay",                      // delay | mod | phaser | reverb | drive | comp
      "name": "My Tape", "short": "MYTP",     // short: up to ~5 characters for tiles
      "cat": "dly",                           // dyn drive amp cab eq mod dly rev util
      "desc": "Dark tape echo",
      "cpu": 5,                               // estimated load of one Cortex-A35 core in %
      "set": {"hp": 80, "wow_rate": 0.5},     // fixed internal values
      "params": [
        {"id": "time", "name": "Time", "min": 50, "max": 1000, "def": 350, "unit": "ms", "log": true,
         "map": {"to": "time"}},
        {"id": "age", "name": "Age", "min": 0, "max": 10, "def": 4,
         "map": [{"to": "lp", "range": [9000, 2500], "curve": "log"},
                 {"to": "wow_depth", "range": [0.1, 1.2]}]},
        {"id": "heads", "name": "Heads", "kind": "enum", "options": ["1", "2"], "def": 0,
         "map": {"to": "heads", "values": [1, 2]}}
      ],
      "tr": {"cs": {"name": "Moje páska", "desc": "Tmavé páskové echo",
                    "params": {"age": {"name": "Stáří"}, "heads": {"options": ["1", "2"]}}}}
    }
  ]
}
```

(Comments are only for explanation. JSON itself has no comments.)

### Knobs (`params`)

| field | meaning |
|---|---|
| `id`, `name` | parameter id (kept in presets) and display name |
| `min`, `max`, `def`, `unit` | range, default and unit (`dB`, `ms`, `Hz`, `%`, `:1`, `pan` or empty) |
| `log` | logarithmic knob (min > 0) |
| `step` | snap to steps (e.g. `1` for integers) |
| `kind` | `float` (default), `enum` with `options`, `bool` |
| `map` | one mapping or a list of mappings onto internal parameters |

### Mappings

| form | result |
|---|---|
| `{"to": "x"}` | the knob value is used as is |
| `{"to": "x", "range": [a, b]}` | knob position 0–1 → a…b (linear) |
| `… "curve": "log"` | a·(b/a)^t (a, b > 0), for frequencies |
| `… "power": 2` | position raised to a power before mapping |
| `{"to": "x", "values": [..]}` | enum/bool: one value per option |
| `"op": "mul"` / `"op": "add"` | multiplies / adds instead of setting |

Evaluation order: engine defaults → `set` → all `set` mappings → all `mul` → all `add`.
Results are then clamped to the engine limits. This lets one knob (e.g. *Type*) shift a value
set by another knob (e.g. *Size*).

## Engines

### `delay` (stereo)

Delay line with up to 4 heads, filters, saturation, wow/flutter, modulation, diffusion, ducking, lo-fi and ping-pong.

| id | default | min | max | meaning |
|---|---|---|---|---|
| `time` | 380 | 1 | 2500 | time of head 1 [ms] |
| `feedback` | 0.35 | 0 | 1.1 | feedback amount (0-1.1) |
| `mix` | 0.3 | 0 | 2 | wet level |
| `dry` | 1 | 0 | 1 | dry level |
| `lp` | 20000 | 200 | 20000 | low-pass in the loop [Hz] (>=19000 off) |
| `hp` | 20 | 20 | 2000 | high-pass in the loop [Hz] (<=25 off) |
| `sat` | 0 | 0 | 1 | saturation in the loop |
| `wow_rate` | 0.6 | 0.05 | 5 | wow rate [Hz] |
| `wow_depth` | 0 | 0 | 5 | wow depth [ms] |
| `flutter_rate` | 6 | 1 | 20 | flutter rate [Hz] |
| `flutter_depth` | 0 | 0 | 1 | flutter depth [ms] (sine + random) |
| `mod_rate` | 0.8 | 0.05 | 10 | modulation rate [Hz] |
| `mod_depth` | 0 | 0 | 10 | modulation depth [ms] |
| `mod_shape` | 0 | 0 | 1 | 0 sine, 1 triangle |
| `heads` | 1 | 1 | 4 | number of playback heads |
| `r2` | 0.5 | 0.05 | 1 | head 2 time as a fraction of head 1 |
| `r3` | 0.75 | 0.05 | 1 | head 3 time fraction |
| `r4` | 0.25 | 0.05 | 1 | head 4 time fraction |
| `l1` | 1 | 0 | 1 | head 1 level |
| `l2` | 0 | 0 | 1 | head 2 level |
| `l3` | 0 | 0 | 1 | head 3 level |
| `l4` | 0 | 0 | 1 | head 4 level |
| `diffuse` | 0 | 0 | 0.8 | all-pass diffusion in the loop |
| `duck` | 0 | 0 | 1 | ducking amount (wet drops while playing) |
| `duck_release` | 300 | 20 | 2000 | ducking release [ms] |
| `bits` | 0 | 0 | 16 | bit depth reduction (0 off) |
| `srate` | 0 | 0 | 48000 | sample-and-hold rate [Hz] (0 off) |
| `noise` | 0 | 0 | 1 | hiss in the loop |
| `pingpong` | 0 | 0 | 1 | ping-pong (stereo) |
| `spread` | 0 | 0 | 50 | right channel time offset [ms] |
| `smooth` | 120 | 1 | 2000 | time change glide [ms] |
| `width` | 1 | 0 | 1 | stereo width of the repeats |

### `mod` (stereo)

Modulated short delay: chorus, flanger, vibrato, ensemble.

| id | default | min | max | meaning |
|---|---|---|---|---|
| `voices` | 1 | 1 | 3 | number of voices |
| `base` | 8 | 0.1 | 40 | base delay [ms] |
| `depth` | 2 | 0 | 15 | modulation depth [ms] |
| `rate` | 0.8 | 0.02 | 15 | LFO rate [Hz] |
| `shape` | 0 | 0 | 1 | 0 sine, 1 triangle |
| `spread` | 0.25 | 0 | 1 | right channel LFO phase offset (0-1) |
| `feedback` | 0 | -0.95 | 0.95 | feedback (flanger) |
| `mix` | 0.5 | 0 | 1 | wet level |
| `dry` | 1 | 0 | 1 | dry level (0 = vibrato) |
| `lp` | 20000 | 500 | 20000 | low-pass on the wet signal [Hz] |
| `hp` | 20 | 20 | 2000 | high-pass on the wet signal [Hz] |
| `sat` | 0 | 0 | 1 | saturation of the wet signal (BBD) |
| `drift` | 0 | 0 | 1 | random detune between voices |

### `phaser` (stereo)

All-pass phaser, 1-12 stages, vibe mode.

| id | default | min | max | meaning |
|---|---|---|---|---|
| `stages` | 4 | 1 | 12 | number of all-pass stages |
| `fmin` | 200 | 20 | 2000 | sweep start [Hz] |
| `fmax` | 2000 | 100 | 10000 | sweep end [Hz] |
| `rate` | 0.5 | 0.02 | 15 | LFO rate [Hz] |
| `shape` | 0 | 0 | 2 | 0 sine, 1 triangle, 2 vibe (lamp + photocell) |
| `feedback` | 0.4 | -0.95 | 0.95 | feedback |
| `mix` | 0.5 | 0 | 1 | wet level |
| `dry` | 1 | 0 | 1 | dry level |
| `spread` | 0 | 0 | 1 | right channel LFO phase offset |
| `stagger` | 0 | 0 | 1 | spreads the stage frequencies (vibe) |

### `reverb` (stereo)

Comb/all-pass reverb (Freeverb topology) with size, pre-delay, width and filters.

| id | default | min | max | meaning |
|---|---|---|---|---|
| `room` | 0.84 | 0 | 0.99 | decay (comb feedback) |
| `damp` | 0.3 | 0 | 1 | high-frequency damping |
| `predelay` | 20 | 0 | 250 | pre-delay [ms] |
| `width` | 1 | 0 | 1 | stereo width |
| `mix` | 0.2 | 0 | 2 | wet level |
| `dry` | 1 | 0 | 1 | dry level |
| `lowcut` | 20 | 20 | 1000 | input low cut [Hz] |
| `highcut` | 20000 | 1000 | 20000 | input high cut [Hz] |
| `size` | 1 | 0.4 | 1.6 | room size (comb length scale) |
| `diffusion` | 0.5 | 0.1 | 0.75 | all-pass diffusion |

### `drive`

Overdrive / distortion / fuzz: pre-filters, mid boost, clipping, clean blend, tone.

| id | default | min | max | meaning |
|---|---|---|---|---|
| `gain` | 20 | 0 | 60 | drive gain [dB] |
| `pre_hp` | 20 | 20 | 2000 | high-pass before clipping [Hz] |
| `pre_lp` | 20000 | 500 | 20000 | low-pass before clipping [Hz] |
| `mid_freq` | 720 | 100 | 5000 | mid boost frequency [Hz] |
| `mid_gain` | 0 | -12 | 18 | mid boost [dB] |
| `clip` | 0 | 0 | 4 | 0 soft, 1 hard, 2 asymmetric diodes, 3 fuzz, 4 tube |
| `bias` | 0 | -0.5 | 0.5 | asymmetry |
| `clean` | 0 | 0 | 1 | clean blend |
| `tone` | 8000 | 300 | 20000 | output low-pass [Hz] |
| `post_hp` | 20 | 20 | 1000 | output high-pass [Hz] |
| `level` | 0 | -30 | 12 | output level [dB] |
| `mid_q` | 0.7 | 0.3 | 4 | mid boost Q |

### `comp`

Compressor: peak/RMS detector, soft knee, opto release, side-chain high-pass, parallel mix.

| id | default | min | max | meaning |
|---|---|---|---|---|
| `thresh` | -20 | -60 | 0 | threshold [dB] |
| `ratio` | 4 | 1 | 30 | ratio |
| `attack` | 10 | 0.1 | 200 | attack [ms] |
| `release` | 120 | 5 | 2000 | release [ms] |
| `knee` | 6 | 0 | 24 | knee width [dB] |
| `makeup` | 0 | -12 | 36 | makeup gain [dB] |
| `mix` | 1 | 0 | 1 | compressed share (parallel compression) |
| `detect` | 0 | 0 | 1 | 0 peak, 1 RMS |
| `opto` | 0 | 0 | 1 | program-dependent release |
| `sc_hp` | 20 | 20 | 500 | side-chain high-pass [Hz] |
