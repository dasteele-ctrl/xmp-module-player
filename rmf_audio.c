/* NeoBAE backend for the player's isolated PCM helper (zx_audio.h ABI).
 * Each helper owns one mixer, including during concurrent playback/export. */
#include "zx_audio.h"
#include "NeoBAE.h"
#include "BAE_API.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char rmf_bank[], rmf_bank_end[];

struct ZxAudio {
    BAEMixer mixer;
    BAESong song;
    int64_t duration_ms;
    int16_t *slice;
    int frames, offset;
};

void zx_audio_close(ZxAudio *a)
{
    if (!a) return;
    if (a->song) BAESong_Delete(a->song);
    if (a->mixer) BAEMixer_Delete(a->mixer);
    BAE_Cleanup();
    free(a->slice);
    free(a);
}

ZxAudio *zx_audio_open(const char *path, char *error, unsigned long size)
{
    const char *step = "initializing NeoBAE";
    ZxAudio *a = calloc(1, sizeof(*a));
    if (!a || BAE_Setup() != 0) goto fail;
    a->mixer = BAEMixer_New();
    if (!a->mixer || BAEMixer_Open(a->mixer, BAE_RATE_44K, BAE_LINEAR_INTERPOLATION,
        BAE_USE_16 | BAE_USE_STEREO, 64, 8, 64, FALSE) != BAE_NO_ERROR) goto fail;
    BAEMixer_MakeCurrent(a->mixer);
    BAEBankToken token;
    const char *bank = getenv("MODULEZ_RMF_BANK");
    step = "loading the soundbank";
    BAEResult result = bank && *bank
        ? BAEMixer_AddBankFromFile(a->mixer, (BAEPathName)bank, &token)
        : BAEMixer_AddBankFromMemory(a->mixer, rmf_bank, rmf_bank_end - rmf_bank, &token);
    if (result != BAE_NO_ERROR) goto fail;
    step = "loading the RMF song";
    a->song = BAESong_New(a->mixer);
    if (!a->song || BAESong_LoadRmfFromFile(a->song, (BAEPathName)path, 0, TRUE) != BAE_NO_ERROR) goto fail;
    uint64_t us = 0;
    BAESong_GetMicrosecondLength64(a->song, &us);
    a->duration_ms = us / 1000;
    step = "starting the song";
    if (BAESong_Preroll(a->song) != BAE_NO_ERROR || BAESong_Start(a->song, 0) != BAE_NO_ERROR) goto fail;
    BAESong_SetLoops(a->song, 0);
    a->frames = BAE_GetMaxSamplePerSlice();
    if (a->frames <= 0) goto fail;
    a->slice = calloc((size_t)a->frames * 2, sizeof(int16_t));
    if (!a->slice) goto fail;
    a->offset = a->frames;
    return a;
fail:
    snprintf(error, size, "Beatnik RMF: failed while %s", step);
    zx_audio_close(a);
    return NULL;
}

int zx_audio_read(ZxAudio *a, int16_t *out, int frames)
{
    int copied = 0;
    while (copied < frames) {
        if (a->offset == a->frames) {
            BAE_BOOL done = FALSE;
            if (BAESong_IsDone(a->song, &done) != BAE_NO_ERROR) return -1;
            if (done) break;
            BAE_BuildMixerSlice(NULL, a->slice, a->frames * 4, a->frames);
            a->offset = 0;
        }
        int count = a->frames - a->offset;
        if (count > frames - copied) count = frames - copied;
        memcpy(out + copied * 2, a->slice + a->offset * 2, (size_t)count * 4);
        a->offset += count;
        copied += count;
    }
    return copied;
}

int zx_audio_seek(ZxAudio *a, int64_t ms)
{
    if (ms < 0 || ms > INT64_MAX / 1000 ||
        BAESong_SetMicrosecondPosition64(a->song, (uint64_t)ms * 1000) != BAE_NO_ERROR) return -1;
    a->offset = a->frames;
    return 0;
}

int64_t zx_audio_duration_ms(const ZxAudio *a) { return a->duration_ms; }
