#ifndef MODULEZ_VISUALIZER_PCM_H
#define MODULEZ_VISUALIZER_PCM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MODULEZ_VIS_PCM_FRAMES 1024
/* One producer (SDL callback), one consumer (GTK thread). Never blocks. */
void modulez_vis_pcm_enable(bool enabled);
void modulez_vis_pcm_push(const int16_t *stereo, size_t frames);
size_t modulez_vis_pcm_pop(int16_t stereo[MODULEZ_VIS_PCM_FRAMES * 2]);
#endif
