# Third-party components

namfx itself is GPL-3.0-only, with a commercial license available (see [COMMERCIAL.md](COMMERCIAL.md)).
It uses the components below. All of them have permissive licenses, which allow both the GPL
release and commercial licensing.

| Component | Used for | License | Included how |
|---|---|---|---|
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | NAM model inference | MIT | fetched at build time (`tools/fetch_deps.sh`), compiled in |
| [Eigen](https://eigen.tuxfamily.org) | linear algebra for NAM | MPL-2.0 | NAM submodule or system `libeigen3-dev` (headers) |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON | MIT | via NeuralAmpModelerCore |
| [stb_truetype, stb_image_write](https://github.com/nothings/stb) | font rendering, PNG snapshots | MIT / public domain | `src/ui/` |
| [DejaVu fonts](https://dejavu-fonts.github.io) | on-screen UI fonts | Bitstream Vera / DejaVu license | `fonts/` (see `fonts/LICENSE-DejaVu.txt`) |
| [lilv](https://drobilla.net/software/lilv) (+ serd, sord, sratom, zix) | LV2 plugin host | ISC | system library (`liblilv-dev`), linked dynamically, optional |
| [LV2 headers](https://lv2plug.in) | LV2 plugin API | ISC | system package (`lv2-dev`) |
| ALSA (`libasound`) | audio I/O | LGPL-2.1 | system library, linked dynamically |
| SDL2 | display output | zlib | system library, linked dynamically |

## Not distributed with namfx

namfx loads these at runtime. They belong to their authors and are not part of namfx:

- NAM models (`.nam`), e.g. from [TONE3000](https://www.tone3000.com)
- impulse responses (`.wav`)
- LV2 plugins, e.g. guitarix, x42, Airwindows. They are installed by the user, e.g. with
  `apt install guitarix-lv2`.
- effect-type packs made by third parties (`data/fx/*.json`)

The test files in `data/` (`_test_NAM_example_*.nam`, `nam_example_di.wav`) come from
NeuralAmpModelerCore (MIT). The synthetic IR and the test tone were generated for namfx.
