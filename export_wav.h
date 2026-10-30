#ifndef EXPORT_WAV_H
#define EXPORT_WAV_H

#include <glib.h>
#include <stdint.h>

typedef enum {
    EXPORT_WAV_XMP,
    EXPORT_WAV_FFMPEG,
    EXPORT_WAV_SID,
    EXPORT_WAV_ZXTUNE,
    EXPORT_WAV_MIDI
} ExportWavFormat;

typedef struct {
    const char *source;
    const char *destination;
    ExportWavFormat format;
    unsigned int sid_seconds; /* SID files lack a guaranteed song length. */
    unsigned int sid_subtune; /* Export the currently selected SID subtune. */
    const char *soundfont;    /* MIDI export uses the currently selected font. */
} ExportWavRequest;

/* Runs off the GTK thread, with independent decoder instances.
 * progress: 0..100 or -1 if the duration is unknown.
 * cancel: set to 1 by the UI to abort; destination remains unchanged.
 */
int export_wav_render(const ExportWavRequest *request,
                      volatile gint *progress, const volatile gint *cancel,
                      char *error, unsigned long error_size);

#endif
