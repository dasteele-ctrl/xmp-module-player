#include "adlib_audio.h"
#include <adplug/adplug.h>
#include <adplug/nemuopl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <strings.h>

static constexpr int rate = 44100;

const char *const adlib_extensions[] = {
    ".rad", ".hsc", ".hsp", ".cmf", ".d00", ".a2m", ".a2t", ".dro",
    ".amd", ".sa2", ".bam", ".cff", ".dfm", ".sop", ".imf",
    ".xms", ".pis", ".xad", ".jbm", ".lds", ".mkj", ".mtk", ".mtr",
    ".rix", ".sat", ".dmo", nullptr
};

int adlib_is_path(const char *path)
{
    const char *ext = std::strrchr(path, '.');
    if (ext)
        for (const char *const *p = adlib_extensions; *p; ++p)
            if (!strcasecmp(ext, *p)) return 1;
    return 0;
}

int adlib_is_fm_s3m(const char *path)
{
    const char *ext = std::strrchr(path, '.');
    if (!ext || strcasecmp(ext, ".s3m")) return 0;
    FILE *file = std::fopen(path, "rb");
    if (!file) return 0;
    unsigned char header[96];
    bool valid = std::fread(header, 1, sizeof(header), file) == sizeof(header)
        && !std::memcmp(header + 44, "SCRM", 4);
    std::fclose(file);
    if (!valid) return 0;
    bool fm = false;
    for (int i = 64; i < 96; ++i) {
        if (header[i] < 16) return 0; // Keep PCM and mixed songs on libxmp.
        if (header[i] <= 29) fm = true;
    }
    return fm;
}

struct AdlibAudio {
    CNemuopl opl{rate};
    std::unique_ptr<CPlayer> player;
    int64_t duration = 0;
    double pending = 0;
    bool ended = false;
};

AdlibAudio *adlib_audio_open(const char *path)
{
    try {
        std::unique_ptr<AdlibAudio> audio(new AdlibAudio);
        const char *ext = std::strrchr(path, '.');
        for (const CPlayerDesc *desc : CAdPlug::players) {
            for (unsigned i = 0; desc->get_extension(i); ++i) {
                if (!ext || strcasecmp(ext, desc->get_extension(i))) continue;
                audio->player.reset(desc->factory(&audio->opl));
                if (audio->player && audio->player->load(path)) {
                    audio->duration = audio->player->songlength();
                    audio->player->rewind();
                    return audio.release();
                }
                break;
            }
        }
    } catch (...) {
        // A failed loader must not unwind through the C audio worker.
    }
    return nullptr;
}

void adlib_audio_close(AdlibAudio *audio) { delete audio; }

int adlib_audio_read(AdlibAudio *audio, int16_t *pcm, int frames)
{
    int written = 0;
    while (!audio->ended && written < frames) {
        if (audio->pending < 1) {
            if (!audio->player->update()) {
                audio->ended = true;
                break;
            }
            double hz = audio->player->getrefresh();
            if (!std::isfinite(hz) || hz <= 0 || hz > rate) return -1;
            audio->pending += rate / hz;
        }
        int chunk = static_cast<int>(std::min<double>(audio->pending, frames - written));
        audio->opl.update(pcm + written * 2, chunk);
        written += chunk;
        audio->pending -= chunk;
    }
    return written;
}

int adlib_audio_seek(AdlibAudio *audio, int64_t ms)
{
    audio->player->seek(std::max<int64_t>(0, std::min(ms, audio->duration)));
    audio->pending = 0;
    audio->ended = false;
    return 0;
}

int64_t adlib_audio_duration_ms(const AdlibAudio *audio) { return audio->duration; }
