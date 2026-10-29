#define _POSIX_C_SOURCE 200809L
#include "export_wav.h"
#include "ffmpeg_audio.h"
#include "sid_audio.h"
#include "zx_audio.h"

#include <glib/gstdio.h>
#include <xmp.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define EXPORT_RATE 44100u
#define EXPORT_FRAMES_PER_BLOCK 4096
#define EXPORT_BYTES_PER_FRAME 4u
#define EXPORT_FALLBACK_SECONDS 180u

static void set_error(char *error, unsigned long size, const char *message)
{
    if (error && size)
        g_snprintf(error, size, "%s", message ? message : "Export failed");
}

static void set_errno_error(char *error, unsigned long size, const char *task)
{
    if (error && size)
        g_snprintf(error, size, "%s: %s", task, g_strerror(errno));
}

static void le16(unsigned char *where, uint16_t value)
{
    where[0] = (unsigned char)value;
    where[1] = (unsigned char)(value >> 8);
}

static void le32(unsigned char *where, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        where[i] = (unsigned char)(value >> (8 * i));
}

static int write_wav_header(FILE *stream, uint32_t pcm_bytes)
{
    unsigned char header[44] = {0};
    memcpy(header + 0, "RIFF", 4);
    le32(header + 4, pcm_bytes + 36);
    memcpy(header + 8, "WAVEfmt ", 8);
    le32(header + 16, 16);                 /* PCM format chunk size */
    le16(header + 20, 1);                  /* uncompressed integer PCM */
    le16(header + 22, 2);                  /* stereo */
    le32(header + 24, EXPORT_RATE);
    le32(header + 28, EXPORT_RATE * EXPORT_BYTES_PER_FRAME);
    le16(header + 32, EXPORT_BYTES_PER_FRAME);
    le16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    le32(header + 40, pcm_bytes);
    return fwrite(header, 1, sizeof(header), stream) == sizeof(header);
}

static void update_progress(volatile gint *progress, uint64_t written,
                            uint64_t expected)
{
    if (!progress) return;
    if (expected) {
        uint64_t percent = (written * 100u) / expected;
        if (percent > 99) percent = 99; /* 100 only after finalizing WAV. */
        g_atomic_int_set(progress, (gint)percent);
    } else {
        g_atomic_int_set(progress, -1);
    }
}

int export_wav_render(const ExportWavRequest *request,
                      volatile gint *progress, const volatile gint *cancel,
                      char *error, unsigned long error_size)
{
    if (progress) g_atomic_int_set(progress, 0);
    set_error(error, error_size, "Export failed");

    if (!request || !request->source || !*request->source ||
        !request->destination || !*request->destination) {
        set_error(error, error_size, "No source or destination was selected");
        return 0;
    }

    xmp_context xmp = NULL;
    int xmp_loaded = 0;
    int xmp_started = 0;
    FfmpegAudio *ffmpeg = NULL;
    SidAudio *sid = NULL;
    ZxAudio *zx = NULL;
    uint64_t frame_limit = 0;
    uint64_t frames_written = 0;
    int16_t pcm[EXPORT_FRAMES_PER_BLOCK * 2];
    FILE *wav = NULL;
    int success = 0;
    gchar *temporary = NULL;

    if (cancel && g_atomic_int_get(cancel)) {
        set_error(error, error_size, "Export cancelled");
        goto cleanup;
    }

    switch (request->format) {
    case EXPORT_WAV_XMP: {
        xmp = xmp_create_context();
        if (!xmp || xmp_load_module(xmp, request->source) < 0) {
            set_error(error, error_size, "Could not load tracker module for export");
            goto cleanup;
        }
        xmp_loaded = 1;
        if (xmp_start_player(xmp, EXPORT_RATE, 0) < 0) {
            set_error(error, error_size, "Could not start tracker module renderer");
            goto cleanup;
        }
        xmp_started = 1;
        struct xmp_frame_info info;
        xmp_get_frame_info(xmp, &info);
        const uint64_t module_ms = info.total_time > 0
            ? (uint64_t)info.total_time
            : (uint64_t)EXPORT_FALLBACK_SECONDS * 1000u;
        frame_limit = module_ms * EXPORT_RATE / 1000u;
        break;
    }
    case EXPORT_WAV_FFMPEG:
        ffmpeg = ffmpeg_audio_open(request->source, error, error_size);
        if (!ffmpeg) goto cleanup;
        if (ffmpeg_audio_duration_ms(ffmpeg) > 0)
            frame_limit = (uint64_t)ffmpeg_audio_duration_ms(ffmpeg) * EXPORT_RATE / 1000u;
        break;
    case EXPORT_WAV_SID:
        if (request->sid_seconds == 0 || request->sid_seconds > 3600) {
            set_error(error, error_size, "Please choose a SID export length of 1–3600 seconds");
            goto cleanup;
        }
        sid = sid_audio_open(request->source, error, error_size);
        if (!sid) goto cleanup;
        if (request->sid_subtune &&
            sid_audio_select_song(sid, request->sid_subtune) <= 0) {
            set_error(error, error_size, "Could not select the current SID subtune");
            goto cleanup;
        }
        frame_limit = (uint64_t)request->sid_seconds * EXPORT_RATE;
        break;
    case EXPORT_WAV_ZXTUNE:
        zx = zx_audio_open(request->source, error, error_size);
        if (!zx) goto cleanup;
        frame_limit = zx_audio_duration_ms(zx) > 0
            ? (uint64_t)zx_audio_duration_ms(zx) * EXPORT_RATE / 1000u
            : (uint64_t)EXPORT_FALLBACK_SECONDS * EXPORT_RATE;
        break;
    default:
        set_error(error, error_size, "Unsupported playback format");
        goto cleanup;
    }

    /* Create output alongside the destination and replace it only after a
     * successful export; a failed/cancelled render preserves prior files. */
    temporary = g_strdup_printf("%s.tmp.XXXXXX", request->destination);
    int temporary_fd = g_mkstemp(temporary);
    if (temporary_fd < 0) {
        set_errno_error(error, error_size, "Could not create temporary WAV");
        goto cleanup;
    }
    wav = fdopen(temporary_fd, "wb+");
    if (!wav) {
        set_errno_error(error, error_size, "Could not open temporary WAV");
        close(temporary_fd);
        goto cleanup;
    }

    if (!write_wav_header(wav, 0)) {
        set_errno_error(error, error_size, "Could not write WAV header");
        goto cleanup;
    }

    update_progress(progress, 0, frame_limit);
    while (1) {
        if (cancel && g_atomic_int_get(cancel)) {
            set_error(error, error_size, "Export cancelled");
            goto cleanup;
        }
        if (frame_limit && frames_written >= frame_limit)
            break;

        int frames = EXPORT_FRAMES_PER_BLOCK;
        if (frame_limit && frame_limit - frames_written < (uint64_t)frames)
            frames = (int)(frame_limit - frames_written);

        int got = -1;
        if (xmp) {
            int result = xmp_play_buffer(xmp, pcm,
                                        frames * EXPORT_BYTES_PER_FRAME, 0);
            if (result == -XMP_END)
                break;
            if (result >= 0)
                got = frames;
        } else if (ffmpeg) {
            got = ffmpeg_audio_read(ffmpeg, pcm, frames);
        } else if (sid) {
            got = sid_audio_read(sid, pcm, frames);
        } else if (zx) {
            got = zx_audio_read(zx, pcm, frames);
        }

        if (got < 0) {
            set_error(error, error_size, "Audio decoder reported an error during export");
            goto cleanup;
        }
        if (got == 0)
            break;

        /* Regular RIFF/WAV uses 32-bit chunk lengths. */
        if (frames_written + (uint64_t)got > (UINT32_MAX - 36u) / EXPORT_BYTES_PER_FRAME) {
            set_error(error, error_size, "WAV would exceed the 4 GB RIFF limit");
            goto cleanup;
        }

        if (fwrite(pcm, EXPORT_BYTES_PER_FRAME, (size_t)got, wav) != (size_t)got) {
            set_errno_error(error, error_size, "Could not write WAV audio");
            goto cleanup;
        }
        frames_written += (uint64_t)got;
        update_progress(progress, frames_written, frame_limit);
    }

    if (!frames_written) {
        set_error(error, error_size, "The decoder produced no audio");
        goto cleanup;
    }
    if (fseek(wav, 0, SEEK_SET) != 0 ||
        !write_wav_header(wav, (uint32_t)(frames_written * EXPORT_BYTES_PER_FRAME)) ||
        fflush(wav) != 0) {
        set_errno_error(error, error_size, "Could not finalize WAV header");
        goto cleanup;
    }
    if (fclose(wav) != 0) {
        wav = NULL;
        set_errno_error(error, error_size, "Could not finish writing WAV");
        goto cleanup;
    }
    wav = NULL;

    if (cancel && g_atomic_int_get(cancel)) {
        set_error(error, error_size, "Export cancelled");
        goto cleanup;
    }
    if (g_rename(temporary, request->destination) != 0) {
        set_errno_error(error, error_size, "Could not save the exported WAV");
        goto cleanup;
    }
    success = 1;
    if (progress) g_atomic_int_set(progress, 100);
    set_error(error, error_size, "");

cleanup:
    if (wav) fclose(wav);
    if (!success && temporary) g_unlink(temporary);
    g_free(temporary);
    if (zx) zx_audio_close(zx);
    if (sid) sid_audio_close(sid);
    if (ffmpeg) ffmpeg_audio_close(ffmpeg);
    if (xmp_started) xmp_end_player(xmp);
    if (xmp_loaded) xmp_release_module(xmp);
    if (xmp) xmp_free_context(xmp);
    return success;
}
