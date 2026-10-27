#include <gtk/gtk.h>
#include <SDL.h>
#include <xmp.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define AUDIO_RATE 44100
#define SCOPE_ROW_HEIGHT 76
#define SCOPE_MARGIN 8

static xmp_context ctx;
static SDL_AudioDeviceID audio_device;
static int module_loaded = 0;
static int player_started = 0;
static int playing = 0;
static int stopped = 0;
static int audio_ended = 0;
static int updating_seek = 0;

static GtkWidget *window;
static GtkWidget *title_label;
static GtkWidget *format_label;
static GtkWidget *status_label;
static GtkWidget *time_label;
static GtkWidget *seek_scale;
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

#define VIZ_BANDS 32
#define VIZ_FFT_SIZE 1024
#define VIZ_RING_SIZE 8192

static int16_t viz_ring[VIZ_RING_SIZE];
static unsigned int viz_write_pos;
static double viz_levels[VIZ_BANDS];
static double viz_peaks[VIZ_BANDS];
static double viz_spectrum[VIZ_BANDS];
static int selected_sample = 0;

static char current_file[4096] = "";
static GtkListStore *playlist_store;
static GtkWidget *playlist_view;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    memset(stream, 0, len);

    if (!module_loaded || !player_started || !playing || audio_ended)
        return;

    int rc = xmp_play_buffer(ctx, stream, len, 0);

    /* Capture mono samples for the visualizer; audio is S16 stereo. */
    const int16_t *pcm = (const int16_t *)stream;
    int sample_count = len / (int)(2 * sizeof(int16_t));
    for (int i = 0; i < sample_count; i++) {
        int mono = ((int)pcm[i * 2] + (int)pcm[i * 2 + 1]) / 2;
        viz_ring[viz_write_pos] = (int16_t)mono;
        viz_write_pos = (viz_write_pos + 1) % VIZ_RING_SIZE;
    }

    if (rc == -XMP_END) {
        audio_ended = 1;
        playing = 0;
    }
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
    cairo_set_source_rgb(cr, 0.063, 0.071, 0.086);
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

static void open_module(const char *filename)
{
    SDL_PauseAudioDevice(audio_device, 1);
    SDL_LockAudioDevice(audio_device);

    playing = 0;
    audio_ended = 0;
    stopped = 0;

    if (player_started) {
        xmp_end_player(ctx);
        player_started = 0;
    }

    if (module_loaded) {
        xmp_release_module(ctx);
        module_loaded = 0;
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

static void on_open(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Open module", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Tracker modules and music files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            open_module(filename);
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_play(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    if (!module_loaded)
        return;

    if (playing) {
        SDL_PauseAudioDevice(audio_device, 1);
        playing = 0;
        set_status("Paused");
    } else {
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
    }
    update_play_button();
}

static void on_stop(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    if (!module_loaded)
        return;

    SDL_PauseAudioDevice(audio_device, 1);
    SDL_LockAudioDevice(audio_device);
    xmp_stop_module(ctx);
    playing = 0;
    stopped = 1;
    audio_ended = 0;
    SDL_UnlockAudioDevice(audio_device);

    update_play_button();
    set_status("Stopped");
}

static void on_previous(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    if (!module_loaded) return;

    SDL_LockAudioDevice(audio_device);
    xmp_prev_position(ctx);
    SDL_UnlockAudioDevice(audio_device);
}

static void on_next(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    if (!module_loaded) return;

    SDL_LockAudioDevice(audio_device);
    xmp_next_position(ctx);
    SDL_UnlockAudioDevice(audio_device);
}

static void on_seek(GtkRange *range, gpointer data)
{
    (void)data;
    if (!module_loaded || updating_seek)
        return;

    int ms = (int)gtk_range_get_value(range);
    SDL_LockAudioDevice(audio_device);
    xmp_seek_time(ctx, ms);
    audio_ended = 0;
    stopped = 0;
    SDL_UnlockAudioDevice(audio_device);
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

    cairo_set_source_rgb(cr, 0.025, 0.035, 0.065);
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
    if (!module_loaded || !playing || audio_ended) {
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

    cairo_set_source_rgb(cr, 0.055, 0.065, 0.080);
    cairo_paint(cr);

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

        cairo_set_source_rgb(cr, 0.035, 0.045, 0.050);
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

    open_module(filename);
    if (module_loaded)
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

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

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

    gtk_file_chooser_set_do_overwrite_confirmation(
        GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(
        GTK_FILE_CHOOSER(dialog), "playlist.m3u");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

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

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Tracker modules and music files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
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
                return module_loaded && playing;
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

    cairo_set_source_rgb(cr, 0.055, 0.065, 0.080);
    cairo_paint(cr);

    cairo_select_font_face(cr, "Monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);

    if (!module_loaded) {
        cairo_set_source_rgb(cr, 0.70, 0.73, 0.78);
        cairo_move_to(cr, 12, 24);
        cairo_show_text(cr, "Load a module to view its pattern.");
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
            cairo_set_source_rgb(cr, 0.075, 0.087, 0.105);
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

    if (!module_loaded)
        return G_SOURCE_CONTINUE;

    struct xmp_frame_info fi;
    SDL_LockAudioDevice(audio_device);
    xmp_get_frame_info(ctx, &fi);
    int ended = audio_ended;
    int is_playing = playing;
    SDL_UnlockAudioDevice(audio_device);

    int total = fi.total_time > 0 ? fi.total_time : 1;
    int current = fi.time;
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
    if (playing)
        gtk_label_set_text(GTK_LABEL(status_label), status);
    else if (ended)
        set_status("Playback finished");

    if (ended && !playlist_advance())
        update_play_button();

    update_visualizer_spectrum();

    if (scope_area)
        gtk_widget_queue_draw(scope_area);

    if (pattern_area)
        gtk_widget_queue_draw(pattern_area);

    if (visualizer_area)
        gtk_widget_queue_draw(visualizer_area);

    return G_SOURCE_CONTINUE;
}

static GtkWidget *make_button(const char *label, GCallback callback)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    g_signal_connect(button, "clicked", callback, NULL);
    return button;
}

static void apply_style(void)
{
    const char *css =
        "window { background: #17191e; color: #e3e6ed; }"
        "label { color: #e3e6ed; }"
        "treeview#playlist-view { background-color: #101216; color: #e3e6ed; }"
        "treeview#playlist-view:selected { background-color: #31506d; color: #ffffff; }"
        "treeview#playlist-view row:hover { background-color: #252b35; }"
        "listbox#sample-list { background-color: #101216; color: #e3e6ed; }"
        "listbox#sample-list row { background-color: #101216; color: #e3e6ed; }"
        "listbox#sample-list row:selected { background-color: #31506d; color: #ffffff; }"
        "label#sample-details { background-color: #20242c; color: #e3e6ed; padding: 10px; }"
        "textview#module-comments { background-color: #101216; color: #e3e6ed; }"
        "button { background: #292d36; color: #e3e6ed;"
        " border: 1px solid #414754; border-radius: 4px;"
        " padding: 7px 12px; }"
        "button:hover { background: #394151; }"
        "scale trough { background: #303541; min-height: 6px; }"
        "scale highlight { background: #4b9fe8; }"
        "scale slider { background: #d9e8f7; min-width: 14px;"
        " min-height: 14px; }"
        ".module-title { font-size: 19px; font-weight: bold; }"
        "label#about-title { font-size: 24px; font-weight: bold; }"
        ".muted { color: #9ba4b4; }";

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

    if (audio_device) {
        SDL_PauseAudioDevice(audio_device, 1);
        SDL_LockAudioDevice(audio_device);
        if (player_started) xmp_end_player(ctx);
        if (module_loaded) xmp_release_module(ctx);
        player_started = 0;
        module_loaded = 0;
        SDL_UnlockAudioDevice(audio_device);
        SDL_CloseAudioDevice(audio_device);
    }

    if (ctx) xmp_free_context(ctx);
    SDL_Quit();
    gtk_main_quit();
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

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

    GdkPixbuf *app_icon = gdk_pixbuf_new_from_resource(
        "/org/helllabs/xmp/icons/xmp.png", NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "XMP Module Player");

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
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 850);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(window), notebook);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 20);

    GtkWidget *player_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(player_page), 8);
    gtk_container_add(GTK_CONTAINER(player_page), outer);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(outer), top, FALSE, FALSE, 0);

    GtkWidget *cover = app_icon ? gtk_image_new_from_pixbuf(app_icon) : gtk_image_new();
    gtk_widget_set_size_request(cover, 100, 100);
    gtk_box_pack_start(GTK_BOX(top), cover, FALSE, FALSE, 0);

    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(top), info, TRUE, TRUE, 0);

    title_label = gtk_label_new("No module loaded");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0);
    gtk_widget_set_name(title_label, "module-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(title_label),
                                "module-title");
    gtk_box_pack_start(GTK_BOX(info), title_label, FALSE, FALSE, 0);

    format_label = gtk_label_new("Open a tracker module to begin");
    gtk_label_set_xalign(GTK_LABEL(format_label), 0);
    gtk_box_pack_start(GTK_BOX(info), format_label, FALSE, FALSE, 0);

    channels_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(channels_label), 0);
    gtk_box_pack_start(GTK_BOX(info), channels_label, FALSE, FALSE, 0);

    /* Mirrored FFT spectrum in the open space at the top right. */
    visualizer_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(visualizer_area, 320, 100);
    gtk_widget_set_hexpand(visualizer_area, TRUE);
    g_signal_connect(visualizer_area, "draw",
                     G_CALLBACK(draw_visualizer), NULL);
    gtk_box_pack_start(GTK_BOX(top), visualizer_area, TRUE, TRUE, 0);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(outer), controls, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(controls),
        make_button("Open module…", G_CALLBACK(on_open)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls),
        make_button("|◀", G_CALLBACK(on_previous)), FALSE, FALSE, 0);

    play_button = gtk_button_new_with_label("▶  Play");
    g_signal_connect(play_button, "clicked", G_CALLBACK(on_play), NULL);
    gtk_box_pack_start(GTK_BOX(controls), play_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(controls),
        make_button("■  Stop", G_CALLBACK(on_stop)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls),
        make_button("▶|", G_CALLBACK(on_next)), FALSE, FALSE, 0);

    /* Playlist */
    GtkWidget *playlist_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(outer), playlist_box, FALSE, FALSE, 0);

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
    gtk_widget_set_size_request(playlist_scroll, -1, 110);
    gtk_container_add(GTK_CONTAINER(playlist_scroll), playlist_view);
    gtk_box_pack_start(GTK_BOX(playlist_box), playlist_scroll,
                       FALSE, TRUE, 0);

    seek_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 100);
    gtk_scale_set_draw_value(GTK_SCALE(seek_scale), FALSE);
    g_signal_connect(seek_scale, "value-changed", G_CALLBACK(on_seek), NULL);
    GtkWidget *pattern_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pattern_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(pattern_scroll, -1, 230);

    pattern_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(pattern_area, 900, 270);
    g_signal_connect(pattern_area, "draw", G_CALLBACK(draw_pattern), NULL);
    gtk_container_add(GTK_CONTAINER(pattern_scroll), pattern_area);

    GtkWidget *pattern_heading = gtk_label_new("Pattern View");
    gtk_widget_set_halign(pattern_heading, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(outer), pattern_heading,
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(outer), pattern_scroll, FALSE, TRUE, 0);

    GtkWidget *scope_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scope_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scope_scroll, -1, 140);
    scope_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(scope_area, 620, 180);
    g_signal_connect(scope_area, "draw", G_CALLBACK(draw_scopes), NULL);
    gtk_container_add(GTK_CONTAINER(scope_scroll), scope_area);
    gtk_box_pack_start(GTK_BOX(outer), scope_scroll, TRUE, TRUE, 0);

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
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sample_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sample_scroll, -1, 220);
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
    gtk_widget_set_size_request(sample_waveform_area, -1, 120);
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
    gtk_widget_set_size_request(comments_scroll, -1, 140);
    gtk_container_add(GTK_CONTAINER(comments_scroll), module_comments_view);
    gtk_box_pack_start(GTK_BOX(samples_page), comments_scroll,
                       TRUE, TRUE, 0);

    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), samples_page,
        gtk_label_new("Samples & Notes"));

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

    GtkWidget *about_title = gtk_label_new("XMP Module Player");
    gtk_widget_set_name(about_title, "about-title");
    gtk_box_pack_start(GTK_BOX(about_page), about_title,
                       FALSE, FALSE, 0);

    GtkWidget *about_description = gtk_label_new(
        "A native Linux tracker module player built with GTK3, SDL2, and libxmp.");
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

    if (argc > 1)
        open_module(argv[1]);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
