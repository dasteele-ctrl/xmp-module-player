#include "uade_audio.h"
#include <uade/uade.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct UadeAudio {
    struct uade_state *state;
    char title[256];
    char format[256];
};

UadeAudio *uade_audio_open(const char *path, char *error, size_t size)
{
    UadeAudio *a = calloc(1, sizeof(*a));
    struct uade_config *config = uade_new_config();
    if (!a || !config) {
        free(a);
        free(config);
        if (size) snprintf(error, size, "Could not allocate UADE state.");
        return NULL;
    }
    uade_config_set_option(config, UC_FREQUENCY, "44100");
    /* Overrides also allow a future AppImage to use its bundled runtime. */
    const char *base = getenv("XMP_UADE_BASE_DIR");
    const char *core = getenv("XMP_UADE_CORE");
    if (base && *base) uade_config_set_option(config, UC_BASE_DIR, base);
    if (core && *core) uade_config_set_option(config, UC_UADECORE_FILE, core);
    a->state = uade_new_state(config);
    free(config);
    if (!a->state || !uade_is_our_file(path, a->state) ||
        uade_play(path, -1, a->state) != 1) {
        if (size) snprintf(error, size, "UADE could not play this file; check its companion files and UADE runtime.");
        uade_audio_close(a);
        return NULL;
    }
    if (uade_get_sampling_rate(a->state) != 44100) {
        if (size) snprintf(error, size, "UADE did not select 44100 Hz.");
        uade_audio_close(a);
        return NULL;
    }
    const struct uade_song_info *info = uade_get_song_info(a->state);
    if (info) {
        snprintf(a->title, sizeof(a->title), "%s", info->modulename);
        snprintf(a->format, sizeof(a->format), "%s", info->formatname);
    }
    return a;
}

int uade_audio_read(UadeAudio *a, int16_t *pcm, int frames)
{
    if (!a || !pcm || frames <= 0) return -1;
    ssize_t bytes = uade_read(pcm, (size_t)frames * UADE_BYTES_PER_FRAME, a->state);
    struct uade_notification note;
    while (uade_read_notification(&note, a->state))
        uade_cleanup_notification(&note);
    if (bytes < 0 || bytes % UADE_BYTES_PER_FRAME) return -1;
    return (int)(bytes / UADE_BYTES_PER_FRAME);
}

const char *uade_audio_title(const UadeAudio *a) { return a ? a->title : ""; }
const char *uade_audio_format(const UadeAudio *a) { return a ? a->format : ""; }
void uade_audio_close(UadeAudio *a)
{
    if (!a) return;
    /* cleanup includes stop; do not issue a second stop on a failed state. */
    if (a->state) uade_cleanup_state(a->state);
    free(a);
}
