#pragma once
#include <cstdint>
#include <Arduino.h>

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

namespace Color {
    constexpr uint16_t BLACK      = 0x0000;
    constexpr uint16_t WHITE      = 0xFFFF;
    constexpr uint16_t BG_DAY     = rgb565(24, 20, 37);
    constexpr uint16_t BG_NIGHT   = rgb565(10, 10, 20);
    constexpr uint16_t GROUND     = rgb565(46, 34, 47);
    constexpr uint16_t GROUND_TOP = rgb565(62, 53, 70);
    constexpr uint16_t STAR       = rgb565(200, 200, 140);
    constexpr uint16_t CLOCK_TEXT = rgb565(180, 180, 200);
    constexpr uint16_t STATUS_DIM = rgb565(100, 100, 120);
    constexpr uint16_t CHAT_USER  = rgb565(80, 120, 200);
    constexpr uint16_t CHAT_AI    = rgb565(60, 160, 80);
    constexpr uint16_t INPUT_BG   = rgb565(40, 36, 50);
    constexpr uint16_t TRANSPARENT = rgb565(255, 0, 255);
}

struct Timer {
    unsigned long interval;
    unsigned long lastTick;

    Timer(unsigned long ms = 1000) : interval(ms), lastTick(0) {}

    bool tick() {
        unsigned long now = millis();
        if (now - lastTick >= interval) {
            lastTick = now;
            return true;
        }
        return false;
    }

    void reset() { lastTick = millis(); }
    void setInterval(unsigned long ms) { interval = ms; }
};
