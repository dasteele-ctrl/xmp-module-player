#include "visualizer_pcm.h"
#include <stdatomic.h>
#include <string.h>
#define SLOTS 16u
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Visualizer PCM needs lock-free integer atomics");
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "Visualizer PCM needs lock-free boolean atomics");
static struct { size_t frames; int16_t pcm[MODULEZ_VIS_PCM_FRAMES * 2]; } slots[SLOTS];
static atomic_uint written, consumed;
static atomic_bool enabled;
void modulez_vis_pcm_enable(bool value)
{
    atomic_store_explicit(&enabled, value, memory_order_release);
}
void modulez_vis_pcm_push(const int16_t *stereo, size_t frames)
{
    if (!stereo || !atomic_load_explicit(&enabled, memory_order_acquire)) return;
    while (frames) {
        unsigned w = atomic_load_explicit(&written, memory_order_relaxed);
        unsigned r = atomic_load_explicit(&consumed, memory_order_acquire);
        if (w - r >= SLOTS) return; /* Drop visuals rather than wait for GTK. */
        size_t n = frames > MODULEZ_VIS_PCM_FRAMES ? MODULEZ_VIS_PCM_FRAMES : frames;
        slots[w % SLOTS].frames = n;
        memcpy(slots[w % SLOTS].pcm, stereo, n * 2 * sizeof(int16_t));
        atomic_store_explicit(&written, w + 1, memory_order_release);
        stereo += n * 2;
        frames -= n;
    }
}
size_t modulez_vis_pcm_pop(int16_t stereo[MODULEZ_VIS_PCM_FRAMES * 2])
{
    unsigned r = atomic_load_explicit(&consumed, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&written, memory_order_acquire);
    if (w == r) return 0;
    size_t n = slots[r % SLOTS].frames;
    memcpy(stereo, slots[r % SLOTS].pcm, n * 2 * sizeof(int16_t));
    atomic_store_explicit(&consumed, r + 1, memory_order_release);
    return n;
}
