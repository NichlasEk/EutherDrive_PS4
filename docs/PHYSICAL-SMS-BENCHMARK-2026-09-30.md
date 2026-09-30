# Physical PS4 SMS benchmark, 2026-09-30

USB benchmark 0.01 produced four completed cases with matching video/audio
hashes, sample counts and nonzero counts. Final PPM SHA256 also matches the
previous isolated shadPS4 reference for both repeats of each game.

| Game | Physical pipeline FPS, r1 / r2 | shadPS4 pipeline FPS | Physical core ms | Audio getter/mix ms | Framebuffer copy ms |
|---|---:|---:|---:|---:|---:|
| Black Belt | 25.82 / 25.84 | 86.99 | 32.376 / 32.347 | 4.497 / 4.489 | 1.859 / 1.858 |
| Alex Kidd | 27.84 / 27.83 | 94.67 | 29.654 / 29.652 | 4.407 / 4.419 | 1.860 / 1.861 |

Each case uses 1,200 warmup and 300 measured frames, fixed replay, and a fresh
Mono domain. Timed intervals exclude GPU presentation and native audio output.
Physical wall FPS including hashing is 25.62 / 25.64 and 27.60 / 27.60,
respectively. Repeats are very close; this is not a long thermal stability test.

Sampled SMS subprofiles: Black Belt Z80 21.6 ms, VDP 9.9 / 10.0 ms;
Alex Kidd Z80 19.5 / 19.4 ms, VDP 9.8 ms. These are sampled independently
of full-frame averages and should not be treated as an exact additive split.
Gen0 collections: Black Belt 6 / 5, Alex Kidd 2 / 2; no Gen1/2 collections.
Collection counts alone do not establish GC duration.

## Interpretation and next experiment

The tested compute pipeline is about 3.4 times slower on the physical console
than in shadPS4 on the Xeon host. This is the same player, Mono binary, ROM
and replay, with matching output. shadPS4 does not model Jaguar execution
speed; host FPS cannot predict console FPS.

Presentation or audio-queue waits cannot explain this measured deficit: neither
is in the timed path. This does not isolate C# or Mono as the sole cause; CPU,
generated code, memory access and runtime behavior still need distinguishing.

Alex Kidd takes about 35.9 ms in the pipeline; 60 FPS needs 16.67 ms, requiring
about 2.15 times the current throughput before any additional serial output
cost. The sampled Z80 alone costs about 19.5 ms. Removing all of that cost
would still leave roughly 16.4 ms, so Z80 improvements alone are unlikely to
provide comfortable 60 FPS headroom.

First controlled candidate: remove disabled diagnostic work from the Z80
memory-read hot path while retaining mapper behavior. Compare deterministic
hashes in shadPS4, then repeat the same physical benchmark with baseline and
candidate. Follow with VDP rendering and sound mix costs. Do not switch graphics
drivers based on these measurements. The earlier interactive ~19 FPS photo
used a different workload; subtracting its timing from this replay would not
give a reliable presentation cost.

## Evidence and limits

Local archive: `build/hardware-results/20260930-135911/` (ignored).
All 95 files from ten USB snapshots were copied and SHA256-verified against
the stick. `archive.json` records sizes/hashes; `comparison.json` records the
four cases and reference paths. Original USB files remain intact.

Latest snapshot: `EutherDriveBench/run-0010/`, internal suite run-0001.
It contains four completed cases and START sonic1. No completed MD/SNES result,
no complete.txt, and no final Mono cleanup/credential restoration result are
present in this partial export. This does not establish a crash. Snapshot
numbers are export numbers, not independent full-suite repetitions.
No `.partial` files were found in the archive.

Exported build-info.json matches the delivered 0.01 bootstrap hash:
`03215a9562397a52706ab5a1f97f39b102b1507176281e1166d8f57f04925014`.
Player: `ea9c9340a4c7ae20bc30da58216ef4b042d429fcdd2c35dcb23fc70b0ea333d9`.
Mono: `fa39527bbd559f72efd09d9f39c3b6d7ed03be7f75d47965ad333fa9d4932414`.

Sequential host references (not the later concurrent functional suite run):

- Black Belt: `build/shadps4-mono/20260930-124602-n2ln97kg/result.json`.
- Alex Kidd: `build/shadps4-mono/20260930-130604-gtgl14gl/result.json`.

See [performance workflow](PERFORMANCE-WORKFLOW.md) and
[benchmark usage](AUTO-BENCHMARK.md).
