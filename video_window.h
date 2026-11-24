#ifndef MODULEZ_VIDEO_WINDOW_H
#define MODULEZ_VIDEO_WINDOW_H

#include <gtk/gtk.h>
typedef struct VideoWindow VideoWindow;

gboolean video_filename(const char *path);
void video_add_patterns(GtkFileFilter *filter);
VideoWindow *video_open(GtkWindow *parent, const char *path, int volume, GError **error);
void video_add_controls(VideoWindow *video, GtkButton *play, GtkButton *stop,
                        GtkRange *seek, GtkRange *volume);
void video_close(VideoWindow *video);
void video_pause(VideoWindow *video, gboolean paused);
GtkWidget *player_mute_button_new(GtkRange *volume);
void video_volume(VideoWindow *video, int volume);
void video_seek(VideoWindow *video, double seconds);
/* Return TRUE once at EOF or failure. Error messages have static lifetime. */
gboolean video_position(VideoWindow *video, double *seconds, double *duration, const char **error);
#endif
