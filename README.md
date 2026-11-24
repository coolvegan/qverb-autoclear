# QVerb — Auto-Clear Vocal Reverb for Akai MPC

A compact vocal reverb with six knobs and two switches. The reverb core is
MVerb's Dattorro figure-of-eight algorithm. The Auto Clear stage is a musical
vocal-keyed ducker; at 100% it becomes a hard tank clear. The output limiter
runs at 2x oversampling, so a hot reverb sum stays silky instead of turning
fizzy.

![QVerb on the Akai MPC](qverb-mpc.jpg)

## Controls

| Control | Function |
|---|---|
| **Mix** | dry/wet |
| **Decay** | reverb time, 0.3–12 s |
| **Tone** | dark to airy damping/bandwidth |
| **Pre-Delay** | 0–120 ms |
| **Width** | mono to extra wide |
| **Clear** | Auto Clear depth, 0–100% |
| **Auto Clear** | on/off |
| **OS 2x** | 2x oversampling around the output limiter, on by default |

No size, density, early-level or output-gain controls. The size is fixed at a
musical vocal hall and the output is limited internally.

## Output limiter and OS 2x

The final stage is a soft clipper with its knee at **−0.35 dBFS**. Below the
knee it is bit-transparent; only peaks above it are shaped. With **OS 2x** on,
the clipper runs at twice the sample rate, so the harmonics it creates stay
above Nyquist instead of folding back into the audible band. That fold-back is
what made hot tails sound harsh and "fizzy".

- **On** (default): 6 samples of latency (0.14 ms at 44.1 kHz), reported to the
  host and compensated automatically. Use this for anything loud.
- **Off**: latency-free, original 1x clipper. Useful when the track already has
  latency-critical routing or you want the exact 1.0 behaviour.

The reverb core itself is linear and static, so it does not need oversampling.

## Auto Clear for vocals

MPC effects never receive MIDI, so the trigger comes from the audio of the
voice. The detector is adaptive and works over a sustained pad or a busy mix.

- **0–99%**: the wet reverb is smoothly reduced while the vocal is present and
  blooms again after the phrase. This is the setting for breathy, airy vocals.
- **100%**: hard clear. The wet path fades to silence, the complete reverb tank
  is wiped, and the new vocal refills it. The old tail is gone.
- The dry vocal is never filtered or ducked.

## Programs

`Vocal Air` (default), `Close Vocal`, `Hazy Pad`, `Dub Vocal`, `Wide Hall`,
`Room`, `Init`, `Silky Drums`.

`Silky Drums` is Mix 7 %, Decay 2.80 s, Tone 80 %, Pre-Delay 10 ms, Width
140 %, Clear 62 %, Auto Clear on: a short, bright, wide bloom that only opens
between the hits.

## Build

Requirements: Docker, a C++17 compiler for the native host test, and Python 3.

```bash
./scripts/build-mpc.sh
```

The script builds the native copy, runs the MPC host simulator, generates the
touch UI, cross-compiles ARM64 and ARM32 under Docker, validates the folder and
writes:

```
dist/mpc/Tilr - VST - QVerb/
dist/Tilr - VST - QVerb-1.1.0.zip
```

Install on a connected MPC:

```bash
./scripts/install-mpc.sh
```

Copy the folder to `<drive>/VST2 Plugins/`, enable **External Plugin Support**
in the MPC preferences, restart/rescan.

## Signal path

```
Input -> 80 Hz high-pass -> Pre-Delay -> MVerb/Dattorro
      -> tone/damping -> stereo width -> Auto Clear -> dry/wet
      -> 2x oversampled limiter (knee -0.35 dBFS)
```

## Versions

- **1.1.0** — 2x oversampled output limiter with an on/off switch, knee raised
  from −0.92 to −0.35 dBFS, new `Silky Drums` program.
- **1.0.0** — first release. Tag `v1.0.0` is the reference version with the
  original 1x limiter.

## Credits and license

- MVerb by Martin Eastwood (Dattorro reverb), GPL-3.0-or-later.
- MPC plugin framework from the MIT-licensed MPC Plugin SDK.
- QDelay/QVerb glue and Auto Clear by Tilr.
- REV-246 was used as a reference for the ducker and program idea; its EMT
  engine is not included.

The project is GPL-3.0-or-later. See `LICENSE` and `MVERB-LICENSE.txt`.
