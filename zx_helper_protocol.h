#ifndef ZX_HELPER_PROTOCOL_H
#define ZX_HELPER_PROTOCOL_H

#include <stdint.h>

/* Fixed-size, host-local protocol between xmp-linux and xmp-zx-helper. */
enum {
    ZX_CMD_OPEN = 1,
    ZX_CMD_READ = 2,
    ZX_CMD_SEEK = 3,
    ZX_CMD_CLOSE = 4,
    ZX_MAX_FRAMES = 4096,
    ZX_ERROR_BYTES = 512,
    ZX_MAX_FILENAME_BYTES = 65536
};

typedef struct {
    int32_t command;
    int32_t number;
    int64_t position;
} ZxHelperRequest;

typedef struct {
    int32_t result;
    int32_t reserved;
    int64_t duration_ms;
} ZxHelperResponse;

#endif
