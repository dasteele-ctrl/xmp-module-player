#include "video_window.h"
#include <mpv/client.h>
#include <mpv/render.h>
#include <string.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

struct VideoWindow {
    GtkWindow *parent;
    GtkWidget *window, *area, *layout, *fullscreen_button;
    GtkWidget *overlay, *controls;
    GtkEventController *motion;
    gint64 last_activity;
    GtkButton *play_button;
    GtkRange *timeline;
    gboolean fullscreen, maximized;
    mpv_handle *mpv;
    mpv_render_context *renderer;
    cairo_surface_t *surface;
    gulong parent_configure;
    gboolean docked, dragging_from_dock;
};

static const char *extensions[] = {
    "mp4", "m4v", "mkv", "avi", "mov", "webm", "mpeg", "mpg", "ogv",
    "wmv", "asf", "flv", "vob", "ts", "mts", "m2ts", "3gp"
};

gboolean video_filename(const char *path)
{
    const char *dot = strrchr(path, '.');
    for (guint i = 0; dot && i < G_N_ELEMENTS(extensions); ++i)
        if (!g_ascii_strcasecmp(dot + 1, extensions[i])) return TRUE;
    return FALSE;
}

void video_add_patterns(GtkFileFilter *filter)
{
    for (guint i = 0; i < G_N_ELEMENTS(extensions); ++i) {
        GString *pattern = g_string_new("*.");
        for (const char *p = extensions[i]; *p; ++p)
            g_string_append_printf(pattern, "[%c%c]", *p, g_ascii_toupper(*p));
        gtk_file_filter_add_pattern(filter, pattern->str);
        g_string_free(pattern, TRUE);
    }
}

static void dock_point(VideoWindow *v, int *x, int *y)
{
    GdkRectangle bounds;
    gdk_window_get_frame_extents(gtk_widget_get_window(GTK_WIDGET(v->parent)), &bounds);
    *x = bounds.x + bounds.width;
    *y = bounds.y;
}

static gboolean follow_parent(GtkWidget *w, GdkEventConfigure *event, gpointer data)
{
    VideoWindow *v = data;
    (void)w; (void)event;
    if (v->docked && !v->fullscreen && !v->maximized) {
        v->dragging_from_dock = FALSE;
        int x, y;
        dock_point(v, &x, &y);
        gtk_window_move(GTK_WINDOW(v->window), x, y);
    }
    return FALSE;
}

static gboolean start_drag(GtkWidget *w, GdkEventButton *event, gpointer data)
{
    VideoWindow *v = data;
    (void)w;
    if (event->button != 1 || event->type != GDK_BUTTON_PRESS) return FALSE;
    v->dragging_from_dock = v->docked;
    /* GTK handles title-bar dragging and button clicks. */
    return FALSE;
}

static gboolean snap_near_parent(GtkWidget *w, GdkEventConfigure *event, gpointer data)
{
    VideoWindow *v = data;
    (void)event;
    if (v->fullscreen || v->maximized || (v->docked && !v->dragging_from_dock)) return FALSE;
    int x, y, left, top;
    dock_point(v, &x, &y);
    gtk_window_get_position(GTK_WINDOW(w), &left, &top);
    if (ABS(x - left) > 20 || ABS(y - top) > 36) {
        v->dragging_from_dock = FALSE;
        v->docked = FALSE;
    } else if (!v->docked) {
        v->docked = TRUE;
        gtk_window_move(GTK_WINDOW(w), x, y);
    }
    return FALSE;
}

static gboolean draw_picture(GtkWidget *area, cairo_t *cr, gpointer data)
{
    VideoWindow *v = data;
    int size[] = {gtk_widget_get_allocated_width(area), gtk_widget_get_allocated_height(area)};
    if (!v->surface || cairo_image_surface_get_width(v->surface) != size[0] ||
        cairo_image_surface_get_height(v->surface) != size[1]) {
        g_clear_pointer(&v->surface, cairo_surface_destroy);
        v->surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, size[0], size[1]);
    }
    if (cairo_surface_status(v->surface) != CAIRO_STATUS_SUCCESS) return FALSE;
    cairo_surface_flush(v->surface);
    size_t stride = cairo_image_surface_get_stride(v->surface);
    int block = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_SW_SIZE, size},
        {MPV_RENDER_PARAM_SW_FORMAT, G_BYTE_ORDER == G_LITTLE_ENDIAN ? "bgr0" : "0rgb"},
        {MPV_RENDER_PARAM_SW_STRIDE, &stride},
        {MPV_RENDER_PARAM_SW_POINTER, cairo_image_surface_get_data(v->surface)},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block}, {0, NULL}
    };
    mpv_render_context_render(v->renderer, params);
    cairo_surface_mark_dirty(v->surface);
    cairo_set_source_surface(cr, v->surface, 0, 0);
    cairo_paint(cr);
    return TRUE;
}

static void subtitle_error(VideoWindow *v, const char *action, int status)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(v->window),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
        "%s: %s", action, mpv_error_string(status));
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show(dialog);
}

static void subtitle_response(GtkDialog *dialog, gint response, gpointer data)
{
    VideoWindow *v = data;
    if (response == GTK_RESPONSE_ACCEPT) {
        g_autofree char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (path) {
            const char *args[] = {"sub-add", path, "select", NULL};
            int rc = mpv_command(v->mpv, args);
            if (rc < 0) subtitle_error(v, "Could not load subtitles", rc);
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static gboolean is_srt(const GtkFileFilterInfo *info, gpointer unused)
{
    (void)unused;
    const char *dot = strrchr(info->filename, '.');
    return dot && !g_ascii_strcasecmp(dot, ".srt");
}

static void choose_subtitles(GtkMenuItem *item, gpointer data)
{
    VideoWindow *v = data;
    (void)item;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Load subtitles", GTK_WINDOW(v->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Load", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "SubRip subtitles (*.srt)");
    gtk_file_filter_add_custom(filter, GTK_FILE_FILTER_FILENAME, is_srt, NULL, NULL);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    char *video_path = mpv_get_property_string(v->mpv, "path");
    if (video_path) {
        g_autofree char *folder = g_path_get_dirname(video_path);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), folder);
        mpv_free(video_path);
    }
    g_signal_connect(dialog, "response", G_CALLBACK(subtitle_response), v);
    gtk_widget_show(dialog);
}

static void subtitles_off(GtkMenuItem *item, gpointer data)
{
    VideoWindow *v = data;
    (void)item;
    int rc = mpv_set_property_string(v->mpv, "sid", "no");
    if (rc < 0) subtitle_error(v, "Could not turn subtitles off", rc);
}

static void show_controls(VideoWindow *v)
{
    v->last_activity = g_get_monotonic_time();
    gtk_widget_set_opacity(v->controls, 1.0);
    gtk_widget_show(v->controls);
}

static void pointer_moved(GtkEventControllerMotion *motion, double x, double y, gpointer data)
{
    VideoWindow *v = data;
    (void)motion;
    (void)x;
    (void)y;
    if (v->fullscreen)
        show_controls(v);
}

static void place_controls(VideoWindow *v)
{
    /* The fullscreen toolbar floats over the picture so fading never resizes it. */
    g_object_ref(v->controls);
    gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(v->controls)), v->controls);
    if (v->fullscreen) {
        gtk_widget_set_valign(v->controls, GTK_ALIGN_END);
        gtk_overlay_add_overlay(GTK_OVERLAY(v->overlay), v->controls);
    } else {
        gtk_widget_set_valign(v->controls, GTK_ALIGN_FILL);
        gtk_box_pack_end(GTK_BOX(v->layout), v->controls, FALSE, FALSE, 0);
    }
    g_object_unref(v->controls);
    show_controls(v);
}

static gboolean tick_picture(GtkWidget *area, GdkFrameClock *clock, gpointer data)
{
    VideoWindow *v = data;
    if (v->fullscreen && v->controls && gtk_widget_get_visible(v->controls)) {
        gint64 now = gdk_frame_clock_get_frame_time(clock);
        /* Keep the toolbar available during a slider drag or popup interaction. */
        if (gtk_window_group_get_current_grab(gtk_window_get_group(GTK_WINDOW(v->window))))
            v->last_activity = now;
        gint64 fade_elapsed = now - v->last_activity - 5 * G_USEC_PER_SEC;
        if (fade_elapsed >= 250000)
            gtk_widget_hide(v->controls);
        else if (fade_elapsed > 0)
            gtk_widget_set_opacity(v->controls, 1.0 - fade_elapsed / 250000.0);
    }
    if (mpv_render_context_update(v->renderer) & MPV_RENDER_UPDATE_FRAME)
        gtk_widget_queue_draw(area);
    return G_SOURCE_CONTINUE;
}

static void toggle_fullscreen(VideoWindow *v)
{
    if (v->fullscreen)
        gtk_window_unfullscreen(GTK_WINDOW(v->window));
    else
        gtk_window_fullscreen(GTK_WINDOW(v->window));
}

static gboolean window_state(GtkWidget *widget, GdkEventWindowState *event, gpointer data)
{
    VideoWindow *v = data;
    (void)widget;
    v->maximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
    if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
        v->fullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
        if (v->controls)
            place_controls(v);
        if (v->fullscreen_button) {
            gtk_button_set_image(GTK_BUTTON(v->fullscreen_button), gtk_image_new_from_icon_name(
                v->fullscreen ? "view-restore-symbolic" : "view-fullscreen-symbolic", GTK_ICON_SIZE_BUTTON));
            gtk_widget_set_tooltip_text(v->fullscreen_button, v->fullscreen ? "Leave fullscreen" : "Fullscreen");
        }
    }
    if (event->changed_mask & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN))
        follow_parent(NULL, NULL, v);
    return FALSE;
}

static gboolean picture_click(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    if (event->button != 1 || event->type != GDK_2BUTTON_PRESS) return FALSE;
    toggle_fullscreen(data);
    return TRUE;
}

static gboolean video_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    VideoWindow *v = data;
    (void)widget;
    if (v->fullscreen)
        show_controls(v);
    if (!(event->state & gtk_accelerator_get_default_mod_mask())) {
        switch (event->keyval) {
        case GDK_KEY_space:
            gtk_button_clicked(v->play_button);
            return TRUE;
        case GDK_KEY_Left:
        case GDK_KEY_Right:
            if (gtk_widget_is_sensitive(GTK_WIDGET(v->timeline))) {
                double position_ms = gtk_range_get_value(v->timeline);
                double offset_ms = event->keyval == GDK_KEY_Left ? -5000.0 : 5000.0;
                gtk_range_set_value(v->timeline, position_ms + offset_ms);
            }
            return TRUE;
        }
    }
    if (event->keyval != GDK_KEY_F11 && !(event->keyval == GDK_KEY_Escape && v->fullscreen))
        return FALSE;
    toggle_fullscreen(v);
    return TRUE;
}

static gboolean stop_on_video_close(GtkWidget *window, GdkEvent *event, gpointer data)
{
    (void)window;
    (void)event;
    gtk_button_clicked(GTK_BUTTON(data));
    return TRUE;
}

void video_add_controls(VideoWindow *v, GtkButton *play, GtkButton *stop,
                        GtkRange *seek, GtkRange *volume)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *toggle = gtk_button_new();
    g_object_bind_property(play, "label", toggle, "label", G_BINDING_SYNC_CREATE);
    g_signal_connect_swapped(toggle, "clicked", G_CALLBACK(gtk_button_clicked), play);
    gtk_box_pack_start(GTK_BOX(bar), toggle, FALSE, FALSE, 0);

    GtkWidget *stop_button = gtk_button_new_from_icon_name("media-playback-stop-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(stop_button, "Stop");
    g_signal_connect_swapped(stop_button, "clicked", G_CALLBACK(gtk_button_clicked), stop);
    g_signal_connect_object(v->window, "delete-event",
                            G_CALLBACK(stop_on_video_close), G_OBJECT(stop), 0);
    gtk_box_pack_start(GTK_BOX(bar), stop_button, FALSE, FALSE, 0);

    GtkWidget *timeline = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, gtk_range_get_adjustment(seek));
    v->play_button = play;
    v->timeline = GTK_RANGE(timeline);
    g_signal_connect(v->window, "key-press-event", G_CALLBACK(video_key), v);
    gtk_scale_set_draw_value(GTK_SCALE(timeline), FALSE);
    gtk_widget_set_tooltip_text(timeline, "Seek");
    g_object_bind_property(seek, "sensitive", timeline, "sensitive", G_BINDING_SYNC_CREATE);
    gtk_box_pack_start(GTK_BOX(bar), timeline, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(bar), player_mute_button_new(volume), FALSE, FALSE, 0);
    GtkWidget *sound = gtk_volume_button_new();
    gtk_scale_button_set_adjustment(GTK_SCALE_BUTTON(sound), gtk_range_get_adjustment(volume));
    gtk_box_pack_start(GTK_BOX(bar), sound, FALSE, FALSE, 0);

    GtkWidget *subtitles = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(subtitles), "Subtitles");
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *load = gtk_menu_item_new_with_label("Load .srt…");
    GtkWidget *off = gtk_menu_item_new_with_label("Off");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), load);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), off);
    g_signal_connect(load, "activate", G_CALLBACK(choose_subtitles), v);
    g_signal_connect(off, "activate", G_CALLBACK(subtitles_off), v);
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(subtitles), menu);
    gtk_box_pack_start(GTK_BOX(bar), subtitles, FALSE, FALSE, 0);

    v->fullscreen_button = gtk_button_new_from_icon_name("view-fullscreen-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(v->fullscreen_button, "Fullscreen");
    g_signal_connect_swapped(v->fullscreen_button, "clicked", G_CALLBACK(toggle_fullscreen), v);
    gtk_box_pack_start(GTK_BOX(bar), v->fullscreen_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(v->layout), bar, FALSE, FALSE, 0);
    v->controls = bar;
    v->motion = gtk_event_controller_motion_new(v->window);
    gtk_event_controller_set_propagation_phase(v->motion, GTK_PHASE_CAPTURE);
    g_signal_connect(v->motion, "motion", G_CALLBACK(pointer_moved), v);
    gtk_widget_show_all(bar);
    if (v->fullscreen)
        place_controls(v);
}

void video_close(VideoWindow *v)
{
    if (!v) return;
    g_clear_object(&v->motion);
    if (v->parent_configure) g_signal_handler_disconnect(v->parent, v->parent_configure);
    if (v->window) gtk_widget_destroy(v->window);
    if (v->renderer) mpv_render_context_free(v->renderer);
    if (v->mpv) mpv_terminate_destroy(v->mpv);
    g_clear_pointer(&v->surface, cairo_surface_destroy);
    g_free(v);
}

VideoWindow *video_open(GtkWindow *parent, const char *path, int volume, GError **error)
{
    VideoWindow *v = g_new0(VideoWindow, 1);
    v->parent = parent;
    v->mpv = mpv_create();
    int rc = MPV_ERROR_NOMEM;
    if (!v->mpv) goto fail;
    mpv_set_option_string(v->mpv, "vo", "libmpv");
    mpv_set_option_string(v->mpv, "autoload-files", "yes");
    mpv_set_option_string(v->mpv, "sub-auto", "exact");
    mpv_set_option_string(v->mpv, "audio-display", "no");
    mpv_set_option_string(v->mpv, "pause", "yes");
    rc = mpv_initialize(v->mpv);
    if (rc < 0) goto fail;
    mpv_render_param init[] = {{MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_SW}, {0, NULL}};
    rc = mpv_render_context_create(&v->renderer, v->mpv, init);
    if (rc < 0) goto fail;
    video_volume(v, volume);
    const char *load[] = {"loadfile", path, NULL};
    rc = mpv_command(v->mpv, load);
    if (rc < 0) goto fail;

    v->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_group_add_window(gtk_window_get_group(parent), GTK_WINDOW(v->window));
    gtk_window_set_title(GTK_WINDOW(v->window), "Modulez Video");
    GtkWidget *bar = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(bar), "Modulez Video");
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(bar), FALSE);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(bar), ":minimize,maximize,close");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), TRUE);
    gtk_widget_add_events(bar, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(bar, "button-press-event", G_CALLBACK(start_drag), v);
    gtk_widget_set_name(bar, "modulez-dialog-titlebar");
    gtk_widget_set_name(v->window, "modulez-dialog-window");
    gtk_window_set_titlebar(GTK_WINDOW(v->window), bar);
    gtk_style_context_add_class(gtk_widget_get_style_context(v->window), "modulez-modern-dialog");
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "modulez-dialog-titlebar");
    gtk_window_set_default_size(GTK_WINDOW(v->window), 480, 300);
    v->area = gtk_drawing_area_new();
    v->layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(v->window), v->layout);
    v->overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(v->overlay), v->area);
    gtk_box_pack_start(GTK_BOX(v->layout), v->overlay, TRUE, TRUE, 0);
    gtk_widget_add_events(v->area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(v->area, "button-press-event", G_CALLBACK(picture_click), v);
    g_signal_connect(v->window, "window-state-event", G_CALLBACK(window_state), v);
    g_signal_connect(v->area, "draw", G_CALLBACK(draw_picture), v);
    gtk_widget_add_tick_callback(v->area, tick_picture, v, NULL);
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(gtk_widget_get_display(v->window))) {
        v->docked = TRUE;
        v->parent_configure = g_signal_connect(parent, "configure-event", G_CALLBACK(follow_parent), v);
        g_signal_connect(v->window, "configure-event", G_CALLBACK(snap_near_parent), v);
        int x, y;
        dock_point(v, &x, &y);
        GdkMonitor *monitor = gdk_display_get_monitor_at_window(
            gtk_widget_get_display(v->window), gtk_widget_get_window(GTK_WIDGET(parent)));
        GdkRectangle work;
        gdk_monitor_get_workarea(monitor, &work);
        int left, top;
        gtk_window_get_position(parent, &left, &top);
        int width = MIN(480, work.width - (x - left));
        if (width >= 200) {
            gtk_window_set_default_size(GTK_WINDOW(v->window), width, 300);
            int shift = MAX(0, x + width - (work.x + work.width));
            if (shift) gtk_window_move(parent, left - shift, top);
            gtk_window_move(GTK_WINDOW(v->window), x - shift, y);
        } else {
            v->docked = FALSE;
            gtk_window_set_position(GTK_WINDOW(v->window), GTK_WIN_POS_CENTER_ON_PARENT);
        }
    }
#endif
    gtk_widget_show_all(v->window);
    return v;
fail:
    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, mpv_error_string(rc));
    video_close(v);
    return NULL;
}

void video_pause(VideoWindow *v, gboolean paused)
{
    int flag = !!paused;
    mpv_set_property(v->mpv, "pause", MPV_FORMAT_FLAG, &flag);
    if (!paused) gtk_widget_show(v->window);
}

/* Both windows operate on the main volume slider's adjustment. */
static int last_audible_volume = 100;

static void update_mute_button(GtkAdjustment *level, GtkButton *button)
{
    int volume = (int)gtk_adjustment_get_value(level);
    if (volume > 0)
        last_audible_volume = volume;
    const char *label = volume > 0 ? "Mute" : "Unmute";
    if (g_strcmp0(gtk_button_get_label(button), label))
        gtk_button_set_label(button, label);
}

static void toggle_mute(GtkButton *button, GtkAdjustment *level)
{
    (void)button;
    gtk_adjustment_set_value(level, gtk_adjustment_get_value(level) > 0
                                   ? 0 : last_audible_volume);
}

GtkWidget *player_mute_button_new(GtkRange *volume)
{
    GtkAdjustment *level = gtk_range_get_adjustment(volume);
    GtkWidget *button = gtk_button_new();
    g_signal_connect_object(level, "value-changed", G_CALLBACK(update_mute_button), button, 0);
    g_signal_connect_object(button, "clicked", G_CALLBACK(toggle_mute), level, 0);
    update_mute_button(level, GTK_BUTTON(button));
    return button;
}

void video_volume(VideoWindow *v, int volume)
{
    double value = volume;
    mpv_set_property(v->mpv, "volume", MPV_FORMAT_DOUBLE, &value);
}

void video_seek(VideoWindow *v, double seconds)
{
    char value[G_ASCII_DTOSTR_BUF_SIZE];
    const char *args[] = {"seek", g_ascii_dtostr(value, sizeof(value), seconds), "absolute+exact", NULL};
    mpv_command(v->mpv, args);
}

gboolean video_position(VideoWindow *v, double *seconds, double *duration, const char **error)
{
    mpv_event *event;
    while ((event = mpv_wait_event(v->mpv, 0))->event_id != MPV_EVENT_NONE) {
        if (event->event_id == MPV_EVENT_END_FILE) {
            mpv_event_end_file *end = event->data;
            if (end->reason == MPV_END_FILE_REASON_ERROR) *error = mpv_error_string(end->error);
            if (*error || end->reason == MPV_END_FILE_REASON_EOF) return TRUE;
        }
    }
    mpv_get_property(v->mpv, "time-pos", MPV_FORMAT_DOUBLE, seconds);
    mpv_get_property(v->mpv, "duration", MPV_FORMAT_DOUBLE, duration);
    return FALSE;
}
