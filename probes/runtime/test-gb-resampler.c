#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "gb-resampler.h"
typedef struct { float data[50000][2]; int count; } Capture;
static Capture a, b;
static int collect(float l, float r, void *ctx) {
    Capture *c = ctx;
    assert(c->count < 50000 && isfinite(l) && isfinite(r));
    assert(l >= -1 && l <= 1 && r >= -1 && r <= 1);
    c->data[c->count][0] = l; c->data[c->count++][1] = r;
    return 1;
}
int main(void) {
    static int16_t pcm[44101 * 2];
    for (int i = 0; i < 44101; ++i) { pcm[i*2] = (i % 1000) * 30; pcm[i*2+1] = -pcm[i*2]; }
    GbResampler x = {0}, y = {0};
    assert(gb_resample(&x, pcm, 88202, collect, &a));
    for (int i = 0; i < 88202; i += 2) assert(gb_resample(&y, pcm+i, 2, collect, &b));
    assert(a.count == 48000 && b.count == a.count);
    assert(!memcmp(a.data, b.data, (size_t)a.count * sizeof(a.data[0])));
    for (int i = 0; i < a.count; ++i) assert(a.data[i][0] == -a.data[i][1]);
    assert(!gb_resample(&x, pcm, 3, collect, &a));
    assert(!gb_resample(&x, NULL, 2, collect, &a));
    puts("PASS resampler: 44100->48000, chunk continuity, stereo, bounds");
}
