#ifndef FFMPEG_AUDIO_H
#define FFMPEG_AUDIO_H

#include <stdint.h>

typedef struct FfmpegAudio FfmpegAudio;

/* Open a media file containing an audio stream. */
FfmpegAudio *ffmpeg_audio_open(const char *filename,
                               char *error_text,
                               unsigned long error_text_size);

/* Read up to max_frames of interleaved stereo S16 PCM at 44100 Hz.
   Returns frames read, 0 at end of file, or -1 on error. */
int ffmpeg_audio_read(FfmpegAudio *audio, int16_t *output,
                      int max_frames);

/* Duration in milliseconds, or 0 if unavailable. */
int ffmpeg_audio_seek(FfmpegAudio *audio, int64_t position_ms);
int64_t ffmpeg_audio_duration_ms(const FfmpegAudio *audio);


/* Release all decoder resources. */
void ffmpeg_audio_close(FfmpegAudio *audio);

#endif
