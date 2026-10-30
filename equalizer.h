#ifndef XMP_EQUALIZER_H
#define XMP_EQUALIZER_H

#include <stdint.h>

#define XMP_EQ_BANDS 10

typedef struct {
    double b0, b1, b2, a1, a2;
    double z1[2], z2[2];
} XmpEqBiquad;

/* The audio callback owns the filter state. UI changes must be made while
 * holding SDL_LockAudioDevice() so the targets are updated atomically. */
typedef struct {
    double target_db[XMP_EQ_BANDS];
    double smooth_db[XMP_EQ_BANDS];
    double target_preamp_db;
    double target_preamp_gain;
    double current_preamp_gain;
    double wet;
    int enabled;
    XmpEqBiquad filters[XMP_EQ_BANDS];
} XmpEqualizer;

extern const double xmp_eq_frequencies[XMP_EQ_BANDS];

void xmp_eq_init(XmpEqualizer *eq);
void xmp_eq_set_band(XmpEqualizer *eq, int band, double db);
void xmp_eq_set_preamp(XmpEqualizer *eq, double db);
void xmp_eq_set_enabled(XmpEqualizer *eq, int enabled);
void xmp_eq_reset_history(XmpEqualizer *eq);
void xmp_eq_process_s16(XmpEqualizer *eq, int16_t *pcm, int frames);

/* Desired magnitude response, in dB, for the GTK/Cairo curve. */
double xmp_eq_response_db(const XmpEqualizer *eq, double frequency_hz);

#endif
