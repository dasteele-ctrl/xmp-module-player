#ifndef SID_AUDIO_H
#define SID_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SidAudio SidAudio;

SidAudio *sid_audio_open(const char *filename,
                         char *error_text,
                         unsigned long error_text_size);

int sid_audio_read(SidAudio *audio, int16_t *output, int max_frames);

int sid_audio_select_song(SidAudio *audio, unsigned int song_number);

unsigned int sid_audio_song_count(const SidAudio *audio);
unsigned int sid_audio_current_song(const SidAudio *audio);

const char *sid_audio_title(const SidAudio *audio);
const char *sid_audio_author(const SidAudio *audio);
const char *sid_audio_release_info(const SidAudio *audio);
const char *sid_audio_error(const SidAudio *audio);

void sid_audio_close(SidAudio *audio);

#ifdef __cplusplus
}
#endif

#endif
