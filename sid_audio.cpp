#include "sid_audio.h"

#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/builders/residfp.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

struct SidAudio {
    ReSIDfpBuilder builder;
    sidplayfp player;
    std::unique_ptr<SidTune> tune;

    unsigned int songs = 0;
    unsigned int current_song = 0;

    std::string title;
    std::string author;
    std::string release;
    std::string error;

    SidAudio() : builder("ReSIDfp") {}
};

static void copy_error(char *destination,
                       unsigned long size,
                       const char *message)
{
    if (!destination || size == 0)
        return;

    std::snprintf(destination, size, "%s",
                  message ? message : "Unknown SID playback error");
}

static std::string tune_text(const SidTuneInfo *info, unsigned int index)
{
    if (!info)
        return {};

    const char *value = info->infoString(index);
    return value ? value : "";
}

extern "C" SidAudio *sid_audio_open(const char *filename,
                                    char *error_text,
                                    unsigned long error_text_size)
{
    if (!filename || !*filename) {
        copy_error(error_text, error_text_size, "No SID filename supplied");
        return nullptr;
    }

    std::unique_ptr<SidAudio> audio(new SidAudio);

    audio->tune.reset(new SidTune(filename));

    if (!audio->tune->getInfo()) {
        copy_error(error_text, error_text_size,
                   audio->tune->statusString());
        return nullptr;
    }

    const SidTuneInfo *info = audio->tune->getInfo();

    audio->songs = info->songs();
    audio->current_song = audio->tune->selectSong(info->startSong());
    audio->title = tune_text(info, 0);
    audio->author = tune_text(info, 1);
    audio->release = tune_text(info, 2);

    if (!audio->player.load(audio->tune.get())) {
        copy_error(error_text, error_text_size,
                   audio->player.error());
        return nullptr;
    }

    if (audio->builder.create(1) == 0) {
        copy_error(error_text, error_text_size,
                   audio->builder.error());
        return nullptr;
    }

    SidConfig config = audio->player.config();
    config.frequency = 44100;
    config.playback = SidConfig::MONO;
    config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
    config.sidEmulation = &audio->builder;

    if (!audio->player.config(config)) {
        copy_error(error_text, error_text_size,
                   audio->player.error());
        return nullptr;
    }

    copy_error(error_text, error_text_size, "");
    return audio.release();
}

extern "C" int sid_audio_read(SidAudio *audio,
                              int16_t *output,
                              int max_frames)
{
    if (!audio || !output || max_frames <= 0)
        return -1;

    // Generate mono samples into the first half of SDL's stereo buffer.
    const uint_least32_t received =
        audio->player.play(reinterpret_cast<short *>(output),
                           static_cast<uint_least32_t>(max_frames));

    if (received == 0) {
        const char *message = audio->player.error();
        if (message && *message) {
            audio->error = message;
            return -1;
        }
        return 0;
    }

    // Expand backwards to avoid overwriting unread mono samples.
    for (uint_least32_t i = received; i > 0; --i) {
        const int16_t sample = output[i - 1];
        output[(i - 1) * 2] = sample;
        output[(i - 1) * 2 + 1] = sample;
    }

    return static_cast<int>(received);
}

extern "C" int sid_audio_select_song(SidAudio *audio,
                                     unsigned int song_number)
{
    if (!audio || !audio->tune ||
        song_number < 1 || song_number > audio->songs)
        return 0;

    const unsigned int selected = audio->tune->selectSong(song_number);
    if (selected == 0) {
        audio->error = audio->tune->statusString();
        return 0;
    }

    if (!audio->player.load(audio->tune.get())) {
        audio->error = audio->player.error();
        return 0;
    }

    audio->current_song = song_number;
    audio->error.clear();
    return 1;
}

extern "C" unsigned int sid_audio_song_count(const SidAudio *audio)
{
    return audio ? audio->songs : 0;
}

extern "C" unsigned int sid_audio_current_song(const SidAudio *audio)
{
    return audio ? audio->current_song : 0;
}

extern "C" const char *sid_audio_title(const SidAudio *audio)
{
    return audio ? audio->title.c_str() : "";
}

extern "C" const char *sid_audio_author(const SidAudio *audio)
{
    return audio ? audio->author.c_str() : "";
}

extern "C" const char *sid_audio_release_info(const SidAudio *audio)
{
    return audio ? audio->release.c_str() : "";
}

extern "C" const char *sid_audio_error(const SidAudio *audio)
{
    return audio ? audio->error.c_str() : "No SID player";
}

extern "C" void sid_audio_close(SidAudio *audio)
{
    delete audio;
}
