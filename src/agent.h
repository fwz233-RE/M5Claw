#pragma once
#include <Arduino.h>

typedef void (*AgentResponseCallback)(const char* text);

struct AgentResponseInfo {
    const char* text;
    const char* channel;
    const char* chatId;
};

typedef void (*AgentResponseExCallback)(const AgentResponseInfo* info);

struct ExternalConv {
    char* userText;
    char* aiText;
    char channel[16];
};

namespace Agent {
    void init();
    void start();

    void sendMessage(const char* text, AgentResponseCallback onResponse);
    void sendMessageEx(const char* text, const char* channel, const char* chatId,
                       AgentResponseExCallback onResponse);

    bool isBusy();
    bool isReady();

    bool hasExternalConv();
    ExternalConv takeExternalConv();
}
