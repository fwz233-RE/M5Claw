#pragma once
#include <Arduino.h>

namespace DashScopeSTT {
    void init(const char* apiKey);
    // Recognize audio from a PCM buffer (16kHz, 16-bit, mono)
    // Blocks until recognition is complete
    String recognize(const int16_t* samples, size_t sampleCount);
}
