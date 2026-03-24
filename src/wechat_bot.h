#pragma once
#include <Arduino.h>

namespace WechatBot {
    void init();
    void start();
    void stop();
    void resume();
    bool sendMessage(const char* userId, const char* text);
    bool sendTyping(const char* userId, int status);
    bool isRunning();
    bool isPaired();

    bool hasIncomingForDisplay();
    char* takeIncomingForDisplay();

    const char* getApiHost();
    const char* getLastUserId();

    // Pairing flow
    enum PairState { PAIR_IDLE, PAIR_WAITING, PAIR_OK, PAIR_FAIL };
    PairState getPairState();
    const char* startPairing();   // returns QR content, or nullptr on error
    bool pollPairing();           // true = pairing complete
    void cancelPairing();
}
