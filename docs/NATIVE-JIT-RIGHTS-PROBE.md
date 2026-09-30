# Native JIT rights probe 0.05

This local hardware experiment follows 0.04's confirmed
`sceKernelJitCreateSharedMemory` failure `0x80020001` (EPERM). User reports
firmware 9.60 and GoldHEN 2.4. No Mono runtime, managed executable or BCL is
included. It uses the same title ID as the earlier probes and replaces the
installed probe, now titled **EutherDrive Native JIT Rights**.

Build: `./scripts/package-jit-rights-probe.sh`

Output: `dist/eutherdrive-jit-rights-probe-0.05.pkg`

The helper is the pinned/adapted libjbc already used by UT99's USB adapter:
`bucanero/ps4-libjbc` revision `835fe016ff0ae5dd89b9249f39cc0fe093fd07dd`.
The build calls `../ut99-orbis/scripts/build-ps4-usb.py`; that script accepts
`UT99_JBC_SOURCE` for its source cache. Input hashes are recorded in
`build/jit-rights-probe/jbc-source-sha256.txt`. Its unresolved license means
this linked helper remains a private test dependency.

## Transaction

1. Query firmware; refuse credential operations unless it reports 9.60.
2. Try the native return-42 JIT test with original rights. Stop if it passes.
3. Read the current process credentials using the existing homebrew kernel
   service and validate pointer ranges and the credential snapshot.
4. Write only the three contiguous Sony fields at the pinned helper's offset
   `ucred + 88`: auth ID `0x3801000000000013`, credential and capability masks
   `UINT64_MAX`. Read back and compare the full credential snapshot, including
   unchanged UID, prison and filesystem roots.
5. Run the same JIT test; release its mappings and handles on normal completion.
6. Restore the original three fields even if the temporary write failed or
   was partial. Retry up to three times and verify the full original snapshot.

The broad `jbc_jailbreak_cred` and `jbc_set_cred` routines are not called.
No filesystem root, UID, prison, firmware image or global kernel patch is
intentionally changed. No exploit is installed. This is still experimental
kernel-memory access: a process/kernel crash can interrupt the restoration
path. If the screen reports a restore failure, close the app and restart the
console before further tests. Do not use firmware spoofing for this test.

## Evidence and interpretation

- Build with warnings as errors and PKG validation: passed.
- Actual transaction code tested with seven injected scenarios: success,
  JIT failure, partial credential write, transient restore failure, permanent
  restore failure, missing kernel service and wrong firmware. Tests pass and
  assert restoration/no-write behavior or explicit restore failure as appropriate.
- ELF imports only `libkernel.so` and `libSceVideoOut.so`.
- shadPS4 cannot provide the existing libkernel handle needed by these probes;
  it cannot validate kernel rights or JIT execution on a physical PS4.
- Physical 0.05 result: **PASS**, confirmed by the user's console photo. The
  baseline failed; after changing only auth/cred/cap, native JIT returned 42.
  UID remained 1, and the original credential snapshot was restored and verified.
  This is native JIT proof on the user's reported 9.60 / GoldHEN 2.4 console,
  not proof of Mono execution. The observed final line was
  `RESULT PASS JIT=42 RESTORE=verified`.

Log: `/data/eutherdrive-ps4/jit-rights-probe.log`.

Expected success: `RESULT PASS JIT=42 RESTORE=verified`. Photograph the final
screen, including AFTER/RESTORED and any FAIL lines. Even success establishes
only the native JIT route; Mono initialization remains a separate test.

SHA-256: `2957452cc1c4e023d8597987b600741f81b673f4276e4477bbd4c35328e39aa3`.
