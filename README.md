# vu — terminal stereo VU meter

Real-time stereo loudness metering for your terminal. `vu` listens to your
audio sink's **monitor source** — exactly what is reaching your speakers —
and renders two animated bars on a dB scale with peak-hold markers.

```
    -60   -50    -40   -30    -20   -10    0  dBFS
L  [██████████████████████████████████·     ]     -6.0
R  [███████████████████████████·          ]     -7.4
monitor: alsa_output.usb-M-Audio_M-Track_2X2-00.pro-output-0.monitor | theme: default
q quit · d device · c theme · t units · space pause
```

## Features

- **Meters the real output path.** Taps the sink's monitor source, so it
  shows what every application is actually playing, summed — not one
  stream.
- **RMS ballistics + peak-hold.** Bars follow RMS level with a 20 dB / 0.3 s
  release (standard PPM-style); the `·` marks the recent peak and decays on
  its own. Both are tunable.
- **As close to real-time as the audio stack allows.** The whole pipeline —
  wire quantum + buffer + meter + UI — adds roughly **one audio quantum
  (~20 ms)** of lag, not the hundreds of milliseconds a naive buffer
  would.
- **dBFS or relative mode.** `t` toggles between absolute dBFS and dB
  relative to the session's loudest level.
- **Palette-adaptive themes.** Four presets (`default`, `warm`, `mono`,
  `neon`) plus per-color overrides. Bars use the terminal's base-16 palette
  and text/scale use the terminal's own default colors, so `vu` looks right
  in light *or* dark terminals and with any 256-color palette (Gruvbox,
  Catppuccin, …).
- **Device cycling.** `d` walks every monitor source on the system;
  `--list` shows them all.
- **Pause, config file, CLI overrides**, graceful resize, clean shutdown.
- Pure C11, one static-ish binary, `ncursesw` + `libpulse` — no other deps.

## Build

```sh
make        # builds ./vu
make test   # unit tests (meter DSP, config parsing)
```

Requires the `libpulse` and `ncursesw` dev packages.

## Run

```sh
./vu                          # default sink's monitor
./vu --list                  # list monitor sources
./vu -d <monitor>            # pick a monitor source
./vu -t neon -b 60           # theme + bar length
./vu --db-min -50 --db-max 0 # custom scale range
./vu -c /path/to/vu.conf     # alternate config file
```

Keys while running:

| key | action |
|-----|--------|
| `q` | quit |
| `space` | pause / resume the meter (audio keeps playing) |
| `t` | toggle dBFS / dB relative to session max |
| `c` | cycle theme |
| `d` | cycle to the next monitor source |

## Configuration

`~/.config/vu/vu.conf` (INI-style, `key = value`); CLI flags override the
file, the file overrides the built-in defaults. Full sample: `config/vu.conf`.

| key | default | meaning |
|-----|---------|---------|
| `theme` | `default` | `default`, `warm`, `mono`, or `neon` |
| `bar_length` | `40` | bar cells, 4–200 (auto-shrinks on narrow terminals) |
| `db_min` / `db_max` | `-60` / `0` | scale range in dB |
| `release_dbps` | `60` | bar fall speed in dB/second (higher = snappier) |
| `hold_decay_dbps` | `30` | peak-marker decay in dB/second |
| `rms_window` | `0.01` | RMS averaging window in seconds |
| `color_normal` / `color_hot` / `color_peak` / `color_scale` / `color_text` | preset | any color name, `0`–`255`, or `default` (terminal default); any override switches the theme to `custom` |

## Metering & latency

The display lags real audio by:

1. one wire quantum from the PipeWire/Pulse monitor tap (~21 ms at 48 kHz
   with a 1024-frame period),
2. ~half the client receive buffer — `vu` sizes that buffer to **one
   quantum**, so this adds only ~10 ms instead of the 100+ ms a 200 ms
   buffer would,
3. the RMS window (10 ms) and one UI frame (~10 ms).

Total ≈ 50 ms worst case — the bar moves with the beat, and after a
stop the bars settle in under a second. If you want an even faster fall,
raise `release_dbps`; the hard floor is the audio stack's own quantum,
which no meter can beat.

## Notes

- Works over the **PulseAudio protocol**, including PipeWire's Pulse bridge
  (the common setup). It deliberately does not use the native PipeWire
  API — this codebase targets the older `libpulse` headers that ship on
  this system.
- The receive-buffer sizing (`maxlength = tlength = 1 quantum`) is tuned
  for the PipeWire 1.6.10 Pulse bridge, which delivers 1 frame per write
  when `maxlength` is 0. If you ever run against a different server and
  see choppy updates, that line in `src/backend.c` is the place to look.
- Bars are drawn as reverse-video blocks, so no UTF-8 width assumptions —
  works in narrow terminals too (the status line wraps; below ~20 columns
  it shows a "too narrow" message instead).
- The metering is of the *sink's* output: system sounds, other apps, and
  anything routed to that sink are all included.
