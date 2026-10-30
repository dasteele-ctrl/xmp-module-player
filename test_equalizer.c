/* Dependency-free tests for XMP's 44.1 kHz stereo EQ engine. */
#include "equalizer.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846

static double measure_1khz(double band_db, double preamp_db, int enabled)
{
    XmpEqualizer eq;
    xmp_eq_init(&eq);
    xmp_eq_set_band(&eq, 5, band_db);
    xmp_eq_set_preamp(&eq, preamp_db);
    xmp_eq_set_enabled(&eq, enabled);

    double sum = 0;
    int samples = 0;
    for (int block = 0; block < 140; ++block) {
        int16_t pcm[512 * 2];
        for (int frame = 0; frame < 512; ++frame) {
            double phase = 2 * PI * 1000.0 * (block * 512 + frame) / 44100.0;
            pcm[frame * 2] = (int16_t)(4000 * sin(phase));
            pcm[frame * 2 + 1] = 0;
        }
        xmp_eq_process_s16(&eq, pcm, 512);
        for (int frame = 0; frame < 512; ++frame) {
            assert(pcm[frame * 2 + 1] == 0); /* Independent stereo states. */
            if (block > 100) {
                double v = pcm[frame * 2];
                sum += v * v;
                samples++;
            }
        }
    }
    return sqrt(sum / samples);
}

int main(void)
{
    XmpEqualizer eq;
    xmp_eq_init(&eq);
    int16_t original[1024], test[1024];
    for (int i = 0; i < 1024; ++i)
        original[i] = (int16_t)(i * 61 - 25000);
    memcpy(test, original, sizeof(test));
    xmp_eq_process_s16(&eq, test, 512);
    assert(memcmp(test, original, sizeof(test)) == 0); /* Bypass is exact. */

    double flat = measure_1khz(0, 0, 1);
    double boosted = measure_1khz(9, 0, 1);
    double cut = measure_1khz(-9, 0, 1);
    double preamp = measure_1khz(0, -6, 1);
    double bypass = measure_1khz(9, 0, 0);
    assert(boosted > flat * 2.0);
    assert(cut < flat * 0.5);
    assert(preamp > flat * 0.47 && preamp < flat * 0.54);
    assert(fabs(bypass / flat - 1.0) < 0.01);

    xmp_eq_init(&eq);
    xmp_eq_set_enabled(&eq, 1);
    xmp_eq_set_band(&eq, 3, 20.0);
    assert(eq.target_db[3] == 12.0); /* Slider limits enforced in DSP. */
    xmp_eq_set_band(&eq, 3, -20.0);
    assert(eq.target_db[3] == -12.0);
    printf("EQ tests passed (flat %.0f, +9dB %.0f, -9dB %.0f, -6dB preamp %.0f)\n",
           flat, boosted, cut, preamp);
    return 0;
}
