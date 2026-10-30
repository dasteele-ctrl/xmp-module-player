#ifndef MIDI_AUDIO_H
#define MIDI_AUDIO_H

#include <stdint.h>
#include <stddef.h>

typedef struct MidiAudio MidiAudio;

/* A sample-clocked FluidSynth renderer: no second audio device or timer. */
MidiAudio *midi_audio_open(const char *filename, const char *soundfont,
                           char *error, size_t error_size);
void midi_audio_close(MidiAudio *midi);

/* Interleaved, signed 16-bit stereo PCM, at 44100 samples/second. */
int midi_audio_read(MidiAudio *midi, int16_t *output, int frames);
int midi_audio_restart(MidiAudio *midi);
int midi_audio_seek(MidiAudio *midi, int64_t milliseconds);
int64_t midi_audio_position_ms(const MidiAudio *midi);
int64_t midi_audio_duration_ms(const MidiAudio *midi);

/* Returns a newly allocated path (g_free it), or NULL. */
char *midi_audio_find_default_soundfont(void);
int midi_audio_valid_soundfont(const char *filename);
int midi_audio_set_soundfont(MidiAudio *midi, const char *filename,
                              char *error, size_t error_size);
const char *midi_audio_soundfont(const MidiAudio *midi);

#endif
