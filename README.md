# vu — terminal stereo VU meter

A terminal-based stereo loudness meter for PulseAudio/PipeWire, in C + ncursesw.
It meters a **monitor source** (what your sink is playing) and shows two bars on a
dB scale with peak-hold markers.

## Build

```sh
make        # builds ./vu
make test   # unit tests (meter DSP, config parsing)
```

Requires: `libpulse`/`libpulse-simple` dev, `ncursesw` dev (`pkg-config` for both).

## Run

```sh
./vu                        # default sink's monitor
./vu --list                # list available monitor sources
./vu -d <monitor>          # pick a monitor source
./vu -t neon -b 60         # theme + bar length
```

Config file `~/.config/vu/vu.conf` (see `config/vu.conf` for all keys); CLI flags
override it. Keys: `q` quit · `d` cycle device · `c` cycle theme · `t` toggle
dBFS/relative units · `space` pause.
