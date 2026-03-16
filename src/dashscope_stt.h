#pragma once
#include <Arduino.h>

namespace DashScopeSTT {
    void init(const char* apiKey);

    // Streaming API — record and send audio in real-time
    bool beginStream();
    void feedAudio(const int16_t* samples, size_t count);
    void poll();
    String endStream();
    bool isStreaming();
    String getPartialText();

    // Legacy batch API (uses streaming internally)
    String recognize(const int16_t* samples, size_t sampleCount);
}
