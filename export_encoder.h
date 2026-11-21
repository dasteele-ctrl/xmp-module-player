#ifndef EXPORT_ENCODER_H
#define EXPORT_ENCODER_H
#include <glib.h>
#include <stdint.h>

typedef struct {
    const char *label, *extension, *codec, *container;
    int rate, bitrate;
} BatchFormat;

extern const BatchFormat batch_formats[];
extern const guint batch_format_count;
typedef struct ExportEncoder ExportEncoder;

ExportEncoder *export_encoder_open(const char *path, const BatchFormat *format,
                                   const char *title, const char *artist, GError **error);
gboolean export_encoder_write(ExportEncoder *enc, const int16_t *pcm, int frames, GError **error);
gboolean export_encoder_finish(ExportEncoder *enc, GError **error);
void export_encoder_free(ExportEncoder *enc);
#endif
