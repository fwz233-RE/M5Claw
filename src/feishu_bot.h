#pragma once
#include <Arduino.h>

namespace FeishuBot {
    void init();
    void start();
    void stop();
    void resume();
    bool sendMessage(const char* chatId, const char* text);
    bool replyMessage(const char* messageId, const char* text);
    bool isRunning();
    bool isStopped();

    bool hasIncomingForDisplay();
    char* takeIncomingForDisplay();
}
