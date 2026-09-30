# Experimental shadPS4 Mono support

Upstream base: `e3ce810f3a653f43ac64ebab63023de281a4103a` (0.18.0).
The patch is GPL-2.0-or-later, matching shadPS4; see `COPYING`.
It contains independently written emulator code, not firmware or Mono binaries.

```sh
python3 scripts/build-shadps4-mono.py --jobs 8
SHADPS4="$PWD/build/shadps4-dev/shadps4" \
SHADPS4_EXPERIMENTAL_MONO=1 \
  python3 scripts/probe-shadps4-mono.py --jit-preflight
```

Sources stay in ignored `.deps/shadps4-mono`, and the built emulator stays in
`build/shadps4-dev`. The ordinary AppImage and PS4 package are not replaced.
The environment variable opts into experimental HLE registrations; without
it they are not installed. A real libc system module still takes precedence
over libc HLE in upstream's loader.

First slice: dynamic mspace creation/malloc/calloc/realloc/free. The shipped
Mono's allocation wrapper calls create with name, null base, zero capacity
and flags 6. That call shape was verified in the local pinned PRX at module
offset `0x27b950`; no disassembly/binary content is published here.
Other create modes deliberately fail. This is not full libc compatibility.

Each pool tracks its own allocations under a lock. Reallocation preserves
the original block on failure, calloc checks multiplication overflow, and
invalid/cross-pool frees do not reach the host allocator. Reference-counted
lookups retain a pool through concurrent handle destruction. Tests cover
alignment, zeroing, preservation, overflow, isolation, lifetime and eight
concurrent allocator users under AddressSanitizer/UndefinedBehaviorSanitizer.
Host-backed allocations are a bring-up implementation, not PS4 heap timing.

Do not infer managed gameplay from a native GPU frame or a successful PRX
load. The probe reports managed PASS only after its managed tests and native
host both report success. The hardware JIT/credential preflight is skipped
only in the clearly labelled generated diagnostic host; it is not patched
out of the production application.

The Linux JIT backend implements create/alias/map using memfd shared backing,
guest descriptors and the existing guest VMA manager. Executable mappings
are permitted only for these tagged JIT files; ordinary file mappings keep
the upstream behavior. Alias/map requests cannot escalate descriptor rights.
This implements the observed Mono path, not every PS4 JIT ABI behavior.

`--jit-preflight` verifies shared RX/RW views, rejected invalid descriptors
and permission escalation, closes both descriptors, then writes and executes
machine code returning 42. SDK import definitions are generated into an
isolated SDK overlay for the linker and SELF converter; they are not deployed
as a guest library. The production hardware preflight is unchanged.

Managed runtime bring-up (2026-09-30)
-----------------------------------

The original initialization failure is fixed without a firmware dump. Actual
blockers included guest varargs formatting, signal numbering/query semantics,
Dinkumware character tables, a `strncpy_s` buffer overwrite, missing memory
operations, and dynamically generated TLS reads using host FS instead of guest
GS. The JIT monitor invalidates executable aliases on writes and translates the
observed Mono TCB-load instructions before execution, including rewritten code,
unaligned secondary entries and cross-page instructions. This is experimental
recognition of a narrow instruction shape, not a general arbitrary-JIT translator.

The virtual `libkernel` module exposes only registered emulator symbols; it is
not a fabricated firmware dump. Native calls now resolve through this module.
Directory functions use the guest VFS. That VFS has no guest symlink nodes;
`lstat` shares `stat` semantics, including errors, without exposing host mount
symlinks. Unknown sysctl queries return an error rather than fake success.

Linux thread suspension uses a reserved host signal, a real stopped register
context and a futex handshake. It supplies the observed Mono suspend/get-context/
resume path for GC. The handler does not allocate, log or take a mutex. General
context replacement and arbitrary concurrent suspend callers are not supported.

Evidence `build/shadps4-mono/20260930-121911/`: all eleven C# tests passed,
including concurrent GC with two allocating workers, directory enumeration,
threads, exceptions, Span, native calls and file IO; Mono cleanup returned.
Native signal delivery/masks/query/restore and shared-JIT/TLS tests also passed.
Earlier basic C# tests passed with and without guarded heap allocation
(`20260930-115953`, `20260930-120041`).

```sh
SHADPS4="$PWD/build/shadps4-dev/shadps4" SHADPS4_EXPERIMENTAL_MONO=1 \
  python3 scripts/probe-shadps4-mono.py --jit-preflight --stress --timeout 45
# Optional heap diagnostics; do not use for throughput measurements:
SHADPS4="$PWD/build/shadps4-dev/shadps4" SHADPS4_EXPERIMENTAL_MONO=1 \
SHADPS4_MONO_GUARD_HEAP=1 \
  python3 scripts/probe-shadps4-mono.py --jit-preflight --stress --timeout 45
```

Guarded allocations have inaccessible pages at both ends and retain freed
mappings as inaccessible until pool destruction. Alignment leaves up to 15 bytes
of padding before the upper guard; this is not full guest AddressSanitizer.
Standalone allocator, formatting and bounded-string tests run with ASan/UBSan.

`--benchmark-rom PATH --benchmark-frames 300` runs the unchanged packaged
player assembly and PS4 Mono against a local ROM, with 300 warmup frames. It
measures core/audio/framebuffer work and hashes generated samples/pixels; GPU
presentation and the native audio queue are not timed. ROM data stays local.
This executes on the host CPU and cannot predict Jaguar throughput on PS4.

Remaining compatibility limits include unsupported libc/kernel calls, partial
signal contexts, JIT protection/concurrency corner cases and self-modifying code
executing from its own writable page. Passing the tested runtime slice does not
establish support for arbitrary C# applications or full emulator gameplay.
The production PS4 package, credential preflight and USB are unchanged.

Final validation (2026-09-30)
---------------------------

- `20260930-123135-sjlqtyvk`: all twelve managed tests PASS with the final
  emulator, including floating remainder/formatting, concurrent GC, directories,
  native calls and cleanup. Native signal/JIT/TLS preflight PASS.
- `20260930-122458`: twelve tests also PASS with guarded heap allocation.
- `20260930-122750` / `20260930-122952`: actual PS4 Mono + unchanged player,
  Alex Kidd, 300 warmup + 300 measured frames: 95.99 / 97.19 pipeline FPS.
  Second run: wall 95.60 FPS, core 8.192 ms, audio 1.597 ms, copy 0.501 ms.
- Desktop comparison `build/benchmarks/20260930-122923`: 100.82 / 104.52 FPS.
  All four runs use identical ROM/player hashes and produce video `09cda47b`,
  audio `7f917301`, 441000 samples, 379964 nonzero samples.
- `20260930-123113`: the same emulator with the experiment disabled still
  fails at the unsupported Mono startup path; no false managed PASS.
- Build script, ASan/UBSan unit tests and subsequent incremental build passed.
  Patch application was checked using a fresh index at the pinned upstream HEAD.

Final emulator SHA-256:
`d0c25f626a62d8fdc1f6fe759304c6d2b455fe6bea63c431ba0c1493b2a1b8af`.
The source patch is the durable artifact; binaries, logs and ROMs stay ignored.
