#ifndef ZX_AUDIO_H
#define ZX_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZxAudio ZxAudio;

/* ZXTune's native renderer: no external player and no temporary WAV file. */
ZxAudio *zx_audio_open(const char *filename, char *error, unsigned long error_size);
int zx_audio_read(ZxAudio *audio, int16_t *output, int max_frames);
int zx_audio_seek(ZxAudio *audio, int64_t milliseconds);
int64_t zx_audio_duration_ms(const ZxAudio *audio);
void zx_audio_close(ZxAudio *audio);

#ifdef __cplusplus
}
#endif

#endif
