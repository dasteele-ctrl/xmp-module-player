#ifndef FFMPEG_AUDIO_H
#define FFMPEG_AUDIO_H

#include <stdint.h>

typedef struct FfmpegAudio FfmpegAudio;

/* Open a media file containing an audio stream. */
FfmpegAudio *ffmpeg_audio_open(const char *filename,
                               char *error_text,
                               unsigned long error_text_size);

/* For player playback, return NULL and set has_video when a genuine video
   track is present. The audio-only API above also serves export callers. */
FfmpegAudio *ffmpeg_open_playback(const char *filename, int *has_video,
                                 char *error_text, unsigned long error_text_size);

/* The interrupt callback may run during open/read; opaque must remain valid
   until close. Return nonzero to cancel. Call from a worker thread. */
typedef int (*FfmpegAudioInterrupt)(void *opaque);
FfmpegAudio *ffmpeg_audio_open_stream(const char *url,
                                     FfmpegAudioInterrupt interrupt,
                                     void *opaque, char *error_text,
                                     unsigned long error_text_size);

/* Copy the latest ICY title. Call only from the decoder-owning thread. */
void ffmpeg_audio_stream_title(FfmpegAudio *audio, char *title,
                               unsigned long title_size);

/* Read up to max_frames of interleaved stereo S16 PCM at 44100 Hz.
   Returns frames read, 0 at end of file, or -1 on error. */
int ffmpeg_audio_read(FfmpegAudio *audio, int16_t *output,
                      int max_frames);

/* Duration in milliseconds, or 0 if unavailable. */
int ffmpeg_audio_seek(FfmpegAudio *audio, int64_t position_ms);
int64_t ffmpeg_audio_duration_ms(const FfmpegAudio *audio);


const char *ffmpeg_audio_tag(const FfmpegAudio *audio, const char *key);

/* Release all decoder resources. */
void ffmpeg_audio_close(FfmpegAudio *audio);

#endif
