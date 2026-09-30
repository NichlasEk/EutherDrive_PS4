// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nichlas Eklöf
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Sysmodule.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <stb/stb_easy_font.h>
#ifdef GB_PLAYER
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#endif

#ifdef NATIVE_CREDENTIAL_PROBE
#define PROBE_TITLE "EutherDrive Native JIT Rights 0.05"
#elif defined(GB_PLAYER)
#ifdef CONSOLE_PLAYER
#define PROBE_TITLE "EutherDrive Consoles 0.14"
#elif defined(SMS_PLAYER)
#define PROBE_TITLE "EutherDrive Master System 0.10"
#else
#define PROBE_TITLE "EutherDrive GB 0.09"
#endif
#elif defined(GB_CORE_PROBE)
#define PROBE_TITLE "EutherDrive GB Core Probe 0.07"
#elif defined(MONO_CREDENTIAL_PROBE)
#define PROBE_TITLE "EutherDrive Mono Probe 0.06"
#else
#define PROBE_TITLE "EutherDrive Mono Probe 0.04"
#endif

static int logfile = -1, video = -1, failed, kernel_handle = -1;
static uint32_t *frames[2];
static int64_t sequence;
static char lines[18][100];
static unsigned line_count;
static unsigned char report_busy;

#ifdef GB_PLAYER
#include "gb-player.h"
#endif

static void rectangle(uint32_t *frame, int x, int y, int right, int bottom) {
    for (; y < bottom && y < 720; ++y)
        for (int col = x; col < right && col < 1280; ++col)
            if (y >= 0 && col >= 0) frame[y * 1280 + col] = 0xffffffff;
}

static void draw_text_at(uint32_t *frame, int x, int y, char *text, uint32_t color) {
    struct Vertex { float x, y, z; unsigned char color[4]; };
    static struct Vertex vertices[4096];
    int count = stb_easy_font_print(0, 0, text, NULL, vertices, sizeof(vertices));
    for (int q = 0; q < count; ++q) {
        struct Vertex a = vertices[q * 4], b = vertices[q * 4 + 2];
        int left = x + (int)(a.x * 2), top = y + (int)(a.y * 2);
        int right = x + (int)(b.x * 2), bottom = y + (int)(b.y * 2);
        for (int row = top; row < bottom && row < 720; ++row)
            for (int col = left; col < right && col < 1280; ++col)
                if (row >= 0 && col >= 0) frame[row * 1280 + col] = color;
    }
}

static void draw_text(uint32_t *frame, int y, char *text) { draw_text_at(frame, 40, y, text, 0xffeef6ff); }

static void display(void) {
    if (video < 0 || !frames[0]) return;
    int index = sequence % 2;
    ++sequence;
    uint32_t *frame = frames[index];
    for (int i = 0; i < 1280 * 720; ++i)
        frame[i] = failed ? 0xff501820 : 0xff102840;
    draw_text(frame, 30, PROBE_TITLE " - photograph the last checkpoint");
    unsigned first = line_count > 18 ? line_count - 18 : 0;
    for (unsigned i = first; i < line_count; ++i)
        draw_text(frame, 85 + (int)(i - first) * 30, lines[i % 18]);
    if (sceVideoOutSubmitFlip(video, index, ORBIS_VIDEO_OUT_FLIP_VSYNC, sequence) < 0) {
        video = -1;
        return;
    }
    for (int attempt = 0; attempt < 120; ++attempt) {
        OrbisVideoOutFlipStatus state = {0};
        if (sceVideoOutGetFlipStatus(video, &state) >= 0 && state.flipArg == sequence) return;
        sceKernelUsleep(16000);
    }
    video = -1; // Never reuse a buffer when presentation completion is unknown.
}

static void report(const char *format, ...) {
    // Mono may emit diagnostics from worker threads. Serialize the shared
    // line ring and VideoOut buffers; this path never calls back into Mono.
    while (__atomic_test_and_set(&report_busy, __ATOMIC_ACQUIRE)) sceKernelUsleep(1000);
    char text[768];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text) - 2, format, args);
    va_end(args);
    if (strstr(text, "FAIL")) failed = 1;
    snprintf(lines[line_count++ % 18], sizeof(lines[0]), "%.95s", text);
    strcat(text, "\n");
    sceKernelDebugOutText(0, text);
    if (logfile >= 0) {
        size_t remaining = strlen(text), offset = 0;
        while (remaining) {
            int64_t written = sceKernelWrite(logfile, text + offset, remaining);
            if (written <= 0) break;
            remaining -= (size_t)written;
            offset += (size_t)written;
        }
#ifdef CONSOLE_PLAYER
        if (failed) sceKernelFsync(logfile);
#else
        sceKernelFsync(logfile);
#endif
    }
#ifdef CONSOLE_PLAYER
    if (!player_frontend_active || failed) display();
#else
    display();
#endif
    __atomic_clear(&report_busy, __ATOMIC_RELEASE);
}

static void init_video(void) {
    video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    off_t offset = 0;
    void *memory = NULL;
    if (video < 0 || sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
            16 * 1024 * 1024, 0x200000, 3, &offset) < 0 ||
        sceKernelMapDirectMemory(&memory, 16 * 1024 * 1024, 0x33, 0, offset, 0x200000) < 0) {
        video = -1;
        report("WARN video allocation failed; read native-probe.log");
        return;
    }
    frames[0] = memory;
    frames[1] = frames[0] + 1280 * 720;
    OrbisVideoOutBufferAttribute attr = {0};
    sceVideoOutSetBufferAttribute(&attr, 0x80000000, 1, 0, 1280, 720, 1280);
    void *buffers[] = {frames[0], frames[1]};
    if (sceVideoOutRegisterBuffers(video, 0, buffers, 2, &attr) < 0) video = -1;
}

static int resolve(int handle, const char *name, void **destination) {
    *destination = NULL;
    int result = sceKernelDlsym(handle, name, destination);
    if (result < 0 || !*destination) {
        report("FAIL symbol %s: 0x%08x", name, (unsigned)result);
        return 0;
    }
    return 1;
}

static int load_module(const char *name) {
    char path[320];
    // Only packaged modules use filesystem paths. Firmware modules must be
    // loaded by the system module service within the application's sandbox.
    snprintf(path, sizeof(path), "/app0/sce_module/%s", name);
    report("LOAD %s", path);
    int handle = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, NULL);
    if (handle < 0) report("FAIL load %s: 0x%08x", path, (unsigned)handle);
    return handle;
}

static void *(*image_open)(char *, unsigned, int, int *, int, const char *);

// Replacement for the pinned runtime's SPRX-only image loader. Paths and
// buffers stay valid across image_open; need_copy transfers no host allocation.
static void *load_image(const char *name, int *status, int unused, int refonly) {
    (void)unused;
    if (!name) return NULL;
    report("IMAGE request %.70s", name);
    const char *basename = strrchr(name, '/');
    basename = basename ? basename + 1 : name;
    char path[512];
    snprintf(path, sizeof(path), "%s", name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(path, sizeof(path), "/app0/mono/4.5/%s", basename);
        file = fopen(path, "rb");
    }
    if (!file && strlen(basename) > 4 && !strcmp(basename + strlen(basename) - 4, ".exe")) {
        snprintf(path, sizeof(path), "/app0/mono/4.5/%.*s.dll", (int)strlen(basename) - 4, basename);
        file = fopen(path, "rb");
    }
    if (!file) { report("IMAGE miss %.70s", basename); return NULL; }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length <= 0 || length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return NULL;
    }
    char *bytes = malloc((size_t)length);
    if (!bytes) { fclose(file); return NULL; }
    size_t got = fread(bytes, 1, (size_t)length, file);
    fclose(file);
    int local_status = 0;
    report("IMAGE decode %.55s bytes=%ld", basename, length);
    void *image = got == (size_t)length
        ? image_open(bytes, (unsigned)length, 1, &local_status, refonly, path) : NULL;
    free(bytes);
    report("IMAGE result %.55s status=%d image=%p", basename, local_status, image);
    if (status) *status = local_status;
    return image;
}

static int install_hook(int handle) {
    OrbisKernelModuleInfo info = {0};
    info.size = sizeof(info);
    int rc = sceKernelGetModuleInfo(handle, &info);
    const size_t offset = 0x18cc60;
    // Verified against the SHA-256-pinned SELF's executable segment at build time.
    const unsigned char expected[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28,0x44,0x89,0x4d,0xc4};
    if (rc < 0 || !info.segmentCount || !info.segmentInfo[0].address ||
        info.segmentInfo[0].size < offset + sizeof(expected)) {
        report("FAIL Mono segment info: 0x%08x", (unsigned)rc); return 0;
    }
    unsigned char *target = (unsigned char *)info.segmentInfo[0].address + offset;
    if (memcmp(target, expected, sizeof(expected))) {
        report("FAIL Mono hook signature mismatch"); return 0;
    }
    uintptr_t page = (uintptr_t)target & ~(uintptr_t)0x3fff;
    rc = sceKernelMprotect((void *)page, 0x4000, 7);
    if (rc < 0) { report("FAIL hook protection: 0x%08x", (unsigned)rc); return 0; }
    unsigned char jump[14] = {0xff, 0x25, 0, 0, 0, 0};
    void *replacement = (void *)load_image;
    memcpy(jump + 6, &replacement, sizeof(replacement));
    memcpy(target, jump, sizeof(jump));
    rc = sceKernelMprotect((void *)page, 0x4000, info.segmentInfo[0].prot);
    if (rc < 0) { report("FAIL restoring hook protection: 0x%08x", (unsigned)rc); return 0; }
    return 1;
}

static void *dl_load(const char *name, int flags, char **error, void *user) {
    (void)flags; (void)user;
    if (error) *error = NULL;
    report("NATIVE library request %.70s", name ? name : "(null)");
    if (name && (!strcmp(name, "libkernel") || !strcmp(name, "libkernel.sprx")))
        return (void *)(intptr_t)kernel_handle;
    return NULL;
}
static void *dl_symbol(void *handle, const char *name, char **error, void *user) {
    (void)user;
    if (error) *error = NULL;
    void *symbol = NULL;
    if (sceKernelDlsym((int)(intptr_t)handle, name, &symbol) < 0) return NULL;
    return symbol;
}
static void *dl_close(void *handle, void *user) { (void)handle; (void)user; return NULL; }
static void mono_log(const char *domain, const char *level, const char *message, int fatal, void *user) {
    (void)domain; (void)user;
    report("%s Mono %s: %.550s", fatal ? "FAIL" : "LOG", level ? level : "", message ? message : "");
}
static int managed_pass;
static void managed_report(const char *text) {
    if (text && !strcmp(text, "RESULT PASS")) managed_pass = 1;
    report("%s", text ? text : "(null)");
}

// ABI reference: CTurt/PS4-SDK libPS4/source/jit.c. Check every operation;
// this probes one JIT allocation route, not every allocator Mono may use.
static int check_jit(void) {
    int (*create)(int, size_t, int, int *);
    int (*alias)(int, int, int *);
    if (!resolve(kernel_handle, "sceKernelJitCreateSharedMemory", (void **)&create) ||
        !resolve(kernel_handle, "sceKernelJitCreateAliasOfSharedMemory", (void **)&alias)) return 0;
    int executable = -1, writable = -1, rc, okay = 0;
    void *rx = NULL, *rw = NULL;
    int mapped_rx = 0, mapped_rw = 0;
    const size_t size = 0x4000;
    report("JIT create shared memory");
    rc = create(0, size, PROT_READ | PROT_WRITE | PROT_EXEC, &executable);
    if (rc != 0 || executable < 0) { report("FAIL JIT create: 0x%08x", (unsigned)rc); goto done; }
    report("JIT create writable alias");
    rc = alias(executable, PROT_READ | PROT_WRITE, &writable);
    if (rc != 0 || writable < 0) { report("FAIL JIT alias: 0x%08x", (unsigned)rc); goto done; }
    report("JIT map executable view");
    rc = sceKernelMmap(NULL, size, PROT_READ | PROT_EXEC, MAP_SHARED, executable, 0, &rx);
    if (rc != 0) { report("FAIL JIT RX map: 0x%08x", (unsigned)rc); goto done; }
    mapped_rx = 1;
    report("JIT map writable view");
    rc = sceKernelMmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, writable, 0, &rw);
    if (rc != 0) { report("FAIL JIT RW map: 0x%08x", (unsigned)rc); goto done; }
    mapped_rw = 1;
    const unsigned char code[] = {0xb8, 42, 0, 0, 0, 0xc3}; // mov eax,42; ret
    memcpy(rw, code, sizeof(code));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (memcmp(rx, code, sizeof(code))) { report("FAIL JIT alias bytes differ"); goto done; }
    report("JIT execute return-42 function");
    int value = ((int (*)(void))rx)();
    if (value != 42) { report("FAIL JIT returned %d", value); goto done; }
    report("PASS native JIT returned 42");
    okay = 1;
done:
    if (mapped_rw && sceKernelMunmap(rw, size) < 0) { report("FAIL JIT RW unmap"); okay = 0; }
    if (mapped_rx && sceKernelMunmap(rx, size) < 0) { report("FAIL JIT RX unmap"); okay = 0; }
    if (writable >= 0) sceKernelClose(writable);
    if (executable >= 0) sceKernelClose(executable);
    return okay;
}

static void mono_print(const char *text, int stdout_stream) {
    report("MONO %s: %.550s", stdout_stream ? "stdout" : "stderr", text ? text : "");
}

static void enable_mono_diagnostics(int mono) {
    void (*set_level)(const char *) = NULL;
    void (*set_print)(void (*)(const char *, int)) = NULL;
    void (*set_error)(void (*)(const char *, int)) = NULL;
    if (sceKernelDlsym(mono, "mono_trace_set_level_string", (void **)&set_level) >= 0 && set_level)
        set_level(
#ifdef CONSOLE_PLAYER
            "warning"
#else
            "debug"
#endif
        );
    if (sceKernelDlsym(mono, "mono_trace_set_print_handler", (void **)&set_print) >= 0 && set_print)
        set_print(mono_print);
    if (sceKernelDlsym(mono, "mono_trace_set_printerr_handler", (void **)&set_error) >= 0 && set_error)
        set_error(mono_print);
    report("Mono diagnostics: level=%d stdout=%d stderr=%d", set_level != NULL, set_print != NULL, set_error != NULL);
}

#if defined(NATIVE_CREDENTIAL_PROBE) || defined(MONO_CREDENTIAL_PROBE)
#include "credential-probe.h"
#endif

static int run_mono(void) {
    managed_pass = 0;
    report("02 kernel found; loading Mono dependencies");
    const struct {
        const char *name;
        enum OrbisSysModuleInternal id;
    } dependencies[] = {
        {"libSceIpmi", ORBIS_SYSMODULE_INTERNAL_IPMI},
        {"libSceNet", ORBIS_SYSMODULE_INTERNAL_NET},
        {"libSceSystemService", ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE},
        {"libSceUserService", ORBIS_SYSMODULE_INTERNAL_USER_SERVICE}
    };
    for (unsigned i = 0; i < sizeof(dependencies) / sizeof(dependencies[0]); ++i) {
        report("SYSMODULE %s id=0x%08x", dependencies[i].name, (unsigned)dependencies[i].id);
        uint32_t result = sceSysmoduleLoadModuleInternal(dependencies[i].id);
        if (result != 0) {
            report("FAIL sysmodule %s: 0x%08x", dependencies[i].name, result);
            return 0;
        }
        report("OK %s", dependencies[i].name);
    }
    report("03 loading libmonosgen-2.0.prx");
    int mono = load_module("libmonosgen-2.0.prx");
    if (mono < 0) return 0;
    void (*set_dirs)(const char *, const char *);
    void *(*jit_init)(const char *);
    void (*jit_cleanup)(void *);
    void *(*assembly_load)(void *, const char *, int *, int);
    void *(*class_from_name)(void *, const char *, const char *);
    void *(*get_method)(void *, const char *, int);
    void *(*invoke)(void *, void *, void **, void **);
    void (*trace_handler)(void (*)(const char *, const char *, const char *, int, void *), void *);
    void *(*fallback)(void *(*)(const char *, int, char **, void *),
        void *(*)(void *, const char *, char **, void *), void *(*)(void *, void *), void *);
    void (*add_call)(const char *, const void *);
#define RESOLVE(name, variable) if (!resolve(mono, name, (void **)&variable)) return 0
    RESOLVE("mono_set_dirs", set_dirs);
    RESOLVE("mono_jit_init", jit_init);
    RESOLVE("mono_jit_cleanup", jit_cleanup);
    RESOLVE("mono_image_open_from_data_with_name", image_open);
    RESOLVE("mono_assembly_load_from_full", assembly_load);
    RESOLVE("mono_class_from_name", class_from_name);
    RESOLVE("mono_class_get_method_from_name", get_method);
    RESOLVE("mono_runtime_invoke", invoke);
    RESOLVE("mono_trace_set_log_handler", trace_handler);
    RESOLVE("mono_dl_fallback_register", fallback);
    RESOLVE("mono_add_internal_call", add_call);
    report("04 exports resolved; verifying DLL hook");
    if (!install_hook(mono)) return 0;
    trace_handler(mono_log, NULL);
    enable_mono_diagnostics(mono);
    fallback(dl_load, dl_symbol, dl_close, NULL);
    set_dirs("/app0", "/app0/mono");
    report("05 entering mono_jit_init; native JIT preflight passed");
    void *domain = jit_init("eutherdrive-probe");
    if (!domain) { report("FAIL Mono domain"); return 0; }
    int success = 0;
    report("05b Mono domain initialized");
    add_call("Orbis.Program::NativeReport", (const void *)managed_report);
#ifdef GB_PLAYER
    add_call("Orbis.Program::NativeInput", (const void *)gb_input);
    add_call("Orbis.Program::NativePresent", (const void *)gb_present);
    add_call("Orbis.Program::NativePreview", (const void *)console_preview);
    add_call("Orbis.Program::NativePresentFrame", (const void *)console_present);
    add_call("Orbis.Program::NativeClose", (const void *)gb_close);
    add_call("Orbis.Program::NativeMenu", (const void *)gb_menu);
    add_call("Orbis.Program::NativeAudio", (const void *)gb_audio);
    add_call("Orbis.Program::NativeAudioStop", (const void *)gb_audio_close);
    add_call("Orbis.Program::NativeMute", (const void *)gb_set_mute);
#endif
    report("06 loading main.exe");
    int status = 0;
    void *image = load_image("/app0/main.exe", &status, 0, 0);
    if (!image || !assembly_load(image, "/app0/main.exe", &status, 0)) {
        report("FAIL managed assembly: %d", status); goto cleanup;
    }
    void *klass = class_from_name(image, "Orbis", "Program");
    void *method = klass ? get_method(klass, "Main", 0) : NULL;
    if (!method) { report("FAIL managed entry point"); goto cleanup; }
    report("07 invoking managed tests");
    void *exception = NULL;
    invoke(method, NULL, NULL, &exception);
    if (exception) { report("FAIL unhandled managed exception"); goto cleanup; }
    report("08 managed entry returned; inspect RESULT above");
    success = managed_pass && !failed;
cleanup:
    report("09 shutting down Mono before restoring process rights");
    jit_cleanup(domain);
    report("09b Mono cleanup returned");
    return success && !failed;
}

static void run(void) {
    report("01 native entry; locating existing libkernel");
    OrbisKernelModule handles[256];
    size_t count = 0;
    int rc = sceKernelGetModuleList(handles, 256, &count);
    if (rc < 0 || count > 256) { report("FAIL module list: 0x%08x", (unsigned)rc); return; }
    for (size_t i = 0; i < count; ++i) {
        void *symbol = NULL;
        if (sceKernelDlsym(handles[i], "sceKernelGetProcessTime", &symbol) >= 0 && symbol) {
            kernel_handle = handles[i]; break;
        }
    }
    if (kernel_handle < 0) { report("FAIL existing libkernel not found"); return; }
#ifdef NATIVE_CREDENTIAL_PROBE
    credential_probe(NULL);
#elif defined(MONO_CREDENTIAL_PROBE)
    credential_probe(run_mono);
#else
    if (check_jit()) run_mono();
#endif
}

int main(void) {
    sceKernelMkdir("/data/eutherdrive-ps4", 0777);
#ifdef NATIVE_CREDENTIAL_PROBE
    logfile = sceKernelOpen("/data/eutherdrive-ps4/jit-rights-probe.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
#else
    logfile = sceKernelOpen("/data/eutherdrive-ps4/native-probe.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
#endif
    report(PROBE_TITLE " - native log start");
    init_video();
    if (logfile < 0) report("WARN native log open failed: 0x%08x", (unsigned)logfile);
    run();
#ifdef CONSOLE_PLAYER
    player_frontend_active = 0;
#endif
    report("Probe stopped. Photograph screen; close with PS button.");
    if (logfile >= 0) sceKernelFsync(logfile);
    for (;;) sceKernelUsleep(1000000);
}
