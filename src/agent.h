#pragma once
#include <Arduino.h>

typedef void (*AgentResponseCallback)(const char* text);

namespace Agent {
    void init();
    void start();
    void sendMessage(const char* text, AgentResponseCallback onResponse);
    bool isBusy();
}
