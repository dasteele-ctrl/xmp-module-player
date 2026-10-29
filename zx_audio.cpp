#include "zx_audio.h"

#include "core/service.h"
#include "io/api.h"
#include "module/players/pipeline.h"
#include "parameters/container.h"
#include "sound/sample.h"
#include "tools/progress_callback.h"

#include "error.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>

struct ZxAudio {
    Parameters::Container::Ptr parameters;
    ZXTune::Service::Ptr service;
    Module::Holder::Ptr module;
    Module::Renderer::Ptr renderer;
    Sound::Chunk block;
    std::size_t offset = 0;
    int64_t duration_ms = 0;
};

static_assert(Sound::Sample::CHANNELS == 2 && Sound::Sample::BITS == 16,
              "XMP expects stereo 16-bit samples");
static_assert(sizeof(Sound::Sample) == 2 * sizeof(int16_t),
              "ZXTune and SDL sample layouts must match");

static void copy_error(char *destination, unsigned long size, const char *text)
{
    if (destination && size)
        std::snprintf(destination, size, "%s", text ? text : "Unknown ZXTune error");
}

extern "C" ZxAudio *zx_audio_open(const char *filename, char *error,
                                   unsigned long error_size)
{
    if (!filename || !*filename) {
        copy_error(error, error_size, "No ZX tune filename provided");
        return nullptr;
    }

    try {
        auto audio = std::make_unique<ZxAudio>();
        audio->parameters = Parameters::Container::Create();
        audio->service = ZXTune::Service::Create(audio->parameters);
        const auto id = IO::ResolveUri(filename);
        auto data = IO::OpenData(id->Path(), *audio->parameters,
                                 Log::ProgressCallback::Stub());
        audio->module = audio->service->OpenModule(
            std::move(data), id->Subpath(), Parameters::Container::Create());
        if (!audio->module) {
            copy_error(error, error_size, "No supported ZX tune found");
            return nullptr;
        }

        audio->duration_ms = audio->module->GetModuleInformation().Duration.Get();
        audio->renderer = Module::CreatePipelinedRenderer(
            *audio->module, 44100, audio->parameters);
        if (!audio->renderer) {
            copy_error(error, error_size, "ZXTune could not create an audio renderer");
            return nullptr;
        }
        copy_error(error, error_size, "");
        return audio.release();
    } catch (const Error& failure) {
        copy_error(error, error_size, failure.ToString().c_str());
    } catch (const std::exception& failure) {
        copy_error(error, error_size, failure.what());
    } catch (...) {
        copy_error(error, error_size, "Unknown error opening ZX tune");
    }
    return nullptr;
}

extern "C" int zx_audio_read(ZxAudio *audio, int16_t *output, int max_frames)
{
    if (!audio || !output || max_frames <= 0)
        return -1;

    try {
        int count = 0;
        while (count < max_frames) {
            if (audio->offset >= audio->block.size()) {
                audio->block = audio->renderer->Render();
                audio->offset = 0;
                if (audio->block.empty())
                    break;
            }
            const auto available = audio->block.size() - audio->offset;
            const auto frames = std::min<std::size_t>(
                available, static_cast<std::size_t>(max_frames - count));
            std::memcpy(output + count * 2,
                        audio->block.data() + audio->offset,
                        frames * sizeof(Sound::Sample));
            audio->offset += frames;
            count += static_cast<int>(frames);
        }
        return count;
    } catch (...) {
        return -1;
    }
}

extern "C" int zx_audio_seek(ZxAudio *audio, int64_t milliseconds)
{
    if (!audio || milliseconds < 0)
        return -1;

    try {
        if (milliseconds > audio->duration_ms && audio->duration_ms > 0)
            milliseconds = audio->duration_ms;
        audio->renderer->SetPosition(Time::AtMillisecond(milliseconds));
        audio->block.clear();
        audio->offset = 0;
        return 0;
    } catch (...) {
        return -1;
    }
}

extern "C" int64_t zx_audio_duration_ms(const ZxAudio *audio)
{
    return audio ? audio->duration_ms : 0;
}

extern "C" void zx_audio_close(ZxAudio *audio)
{
    delete audio;
}
