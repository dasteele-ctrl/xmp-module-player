#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <SDL.h>
#include <xmp.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include "ffmpeg_audio.h"
#include "sid_audio.h"
#include "midi_audio.h"
#include "equalizer.h"
#include "hvsc_download.h"
#ifdef ENABLE_WAV_EXPORT
#include "export_wav.h"
#endif
#ifdef ENABLE_ZXTUNE_NATIVE
#include "zx_audio.h"
#endif

#define AUDIO_RATE 44100
#define SCOPE_ROW_HEIGHT 76
#define SCOPE_MARGIN 8

typedef enum {
    PLAYBACK_NONE,
    PLAYBACK_MODULE,
    PLAYBACK_FFMPEG,
    PLAYBACK_SID,
    PLAYBACK_MIDI,
    PLAYBACK_ZX
} PlaybackMode;

static PlaybackMode playback_mode = PLAYBACK_NONE;
static xmp_context ctx;
static SDL_AudioDeviceID audio_device;
static int module_loaded = 0;
static int player_started = 0;
static int playing = 0;
static int stopped = 0;
static int audio_ended = 0;
static int updating_seek = 0;
static int master_volume = 100;
static XmpEqualizer equalizer;

/* ZX tunes are converted by zxtune123 and played through the existing
 * FFmpeg / SDL pipeline. No additional audio device or decoder thread. */
typedef struct {
    GSubprocess *process;
    char *directory;
    char *wav_path;
    char *source_path;
    unsigned generation;
    int autoplay;
} ZxConversion;
static ZxConversion *zx_pending;
static unsigned zx_generation;
static char *zx_active_directory;
static char *zx_active_wav;
static char *zx_active_source;
static int zx_autoplay_request;

static GtkWidget *window;
static GtkWidget *title_label;
static GtkWidget *format_label;
static GtkWidget *status_label;
static GtkWidget *time_label;
static GtkWidget *seek_scale;
static GtkWidget *volume_scale;
static GtkWidget *play_button;
static GtkWidget *channels_label;
static GtkWidget *details_label;
static GtkWidget *scope_area;
static GtkWidget *pattern_area;
static GtkWidget *sample_list;
static GtkWidget *sample_details_label;
static GtkWidget *sample_waveform_area;
static GtkWidget *module_comments_view;

static GtkWidget *visualizer_area;
static GtkWidget *eq_graph_area;
static GtkWidget *eq_band_scales[XMP_EQ_BANDS];
static GtkWidget *eq_preamp_scale;
static GtkWidget *eq_preset_combo;
static GtkWidget *eq_enabled_switch;
static guint eq_save_timer;
static gboolean eq_updating_ui;
static int eq_preset_index;

#define VIZ_BANDS 32
#define VIZ_FFT_SIZE 1024
#define VIZ_RING_SIZE 8192
#define AUDIO_BUFFER_FRAMES (AUDIO_RATE * 2)

static int16_t viz_ring[VIZ_RING_SIZE];

/* Shared PCM buffer: decoder thread produces; SDL callback consumes. */
static int16_t audio_ring[AUDIO_BUFFER_FRAMES * 2];
static unsigned int audio_ring_read_pos;
static unsigned int audio_ring_write_pos;
static SDL_Thread *decoder_thread;
static SDL_atomic_t decoder_stop;
static FfmpegAudio *ffmpeg_audio;
static SidAudio *sid_audio;
static MidiAudio *midi_audio;
static gchar *selected_soundfont;
static GtkWidget *soundfont_label;
static GtkListStore *soundfont_store;
static GtkWidget *soundfont_view;
static gchar *favorite_soundfont;

enum {
    SF_NAME,
    SF_FORMAT,
    SF_PATH,
    SF_STATE,
    SF_COLUMNS
};
#ifdef ENABLE_ZXTUNE_NATIVE
static ZxAudio *zx_audio;
#endif
static SDL_atomic_t ffmpeg_decode_finished;
static SDL_atomic_t ffmpeg_decode_error;
static int64_t ffmpeg_position_frames;
static int64_t ffmpeg_duration_ms;
static SDL_atomic_t ffmpeg_seek_requested;
static int64_t ffmpeg_seek_target_ms;
static SDL_atomic_t ffmpeg_seek_generation;
static unsigned int viz_write_pos;
static double viz_levels[VIZ_BANDS];
static double viz_peaks[VIZ_BANDS];
static double viz_spectrum[VIZ_BANDS];
static int selected_sample = 0;
#ifdef ENABLE_WAV_EXPORT
typedef struct {
    ExportWavRequest request;
    gchar *source;
    gchar *destination;
    GtkWidget *dialog;
    GtkWidget *progress_bar;
    GThread *thread;
    guint progress_timer;
    volatile gint progress;
    volatile gint cancel;
    volatile gint finished;
    int succeeded;
    char error[512];
} WavExportTask;
static WavExportTask *active_wav_export;
#endif
static int decoder_thread_fn(void *unused)
{
    (void)unused;

    int16_t chunk[4096 * 2];

    while (!SDL_AtomicGet(&decoder_stop)) {
        if (SDL_AtomicGet(&ffmpeg_seek_requested)) {
            SDL_LockAudioDevice(audio_device);
            int64_t target_ms = ffmpeg_seek_target_ms;
            SDL_AtomicSet(&ffmpeg_seek_requested, 0);
            SDL_UnlockAudioDevice(audio_device);

            int seek_result;
#ifdef ENABLE_ZXTUNE_NATIVE
            if (zx_audio)
                seek_result = zx_audio_seek(zx_audio, target_ms);
            else
#endif
                seek_result = ffmpeg_audio_seek(ffmpeg_audio, target_ms);
            if (seek_result < 0) {
                SDL_AtomicSet(&ffmpeg_decode_error, 1);
                SDL_AtomicSet(&ffmpeg_decode_finished, 1);
                break;
            }

            SDL_LockAudioDevice(audio_device);
            audio_ring_read_pos = 0;
            audio_ring_write_pos = 0;
            ffmpeg_position_frames = target_ms * AUDIO_RATE / 1000;
            SDL_UnlockAudioDevice(audio_device);

            SDL_AtomicSet(&ffmpeg_decode_finished, 0);
            SDL_AtomicSet(&ffmpeg_decode_error, 0);
        }

        SDL_LockAudioDevice(audio_device);
        unsigned int read_pos = audio_ring_read_pos;
        unsigned int write_pos = audio_ring_write_pos;
        unsigned int used =
            (write_pos + AUDIO_BUFFER_FRAMES - read_pos) %
            AUDIO_BUFFER_FRAMES;
        unsigned int free_frames = AUDIO_BUFFER_FRAMES - used - 1;
        SDL_UnlockAudioDevice(audio_device);

        if (free_frames == 0) {
            SDL_Delay(5);
            continue;
        }

        int generation = SDL_AtomicGet(&ffmpeg_seek_generation);
        int request = free_frames > 4096 ? 4096 : (int)free_frames;
        int got;
#ifdef ENABLE_ZXTUNE_NATIVE
        if (zx_audio)
            got = zx_audio_read(zx_audio, chunk, request);
        else
#endif
            got = ffmpeg_audio_read(ffmpeg_audio, chunk, request);

        if (generation != SDL_AtomicGet(&ffmpeg_seek_generation))
            continue;

        if (got < 0) {
            SDL_AtomicSet(&ffmpeg_decode_error, 1);
            SDL_AtomicSet(&ffmpeg_decode_finished, 1);
            break;
        }

        if (got == 0) {
            SDL_AtomicSet(&ffmpeg_decode_finished, 1);
            break;
        }

        SDL_LockAudioDevice(audio_device);

        if (generation == SDL_AtomicGet(&ffmpeg_seek_generation) &&
            !SDL_AtomicGet(&ffmpeg_seek_requested)) {
            for (int i = 0; i < got; i++) {
                unsigned int slot = audio_ring_write_pos;
                audio_ring[slot * 2] = chunk[i * 2];
                audio_ring[slot * 2 + 1] = chunk[i * 2 + 1];
                audio_ring_write_pos = (slot + 1) % AUDIO_BUFFER_FRAMES;
            }
        }

        SDL_UnlockAudioDevice(audio_device);
    }

    return 0;
}

static void stop_ffmpeg_decoder(void)
{
    /* Stop the shared PCM producer (FFmpeg or native ZXTune) before freeing it. */
    SDL_AtomicSet(&decoder_stop, 1);

    if (decoder_thread) {
        SDL_WaitThread(decoder_thread, NULL);
        decoder_thread = NULL;
    }

    if (ffmpeg_audio) {
        ffmpeg_audio_close(ffmpeg_audio);
        ffmpeg_audio = NULL;
    }
#ifdef ENABLE_ZXTUNE_NATIVE
    if (zx_audio) {
        zx_audio_close(zx_audio);
        zx_audio = NULL;
    }
#endif

    if (audio_device) {
        SDL_PauseAudioDevice(audio_device, 1);
        SDL_LockAudioDevice(audio_device);
        audio_ring_read_pos = 0;
        audio_ring_write_pos = 0;
        SDL_UnlockAudioDevice(audio_device);
    }

    SDL_AtomicSet(&ffmpeg_decode_finished, 0);
    SDL_AtomicSet(&ffmpeg_decode_error, 0);
    SDL_AtomicSet(&ffmpeg_seek_requested, 0);
    ffmpeg_position_frames = 0;
    ffmpeg_duration_ms = 0;
}

static void zx_free_conversion(ZxConversion *job)
{
    if (!job)
        return;
    if (job->wav_path)
        g_unlink(job->wav_path);
    if (job->directory)
        g_rmdir(job->directory);
    g_clear_object(&job->process);
    g_free(job->directory);
    g_free(job->wav_path);
    g_free(job->source_path);
    g_free(job);
}

static void zx_cancel_pending(void)
{
    ++zx_generation;
    if (zx_pending) {
        /* The async completion callback owns the cancelled job. */
        g_subprocess_force_exit(zx_pending->process);
        if (zx_pending->wav_path)
            g_unlink(zx_pending->wav_path);
        if (zx_pending->directory)
            g_rmdir(zx_pending->directory);
        zx_pending = NULL;
    }
}

static void zx_remove_active(void)
{
    /* Always stop_ffmpeg_decoder() before unlinking its backing WAV. */
    if (zx_active_wav)
        g_unlink(zx_active_wav);
    if (zx_active_directory)
        g_rmdir(zx_active_directory);
    g_clear_pointer(&zx_active_wav, g_free);
    g_clear_pointer(&zx_active_directory, g_free);
    g_clear_pointer(&zx_active_source, g_free);
}

static gboolean zx_is_music_path(const char *filename)
{
    static const char *extensions[] = {
        ".ay", ".ayc", ".asc", ".chi", ".dmm", ".dst", ".ftc",
        ".gtr", ".psc", ".psg", ".pt1", ".pt2", ".pt3",
        ".sqt", ".st1", ".st3", ".stc", ".stp", ".str",
        ".tfc", ".tfd", ".tfe", ".vtx", ".ym", NULL
    };
    const char *extension = strrchr(filename, '.');
    if (!extension)
        return FALSE;
    for (int i = 0; extensions[i]; ++i)
        if (g_ascii_strcasecmp(extension, extensions[i]) == 0)
            return TRUE;
    return FALSE;
}

static char current_file[4096] = "";
static GtkListStore *playlist_store;
static GtkWidget *playlist_view;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    memset(stream, 0, len);

    if (!playing || audio_ended)
        return;

    if (playback_mode == PLAYBACK_MODULE) {
        if (!module_loaded || !player_started)
            return;

        int rc = xmp_play_buffer(ctx, stream, len, 0);

        if (rc == -XMP_END) {
            audio_ended = 1;
            playing = 0;
        }
    } else if (playback_mode == PLAYBACK_FFMPEG ||
               playback_mode == PLAYBACK_ZX) {
        int16_t *out = (int16_t *)stream;
        int requested_frames = len / (int)(2 * sizeof(int16_t));
        int copied_frames = 0;

        while (copied_frames < requested_frames &&
               audio_ring_read_pos != audio_ring_write_pos) {
            unsigned int slot = audio_ring_read_pos;
            out[copied_frames * 2] = audio_ring[slot * 2];
            out[copied_frames * 2 + 1] = audio_ring[slot * 2 + 1];
            audio_ring_read_pos = (slot + 1) % AUDIO_BUFFER_FRAMES;
            copied_frames++;
        }

        ffmpeg_position_frames += copied_frames;

        if (copied_frames < requested_frames &&
            SDL_AtomicGet(&ffmpeg_decode_finished) &&
            audio_ring_read_pos == audio_ring_write_pos) {
            audio_ended = 1;
            playing = 0;
        }
    } else if (playback_mode == PLAYBACK_SID) {
        int requested_frames = len / (int)(2 * sizeof(int16_t));
        int got = sid_audio ? sid_audio_read(sid_audio,
                                              (int16_t *)stream,
                                              requested_frames) : 0;
        if (got <= 0) {
            audio_ended = 1;
            playing = 0;
        }
    } else if (playback_mode == PLAYBACK_MIDI) {
        int requested_frames = len / (int)(2 * sizeof(int16_t));
        int got = midi_audio ? midi_audio_read(midi_audio,
                                               (int16_t *)stream,
                                               requested_frames) : 0;
        if (got <= 0) {
            audio_ended = 1;
            playing = 0;
        }
    } else {
        return;
    }

    /* One shared post-processing path for every playback engine. */
    xmp_eq_process_s16(&equalizer, (int16_t *)stream,
                       len / (int)(2 * sizeof(int16_t)));

    /* Apply master volume after the EQ. */
    int16_t *volume_pcm = (int16_t *)stream;
    int pcm_samples = len / (int)sizeof(int16_t);
    if (master_volume < 100) {
        for (int i = 0; i < pcm_samples; i++)
            volume_pcm[i] = (int16_t)(((int32_t)volume_pcm[i] * master_volume) / 100);
    }

    /* Capture mono samples for the visualizer; audio is S16 stereo. */
    const int16_t *pcm = (const int16_t *)stream;
    int sample_count = len / (int)(2 * sizeof(int16_t));

    for (int i = 0; i < sample_count; i++) {
        int mono = ((int)pcm[i * 2] + (int)pcm[i * 2 + 1]) / 2;
        viz_ring[viz_write_pos] = (int16_t)mono;
        viz_write_pos = (viz_write_pos + 1) % VIZ_RING_SIZE;
    }
}

static void on_volume_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    int volume = (int)gtk_range_get_value(range);
    if (audio_device) SDL_LockAudioDevice(audio_device);
    master_volume = volume;
    if (audio_device) SDL_UnlockAudioDevice(audio_device);
}

static void update_play_button(void)
{
    gtk_button_set_label(GTK_BUTTON(play_button),
                         playing ? "❚❚  Pause" : "▶  Play");
}

static void set_status(const char *message)
{
    gtk_label_set_text(GTK_LABEL(status_label), message);
}

static void update_sample_details(int index)
{
    if (!module_loaded || index < 0)
        return;

    struct xmp_module *mod = NULL;
    struct xmp_module_info mi;
    xmp_get_module_info(ctx, &mi);
    mod = mi.mod;

    if (!mod || index >= mod->smp)
        return;

    struct xmp_sample *smp = &mod->xxs[index];
    char text[768];
    char flags[256] = "";

    if (smp->flg & XMP_SAMPLE_LOOP)
        strcat(flags, "Looping, ");
    if (smp->flg & XMP_SAMPLE_LOOP_BIDIR)
        strcat(flags, "Bidirectional loop, ");
    if (smp->flg & XMP_SAMPLE_LOOP_REVERSE)
        strcat(flags, "Reverse, ");
    if (smp->flg & XMP_SAMPLE_16BIT)
        strcat(flags, "16-bit, ");
    if (smp->flg & XMP_SAMPLE_STEREO)
        strcat(flags, "Stereo, ");

    if (flags[0]) {
        flags[strlen(flags) - 2] = '\0';
    } else {
        strcpy(flags, "No special flags");
    }

    snprintf(text, sizeof(text),
             "Sample %d: %s\n"
             "Length: %d bytes\n"
             "Loop start: %d\n"
             "Loop end: %d\n"
             "Flags: %s",
             index + 1,
             smp->name[0] ? smp->name : "(unnamed sample)",
             smp->len, smp->lps, smp->lpe, flags);

    gtk_label_set_text(GTK_LABEL(sample_details_label), text);
}

static gboolean on_sample_waveform_draw(GtkWidget *widget,
                                        cairo_t *cr, gpointer data)
{
    (void)data;

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width;
    int height = allocation.height;

    if (width <= 1 || height <= 1)
        return FALSE;

    /* Dark background and center line. */
    cairo_set_source_rgb(cr, 0.028, 0.060, 0.075);
    cairo_paint(cr);

    double center = height / 2.0;
    cairo_set_source_rgb(cr, 0.24, 0.27, 0.32);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0, center);
    cairo_line_to(cr, width, center);
    cairo_stroke(cr);

    if (!module_loaded || !ctx)
        return FALSE;

    struct xmp_module_info mi;
    xmp_get_module_info(ctx, &mi);

    if (!mi.mod || selected_sample < 0 ||
        selected_sample >= mi.mod->smp || !mi.mod->xxs)
        return FALSE;

    struct xmp_sample *smp = &mi.mod->xxs[selected_sample];
    if (!smp->data || smp->len <= 0)
        return FALSE;

    int stereo = !!(smp->flg & XMP_SAMPLE_STEREO);
    int stride = stereo ? 2 : 1;
    int count = smp->len;
    double amplitude = height * 0.43;

    /* Shade the loop range when it is valid. */
    if ((smp->flg & XMP_SAMPLE_LOOP) &&
        smp->lps >= 0 && smp->lpe > smp->lps &&
        smp->lpe <= count) {
        double x1 = (double)smp->lps / count * width;
        double x2 = (double)smp->lpe / count * width;
        cairo_set_source_rgba(cr, 0.20, 0.38, 0.30, 0.35);
        cairo_rectangle(cr, x1, 0, x2 - x1, height);
        cairo_fill(cr);
    }

    cairo_set_source_rgb(cr, 0.35, 0.92, 0.57);
    cairo_set_line_width(cr, 1.0);

    for (int x = 0; x < width; x++) {
        int first = (int)((int64_t)x * count / width);
        int last = (int)((int64_t)(x + 1) * count / width);

        if (first >= count)
            first = count - 1;
        if (last <= first)
            last = first + 1;
        if (last > count)
            last = count;

        double low = 1.0;
        double high = -1.0;

        for (int i = first; i < last; i++) {
            double value;

            if (smp->flg & XMP_SAMPLE_16BIT) {
                const int16_t *pcm = (const int16_t *)smp->data;
                value = pcm[(int64_t)i * stride] / 32768.0;
            } else {
                const int8_t *pcm = (const int8_t *)smp->data;
                value = pcm[(int64_t)i * stride] / 128.0;
            }

            if (value < low)
                low = value;
            if (value > high)
                high = value;
        }

        cairo_move_to(cr, x + 0.5, center - high * amplitude);
        cairo_line_to(cr, x + 0.5, center - low * amplitude);
    }

    cairo_stroke(cr);
    return FALSE;
}

static void on_sample_selected(GtkListBox *box, GtkListBoxRow *row,
                               gpointer data)
{
    (void)box;
    (void)data;

    if (!row)
        return;

    selected_sample = gtk_list_box_row_get_index(row);
    update_sample_details(selected_sample);

    if (sample_waveform_area)
        gtk_widget_queue_draw(sample_waveform_area);
}

static void populate_sample_browser(void)
{
    if (!sample_list || !sample_details_label || !module_comments_view)
        return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(sample_list));
    for (GList *it = children; it; it = it->next)
        gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(children);

    gtk_label_set_text(GTK_LABEL(sample_details_label),
                       "Load a module to inspect its samples.");

    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(module_comments_view));
    gtk_text_buffer_set_text(buffer, "", -1);

    if (!module_loaded)
        return;

    struct xmp_module_info mi;
    xmp_get_module_info(ctx, &mi);

    if (mi.mod) {
        for (int i = 0; i < mi.mod->smp; i++) {
            struct xmp_sample *smp = &mi.mod->xxs[i];
            char name[160];

            snprintf(name, sizeof(name), "%02d  %s",
                     i + 1,
                     smp->name[0] ? smp->name : "(unnamed sample)");

            GtkWidget *label = gtk_label_new(name);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_widget_set_margin_start(label, 6);
            gtk_widget_set_margin_end(label, 6);
            gtk_widget_set_margin_top(label, 3);
            gtk_widget_set_margin_bottom(label, 3);
            gtk_list_box_insert(GTK_LIST_BOX(sample_list), label, -1);
        }

        if (mi.mod->smp > 0) {
            selected_sample = 0;
            gtk_widget_show_all(sample_list);
            gtk_list_box_select_row(GTK_LIST_BOX(sample_list),
                                    gtk_list_box_get_row_at_index(
                                        GTK_LIST_BOX(sample_list), 0));
            update_sample_details(0);
        } else {
            gtk_label_set_text(GTK_LABEL(sample_details_label),
                               "This module contains no samples.");
        }
    }

    if (mi.comment && mi.comment[0]) {
        gtk_text_buffer_set_text(buffer, mi.comment, -1);
    } else {
        gtk_text_buffer_set_text(
            buffer, "This module contains no embedded comments.", -1);
    }
}

static int open_ffmpeg_audio(const char *filename)
{
    char error_text[512] = "";

    if (!filename || !*filename || !audio_device || !ctx)
        return 0;

    stop_ffmpeg_decoder();
    SDL_PauseAudioDevice(audio_device, 1);
    SDL_LockAudioDevice(audio_device);

    playing = 0;
    audio_ended = 0;
    stopped = 0;
    playback_mode = PLAYBACK_NONE;

    if (player_started) {
        xmp_end_player(ctx);
        player_started = 0;
    }

    if (module_loaded) {
        xmp_release_module(ctx);
        module_loaded = 0;
    }
    if (sid_audio) {
        sid_audio_close(sid_audio);
        sid_audio = NULL;
    }
    if (midi_audio) {
        midi_audio_close(midi_audio);
        midi_audio = NULL;
    }

    SDL_UnlockAudioDevice(audio_device);

    ffmpeg_audio = ffmpeg_audio_open(filename, error_text,
                                     sizeof(error_text));
    if (!ffmpeg_audio) {
        set_status(error_text[0] ? error_text : "Unsupported audio file.");
        gtk_label_set_text(GTK_LABEL(title_label), "Unable to open audio");
        update_play_button();
        return 0;
    }

    ffmpeg_duration_ms = ffmpeg_audio_duration_ms(ffmpeg_audio);
    ffmpeg_position_frames = 0;
    SDL_AtomicSet(&decoder_stop, 0);
    SDL_AtomicSet(&ffmpeg_decode_finished, 0);
    SDL_AtomicSet(&ffmpeg_decode_error, 0);

    decoder_thread = SDL_CreateThread(decoder_thread_fn,
                                      "ffmpeg-decoder", NULL);
    if (!decoder_thread) {
        snprintf(error_text, sizeof(error_text),
                 "Could not start decoder thread: %s", SDL_GetError());
        ffmpeg_audio_close(ffmpeg_audio);
        ffmpeg_audio = NULL;
        set_status(error_text);
        return 0;
    }

    playback_mode = PLAYBACK_FFMPEG;
    g_strlcpy(current_file, filename, sizeof(current_file));

    char *base = g_path_get_basename(filename);
    gtk_label_set_text(GTK_LABEL(title_label), base ? base : "Audio file");
    gtk_label_set_text(GTK_LABEL(format_label), "Audio file (FFmpeg)");
    gtk_label_set_text(GTK_LABEL(channels_label), "Stereo    44100 Hz");
    gtk_label_set_text(GTK_LABEL(details_label), filename);
    gtk_label_set_text(GTK_LABEL(time_label), "00:00");

    gtk_widget_set_sensitive(seek_scale, TRUE);
    updating_seek = 1;
    gtk_range_set_range(GTK_RANGE(seek_scale), 0,
                        ffmpeg_duration_ms > 0 ? ffmpeg_duration_ms : 1);
    gtk_range_set_value(GTK_RANGE(seek_scale), 0);
    updating_seek = 0;

    if (base)
        g_free(base);

    if (scope_area)
        gtk_widget_queue_draw(scope_area);
    if (pattern_area)
        gtk_widget_queue_draw(pattern_area);
    if (visualizer_area)
        gtk_widget_queue_draw(visualizer_area);
    if (eq_graph_area)
        gtk_widget_queue_draw(eq_graph_area);

    update_play_button();
    set_status("Audio file loaded");
    return 1;
}

static void on_play(GtkButton *button, gpointer data);

static void zx_apply_labels(void)
{
    if (!zx_active_source)
        return;

    char *base = g_path_get_basename(zx_active_source);
    g_strlcpy(current_file, zx_active_source, sizeof(current_file));
    gtk_label_set_text(GTK_LABEL(title_label), base);
    gtk_label_set_text(GTK_LABEL(format_label),
                       playback_mode == PLAYBACK_ZX ?
                       "ZX Spectrum / AY-YM (ZXTune native)" :
                       "ZX Spectrum / AY-YM (ZXTune)");
    gtk_label_set_text(GTK_LABEL(channels_label),
                       "Stereo mixed output    44100 Hz");
    gtk_label_set_text(GTK_LABEL(details_label), zx_active_source);
    populate_sample_browser();
    if (scope_area) {
        gtk_widget_set_size_request(scope_area, 620, SCOPE_ROW_HEIGHT);
        gtk_widget_queue_draw(scope_area);
    }
    if (pattern_area)
        gtk_widget_queue_draw(pattern_area);
    g_free(base);
}

static void zx_conversion_complete(GObject *object, GAsyncResult *result,
                                   gpointer user_data)
{
    ZxConversion *job = user_data;
    GError *error = NULL;
    gboolean waited = g_subprocess_wait_finish(G_SUBPROCESS(object),
                                                result, &error);
    gboolean current = zx_pending == job &&
                       job->generation == zx_generation;
    if (current)
        zx_pending = NULL;

    if (current && waited && g_subprocess_get_successful(job->process) &&
        g_file_test(job->wav_path, G_FILE_TEST_IS_REGULAR) &&
        open_ffmpeg_audio(job->wav_path)) {
        /* FFmpeg owns an open handle; preserve the WAV for Stop/Play restart. */
        zx_active_directory = g_steal_pointer(&job->directory);
        zx_active_wav = g_steal_pointer(&job->wav_path);
        zx_active_source = g_strdup(job->source_path);
        zx_apply_labels();
        set_status("ZX Spectrum tune loaded");
        if (job->autoplay)
            on_play(NULL, NULL);
    } else if (current) {
        gtk_label_set_text(GTK_LABEL(title_label), "Unable to load ZX tune");
        set_status("ZXTune could not render this tune.");
    }

    g_clear_error(&error);
    zx_free_conversion(job);
}

static void zx_start_conversion(const char *filename)
{
    char *executable = g_find_program_in_path("zxtune123");
    if (!executable) {
        char *local = g_build_filename(g_get_home_dir(), "zxtune", "bin",
                                       "linux", "release", "zxtune123", NULL);
        if (g_file_test(local, G_FILE_TEST_IS_EXECUTABLE))
            executable = g_strdup(local);
        g_free(local);
    }
    if (!executable) {
        gtk_label_set_text(GTK_LABEL(title_label), "ZXTune CLI not found");
        set_status("Build/install zxtune123 to play ZX tunes.");
        return;
    }

    GError *error = NULL;
    ZxConversion *job = g_new0(ZxConversion, 1);
    job->directory = g_dir_make_tmp("xmp-zx-XXXXXX", &error);
    if (!job->directory) {
        set_status(error ? error->message : "Cannot create temporary folder.");
        g_clear_error(&error);
        zx_free_conversion(job);
        g_free(executable);
        return;
    }

    job->source_path = g_strdup(filename);
    job->wav_path = g_build_filename(job->directory, "track.wav", NULL);
    job->generation = zx_generation;
    job->autoplay = zx_autoplay_request;
    char *wav_option = g_strdup_printf("filename=%s", job->wav_path);
    const char *argv[] = {executable, "--frequency", "44100", "--wav",
                          wav_option, filename, NULL};
    job->process = g_subprocess_newv(argv,
                                    G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                    G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                                    &error);
    g_free(wav_option);
    g_free(executable);

    if (!job->process) {
        set_status(error ? error->message : "Cannot start zxtune123.");
        g_clear_error(&error);
        zx_free_conversion(job);
        return;
    }

    zx_pending = job;
    gtk_label_set_text(GTK_LABEL(title_label), "Rendering ZX Spectrum tune…");
    gtk_label_set_text(GTK_LABEL(format_label), "ZXTune");
    gtk_label_set_text(GTK_LABEL(channels_label), "");
    gtk_label_set_text(GTK_LABEL(time_label), "00:00 / 00:00");
    set_status("Converting ZX tune to audio…");
    g_subprocess_wait_async(job->process, NULL, zx_conversion_complete, job);
}

#ifdef ENABLE_ZXTUNE_NATIVE
static int open_zx_native(const char *filename)
{
    char error_text[512] = "";
    if (!filename || !*filename || !audio_device)
        return 0;

    /* Decoder work must finish before we replace the renderer. */
    stop_ffmpeg_decoder();
    SDL_PauseAudioDevice(audio_device, 1);

    zx_audio = zx_audio_open(filename, error_text, sizeof(error_text));
    if (!zx_audio) {
        set_status(error_text[0] ? error_text :
                   "Could not open native ZXTune renderer.");
        return 0;
    }

    ffmpeg_duration_ms = zx_audio_duration_ms(zx_audio);
    ffmpeg_position_frames = 0;
    SDL_AtomicSet(&decoder_stop, 0);
    SDL_AtomicSet(&ffmpeg_decode_finished, 0);
    SDL_AtomicSet(&ffmpeg_decode_error, 0);
    SDL_AtomicSet(&ffmpeg_seek_requested, 0);

    decoder_thread = SDL_CreateThread(decoder_thread_fn,
                                      "zxtune-native-renderer", NULL);
    if (!decoder_thread) {
        set_status("Could not start native ZXTune audio thread.");
        zx_audio_close(zx_audio);
        zx_audio = NULL;
        return 0;
    }

    playback_mode = PLAYBACK_ZX;
    playing = 0;
    stopped = 0;
    audio_ended = 0;
    g_free(zx_active_source);
    zx_active_source = g_strdup(filename);

    gtk_widget_set_sensitive(seek_scale, TRUE);
    updating_seek = 1;
    gtk_range_set_range(GTK_RANGE(seek_scale), 0,
                        ffmpeg_duration_ms > 0 ? ffmpeg_duration_ms : 1);
    gtk_range_set_value(GTK_RANGE(seek_scale), 0);
    updating_seek = 0;
    gtk_label_set_text(GTK_LABEL(time_label), "00:00");
    zx_apply_labels();
    update_play_button();
    set_status("ZX Spectrum tune loaded (native audio)");
    return 1;
}
#endif

static void open_module(const char *filename)
{
    stop_ffmpeg_decoder();
    zx_cancel_pending();
    zx_remove_active();
    SDL_PauseAudioDevice(audio_device, 1);
    SDL_LockAudioDevice(audio_device);

    playing = 0;
    audio_ended = 0;
    stopped = 0;
    playback_mode = PLAYBACK_NONE;

    if (player_started) {
        xmp_end_player(ctx);
        player_started = 0;
    }

    if (module_loaded) {
        xmp_release_module(ctx);
        module_loaded = 0;
    }
    if (sid_audio) {
        sid_audio_close(sid_audio);
        sid_audio = NULL;
    }
    if (midi_audio) {
        midi_audio_close(midi_audio);
        midi_audio = NULL;
    }

    if (zx_is_music_path(filename)) {
        SDL_UnlockAudioDevice(audio_device);
        current_file[0] = '\0';
        update_play_button();
#ifdef ENABLE_ZXTUNE_NATIVE
        if (open_zx_native(filename)) {
            if (zx_autoplay_request)
                on_play(NULL, NULL);
            return;
        }
#endif
        zx_start_conversion(filename);
        return;
    }

    int rc = xmp_load_module(ctx, filename);
    if (rc < 0) {
        SDL_UnlockAudioDevice(audio_device);
        update_play_button();
        set_status("Could not load this module.");
        gtk_label_set_text(GTK_LABEL(title_label), "Unable to load module");
        return;
    }

    struct xmp_module_info mi;
    xmp_get_module_info(ctx, &mi);

    if (xmp_start_player(ctx, AUDIO_RATE,
                         0) < 0) {
        xmp_release_module(ctx);
        SDL_UnlockAudioDevice(audio_device);
        set_status("Could not start the playback engine.");
        return;
    }

    module_loaded = 1;
    player_started = 1;
    playback_mode = PLAYBACK_MODULE;
    g_strlcpy(current_file, filename, sizeof(current_file));

    char title[512];
    char format[512];
    snprintf(title, sizeof(title), "%s",
             mi.mod->name[0] ? mi.mod->name : "Untitled module");
    snprintf(format, sizeof(format), "%s",
             mi.mod->type[0] ? mi.mod->type : "Unknown format");

    gtk_label_set_text(GTK_LABEL(title_label), title);
    gtk_label_set_text(GTK_LABEL(format_label), format);

    char details[256];
    snprintf(details, sizeof(details),
             "%d channels    %d instruments    %d patterns",
             mi.mod->chn, mi.mod->ins, mi.mod->pat);
    gtk_label_set_text(GTK_LABEL(channels_label), details);
    if (scope_area) {
        gtk_widget_set_size_request(scope_area, 620,
                                    mi.mod->chn * SCOPE_ROW_HEIGHT);
        gtk_widget_queue_draw(scope_area);
    }

    gtk_label_set_text(GTK_LABEL(details_label), filename);

    struct xmp_frame_info fi;
    xmp_get_frame_info(ctx, &fi);
    gtk_widget_set_sensitive(seek_scale, TRUE);
    updating_seek = 1;
    gtk_range_set_range(GTK_RANGE(seek_scale), 0,
                        fi.total_time > 0 ? fi.total_time : 1);
    gtk_range_set_value(GTK_RANGE(seek_scale), 0);
    updating_seek = 0;

    char time_text[64];
    snprintf(time_text, sizeof(time_text), "00:00 / %02d:%02d",
             fi.total_time / 60000, (fi.total_time / 1000) % 60);
    gtk_label_set_text(GTK_LABEL(time_label), time_text);

    populate_sample_browser();

    SDL_UnlockAudioDevice(audio_device);
    update_play_button();
    set_status("Module loaded");
}

static int open_sid(const char *filename)
{
    char error_text[512] = "";

    if (!filename || !*filename || !audio_device)
        return 0;

    stop_ffmpeg_decoder();
    SDL_PauseAudioDevice(audio_device, 1);
    SDL_LockAudioDevice(audio_device);

    playing = 0;
    audio_ended = 0;
    stopped = 0;
    playback_mode = PLAYBACK_NONE;

    if (player_started) {
        xmp_end_player(ctx);
        player_started = 0;
    }
    if (module_loaded) {
        xmp_release_module(ctx);
        module_loaded = 0;
    }
    if (sid_audio) {
        sid_audio_close(sid_audio);
        sid_audio = NULL;
    }
    if (midi_audio) {
        midi_audio_close(midi_audio);
        midi_audio = NULL;
    }
    SDL_UnlockAudioDevice(audio_device);

    sid_audio = sid_audio_open(filename, error_text, sizeof(error_text));
    if (!sid_audio) {
        if (error_text[0])
            set_status(error_text);
        return 0;
    }

    SDL_LockAudioDevice(audio_device);
    playback_mode = PLAYBACK_SID;
    SDL_UnlockAudioDevice(audio_device);

    g_strlcpy(current_file, filename, sizeof(current_file));

    const char *sid_title = sid_audio_title(sid_audio);
    const char *sid_author = sid_audio_author(sid_audio);
    const char *sid_release = sid_audio_release_info(sid_audio);
    char title[512];
    char details[1024];
    snprintf(title, sizeof(title), "%s",
             sid_title && sid_title[0] ? sid_title : "Untitled SID tune");
    snprintf(details, sizeof(details), "%s%s%s%s%s",
             filename,
             sid_author && sid_author[0] ? "\nAuthor: " : "",
             sid_author && sid_author[0] ? sid_author : "",
             sid_release && sid_release[0] ? "\nRelease: " : "",
             sid_release && sid_release[0] ? sid_release : "");

    gtk_label_set_text(GTK_LABEL(title_label), title);
    gtk_label_set_text(GTK_LABEL(format_label), "Commodore 64 SID (sidplayfp)");
    gtk_label_set_text(GTK_LABEL(channels_label), "Stereo    44100 Hz");
    gtk_label_set_text(GTK_LABEL(details_label), details);
    gtk_label_set_text(GTK_LABEL(time_label), "SID tune");
    gtk_widget_set_sensitive(seek_scale, FALSE);
    updating_seek = 1;
    gtk_range_set_range(GTK_RANGE(seek_scale), 0, 1);
    gtk_range_set_value(GTK_RANGE(seek_scale), 0);
    updating_seek = 0;

    populate_sample_browser();
    if (scope_area)
        gtk_widget_queue_draw(scope_area);
    if (pattern_area)
        gtk_widget_queue_draw(pattern_area);
    if (visualizer_area)
        gtk_widget_queue_draw(visualizer_area);

    update_play_button();
    set_status("SID tune loaded");
    return 1;
}

static gboolean is_midi_path(const char *filename)
{
    const char *dot = filename ? strrchr(filename, '.') : NULL;
    return dot && (g_ascii_strcasecmp(dot, ".mid") == 0 ||
                   g_ascii_strcasecmp(dot, ".midi") == 0 ||
                   g_ascii_strcasecmp(dot, ".kar") == 0);
}

static int open_midi(const char *filename)
{
    char error[512] = "";
    if (!filename || !*filename || !audio_device)
        return 0;

    stop_ffmpeg_decoder();
    zx_cancel_pending();
    zx_remove_active();
    SDL_PauseAudioDevice(audio_device, 1);
    SDL_LockAudioDevice(audio_device);
    playing = 0;
    audio_ended = 0;
    stopped = 0;
    playback_mode = PLAYBACK_NONE;
    if (player_started) {
        xmp_end_player(ctx);
        player_started = 0;
    }
    if (module_loaded) {
        xmp_release_module(ctx);
        module_loaded = 0;
    }
    if (sid_audio) {
        sid_audio_close(sid_audio);
        sid_audio = NULL;
    }
    if (midi_audio) {
        midi_audio_close(midi_audio);
        midi_audio = NULL;
    }
    SDL_UnlockAudioDevice(audio_device);

    midi_audio = midi_audio_open(filename, selected_soundfont,
                                 error, sizeof(error));
    if (!midi_audio) {
        current_file[0] = '\0';
        gtk_label_set_text(GTK_LABEL(title_label), "Unable to load MIDI");
        set_status(error[0] ? error : "Could not open MIDI file");
        populate_sample_browser();
        update_play_button();
        return 0;
    }

    SDL_LockAudioDevice(audio_device);
    playback_mode = PLAYBACK_MIDI;
    SDL_UnlockAudioDevice(audio_device);
    g_strlcpy(current_file, filename, sizeof(current_file));

    gchar *base = g_path_get_basename(filename);
    gchar *details = g_strdup_printf("%s\nSoundFont: %s", filename,
                                      midi_audio_soundfont(midi_audio));
    gtk_label_set_text(GTK_LABEL(title_label), base);
    gtk_label_set_text(GTK_LABEL(format_label),
                       "Standard MIDI File (FluidSynth)");
    gtk_label_set_text(GTK_LABEL(channels_label), "Stereo    44100 Hz");
    gtk_label_set_text(GTK_LABEL(details_label), details);
    g_free(base);
    g_free(details);

    int64_t duration = midi_audio_duration_ms(midi_audio);
    char time_text[80];
    g_snprintf(time_text, sizeof(time_text), "00:00 / %02lld:%02lld",
               (long long)(duration / 60000),
               (long long)((duration / 1000) % 60));
    gtk_label_set_text(GTK_LABEL(time_label), time_text);
    gtk_widget_set_sensitive(seek_scale, TRUE);
    updating_seek = 1;
    gtk_range_set_range(GTK_RANGE(seek_scale), 0, duration > 0 ? duration : 1);
    gtk_range_set_value(GTK_RANGE(seek_scale), 0);
    updating_seek = 0;

    populate_sample_browser();
    if (scope_area) {
        gtk_widget_set_size_request(scope_area, 620, 180);
        gtk_widget_queue_draw(scope_area);
    }
    if (pattern_area)
        gtk_widget_queue_draw(pattern_area);
    if (visualizer_area)
        gtk_widget_queue_draw(visualizer_area);
    update_play_button();
    set_status("MIDI loaded (FluidSynth)");
    return 1;
}

static char *get_last_folder_file(void)
{
    return g_build_filename(g_get_user_config_dir(),
                            "xmp-module-player",
                            "last-folder", NULL);
}

static void restore_last_folder(GtkWidget *dialog)
{
    char *settings_file = get_last_folder_file();
    char *folder = NULL;

    if (g_file_get_contents(settings_file, &folder, NULL, NULL)) {
        g_strstrip(folder);
        if (*folder && g_file_test(folder, G_FILE_TEST_IS_DIR))
            gtk_file_chooser_set_current_folder(
                GTK_FILE_CHOOSER(dialog), folder);
    }

    g_free(folder);
    g_free(settings_file);
}

static void remember_last_folder(GtkWidget *dialog)
{
    char *folder = gtk_file_chooser_get_current_folder(
        GTK_FILE_CHOOSER(dialog));

    if (folder) {
        char *settings_file = get_last_folder_file();
        char *settings_dir = g_path_get_dirname(settings_file);

        g_mkdir_with_parents(settings_dir, 0700);
        g_file_set_contents(settings_file, folder, -1, NULL);

        g_free(settings_dir);
        g_free(settings_file);
        g_free(folder);
    }
}

static char *get_soundfont_settings_file(void)
{
    return g_build_filename(g_get_user_config_dir(),
                            "xmp-module-player", "soundfont", NULL);
}

static char *get_soundfont_library_file(void)
{
    return g_build_filename(g_get_user_config_dir(),
                            "xmp-module-player", "soundfonts.ini", NULL);
}

static gboolean soundfont_find(const char *path, GtkTreeIter *result)
{
    if (!soundfont_store || !path)
        return FALSE;
    GtkTreeModel *model = GTK_TREE_MODEL(soundfont_store);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gchar *existing = NULL;
        gtk_tree_model_get(model, &iter, SF_PATH, &existing, -1);
        gboolean found = g_strcmp0(existing, path) == 0;
        g_free(existing);
        if (found) {
            if (result) *result = iter;
            return TRUE;
        }
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    return FALSE;
}

/* Only paths are stored, never copies of the user's SoundFont files. */
static gboolean soundfont_add_to_library(const char *path, gboolean validate)
{
    if (!path || !*path || !soundfont_store)
        return FALSE;
    gchar *canonical = g_canonicalize_filename(path, NULL);
    if (soundfont_find(canonical, NULL) ||
        (validate && !midi_audio_valid_soundfont(canonical))) {
        g_free(canonical);
        return FALSE;
    }

    gchar *name = g_path_get_basename(canonical);
    const char *dot = strrchr(name, '.');
    const char *format = dot && g_ascii_strcasecmp(dot, ".sf3") == 0
                         ? "SF3" : "SF2";
    GtkTreeIter iter;
    gtk_list_store_append(soundfont_store, &iter);
    gtk_list_store_set(soundfont_store, &iter,
                       SF_NAME, name, SF_FORMAT, format,
                       SF_PATH, canonical, SF_STATE, "", -1);
    g_free(name);
    g_free(canonical);
    return TRUE;
}

static void refresh_soundfont_library(void)
{
    if (!soundfont_store)
        return;
    gchar *fallback = NULL;
    const char *active = midi_audio
        ? midi_audio_soundfont(midi_audio) : selected_soundfont;
    if (!active) {
        fallback = midi_audio_find_default_soundfont();
        active = fallback;
    }

    GtkTreeModel *model = GTK_TREE_MODEL(soundfont_store);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gchar *path = NULL;
        gtk_tree_model_get(model, &iter, SF_PATH, &path, -1);
        gboolean current = g_strcmp0(path, active) == 0;
        gboolean favorite = g_strcmp0(path, favorite_soundfont) == 0;
        const char *state = !g_file_test(path, G_FILE_TEST_IS_REGULAR)
            ? "Missing"
            : current && favorite ? "Active · Default"
            : current ? "Active"
            : favorite ? "Default" : "";
        gtk_list_store_set(soundfont_store, &iter, SF_STATE, state, -1);
        g_free(path);
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    g_free(fallback);
}

static void save_soundfont_library(void)
{
    if (!soundfont_store)
        return;
    GKeyFile *keys = g_key_file_new();
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    GtkTreeModel *model = GTK_TREE_MODEL(soundfont_store);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gchar *path = NULL;
        gtk_tree_model_get(model, &iter, SF_PATH, &path, -1);
        if (path) g_ptr_array_add(paths, path);
        valid = gtk_tree_model_iter_next(model, &iter);
    }

    const gchar *empty_list[] = {NULL};
    g_key_file_set_string_list(keys, "Library", "paths",
        paths->len ? (const gchar *const *)paths->pdata : empty_list,
        paths->len);
    if (favorite_soundfont)
        g_key_file_set_string(keys, "Library", "startup-default",
                              favorite_soundfont);
    gsize length = 0;
    gchar *contents = g_key_file_to_data(keys, &length, NULL);
    gchar *path = get_soundfont_library_file();
    gchar *directory = g_path_get_dirname(path);
    GError *error = NULL;
    if (g_mkdir_with_parents(directory, 0700) != 0 ||
        !g_file_set_contents(path, contents, length, &error)) {
        set_status(error ? error->message : "Could not save SoundFont library");
        g_clear_error(&error);
    }
    g_free(directory);
    g_free(path);
    g_free(contents);
    g_ptr_array_free(paths, TRUE);
    g_key_file_free(keys);
}

static void load_soundfont_library(void)
{
    GKeyFile *keys = g_key_file_new();
    gchar *file = get_soundfont_library_file();
    if (g_key_file_load_from_file(keys, file, G_KEY_FILE_NONE, NULL)) {
        gsize count = 0;
        gchar **paths = g_key_file_get_string_list(keys, "Library",
                                                  "paths", &count, NULL);
        for (gsize i = 0; i < count; ++i)
            soundfont_add_to_library(paths[i], FALSE);
        g_strfreev(paths);
        favorite_soundfont = g_key_file_get_string(keys, "Library",
                                                  "startup-default", NULL);
    }
    g_key_file_free(keys);
    g_free(file);

    /* Migration from XMP 1.1's single selected-SoundFont setting. */
    if (selected_soundfont)
        soundfont_add_to_library(selected_soundfont, FALSE);
    if (selected_soundfont && !midi_audio_valid_soundfont(selected_soundfont))
        g_clear_pointer(&selected_soundfont, g_free);
    if (favorite_soundfont && midi_audio_valid_soundfont(favorite_soundfont)) {
        soundfont_add_to_library(favorite_soundfont, FALSE);
        g_free(selected_soundfont);
        selected_soundfont = g_strdup(favorite_soundfont);
    }
    /* Don't persist an AppImage's temporary mount path as a library entry.
     * The current bundled/system bank is always visible in the label below. */
    refresh_soundfont_library();
}

static gchar *get_selected_library_soundfont(void)
{
    if (!soundfont_view)
        return NULL;
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(soundfont_view));
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return NULL;
    gchar *path = NULL;
    gtk_tree_model_get(model, &iter, SF_PATH, &path, -1);
    return path;
}

static void update_soundfont_label(void)
{
    if (!soundfont_label)
        return;
    char *fallback = NULL;
    const char *path = selected_soundfont;
    if (!path || !*path) {
        fallback = midi_audio_find_default_soundfont();
        path = fallback;
    }
    gchar *message = path
        ? g_strdup_printf("%s\n%s", selected_soundfont ?
                          "Selected SoundFont:" : "System SoundFont:", path)
        : g_strdup("No SoundFont found. Choose an SF2 or SF3 file to play MIDI.");
    gtk_label_set_text(GTK_LABEL(soundfont_label), message);
    g_free(message);
    g_free(fallback);
    refresh_soundfont_library();
}

static gboolean set_selected_soundfont(const char *path)
{
    char error[512] = "";
    if (!midi_audio_valid_soundfont(path)) {
        set_status("Please choose a valid SF2 or SF3 SoundFont file");
        return FALSE;
    }

    if (midi_audio) {
        SDL_LockAudioDevice(audio_device);
        int ok = midi_audio_set_soundfont(midi_audio, path, error,
                                          sizeof(error));
        SDL_UnlockAudioDevice(audio_device);
        if (!ok) {
            set_status(error[0] ? error : "Could not load the SoundFont");
            return FALSE;
        }
        gchar *details = g_strdup_printf("%s\nSoundFont: %s",
                                          current_file, path);
        gtk_label_set_text(GTK_LABEL(details_label), details);
        g_free(details);
    }

    g_free(selected_soundfont);
    selected_soundfont = g_strdup(path);
    char *settings_file = get_soundfont_settings_file();
    char *settings_dir = g_path_get_dirname(settings_file);
    g_mkdir_with_parents(settings_dir, 0700);
    g_file_set_contents(settings_file, path, -1, NULL);
    g_free(settings_dir);
    g_free(settings_file);
    soundfont_add_to_library(path, FALSE);
    save_soundfont_library();
    update_soundfont_label();
    set_status("SoundFont selected and saved");
    return TRUE;
}

/* GtkDialog has separate content and button areas. Give both the same
 * Deep Ocean surface, regardless of the host GTK theme. */
static void theme_dialog(GtkWidget *dialog)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(dialog),
                                "xmp-dialog");

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *actions = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
    if (content)
        gtk_style_context_add_class(gtk_widget_get_style_context(content),
                                    "xmp-dialog-surface");
    if (actions)
        gtk_style_context_add_class(gtk_widget_get_style_context(actions),
                                    "xmp-dialog-surface");
}

static void on_choose_soundfont(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Add MIDI SoundFonts", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Add", GTK_RESPONSE_ACCEPT, NULL);
    theme_dialog(dialog);
    restore_last_folder(dialog);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "SoundFont files (SF2, SF3)");
    gtk_file_filter_add_pattern(filter, "*.sf2");
    gtk_file_filter_add_pattern(filter, "*.SF2");
    gtk_file_filter_add_pattern(filter, "*.sf3");
    gtk_file_filter_add_pattern(filter, "*.SF3");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        guint count = 0;
        for (GSList *item = files; item; item = item->next)
            if (soundfont_add_to_library(item->data, TRUE)) ++count;
        g_slist_free_full(files, g_free);
        save_soundfont_library();
        refresh_soundfont_library();
        remember_last_folder(dialog);
        gchar *message = g_strdup_printf(
            "%u SoundFont(s) added. Double-click one to activate.", count);
        set_status(message);
        g_free(message);
    }
    gtk_widget_destroy(dialog);
}

static void on_activate_soundfont(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    gchar *path = get_selected_library_soundfont();
    if (path) {
        set_selected_soundfont(path);
        g_free(path);
    } else {
        set_status("Select a SoundFont from the library first");
    }
}

static void on_soundfont_row_activated(GtkTreeView *view, GtkTreePath *path,
                                       GtkTreeViewColumn *column, gpointer data)
{
    (void)view;
    (void)column;
    (void)data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(soundfont_store), &iter, path)) {
        gchar *filename = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(soundfont_store), &iter,
                           SF_PATH, &filename, -1);
        set_selected_soundfont(filename);
        g_free(filename);
    }
}

static void on_favorite_soundfont(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    gchar *path = get_selected_library_soundfont();
    if (!path) {
        set_status("Select a SoundFont to set or clear its default status");
        return;
    }
    if (!midi_audio_valid_soundfont(path)) {
        set_status("That SoundFont file is unavailable");
        g_free(path);
        return;
    }
    if (g_strcmp0(path, favorite_soundfont) == 0) {
        g_clear_pointer(&favorite_soundfont, g_free);
        set_status("Startup default cleared");
    } else {
        g_free(favorite_soundfont);
        favorite_soundfont = g_strdup(path);
        set_status("Startup default saved");
    }
    save_soundfont_library();
    refresh_soundfont_library();
    g_free(path);
}

static void on_remove_soundfont(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    gchar *path = get_selected_library_soundfont();
    if (!path)
        return;
    if (midi_audio &&
        g_strcmp0(path, midi_audio_soundfont(midi_audio)) == 0) {
        set_status("Switch MIDI to another SoundFont before removing this one");
        g_free(path);
        return;
    }
    GtkTreeIter iter;
    if (soundfont_find(path, &iter)) {
        gtk_list_store_remove(soundfont_store, &iter);
        if (g_strcmp0(path, favorite_soundfont) == 0)
            g_clear_pointer(&favorite_soundfont, g_free);
        if (g_strcmp0(path, selected_soundfont) == 0) {
            g_clear_pointer(&selected_soundfont, g_free);
            gchar *settings = get_soundfont_settings_file();
            g_file_set_contents(settings, "", 0, NULL);
            g_free(settings);
        }
        save_soundfont_library();
        update_soundfont_label();
        set_status("Removed from library. Original SoundFont file was not deleted.");
    }
    g_free(path);
}

#define MAX_IMPORTED_SOUNDFONTS 500
static void scan_soundfont_folder(const char *folder, int depth, guint *added)
{
    if (depth > 8 || *added >= MAX_IMPORTED_SOUNDFONTS)
        return;
    GDir *dir = g_dir_open(folder, 0, NULL);
    if (!dir)
        return;
    const gchar *name;
    while ((name = g_dir_read_name(dir)) && *added < MAX_IMPORTED_SOUNDFONTS) {
        if (name[0] == '.')
            continue;
        gchar *path = g_build_filename(folder, name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_DIR) &&
            !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
            scan_soundfont_folder(path, depth + 1, added);
        } else {
            const char *dot = strrchr(name, '.');
            if (dot && (g_ascii_strcasecmp(dot, ".sf2") == 0 ||
                        g_ascii_strcasecmp(dot, ".sf3") == 0)) {
                if (soundfont_add_to_library(path, TRUE))
                    ++*added;
            }
        }
        g_free(path);
    }
    g_dir_close(dir);
}

static void on_import_soundfont_folder(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Import SoundFonts From Folder", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Scan Folder", GTK_RESPONSE_ACCEPT, NULL);
    theme_dialog(dialog);
    restore_last_folder(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder) {
            guint added = 0;
            scan_soundfont_folder(folder, 0, &added);
            save_soundfont_library();
            refresh_soundfont_library();
            gchar *message = g_strdup_printf(
                "%u SoundFont(s) imported%s. Files were not moved.",
                added, added >= MAX_IMPORTED_SOUNDFONTS ? " (limit reached)" : "");
            set_status(message);
            g_free(message);
            remember_last_folder(dialog);
            g_free(folder);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_system_soundfont(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    char *fallback = midi_audio_find_default_soundfont();
    if (!fallback) {
        set_status("No system SoundFont found. Choose an SF2/SF3 file.");
        return;
    }
    char error[512] = "";
    if (midi_audio) {
        SDL_LockAudioDevice(audio_device);
        int ok = midi_audio_set_soundfont(midi_audio, fallback, error,
                                          sizeof(error));
        SDL_UnlockAudioDevice(audio_device);
        if (!ok) {
            set_status(error[0] ? error : "Could not load system SoundFont");
            g_free(fallback);
            return;
        }
        gchar *details = g_strdup_printf("%s\nSoundFont: %s",
                                          current_file, fallback);
        gtk_label_set_text(GTK_LABEL(details_label), details);
        g_free(details);
    }
    g_clear_pointer(&selected_soundfont, g_free);
    char *settings_file = get_soundfont_settings_file();
    g_file_set_contents(settings_file, "", 0, NULL);
    g_free(settings_file);
    update_soundfont_label();
    set_status("Using system SoundFont");
    g_free(fallback);
}


static char *get_hvsc_folder_file(void)
{
    return g_build_filename(g_get_user_config_dir(),
                            "xmp-module-player",
                            "hvsc-folder", NULL);
}

static void on_hvsc_folder_changed(GtkFileChooserButton *chooser,
                                   gpointer data)
{
    (void)data;
    char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    if (!folder) return;

    char *settings_file = get_hvsc_folder_file();
    char *settings_dir = g_path_get_dirname(settings_file);

    g_mkdir_with_parents(settings_dir, 0700);
    g_file_set_contents(settings_file, folder, -1, NULL);

    g_free(settings_dir);
    g_free(settings_file);
    g_free(folder);
}



/* HVSC waveform progress indicator.
   The fraction will later be updated by the download worker. */

static gboolean hvsc_waveform_draw(GtkWidget *widget,
                                  cairo_t *cr,
                                  gpointer data)
{
    (void)data;

    GtkAllocation area;
    gtk_widget_get_allocation(widget, &area);

    double width = area.width;
    double height = area.height;

    double fraction = gtk_progress_bar_get_fraction(
        GTK_PROGRESS_BAR(
            g_object_get_data(G_OBJECT(widget), "progress")));

    fraction = CLAMP(fraction, 0.0, 1.0);

    cairo_set_source_rgb(cr, 0.035, 0.079, 0.095);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    double center = height / 2.0;
    double step = 4.0;

    for (int i = 0; i < (int)(width / step); i++) {
        double x = i * step + 1.0;

        double amplitude =
            5.0 +
            fabs(sin(i * 1.71) * cos(i * 0.31)) * 28.0 +
            fabs(sin(i * 0.18)) * 7.0;

        amplitude = MIN(amplitude, height - 8.0);

        if (x < width * fraction)
            cairo_set_source_rgb(cr, 0.212, 0.769, 0.847);
        else
            cairo_set_source_rgb(cr, 0.278, 0.333, 0.412);

        cairo_rectangle(cr, x, center - amplitude / 2.0,
                        2.6, amplitude);
        cairo_fill(cr);
    }

    cairo_set_source_rgba(cr, 0.51, 0.57, 0.65, 0.45);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, 0, center);
    cairo_line_to(cr, width, center);
    cairo_stroke(cr);

    if (fraction > 0.0) {
        cairo_set_source_rgb(cr, 0.90, 0.98, 1.0);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, width * fraction, 0);
        cairo_line_to(cr, width * fraction, height);
        cairo_stroke(cr);
    }

    return FALSE;
}


typedef struct {
    GtkWidget *button;
    GtkWidget *progress;
    GtkWidget *waveform;
    GtkWidget *status;
    GtkWidget *collection_folder;
    char *install_parent;
} HvscUI;

static void on_hvsc_progress(double fraction,
                             const char *status,
                             gpointer user_data)
{
    HvscUI *ui = user_data;

    gboolean finished = (fraction == 2.0 || fraction == -2.0);

    if (!finished && !hvsc_download_is_running())
        return;

    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(ui->progress),
        fraction == 2.0 ? 1.0 :
        fraction < 0.0 ? 0.0 : CLAMP(fraction, 0.0, 1.0));

    gtk_widget_queue_draw(ui->waveform);

    gtk_label_set_text(
        GTK_LABEL(ui->status), status);

    if (fraction == 2.0 &&
        g_str_has_prefix(status, "HVSC installation completed") &&
        ui->install_parent) {
        char *folder = g_build_filename(
            ui->install_parent, "HVSC", "C64Music", NULL);

        if (g_file_test(folder, G_FILE_TEST_IS_DIR)) {
            gtk_file_chooser_set_filename(
                GTK_FILE_CHOOSER(ui->collection_folder), folder);

            on_hvsc_folder_changed(
                GTK_FILE_CHOOSER_BUTTON(ui->collection_folder), NULL);
        }

        g_free(folder);
        g_clear_pointer(&ui->install_parent, g_free);
    }

    if (finished) {
        gtk_button_set_label(
            GTK_BUTTON(ui->button), "Download & Install HVSC");
        gtk_widget_set_sensitive(ui->button, TRUE);
    }
}

static void on_hvsc_download_clicked(GtkButton *button,
                                     gpointer user_data)
{
    GtkWidget **widgets = user_data;

    if (hvsc_download_is_running()) {
        hvsc_download_cancel();
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        return;
    }

    GtkWidget *chooser = widgets[0];
    HvscUI *ui = (HvscUI *)widgets[1];

    char *parent = gtk_file_chooser_get_filename(
        GTK_FILE_CHOOSER(chooser));

    if (!parent)
        return;

    char *destination = g_build_filename(parent, "HVSC", NULL);

    if (g_file_test(destination, G_FILE_TEST_EXISTS) ||
        g_file_test(destination, G_FILE_TEST_IS_SYMLINK)) {
        gtk_label_set_text(
            GTK_LABEL(ui->status),
            "Installation refused: HVSC already exists.");
        g_free(destination);
        g_free(parent);
        return;
    }

    gtk_button_set_label(GTK_BUTTON(button), "Cancel Download");
    gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);

    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(ui->progress), 0.0);
    gtk_widget_queue_draw(ui->waveform);

    gtk_label_set_text(
        GTK_LABEL(ui->status),
        "Starting HVSC download...");

    g_free(ui->install_parent);
    ui->install_parent = g_strdup(parent);

    hvsc_download_async(parent, on_hvsc_progress, ui);

    g_free(destination);
    g_free(parent);
}

static void on_hvsc_check_destination(GtkButton *button, gpointer data)
{
    (void)button;

    GtkFileChooser *chooser = GTK_FILE_CHOOSER(data);
    char *parent = gtk_file_chooser_get_filename(chooser);

    if (!parent) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
            "Please select an installation directory.");
        theme_dialog(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    char *destination = g_build_filename(parent, "HVSC", NULL);
    char *message = NULL;
    GtkMessageType type;

    if (!g_file_test(parent, G_FILE_TEST_IS_DIR)) {
        message = g_strdup("The selected parent directory does not exist.");
        type = GTK_MESSAGE_ERROR;
    } else if (g_file_test(destination, G_FILE_TEST_EXISTS) ||
               g_file_test(destination, G_FILE_TEST_IS_SYMLINK)) {
        message = g_strdup_printf(
            "Installation refused.\n\n%s already exists.\n"
            "No files have been changed.", destination);
        type = GTK_MESSAGE_WARNING;
    } else {
        message = g_strdup_printf(
            "Destination is available:\n\n%s\n\n"
            "No files have been changed.\n"
            "Downloading will be enabled in the next stage.",
            destination);
        type = GTK_MESSAGE_INFO;
    }

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(window), GTK_DIALOG_MODAL,
        type, GTK_BUTTONS_CLOSE, "%s", message);

    theme_dialog(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_free(message);
    g_free(destination);
    g_free(parent);
}

static void on_hvsc_open_folder(GtkButton *button, gpointer data)
{
    (void)button;

    GtkFileChooser *chooser = GTK_FILE_CHOOSER(data);
    char *folder = gtk_file_chooser_get_filename(chooser);
    if (!folder) return;

    GError *error = NULL;
    char *argv[] = { "xdg-open", folder, NULL };

    if (!g_spawn_async(NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, &error)) {
        g_warning("Cannot open HVSC folder: %s", error->message);
        g_clear_error(&error);
    }

    g_free(folder);
}

static void on_open(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Open module", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);
    theme_dialog(dialog);
    restore_last_folder(dialog);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio and tracker files");

    const char *patterns[] = {
        "*.mod", "*.MOD", "*.xm", "*.XM", "*.it", "*.IT",
        "*.mid", "*.MID", "*.midi", "*.MIDI", "*.kar", "*.KAR",
        "*.s3m", "*.S3M", "*.mptm", "*.MPTM", "*.mtm", "*.MTM",
        "*.669", "*.amf", "*.med", "*.okt", "*.ult", "*.sid", "*.SID",
        "*.mp3", "*.MP3", "*.wav", "*.WAV", "*.m4a", "*.M4A",
        "*.aac", "*.AAC", "*.ogg", "*.OGG", "*.oga", "*.OGA", "*.opus", "*.OPUS",
        "*.flac", "*.FLAC", "*.webm", "*.WEBM", "*.mka", "*.MKA",
        "*.aiff", "*.aif", "*.wma", "*.mp4", "*.MP4",
        "*.pt3", "*.PT3", "*.pt2", "*.PT2", "*.ay", "*.AY",
        "*.vtx", "*.VTX", "*.ym", "*.YM", "*.stc", "*.STC",
        "*.sqt", "*.SQT", "*.psg", "*.PSG", "*.asc", "*.ASC"
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
        gtk_file_filter_add_pattern(filter, patterns[i]);

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All files");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        remember_last_folder(dialog);
        char *filename =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            if (is_midi_path(filename)) {
                open_midi(filename);
            } else {
                open_module(filename);
                if (!zx_is_music_path(filename) && !module_loaded &&
                    !open_sid(filename))
                    open_ffmpeg_audio(filename);
            }
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
}

#ifdef ENABLE_WAV_EXPORT
static void free_wav_export_task(WavExportTask *task)
{
    if (!task) return;
    g_free(task->source);
    if (task->request.format == EXPORT_WAV_MIDI)
        g_free((gpointer)task->request.soundfont);
    g_free(task->destination);
    g_free(task);
}

static gpointer wav_export_worker(gpointer data)
{
    WavExportTask *task = data;
    task->succeeded = export_wav_render(&task->request, &task->progress,
                                       &task->cancel,
                                       task->error, sizeof(task->error));
    g_atomic_int_set(&task->finished, 1);
    return NULL;
}

static void on_wav_export_response(GtkDialog *dialog, gint response,
                                   gpointer data)
{
    WavExportTask *task = data;
    if (response != GTK_RESPONSE_CANCEL &&
        response != GTK_RESPONSE_DELETE_EVENT)
        return;

    g_atomic_int_set(&task->cancel, 1);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_CANCEL, FALSE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(task->progress_bar),
                              "Cancelling export…");
}

static gboolean on_wav_export_delete(GtkWidget *dialog, GdkEvent *event,
                                     gpointer data)
{
    (void)dialog;
    (void)event;
    WavExportTask *task = data;
    g_atomic_int_set(&task->cancel, 1);
    return TRUE; /* Keep the dialog alive until the worker has exited. */
}

static gboolean update_wav_export_dialog(gpointer data)
{
    WavExportTask *task = data;

    if (g_atomic_int_get(&task->finished)) {
        g_thread_join(task->thread);
        task->progress_timer = 0;
        active_wav_export = NULL;
        gtk_widget_destroy(task->dialog);

        if (task->succeeded) {
            set_status("WAV export completed successfully");
            GtkWidget *message = gtk_message_dialog_new(
                GTK_WINDOW(window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
                "WAV exported successfully:\n%s", task->destination);
            theme_dialog(message);
            gtk_dialog_run(GTK_DIALOG(message));
            gtk_widget_destroy(message);
        } else if (!g_atomic_int_get(&task->cancel)) {
            set_status(task->error[0] ? task->error : "WAV export failed");
            GtkWidget *message = gtk_message_dialog_new(
                GTK_WINDOW(window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                "WAV export failed:\n%s", task->error);
            theme_dialog(message);
            gtk_dialog_run(GTK_DIALOG(message));
            gtk_widget_destroy(message);
        } else {
            set_status("WAV export cancelled");
        }

        free_wav_export_task(task);
        return G_SOURCE_REMOVE;
    }

    int progress = g_atomic_int_get(&task->progress);
    if (progress < 0) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(task->progress_bar));
    } else {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(task->progress_bar),
                                      progress / 100.0);
        if (!g_atomic_int_get(&task->cancel)) {
            char message[64];
            g_snprintf(message, sizeof(message), "Rendering… %d%%", progress);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(task->progress_bar),
                                      message);
        }
    }
    return G_SOURCE_CONTINUE;
}

static void on_export_wav(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    if (active_wav_export) {
        set_status("A WAV export is already running");
        return;
    }
    if (playback_mode == PLAYBACK_NONE || !current_file[0]) {
        set_status("Open a music file before exporting it");
        return;
    }

    ExportWavFormat format = EXPORT_WAV_FFMPEG;
    unsigned int sid_subtune = 0;
    const char *source = current_file;
    if (playback_mode == PLAYBACK_MODULE) {
        format = EXPORT_WAV_XMP;
    } else if (playback_mode == PLAYBACK_SID) {
        format = EXPORT_WAV_SID;
        SDL_LockAudioDevice(audio_device);
        if (sid_audio) sid_subtune = sid_audio_current_song(sid_audio);
        SDL_UnlockAudioDevice(audio_device);
    } else if (playback_mode == PLAYBACK_MIDI) {
        format = EXPORT_WAV_MIDI;
    } else if (playback_mode == PLAYBACK_ZX ||
               (playback_mode == PLAYBACK_FFMPEG && zx_active_wav)) {
        format = EXPORT_WAV_ZXTUNE;
        if (zx_active_source) source = zx_active_source;
    }

    GtkWidget *chooser = gtk_file_chooser_dialog_new(
        "Export audio to WAV", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Export", GTK_RESPONSE_ACCEPT, NULL);
    theme_dialog(chooser);

    gchar *base = g_path_get_basename(source);
    char *extension = strrchr(base, '.');
    if (extension) *extension = '\0';
    gchar *suggested = g_strconcat(base, ".wav", NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), suggested);
    g_free(suggested);
    g_free(base);

    gchar *destination = NULL;
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
        destination = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    gtk_widget_destroy(chooser);
    if (!destination) return;

    const char *dot = strrchr(destination, '.');
    if (!dot || g_ascii_strcasecmp(dot, ".wav") != 0) {
        gchar *with_extension = g_strconcat(destination, ".wav", NULL);
        g_free(destination);
        destination = with_extension;
    }

    /* Confirm the final path, including any appended .wav extension. The
     * file chooser may only have checked the name without that extension. */
    if (g_file_test(destination, G_FILE_TEST_EXISTS) ||
        g_file_test(destination, G_FILE_TEST_IS_SYMLINK)) {
        GtkWidget *replace_dialog = gtk_message_dialog_new(
            GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
            "This file already exists:\n%s\n\nReplace it with the exported WAV?",
            destination);
        theme_dialog(replace_dialog);
        gtk_dialog_add_buttons(GTK_DIALOG(replace_dialog),
                               "_Cancel", GTK_RESPONSE_CANCEL,
                               "_Replace", GTK_RESPONSE_ACCEPT, NULL);
        gboolean replace = gtk_dialog_run(GTK_DIALOG(replace_dialog)) ==
                           GTK_RESPONSE_ACCEPT;
        gtk_widget_destroy(replace_dialog);
        if (!replace) {
            g_free(destination);
            return;
        }
    }

    unsigned int sid_seconds = 180;
    if (format == EXPORT_WAV_SID) {
        GtkWidget *duration_dialog = gtk_dialog_new_with_buttons(
            "SID export duration", GTK_WINDOW(window), GTK_DIALOG_MODAL,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Continue", GTK_RESPONSE_ACCEPT, NULL);
        theme_dialog(duration_dialog);
        GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(duration_dialog));
        GtkWidget *description = gtk_label_new(
            "SID files do not contain a fixed duration.\n"
            "How many seconds should be exported?");
        GtkWidget *seconds = gtk_spin_button_new_with_range(1, 3600, 5);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(seconds), 180);
        gtk_box_pack_start(GTK_BOX(area), description, FALSE, FALSE, 8);
        gtk_box_pack_start(GTK_BOX(area), seconds, FALSE, FALSE, 8);
        gtk_widget_show_all(duration_dialog);
        gboolean accepted = gtk_dialog_run(GTK_DIALOG(duration_dialog)) ==
                            GTK_RESPONSE_ACCEPT;
        if (accepted)
            sid_seconds = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(seconds));
        gtk_widget_destroy(duration_dialog);
        if (!accepted) {
            g_free(destination);
            return;
        }
    }

    WavExportTask *task = g_new0(WavExportTask, 1);
    task->source = g_strdup(source);
    task->destination = destination;
    task->request.source = task->source;
    task->request.destination = task->destination;
    task->request.format = format;
    task->request.sid_seconds = sid_seconds;
    task->request.sid_subtune = sid_subtune;
    task->request.soundfont = NULL;
    if (format == EXPORT_WAV_MIDI) {
        /* Keep a private copy: the SoundFonts tab may change while exporting. */
        task->request.soundfont = g_strdup(midi_audio_soundfont(midi_audio));
    }
    task->dialog = gtk_dialog_new_with_buttons(
        "Exporting WAV", GTK_WINDOW(window), GTK_DIALOG_MODAL,
        "_Cancel Export", GTK_RESPONSE_CANCEL, NULL);
    theme_dialog(task->dialog);
    gtk_window_set_default_size(GTK_WINDOW(task->dialog), 420, -1);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(task->dialog));
    GtkWidget *description = gtk_label_new(
        "Rendering the music as fast as possible, without playback.");
    gtk_box_pack_start(GTK_BOX(area), description, FALSE, FALSE, 8);
    task->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(task->progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(task->progress_bar), "Starting…");
    gtk_box_pack_start(GTK_BOX(area), task->progress_bar, FALSE, FALSE, 8);
    g_signal_connect(task->dialog, "response",
                     G_CALLBACK(on_wav_export_response), task);
    g_signal_connect(task->dialog, "delete-event",
                     G_CALLBACK(on_wav_export_delete), task);

    active_wav_export = task;
    task->thread = g_thread_new("wav-export", wav_export_worker, task);
    task->progress_timer = g_timeout_add(100, update_wav_export_dialog, task);
    gtk_widget_show_all(task->dialog);
    set_status("Exporting audio to WAV in the background");
}
#endif

static void on_play(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    if (playback_mode == PLAYBACK_NONE && zx_pending) {
        zx_pending->autoplay = 1;
        set_status("Will play after ZX rendering finishes");
        return;
    }
    if (playback_mode == PLAYBACK_NONE)
        return;

    if (playback_mode == PLAYBACK_FFMPEG &&
        (stopped || audio_ended || !ffmpeg_audio)) {
        char filename[sizeof(current_file)];
        g_strlcpy(filename, zx_active_wav ? zx_active_wav : current_file,
                  sizeof(filename));
        if (!filename[0] || !open_ffmpeg_audio(filename))
            return;
        if (zx_active_wav)
            zx_apply_labels();
    }
#ifdef ENABLE_ZXTUNE_NATIVE
    if (playback_mode == PLAYBACK_ZX &&
        (stopped || audio_ended || !zx_audio)) {
        char filename[sizeof(current_file)];
        g_strlcpy(filename, current_file, sizeof(filename));
        if (!filename[0] || !open_zx_native(filename))
            return;
    }
#endif

    if (playing) {
        SDL_PauseAudioDevice(audio_device, 1);
        playing = 0;
        set_status("Paused");
    } else if (playback_mode == PLAYBACK_MODULE) {
        SDL_LockAudioDevice(audio_device);
        if (stopped || audio_ended) {
            xmp_restart_module(ctx);
            stopped = 0;
            audio_ended = 0;
        }
        playing = 1;
        SDL_UnlockAudioDevice(audio_device);
        SDL_PauseAudioDevice(audio_device, 0);
        set_status("Playing");
    } else if (playback_mode == PLAYBACK_SID) {
        SDL_LockAudioDevice(audio_device);
        if (stopped || audio_ended) {
            unsigned int song = sid_audio_current_song(sid_audio);
            if (!sid_audio || sid_audio_select_song(sid_audio, song) < 0) {
                SDL_UnlockAudioDevice(audio_device);
                set_status("Could not restart SID tune.");
                return;
            }
            stopped = 0;
            audio_ended = 0;
        }
        playing = 1;
        SDL_UnlockAudioDevice(audio_device);
        SDL_PauseAudioDevice(audio_device, 0);
        set_status("Playing SID tune");
    } else if (playback_mode == PLAYBACK_MIDI) {
        SDL_LockAudioDevice(audio_device);
        if (stopped || audio_ended) {
            if (!midi_audio || !midi_audio_restart(midi_audio)) {
                SDL_UnlockAudioDevice(audio_device);
                set_status("Could not restart MIDI file");
                return;
            }
        }
        stopped = 0;
        audio_ended = 0;
        playing = 1;
        SDL_UnlockAudioDevice(audio_device);
        SDL_PauseAudioDevice(audio_device, 0);
        set_status("Playing MIDI file");
    } else if (playback_mode == PLAYBACK_FFMPEG) {
        stopped = 0;
        audio_ended = 0;
        playing = 1;
        SDL_PauseAudioDevice(audio_device, 0);
        set_status("Playing");
    } else if (playback_mode == PLAYBACK_ZX) {
        stopped = 0;
        audio_ended = 0;
        playing = 1;
        SDL_PauseAudioDevice(audio_device, 0);
        set_status("Playing ZX Spectrum tune");
    }

    update_play_button();
}

static void on_stop(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    if (playback_mode == PLAYBACK_NONE && zx_pending) {
        zx_cancel_pending();
        gtk_label_set_text(GTK_LABEL(title_label), "No module loaded");
        set_status("ZX rendering cancelled");
        return;
    }
    if (playback_mode == PLAYBACK_NONE)
        return;

    SDL_PauseAudioDevice(audio_device, 1);

    if (playback_mode == PLAYBACK_MODULE) {
        SDL_LockAudioDevice(audio_device);
        xmp_stop_module(ctx);
        SDL_UnlockAudioDevice(audio_device);
    } else if (playback_mode == PLAYBACK_FFMPEG ||
               playback_mode == PLAYBACK_ZX) {
        stop_ffmpeg_decoder();
    } else if (playback_mode == PLAYBACK_SID && sid_audio) {
        SDL_LockAudioDevice(audio_device);
        sid_audio_select_song(sid_audio, sid_audio_current_song(sid_audio));
        SDL_UnlockAudioDevice(audio_device);
    } else if (playback_mode == PLAYBACK_MIDI && midi_audio) {
        SDL_LockAudioDevice(audio_device);
        midi_audio_restart(midi_audio);
        SDL_UnlockAudioDevice(audio_device);
    }

    SDL_LockAudioDevice(audio_device);
    xmp_eq_reset_history(&equalizer);
    SDL_UnlockAudioDevice(audio_device);

    playing = 0;
    stopped = 1;
    audio_ended = 0;

    update_play_button();
    set_status("Stopped");
}

static void on_previous(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    if (playback_mode == PLAYBACK_MIDI && midi_audio) {
        SDL_LockAudioDevice(audio_device);
        midi_audio_restart(midi_audio);
        audio_ended = 0;
        stopped = 0;
        SDL_UnlockAudioDevice(audio_device);
        return;
    }
    if (playback_mode == PLAYBACK_SID && sid_audio) {
        SDL_LockAudioDevice(audio_device);
        unsigned int count = sid_audio_song_count(sid_audio);
        unsigned int current = sid_audio_current_song(sid_audio);
        unsigned int target = (count > 0 && current > 1) ? current - 1 : count;
        if (count > 0 && sid_audio_select_song(sid_audio, target) > 0) {
            stopped = 0;
            audio_ended = 0;
        }
        SDL_UnlockAudioDevice(audio_device);
        return;
    }
    if (zx_active_wav && playback_mode == PLAYBACK_FFMPEG && ffmpeg_audio) {
        SDL_LockAudioDevice(audio_device);
        ffmpeg_seek_target_ms = 0;
        SDL_AtomicAdd(&ffmpeg_seek_generation, 1);
        SDL_AtomicSet(&ffmpeg_seek_requested, 1);
        audio_ended = 0;
        SDL_UnlockAudioDevice(audio_device);
        return;
    }
#ifdef ENABLE_ZXTUNE_NATIVE
    if (playback_mode == PLAYBACK_ZX && zx_audio) {
        SDL_LockAudioDevice(audio_device);
        ffmpeg_seek_target_ms = 0;
        SDL_AtomicAdd(&ffmpeg_seek_generation, 1);
        SDL_AtomicSet(&ffmpeg_seek_requested, 1);
        audio_ended = 0;
        SDL_UnlockAudioDevice(audio_device);
        return;
    }
#endif
    if (!module_loaded) return;

    SDL_LockAudioDevice(audio_device);
    xmp_prev_position(ctx);
    SDL_UnlockAudioDevice(audio_device);
}

static void on_next(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    if (playback_mode == PLAYBACK_MIDI && midi_audio) {
        SDL_LockAudioDevice(audio_device);
        midi_audio_seek(midi_audio, midi_audio_duration_ms(midi_audio));
        audio_ended = 0;
        SDL_UnlockAudioDevice(audio_device);
        return;
    }
    if (playback_mode == PLAYBACK_SID && sid_audio) {
        SDL_LockAudioDevice(audio_device);
        unsigned int count = sid_audio_song_count(sid_audio);
        unsigned int current = sid_audio_current_song(sid_audio);
        unsigned int target = (count > 0 && current < count) ? current + 1 : 1;
        if (count > 0 && sid_audio_select_song(sid_audio, target) > 0) {
            stopped = 0;
            audio_ended = 0;
        }
        SDL_UnlockAudioDevice(audio_device);
        return;
    }
    if (zx_active_wav && playback_mode == PLAYBACK_FFMPEG && ffmpeg_audio) {
        SDL_LockAudioDevice(audio_device);
        ffmpeg_seek_target_ms = ffmpeg_duration_ms;
        SDL_AtomicAdd(&ffmpeg_seek_generation, 1);
        SDL_AtomicSet(&ffmpeg_seek_requested, 1);
        SDL_UnlockAudioDevice(audio_device);
        return;
    }
#ifdef ENABLE_ZXTUNE_NATIVE
    if (playback_mode == PLAYBACK_ZX && zx_audio) {
        SDL_LockAudioDevice(audio_device);
        ffmpeg_seek_target_ms = ffmpeg_duration_ms;
        SDL_AtomicAdd(&ffmpeg_seek_generation, 1);
        SDL_AtomicSet(&ffmpeg_seek_requested, 1);
        SDL_UnlockAudioDevice(audio_device);
        return;
    }
#endif
    if (!module_loaded) return;

    SDL_LockAudioDevice(audio_device);
    xmp_next_position(ctx);
    SDL_UnlockAudioDevice(audio_device);
}

static void on_seek(GtkRange *range, gpointer data)
{
    (void)data;

    if (updating_seek)
        return;

    int ms = (int)gtk_range_get_value(range);

    if (playback_mode == PLAYBACK_MODULE && module_loaded) {
        SDL_LockAudioDevice(audio_device);
        xmp_seek_time(ctx, ms);
        audio_ended = 0;
        stopped = 0;
        SDL_UnlockAudioDevice(audio_device);
    } else if (playback_mode == PLAYBACK_MIDI && midi_audio) {
        SDL_LockAudioDevice(audio_device);
        if (audio_ended)
            midi_audio_restart(midi_audio);
        if (midi_audio_seek(midi_audio, ms)) {
            audio_ended = 0;
            stopped = 0;
        }
        SDL_UnlockAudioDevice(audio_device);
    } else if ((playback_mode == PLAYBACK_FFMPEG && ffmpeg_audio)
#ifdef ENABLE_ZXTUNE_NATIVE
               || (playback_mode == PLAYBACK_ZX && zx_audio)
#endif
               ) {
        SDL_LockAudioDevice(audio_device);
        ffmpeg_seek_target_ms = ms;
        SDL_AtomicAdd(&ffmpeg_seek_generation, 1);
        SDL_AtomicSet(&ffmpeg_seek_requested, 1);
        audio_ended = 0;
        stopped = 0;
        SDL_UnlockAudioDevice(audio_device);
    }
}




static gboolean draw_visualizer(GtkWidget *widget, cairo_t *cr,
                                gpointer data)
{
    (void)data;
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    double w = a.width, h = a.height;
    double baseline = h - 8.0;
    double max_height = h - 22.0;

    cairo_set_source_rgb(cr, 0.022, 0.060, 0.075);
    cairo_paint(cr);

    /* Subtle horizontal guides for the rising spectrum. */
    cairo_set_line_width(cr, 1.0);
    for (int i = 1; i <= 4; i++) {
        double y = baseline - max_height * i / 4.0;
        cairo_set_source_rgba(cr, 0.30, 0.48, 0.68, 0.14);
        cairo_move_to(cr, 7, y);
        cairo_line_to(cr, w - 7, y);
        cairo_stroke(cr);
    }

    double gap = 2.0;
    double bar_width = (w - 20.0 - gap * (VIZ_BANDS - 1)) / VIZ_BANDS;
    if (bar_width < 1.0)
        bar_width = 1.0;

    for (int i = 0; i < VIZ_BANDS; i++) {
        double level = viz_levels[i];
        double peak = viz_peaks[i];
        if (level < 0.0) level = 0.0;
        if (level > 1.0) level = 1.0;
        if (peak < level) peak = level;
        if (peak > 1.0) peak = 1.0;

        double x = 10.0 + i * (bar_width + gap);
        double bh = level * max_height;
        double ph = peak * max_height;
        double t = (double)i / (VIZ_BANDS - 1);

        /* Cyan through blue into violet. */
        double r = 0.08 + 0.48 * t;
        double g = 0.86 - 0.52 * t;
        double b = 1.0;

        /* Diffuse glow behind each bar. */
        cairo_set_source_rgba(cr, r, g, b, 0.20);
        cairo_rectangle(cr, x - 1.0, baseline - bh - 2.0,
                        bar_width + 2.0, bh + 4.0);
        cairo_fill(cr);

        /* Bright vertical gradient, rising from the baseline. */
        cairo_pattern_t *grad = cairo_pattern_create_linear(
            x, baseline - max_height, x, baseline);
        cairo_pattern_add_color_stop_rgba(grad, 0.0, r, g, b, 0.68);
        cairo_pattern_add_color_stop_rgba(grad, 1.0, r, g, b, 1.0);
        cairo_set_source(cr, grad);
        cairo_rectangle(cr, x, baseline - bh, bar_width, bh);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);

        /* Bright peak-hold marker above each bar. */
        cairo_set_source_rgba(cr, 0.78, 0.95, 1.0, 0.96);
        cairo_rectangle(cr, x, baseline - ph - 1.0,
                        bar_width, 2.0);
        cairo_fill(cr);
    }

    /* Crisp baseline. */
    cairo_set_source_rgba(cr, 0.35, 0.80, 1.0, 0.75);
    cairo_set_line_width(cr, 1.2);
    cairo_move_to(cr, 8, baseline);
    cairo_line_to(cr, w - 8, baseline);
    cairo_stroke(cr);

    return FALSE;
}

static void update_visualizer_spectrum(void)
{
    /* Let the bars and peak markers settle while playback is stopped. */
    if ((playback_mode != PLAYBACK_ZX &&
         playback_mode != PLAYBACK_MODULE &&
         playback_mode != PLAYBACK_FFMPEG &&
         playback_mode != PLAYBACK_MIDI &&
         playback_mode != PLAYBACK_SID) ||
        !playing || audio_ended) {
        for (int i = 0; i < VIZ_BANDS; i++) {
            viz_levels[i] *= 0.82;
            viz_peaks[i] *= 0.96;
        }
        return;
    }

    double re[VIZ_FFT_SIZE] = {0};
    double im[VIZ_FFT_SIZE] = {0};
    int16_t samples[VIZ_FFT_SIZE];

    /* Copy the ring while the audio callback is paused briefly. */
    SDL_LockAudioDevice(audio_device);
    unsigned int end = viz_write_pos;
    for (int i = 0; i < VIZ_FFT_SIZE; i++) {
        unsigned int pos = (end + VIZ_RING_SIZE - VIZ_FFT_SIZE + i)
                           % VIZ_RING_SIZE;
        samples[i] = viz_ring[pos];
    }
    SDL_UnlockAudioDevice(audio_device);

    /* Hann window. */
    for (int i = 0; i < VIZ_FFT_SIZE; i++) {
        double window = 0.5 - 0.5 * cos(
            2.0 * G_PI * i / (VIZ_FFT_SIZE - 1));
        re[i] = (samples[i] / 32768.0) * window;
    }

    /* Iterative radix-2 Cooley-Tukey FFT. */
    for (unsigned int i = 1, j = 0; i < VIZ_FFT_SIZE; i++) {
        unsigned int bit = VIZ_FFT_SIZE >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double tmp = re[i]; re[i] = re[j]; re[j] = tmp;
            tmp = im[i]; im[i] = im[j]; im[j] = tmp;
        }
    }

    for (unsigned int len = 2; len <= VIZ_FFT_SIZE; len <<= 1) {
        double angle = -2.0 * G_PI / len;
        double wr0 = cos(angle), wi0 = sin(angle);
        for (unsigned int i = 0; i < VIZ_FFT_SIZE; i += len) {
            double wr = 1.0, wi = 0.0;
            for (unsigned int j = 0; j < len / 2; j++) {
                unsigned int u = i + j, v = u + len / 2;
                double vr = re[v] * wr - im[v] * wi;
                double vi = re[v] * wi + im[v] * wr;
                re[v] = re[u] - vr;
                im[v] = im[u] - vi;
                re[u] += vr;
                im[u] += vi;
                double next_wr = wr * wr0 - wi * wi0;
                wi = wr * wi0 + wi * wr0;
                wr = next_wr;
            }
        }
    }

    /* Log-ish frequency bands, with smooth attack and decay. */
    for (int band = 0; band < VIZ_BANDS; band++) {
        int low = 2 + (int)(pow((VIZ_FFT_SIZE / 2.0) / 2.0,
                                (double)band / VIZ_BANDS));
        int high = 2 + (int)(pow((VIZ_FFT_SIZE / 2.0) / 2.0,
                                 (double)(band + 1) / VIZ_BANDS));
        if (high <= low) high = low + 1;
        if (high > VIZ_FFT_SIZE / 2) high = VIZ_FFT_SIZE / 2;

        double sum = 0.0;
        int count = 0;
        for (int k = low; k < high; k++) {
            sum += hypot(re[k], im[k]);
            count++;
        }
        double magnitude = count ? sum / count : 0.0;
        double raw_level = magnitude * 0.25;
        /* Soft compression: loud signals rise more gradually. */
        double target = raw_level / (1.0 + raw_level);

        if (target > viz_levels[band])
            viz_levels[band] += (target - viz_levels[band]) * 0.65;
        else
            viz_levels[band] *= 0.82;

        if (viz_levels[band] > viz_peaks[band])
            viz_peaks[band] = viz_levels[band];
        else
            viz_peaks[band] *= 0.96;
    }
}

static gboolean draw_scopes(GtkWidget *widget, cairo_t *cr,
                            gpointer data)
{
    (void)data;

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    const int width = allocation.width;
    const int height = allocation.height;

    cairo_set_source_rgb(cr, 0.028, 0.060, 0.075);
    cairo_paint(cr);

    if (((zx_active_wav && playback_mode == PLAYBACK_FFMPEG) ||
         playback_mode == PLAYBACK_ZX || playback_mode == PLAYBACK_MIDI) &&
        width >= 80 && height >= 20) {
        /* Both ZX backends expose mixed output. Do not represent this
         * waveform as separate AY chip channels. */
        int16_t waveform[2048];
        SDL_LockAudioDevice(audio_device);
        unsigned int start = (viz_write_pos + VIZ_RING_SIZE - 2048) %
                             VIZ_RING_SIZE;
        for (unsigned i = 0; i < 2048; ++i)
            waveform[i] = viz_ring[(start + i) % VIZ_RING_SIZE];
        SDL_UnlockAudioDevice(audio_device);

        cairo_set_source_rgb(cr, 0.73, 0.78, 0.84);
        cairo_move_to(cr, 12, 17);
        cairo_show_text(cr, playback_mode == PLAYBACK_MIDI
                              ? "MIDI - mixed audio waveform"
                              : "ZX Spectrum - mixed audio waveform");
        cairo_set_source_rgb(cr, 0.35, 0.90, 0.65);
        cairo_set_line_width(cr, 1.0);
        for (int x = 0; x < width; ++x) {
            int idx = (int)((int64_t)x * 2048 / width);
            double y = height * 0.56 - waveform[idx] / 32768.0 *
                       (height * 0.35);
            if (x == 0)
                cairo_move_to(cr, x, y);
            else
                cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
        return FALSE;
    }

    if (!module_loaded || !player_started || width < 80 || height < 20)
        return FALSE;

    /*
     * The SDL lock protects libxmp while we inspect playback state and
     * module samples. Keep the drawing work short.
     */
    SDL_LockAudioDevice(audio_device);

    struct xmp_module_info mi;
    struct xmp_frame_info fi;
    xmp_get_module_info(ctx, &mi);
    xmp_get_frame_info(ctx, &fi);

    struct xmp_module *mod = mi.mod;
    if (!mod) {
        SDL_UnlockAudioDevice(audio_device);
        return FALSE;
    }

    const int channels = mod->chn;
    const int row_height = SCOPE_ROW_HEIGHT;
    const int plot_left = 42;
    const int plot_right = width - 12;
    const int plot_width = plot_right - plot_left;

    for (int ch = 0; ch < channels; ++ch) {
        int y = ch * row_height;
        if (y >= height)
            break;

        struct xmp_channel_info *ci = &fi.channel_info[ch];

        /* Channel panel */
        cairo_set_source_rgb(cr, 0.105, 0.120, 0.145);
        cairo_rectangle(cr, 4, y + 3, width - 8, row_height - 6);
        cairo_fill(cr);

        /* Channel number */
        char label[64];
        snprintf(label, sizeof(label), "%02d", ch + 1);
        cairo_select_font_face(cr, "Monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgb(cr, 0.88, 0.91, 0.95);
        cairo_move_to(cr, 12, y + 22);
        cairo_show_text(cr, label);

        /* Waveform background */
        const double top = y + 9;
        const double bottom = y + 49;
        const double mid = (top + bottom) / 2.0;

        cairo_set_source_rgb(cr, 0.022, 0.050, 0.065);
        cairo_rectangle(cr, plot_left, top, plot_width, bottom - top);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.18, 0.23, 0.22);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, plot_left, mid);
        cairo_line_to(cr, plot_right, mid);
        cairo_stroke(cr);

        /*
         * ci->sample is documented as the current sample number.
         * Treat positive values as one-based, and validate before access.
         */
        int sample_index = ci->sample > 0
                         ? (int)ci->sample - 1 : -1;

        if (sample_index >= 0 && sample_index < mod->smp &&
            mod->xxs && plot_width > 1) {
            struct xmp_sample *sp = &mod->xxs[sample_index];

            if (sp->data && sp->len > 0) {
                int sample_count = sp->len;
                int stereo = !!(sp->flg & XMP_SAMPLE_STEREO);
                int stride = stereo ? 2 : 1;
                unsigned int position = ci->position;
                int looping = !!(sp->flg & XMP_SAMPLE_LOOP);
                int loop_start = sp->lps;
                int loop_end = sp->lpe;

                if (position >= (unsigned int)sample_count)
                    position = (unsigned int)(sample_count - 1);

                /*
                 * Display a short moving section of the current sample.
                 * The loop bounds are used only when they are valid.
                 */
                int valid_loop = looping &&
                                 loop_start >= 0 &&
                                 loop_end > loop_start &&
                                 loop_end <= sample_count;

                int window_samples = 512;
                int step = window_samples / plot_width;
                if (step < 1)
                    step = 1;

                cairo_set_source_rgb(cr, 0.20, 0.96, 0.48);
                cairo_set_line_width(cr, 1.15);

                for (int x = 0; x < plot_width; ++x) {
                    long sample_pos = (long)position + (long)x * step;

                    if (sample_pos >= sample_count) {
                        if (valid_loop) {
                            long loop_len = loop_end - loop_start;
                            sample_pos = loop_start +
                                (sample_pos - loop_start) % loop_len;
                        } else {
                            sample_pos = sample_count - 1;
                        }
                    }

                    if (valid_loop && sample_pos >= loop_end) {
                        long loop_len = loop_end - loop_start;
                        sample_pos = loop_start +
                            (sample_pos - loop_start) % loop_len;
                    }

                    if (sample_pos < 0 || sample_pos >= sample_count)
                        continue;

                    long data_index = sample_pos * stride;
                    double amplitude;

                    if (sp->flg & XMP_SAMPLE_16BIT) {
                        const int16_t *pcm = (const int16_t *)sp->data;
                        amplitude = pcm[data_index] / 32768.0;
                    } else {
                        const int8_t *pcm = (const int8_t *)sp->data;
                        amplitude = pcm[data_index] / 128.0;
                    }

                    double py = mid - amplitude * 17.0;
                    if (x == 0)
                        cairo_move_to(cr, plot_left + x, py);
                    else
                        cairo_line_to(cr, plot_left + x, py);
                }
                cairo_stroke(cr);
            }
        }

        /* Volume meter and channel metadata */
        int volume = ci->volume;
        if (volume < 0) volume = 0;
        if (volume > 64) volume = 64;

        char info[96];
        snprintf(info, sizeof(info), "VOL %02d   PAN %03d",
                 volume, ci->pan);

        cairo_set_font_size(cr, 10);
        cairo_set_source_rgb(cr, 0.68, 0.72, 0.78);
        cairo_move_to(cr, plot_left, y + 64);
        cairo_show_text(cr, info);

        cairo_set_source_rgb(cr, 0.12, 0.16, 0.18);
        cairo_rectangle(cr, plot_right - 100, y + 55, 100, 8);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.20, 0.55, 0.95);
        cairo_rectangle(cr, plot_right - 100, y + 55,
                        100.0 * volume / 64.0, 8);
        cairo_fill(cr);
    }

    SDL_UnlockAudioDevice(audio_device);
    return FALSE;
}

static void play_playlist_file(const char *filename);

static void on_file_drop(GtkWidget *widget,
                         GdkDragContext *context,
                         gint x, gint y,
                         GtkSelectionData *selection_data,
                         guint info, guint time,
                         gpointer data)
{
    (void)widget;
    (void)x;
    (void)y;
    (void)info;
    (void)data;

    gboolean success = FALSE;
    gchar **uris = gtk_selection_data_get_uris(selection_data);

    if (uris) {
        GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

        for (gint i = 0; uris[i] != NULL; i++) {
            gchar *filename = g_filename_from_uri(uris[i], NULL, NULL);
            if (filename)
                g_ptr_array_add(files, filename);
        }

        if (files->len == 1) {
            const gchar *filename = g_ptr_array_index(files, 0);
            play_playlist_file(filename);
            success = TRUE;
        } else if (files->len > 1) {
            for (guint i = 0; i < files->len; i++) {
                const gchar *filename = g_ptr_array_index(files, i);
                gchar *base = g_path_get_basename(filename);
                GtkTreeIter iter;

                gtk_list_store_append(playlist_store, &iter);
                gtk_list_store_set(playlist_store, &iter,
                                   0, base, 1, filename, -1);
                g_free(base);
            }

            set_status("Files added to playlist");
            success = TRUE;
        }

        g_ptr_array_free(files, TRUE);
        g_strfreev(uris);
    }

    gtk_drag_finish(context, success, FALSE, time);
}

static void play_playlist_file(const char *filename)
{
    if (!filename || !*filename)
        return;

    if (is_midi_path(filename)) {
        if (open_midi(filename))
            on_play(NULL, NULL);
        return;
    }

    zx_autoplay_request = 1;
    open_module(filename);
    zx_autoplay_request = 0;

    if (zx_is_music_path(filename))
        return;

    if (module_loaded) {
        on_play(NULL, NULL);
        return;
    }

    if (open_sid(filename)) {
        on_play(NULL, NULL);
        return;
    }

    if (open_ffmpeg_audio(filename))
        on_play(NULL, NULL);
}


static void on_playlist_load_m3u(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Load M3U Playlist", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);
    theme_dialog(dialog);
    restore_last_folder(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }
    remember_last_folder(dialog);
    char *filename = gtk_file_chooser_get_filename(
        GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);

    gchar *contents = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (!g_file_get_contents(filename, &contents, &length, &error)) {
        GtkWidget *message = gtk_message_dialog_new(
            GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Could not load playlist: %s", error->message);
        theme_dialog(message);
        gtk_dialog_run(GTK_DIALOG(message));
        gtk_widget_destroy(message);
        g_error_free(error);
        g_free(filename);
        return;
    }

    char *directory = g_path_get_dirname(filename);
    gchar **lines = g_strsplit(contents, "\n", -1);

    gtk_list_store_clear(playlist_store);

    for (guint i = 0; lines[i] != NULL; i++) {
        char *line = g_strstrip(lines[i]);

        if (!*line || line[0] == '#')
            continue;

        char *path = NULL;

        if (g_str_has_prefix(line, "file://"))
            path = g_filename_from_uri(line, NULL, NULL);

        if (!path) {
            path = g_path_is_absolute(line)
                ? g_strdup(line)
                : g_build_filename(directory, line, NULL);
        }

        char *base = g_path_get_basename(path);
        GtkTreeIter iter;
        gtk_list_store_append(playlist_store, &iter);
        gtk_list_store_set(playlist_store, &iter,
                           0, base, 1, path, -1);
        g_free(base);
        g_free(path);
    }

    g_strfreev(lines);
    g_free(directory);
    g_free(contents);
    g_free(filename);
}

static void on_playlist_save_m3u(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Save M3U Playlist", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT, NULL);
    theme_dialog(dialog);

    gtk_file_chooser_set_do_overwrite_confirmation(
        GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(
        GTK_FILE_CHOOSER(dialog), "playlist.m3u");
    restore_last_folder(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }
    remember_last_folder(dialog);
    char *filename = gtk_file_chooser_get_filename(
        GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);

    GString *contents = g_string_new("#EXTM3U\n");
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(
        GTK_TREE_MODEL(playlist_store), &iter);

    while (valid) {
        char *path = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(playlist_store), &iter,
                           1, &path, -1);
        if (path) {
            g_string_append(contents, path);
            g_string_append_c(contents, '\n');
            g_free(path);
        }
        valid = gtk_tree_model_iter_next(
            GTK_TREE_MODEL(playlist_store), &iter);
    }

    GError *error = NULL;
    if (!g_file_set_contents(filename, contents->str,
                             contents->len, &error)) {
        GtkWidget *message = gtk_message_dialog_new(
            GTK_WINDOW(window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Could not save playlist: %s", error->message);
        theme_dialog(message);
        gtk_dialog_run(GTK_DIALOG(message));
        gtk_widget_destroy(message);
        g_error_free(error);
    }

    g_string_free(contents, TRUE);
    g_free(filename);
}

static void on_playlist_add(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Add modules to playlist", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Add", GTK_RESPONSE_ACCEPT, NULL);
    theme_dialog(dialog);
    restore_last_folder(dialog);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio and tracker files");

    const char *patterns[] = {
        "*.mod", "*.MOD", "*.xm", "*.XM", "*.it", "*.IT",
        "*.mid", "*.MID", "*.midi", "*.MIDI", "*.kar", "*.KAR",
        "*.s3m", "*.S3M", "*.mptm", "*.MPTM", "*.mtm", "*.MTM",
        "*.669", "*.amf", "*.med", "*.okt", "*.ult", "*.sid", "*.SID",
        "*.mp3", "*.MP3", "*.wav", "*.WAV", "*.m4a", "*.M4A",
        "*.aac", "*.AAC", "*.ogg", "*.OGG", "*.oga", "*.OGA", "*.opus", "*.OPUS",
        "*.flac", "*.FLAC", "*.webm", "*.WEBM", "*.mka", "*.MKA",
        "*.aiff", "*.aif", "*.wma", "*.mp4", "*.MP4",
        "*.pt3", "*.PT3", "*.pt2", "*.PT2", "*.ay", "*.AY",
        "*.vtx", "*.VTX", "*.ym", "*.YM", "*.stc", "*.STC",
        "*.sqt", "*.SQT", "*.psg", "*.PSG", "*.asc", "*.ASC"
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
        gtk_file_filter_add_pattern(filter, patterns[i]);

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All files");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        remember_last_folder(dialog);
        GSList *files =
            gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));

        for (GSList *item = files; item; item = item->next) {
            char *filename = item->data;
            char *base = g_path_get_basename(filename);
            GtkTreeIter iter;

            gtk_list_store_append(playlist_store, &iter);
            gtk_list_store_set(playlist_store, &iter,
                               0, base, 1, filename, -1);
            g_free(base);
        }

        g_slist_free_full(files, g_free);
    }

    gtk_widget_destroy(dialog);
}

static void on_playlist_remove(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(playlist_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
}

static void on_playlist_clear(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    gtk_list_store_clear(playlist_store);
}

static void on_playlist_activate(GtkTreeView *view, GtkTreePath *path,
                                 GtkTreeViewColumn *column, gpointer data)
{
    (void)view;
    (void)column;
    (void)data;

    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(playlist_store),
                                 &iter, path))
        return;

    char *filename = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(playlist_store), &iter,
                       1, &filename, -1);

    if (filename) {
        play_playlist_file(filename);
        g_free(filename);
    }
}

static gboolean playlist_advance(void)
{
    if (!playlist_store || !current_file[0])
        return FALSE;

    GtkTreeModel *model = GTK_TREE_MODEL(playlist_store);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

    while (valid) {
        char *filename = NULL;
        gtk_tree_model_get(model, &iter, 1, &filename, -1);

        gboolean matches = filename &&
                           g_strcmp0(filename, current_file) == 0;
        g_free(filename);

        if (matches) {
            if (!gtk_tree_model_iter_next(model, &iter))
                return FALSE;

            char *next_file = NULL;
            gtk_tree_model_get(model, &iter, 1, &next_file, -1);

            if (next_file) {
                play_playlist_file(next_file);
                g_free(next_file);
                return (playback_mode != PLAYBACK_NONE && playing) ||
                       zx_pending != NULL;
            }
            return FALSE;
        }

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    return FALSE;
}



static gboolean draw_pattern(GtkWidget *widget, cairo_t *cr,
                             gpointer data)
{
    (void)data;

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int width = allocation.width;
    int height = allocation.height;

    cairo_set_source_rgb(cr, 0.028, 0.060, 0.075);
    cairo_paint(cr);

    cairo_select_font_face(cr, "Monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);

    if (!module_loaded) {
        cairo_set_source_rgb(cr, 0.70, 0.73, 0.78);
        cairo_move_to(cr, 12, 24);
        cairo_show_text(cr, zx_active_source ?
                        "ZX tunes do not expose XMP pattern data." :
                        "Load a module to view its pattern.");
        return FALSE;
    }

    SDL_LockAudioDevice(audio_device);

    struct xmp_module_info mi;
    struct xmp_frame_info fi;
    xmp_get_module_info(ctx, &mi);
    xmp_get_frame_info(ctx, &fi);

    struct xmp_module *mod = mi.mod;
    int display_pattern = fi.pattern;
    int display_row = fi.row;

    if (!mod || !mod->xxp || !mod->xxt ||
        display_pattern < 0 || display_pattern >= mod->pat ||
        !mod->xxp[display_pattern]) {
        SDL_UnlockAudioDevice(audio_device);
        return FALSE;
    }

    struct xmp_pattern *pat = mod->xxp[display_pattern];
    int channels = mod->chn;
    if (channels < 1) {
        SDL_UnlockAudioDevice(audio_device);
        return FALSE;
    }

    const int left = 42;
    const int top = 36;
    const int row_height = 18;
    const int channel_width = 110;
    const int visible_rows = 13;
    const int total_rows = pat->rows;
    int first_row = display_row - visible_rows / 2;

    if (first_row < 0)
        first_row = 0;
    if (first_row > total_rows - visible_rows)
        first_row = total_rows > visible_rows
                  ? total_rows - visible_rows : 0;

    char heading[96];
    snprintf(heading, sizeof(heading),
             "PATTERN %02d     ROW %03d / %03d",
             display_pattern, display_row,
             total_rows > 0 ? total_rows - 1 : 0);
    cairo_set_source_rgb(cr, 0.88, 0.91, 0.96);
    cairo_select_font_face(cr, "Monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_move_to(cr, 10, 20);
    cairo_show_text(cr, heading);

    cairo_set_font_size(cr, 10);
    for (int ch = 0; ch < channels; ++ch) {
        int x = left + ch * channel_width;
        if (x >= width)
            break;

        cairo_set_source_rgb(cr, 0.14, 0.17, 0.21);
        cairo_rectangle(cr, x, 23, channel_width - 2, 18);
        cairo_fill(cr);

        char label[32];
        snprintf(label, sizeof(label), "CH%02d NOTE INS VOL FX", ch + 1);
        cairo_set_source_rgb(cr, 0.66, 0.78, 0.91);
        cairo_move_to(cr, x + 3, 35);
        cairo_show_text(cr, label);
    }

    static const char *notes[] = {
        "C-", "C#", "D-", "D#", "E-", "F-",
        "F#", "G-", "G#", "A-", "A#", "B-"
    };

    for (int visible = 0; visible < visible_rows; ++visible) {
        int row = first_row + visible;
        if (row >= total_rows)
            break;

        int y = top + visible * row_height;

        if (row == display_row) {
            cairo_set_source_rgb(cr, 0.13, 0.27, 0.36);
            cairo_rectangle(cr, 2, y - 13, width - 4, row_height);
            cairo_fill(cr);
        } else if ((row & 1) == 0) {
            cairo_set_source_rgb(cr, 0.037, 0.080, 0.095);
            cairo_rectangle(cr, 2, y - 13, width - 4, row_height);
            cairo_fill(cr);
        }

        char row_label[16];
        snprintf(row_label, sizeof(row_label), "%02X", row);
        cairo_set_source_rgb(cr, row == fi.row ? 1.0 : 0.60,
                             row == fi.row ? 0.88 : 0.65,
                             row == fi.row ? 0.48 : 0.72);
        cairo_move_to(cr, 8, y);
        cairo_show_text(cr, row_label);

        for (int ch = 0; ch < channels; ++ch) {
            int x = left + ch * channel_width;
            if (x + channel_width > width)
                break;

            int track_index = pat->index[ch];
            if (track_index < 0 || track_index >= mod->trk ||
                !mod->xxt[track_index])
                continue;

            struct xmp_track *track = mod->xxt[track_index];
            if (row >= track->rows)
                continue;

            struct xmp_event *ev = &track->event[row];

            char note[8] = "...";
            if (ev->note == XMP_KEY_OFF)
                snprintf(note, sizeof(note), "OFF");
            else if (ev->note == XMP_KEY_CUT)
                snprintf(note, sizeof(note), "CUT");
            else if (ev->note == XMP_KEY_FADE)
                snprintf(note, sizeof(note), "FADE");
            else if (ev->note > 0 && ev->note < XMP_KEY_OFF) {
                int n = ev->note - 1;
                snprintf(note, sizeof(note), "%s%d",
                         notes[n % 12], n / 12);
            }

            char ins[8], vol[8], fx[12];
            if (ev->ins)
                snprintf(ins, sizeof(ins), "%02X", ev->ins);
            else
                snprintf(ins, sizeof(ins), "..");

            if (ev->vol)
                snprintf(vol, sizeof(vol), "%02X", ev->vol);
            else
                snprintf(vol, sizeof(vol), "..");

            if (ev->fxt || ev->fxp)
                snprintf(fx, sizeof(fx), "%02X%02X",
                         ev->fxt, ev->fxp);
            else
                snprintf(fx, sizeof(fx), "....");

            cairo_set_source_rgb(cr, 0.85, 0.88, 0.92);
            cairo_move_to(cr, x + 3, y);
            cairo_show_text(cr, note);

            cairo_set_source_rgb(cr, 0.75, 0.83, 0.68);
            cairo_move_to(cr, x + 38, y);
            cairo_show_text(cr, ins);

            cairo_set_source_rgb(cr, 0.75, 0.75, 0.83);
            cairo_move_to(cr, x + 60, y);
            cairo_show_text(cr, vol);

            cairo_set_source_rgb(cr, 0.80, 0.69, 0.83);
            cairo_move_to(cr, x + 78, y);
            cairo_show_text(cr, fx);
        }
    }

    SDL_UnlockAudioDevice(audio_device);
    return FALSE;
}

static gboolean update_display(gpointer data)
{
    (void)data;

    if (playback_mode == PLAYBACK_NONE)
        return G_SOURCE_CONTINUE;

    int ended;
    int is_playing;
    int current;
    int total;

    if (playback_mode == PLAYBACK_FFMPEG ||
        playback_mode == PLAYBACK_ZX) {
        SDL_LockAudioDevice(audio_device);
        int64_t position_frames = ffmpeg_position_frames;
        int64_t duration_ms = ffmpeg_duration_ms;
        ended = audio_ended;
        is_playing = playing;
        SDL_UnlockAudioDevice(audio_device);

        current = (int)(position_frames * 1000 / AUDIO_RATE);
        total = duration_ms > 0 ? (int)duration_ms : 1;
        if (current < 0) current = 0;
        if (current > total) current = total;

        if (is_playing || ended) {
            updating_seek = 1;
            gtk_range_set_value(GTK_RANGE(seek_scale), current);
            updating_seek = 0;
        }

        char time_text[80];
        snprintf(time_text, sizeof(time_text), "%02d:%02d / %02d:%02d",
                 current / 60000, (current / 1000) % 60,
                 total / 60000, (total / 1000) % 60);
        gtk_label_set_text(GTK_LABEL(time_label), time_text);

        if (is_playing)
            set_status(zx_active_source ? "Playing ZX Spectrum tune" :
                       "Playing audio file");
        else if (ended)
            set_status("Playback finished");
    } else if (playback_mode == PLAYBACK_MIDI) {
        SDL_LockAudioDevice(audio_device);
        int64_t position = midi_audio ? midi_audio_position_ms(midi_audio) : 0;
        int64_t duration = midi_audio ? midi_audio_duration_ms(midi_audio) : 0;
        ended = audio_ended;
        is_playing = playing;
        SDL_UnlockAudioDevice(audio_device);

        total = duration > 0 ? (int)MIN(duration, INT_MAX) : 1;
        current = (int)MIN(position, total);
        if (is_playing || ended) {
            updating_seek = 1;
            gtk_range_set_value(GTK_RANGE(seek_scale), current);
            updating_seek = 0;
        }
        char time_text[80];
        g_snprintf(time_text, sizeof(time_text), "%02d:%02d / %02d:%02d",
                   current / 60000, (current / 1000) % 60,
                   total / 60000, (total / 1000) % 60);
        gtk_label_set_text(GTK_LABEL(time_label), time_text);
        if (is_playing)
            set_status("Playing MIDI file");
        else if (ended)
            set_status("MIDI playback finished");
    } else if (playback_mode == PLAYBACK_SID) {
        SDL_LockAudioDevice(audio_device);
        unsigned int song = sid_audio ? sid_audio_current_song(sid_audio) : 0;
        unsigned int count = sid_audio ? sid_audio_song_count(sid_audio) : 0;
        ended = audio_ended;
        is_playing = playing;
        SDL_UnlockAudioDevice(audio_device);

        char sid_status[128];
        snprintf(sid_status, sizeof(sid_status), "SID subtune %u / %u",
                 song, count);
        gtk_label_set_text(GTK_LABEL(time_label), "SID tune");
        if (is_playing)
            gtk_label_set_text(GTK_LABEL(status_label), sid_status);
        else if (ended)
            set_status("SID playback finished");
        current = 0;
        total = 1;
    } else {
        struct xmp_frame_info fi;
        SDL_LockAudioDevice(audio_device);
        xmp_get_frame_info(ctx, &fi);
        ended = audio_ended;
        is_playing = playing;
        SDL_UnlockAudioDevice(audio_device);

        total = fi.total_time > 0 ? fi.total_time : 1;
        current = fi.time;
        if (current < 0) current = 0;
        if (current > total) current = total;

        if (is_playing || ended) {
            updating_seek = 1;
            gtk_range_set_range(GTK_RANGE(seek_scale), 0, total);
            gtk_range_set_value(GTK_RANGE(seek_scale), current);
            updating_seek = 0;
        }

        char time_text[80];
        snprintf(time_text, sizeof(time_text), "%02d:%02d / %02d:%02d",
                 current / 60000, (current / 1000) % 60,
                 total / 60000, (total / 1000) % 60);
        gtk_label_set_text(GTK_LABEL(time_label), time_text);

        char status[160];
        snprintf(status, sizeof(status),
                 "Order %d    Pattern %d    Row %d    Speed %d    BPM %d",
                 fi.pos, fi.pattern, fi.row, fi.speed, fi.bpm);
        if (is_playing)
            gtk_label_set_text(GTK_LABEL(status_label), status);
        else if (ended)
            set_status("Playback finished");
    }

    if (ended && !playlist_advance())
        update_play_button();

    update_visualizer_spectrum();

    if (scope_area)
        gtk_widget_queue_draw(scope_area);
    if (pattern_area)
        gtk_widget_queue_draw(pattern_area);
    if (visualizer_area)
        gtk_widget_queue_draw(visualizer_area);
    if (eq_graph_area)
        gtk_widget_queue_draw(eq_graph_area);

    return G_SOURCE_CONTINUE;
}

static GtkWidget *make_button(const char *label, GCallback callback)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    g_signal_connect(button, "clicked", callback, NULL);
    return button;
}

/* Graphic equalizer. This state belongs to the user, not the AppImage, so it
 * stays in XDG_CONFIG_HOME when an AppImage is rebuilt or replaced. */
typedef struct {
    const char *name;
    double gains[XMP_EQ_BANDS];
    double preamp;
} EqPreset;

static const EqPreset eq_presets[] = {
    {"Flat",         { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  0},
    {"Rock",         { 4, 3, 2, 0,-1,-1, 1, 3, 4, 3}, -4},
    {"Pop",          {-1, 1, 3, 4, 2,-1,-1, 1, 2, 2}, -4},
    {"Classical",    { 3, 2, 1, 0, 0, 0, 1, 2, 3, 4}, -4},
    {"Jazz",         { 3, 2, 1, 2,-1,-1, 0, 1, 2, 3}, -3},
    {"Bass Boost",   { 6, 5, 4, 2, 0, 0, 0, 0, 0, 0}, -6},
    {"Vocal Clarity",{-3,-2,-1, 0, 2, 3, 4, 3, 1, 0}, -4},
    {"Custom",       { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  0}
};
#define EQ_CUSTOM_PRESET ((int)G_N_ELEMENTS(eq_presets) - 1)

static gchar *eq_settings_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "xmp-module-player",
                            "equalizer.ini", NULL);
}

static void eq_save_settings(void)
{
    GKeyFile *keyfile = g_key_file_new();
    g_key_file_set_boolean(keyfile, "Equalizer", "enabled", equalizer.enabled);
    g_key_file_set_integer(keyfile, "Equalizer", "preset", eq_preset_index);
    g_key_file_set_double(keyfile, "Equalizer", "preamp", equalizer.target_preamp_db);
    for (int i = 0; i < XMP_EQ_BANDS; ++i) {
        gchar *key = g_strdup_printf("band%d", i);
        g_key_file_set_double(keyfile, "Equalizer", key, equalizer.target_db[i]);
        g_free(key);
    }
    gchar *file = eq_settings_path();
    gchar *directory = g_path_get_dirname(file);
    if (g_mkdir_with_parents(directory, 0700) == 0) {
        gsize size = 0;
        gchar *contents = g_key_file_to_data(keyfile, &size, NULL);
        GError *error = NULL;
        if (!g_file_set_contents(file, contents, size, &error)) {
            g_warning("Could not save equalizer settings: %s", error->message);
            g_clear_error(&error);
        }
        g_free(contents);
    } else {
        g_warning("Could not create equalizer settings directory: %s", directory);
    }
    g_free(file);
    g_free(directory);
    g_key_file_free(keyfile);
}

static gboolean eq_deferred_save(gpointer user_data)
{
    (void)user_data;
    eq_save_timer = 0;
    eq_save_settings();
    return G_SOURCE_REMOVE;
}

static void eq_schedule_save(void)
{
    if (eq_save_timer)
        g_source_remove(eq_save_timer);
    eq_save_timer = g_timeout_add(500, eq_deferred_save, NULL);
}

static void eq_load_settings(void)
{
    gchar *file = eq_settings_path();
    GKeyFile *keyfile = g_key_file_new();
    if (g_key_file_load_from_file(keyfile, file, G_KEY_FILE_NONE, NULL)) {
        if (g_key_file_has_key(keyfile, "Equalizer", "enabled", NULL))
            xmp_eq_set_enabled(&equalizer,
                g_key_file_get_boolean(keyfile, "Equalizer", "enabled", NULL));
        if (g_key_file_has_key(keyfile, "Equalizer", "preset", NULL)) {
            int preset = g_key_file_get_integer(keyfile, "Equalizer", "preset", NULL);
            eq_preset_index = CLAMP(preset, 0, EQ_CUSTOM_PRESET);
        }
        if (g_key_file_has_key(keyfile, "Equalizer", "preamp", NULL))
            xmp_eq_set_preamp(&equalizer,
                g_key_file_get_double(keyfile, "Equalizer", "preamp", NULL));
        for (int i = 0; i < XMP_EQ_BANDS; ++i) {
            gchar *key = g_strdup_printf("band%d", i);
            if (g_key_file_has_key(keyfile, "Equalizer", key, NULL))
                xmp_eq_set_band(&equalizer, i,
                    g_key_file_get_double(keyfile, "Equalizer", key, NULL));
            g_free(key);
        }
    }
    g_key_file_free(keyfile);
    g_free(file);
}

static void eq_refresh_graph(void)
{
    if (eq_graph_area)
        gtk_widget_queue_draw(eq_graph_area);
}

static void eq_apply_preset(int preset)
{
    if (preset < 0 || preset >= EQ_CUSTOM_PRESET)
        return;

    eq_preset_index = preset;
    eq_updating_ui = TRUE;
    SDL_LockAudioDevice(audio_device);
    for (int i = 0; i < XMP_EQ_BANDS; ++i)
        xmp_eq_set_band(&equalizer, i, eq_presets[preset].gains[i]);
    xmp_eq_set_preamp(&equalizer, eq_presets[preset].preamp);
    SDL_UnlockAudioDevice(audio_device);
    for (int i = 0; i < XMP_EQ_BANDS; ++i)
        if (eq_band_scales[i])
            gtk_range_set_value(GTK_RANGE(eq_band_scales[i]),
                                eq_presets[preset].gains[i]);
    if (eq_preamp_scale)
        gtk_range_set_value(GTK_RANGE(eq_preamp_scale), eq_presets[preset].preamp);
    if (eq_preset_combo)
        gtk_combo_box_set_active(GTK_COMBO_BOX(eq_preset_combo), preset);
    eq_updating_ui = FALSE;
    eq_refresh_graph();
    eq_schedule_save();
}

static void eq_on_preset_changed(GtkComboBox *combo, gpointer user_data)
{
    (void)user_data;
    if (eq_updating_ui)
        return;
    int selected = gtk_combo_box_get_active(combo);
    if (selected == EQ_CUSTOM_PRESET) {
        eq_preset_index = EQ_CUSTOM_PRESET;
        eq_schedule_save();
    } else {
        eq_apply_preset(selected);
    }
}

static void eq_mark_custom(void)
{
    if (eq_preset_index == EQ_CUSTOM_PRESET)
        return;
    eq_preset_index = EQ_CUSTOM_PRESET;
    eq_updating_ui = TRUE;
    if (eq_preset_combo)
        gtk_combo_box_set_active(GTK_COMBO_BOX(eq_preset_combo),
                                 EQ_CUSTOM_PRESET);
    eq_updating_ui = FALSE;
}

static void eq_on_band_changed(GtkRange *range, gpointer user_data)
{
    if (eq_updating_ui)
        return;
    int band = GPOINTER_TO_INT(user_data);
    SDL_LockAudioDevice(audio_device);
    xmp_eq_set_band(&equalizer, band, gtk_range_get_value(range));
    SDL_UnlockAudioDevice(audio_device);
    eq_mark_custom();
    eq_refresh_graph();
    eq_schedule_save();
}

static void eq_on_preamp_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    if (eq_updating_ui)
        return;
    SDL_LockAudioDevice(audio_device);
    xmp_eq_set_preamp(&equalizer, gtk_range_get_value(range));
    SDL_UnlockAudioDevice(audio_device);
    eq_mark_custom();
    eq_refresh_graph();
    eq_schedule_save();
}

static void eq_on_enabled_changed(GObject *switch_object,
                                  GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    (void)user_data;
    SDL_LockAudioDevice(audio_device);
    xmp_eq_set_enabled(&equalizer,
        gtk_switch_get_active(GTK_SWITCH(switch_object)));
    SDL_UnlockAudioDevice(audio_device);
    eq_refresh_graph();
    eq_schedule_save();
}

static void eq_on_reset(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    eq_apply_preset(0);
}

static gboolean eq_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    (void)user_data;
    const double width = gtk_widget_get_allocated_width(widget);
    const double height = gtk_widget_get_allocated_height(widget);
    const double left = 46, right = width - 24;
    const double top = 25, bottom = height - 33;
    const double plot_w = MAX(1.0, right - left);
    const double plot_h = MAX(1.0, bottom - top);

    cairo_pattern_t *background = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgb(background, 0, .029, .077, .098);
    cairo_pattern_add_color_stop_rgb(background, 1, .017, .049, .063);
    cairo_set_source(cr, background);
    cairo_paint(cr);
    cairo_pattern_destroy(background);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.5);
    for (int db = -12; db <= 12; db += 6) {
        double y = top + (12.0 - db) / 24.0 * plot_h;
        cairo_set_source_rgba(cr, .32, .68, .7, db == 0 ? .35 : .14);
        cairo_set_line_width(cr, db == 0 ? 1.1 : .7);
        cairo_move_to(cr, left, y);
        cairo_line_to(cr, right, y);
        cairo_stroke(cr);
        char label[16];
        g_snprintf(label, sizeof(label), "%+d", db);
        cairo_set_source_rgba(cr, .65, .80, .81, .85);
        cairo_move_to(cr, 8, y + 4);
        cairo_show_text(cr, label);
    }
    const double marks[] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    const char *mark_text[] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
    const double loglow = log(20), logspan = log(20000.0 / 20.0);
    for (int i = 0; i < XMP_EQ_BANDS; ++i) {
        double x = left + (log(marks[i]) - loglow) / logspan * plot_w;
        cairo_set_source_rgba(cr, .31, .62, .64, .12);
        cairo_move_to(cr, x, top);
        cairo_line_to(cr, x, bottom);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, .58, .79, .80, .9);
        cairo_move_to(cr, x - (double)strlen(mark_text[i]) * 3.1, height - 12);
        cairo_show_text(cr, mark_text[i]);
    }

    /* The main player's smoothed spectrum appears softly behind the EQ curve. */
    for (int i = 0; i < VIZ_BANDS; ++i) {
        double level = CLAMP(viz_levels[i], 0.0, 1.0);
        double x0 = left + (i + .22) * plot_w / VIZ_BANDS;
        double bar_h = level * plot_h * .7;
        cairo_pattern_t *bar = cairo_pattern_create_linear(0, bottom - bar_h, 0, bottom);
        cairo_pattern_add_color_stop_rgba(bar, 0, .22, .94, .88, .12);
        cairo_pattern_add_color_stop_rgba(bar, 1, .12, .64, .67, .02);
        cairo_set_source(cr, bar);
        cairo_rectangle(cr, x0, bottom - bar_h, plot_w / VIZ_BANDS * .58, bar_h);
        cairo_fill(cr);
        cairo_pattern_destroy(bar);
    }

    /* Draw a smooth logarithmic-frequency response without another DSP lib. */
    XmpEqualizer snapshot = equalizer;
    const int points = 220;
    double curve_x[221], curve_y[221];
    for (int i = 0; i <= points; ++i) {
        double fraction = (double)i / points;
        double freq = 20.0 * exp(logspan * fraction);
        double db = CLAMP(xmp_eq_response_db(&snapshot, freq), -12.0, 12.0);
        curve_x[i] = left + fraction * plot_w;
        curve_y[i] = top + (12.0 - db) / 24.0 * plot_h;
    }
    cairo_move_to(cr, curve_x[0], bottom);
    for (int i = 0; i <= points; ++i)
        cairo_line_to(cr, curve_x[i], curve_y[i]);
    cairo_line_to(cr, curve_x[points], bottom);
    cairo_close_path(cr);
    cairo_pattern_t *fill = cairo_pattern_create_linear(0, top, 0, bottom);
    cairo_pattern_add_color_stop_rgba(fill, 0, .1, .94, .83, .21);
    cairo_pattern_add_color_stop_rgba(fill, 1, .1, .65, .70, .015);
    cairo_set_source(cr, fill);
    cairo_fill(cr);
    cairo_pattern_destroy(fill);
    for (int glow = 5; glow >= 1; --glow) {
        cairo_set_source_rgba(cr, .13, .94, .91, glow == 1 ? .93 : .045);
        cairo_set_line_width(cr, glow == 1 ? 2.0 : glow * 2.7);
        cairo_move_to(cr, curve_x[0], curve_y[0]);
        for (int i = 1; i <= points; ++i)
            cairo_line_to(cr, curve_x[i], curve_y[i]);
        cairo_stroke(cr);
    }
    return FALSE;
}

static GtkWidget *eq_create_page(gboolean compact_ui)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, compact_ui ? 9 : 16);
    gtk_container_set_border_width(GTK_CONTAINER(page), compact_ui ? 14 : 22);
    GtkWidget *heading = gtk_label_new("10-Band Graphic Equalizer");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(heading), "module-title");
    gtk_box_pack_start(GTK_BOX(page), heading, FALSE, FALSE, 0);
    GtkWidget *description = gtk_label_new(
        "Shape your sound in real time across every playback format. "
        "Lower Preamp when boosting frequencies to avoid clipping.");
    gtk_label_set_xalign(GTK_LABEL(description), 0);
    gtk_label_set_line_wrap(GTK_LABEL(description), TRUE);
    gtk_box_pack_start(GTK_BOX(page), description, FALSE, FALSE, 0);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_box_pack_start(GTK_BOX(page), toolbar, FALSE, FALSE, 0);
    GtkWidget *preset_label = gtk_label_new("Preset");
    gtk_box_pack_start(GTK_BOX(toolbar), preset_label, FALSE, FALSE, 0);
    eq_preset_combo = gtk_combo_box_text_new();
    for (guint i = 0; i < G_N_ELEMENTS(eq_presets); ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(eq_preset_combo),
                                       eq_presets[i].name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(eq_preset_combo), eq_preset_index);
    g_signal_connect(eq_preset_combo, "changed", G_CALLBACK(eq_on_preset_changed), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), eq_preset_combo, TRUE, TRUE, 0);
    GtkWidget *reset = make_button("Reset Flat", G_CALLBACK(eq_on_reset));
    gtk_box_pack_start(GTK_BOX(toolbar), reset, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_label_new("EQ On"), FALSE, FALSE, 0);
    eq_enabled_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(eq_enabled_switch), equalizer.enabled);
    g_signal_connect(eq_enabled_switch, "notify::active",
                     G_CALLBACK(eq_on_enabled_changed), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), eq_enabled_switch, FALSE, FALSE, 0);

    eq_graph_area = gtk_drawing_area_new();
    gtk_widget_set_name(eq_graph_area, "eq-graph");
    gtk_widget_set_size_request(eq_graph_area, -1, compact_ui ? 145 : 280);
    gtk_widget_set_hexpand(eq_graph_area, TRUE);
    g_signal_connect(eq_graph_area, "draw", G_CALLBACK(eq_draw), NULL);
    gtk_box_pack_start(GTK_BOX(page), eq_graph_area, TRUE, TRUE, 0);

    GtkWidget *slider_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(slider_row), TRUE);
    gtk_widget_set_name(slider_row, "eq-sliders");
    gtk_box_pack_start(GTK_BOX(page), slider_row, FALSE, FALSE, 0);
    const char *frequencies[] = {"31 Hz","62 Hz","125 Hz","250 Hz","500 Hz",
                                 "1 kHz","2 kHz","4 kHz","8 kHz","16 kHz"};
    for (int i = 0; i < XMP_EQ_BANDS; ++i) {
        GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_box_pack_start(GTK_BOX(slider_row), column, TRUE, TRUE, 0);
        GtkWidget *label = gtk_label_new(frequencies[i]);
        gtk_box_pack_start(GTK_BOX(column), label, FALSE, FALSE, 0);
        eq_band_scales[i] = gtk_scale_new_with_range(
            GTK_ORIENTATION_VERTICAL, -12, 12, 0.5);
        gtk_scale_set_draw_value(GTK_SCALE(eq_band_scales[i]), TRUE);
        gtk_scale_set_digits(GTK_SCALE(eq_band_scales[i]), 1);
        gtk_widget_set_size_request(eq_band_scales[i], -1, compact_ui ? 130 : 200);
        gtk_range_set_value(GTK_RANGE(eq_band_scales[i]), equalizer.target_db[i]);
        g_signal_connect(eq_band_scales[i], "value-changed",
                         G_CALLBACK(eq_on_band_changed), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(column), eq_band_scales[i], FALSE, FALSE, 0);
    }

    GtkWidget *preamp_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(page), preamp_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(preamp_row),
        gtk_label_new("Preamp"), FALSE, FALSE, 0);
    eq_preamp_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                -12, 12, 0.5);
    gtk_scale_set_digits(GTK_SCALE(eq_preamp_scale), 1);
    gtk_scale_set_value_pos(GTK_SCALE(eq_preamp_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(eq_preamp_scale), equalizer.target_preamp_db);
    g_signal_connect(eq_preamp_scale, "value-changed",
                     G_CALLBACK(eq_on_preamp_changed), NULL);
    gtk_box_pack_start(GTK_BOX(preamp_row), eq_preamp_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(preamp_row),
        gtk_label_new("dB"), FALSE, FALSE, 0);
    return page;
}

static void apply_style(void)
{
    const char *css =
        "window, notebook, notebook stack { background: #071218; color: #dbeff0; }"
        "label { color: #dbeff0; }"
        /* Dialogs have GTK controls that the player tabs do not use. Cover
         * their panels, file lists, sidebars, entries and spin buttons. */
        "window.dialog, window.dialog.background {"
        " background: #071218; color: #dbeff0; }"
        "window.dialog box, window.dialog grid, window.dialog paned,"
        " window.dialog stack { background-color: #071218; }"
        "window.dialog headerbar, window.dialog .dialog-action-box {"
        " background: #0d1e25; color: #dbeff0; }"
        /* Override the theme's background images as well as its colors.
         * File chooser action buttons live outside the filechooser node. */
        "window.xmp-dialog, .xmp-dialog-surface,"
        "window.xmp-dialog .dialog-action-box {"
        " background: #071218; color: #dbeff0; }"
        "filechooser { background: #071218; color: #dbeff0; }"
        /* GTK places the file-type filter in its own GtkActionBar. */
        "filechooser actionbar { background: #071218; color: #dbeff0; }"
        "filechooser scrolledwindow, filechooser viewport, filechooser .view,"
        " filechooser treeview, filechooser iconview {"
        " background: #050d11; color: #dbeff0; }"
        "filechooser treeview:selected, filechooser iconview:selected {"
        " background: #16515a; color: #ffffff; }"
        "filechooser placessidebar, filechooser placessidebar list,"
        " filechooser placessidebar row, filechooser .sidebar {"
        " background: #0d1e25; color: #dbeff0; }"
        "filechooser placessidebar row:hover { background: #163039; }"
        "filechooser placessidebar row:selected {"
        " background: #16515a; color: #ffffff; }"
        "filechooser placessidebar row:selected label { color: #ffffff; }"
        "window.dialog entry, entry, spinbutton,"
        " spinbutton entry, filechooser entry {"
        " background: #050d11; color: #dbeff0;"
        " border-color: #34616a; caret-color: #65eee0; }"
        "spinbutton button {"
        " background: #10262e; color: #dbeff0;"
        " border-color: #34616a; }"
        "window.dialog button:hover { background: #193b44; }"
        "progressbar trough { background: #163139; }"
        "progressbar progress { background: #39d4c5; }"
        "popover, popover.background { background: #0d1e25;"
        " color: #dbeff0; border-color: #34616a; }"
        /* Combo boxes and file-type filters open GtkMenu popups which
         * otherwise inherit the user's light/dark desktop theme. */
        "menu, menu.background, window.popup {"
        " background-color: #0d1e25; background-image: none;"
        " color: #dbeff0; border-color: #34616a; }"
        "menuitem { background-color: transparent; background-image: none;"
        " color: #dbeff0; }"
        "menuitem label { color: #dbeff0; }"
        "menuitem:hover, menuitem:selected { background-color: #16515a;"
        " background-image: none; color: #ffffff; }"
        "menuitem:hover label, menuitem:selected label { color: #ffffff; }"
        "menuitem:disabled, menuitem:disabled label { color: #819fa2; }"
        "combobox window.popup, combobox window.popup scrolledwindow,"
        " combobox window.popup .view, combobox window.popup treeview {"
        " background-color: #0d1e25; color: #dbeff0; }"
        "combobox window.popup .view:selected,"
        " combobox window.popup treeview:selected {"
        " background-color: #16515a; color: #ffffff; }"
        "notebook header { background: #08181d; border-color: #25545a; }"
        "notebook tab { background: #0d1e25; color: #b8d7d8;"
        " border: 1px solid #28525a; padding: 8px 12px; }"
        "notebook tab:checked { background: #14343b;"
        " border-bottom: 2px solid #45d9cb; }"
        "notebook tab:hover { background: #163039; }"
        "treeview#playlist-view { background-color: #050d11; color: #dbeff0; }"
        "treeview#playlist-view:selected { background-color: #16515a; color: #ffffff; }"
        "treeview#playlist-view row:hover { background-color: #10242d; }"
        "treeview#soundfont-view { background-color: #050d11; color: #dbeff0; }"
        "treeview#soundfont-view:selected { background-color: #16515a; color: #ffffff; }"
        "treeview#soundfont-view row:hover { background-color: #10242d; }"
        "box#samples-page { background-color: #071218; }"
        "scrolledwindow#sample-scroll, scrolledwindow#sample-scroll viewport {"
        " background-color: #050d11; background-image: none; }"
        "list#sample-list { background-color: #050d11;"
        " background-image: none; color: #dbeff0; }"
        "list#sample-list row { background-color: #050d11;"
        " background-image: none; color: #dbeff0; }"
        "list#sample-list row:hover { background-color: #10242d; }"
        "list#sample-list row:selected { background-color: #16515a; color: #ffffff; }"
        "list#sample-list row label { background: transparent; color: #dbeff0; }"
        "list#sample-list row:selected label { color: #ffffff; }"
        "label#sample-details { background-color: #0d1e25; color: #dbeff0; padding: 10px; }"
        "textview#module-comments, textview#module-comments text {"
        " background-color: #050d11; color: #dbeff0; }"
        "button, combobox button { background: #10262e; color: #dbeff0;"
        " border: 1px solid #34616a; border-radius: 4px;"
        " padding: 7px 12px; }"
        "button:hover { background: #193b44; }"
        "scale trough { background: #163139; min-height: 6px; }"
        "scale highlight { background: #39d4c5; }"
        "scale slider { background: #9af4e7; min-width: 14px;"
        " min-height: 14px; }"
        "switch { background: #163139; }"
        "switch:checked { background: #238f8b; }"
        "switch slider { background: #c4fff4; }"
        "#eq-graph { border: 1px solid #2a5c62;"
        " border-radius: 8px; }"
        "#eq-sliders scale slider { background: #65eee0;"
        " border: 1px solid #b1fff4; min-width: 19px; min-height: 19px; }"
        ".module-title { font-size: 19px; font-weight: bold; }"
        "label#about-title { font-size: 24px; font-weight: bold; }"
        "label#about-description { background-color: #0d1e25;"
        " color: #dbeff0; padding: 18px;"
        " border: 1px solid #34616a; border-radius: 6px; }"
        ".muted { color: #9dbfc2; }";

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    if (eq_save_timer) {
        g_source_remove(eq_save_timer);
        eq_save_timer = 0;
    }
    eq_save_settings();

#ifdef ENABLE_WAV_EXPORT
    if (active_wav_export) {
        WavExportTask *task = active_wav_export;
        g_atomic_int_set(&task->cancel, 1);
        if (task->progress_timer)
            g_source_remove(task->progress_timer);
        g_thread_join(task->thread);
        gtk_widget_destroy(task->dialog);
        active_wav_export = NULL;
        free_wav_export_task(task);
    }
#endif

    stop_ffmpeg_decoder();
    zx_cancel_pending();
    zx_remove_active();

    if (audio_device) {
        SDL_PauseAudioDevice(audio_device, 1);
        SDL_LockAudioDevice(audio_device);
        if (player_started) xmp_end_player(ctx);
        if (module_loaded) xmp_release_module(ctx);
        if (sid_audio) {
            sid_audio_close(sid_audio);
            sid_audio = NULL;
        }
        if (midi_audio) {
            midi_audio_close(midi_audio);
            midi_audio = NULL;
        }
        player_started = 0;
        module_loaded = 0;
        SDL_UnlockAudioDevice(audio_device);
        SDL_CloseAudioDevice(audio_device);
    }

    if (ctx) xmp_free_context(ctx);
    g_free(selected_soundfont);
    g_free(favorite_soundfont);
    SDL_Quit();
    gtk_main_quit();
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    /* Request a dark GTK fallback for controls the application does not
     * explicitly style (notably file-chooser internals and popup menus). */
    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", TRUE, NULL);

    xmp_eq_init(&equalizer);
    eq_load_settings();

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        g_printerr("SDL audio initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    ctx = xmp_create_context();
    if (!ctx) {
        g_printerr("Could not create libxmp context.\n");
        SDL_Quit();
        return 1;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!audio_device) {
        g_printerr("Could not open audio device: %s\n", SDL_GetError());
        xmp_free_context(ctx);
        SDL_Quit();
        return 1;
    }
    SDL_PauseAudioDevice(audio_device, 1);

    apply_style();

    char *saved_soundfont_file = get_soundfont_settings_file();
    if (g_file_get_contents(saved_soundfont_file, &selected_soundfont,
                            NULL, NULL)) {
        g_strstrip(selected_soundfont);
        if (!*selected_soundfont)
            g_clear_pointer(&selected_soundfont, g_free);
    }
    g_free(saved_soundfont_file);

    GdkPixbuf *app_icon = gdk_pixbuf_new_from_resource(
        "/org/helllabs/xmp/icons/xmp.png", NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "XMP Module Player");

    /* Size the initial window to the monitor's usable area. GTK will not
     * honor a smaller default size if notebook pages request more space, so
     * the tall pages also use shorter minimum widgets on compact displays. */
    gint initial_width = 900;
    gint initial_height = 850;
    gboolean compact_ui = FALSE;
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (!monitor && display)
        monitor = gdk_display_get_monitor(display, 0);
    if (monitor) {
        GdkRectangle workarea;
        gdk_monitor_get_workarea(monitor, &workarea);
        compact_ui = workarea.height < 960;
        initial_width = MIN(initial_width, MAX(650, workarea.width - 48));
        initial_height = MIN(initial_height, MAX(480, workarea.height - 48));
    }

    GtkTargetEntry drop_targets[] = {
        { "text/uri-list", 0, 0 }
    };
    gtk_drag_dest_set(window, GTK_DEST_DEFAULT_ALL,
                      drop_targets, G_N_ELEMENTS(drop_targets),
                      GDK_ACTION_COPY);
    g_signal_connect(window, "drag-data-received",
                     G_CALLBACK(on_file_drop), NULL);
    if (app_icon)
        gtk_window_set_icon(GTK_WINDOW(window), app_icon);
    gtk_window_set_default_size(GTK_WINDOW(window), initial_width, initial_height);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(window), notebook);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, compact_ui ? 8 : 12);
    gtk_container_set_border_width(GTK_CONTAINER(outer), compact_ui ? 12 : 20);

    GtkWidget *player_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, compact_ui ? 8 : 12);
    gtk_container_set_border_width(GTK_CONTAINER(player_page), compact_ui ? 4 : 8);
    gtk_container_add(GTK_CONTAINER(player_page), outer);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(outer), top, FALSE, FALSE, 0);

    GdkPixbuf *small_cover = (app_icon && compact_ui)
        ? gdk_pixbuf_scale_simple(app_icon, 80, 80, GDK_INTERP_BILINEAR)
        : NULL;
    GtkWidget *cover = app_icon
        ? gtk_image_new_from_pixbuf(small_cover ? small_cover : app_icon)
        : gtk_image_new();
    if (small_cover)
        g_object_unref(small_cover);
    gtk_widget_set_size_request(cover, compact_ui ? 80 : 100,
                               compact_ui ? 80 : 100);
    gtk_box_pack_start(GTK_BOX(top), cover, FALSE, FALSE, 0);

    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(top), info, TRUE, TRUE, 0);

    title_label = gtk_label_new("No module loaded");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0);
    gtk_widget_set_name(title_label, "module-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(title_label),
                                "module-title");
    gtk_box_pack_start(GTK_BOX(info), title_label, FALSE, FALSE, 0);

    format_label = gtk_label_new("Open a music file to begin");
    gtk_label_set_xalign(GTK_LABEL(format_label), 0);
    gtk_box_pack_start(GTK_BOX(info), format_label, FALSE, FALSE, 0);

    channels_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(channels_label), 0);
    gtk_box_pack_start(GTK_BOX(info), channels_label, FALSE, FALSE, 0);

    /* Mirrored FFT spectrum in the open space at the top right. */
    visualizer_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(visualizer_area, compact_ui ? 280 : 320,
                               compact_ui ? 80 : 100);
    gtk_widget_set_hexpand(visualizer_area, TRUE);
    g_signal_connect(visualizer_area, "draw",
                     G_CALLBACK(draw_visualizer), NULL);
    gtk_box_pack_start(GTK_BOX(top), visualizer_area, TRUE, TRUE, 0);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(outer), controls, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(controls),
        make_button("Open File…", G_CALLBACK(on_open)), FALSE, FALSE, 0);
#ifdef ENABLE_WAV_EXPORT
    gtk_box_pack_start(GTK_BOX(controls),
        make_button("Export WAV…", G_CALLBACK(on_export_wav)), FALSE, FALSE, 0);
#endif
    gtk_box_pack_start(GTK_BOX(controls),
        make_button("|◀", G_CALLBACK(on_previous)), FALSE, FALSE, 0);

    play_button = gtk_button_new_with_label("▶  Play");
    g_signal_connect(play_button, "clicked", G_CALLBACK(on_play), NULL);
    gtk_box_pack_start(GTK_BOX(controls), play_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(controls),
        make_button("■  Stop", G_CALLBACK(on_stop)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls),
        make_button("▶|", G_CALLBACK(on_next)), FALSE, FALSE, 0);

    GtkWidget *volume_label = gtk_label_new("Volume");
    gtk_box_pack_start(GTK_BOX(controls), volume_label, FALSE, FALSE, 0);
    volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(volume_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(volume_scale), GTK_POS_RIGHT);
    gtk_widget_set_size_request(volume_scale, 140, -1);
    gtk_range_set_value(GTK_RANGE(volume_scale), master_volume);
    g_signal_connect(volume_scale, "value-changed", G_CALLBACK(on_volume_changed), NULL);
    gtk_box_pack_start(GTK_BOX(controls), volume_scale, FALSE, FALSE, 0);

   /* Adjustable playlist and pattern sections */
GtkWidget *content_pane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
gtk_box_pack_start(GTK_BOX(outer), content_pane, TRUE, TRUE, 0);

/* Playlist */
GtkWidget *playlist_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget *playlist_controls =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(playlist_box), playlist_controls,
                       FALSE, FALSE, 0);

    GtkWidget *playlist_heading = gtk_label_new("Playlist");
    gtk_widget_set_halign(playlist_heading, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(playlist_controls), playlist_heading,
                       TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(playlist_controls),
        make_button("Add Files", G_CALLBACK(on_playlist_add)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(playlist_controls),
        make_button("Load M3U", G_CALLBACK(on_playlist_load_m3u)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(playlist_controls),
        make_button("Save M3U", G_CALLBACK(on_playlist_save_m3u)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(playlist_controls),
        make_button("Remove", G_CALLBACK(on_playlist_remove)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(playlist_controls),
        make_button("Clear", G_CALLBACK(on_playlist_clear)),
        FALSE, FALSE, 0);

    playlist_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    playlist_view = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(playlist_store));
    gtk_widget_set_name(playlist_view, "playlist-view");
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(playlist_view), FALSE);

    GtkCellRenderer *playlist_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *playlist_column =
        gtk_tree_view_column_new_with_attributes(
            "Track", playlist_renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(playlist_view),
                                playlist_column);

    g_signal_connect(playlist_view, "row-activated",
                     G_CALLBACK(on_playlist_activate), NULL);

    GtkWidget *playlist_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(playlist_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(playlist_scroll, -1, 60);
    gtk_container_add(GTK_CONTAINER(playlist_scroll), playlist_view);
   gtk_box_pack_start(GTK_BOX(playlist_box), playlist_scroll,
                   TRUE, TRUE, 0);
gtk_paned_pack1(GTK_PANED(content_pane), playlist_box, TRUE, TRUE);

    seek_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 100);
    gtk_scale_set_draw_value(GTK_SCALE(seek_scale), FALSE);
    g_signal_connect(seek_scale, "value-changed", G_CALLBACK(on_seek), NULL);
    GtkWidget *pattern_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pattern_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(pattern_scroll, -1, 60);

    pattern_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(pattern_area, 900, 270);
    g_signal_connect(pattern_area, "draw", G_CALLBACK(draw_pattern), NULL);
    gtk_container_add(GTK_CONTAINER(pattern_scroll), pattern_area);

    GtkWidget *pattern_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    GtkWidget *pattern_heading = gtk_label_new("Pattern View");
    gtk_widget_set_halign(pattern_heading, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(pattern_box), pattern_heading,
                   FALSE, FALSE, 0);

gtk_box_pack_start(GTK_BOX(pattern_box), pattern_scroll,
                   TRUE, TRUE, 0);

    /* Put pattern view and scopes together below the playlist. */
    GtkWidget *lower_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    gtk_box_pack_start(GTK_BOX(lower_box), pattern_box,
                       TRUE, TRUE, 0);

    GtkWidget *scope_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scope_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scope_scroll, -1, 60);

    scope_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(scope_area, 620, 180);
    g_signal_connect(scope_area, "draw", G_CALLBACK(draw_scopes), NULL);
    gtk_container_add(GTK_CONTAINER(scope_scroll), scope_area);

    gtk_box_pack_start(GTK_BOX(lower_box), scope_scroll,
                       TRUE, TRUE, 0);

    /* One divider: playlist above, pattern and scopes below. */
    gtk_paned_pack2(GTK_PANED(content_pane), lower_box, TRUE, TRUE);
    gtk_paned_set_position(GTK_PANED(content_pane), compact_ui ? 150 : 230);

    gtk_box_pack_start(GTK_BOX(outer), seek_scale, FALSE, FALSE, 0);

    time_label = gtk_label_new("00:00 / 00:00");
    gtk_label_set_xalign(GTK_LABEL(time_label), 1);
    gtk_box_pack_start(GTK_BOX(outer), time_label, FALSE, FALSE, 0);

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(outer), separator, FALSE, FALSE, 4);

    details_label = gtk_label_new("The original Android project remains untouched.");
    gtk_label_set_xalign(GTK_LABEL(details_label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(details_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(outer), details_label, FALSE, FALSE, 0);

    status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0);
    gtk_box_pack_start(GTK_BOX(outer), status_label, FALSE, FALSE, 0);

    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), player_page, gtk_label_new("Player"));

    GtkWidget *samples_page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_name(samples_page, "samples-page");
    gtk_container_set_border_width(GTK_CONTAINER(samples_page), 12);

    GtkWidget *samples_heading = gtk_label_new("Module Samples");
    gtk_widget_set_halign(samples_heading, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(samples_page), samples_heading,
                       FALSE, FALSE, 0);

    sample_list = gtk_list_box_new();
    gtk_widget_set_name(sample_list, "sample-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(sample_list),
                                    GTK_SELECTION_SINGLE);
    g_signal_connect(sample_list, "row-selected",
        G_CALLBACK(on_sample_selected), NULL);

    GtkWidget *sample_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_name(sample_scroll, "sample-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sample_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sample_scroll, -1, compact_ui ? 160 : 220);
    gtk_container_add(GTK_CONTAINER(sample_scroll), sample_list);
    gtk_box_pack_start(GTK_BOX(samples_page), sample_scroll,
                       TRUE, TRUE, 0);

    sample_details_label = gtk_label_new("Load a module to inspect its samples.");
    gtk_widget_set_name(sample_details_label, "sample-details");
    gtk_label_set_xalign(GTK_LABEL(sample_details_label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(sample_details_label), TRUE);
    gtk_box_pack_start(GTK_BOX(samples_page), sample_details_label,
                       FALSE, FALSE, 0);

    sample_waveform_area = gtk_drawing_area_new();
    gtk_widget_set_name(sample_waveform_area, "sample-waveform");
    gtk_widget_set_size_request(sample_waveform_area, -1, compact_ui ? 90 : 120);
    gtk_widget_set_hexpand(sample_waveform_area, TRUE);
    g_signal_connect(sample_waveform_area, "draw",
        G_CALLBACK(on_sample_waveform_draw), NULL);
    gtk_box_pack_start(GTK_BOX(samples_page), sample_waveform_area,
                       FALSE, FALSE, 0);

    GtkWidget *comments_heading = gtk_label_new("Module Notes / Comments");
    gtk_widget_set_halign(comments_heading, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(samples_page), comments_heading,
                       FALSE, FALSE, 0);

    module_comments_view = gtk_text_view_new();
    gtk_widget_set_name(module_comments_view, "module-comments");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(module_comments_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(module_comments_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(module_comments_view),
                                GTK_WRAP_WORD_CHAR);

    GtkWidget *comments_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(comments_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(comments_scroll, -1, compact_ui ? 100 : 140);
    gtk_container_add(GTK_CONTAINER(comments_scroll), module_comments_view);
    gtk_box_pack_start(GTK_BOX(samples_page), comments_scroll,
                       TRUE, TRUE, 0);

    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), samples_page,
        gtk_label_new("Samples & Notes"));

    /* MIDI SoundFont library: persistent paths, startup default, live switching. */
    GtkWidget *soundfont_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(soundfont_page),
                                   compact_ui ? 14 : 24);

    GtkWidget *soundfont_heading = gtk_label_new("MIDI SoundFonts");
    gtk_widget_set_halign(soundfont_heading, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(soundfont_heading), "module-title");
    gtk_box_pack_start(GTK_BOX(soundfont_page), soundfont_heading,
                       FALSE, FALSE, 0);

    GtkWidget *soundfont_description = gtk_label_new(
        "MIDI files contain musical instructions, not recorded audio.\n"
        "Add SF2/SF3 files or scan a folder to build your personal library.\n"
        "Double-click a SoundFont to use it; set a startup default if desired.");
    gtk_label_set_xalign(GTK_LABEL(soundfont_description), 0);
    gtk_label_set_line_wrap(GTK_LABEL(soundfont_description), TRUE);
    gtk_box_pack_start(GTK_BOX(soundfont_page), soundfont_description,
                       FALSE, FALSE, 0);

    soundfont_store = gtk_list_store_new(SF_COLUMNS, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    load_soundfont_library();
    soundfont_view = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(soundfont_store));
    gtk_widget_set_name(soundfont_view, "soundfont-view");
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(soundfont_view), TRUE);
    const char *sf_headers[] = {"Name", "Format", "File location", "Status"};
    const int sf_fields[] = {SF_NAME, SF_FORMAT, SF_PATH, SF_STATE};
    for (guint i = 0; i < G_N_ELEMENTS(sf_fields); ++i) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            sf_headers[i], renderer, "text", sf_fields[i], NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_expand(column, i == 2);
        gtk_tree_view_append_column(GTK_TREE_VIEW(soundfont_view), column);
    }
    g_signal_connect(soundfont_view, "row-activated",
                     G_CALLBACK(on_soundfont_row_activated), NULL);
    GtkWidget *sf_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sf_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sf_scroll, -1, compact_ui ? 170 : 280);
    gtk_container_add(GTK_CONTAINER(sf_scroll), soundfont_view);
    gtk_box_pack_start(GTK_BOX(soundfont_page), sf_scroll, TRUE, TRUE, 0);

    GtkWidget *sf_library_buttons =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(soundfont_page), sf_library_buttons,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sf_library_buttons),
        make_button("Add Files…", G_CALLBACK(on_choose_soundfont)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sf_library_buttons),
        make_button("Scan Folder…", G_CALLBACK(on_import_soundfont_folder)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sf_library_buttons),
        make_button("Use Selected", G_CALLBACK(on_activate_soundfont)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sf_library_buttons),
        make_button("Set / Clear Default", G_CALLBACK(on_favorite_soundfont)),
        FALSE, FALSE, 0);

    GtkWidget *sf_more_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(soundfont_page), sf_more_buttons,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sf_more_buttons),
        make_button("Remove from Library", G_CALLBACK(on_remove_soundfont)),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sf_more_buttons),
        make_button("Use System SoundFont", G_CALLBACK(on_system_soundfont)),
        FALSE, FALSE, 0);

    soundfont_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(soundfont_label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(soundfont_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(soundfont_label), TRUE);
    gtk_box_pack_start(GTK_BOX(soundfont_page), soundfont_label,
                       FALSE, FALSE, 0);
    update_soundfont_label();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), soundfont_page,
                             gtk_label_new("SoundFonts"));

    /* A built-in, tabbed EQ rather than a second floating window. */
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), eq_create_page(compact_ui),
                             gtk_label_new("Equalizer"));


    /* High Voltage SID Collection */
    GtkWidget *hvsc_page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(hvsc_page), 24);

    GtkWidget *hvsc_title =
        gtk_label_new("High Voltage SID Collection");
    gtk_widget_set_halign(hvsc_title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(hvsc_title), "title");
    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_title,
                       FALSE, FALSE, 0);

    GtkWidget *hvsc_description = gtk_label_new(
        "Download or manage the High Voltage SID Collection (HVSC),\n"
        "an extensive archive of Commodore 64 SID music.");
    gtk_label_set_xalign(GTK_LABEL(hvsc_description), 0);
    gtk_label_set_line_wrap(GTK_LABEL(hvsc_description), TRUE);
    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_description,
                       FALSE, FALSE, 0);

    GtkWidget *hvsc_folder_label =
        gtk_label_new("Collection folder:");
    gtk_widget_set_halign(hvsc_folder_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_folder_label,
                       FALSE, FALSE, 0);

    GtkWidget *hvsc_folder = gtk_file_chooser_button_new(
        "Choose HVSC Collection Folder",
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_folder,
                       FALSE, FALSE, 0);

    char *hvsc_settings_file = get_hvsc_folder_file();
    char *saved_hvsc_folder = NULL;

    if (g_file_get_contents(hvsc_settings_file,
                            &saved_hvsc_folder, NULL, NULL)) {
        g_strstrip(saved_hvsc_folder);
        if (g_file_test(saved_hvsc_folder, G_FILE_TEST_IS_DIR))
            gtk_file_chooser_set_filename(
                GTK_FILE_CHOOSER(hvsc_folder), saved_hvsc_folder);
    }

    g_free(saved_hvsc_folder);
    g_free(hvsc_settings_file);

    g_signal_connect(hvsc_folder, "file-set",
                     G_CALLBACK(on_hvsc_folder_changed), NULL);



    /* Separate installation destination: never use the existing
       collection selector as the download destination. */
    GtkWidget *hvsc_install_label = gtk_label_new(
        "Install a new HVSC collection into:");
    gtk_widget_set_halign(hvsc_install_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_install_label,
                       FALSE, FALSE, 0);

    GtkWidget *hvsc_install_folder =
        gtk_file_chooser_button_new(
            "Choose Installation Parent Folder",
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

    char *hvsc_default_parent =
        g_build_filename(g_get_home_dir(), "Music", NULL);

    if (g_file_test(hvsc_default_parent, G_FILE_TEST_IS_DIR))
        gtk_file_chooser_set_filename(
            GTK_FILE_CHOOSER(hvsc_install_folder),
            hvsc_default_parent);
    else
        gtk_file_chooser_set_filename(
            GTK_FILE_CHOOSER(hvsc_install_folder),
            g_get_home_dir());

    g_free(hvsc_default_parent);

    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_install_folder,
                       FALSE, FALSE, 0);

    GtkWidget *hvsc_install_hint = gtk_label_new(
        "A new HVSC folder will be created inside the selected directory.\n"
        "Existing files and collections will never be overwritten.");
    gtk_label_set_xalign(GTK_LABEL(hvsc_install_hint), 0);
    gtk_label_set_line_wrap(GTK_LABEL(hvsc_install_hint), TRUE);
    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_install_hint,
                       FALSE, FALSE, 0);

    GtkWidget *hvsc_buttons =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_buttons,
                       FALSE, FALSE, 0);

    GtkWidget *hvsc_download =
        gtk_button_new_with_label("Download HVSC");
    gtk_box_pack_start(GTK_BOX(hvsc_buttons), hvsc_download,
                       FALSE, FALSE, 0);

    gtk_button_set_label(
        GTK_BUTTON(hvsc_download), "Check Installation Destination");

    g_signal_connect(
        hvsc_download, "clicked",
        G_CALLBACK(on_hvsc_check_destination),
        hvsc_install_folder);

    GtkWidget *hvsc_open_folder =
        gtk_button_new_with_label("Open HVSC Folder");
    gtk_box_pack_start(GTK_BOX(hvsc_buttons), hvsc_open_folder,
                       FALSE, FALSE, 0);

    g_signal_connect(hvsc_open_folder, "clicked",
                     G_CALLBACK(on_hvsc_open_folder), hvsc_folder);


    GtkWidget *hvsc_progress = gtk_progress_bar_new();

    GtkWidget *hvsc_waveform = gtk_drawing_area_new();
    gtk_widget_set_size_request(hvsc_waveform, -1, 76);

    g_object_set_data(G_OBJECT(hvsc_waveform),
                      "progress", hvsc_progress);

    g_signal_connect(hvsc_waveform, "draw",
                     G_CALLBACK(hvsc_waveform_draw), NULL);

    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_waveform,
                       FALSE, FALSE, 0);

    /* Retain the progress object as our numeric progress state,
       but hide its ordinary GTK visual representation. */
    gtk_widget_set_no_show_all(hvsc_progress, TRUE);
    gtk_widget_hide(hvsc_progress);

    GtkWidget *hvsc_status =
        gtk_label_new("Ready to download or select an existing collection.");
    gtk_label_set_xalign(GTK_LABEL(hvsc_status), 0);
    gtk_box_pack_start(GTK_BOX(hvsc_page), hvsc_status,
                       FALSE, FALSE, 0);
    GtkWidget *hvsc_auto_button =
        gtk_button_new_with_label("Download & Install HVSC");

    gtk_box_pack_start(GTK_BOX(hvsc_buttons),
                       hvsc_auto_button, FALSE, FALSE, 0);

    HvscUI *hvsc_ui = g_new0(HvscUI, 1);
    hvsc_ui->button = hvsc_auto_button;
    hvsc_ui->progress = hvsc_progress;
    hvsc_ui->waveform = hvsc_waveform;
    hvsc_ui->status = hvsc_status;
    hvsc_ui->collection_folder = hvsc_folder;

    GtkWidget **hvsc_data = g_new0(GtkWidget *, 2);
    hvsc_data[0] = hvsc_install_folder;
    hvsc_data[1] = (GtkWidget *)hvsc_ui;

    g_signal_connect(hvsc_auto_button, "clicked",
                     G_CALLBACK(on_hvsc_download_clicked),
                     hvsc_data);


    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), hvsc_page,
        gtk_label_new("HVSC"));

    /* About page */
    GtkWidget *about_page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(about_page), 30);
    gtk_widget_set_halign(about_page, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(about_page, GTK_ALIGN_CENTER);

    GtkWidget *about_icon = app_icon ? gtk_image_new_from_pixbuf(app_icon) : gtk_image_new();
    gtk_widget_set_size_request(about_icon, 128, 128);
    gtk_box_pack_start(GTK_BOX(about_page), about_icon,
                       FALSE, FALSE, 0);

    GtkWidget *about_title = gtk_label_new("XMP Module Player 1.3 (development)");
    gtk_widget_set_name(about_title, "about-title");
    gtk_box_pack_start(GTK_BOX(about_page), about_title,
                       FALSE, FALSE, 0);

    GtkWidget *about_description = gtk_label_new(
        "A native Linux music player built with GTK3 and SDL2.\n"
        "\n"
        "Tracker modules powered by libxmp\n"
        "Commodore 64 SID music powered by libsidplayfp\n"
        "Digital audio powered by FFmpeg\n"
        "MIDI powered by FluidSynth with a SoundFont library\n"
        "Built-in 10-band equalizer with presets and live spectrum\n"
        "MP3, WAV, M4A/AAC, Ogg Vorbis, Opus, FLAC and more\n"
        "\n"
        "High Voltage SID Collection (HVSC) browsing\n"
        "and integrated collection downloader.");
    gtk_widget_set_name(about_description, "about-description");
    gtk_label_set_line_wrap(GTK_LABEL(about_description), TRUE);
    gtk_label_set_justify(GTK_LABEL(about_description), GTK_JUSTIFY_CENTER);
    gtk_widget_set_size_request(about_description, 420, -1);
    gtk_box_pack_start(GTK_BOX(about_page), about_description,
                       FALSE, FALSE, 0);

    GtkWidget *about_developer = gtk_label_new(
        "Developed by David (DaSteele) Harryman");
    gtk_box_pack_start(GTK_BOX(about_page), about_developer,
                       FALSE, FALSE, 0);

    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), about_page,
        gtk_label_new("About"));

    g_timeout_add(33, update_display, NULL);

    if (argc > 1) {
        if (is_midi_path(argv[1])) {
            open_midi(argv[1]);
        } else {
            open_module(argv[1]);
            if (!zx_is_music_path(argv[1]) && !module_loaded &&
                !open_sid(argv[1]))
                open_ffmpeg_audio(argv[1]);
        }
    }

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
