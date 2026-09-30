# SMS read dispatch candidate

This is an opt-in experiment, not the default player build. It moves ordinary
SMS work-RAM and power-of-two Sega ROM reads into a short dispatch method.
The previous path called diagnostic hooks and last-read tracking on every read.

Live bank registers, RAM enable and cartridge enable are checked on reads.
Writes, I/O, Mega Drive reads, non-power-of-two ROMs and other cartridge mappers
use the existing implementation. The experiment skips SMS read diagnostics and
force-read hooks in the short path. Set `EUTHERDRIVE_PS4_SMS_DIAGNOSTICS=1`
before Mono starts to use the complete original diagnostic path instead.
The ordinary build without the flag retains its existing behavior.

## Reproduce

Use an explicit local ROM library as with the ordinary build (private JSON list
of paths), then build into a separate directory:

```sh
ED_CONSOLE_LIBRARY=/absolute/path/to/private-library.json \
  python3 scripts/build-console-player.py \
  --out build/sms-fast-read/candidate --sms-fast-read
python3 scripts/test-sms-read.py build/sms-fast-read/candidate/desktop-host
SHADPS4="$PWD/build/shadps4-dev/shadps4" SHADPS4_EXPERIMENTAL_MONO=1 \
  python3 scripts/probe-shadps4-mono.py --jit-preflight --timeout 300 \
  --benchmark-host build/sms-fast-read/candidate/desktop-host \
  --benchmark-rom build/console-player/rom01/game.sms \
  --benchmark-warmup 1200 --benchmark-frames 300 \
  --benchmark-input probes/consoles/inputs/alex-kidd.txt
python3 scripts/package-benchmark.py --version 0.02 \
  --player build/sms-fast-read/candidate/host/main.exe
```

Candidate assemblies must retain the baseline runtime dependency closure.
Neither `--benchmark-host` nor `--player` changes the ordinary package staging.
The package retains the same five fixed replay cases and reference signatures.
It updates the separate benchmark app, title ID EDBM00001.

## Correctness gates

The mapper test compares every address against `ReadSmsMemoryOriginal`, not
against a second copy of the optimized formula. It performs real bank, RAM and
memory-control writes; covers ROM sizes 0, 8, 16, 32, 48, 64, 128 and 256 KiB;
checks all seven mapper types; and repeats with full diagnostics enabled.
Each mode passes 9,437,184 comparisons. It is a memory-read test, not proof of
all games or I/O timing. Gameplay hashes provide a separate gate.

The current candidate main.exe SHA256 is
`94b4e35d8b6dcf8ed968537644edca63b08934194941875a5e7c59de05641266`.
The baseline remains
`ea9c9340a4c7ae20bc30da58216ef4b042d429fcdd2c35dcb23fc70b0ea333d9`.
Local memory-test evidence: `build/sms-read-tests/20260930-140732/`.

Desktop Alex Kidd repeats and PS4-Mono/shadPS4 Alex Kidd, Black Belt and Sonic 1
retain the baseline video/audio hashes and sample counts. Exact final capture
and ROM/input comparisons are recorded under `build/sms-fast-read/`.

Host measurements are throughput without timed GPU presentation or native
audio output, and do not establish a physical PS4 speedup. The new candidate
needs the same USB test as the baseline described in
[physical SMS measurements](PHYSICAL-SMS-BENCHMARK-2026-09-30.md).

## Host measurements, 2026-09-30

| Replay/runtime | Fresh baseline pipeline FPS | Candidate pipeline FPS |
|---|---:|---:|
| Alex Kidd, desktop Mono | 92.11 / 95.97 | 100.45 / 100.48 |
| Alex Kidd, PS4 Mono in shadPS4 | 89.51 | 96.04 |
| Black Belt, PS4 Mono in shadPS4 | 89.84 | 78.33 / 92.06 |

These ran sequentially with no other benchmark jobs. Normal desktop activity
was not disabled. The first Black Belt candidate measurement has a much higher
p95 (18.723 ms versus 12.114 on repeat); do not discard it or claim a reliable
general speedup. Alex Kidd shows a modest improvement in this sample, while
Black Belt demonstrates host variability larger than the expected benefit.
Sonic 1 is a correctness regression check (59.59 FPS), not a new paired speed
claim. All six guest runs match their ROM/replay, output hashes and exact final
PPM bytes; `build/sms-fast-read/comparison.json` records the checks.

No optimized native Z80, VDP changes, sound changes or new graphics driver are
included. Physical benchmark 0.02 is needed to decide whether this candidate
is worth promoting. A result near 28 FPS still leaves substantial core, VDP
and mixing work before 60 FPS.
