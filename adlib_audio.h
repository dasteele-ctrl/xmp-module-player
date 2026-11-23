#ifndef ADLIB_AUDIO_H
#define ADLIB_AUDIO_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct AdlibAudio AdlibAudio;
extern const char *const adlib_extensions[];
int adlib_is_path(const char *path);
int adlib_is_fm_s3m(const char *path);
AdlibAudio *adlib_audio_open(const char *path);
void adlib_audio_close(AdlibAudio *audio);
int adlib_audio_read(AdlibAudio *audio, int16_t *pcm, int frames);
int adlib_audio_seek(AdlibAudio *audio, int64_t ms);
int64_t adlib_audio_duration_ms(const AdlibAudio *audio);

#ifdef __cplusplus
}
#endif
#endif
