// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nichlas Eklöf
// Included by the native-only host after its reporting/JIT helpers.
// libjbc's private pid_t differs from libc's public typedef.
#define pid_t jbc_private_pid_t
#include "jailbreak.h"
#include "kernelrw.h"
#undef pid_t

static int kernel_pointer(uintptr_t value) {
    return value >= 0xffff800000000000ULL && value < 0xffffffff00000000ULL && !(value & 7);
}

static void show_rights(const char *label, const struct jbc_cred *cred) {
    report("%s uid=%u auth=%016llx", label, cred->uid, (unsigned long long)cred->sceProcType);
    report("%s cred=%016llx cap=%016llx", label,
        (unsigned long long)cred->sonyCred, (unsigned long long)cred->sceProcCap);
}

static void credential_probe(int (*action)(void)) {
    OrbisKernelSwVersion firmware = {0};
    firmware.Size = sizeof(firmware);
    int rc = sceKernelGetSystemSwVersion(&firmware);
    report("Firmware query: rc=0x%08x version=0x%08x", (unsigned)rc, firmware.Version);
    if (rc < 0 || (firmware.Version & 0xffff0000U) != 0x09600000U) {
        report("FAIL this credential experiment is restricted to firmware 9.60");
        return;
    }
    report("BASELINE: JIT with original process rights");
    if (check_jit()) {
        report("PASS baseline JIT; credentials were not changed");
        if (action)
            report(action() ? "RESULT PASS MONO=managed RESTORE=not-needed" : "RESULT FAIL MONO; RIGHTS=unchanged");
        return;
    }
    report("BASELINE failed; checking existing homebrew kernel service");
    // Same pinned kernel-copy implementation as UT99, with zeroed syscall
    // arguments. No exploit is installed; the existing payload must supply it.
    uintptr_t td = jbc_krw_get_td();
    if (!kernel_pointer(td)) { report("FAIL unavailable kernel service"); return; }
    uintptr_t proc = jbc_krw_read64(td + 8, KERNEL_HEAP);
    if (!kernel_pointer(proc)) { report("FAIL invalid process pointer"); return; }
    uintptr_t ucred = jbc_krw_read64(proc + 0x40, KERNEL_HEAP);
    if (!kernel_pointer(ucred)) { report("FAIL invalid credential pointer"); return; }
    struct jbc_cred original = {0}, observed = {0};
    if (jbc_get_cred(&original)) { report("FAIL original credential read"); return; }
    show_rights("BEFORE", &original);
    // These three offsets and values are from pinned libjbc. Do not call its
    // broad jailbreak/set_cred helpers: UID, prison and filesystem roots stay put.
    uint64_t saved[3] = {original.sceProcType, original.sonyCred, original.sceProcCap};
    uint64_t current[3] = {0};
    uintptr_t address = ucred + 88;
    if (jbc_krw_memcpy((uintptr_t)current, address, sizeof(current), KERNEL_HEAP) ||
        memcmp(saved, current, sizeof(saved))) {
        report("FAIL credential snapshot changed; no write attempted"); return;
    }
    uint64_t requested[3] = {0x3801000000000013ULL, UINT64_MAX, UINT64_MAX};
    report("APPLY temporary auth/cred/cap only");
    int applied = !jbc_krw_memcpy(address, (uintptr_t)requested, sizeof(requested), KERNEL_HEAP);
    int jit_passed = 0;
    int action_passed = 0;
    if (applied && !jbc_get_cred(&observed)) {
        struct jbc_cred expected = original;
        expected.sceProcType = requested[0];
        expected.sonyCred = requested[1];
        expected.sceProcCap = requested[2];
        applied = !memcmp(&expected, &observed, sizeof(expected));
    } else applied = 0;
    if (applied) {
        show_rights("AFTER", &observed);
        failed = 0; // The baseline failure was expected; now test changed rights.
        jit_passed = check_jit();
        if (jit_passed && action) {
            report("JIT passed; running Mono with temporary process rights");
            action_passed = action();
        }
    } else report("FAIL temporary credential write/readback");

    // Restore even after a failed/partial write. Every normal post-write path
    // reaches this block. A process/kernel crash cannot guarantee restoration.
    report("RESTORE original auth/cred/cap");
    int restored = 0;
    for (int attempt = 0; attempt < 3 && !restored; ++attempt) {
        if (jbc_krw_memcpy(address, (uintptr_t)saved, sizeof(saved), KERNEL_HEAP)) continue;
        memset(&observed, 0, sizeof(observed));
        restored = !jbc_get_cred(&observed) && !memcmp(&original, &observed, sizeof(original));
    }
    if (!restored) {
        report("FAIL RESTORE readback; close app and restart console");
        return;
    }
    show_rights("RESTORED", &observed);
    report("PASS original credentials restored and verified");
    if (action)
        report(applied && jit_passed && action_passed
            ? "RESULT PASS MONO=managed RESTORE=verified" : "RESULT FAIL MONO/JIT; RESTORE=verified");
    else
        report(applied && jit_passed ? "RESULT PASS JIT=42 RESTORE=verified" : "RESULT FAIL JIT unavailable; RESTORE=verified");
}
