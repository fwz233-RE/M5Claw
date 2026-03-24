#pragma once
#include <Arduino.h>

typedef void (*AgentResponseCallback)(const char* text);
typedef void (*AgentTokenCallback)(const char* token);

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

    void sendMessage(const char* text, AgentResponseCallback onResponse,
                     AgentTokenCallback onToken = nullptr);

    bool isBusy();
    void requestAbort();

    bool hasExternalConv();
    ExternalConv takeExternalConv();
}
