#pragma once
#include <Arduino.h>

typedef bool (*SttAbortCheckFn)();

namespace DashScopeSTT {
    void init(const char* apiKey);

    bool beginStream(SttAbortCheckFn shouldAbort = nullptr);
    void feedAudio(const int16_t* samples, size_t count);
    void poll();
    String endStream();
    bool isStreaming();
    String getPartialText();

    String recognize(const int16_t* samples, size_t sampleCount);
}
