#pragma once
#include <Arduino.h>

namespace DashScopeTTS {
    void init(const char* apiKey);
    // Synthesize text to PCM audio (24kHz, 16-bit, mono)
    // Returns number of samples written to buffer
    // Buffer must be pre-allocated
    size_t synthesize(const char* text, int16_t* buffer, size_t maxSamples);
}
