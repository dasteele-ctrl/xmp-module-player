#include "visualizer_pcm.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#include <stdio.h>
static atomic_bool done;
static unsigned received, previous;
static void *produce(void *data)
{
    (void)data;
    for (unsigned sequence = 1; sequence <= 200000; sequence++) {
        int16_t stereo[] = {(int16_t)(sequence & 65535u), (int16_t)(sequence >> 16)};
        modulez_vis_pcm_push(stereo, 1);
    }
    atomic_store_explicit(&done, true, memory_order_release);
    return NULL;
}
static void verify(const int16_t *pcm, size_t n)
{
    assert(n == 1);
    unsigned sequence = (uint16_t)pcm[0] | ((unsigned)(uint16_t)pcm[1] << 16);
    assert(sequence > previous && sequence <= 200000);
    previous = sequence;
    received++;
}
static void *consume(void *data)
{
    (void)data;
    int16_t pcm[MODULEZ_VIS_PCM_FRAMES * 2];
    for (;;) {
        size_t n = modulez_vis_pcm_pop(pcm);
        if (n) verify(pcm, n);
        else if (atomic_load_explicit(&done, memory_order_acquire)) break;
        else sched_yield();
    }
    return NULL;
}
int main(void)
{
    int16_t input[MODULEZ_VIS_PCM_FRAMES * 4], output[MODULEZ_VIS_PCM_FRAMES * 2];
    for (unsigned i = 0; i < sizeof input / sizeof input[0]; i++) input[i] = (int16_t)(i - 2000);
    modulez_vis_pcm_push(input, 2);
    assert(modulez_vis_pcm_pop(output) == 0); /* Disabled by default. */
    modulez_vis_pcm_enable(true);
    modulez_vis_pcm_push(input, MODULEZ_VIS_PCM_FRAMES + 7);
    assert(modulez_vis_pcm_pop(output) == MODULEZ_VIS_PCM_FRAMES);
    for (unsigned i = 0; i < MODULEZ_VIS_PCM_FRAMES * 2; i++) assert(output[i] == input[i]);
    assert(modulez_vis_pcm_pop(output) == 7);
    for (unsigned i = 0; i < 14; i++) assert(output[i] == input[MODULEZ_VIS_PCM_FRAMES * 2 + i]);
    assert(modulez_vis_pcm_pop(output) == 0);
    for (int i = 0; i < 40; i++) modulez_vis_pcm_push(input, 1);
    int blocks = 0;
    while (modulez_vis_pcm_pop(output)) blocks++;
    assert(blocks == 16); /* Saturation drops new data and returns. */
    modulez_vis_pcm_enable(false);
    modulez_vis_pcm_push(input, 1);
    assert(!modulez_vis_pcm_pop(output));
    modulez_vis_pcm_enable(true);
    pthread_t producer, consumer;
    assert(!pthread_create(&producer, NULL, produce, NULL));
    assert(!pthread_create(&consumer, NULL, consume, NULL));
    assert(!pthread_join(producer, NULL));
    assert(!pthread_join(consumer, NULL));
    size_t n;
    while ((n = modulez_vis_pcm_pop(output))) verify(output, n);
    assert(received > 0);
    printf("Vis PCM: splitting, exact samples, bounded drops, enable/disable, and concurrent ordering passed (%u packets).\n", received);
    return 0;
}
