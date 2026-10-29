#define _POSIX_C_SOURCE 200809L
#include "zx_audio.h"
#include "zx_helper_protocol.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* The native ZXTune library lives in a separate process, so it cannot
   interpose the application's libsidplayfp C++ symbols. The PCM stream is
   sent directly over a local socket, without creating or decoding WAV files. */
struct ZxAudio {
    int fd;
    pid_t child;
    int64_t duration_ms;
};

static int ready_to_read(int fd)
{
    struct pollfd fds = { .fd = fd, .events = POLLIN };
    int result;
    do {
        result = poll(&fds, 1, 15000);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (fds.revents & POLLIN);
}

static int read_exact(int fd, void *data, size_t bytes)
{
    unsigned char *ptr = data;
    while (bytes) {
        if (!ready_to_read(fd)) return 0;
        ssize_t count = read(fd, ptr, bytes);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        ptr += count;
        bytes -= (size_t)count;
    }
    return 1;
}

static int write_exact(int fd, const void *data, size_t bytes)
{
    const unsigned char *ptr = data;
    while (bytes) {
        ssize_t count = send(fd, ptr, bytes, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        ptr += count;
        bytes -= (size_t)count;
    }
    return 1;
}

static int helper_path(char *output, size_t output_size)
{
    char executable[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof(executable) - 1);
    if (length <= 0 || (size_t)length >= sizeof(executable) - 1) return 0;
    executable[length] = '\0';
    char *slash = strrchr(executable, '/');
    if (!slash) return 0;
    slash[1] = '\0';
    int n = snprintf(output, output_size, "%sxmp-zx-helper", executable);
    return n > 0 && (size_t)n < output_size;
}

static void copy_error(char *destination, unsigned long size, const char *error)
{
    if (destination && size)
        snprintf(destination, size, "%s", error);
}

void zx_audio_close(ZxAudio *audio)
{
    if (!audio) return;
    if (audio->fd >= 0) {
        ZxHelperRequest quit = { .command = ZX_CMD_CLOSE };
        (void)write_exact(audio->fd, &quit, sizeof(quit));
        shutdown(audio->fd, SHUT_RDWR);
        close(audio->fd);
    }
    if (audio->child > 0) {
        pid_t result;
        do {
            result = waitpid(audio->child, NULL, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == 0) {
            kill(audio->child, SIGTERM);
            do {
                result = waitpid(audio->child, NULL, 0);
            } while (result < 0 && errno == EINTR);
        }
    }
    free(audio);
}

ZxAudio *zx_audio_open(const char *filename, char *error,
                       unsigned long error_size)
{
    copy_error(error, error_size, "Could not start the ZXTune helper");
    if (!filename || !*filename || strlen(filename) >= ZX_MAX_FILENAME_BYTES)
        return NULL;

    char executable[PATH_MAX];
    if (!helper_path(executable, sizeof(executable))) return NULL;

    int channel[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, channel) < 0) return NULL;

    pid_t child = fork();
    if (child == 0) {
        close(channel[0]);
        if (dup2(channel[1], STDIN_FILENO) < 0 ||
            dup2(channel[1], STDOUT_FILENO) < 0) _exit(127);
        if (channel[1] > STDOUT_FILENO) close(channel[1]);
        execl(executable, executable, (char *)NULL);
        _exit(127);
    }
    close(channel[1]);
    if (child < 0) {
        close(channel[0]);
        return NULL;
    }

    ZxAudio *audio = calloc(1, sizeof(*audio));
    if (!audio) {
        close(channel[0]);
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
        return NULL;
    }
    audio->fd = channel[0];
    audio->child = child;

    ZxHelperRequest open = {
        .command = ZX_CMD_OPEN,
        .number = (int32_t)strlen(filename) + 1
    };
    ZxHelperResponse response;
    char helper_error[ZX_ERROR_BYTES] = "";

    if (!write_exact(audio->fd, &open, sizeof(open)) ||
        !write_exact(audio->fd, filename, (size_t)open.number) ||
        !read_exact(audio->fd, &response, sizeof(response)) ||
        !read_exact(audio->fd, helper_error, sizeof(helper_error))) {
        copy_error(error, error_size,
                   "ZXTune helper did not respond; check that xmp-zx-helper is beside xmp-linux");
        zx_audio_close(audio);
        return NULL;
    }
    helper_error[sizeof(helper_error) - 1] = '\0';
    if (response.result != 1) {
        copy_error(error, error_size,
                   helper_error[0] ? helper_error : "ZXTune rejected this module");
        zx_audio_close(audio);
        return NULL;
    }
    audio->duration_ms = response.duration_ms;
    copy_error(error, error_size, "");
    return audio;
}

int zx_audio_read(ZxAudio *audio, int16_t *output, int max_frames)
{
    if (!audio || !output || max_frames <= 0 || max_frames > ZX_MAX_FRAMES)
        return -1;
    ZxHelperRequest request = { .command = ZX_CMD_READ, .number = max_frames };
    ZxHelperResponse response;
    if (!write_exact(audio->fd, &request, sizeof(request)) ||
        !read_exact(audio->fd, &response, sizeof(response))) return -1;
    if (response.result < 0 || response.result > max_frames) return -1;
    if (response.result &&
        !read_exact(audio->fd, output, (size_t)response.result * 4)) return -1;
    return response.result;
}

int zx_audio_seek(ZxAudio *audio, int64_t milliseconds)
{
    if (!audio || milliseconds < 0) return -1;
    ZxHelperRequest request = { .command = ZX_CMD_SEEK, .position = milliseconds };
    ZxHelperResponse response;
    if (!write_exact(audio->fd, &request, sizeof(request)) ||
        !read_exact(audio->fd, &response, sizeof(response))) return -1;
    return response.result;
}

int64_t zx_audio_duration_ms(const ZxAudio *audio)
{
    return audio ? audio->duration_ms : 0;
}
