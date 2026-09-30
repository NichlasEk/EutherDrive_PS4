// Host fault/queue tests, not evidence of PS4 hardware output.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#define GB_AUDIO_TEST
#define ORBIS_AUDIO_OUT_ERROR_ALREADY_INIT 0x80260002u
#define ORBIS_AUDIO_OUT_PORT_TYPE_MAIN 0
#define ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO 4
typedef pthread_t OrbisPthread;
static int fail_open, fail_output, opened, closed, nonzero;
static int sceAudioOutInit(void) { return 0; }
static int sceAudioOutOpen(int user, int type, int index, unsigned frames, unsigned rate, unsigned format) {
    assert(user == 255 && type == 0 && index == 0 && frames == 512 && rate == 48000 && format == 4);
    if (fail_open) return -1;
    ++opened; return 1;
}
static int sceAudioOutClose(int port) { assert(port == 1); ++closed; return 0; }
static int sceAudioOutOutput(int port, const void *data) {
    assert(port == 1);
    if (!data) return 0;
    if (fail_output) return -123;
    const float *p = data;
    for (int i = 0; i < 1024; ++i) { assert(p[i] >= -0.5f && p[i] <= 0.5f); if (p[i]) ++nonzero; }
    usleep(1000); return 0;
}
static void sceKernelUsleep(unsigned us) { usleep(us); }
static int scePthreadCreate(OrbisPthread *t, const void *a, void *(*f)(void *), void *arg, const char *name) {
    (void)a; (void)name; return pthread_create(t, NULL, f, arg);
}
static int scePthreadJoin(OrbisPthread t, void **out) { return pthread_join(t, out); }
static void report(const char *text, ...) { (void)text; }
#include "gb-audio.h"
int main(void) {
    static int16_t pcm[4096];
    for (int i = 0; i < 4096; ++i) pcm[i] = i & 1 ? -12000 : 12000;
    assert(!gb_audio(NULL, 2) && !gb_audio(pcm, 3));
    fail_open = 1; assert(!gb_audio(pcm, 4096)); gb_audio_close(); fail_open = 0;
    for (int round = 0; round < 3; ++round) {
        nonzero = 0;
        gb_set_mute(round == 1);
        for (int chunk = 0; chunk < 20; ++chunk) assert(gb_audio(pcm, 4096));
        usleep(10000);
        gb_audio_close();
        assert(gb_audio_blocks > 0 && (round == 1 ? nonzero == 0 : nonzero > 0));
        assert(opened == closed && gb_audio_running == 0);
    }
    fail_output = 1;
    assert(gb_audio(pcm, 4096));
    usleep(20000);
    assert(!gb_audio(pcm, 4096));
    gb_audio_close();
    assert(gb_audio_error == -123 && opened == closed);
    puts("PASS audio queue: wrap, gain, mute, reopen, open/output errors, shutdown");
    return 0;
}
