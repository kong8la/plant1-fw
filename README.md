# PLANT-1 firmware for the M-VAVE FM-1

The FM-1 side of [PLANT-1](https://github.com/kong8la/plant1), the browser synth that started it.
This repository builds a real `.fwsc` package you can flash with
[the PLANT-1 flash page](https://kong8la.com/plant1/flash/).

## M5: an arpeggiator

![M5 on the screen](docs/m5.png)

- **patterns**: UP, DOWN, UPDN (ping-pong), RAND
- **rates**: 1/4, 1/8, 1/8T, 1/16, 1/16T against the tempo
- **octaves** 1-3, **gate** 5-95 % of the step
- **tap tempo** on SEQ (60-200 BPM), the ARP page shows the tempo
- ARP toggles it from any page; held keys become the arp's note pool

## M4: a modulation matrix

- four routes: ENV1 / ENV2 / LFO1 / LFO2 / velocity to any slot's level, cutoff or pitch
- the MOD page (K1 route, K2 source, K3 destination, K4 depth, GLO toggles)
- ENV toggles AMP/MOD, LFO toggles LFO1/LFO2

## M3: an effect chain

- five effect slots: DRIVE, CHORUS, DELAY (139 ms), REVERB, COMP
- an FX page (K1 walks the chain, K2-K4 edit A/B/C, FX toggles)
- four patch slots in flash (PATCH page: K2 picks, SAVE stores)

## M2: presets, envelopes and an LFO

- eight presets: INIT, GLASS BELL, FM BASS, SUPER SAW, PHASE PAD, PLUCK, ACID, DARK CHOIR
- ENV page with the curve drawn, LFO page with its shape, patch storage
- a screen that draws each slot's waveform, the envelope and the LFO

## M1: four generators on the device

- 4 generator slots: analog (saw/square/pulse/sine, polyBLEP), wavetable (4 tables, 2 frames,
  morph), FM (ratio/index/feedback), noise (white to pink)
- a TPT state-variable filter per slot (LP/HP/BP/notch), unison 1-3, six voices

## Build

The package is built on GitHub: `.github/workflows/firmware.yml` calls
[zvenson/fm1-build](https://github.com/zvenson/fm1-build), which installs JieLi's Linux toolchain,
clones the AC79 SDK, runs `build.sh`, runs `test.sh`, attests the `.fwsc` and uploads it as the
`packages` artifact.

Local host simulator (any OS, plain `cc`):

```bash
./build.sh --host
app/build/host/plant1m5_host app/tests/basic.txt app/build/host
app/build/host/plant1m5_host app/tests/arp.txt app/build/host
app/build/host/plant1m5_host app/tests/mod.txt app/build/host
app/build/host/plant1m5_host app/tests/fx.txt app/build/host
```

The device build needs a Linux host (the toolchain is Linux x86-64 only); `build.sh` and the
Actions workflow are the two supported ways.

## Flash it

1. Download the `packages` artifact from the Actions run (or a release).
2. Open <https://kong8la.com/plant1/flash/> in desktop Chrome or Edge.
3. The page loads the current build by itself; connect the FM-1 over USB and press Install.

The package identity is `FM-1_9500` for M5. The flash page checks the package's SHA-256, its
identity and that it carries the update loader before anything is written.

## Layout

| Path | What |
| --- | --- |
| `app/app.c`, `app/app.h` | the app: engine, modulation, arp, presets, effects, panel, screen, storage |
| `app/fm1.toml` | name, package identity, USB names, licence |
| `app/tests/basic.txt` | keys, slots, pages, presets |
| `app/tests/arp.txt` | the arpeggiator, its page and tap tempo |
| `app/tests/mod.txt` | the matrix and the AMP/MOD and LFO1/LFO2 switches |
| `app/tests/fx.txt` | the FX chain and the flash slots |
| `app/tests/leds.txt` | the light show (prints the LED masks) |
| `app/tests/audio*.txt` | render to `note.wav` / `tail.wav` |
| `build.sh` | fetches fm1-base at a pinned commit, runs its `tools/build.py` |
| `test.sh` | the host simulator checks, run by CI |

## Roadmap

- **M0**: one FM voice, ADSR, keys, screen, the build/flash chain proven
- **M1**: 4 generator slots, per-slot filter, unison, 6 voices, panel pages
- **M2**: presets, envelopes with the curve, an LFO, patch storage, a screen that shows the sound
- **M3**: 5-slot effects, an FX page, four flash patch slots
- **M4**: a modulation matrix (ENV1/ENV2/LFO1/LFO2/velocity to any slot's level, cutoff, pitch)
- **M5** (this): an arpeggiator with patterns, rates, octaves, gate and tap tempo
- **M6**: MIDI CC mapping, sixteen presets, a step sequencer

## Licences

The app (`app/`) is MIT. fm1-base and felucca-bsp are MPL-2.0 and are not vendored here; `build.sh`
clones them at a pinned commit.
