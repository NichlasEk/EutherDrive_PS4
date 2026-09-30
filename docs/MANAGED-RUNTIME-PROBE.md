# Managed runtime probe

This probe is the first EutherDrive-specific PS4 milestone. It tests the
available PS4 Mono runtime before any emulator core or frontend is ported.

The managed executable targets the reference runtime's .NET Framework 4.5
profile. It checks arrays, `Span<T>`, generics, exceptions, garbage collection,
timing, threads, file I/O and a P/Invoke call to `sceKernelGetProcessTime`.
Results are printed and written to:

```text
/data/eutherdrive-ps4/runtime-probe.log
```

Version 0.02 also writes `/data/eutherdrive-ps4/native-probe.log` from native
entry, flushing each checkpoint before proceeding. It presents checkpoints
using VideoOut and leaves the final status on screen until closed with the PS
button. Photograph the last checkpoint if the console crashes or stops.

## Build the managed executable

```sh
./scripts/build-runtime-probe.sh
```

This pins `PS4-OpenOrbis-Mono` at revision
`183a861a85d026981160bf25b70c9297af4afdcf`. The reference checkout stays under
ignored `.deps/` unless `PS4_MONO_REFERENCE` points somewhere else. The script
also runs all platform-independent checks with desktop Mono.

## Build the local PS4 package

```sh
./scripts/package-runtime-probe.sh
```

Expected output:

```text
dist/eutherdrive-runtime-probe-0.06.pkg
```

The upstream host source is Unlicense, but its checkout contains prebuilt PS4
Mono/runtime files whose redistribution status has not been established. They
remain outside Git. Do not publish the generated package or copy those files
into this repository before that audit is complete.

The native host is now `probes/runtime/host.c`, compiled with warnings as errors.
The upstream host and its broad jailbreak/set_cred path are not run. Version
0.06 uses the narrow credential transaction verified by native probe 0.05,
linked against UT99's pinned/adapted libjbc helper. The build therefore also
requires the sibling `ut99-orbis` build script and source cache documented in
[the native probe notes](NATIVE-JIT-RIGHTS-PROBE.md).
The current package staging directory is `build/runtime-probe/current-pkgroot`.

The runtime's SPRX-only loader still requires a DLL-loading hook. Installation
is restricted to the SHA-256-pinned runtime, with segment bounds, instruction
signature, page alignment, and protection return values checked before use.
The credential experiment is restricted to firmware 9.60 and requires the
payload's existing kernel service. Failure to change page protection stops the
probe. Mono initialization, managed tests, cleanup and credential restoration
are now verified by the user's physical-console photo of 0.06 on the reported
firmware 9.60 / GoldHEN 2.4 console.

Managed dependencies are recursively resolved by `runtime-dependencies.py`
from the pinned BCL, copied to a fresh staging directory, and checked again
using only the staged files. Desktop Mono is a functional smoke test, not a
substitute for this dependency audit or the console runtime.

The package remains a local experiment: the upstream README explicitly
describes its bundled Mono runtime as a Sony binary. Successful packaging
does not establish redistribution permission.

## Acceptance gates

1. Managed host checks report `RESULT PASS` (native call skipped on desktop).
2. OpenOrbis host, SELF and package build successfully.
3. The package starts in shadPS4 and the log reports every test as `PASS`.
4. The same package starts on the physical PS4 and produces `RESULT PASS`.

Build, package, emulator and physical-console results are recorded separately.

## Current result — 0.06, 2026-09-29

**Physical console PASS.** The user's photo shows managed `RESULT PASS`,
`09b Mono cleanup returned`, verified credential restoration, and the definitive
`RESULT PASS MONO=managed RESTORE=verified`. This confirms the probe's managed
suite (arrays, Span, generics, exceptions, GC, timing, threads, P/Invoke and
file I/O), runtime teardown and restoration on that console. It does not yet
establish emulator-core correctness, frame delivery, gameplay audio or sustained
performance. Keep the 0.06 package/hash below as the known-working baseline.

The user confirmed [native probe 0.05](NATIVE-JIT-RIGHTS-PROBE.md) passed on
firmware 9.60 / GoldHEN 2.4: JIT returned 42 after the three Sony credential
fields changed, and the original credential snapshot was restored and verified.

Version 0.06 runs Mono as a callback inside that same transaction. It retains
the temporary rights through initialization, managed tests and
`mono_jit_cleanup`, then restores and reads back the original credentials.
Recoverable runtime/managed failures also go through cleanup and restoration.
A native process/kernel crash can still interrupt this path; restoration on a
crash is not guaranteed. The broader jailbreak and filesystem changes remain
excluded.

The definitive success line is `RESULT PASS MONO=managed RESTORE=verified`.
If baseline JIT already works, no credentials are changed and the suffix is
`RESTORE=not-needed`. Managed `RESULT PASS` by itself is insufficient: cleanup
and restoration must also finish. The emulator runner checks both logs.

Nine host-side fault-injection cases pass, including callbacks that succeed or
fail while rights are elevated followed by verified restoration. Native build,
desktop managed tests, assembly closure and PKG validation pass. Physical
Mono execution and teardown in 0.06 now pass as recorded above. The emulator still stops
before the transaction because it cannot expose the required libkernel handle.

### Earlier hardware findings

The user's 0.03 photo confirms all four firmware module loads, Mono module
load, required export resolution and checked DLL-hook installation succeeded.
The last checkpoint is `05 initializing Mono`; the user then reports a crash.
This narrows the failure to the `mono_jit_init` call but does not prove its cause.
No managed test ran in that observed execution.

Version 0.04 adds a native JIT preflight: create a 16 KiB shared object and
writable alias, map RX and RW views, check their shared contents, execute
`mov eax,42; ret`, then unmap and close both handles. Every operation is logged
and checked. Failure stops before Mono. Success proves only this native JIT
route, not Mono's allocator, thread, GC or assembly-loading compatibility.
API signatures were checked against
[CTurt's PS4 SDK](https://github.com/CTurt/PS4-SDK/blob/master/libPS4/source/jit.c).
The user's physical 0.04 photo shows `FAIL JIT create: 0x80020001` immediately
after `JIT create shared memory`. The installed SDK defines this as
`ORBIS_KERNEL_ERROR_EPERM`. The create call is denied in this process context;
alias creation, mapping, code execution and Mono initialization are not reached.
This establishes a permission blocker for the tested native JIT route, not the
precise faulting instruction in 0.03 or lack of JIT support on every payload.
The upstream host changed process credentials before Mono; our host deliberately
does not execute that firmware-sensitive path. Exact firmware/GoldHEN versions
are needed before evaluating a compatible process-permission setup.

Version 0.04 also logs image-loader entry, missing files, decode entry/results
and native library requests, enables optional Mono debug/print callbacks, and
serializes log/screen updates from runtime worker threads. It is a diagnostic
build, not a verified fix for the runtime initialization crash.

The user's physical-console photo of 0.02 confirms native entry, VideoOut and
kernel-handle discovery. It stops at `libSceIpmi.sprx` with `0x80020002`
(`ORBIS_KERNEL_ERROR_ENOENT` in the installed SDK). Mono has not started.
The reported code is the last direct path attempt, so it does not establish
the error returned by each earlier path.

Version 0.03 replaces firmware pathname loading with
`sceSysmoduleLoadModuleInternal` using the SDK IDs for IPMI, Net, SystemService
and UserService. Each call logs its module ID and result. Only the packaged
Mono module is loaded by its `/app0/sce_module` path. The host now imports
`libSceSysmodule.so` from firmware. This fixes the incorrect direct-path approach
in the sandbox; the next console test must establish whether firmware accepts
these module requests and whether Mono itself can then run. The subsequent
0.03 photo confirms the module requests succeed, as recorded above.

- Managed compilation and desktop Mono validation: **passed**.
- OpenOrbis host ELF/SELF and PKG construction: **passed**, without suppressing
  pointer-type errors.
- Managed dependency closure: **18 assemblies**, including previously omitted
  `System.dll` and transitive dependencies. A negative test omitting `System.dll`
  correctly fails the audit.
- `PkgTool.Core pkg_validate --verbose`: **passed** for every reported digest
  and signature.
- shadPS4: **blocked before managed execution**. Native entry, VideoOut buffer
  registration and file checkpoints work. The emulator does not expose a
  libkernel handle resolving `sceKernelGetProcessTime` through the module list;
  the host reports `FAIL existing libkernel not found` and stays alive with the
  diagnostic screen. The runner returns 3 for this controlled failure.
  The readable red status screen was visually verified in
  `build/runtime-probe/diagnostic-0.02.png` with SDL's X11 backend under Xvfb.
- Physical PS4: **0.04 stops cleanly at JIT allocation with EPERM**. Version 0.03 reaches Mono initialization and
  then crashes; 0.02's controlled failure is recorded above; 0.01 was reported
  to crash. Exact firmware and GoldHEN versions remain
  unconfirmed; user estimates firmware around 11 and 9 on two consoles.

### Why 0.01 was replaced

- Upstream passed imported function addresses as writable outputs to dlsym,
  used the wrong module for SystemService lookups, and overwrote the JIT alias
  pointer with the map export. The new host resolves only needed exports into
  real pointer variables and checks each result.
- Upstream unconditionally invoked a credential-changing routine and patched
  Mono without checking the module base or mprotect. Version 0.02 removes the
  credential operation and validates the retained DLL hook.
- Packaging omitted `System.dll` and several transitive DLLs. Desktop Mono's
  installed assemblies masked the issue.
- The old report was saved only at the end. Native checkpoints now persist
  immediately, and managed tests report `BEGIN` before each test.
- Standard loader support modules now come from the same SDK as the other
  probes; SFO category is `gd` rather than `gde`.

These are confirmed defects and changes, not proof of the original console's
precise faulting instruction. Preserve the original 0.01 artifact for comparison.

Reproduce the isolated emulator result with:

```sh
./scripts/run-runtime-probe-emulator.sh
```

Current artifact hashes:

```text
main.exe  d7fa1f5fbd688d5d0327e3067904d3e64dc7492de5f273bd32764801150df5cc
eboot.bin cbb8dcc0f0ed6826ac2f560507bf012ff252d0badc7bbcbe0943c0628109e589
PKG       21b0c55924d76a3b21f2a344b6bf25f7d53469234e82646cc8026eab8040cc53
```
