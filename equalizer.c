#include "equalizer.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define EQ_RATE 44100.0
#define EQ_PI 3.14159265358979323846
#define EQ_STEP 128

const double xmp_eq_frequencies[XMP_EQ_BANDS] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
};

static double clamp(double value, double low, double high)
{
    return value < low ? low : value > high ? high : value;
}

/* RBJ peaking EQ, one octave wide. Transposed direct-form II states are
 * independent for left and right. All coefficients are a0-normalized. */
static void biquad_coefficients(XmpEqBiquad *f, double frequency, double db)
{
    const double omega = 2.0 * EQ_PI * frequency / EQ_RATE;
    const double sn = sin(omega);
    const double cs = cos(omega);
    const double a = pow(10.0, db / 40.0);
    const double alpha = sn * sinh(log(2.0) / 2.0 * omega / sn);
    const double inv_a0 = 1.0 / (1.0 + alpha / a);

    f->b0 = (1.0 + alpha * a) * inv_a0;
    f->b1 = (-2.0 * cs) * inv_a0;
    f->b2 = (1.0 - alpha * a) * inv_a0;
    f->a1 = (-2.0 * cs) * inv_a0;
    f->a2 = (1.0 - alpha / a) * inv_a0;
}

void xmp_eq_init(XmpEqualizer *eq)
{
    memset(eq, 0, sizeof(*eq));
    eq->target_preamp_gain = 1.0;
    eq->current_preamp_gain = 1.0;
    for (int i = 0; i < XMP_EQ_BANDS; ++i)
        biquad_coefficients(&eq->filters[i], xmp_eq_frequencies[i], 0);
}

void xmp_eq_set_band(XmpEqualizer *eq, int band, double db)
{
    if (band >= 0 && band < XMP_EQ_BANDS && isfinite(db))
        eq->target_db[band] = clamp(db, -12.0, 12.0);
}

void xmp_eq_set_preamp(XmpEqualizer *eq, double db)
{
    if (!isfinite(db))
        return;
    eq->target_preamp_db = clamp(db, -12.0, 12.0);
    eq->target_preamp_gain = pow(10.0, eq->target_preamp_db / 20.0);
}

void xmp_eq_set_enabled(XmpEqualizer *eq, int enabled)
{
    eq->enabled = !!enabled;
}

void xmp_eq_reset_history(XmpEqualizer *eq)
{
    for (int i = 0; i < XMP_EQ_BANDS; ++i) {
        memset(eq->filters[i].z1, 0, sizeof(eq->filters[i].z1));
        memset(eq->filters[i].z2, 0, sizeof(eq->filters[i].z2));
    }
}

static double biquad_sample(XmpEqBiquad *f, double x, int channel)
{
    const double y = f->b0 * x + f->z1[channel];
    f->z1[channel] = f->b1 * x - f->a1 * y + f->z2[channel];
    f->z2[channel] = f->b2 * x - f->a2 * y;
    return y;
}

static int16_t to_s16(double value)
{
    if (value > 32767.0) return 32767;
    if (value < -32768.0) return -32768;
    return (int16_t)lrint(value);
}

void xmp_eq_process_s16(XmpEqualizer *eq, int16_t *pcm, int frames)
{
    if (!eq || !pcm || frames <= 0)
        return;

    for (int offset = 0; offset < frames; offset += EQ_STEP) {
        int count = frames - offset;
        if (count > EQ_STEP) count = EQ_STEP;

        /* Smooth changed band gains before updating IIR coefficients. A
         * 128-frame step is about 3 ms; continuous filter history avoids
         * wiping notes each time a slider is moved. */
        for (int b = 0; b < XMP_EQ_BANDS; ++b) {
            double delta = eq->target_db[b] - eq->smooth_db[b];
            if (fabs(delta) > 0.0001) {
                eq->smooth_db[b] += delta * 0.18;
                biquad_coefficients(&eq->filters[b],
                                    xmp_eq_frequencies[b], eq->smooth_db[b]);
            }
        }

        for (int i = offset; i < offset + count; ++i) {
            const double target_wet = eq->enabled ? 1.0 : 0.0;
            eq->wet += (target_wet - eq->wet) * 0.0016;
            eq->current_preamp_gain +=
                (eq->target_preamp_gain - eq->current_preamp_gain) * 0.0016;

            for (int ch = 0; ch < 2; ++ch) {
                int index = i * 2 + ch;
                const double dry = pcm[index];
                double filtered = dry;
                for (int b = 0; b < XMP_EQ_BANDS; ++b)
                    filtered = biquad_sample(&eq->filters[b], filtered, ch);

                double mixed = dry + (filtered * eq->current_preamp_gain - dry)
                                         * eq->wet;
                pcm[index] = to_s16(mixed);
            }
        }

        /* Keep long silent periods from leaving subnormal filter states. */
        for (int b = 0; b < XMP_EQ_BANDS; ++b)
            for (int ch = 0; ch < 2; ++ch) {
                if (fabs(eq->filters[b].z1[ch]) < 1e-20)
                    eq->filters[b].z1[ch] = 0;
                if (fabs(eq->filters[b].z2[ch]) < 1e-20)
                    eq->filters[b].z2[ch] = 0;
            }
    }
}

double xmp_eq_response_db(const XmpEqualizer *eq, double frequency_hz)
{
    if (!eq || frequency_hz <= 0 || frequency_hz >= EQ_RATE / 2)
        return 0;

    const double omega = 2 * EQ_PI * frequency_hz / EQ_RATE;
    const double c1 = cos(omega), s1 = sin(omega);
    const double c2 = cos(2 * omega), s2 = sin(2 * omega);
    double total_db = eq->target_preamp_db;

    for (int b = 0; b < XMP_EQ_BANDS; ++b) {
        XmpEqBiquad f = {0};
        biquad_coefficients(&f, xmp_eq_frequencies[b], eq->target_db[b]);
        const double numerator_real = f.b0 + f.b1 * c1 + f.b2 * c2;
        const double numerator_imag = -f.b1 * s1 - f.b2 * s2;
        const double denom_real = 1 + f.a1 * c1 + f.a2 * c2;
        const double denom_imag = -f.a1 * s1 - f.a2 * s2;
        const double numerator = numerator_real * numerator_real +
                                 numerator_imag * numerator_imag;
        const double denominator = denom_real * denom_real +
                                   denom_imag * denom_imag;
        if (numerator > 0 && denominator > 0)
            total_db += 10.0 * log10(numerator / denominator);
    }

    return eq->enabled ? total_db : 0;
}
