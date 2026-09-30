# EutherDrive GB core probe 0.07

First core milestone after physical runtime PASS in 0.06. This is a deterministic
DMG (original Game Boy) CPU/PPU/input test, not a game frontend. GBC mode, physical
controller mapping, displayed core frames, audible sound, saves and sustained
performance remain separate milestones.

## Source boundary

The build reads exactly 20 GB core source files from EutherDrive_Android Git
revision `7771ae7f736caef7f20399a6315d3140217bfbd2`, directory
`EutherDrive.Core/GbEmu`. It does not use a moving worktree or edit that project.
Each original file hash is recorded in `build/gb-probe/source-manifest.json`.
The desktop frontend, full project dependencies and reflection-based savestates
are excluded. GbAdapter is not linked; the probe invokes the existing Emulator
directly. This slice contains no detected Reflection.Emit/DynamicMethod use.

Compatibility is generated only under ignored `build/gb-probe/core`: one
`Array.Empty<short>()` becomes `new short[0]`, and ten `ThrowIfNull` calls become
equivalent explicit null checks. Global usings and compiler attributes are
supplied in `Compat.cs`. Serilog calls are compiled out for the probe to avoid
debug string allocations in the CPU loop. No emulation algorithms are changed.
Roslyn from the installed .NET SDK compiles modern C# against the .NET 4.5
reference profile and the same Span assemblies validated by runtime probe 0.06.

Core provenance and upstream MIT notice are in
`probes/gb/THIRD-PARTY-NOTICES.md`, also included in the package. EutherDrive's
frontend/integration remains Nichlas Eklöf's code. Runtime/libjbc distribution
limitations are unchanged from the earlier local probes.

## Test and independent image oracle

The probe generates an original 32 KiB ROM in C#, with no boot ROM, Nintendo
logo or commercial assets. It runs via the core's post-boot initialization.
The ROM initializes tile 0 and the entire background map, sets the DMG palette,
and repeatedly reads RIGHT through the emulated joypad port to set SCX to 0/1.

Each of the 23,040 pixels is checked against a separately calculated pattern
at frames 60, 120 and 180. Fixed palette colors and alternating stripe geometry
define the oracle; the first run does not generate its own expected results.
The reported FNV-1a hash uses four low-to-high bytes per 32-bit pixel.

| Frame | Simulated RIGHT | Expected frame hash |
| --- | --- | --- |
| 60 | released | `5fc4b9c5` |
| 120 | pressed | `b93fb5c5` |
| 180 | released | `5fc4b9c5` |

This tests CPU instructions, VRAM writes, rendering and input-sensitive scrolling
together. It is not a full GB accuracy suite. APU execution remains in RunFrame,
but samples are drained and sound hardware is disabled by the ROM.

## Build and validation

```sh
python3 scripts/build-gb-probe.py
ED_GB_PROBE=1 ./scripts/package-runtime-probe.sh
ED_GB_PROBE=1 ./scripts/run-runtime-probe-emulator.sh
```

Desktop Mono uses its own corlib plus the tested facade assemblies. PS4 gets
the pinned PS4 corlib and recursively audited dependency closure; those are
deliberately separate directories. The managed EXE bytes are identical in the
desktop run and console package.

- Desktop Mono: all three full-frame pixel checks and hashes **PASS**.
- Native build, nine credential fault tests, dependency closure and package
  signature/digest validation: **PASS**.
- shadPS4: native entry works, but the existing libkernel-handle limitation
  prevents reaching Mono/core execution. No core pass is claimed there.
- Physical PS4 9.60 / GoldHEN 2.4: **0.07 pending**. Runtime 0.06 passed there.

Output: `dist/eutherdrive-gb-core-probe-0.07.pkg`, same title ID as previous probes.
App title: **EutherDrive GB Core Probe**. It uses 0.06's credential transaction,
Mono cleanup and restoration. Each GB check and the definitive native result
are written to `/data/eutherdrive-ps4/native-probe.log` and shown on screen.
Success requires all three `PASS GB` lines and
`RESULT PASS MONO=managed RESTORE=verified`.

The working 0.06 PKG is retained unchanged for rollback/comparison.

SHA-256:

```text
main.exe ae4044046c2357db862c183c93f38be5247ac975f769b7bf15c56dfa420f1809
PKG      02dbd4e757765e537e1e08f9042e0943b1e3b282624d66a95444371cc78ce896
```
