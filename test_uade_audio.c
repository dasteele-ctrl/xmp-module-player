#include "uade_audio.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* Headless real-runtime check: reopen, decode ten seconds, then clean up. */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /path/to/mdat.song\n", argv[0]);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    for (int pass = 0; pass < 3; ++pass) {
        char error[512] = "";
        UadeAudio *a = uade_audio_open(argv[1], error, sizeof(error));
        if (!a) { fprintf(stderr, "%s\n", error); return 1; }
        printf("Pass %d: %s (%s)\n", pass + 1,
               uade_audio_title(a), uade_audio_format(a));
        int16_t pcm[1024 * 2];
        int frames = 0, nonzero = 0, got = 0;
        while (frames < 44100 * 10 &&
               (got = uade_audio_read(a, pcm, 1024)) > 0) {
            for (int i = 0; i < got * 2; ++i)
                if (pcm[i]) nonzero = 1;
            frames += got;
        }
        if (got < 0 || !frames || !nonzero) {
            fprintf(stderr, "Decode failed or produced only silence.\n");
            uade_audio_close(a);
            return 1;
        }
        const int64_t targets[] = {5000, 0};
        for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
            if (uade_audio_seek(a, targets[i]) < 0 ||
                uade_audio_read(a, pcm, 1024) <= 0) {
                fprintf(stderr, "Seek to %lld ms failed.\n", (long long)targets[i]);
                uade_audio_close(a);
                return 1;
            }
        }
        uade_audio_close(a);
        printf("Decoded %d stereo frames; cleanup completed.\n", frames);
    }
    return 0;
}
