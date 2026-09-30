// SPDX-License-Identifier: MIT
// Streaming 44100 -> 48000 Hz stereo interpolation. No PS4 dependency.
typedef struct { int primed, phase; float left, right; } GbResampler;
static int gb_resample(GbResampler *s, const int16_t *pcm, int samples,
                       int (*emit)(float, float, void *), void *context) {
    if (samples < 0 || (samples & 1) || (!pcm && samples)) return 0;
    for (int i = 0; i < samples; i += 2) {
        float left = pcm[i] / 32768.0f, right = pcm[i + 1] / 32768.0f;
        if (!s->primed) { s->left = left; s->right = right; s->primed = 1; continue; }
        while (s->phase < 48000) {
            float f = s->phase / 48000.0f;
            if (!emit(s->left + (left - s->left) * f,
                      s->right + (right - s->right) * f, context)) return 0;
            s->phase += 44100;
        }
        s->phase -= 48000;
        s->left = left; s->right = right;
    }
    return 1;
}
