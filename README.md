# QVerb — Auto-Clear Vocal Reverb for Akai MPC

A compact vocal reverb with six knobs and one switch. The reverb core is
MVerb's Dattorro figure-of-eight algorithm. The Auto Clear stage is a musical
vocal-keyed ducker; at 100% it becomes a hard tank clear.

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

No size, density, early-level or output-gain controls. The size is fixed at a
musical vocal hall and the output is limited internally.

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
`Room`, `Init`.

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
dist/Tilr - VST - QVerb-1.0.0.zip
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
      -> tone/damping -> stereo width -> Auto Clear -> dry/wet -> limiter
```

## Credits and license

- MVerb by Martin Eastwood (Dattorro reverb), GPL-3.0-or-later.
- MPC plugin framework from the MIT-licensed MPC Plugin SDK.
- QDelay/QVerb glue and Auto Clear by Tilr.
- REV-246 was used as a reference for the ducker and program idea; its EMT
  engine is not included.

The project is GPL-3.0-or-later. See `LICENSE` and `MVERB-LICENSE.txt`.
