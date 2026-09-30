// Fault-injection test of the actual transaction/restore code, without PS4 writes.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define pid_t jbc_private_pid_t
#include "jailbreak.h"
#include "kernelrw.h"
#undef pid_t
typedef struct { size_t Size; char VersionString[28]; uint32_t Version; } OrbisKernelSwVersion;
static struct jbc_cred state, initial;
static int failed, mode, writes, jit_calls, result_pass, restore_fail, action_calls;
static uint32_t firmware_version;
static const uintptr_t td_addr = 0xffff800000001000ULL;
static const uintptr_t proc_addr = 0xffff800000002000ULL;
static const uintptr_t cred_addr = 0xffff800000003000ULL;
static void report(const char *format, ...) {
    if (!strcmp(format, "RESULT PASS JIT=42 RESTORE=verified")) result_pass++;
    if (!strcmp(format, "RESULT PASS MONO=managed RESTORE=verified")) result_pass++;
    if (!strcmp(format, "FAIL RESTORE readback; close app and restart console")) restore_fail++;
}
static int sceKernelGetSystemSwVersion(OrbisKernelSwVersion *v) { v->Version = firmware_version; return 0; }
static int check_jit(void) { ++jit_calls; return jit_calls == 2 && mode != 1; }
uintptr_t jbc_krw_get_td(void) { return mode == 5 ? 0 : td_addr; }
uint64_t jbc_krw_read64(uintptr_t p, KmemKind kind) {
    assert(kind == KERNEL_HEAP);
    if (p == td_addr + 8) return proc_addr;
    assert(p == proc_addr + 0x40); return cred_addr;
}
int jbc_get_cred(struct jbc_cred *out) { *out = state; return 0; }
int jbc_krw_memcpy(uintptr_t dst, uintptr_t src, size_t size, KmemKind kind) {
    assert(size == 24 && kind == KERNEL_HEAP);
    if (src == cred_addr + 88) { memcpy((void *)dst, &state.sceProcType, size); return 0; }
    assert(dst == cred_addr + 88);
    ++writes;
    if (mode == 4 && writes > 1) return -1; // Restoration permanently denied.
    if (mode == 3 && writes == 2) return -1; // First restore fails; retry succeeds.
    if (mode == 2 && writes == 1) { memcpy(&state.sceProcType, (void *)src, 8); return -1; }
    memcpy(&state.sceProcType, (void *)src, size);
    return 0;
}
#include "credential-probe.h"
static int simulated_runtime(void) {
    ++action_calls;
    assert(state.sceProcType == 0x3801000000000013ULL);
    assert(state.sonyCred == UINT64_MAX && state.sceProcCap == UINT64_MAX);
    assert(state.uid == initial.uid && state.prison == initial.prison);
    return mode == 7;
}
int main(void) {
    for (mode = 0; mode <= 8; ++mode) {
        memset(&state, 0, sizeof(state));
        state.uid = 123; state.prison = 0xffff800000004000ULL;
        state.cdir = state.rdir = state.jdir = 0xffff800000005000ULL;
        state.sceProcType = 17; state.sonyCred = 23; state.sceProcCap = 42;
        initial = state;
        writes = jit_calls = result_pass = restore_fail = failed = action_calls = 0;
        firmware_version = mode == 6 ? 0x11000000 : 0x09600000;
        credential_probe(mode >= 7 ? simulated_runtime : NULL);
        if (mode == 4) { assert(restore_fail == 1 && !result_pass && writes == 4); }
        else assert(!memcmp(&initial, &state, sizeof(state)));
        if (mode == 0 || mode == 3 || mode == 7) assert(result_pass == 1 && jit_calls == 2);
        else assert(!result_pass);
        if (mode == 2) assert(writes == 2 && jit_calls == 1);
        if (mode == 5 || mode == 6) assert(writes == 0);
        if (mode >= 7) assert(action_calls == 1 && writes == 2);
        printf("PASS transaction fault case %d\n", mode);
    }
    return 0;
}
