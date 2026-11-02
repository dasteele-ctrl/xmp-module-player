#ifndef XMP_UADE_AUDIO_H
#define XMP_UADE_AUDIO_H
#include <stdint.h>
#include <stddef.h>
typedef struct UadeAudio UadeAudio;
/* Calls on one instance must be serialized; close only after joining decoder. */
UadeAudio *uade_audio_open(const char *path, char *error, size_t error_size);
int uade_audio_read(UadeAudio *audio, int16_t *pcm, int frames);
/* Queues an absolute song-relative seek; subsequent reads drive emulation. */
int uade_audio_seek(UadeAudio *audio, int64_t milliseconds);
int64_t uade_audio_duration_ms(const UadeAudio *audio);
const char *uade_audio_title(const UadeAudio *audio);
const char *uade_audio_format(const UadeAudio *audio);
void uade_audio_close(UadeAudio *audio);
#endif
