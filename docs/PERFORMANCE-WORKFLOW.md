# Performance iteration, 2026-09-30

The user tested 0.20 on PS4: it runs, but is still far from 60 FPS.
There is no numerical 0.20 hardware measurement yet. The 0.19 photo showed
about 18.7 FPS, core 32.0 ms, audio 4.4 ms and video 17.0 ms.

## shadPS4 is currently blocked before managed gameplay

Version 0.18.0, revision `e3ce810f3a653f43ac64ebab63023de281a4103a`,
successfully presents the first native Vulkan frame, then the normal host
reports `FAIL existing libkernel not found`. This is not a game FPS result.
The matching local shadPS4 source shows GetModuleList enumerating guest
modules and Dlsym looking up their exports; our libkernel discovery does
not find the HLE implementation as a guest module. The JIT shared-memory
entry points appear in its symbol-name table but have no implementation
in the inspected source.

A separate, build-only experiment attempted `run_mono()` when kernel
discovery failed, without invoking the credential transaction. It used
the same 0.20 managed assembly and Doom3 Vulkan libraries. Mono's PRX
started, called the unimplemented `sceLibcMspaceCreate`, then `abort` and
`ud2`; shadPS4 exited 133. No managed code or ROM frame ran. Bypassing our
initial check therefore does not make this a usable gameplay benchmark.

Local evidence: `build/emulator-mono-investigation/{host.c,build.sh,run.sh,
build.log,result.log,emulator.log}`. The experiment has its own eboot,
stage and emulator data profile. It did not replace the physical package,
normal staging symlink, USB contents or production host source.

## Desktop Mono: repeatable CPU/audio/framebuffer baseline

Run after building and validating the console player:

```sh
python3 scripts/benchmark-console-player.py \
  build/console-player/rom01/game.sms \
  build/console-player/rom02/game.md \
  build/console-player/rom04/game.sfc
```

These local slots are Alex Kidd, Sonic 1 and Zelda respectively. ROMs are
private and are not tracked. Explicit ROM paths also work.

The harness loads the already-built `main.exe`; delegates call the actual
`Orbis.Emulator` backend. It does not rebuild or substitute a simplified
core. Each repeat is a new Mono process, loads the ROM, runs 300 warmup
frames and measures 900 frames using the existing smoke-test button script.
Three repeats are the default. Actual audio is mixed and drained every frame,
and the real framebuffer conversion runs every frame. No artificial sleep,
frame skipping or muted-audio shortcut is used.

`pipeline_fps` is the reciprocal of the mean time in RunFrame, ConsumeAudioBuffer
and GetFrameBuffer. `wall_fps` also includes input, harness bookkeeping and
hashing every output pixel/audio sample. Both exclude loading, warmup and
final reporting. Neither includes native audio resampling/queueing,
Vulkan presentation, vsync, PS4 Mono or PS4 CPU behavior. They are throughput,
not displayed FPS. Frame p95 and GC collection counts are also recorded.

Repeated video/audio hashes and sample counts must match. Full logs,
assembly/ROM hashes, runtime version and timing results are written to
`build/benchmarks/<timestamp>/manifest.json`. Hash matches establish
repeatability; correctness still needs the existing ROM smoke/reference
checks. Keep ROM, frame range, input, runtime options and machine load
consistent when comparing changes. `--host` allows another built player;
`--mono-option=--optimize=all` permits a separate runtime experiment.

## Measurements

Baseline 0.20, Xeon E5-2697 v3, desktop Mono 6.12.0, three fresh processes
per game, 300 warmup + 900 measured frames:

| Game | Pipeline FPS range | Wall FPS range | Core ms/frame | Audio ms/frame | Copy ms/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| Alex Kidd (SMS) | 100.17–100.62 | 98.48–98.92 | 8.13–8.17 | 1.35–1.36 | 0.46 |
| Sonic 1 (MD) | 67.84–68.61 | 66.72–67.48 | 12.58–12.74 | 1.34–1.35 | 0.65–0.66 |
| Zelda (SNES) | 51.54–52.30 | 51.02–51.76 | 18.59–18.87 | 0.001 | 0.53 |

SNES audio generation occurs inside its core; the tiny audio-buffer retrieval
time does not mean sound generation is free. All three games had identical
full-output hashes and sample counts across repeats. Evidence:
`build/benchmark-0.20.log`, `build/benchmarks/20260930-103603/manifest.json`.
Player assembly SHA-256:
`ea9c9340a4c7ae20bc30da58216ef4b042d429fcdd2c35dcb23fc70b0ea333d9`.

A separate three-repeat Alex Kidd experiment with `--mono-option=--optimize=all`
gave 100.94, 100.63 and 95.84 pipeline FPS. The median changed by only +0.15%,
with a slower third run; this provides no convincing speedup. Every output
hash/sample count matched the default run. This is desktop Mono only and
does not settle PS4 JIT performance. No production runtime flags changed.
Evidence: `build/benchmark-0.20-mono-all.log`,
`build/benchmarks/20260930-103929/manifest.json`,
`build/benchmark-0.20-comparison.json`.

## Iteration gates

1. Establish repeatable desktop baseline and inspect the dominant stage.
2. Change one CPU/JIT/audio/frame-copy issue; compare repeated timings and
   output hashes against baseline. Keep the five-ROM correctness suite.
3. Continue native Vulkan correctness/startup checks under shadPS4. A first
   GPU frame is not a managed-gameplay pass.
4. Send a physical package when a meaningful gain survives these checks.
   Measure the same game/scene on PS4; a desktop 60 FPS result cannot certify
   the console's CPU, runtime, audio and presentation costs.

Getting full gameplay into shadPS4 remains a separate compatibility task:
libkernel discovery alone is insufficient; Mono also needs libc and runtime
services. Do not remove physical JIT/credential checks to mask this failure.

## Repeatable Mono compatibility probe

```sh
python3 scripts/probe-shadps4-mono.py --timeout 25
# Once a decrypted system module has been read from the user's console:
python3 scripts/probe-shadps4-mono.py --sys-modules /path/to/private/module-dump
```

The tool creates a fresh build/stage/profile under `build/shadps4-mono/`.
It compiles the existing small runtime test (arrays, Span, generics,
exceptions, GC, timing, threads, native call and file IO), using the same
packaged PS4 Mono PRX/BCL. It generates a distinctly labelled native
diagnostic host, without the hardware credential/JIT preflight. Missing
libkernel discovery is logged, not presented as a success; native P/Invoke
still needs a real solution. The host explicitly labels the skipped preflight.
Production source, physical package and USB are untouched.

`--sys-modules` links only `libSceLibcInternal.sprx` into this fresh emulator
profile. There is no such dump on this computer at the time of this check.
The matching shadPS4 `src/core/linker.cpp` explicitly loads this file and
initializes its allocator; absent it, shadPS4 falls back to incomplete HLE.
Dumping this module is the next narrow experiment, not a guarantee of Mono
support. Read it from the user's PS4 via a decrypt-capable FTP server when
available. The user said FTP is off and can be enabled later; do not assume
a connection/IP/port. See the [official shadPS4 setup guide](https://github.com/shadps4-emu/shadPS4/wiki/I.-Quick-start-%5BUsers%5D)
for the distinction between ordinary FTP copies and decrypted dumps.

The new probe was actually run without system modules on 2026-09-30:
first GPU frame PASS, Mono load reached, then `sceLibcMspaceCreate`, `fwrite`
and `abort` stubs were called, followed by exit 133. Tool exit 3 correctly
reports no managed pass. Evidence: `build/shadps4-mono/20260930-105012/`.
It records module/runtime hashes, checkpoints, *called* stubs and all unresolved
imports separately in `result.json`. In this run 83 libc, 60 kernel and two
RegMgr names were unresolved across the loaded modules; this does not mean
every function is needed on the actual execution path.

Among unresolved imports are `sceKernelJitCreateSharedMemory`,
`sceKernelJitCreateAliasOfSharedMemory` and `sceKernelJitMapSharedMemory`.
Their names exist in the matching shadPS4 symbol table, but implementations
were not found. They will need investigation/implementation after startup
advances; returning fake success is not sufficient. Shared RX/RW aliasing,
execution, cleanup, threading and GC must actually work before ROM timings
are meaningful.

Even with the same PS4 Mono running, shadPS4 executes x86-64 guest code on the
host CPU (see `RunMainEntry` in its linker). It does not reproduce a Jaguar
CPU's cycle costs here. Thus this can improve runtime compatibility and
code-path comparisons, but does not eliminate the need to profile the 19 FPS
case on hardware. A corresponding physical stage/microbenchmark is needed
to separate runtime/JIT cost from CPU and presentation costs.
