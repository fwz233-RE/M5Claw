#pragma once
#include <Arduino.h>

namespace MediaUtils {
    bool downloadHttpsUrlToFile(const char* url, const char* destPath,
                                char* mimeTypeOut, size_t mimeTypeOutSize,
                                size_t maxBytes);
    const char* guessMimeType(const char* path, const char* fallbackMime = "application/octet-stream");
}
