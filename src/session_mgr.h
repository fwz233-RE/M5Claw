#pragma once
#include <Arduino.h>

namespace SessionMgr {
    void init();
    bool appendMessage(const char* chat_id, const char* role, const char* content);
    String getHistoryJson(const char* chat_id, int maxMessages);
    void clearSession(const char* chat_id);
}
