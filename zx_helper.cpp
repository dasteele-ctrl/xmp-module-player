#include "zx_audio.h"
#include "zx_helper_protocol.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <unistd.h>

static bool read_exact(int fd, void *buffer, size_t bytes)
{
    auto *p = static_cast<unsigned char *>(buffer);
    while (bytes) {
        ssize_t n = read(fd, p, bytes);
        if (n == 0) return false;
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        bytes -= static_cast<size_t>(n);
    }
    return true;
}

static bool write_exact(int fd, const void *buffer, size_t bytes)
{
    auto *p = static_cast<const unsigned char *>(buffer);
    while (bytes) {
        ssize_t n = write(fd, p, bytes);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        p += n;
        bytes -= static_cast<size_t>(n);
    }
    return true;
}

int main()
{
    ZxAudio *renderer = nullptr;
    int16_t samples[ZX_MAX_FRAMES * 2];

    while (true) {
        ZxHelperRequest req{};
        if (!read_exact(STDIN_FILENO, &req, sizeof(req))) break;

        ZxHelperResponse reply{};
        if (req.command == ZX_CMD_OPEN) {
            char error[ZX_ERROR_BYTES]{};
            if (renderer || req.number <= 1 ||
                req.number > ZX_MAX_FILENAME_BYTES) {
                std::strncpy(error, "Invalid ZXTune open request", sizeof(error) - 1);
            } else {
                std::vector<char> filename(static_cast<size_t>(req.number));
                if (!read_exact(STDIN_FILENO, filename.data(), filename.size()))
                    break;
                if (filename.back() != '\0' ||
                    std::strlen(filename.data()) != filename.size() - 1) {
                    std::strncpy(error, "Invalid ZXTune filename", sizeof(error) - 1);
                } else {
                    renderer = zx_audio_open(filename.data(), error, sizeof(error));
                }
            }
            reply.result = renderer ? 1 : -1;
            reply.duration_ms = renderer ? zx_audio_duration_ms(renderer) : 0;
            if (!write_exact(STDOUT_FILENO, &reply, sizeof(reply)) ||
                !write_exact(STDOUT_FILENO, error, sizeof(error))) break;
        } else if (req.command == ZX_CMD_READ) {
            reply.result = renderer && req.number > 0 &&
                           req.number <= ZX_MAX_FRAMES
                               ? zx_audio_read(renderer, samples, req.number)
                               : -1;
            if (reply.result > ZX_MAX_FRAMES) reply.result = -1;
            if (!write_exact(STDOUT_FILENO, &reply, sizeof(reply))) break;
            if (reply.result > 0 &&
                !write_exact(STDOUT_FILENO, samples,
                             static_cast<size_t>(reply.result) * 4)) break;
        } else if (req.command == ZX_CMD_SEEK) {
            reply.result = renderer ? zx_audio_seek(renderer, req.position) : -1;
            if (!write_exact(STDOUT_FILENO, &reply, sizeof(reply))) break;
        } else if (req.command == ZX_CMD_CLOSE) {
            break;
        } else {
            break;
        }
    }

    zx_audio_close(renderer);
    return 0;
}
