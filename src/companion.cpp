#include "companion.h"
#include "sunset_data.h"
#include <time.h>
#include <math.h>

static const uint8_t* const sunset_scenes[] = {
    scene_pixels_1, scene_pixels_2, scene_pixels_3
};

// ── RGB565 per-channel blend (t = 0..255) ──

static uint16_t blendRGB565(uint16_t a, uint16_t b, uint8_t t) {
    uint8_t r1 = (a >> 11) & 0x1F, g1 = (a >> 5) & 0x3F, b1 = a & 0x1F;
    uint8_t r2 = (b >> 11) & 0x1F, g2 = (b >> 5) & 0x3F, b2 = b & 0x1F;
    uint8_t r = r1 + ((int)(r2 - r1) * t / 255);
    uint8_t g = g1 + ((int)(g2 - g1) * t / 255);
    uint8_t bl = b1 + ((int)(b2 - b1) * t / 255);
    return (r << 11) | (g << 5) | bl;
}

// ══════════════════════════════════════════════════════════════
//  Lifecycle
// ══════════════════════════════════════════════════════════════

void Companion::begin(M5Canvas&) {
    currentScene = 1;
    targetScene  = 1;
    transitionActive = false;
    transitionAlpha  = 0;
}

void Companion::update(M5Canvas& canvas) {
    if (transitionActive) {
        transitionAlpha += TRANSITION_STEP;
        if (transitionAlpha >= TRANSITION_END) {
            transitionAlpha = TRANSITION_END;
            currentScene    = targetScene;
            transitionActive = false;
        }
    }
    drawScene(canvas);
    drawTopBar(canvas);
}

// ══════════════════════════════════════════════════════════════
//  Sunset scene cycle  (SunSet1 → 2 → 3 → 1)
// ══════════════════════════════════════════════════════════════

void Companion::cycleSunset() {
    if (transitionActive) currentScene = targetScene;
    targetScene     = (currentScene + 1) % 3;
    transitionAlpha = 0;
    transitionActive = true;
}

// ══════════════════════════════════════════════════════════════
//  Scene renderer — staggered top-to-bottom crossfade
//  Sky (top) transitions first, ground follows, creating a
//  natural "sunset depth change" visual.
// ══════════════════════════════════════════════════════════════

void Companion::drawScene(M5Canvas& canvas) {
    const uint8_t* srcA = sunset_scenes[currentScene];

    if (!transitionActive) {
        for (int y = 0; y < SCREEN_H; y++) {
            const int off = y * SCREEN_W;
            for (int x = 0; x < SCREEN_W; x++)
                canvas.drawPixel(x, y,
                    pgm_read_word(&scene_palette[pgm_read_byte(&srcA[off + x])]));
        }
        return;
    }

    const uint8_t* srcB = sunset_scenes[targetScene];

    for (int y = 0; y < SCREEN_H; y++) {
        int offset    = y * STAGGER_RANGE / (SCREEN_H - 1);
        int lineAlpha = transitionAlpha - offset;
        if (lineAlpha < 0)   lineAlpha = 0;
        if (lineAlpha > 255) lineAlpha = 255;
        uint8_t alpha = (uint8_t)lineAlpha;

        const int off = y * SCREEN_W;

        if (alpha == 0) {
            for (int x = 0; x < SCREEN_W; x++)
                canvas.drawPixel(x, y,
                    pgm_read_word(&scene_palette[pgm_read_byte(&srcA[off + x])]));
        } else if (alpha >= 255) {
            for (int x = 0; x < SCREEN_W; x++)
                canvas.drawPixel(x, y,
                    pgm_read_word(&scene_palette[pgm_read_byte(&srcB[off + x])]));
        } else {
            for (int x = 0; x < SCREEN_W; x++) {
                uint16_t cA = pgm_read_word(&scene_palette[pgm_read_byte(&srcA[off + x])]);
                uint16_t cB = pgm_read_word(&scene_palette[pgm_read_byte(&srcB[off + x])]);
                canvas.drawPixel(x, y, blendRGB565(cA, cB, alpha));
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════
//  Top bar — outlined text over the scene
// ══════════════════════════════════════════════════════════════

static void drawOText(M5Canvas& c, const char* t, int x, int y,
                      uint16_t fg, uint16_t bg) {
    c.setTextColor(bg);
    c.drawString(t, x-1, y); c.drawString(t, x+1, y);
    c.drawString(t, x, y-1); c.drawString(t, x, y+1);
    c.setTextColor(fg);
    c.drawString(t, x, y);
}

static const char* weatherTypeName(WeatherType type) {
    switch (type) {
        case WeatherType::CLEAR:         return "Clear";
        case WeatherType::PARTLY_CLOUDY: return "Cloudy";
        case WeatherType::OVERCAST:      return "Overcast";
        case WeatherType::FOG:           return "Fog";
        case WeatherType::DRIZZLE:       return "Drizzle";
        case WeatherType::RAIN:          return "Rain";
        case WeatherType::SNOW:          return "Snow";
        case WeatherType::THUNDER:       return "Thunder";
        default:                         return "";
    }
}

void Companion::drawTopBar(M5Canvas& canvas) {
    uint16_t fg = rgb565(248, 248, 255);
    uint16_t bg = rgb565(12, 12, 20);
    canvas.setTextSize(1);

    drawOText(canvas, "[Tab]chat", 3, 3, fg, bg);

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        int tw = canvas.textWidth(buf);
        drawOText(canvas, buf, (SCREEN_W - tw) / 2, 3, fg, bg);
    }

    canvas.setTextDatum(TR_DATUM);
    int rx = SCREEN_W - 3;

    static int32_t cachedBatt = -1;
    static unsigned long lastBattRead = 0;
    unsigned long now = millis();
    if (cachedBatt < 0 || now - lastBattRead > 10000) {
        int32_t raw = M5Cardputer.Power.getBatteryLevel();
        if (raw >= 0 && raw <= 100)
            cachedBatt = (cachedBatt < 0) ? raw : (cachedBatt * 3 + raw) / 4;
        lastBattRead = now;
    }
    if (cachedBatt >= 0 && cachedBatt <= 100) {
        char bs[8]; snprintf(bs, sizeof(bs), "%d%%", (int)cachedBatt);
        drawOText(canvas, bs, rx, 3, fg, bg);
        rx -= canvas.textWidth(bs) + 6;
    }

    if (weather.valid) {
        char ws[24];
        int t = (int)roundf(weather.temperature);
        const char* wn = weatherTypeName(weather.type);
        if (wn[0] && strlen(wn) > 3) {
            char abbr[8]; memcpy(abbr, wn, 3); abbr[3] = '\0';
            snprintf(ws, sizeof(ws), "%dC %s..", t, abbr);
        } else if (wn[0]) {
            snprintf(ws, sizeof(ws), "%dC %s", t, wn);
        } else {
            snprintf(ws, sizeof(ws), "%dC", t);
        }
        int ww = canvas.textWidth(ws);
        int minX = SCREEN_W / 2 + 24;
        if (rx - ww < minX) rx = minX + ww;
        drawOText(canvas, ws, rx, 3, fg, bg);
    }
    canvas.setTextDatum(TL_DATUM);
}

// ── Sound effects ──

void Companion::playKeyClick() { M5Cardputer.Speaker.tone(800, 30); }
void Companion::playNotification() {
    M5Cardputer.Speaker.tone(1200, 80); delay(100);
    M5Cardputer.Speaker.tone(1600, 80);
}
void Companion::playHappy() {
    M5Cardputer.Speaker.tone(1000, 50); delay(60);
    M5Cardputer.Speaker.tone(1400, 50); delay(60);
    M5Cardputer.Speaker.tone(1800, 80);
}

void Companion::triggerHappy() { playHappy(); }
void Companion::triggerTalk()  {}
void Companion::triggerIdle()  { playNotification(); }
void Companion::triggerSleep() {}

// ── Notification toast ──

void Companion::showNotification(const char* app, const char* title,
                                  const char* body) {
    strncpy(notifyApp,   app,   sizeof(notifyApp)-1);   notifyApp[sizeof(notifyApp)-1]='\0';
    strncpy(notifyTitle, title, sizeof(notifyTitle)-1); notifyTitle[sizeof(notifyTitle)-1]='\0';
    strncpy(notifyBody,  body,  sizeof(notifyBody)-1);   notifyBody[sizeof(notifyBody)-1]='\0';
    notificationActive = true;
    notificationStartTime = millis();
    playNotification();
}

void Companion::drawNotificationOverlay(M5Canvas& canvas) {
    if (!notificationActive) return;
    if (millis() - notificationStartTime > NOTIFICATION_DURATION) {
        notificationActive = false; return;
    }
    int barH = 28;
    canvas.fillRect(0, 0, SCREEN_W, barH, rgb565(30, 30, 40));
    canvas.drawFastHLine(0, barH, SCREEN_W, Color::STATUS_DIM);
    canvas.setTextSize(1);
    char line1[80];
    if (notifyApp[0])
        snprintf(line1, sizeof(line1), "[%s] %s", notifyApp, notifyTitle);
    else
        snprintf(line1, sizeof(line1), "%s", notifyTitle);
    canvas.setTextColor(Color::WHITE);
    canvas.drawString(line1, 4, 2);
    canvas.setTextColor(Color::CLOCK_TEXT);
    canvas.drawString(notifyBody, 4, 15);
}

// ══════════════════════════════════════════════════════════════
//  Boot animation — scanline reveal of default sunset scene
// ══════════════════════════════════════════════════════════════

void playBootAnimation(M5Canvas& canvas) {
    canvas.fillScreen(Color::BLACK);
    canvas.pushSprite(0, 0);

    const uint8_t* src = scene_pixels_2;

    for (int revealY = 0; revealY < SCREEN_H; revealY += 2) {
        int yStart = (revealY >= 2) ? revealY - 1 : 0;
        for (int y = yStart; y <= revealY && y < SCREEN_H; y++) {
            const int off = y * SCREEN_W;
            for (int x = 0; x < SCREEN_W; x++)
                canvas.drawPixel(x, y,
                    pgm_read_word(&scene_palette[pgm_read_byte(&src[off + x])]));
        }
        canvas.pushSprite(0, 0);
        delay(8);
    }
    for (int y = SCREEN_H - 2; y < SCREEN_H; y++) {
        const int off = y * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++)
            canvas.drawPixel(x, y,
                pgm_read_word(&scene_palette[pgm_read_byte(&src[off + x])]));
    }

    uint16_t fg = rgb565(248, 248, 255);
    uint16_t bg = rgb565(12, 12, 20);
    canvas.setTextSize(2);
    const char* title = "M5Claw";
    int tw = canvas.textWidth(title);
    int tx = (SCREEN_W - tw) / 2;
    int ty = SCREEN_H / 2 - 8;
    drawOText(canvas, title, tx, ty, fg, bg);
    canvas.pushSprite(0, 0);
    delay(800);
}

// ══════════════════════════════════════════════════════════════
//  Chat ↔ Companion transition wipe
// ══════════════════════════════════════════════════════════════

void playTransition(M5Canvas& canvas, bool toChat) {
    for (int step = 0; step < 8; step++) {
        int h = (step + 1) * (SCREEN_H / 8);
        if (toChat) canvas.fillRect(0, 0, SCREEN_W, h, Color::BLACK);
        else        canvas.fillRect(0, SCREEN_H - h, SCREEN_W, h, Color::BLACK);
        canvas.pushSprite(0, 0);
        delay(25);
    }
}

void playTransitionVertical(M5Canvas& canvas, bool enter) {
    for (int step = 0; step < 8; step++) {
        int h = (step + 1) * (SCREEN_H / 8);
        if (enter) canvas.fillRect(0, SCREEN_H - h, SCREEN_W, h, Color::BLACK);
        else       canvas.fillRect(0, 0, SCREEN_W, h, Color::BLACK);
        canvas.pushSprite(0, 0);
        delay(25);
    }
}
