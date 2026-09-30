// SPDX-License-Identifier: MIT
#pragma once
// Copyright (c) 2026 Nichlas Eklöf
#ifndef GB_AUDIO_TEST
#include <orbis/AudioOut.h>
#endif
#include "gb-resampler.h"
#define GB_AUDIO_CAPACITY 8192u
static float gb_audio_ring[GB_AUDIO_CAPACITY][2];
static unsigned gb_audio_read, gb_audio_write;
static int gb_audio_port = -1, gb_audio_running, gb_audio_stop, gb_audio_error;
static OrbisPthread gb_audio_thread;
static GbResampler gb_resampler;
static unsigned gb_audio_underruns, gb_audio_blocks;
static int gb_muted;
static void report(const char *, ...);

static void *gb_audio_worker(void *unused) {
    (void)unused;
    float buffers[2][512 * 2] __attribute__((aligned(64))) = {{0}};
    unsigned next = 0;
    // Small initial cushion; never hold a Mono pointer on this thread.
    while (!__atomic_load_n(&gb_audio_stop, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&gb_audio_write, __ATOMIC_ACQUIRE) < 2048) sceKernelUsleep(1000);
    while (!__atomic_load_n(&gb_audio_stop, __ATOMIC_ACQUIRE)) {
        unsigned read = __atomic_load_n(&gb_audio_read, __ATOMIC_RELAXED);
        unsigned write = __atomic_load_n(&gb_audio_write, __ATOMIC_ACQUIRE);
        unsigned available = write - read;
        if (available > 512) available = 512;
        memset(buffers[next], 0, sizeof(buffers[next]));
        for (unsigned i = 0; i < available; ++i) {
            buffers[next][i * 2] = gb_audio_ring[(read + i) % GB_AUDIO_CAPACITY][0];
            buffers[next][i * 2 + 1] = gb_audio_ring[(read + i) % GB_AUDIO_CAPACITY][1];
        }
        __atomic_store_n(&gb_audio_read, read + available, __ATOMIC_RELEASE);
        if (available < 512) ++gb_audio_underruns;
        int rc = sceAudioOutOutput(gb_audio_port, buffers[next]);
        if (rc < 0) { __atomic_store_n(&gb_audio_error, rc, __ATOMIC_RELEASE); break; }
        ++gb_audio_blocks;
        next ^= 1;
    }
    int rc = sceAudioOutOutput(gb_audio_port, NULL);
    if (rc < 0) __atomic_store_n(&gb_audio_error, rc, __ATOMIC_RELEASE);
    return NULL;
}

static void gb_audio_close(void) {
    if (gb_audio_running) {
        __atomic_store_n(&gb_audio_stop, 1, __ATOMIC_RELEASE);
        scePthreadJoin(gb_audio_thread, NULL);
        gb_audio_running = 0;
        report("Audio blocks=%u underruns=%u error=%08x", gb_audio_blocks,
               gb_audio_underruns, (unsigned)gb_audio_error);
    }
    if (gb_audio_port > 0) sceAudioOutClose(gb_audio_port);
    gb_audio_port = -1;
}

static int gb_audio_open(void) {
    int rc = sceAudioOutInit();
    if (rc < 0 && (uint32_t)rc != ORBIS_AUDIO_OUT_ERROR_ALREADY_INIT) {
        report("WARN AudioOut init: %08x", (unsigned)rc); return 0;
    }
    gb_audio_port = sceAudioOutOpen(0xff, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, 512,
                                  48000, ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO);
    if (gb_audio_port <= 0) { report("WARN AudioOut open: %08x", (unsigned)gb_audio_port); return 0; }
    gb_audio_read = gb_audio_write = 0;
    gb_audio_stop = gb_audio_error = 0;
    gb_audio_blocks = gb_audio_underruns = 0;
    memset(&gb_resampler, 0, sizeof(gb_resampler));
    rc = scePthreadCreate(&gb_audio_thread, NULL, gb_audio_worker, NULL, "euther-audio");
    if (rc) { report("WARN audio thread: %08x", (unsigned)rc); gb_audio_close(); return 0; }
    gb_audio_running = 1;
    report("Audio ready: 44100 -> 48000 Hz stereo, 50 percent gain");
    return 1;
}

static int gb_audio_emit(float left, float right, void *unused) {
    (void)unused;
    unsigned write = __atomic_load_n(&gb_audio_write, __ATOMIC_RELAXED);
    for (int wait = 0; write - __atomic_load_n(&gb_audio_read, __ATOMIC_ACQUIRE) >= GB_AUDIO_CAPACITY; ++wait) {
        if (wait >= 250 || __atomic_load_n(&gb_audio_error, __ATOMIC_ACQUIRE)) return 0;
        sceKernelUsleep(1000);
    }
    float gain = gb_muted ? 0.0f : 0.5f;
    gb_audio_ring[write % GB_AUDIO_CAPACITY][0] = left * gain;
    gb_audio_ring[write % GB_AUDIO_CAPACITY][1] = right * gain;
    __atomic_store_n(&gb_audio_write, write + 1, __ATOMIC_RELEASE);
    return 1;
}

static int gb_audio(const int16_t *pcm, int samples) {
    if (samples < 0 || samples > 16384 || (samples & 1) || (!pcm && samples)) return 0;
    if (!samples) return 1;
    if (!gb_audio_running && !gb_audio_open()) return 0;
    if (__atomic_load_n(&gb_audio_error, __ATOMIC_ACQUIRE)) return 0;
    return gb_resample(&gb_resampler, pcm, samples, gb_audio_emit, NULL);
}
static void gb_set_mute(int mute) { gb_muted = mute != 0; }
