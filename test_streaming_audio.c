#include "ffmpeg_audio.h"
#include <libavutil/time.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

static int expired(void *opaque)
{
    return av_gettime_relative() >= *(const int64_t *)opaque;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "Usage: %s URL\n", argv[0]); return 2; }
    int64_t deadline = av_gettime_relative() + 20000000;
    char error[512], title[512];
    FfmpegAudio *audio = ffmpeg_audio_open_stream(argv[1], expired, &deadline,
                                                error, sizeof(error));
    if (!audio) { fprintf(stderr, "Open failed: %s\n", error); return 1; }
    int16_t pcm[2048];
    int64_t frames = 0, nonzero = 0;
    while (frames < 44100 * 5 && !expired(&deadline)) {
        int got = ffmpeg_audio_read(audio, pcm, 1024);
        if (got <= 0) break;
        frames += got;
        for (int i = 0; i < got * 2; ++i) nonzero += pcm[i] != 0;
    }
    ffmpeg_audio_stream_title(audio, title, sizeof(title));
    printf("Title: %s\nDecoded frames: %" PRId64 "\nNonzero samples: %" PRId64 "\n",
           title, frames, nonzero);
    int ok = frames >= 44100 * 5 && nonzero > 0 &&
             ffmpeg_audio_duration_ms(audio) == 0 && ffmpeg_audio_seek(audio, 0) < 0;
    ffmpeg_audio_close(audio);
    puts(ok ? "PASS: live PCM decoded; duration unknown; seeking disabled." : "FAIL");
    return ok ? 0 : 1;
}
