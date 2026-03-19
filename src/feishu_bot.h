#pragma once
#include <Arduino.h>

namespace FeishuBot {
    void init();
    void start();
    void stop();
    void resume();
    bool sendMessage(const char* chatId, const char* text);
    bool isRunning();

    bool hasIncomingForDisplay();
    char* takeIncomingForDisplay();

    bool addReaction(const char* messageId, const char* emojiType,
                     char* reactionIdOut, size_t reactionIdSize);
    bool removeReaction(const char* messageId, const char* reactionId);
}
