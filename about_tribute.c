#include "about_tribute.h"
#include <math.h>
#include <stdlib.h>
#include "about_tribute_logo.h"

#define POINT_COUNT G_N_ELEMENTS(logo_points)
#define LOOP_COUNT (G_N_ELEMENTS(logo_offsets) - 1)
#define CLICK_WINDOW_US (2 * G_USEC_PER_SEC)

typedef struct { double x, y, z; } Point;
typedef struct { Point p[4]; double depth, shade; } Face;
typedef struct {
    GtkWidget *area, *pause;
    guint timer;
    gint64 last;
    double elapsed;
    gboolean paused, minimized;
} Tribute;
typedef struct {
    unsigned clicks;
    gint64 first_click;
    GtkWidget *dialog;
} Trigger;

static Point rotate(double x, double y, double z, double t)
{
    double ay = sin(t * .36) * .48, ax = .28 + sin(t * .53) * .18;
    double az = sin(t * .29) * .055;
    double a = x * cos(ay) + z * sin(ay), b = -x * sin(ay) + z * cos(ay);
    double c = y * cos(ax) - b * sin(ax), d = y * sin(ax) + b * cos(ax);
    return (Point){a * cos(az) - c * sin(az), a * sin(az) + c * cos(az), d};
}

static Point project(Point p, double width, double height)
{
    double scale = MAX(1, width - 52) / 830.0;
    double k = 950 / (950 + p.z);
    return (Point){width / 2 + p.x * scale * k,
                   height * .4633 + p.y * scale * k, p.z};
}

static int far_first(const void *a, const void *b)
{
    double d = ((const Face *)b)->depth - ((const Face *)a)->depth;
    return (d > 0) - (d < 0);
}

static void outline(cairo_t *cr, const Point *points, double w, double h)
{
    cairo_new_path(cr);
    for (unsigned l = 0; l < LOOP_COUNT; ++l) {
        for (unsigned i = logo_offsets[l]; i < logo_offsets[l + 1]; ++i) {
            Point p = project(points[i], w, h);
            if (i == logo_offsets[l]) cairo_move_to(cr, p.x, p.y);
            else cairo_line_to(cr, p.x, p.y);
        }
        cairo_close_path(cr);
    }
}

static gboolean draw_tribute(GtkWidget *area, cairo_t *cr, gpointer data)
{
    Tribute *demo = data;
    double w = gtk_widget_get_allocated_width(area), h = gtk_widget_get_allocated_height(area);
    double t = demo->elapsed;
    Point front[POINT_COUNT], back[POINT_COUNT];
    Face faces[POINT_COUNT];
    unsigned count = 0;
    /* Read the current palette on every draw, including after a live theme change. */
    GtkStyleContext *style = gtk_widget_get_style_context(area);
    GdkRGBA background = {16/255.0, 18/255.0, 21/255.0, 1};
    GdkRGBA accent = {.467, .467, .467, 1}, foreground;
    gtk_style_context_lookup_color(style, "modulez_tribute_background", &background);
    gtk_style_context_lookup_color(style, "modulez_tribute_accent", &accent);
    gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &foreground);
    /* Use hue rather than accent brightness to keep the metal legible.
     * A neutral accent retains the original silver finish. */
    double peak = MAX(accent.red, MAX(accent.green, accent.blue));
    double tint[3] = {1, 1, 1};
    if (peak > 0) {
        tint[0] = .65 + .35 * accent.red / peak;
        tint[1] = .65 + .35 * accent.green / peak;
        tint[2] = .65 + .35 * accent.blue / peak;
    }
    gdk_cairo_set_source_rgba(cr, &background);
    cairo_paint(cr);
    for (unsigned i = 0; i < POINT_COUNT; ++i) {
        front[i] = rotate(logo_points[i][0], logo_points[i][1], -12, t);
        back[i] = rotate(logo_points[i][0], logo_points[i][1], 12, t);
    }
    for (unsigned l = 0; l < LOOP_COUNT; ++l) {
        unsigned start = logo_offsets[l], end = logo_offsets[l + 1];
        for (unsigned i = start; i < end; ++i) {
            unsigned j = i + 1 == end ? start : i + 1;
            double dx = logo_points[j][0] - logo_points[i][0];
            double dy = logo_points[j][1] - logo_points[i][1];
            double len = MAX(1, hypot(dx, dy));
            faces[count++] = (Face){{front[i], front[j], back[j], back[i]},
                (front[i].z + front[j].z + back[j].z + back[i].z) / 4,
                58 + 48 * (.5 + .5 * (-dy / len * .45 - dx / len * .8))};
        }
    }
    qsort(faces, count, sizeof(Face), far_first);
    for (unsigned i = 0; i < count; ++i) {
        cairo_new_path(cr);
        for (unsigned j = 0; j < 4; ++j) {
            Point p = project(faces[i].p[j], w, h);
            if (!j) cairo_move_to(cr, p.x, p.y);
            else cairo_line_to(cr, p.x, p.y);
        }
        cairo_close_path(cr);
        double s = faces[i].shade;
        cairo_set_source_rgb(cr, (s-6)/255*tint[0], s/255*tint[1], (s+7)/255*tint[2]);
        cairo_fill(cr);
    }
    outline(cr, front, w, h);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_pattern_t *silver = cairo_pattern_create_linear(0, h*.4633-39, 0, h*.4633+41);
    const double stops[][4] = {{0,170,178,189},{.25,226,229,233},{.46,166,175,184},
        {.52,101,113,125},{.8,184,192,201},{1,224,227,232}};
    for (unsigned i = 0; i < G_N_ELEMENTS(stops); ++i)
        cairo_pattern_add_color_stop_rgb(silver, stops[i][0], stops[i][1]/255*tint[0],
                                        stops[i][2]/255*tint[1], stops[i][3]/255*tint[2]);
    cairo_set_source(cr, silver);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(silver);
    cairo_set_source_rgb(cr, 201/255.0*tint[0], 207/255.0*tint[1], 214/255.0*tint[2]);
    cairo_set_line_width(cr, .7);
    cairo_stroke(cr);
    cairo_save(cr);
    outline(cr, front, w, h);
    cairo_clip(cr);
    double gx = w/2 + sin(t*.44)*w*.48;
    cairo_pattern_t *glint = cairo_pattern_create_linear(gx-90, 0, gx+90, 0);
    cairo_pattern_add_color_stop_rgba(glint, 0, 1, 1, 1, 0);
    cairo_pattern_add_color_stop_rgba(glint, .5, 1, 1, 1, .21);
    cairo_pattern_add_color_stop_rgba(glint, 1, 1, 1, 1, 0);
    cairo_set_source(cr, glint);
    cairo_paint(cr);
    cairo_pattern_destroy(glint);
    cairo_restore(cr);

    /* Separate text keeps credits editable without changing the logo geometry. */
    static const char credits[] = "IN MEMORY OF MODULEZ.ORG   \xe2\x80\xa2   MODULEZ PLAYER   "
        "\xe2\x80\xa2   CREATED BY DASTEELE   \xe2\x80\xa2   GREETINGS TO THE TRACKER MUSICIANS   "
        "\xe2\x80\xa2   KEEP THE SCENE ALIVE   \xe2\x80\xa2   ";
    cairo_save(cr);
    cairo_rectangle(cr, 20, h-56, MAX(0, w-40), 35);
    cairo_clip(cr);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    gdk_cairo_set_source_rgba(cr, &foreground);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "M", &ext);
    double advance = MAX(1, ext.x_advance), total = g_utf8_strlen(credits, -1)*advance;
    double start = -fmod(t*29, total);
    for (double offset = start; offset < w; offset += total) {
        unsigned i = 0;
        for (const char *p = credits; *p; p = g_utf8_next_char(p), ++i) {
            char glyph[7] = {0};
            g_unichar_to_utf8(g_utf8_get_char(p), glyph);
            double x = offset + i*advance;
            if (x < -advance || x > w) continue;
            cairo_move_to(cr, x, h-35 + sin(x*.018-t*.9)*3);
            cairo_show_text(cr, glyph);
        }
    }
    cairo_restore(cr);
    return FALSE;
}

static void style_updated(GtkWidget *widget, gpointer data)
{
    (void)data;
    /* Animation may be paused or disabled when the palette changes. */
    gtk_widget_queue_draw(widget);
}

static gboolean animate(gpointer data)
{
    Tribute *demo = data;
    gint64 now = g_get_monotonic_time();
    if (demo->last) demo->elapsed += MIN((now-demo->last)/1e6, .1);
    demo->last = now;
    gtk_widget_queue_draw(demo->area);
    return G_SOURCE_CONTINUE;
}

static void stop(Tribute *demo)
{
    if (demo->timer) { g_source_remove(demo->timer); demo->timer = 0; }
    demo->last = 0;
}

static void sync_animation(Tribute *demo)
{
    stop(demo);
    if (!demo->paused && !demo->minimized && gtk_widget_get_mapped(demo->area))
        demo->timer = g_timeout_add(42, animate, demo);
}

static void on_map(GtkWidget *widget, gpointer data)
{ (void)widget; sync_animation(data); }
static void on_unmap(GtkWidget *widget, gpointer data)
{ (void)widget; stop(data); }
static gboolean on_window_state(GtkWidget *widget, GdkEventWindowState *event, gpointer data)
{
    (void)widget;
    Tribute *demo = data;
    demo->minimized = (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) != 0;
    sync_animation(demo);
    return FALSE;
}
static void pause_clicked(GtkButton *button, gpointer data)
{
    Tribute *demo = data;
    demo->paused = !demo->paused;
    gtk_button_set_label(button, demo->paused ? "Play animation" : "Pause animation");
    sync_animation(demo);
}
static void response(GtkDialog *dialog, gint response_id, gpointer data)
{ (void)response_id; (void)data; gtk_widget_destroy(GTK_WIDGET(dialog)); }
static void destroyed(GtkWidget *widget, gpointer data)
{
    Trigger *trigger = data;
    Tribute *demo = g_object_get_data(G_OBJECT(widget), "modulez-tribute");
    stop(demo);
    trigger->dialog = NULL;
}
static gboolean escape(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)data;
    if (event->keyval != GDK_KEY_Escape) return FALSE;
    gtk_widget_destroy(widget);
    return TRUE;
}
static void open_tribute(GtkWidget *target, Trigger *trigger)
{
    if (trigger->dialog) { gtk_window_present(GTK_WINDOW(trigger->dialog)); return; }
    GtkWidget *top = gtk_widget_get_toplevel(target);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Modulez — In memory of modulez.org",
        GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(dialog), "xmp-dialog");
    gtk_style_context_add_class(gtk_widget_get_style_context(
        gtk_dialog_get_content_area(GTK_DIALOG(dialog))), "xmp-dialog-surface");
    gtk_style_context_add_class(gtk_widget_get_style_context(
        gtk_dialog_get_action_area(GTK_DIALOG(dialog))), "xmp-dialog-surface");
    trigger->dialog = dialog;
    Tribute *demo = g_new0(Tribute, 1);
    g_object_set_data_full(G_OBJECT(dialog), "modulez-tribute", demo, g_free);
    gboolean animations = TRUE;
    g_object_get(gtk_widget_get_settings(target), "gtk-enable-animations", &animations, NULL);
    demo->paused = !animations;
    demo->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(demo->area, 320, 220);
    atk_object_set_name(gtk_widget_get_accessible(demo->area), "Animated Modulez logo and memorial credits");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 736, 360);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(content), demo->area, TRUE, TRUE, 0);
    demo->pause = gtk_button_new_with_label(demo->paused ? "Play animation" : "Pause animation");
    gtk_widget_set_halign(demo->pause, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), demo->pause, FALSE, FALSE, 6);
    g_signal_connect(demo->area, "style-updated", G_CALLBACK(style_updated), NULL);
    g_signal_connect(demo->area, "draw", G_CALLBACK(draw_tribute), demo);
    g_signal_connect(demo->area, "map", G_CALLBACK(on_map), demo);
    g_signal_connect(demo->area, "unmap", G_CALLBACK(on_unmap), demo);
    g_signal_connect(demo->pause, "clicked", G_CALLBACK(pause_clicked), demo);
    g_signal_connect(dialog, "window-state-event", G_CALLBACK(on_window_state), demo);
    g_signal_connect(dialog, "key-press-event", G_CALLBACK(escape), NULL);
    g_signal_connect(dialog, "response", G_CALLBACK(response), NULL);
    g_signal_connect(dialog, "destroy", G_CALLBACK(destroyed), trigger);
    gtk_widget_show_all(dialog);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
}

static gboolean logo_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    /* GTK also emits 2BUTTON/3BUTTON_PRESS: count only physical presses. */
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY) return FALSE;
    Trigger *trigger = data;
    gint64 now = g_get_monotonic_time();
    if (!trigger->clicks || now-trigger->first_click > CLICK_WINDOW_US) {
        trigger->clicks = 0;
        trigger->first_click = now;
    }
    if (++trigger->clicks == 3) {
        trigger->clicks = 0;
        open_tribute(widget, trigger);
    }
    return TRUE;
}
static void reset_clicks(GtkWidget *widget, gpointer data)
{ (void)widget; ((Trigger *)data)->clicks = 0; }
static void trigger_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    Trigger *trigger = data;
    if (trigger->dialog) gtk_widget_destroy(trigger->dialog);
    g_free(trigger);
}
GtkWidget *modulez_about_tribute_wrap(GtkWidget *image)
{
    GtkWidget *target = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(target), FALSE);
    gtk_widget_set_halign(target, GTK_ALIGN_CENTER);
    gtk_widget_add_events(target, GDK_BUTTON_PRESS_MASK);
    gtk_container_add(GTK_CONTAINER(target), image);
    Trigger *trigger = g_new0(Trigger, 1);
    g_signal_connect(target, "button-press-event", G_CALLBACK(logo_clicked), trigger);
    g_signal_connect(target, "unmap", G_CALLBACK(reset_clicks), trigger);
    g_signal_connect(target, "destroy", G_CALLBACK(trigger_destroyed), trigger);
    return target;
}
