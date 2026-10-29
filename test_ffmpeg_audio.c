#include "ffmpeg_audio.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s audio-file\n", argv[0]);
        return 2;
    }

    char error[256];
    FfmpegAudio *audio = ffmpeg_audio_open(argv[1], error, sizeof(error));

    if (!audio) {
        fprintf(stderr, "Decoder open failed: %s\n", error);
        return 1;
    }

    printf("Opened: %s\n", argv[1]);
    printf("Reported duration: %" PRId64 " ms\n",
           ffmpeg_audio_duration_ms(audio));
    printf("Output format: 44100 Hz, stereo, signed 16-bit PCM\n");

    int16_t buffer[1024 * 2];
    int64_t total_frames = 0;
    int64_t nonzero_samples = 0;
    int peak = 0;

    for (;;) {
        int frames = ffmpeg_audio_read(audio, buffer, 1024);

        if (frames < 0) {
            fprintf(stderr, "Decoding failed.\n");
            ffmpeg_audio_close(audio);
            return 1;
        }

        if (frames == 0)
            break;

        total_frames += frames;

        for (int i = 0; i < frames * 2; i++) {
            int value = buffer[i];
            int magnitude = value < 0 ? -value : value;
            if (magnitude > peak)
                peak = magnitude;
            if (value != 0)
                nonzero_samples++;
        }
    }

    printf("Decoded frames: %" PRId64 "\n", total_frames);
    printf("Decoded duration: %.3f seconds\n",
           (double)total_frames / 44100.0);
    printf("Nonzero samples: %" PRId64 "\n", nonzero_samples);
    printf("Peak sample magnitude: %d\n", peak);

    ffmpeg_audio_close(audio);

    if (total_frames == 0 || nonzero_samples == 0) {
        fprintf(stderr, "Test failed: no audible PCM samples were produced.\n");
        return 1;
    }

    puts("PASS: decoded audio and produced nonzero PCM samples.");
    return 0;
}
