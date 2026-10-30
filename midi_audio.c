#include "midi_audio.h"

#include <fluidsynth.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define MIDI_RATE 44100
#define MIDI_TAIL_MS 2000
#define MAX_MIDI_FILE (32u * 1024u * 1024u)

typedef struct {
    uint64_t tick;
    uint32_t microseconds_per_quarter;
    unsigned int order;
} TempoEvent;

struct MidiAudio {
    fluid_settings_t *settings;
    fluid_synth_t *synth;
    fluid_player_t *player;
    char *filename;
    char *soundfont;
    int soundfont_id;
    GArray *tempos;
    uint16_t division;
    uint64_t total_ticks;
    int64_t duration_ms;
    int64_t position_frames;
};

static void set_error(char *buffer, size_t size, const char *message)
{
    if (buffer && size)
        g_snprintf(buffer, size, "%s", message);
}

static uint16_t be16(const guint8 *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const guint8 *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int varlen(const guint8 **cursor, const guint8 *end, uint32_t *value)
{
    *value = 0;
    for (int i = 0; i < 4; ++i) {
        if (*cursor >= end)
            return 0;
        guint8 byte = *(*cursor)++;
        *value = (*value << 7) | (byte & 0x7f);
        if (!(byte & 0x80))
            return 1;
    }
    return 0;
}

static int compare_tempos(gconstpointer a, gconstpointer b)
{
    const TempoEvent *one = a, *two = b;
    if (one->tick != two->tick)
        return one->tick < two->tick ? -1 : 1;
    if (one->order != two->order)
        return one->order < two->order ? -1 : 1;
    return 0;
}

/* Read the SMF tempo map so a file with tempo changes seeks in real time,
 * rather than treating the whole song as 120 BPM. Format 2 (independent
 * sequences) and SMPTE time divisions are intentionally not supported. */
static int parse_midi_timing(MidiAudio *midi, const char *filename,
                             char *error, size_t error_size)
{
    gchar *contents = NULL;
    gsize size = 0;
    GError *io_error = NULL;
    GStatBuf statbuf;
    if (g_stat(filename, &statbuf) != 0 || statbuf.st_size <= 0 ||
        (uint64_t)statbuf.st_size > MAX_MIDI_FILE) {
        set_error(error, error_size, "MIDI file is missing or too large (32 MB limit)");
        return 0;
    }
    if (!g_file_get_contents(filename, &contents, &size, &io_error)) {
        set_error(error, error_size,
                  io_error ? io_error->message : "Could not read MIDI file");
        g_clear_error(&io_error);
        return 0;
    }

    const guint8 *p = (const guint8 *)contents;
    const guint8 *end = p + size;
    int valid = 0;
    if (size < 14 || memcmp(p, "MThd", 4) != 0 || be32(p + 4) < 6 ||
        (uint64_t)be32(p + 4) > size - 8)
        goto out;

    uint16_t format = be16(p + 8);
    uint16_t tracks = be16(p + 10);
    midi->division = be16(p + 12);
    if (format > 1 || tracks == 0 || (midi->division & 0x8000) ||
        midi->division == 0)
        goto out;
    p += 8 + be32(p + 4);

    midi->tempos = g_array_new(FALSE, FALSE, sizeof(TempoEvent));
    TempoEvent initial = {0, 500000, 0};
    g_array_append_val(midi->tempos, initial);
    unsigned int order = 1;

    for (uint16_t track = 0; track < tracks; ++track) {
        if (end - p < 8 || memcmp(p, "MTrk", 4) != 0 ||
            be32(p + 4) > (uint64_t)(end - p - 8))
            goto out;
        const guint8 *track_end = p + 8 + be32(p + 4);
        p += 8;
        uint64_t tick = 0;
        guint8 running = 0;

        while (p < track_end) {
            uint32_t delta = 0, length = 0;
            if (!varlen(&p, track_end, &delta))
                goto out;
            tick += delta;
            if (tick > midi->total_ticks)
                midi->total_ticks = tick;
            if (p >= track_end)
                goto out;

            guint8 status = *p;
            if (status & 0x80) {
                ++p;
                running = status < 0xf0 ? status : 0;
            } else if (running) {
                status = running;
            } else {
                goto out;
            }

            if (status == 0xff) {
                if (p >= track_end)
                    goto out;
                guint8 type = *p++;
                if (!varlen(&p, track_end, &length) ||
                    length > (uint64_t)(track_end - p))
                    goto out;
                if (type == 0x51 && length == 3) {
                    uint32_t tempo = ((uint32_t)p[0] << 16) |
                                     ((uint32_t)p[1] << 8) | p[2];
                    if (tempo != 0) {
                        TempoEvent event = {tick, tempo, order++};
                        g_array_append_val(midi->tempos, event);
                    }
                }
                p += length;
                if (type == 0x2f)
                    break;
            } else if (status == 0xf0 || status == 0xf7) {
                if (!varlen(&p, track_end, &length) ||
                    length > (uint64_t)(track_end - p))
                    goto out;
                p += length;
            } else if (status >= 0xf0) {
                goto out;
            } else {
                int bytes = (status & 0xf0) == 0xc0 ||
                            (status & 0xf0) == 0xd0 ? 1 : 2;
                if (track_end - p < bytes)
                    goto out;
                for (int i = 0; i < bytes; ++i)
                    if (p[i] & 0x80)
                        goto out;
                p += bytes;
            }
        }
        p = track_end;
    }
    g_array_sort(midi->tempos, compare_tempos);
    valid = 1;

out:
    if (!valid)
        set_error(error, error_size,
                  "Unsupported or invalid MIDI file (SMF type 0/1 required)");
    g_free(contents);
    return valid;
}

static double milliseconds_at_tick(const MidiAudio *midi, uint64_t target)
{
    double ms = 0;
    uint64_t previous = 0;
    uint32_t tempo = 500000;
    for (guint i = 0; i < midi->tempos->len; ++i) {
        TempoEvent event = g_array_index(midi->tempos, TempoEvent, i);
        if (event.tick > target)
            break;
        ms += (double)(event.tick - previous) * tempo /
              (midi->division * 1000.0);
        previous = event.tick;
        tempo = event.microseconds_per_quarter;
    }
    ms += (double)(target - previous) * tempo /
          (midi->division * 1000.0);
    return ms;
}

static uint64_t tick_at_milliseconds(const MidiAudio *midi, double target)
{
    double ms = 0;
    uint64_t previous = 0;
    uint32_t tempo = 500000;
    for (guint i = 0; i < midi->tempos->len; ++i) {
        TempoEvent event = g_array_index(midi->tempos, TempoEvent, i);
        double next = ms + (double)(event.tick - previous) * tempo /
                           (midi->division * 1000.0);
        if (target < next)
            break;
        ms = next;
        previous = event.tick;
        tempo = event.microseconds_per_quarter;
    }
    double ticks = previous + (target - ms) * midi->division * 1000.0 / tempo;
    if (ticks < 0)
        return 0;
    if (ticks > midi->total_ticks)
        return midi->total_ticks;
    return (uint64_t)ticks;
}

int midi_audio_valid_soundfont(const char *filename)
{
    return filename && g_file_test(filename, G_FILE_TEST_IS_REGULAR) &&
           fluid_is_soundfont(filename);
}

char *midi_audio_find_default_soundfont(void)
{
    const char *environment = g_getenv("XMP_SOUNDFONT");
    if (midi_audio_valid_soundfont(environment))
        return g_strdup(environment);

    const char *appdir = g_getenv("APPDIR");
    if (appdir && *appdir) {
        static const char *bundled_names[] = {
            "default-GM.sf3", "default-GM.sf2", "MS Basic.sf3", NULL
        };
        for (int i = 0; bundled_names[i]; ++i) {
            char *bundled = g_build_filename(appdir, "usr", "share", "sounds",
                                             "sf2", bundled_names[i], NULL);
            if (midi_audio_valid_soundfont(bundled))
                return bundled;
            g_free(bundled);
        }
    }

    static const char *candidates[] = {
        "/usr/share/sounds/sf2/MS Basic.sf3",
        "/usr/share/sounds/sf2/default-GM.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/soundfonts/default.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/TimGM6mb.sf2",
        NULL
    };
    for (int i = 0; candidates[i]; ++i)
        if (midi_audio_valid_soundfont(candidates[i]))
            return g_strdup(candidates[i]);
    return NULL;
}

const char *midi_audio_soundfont(const MidiAudio *midi)
{
    return midi ? midi->soundfont : NULL;
}

int midi_audio_set_soundfont(MidiAudio *midi, const char *filename,
                              char *error, size_t error_size)
{
    if (!midi || !midi_audio_valid_soundfont(filename)) {
        set_error(error, error_size, "Not a valid SF2/SF3 SoundFont");
        return 0;
    }

    /* Load the replacement before removing the old font. A failed load
     * therefore leaves the current MIDI instrument set intact. */
    int new_id = fluid_synth_sfload(midi->synth, filename, 1);
    if (new_id == FLUID_FAILED) {
        set_error(error, error_size, "FluidSynth could not load this SoundFont");
        return 0;
    }
    if (midi->soundfont_id >= 0)
        fluid_synth_sfunload(midi->synth, midi->soundfont_id, 1);
    midi->soundfont_id = new_id;
    g_free(midi->soundfont);
    midi->soundfont = g_strdup(filename);
    for (int ch = 0; ch < 16; ++ch)
        fluid_synth_all_sounds_off(midi->synth, ch);
    set_error(error, error_size, "");
    return 1;
}

static int start_player(MidiAudio *midi)
{
    if (midi->player) {
        delete_fluid_player(midi->player);
        midi->player = NULL;
    }
    midi->player = new_fluid_player(midi->synth);
    if (!midi->player ||
        fluid_player_add(midi->player, midi->filename) != FLUID_OK ||
        fluid_player_play(midi->player) != FLUID_OK)
        return 0;
    return 1;
}

MidiAudio *midi_audio_open(const char *filename, const char *soundfont,
                           char *error, size_t error_size)
{
    set_error(error, error_size, "");
    MidiAudio *midi = g_new0(MidiAudio, 1);
    midi->soundfont_id = -1;
    if (!parse_midi_timing(midi, filename, error, error_size))
        goto fail;
    midi->duration_ms = (int64_t)(milliseconds_at_tick(midi, midi->total_ticks) + 0.5);
    midi->filename = g_strdup(filename);
    midi->settings = new_fluid_settings();
    if (!midi->settings)
        goto fail;
    fluid_settings_setnum(midi->settings, "synth.sample-rate", MIDI_RATE);
    fluid_settings_setstr(midi->settings, "player.timing-source", "sample");
    midi->synth = new_fluid_synth(midi->settings);
    if (!midi->synth)
        goto fail;

    char *fallback = NULL;
    if (!soundfont || !*soundfont) {
        fallback = midi_audio_find_default_soundfont();
        soundfont = fallback;
    }
    if (!soundfont || !midi_audio_set_soundfont(midi, soundfont,
                                               error, error_size)) {
        if (!soundfont)
            set_error(error, error_size,
                      "Select a SoundFont (.sf2/.sf3) in the SoundFonts tab");
        g_free(fallback);
        goto fail;
    }
    g_free(fallback);
    if (!start_player(midi))
        goto fail;
    set_error(error, error_size, "");
    return midi;

fail:
    if (error && error_size && !error[0])
        set_error(error, error_size, "Could not initialize FluidSynth MIDI player");
    midi_audio_close(midi);
    return NULL;
}

void midi_audio_close(MidiAudio *midi)
{
    if (!midi)
        return;
    if (midi->player)
        delete_fluid_player(midi->player);
    if (midi->synth)
        delete_fluid_synth(midi->synth);
    if (midi->settings)
        delete_fluid_settings(midi->settings);
    if (midi->tempos)
        g_array_free(midi->tempos, TRUE);
    g_free(midi->filename);
    g_free(midi->soundfont);
    g_free(midi);
}

int midi_audio_restart(MidiAudio *midi)
{
    if (!midi)
        return 0;
    for (int ch = 0; ch < 16; ++ch)
        fluid_synth_all_sounds_off(midi->synth, ch);
    midi->position_frames = 0;
    return start_player(midi);
}

int midi_audio_seek(MidiAudio *midi, int64_t milliseconds)
{
    if (!midi || !midi->player)
        return 0;
    if (milliseconds < 0)
        milliseconds = 0;
    if (milliseconds > midi->duration_ms)
        milliseconds = midi->duration_ms;
    /* MIDI ticks, not milliseconds, are the FluidSynth seek unit. */
    uint64_t tick = tick_at_milliseconds(midi, (double)milliseconds);
    if (tick > INT_MAX || fluid_player_seek(midi->player, (int)tick) != FLUID_OK)
        return 0;
    for (int ch = 0; ch < 16; ++ch)
        fluid_synth_all_sounds_off(midi->synth, ch);
    midi->position_frames = milliseconds * MIDI_RATE / 1000;
    return 1;
}

int midi_audio_read(MidiAudio *midi, int16_t *output, int frames)
{
    if (!midi || !output || frames <= 0)
        return 0;
    int64_t limit = (midi->duration_ms + MIDI_TAIL_MS) * MIDI_RATE / 1000;
    if (midi->position_frames >= limit)
        return 0;
    if (frames > limit - midi->position_frames)
        frames = (int)(limit - midi->position_frames);
    if (fluid_synth_write_s16(midi->synth, frames, output, 0, 2,
                              output, 1, 2) != FLUID_OK)
        return -1;
    midi->position_frames += frames;
    return frames;
}

int64_t midi_audio_position_ms(const MidiAudio *midi)
{
    return midi ? midi->position_frames * 1000 / MIDI_RATE : 0;
}

int64_t midi_audio_duration_ms(const MidiAudio *midi)
{
    return midi ? midi->duration_ms : 0;
}
