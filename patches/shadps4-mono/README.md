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

Verified 2026-09-30, evidence `build/shadps4-mono/20260930-112406/`:
- Native GPU frame and JIT return-42 test PASS.
- Mono PRX loads; its real JIT create/alias/map calls succeed (256 KiB RWX).
- Managed test does NOT pass; runtime initialization still aborts. Actual
  called stubs: access, getenv, getrlimit, set_constraint_handler_s,
  vsnprintf, fprintf and abort. Missing formatting also hides the abort
  diagnostic, so the root cause is not established yet.
- Same binary with the experiment disabled previously stopped earlier in
  mspace creation (`20260930-111329`); enabling mspace alone advanced to the
  missing JIT operations (`20260930-111342`).

Next: implement/test PS4 libc varargs formatting and diagnostic output with
ABI-correct guest argument handling, then inspect the actual Mono error.
Do not simply forward guest va_list or FILE pointers into host libc.
A user-owned libc dump remains an optional alternate diagnostic route.
Threading, GC, managed/native calls and ROM gameplay remain unverified.
Emulator timings will not establish the physical PS4's Jaguar CPU throughput.

Final build-script rerun also passed (including sanitizer tests); repeated
probe `build/shadps4-mono/20260930-112548/result.json` reproduced JIT PASS
and managed FAIL. Final emulator SHA-256:
`e66e12230a948054860179585f5a0ef196d08ec182de86b989915abce8ef7d9e`.
The patch was checked against a fresh index at the pinned upstream revision.
