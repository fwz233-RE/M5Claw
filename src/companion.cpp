#include "companion.h"
#include "sprites.h"
#include <time.h>
#include <math.h>

constexpr int CHAR_SCALE = 2;
constexpr int CHAR_DRAW_W = CHAR_W * CHAR_SCALE;
constexpr int CHAR_DRAW_H = CHAR_H * CHAR_SCALE;
constexpr int CHAR_X = (SCREEN_W - CHAR_DRAW_W) / 2;
constexpr int CHAR_BASE_Y = SCREEN_H - CHAR_DRAW_H - 2;

static uint16_t blendRGB565(uint16_t a, uint16_t b, uint8_t t) {
    uint8_t r1 = (a >> 11) & 0x1F, g1 = (a >> 5) & 0x3F, b1 = a & 0x1F;
    uint8_t r2 = (b >> 11) & 0x1F, g2 = (b >> 5) & 0x3F, b2 = b & 0x1F;
    uint8_t r = r1 + ((int)(r2 - r1) * t / 255);
    uint8_t g = g1 + ((int)(g2 - g1) * t / 255);
    uint8_t bl = b1 + ((int)(b2 - b1) * t / 255);
    return (r << 11) | (g << 5) | bl;
}

static uint16_t tintW(uint16_t base, WeatherType wt) {
    switch (wt) {
        case WeatherType::OVERCAST: return blendRGB565(base, rgb565(100, 100, 108), 140);
        case WeatherType::RAIN:
        case WeatherType::THUNDER:  return blendRGB565(base, rgb565(58, 58, 74), 170);
        case WeatherType::DRIZZLE:  return blendRGB565(base, rgb565(85, 85, 100), 125);
        case WeatherType::SNOW:     return blendRGB565(base, rgb565(130, 130, 142), 130);
        case WeatherType::FOG:      return blendRGB565(base, rgb565(142, 142, 148), 155);
        default: return base;
    }
}

void Companion::begin(M5Canvas& canvas) {
    scrollX = 0;
    walkFrame = 0;
    weatherParticlesInit = false;
}

void Companion::update(M5Canvas& canvas) {
    scrollX += 1;
    if (walkTimer.tick()) walkFrame++;

    drawBackground(canvas);
    drawWeatherEffects(canvas);
    drawCharacter(canvas);
    drawTopBar(canvas);
    drawSimStatusBar(canvas);
}

void Companion::triggerHappy() { playHappy(); }
void Companion::triggerTalk() {}
void Companion::triggerIdle() { playNotification(); }
void Companion::triggerSleep() {}

// ── Weather Simulation ──

static const WeatherType SIM_WEATHER_TYPES[] = {
    WeatherType::CLEAR, WeatherType::PARTLY_CLOUDY, WeatherType::OVERCAST,
    WeatherType::FOG, WeatherType::DRIZZLE, WeatherType::RAIN,
    WeatherType::SNOW, WeatherType::THUNDER
};
static const char* SIM_WEATHER_NAMES[] = {
    "Clear", "Cloudy", "Overcast", "Fog",
    "Drizzle", "Rain", "Snow", "Thunder"
};

void Companion::toggleWeatherSim() {
    weatherSimMode = !weatherSimMode;
    if (weatherSimMode) {
        simWeatherIndex = 0;
        simWeatherData.temperature = 25.0f;
        simWeatherData.type = WeatherType::CLEAR;
        simWeatherData.isDay = true;
        simWeatherData.valid = true;
        setWeather(simWeatherData);
    }
    weatherParticlesInit = false;
}

void Companion::setSimWeatherType(int index) {
    if (index < 1 || index > 8) return;
    simWeatherIndex = index - 1;
    simWeatherData.type = SIM_WEATHER_TYPES[simWeatherIndex];
    simWeatherData.valid = true;
    setWeather(simWeatherData);
    weatherParticlesInit = false;
}

// ── Sound Effects ──

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

// ══════════════════════════════════════════════════════════════
//  Mode-7 style pseudo-3D road (OutRun / F-Zero scanline technique)
//  Road angle matches character's isometric walking direction
// ══════════════════════════════════════════════════════════════

void Companion::drawBackground(M5Canvas& canvas) {
    WeatherType wt = weather.valid ? weather.type : WeatherType::CLEAR;
    int s = scrollX;

    uint16_t skyHi   = tintW(rgb565(58, 118, 208), wt);
    uint16_t skyLo   = tintW(rgb565(138, 188, 232), wt);
    uint16_t cloudW  = tintW(rgb565(232, 238, 246), wt);
    uint16_t cloudSh = tintW(rgb565(192, 200, 216), wt);
    uint16_t sunC    = tintW(rgb565(255, 225, 75), wt);

    uint16_t grassA  = tintW(rgb565(78, 155, 52), wt);
    uint16_t grassB  = tintW(rgb565(56, 122, 38), wt);
    uint16_t roadA   = tintW(rgb565(190, 174, 130), wt);
    uint16_t roadB   = tintW(rgb565(165, 148, 108), wt);
    uint16_t edgeA   = tintW(rgb565(222, 212, 192), wt);
    uint16_t edgeB   = tintW(rgb565(165, 55, 55), wt);
    uint16_t lineC   = tintW(rgb565(242, 238, 215), wt);

    constexpr int GROUND_Y = 72;

    constexpr float VP_X   = 420.0f;
    constexpr float NEAR_X = 95.0f;
    constexpr float CAM_D  = 45.0f;
    constexpr float ROAD_W = 180.0f;
    constexpr float EDGE_W = 25.0f;
    constexpr float LINE_W = 16.0f;
    constexpr float STRIPE_LEN = 8.0f;

    // ── Sky gradient fills everything above GROUND_Y ──
    for (int y = 0; y < GROUND_Y; y++) {
        uint8_t t = (uint8_t)(y * 255 / max(1, GROUND_Y - 1));
        canvas.drawFastHLine(0, y, SCREEN_W, blendRGB565(skyHi, skyLo, t));
    }

    // ── Sun ──
    bool hideSun = (wt >= WeatherType::OVERCAST);
    if (!hideSun) {
        canvas.fillCircle(32, 22, 7, sunC);
        for (int r = 8; r <= 12; r++) {
            uint8_t a = 160 - r * 10;
            canvas.drawCircle(32, 22, r, blendRGB565(skyHi, sunC, a));
        }
    }

    // ── Clouds ──
    struct CD { int wx; int y; int w; int h; int spd; };
    CD cls[] = {{50,8,24,8,5},{160,18,18,6,7},{240,12,16,5,6},{100,28,13,5,8}};
    for (auto& c : cls) {
        int cx = ((c.wx - s / c.spd) % (SCREEN_W + 50) + SCREEN_W + 50) % (SCREEN_W + 50) - 25;
        canvas.fillRoundRect(cx, c.y, c.w, c.h, 3, cloudW);
        canvas.fillRoundRect(cx + c.w / 4, c.y - c.h / 3, c.w / 2, c.h * 2 / 3, 3, cloudW);
        canvas.fillRoundRect(cx + 1, c.y + c.h - 2, c.w - 2, 2, 1, cloudSh);
    }

    // ══════════════════════════════════════════════════════════════
    //  Ground — cumulative texture coord (DDZ/DZ/Z) from bottom up
    //  Eliminates the stripe-reversal aliasing of z=A/dy formula.
    //  Road geometry still uses perspective z for correct widths.
    // ══════════════════════════════════════════════════════════════

    int groundH = SCREEN_H - GROUND_Y;

    // Pre-compute stripe index per scanline from BOTTOM upward
    // DDZ = const accel, DZ = speed, texZ = position
    constexpr float DDZ = 0.048f;
    constexpr float DZ_START = 0.5f;
    float period = STRIPE_LEN * 2.0f;
    float scrollOff = fmodf(-s * 1.2f, period);
    if (scrollOff < 0) scrollOff += period;

    int8_t stripeMap[136];
    {
        float dz = DZ_START;
        float texZ = scrollOff;
        for (int i = 0; i < groundH; i++) {
            int line = SCREEN_H - 1 - i;
            if (line >= GROUND_Y && line < SCREEN_H)
                stripeMap[line] = ((int)(texZ / STRIPE_LEN)) & 1;
            texZ += dz;
            dz += DDZ;
        }
    }

    for (int y = GROUND_Y; y < SCREEN_H; y++) {
        int dy = y - GROUND_Y;
        if (dy == 0) dy = 1;

        float z = CAM_D * (float)groundH / (float)dy;
        float scale = CAM_D / z;

        float roadHalf = ROAD_W * scale * 0.5f;
        float edgeHalf = (ROAD_W + EDGE_W * 2) * scale * 0.5f;
        float lineHalf = LINE_W * scale * 0.5f;

        float t = (float)dy / (float)groundH;
        float roadCX = VP_X + (NEAR_X - VP_X) * t;

        int stripe = stripeMap[y];

        uint16_t gC = stripe ? grassA : grassB;
        uint16_t rC = stripe ? roadA  : roadB;
        uint16_t eC = stripe ? edgeA  : edgeB;

        int rL = (int)(roadCX - roadHalf);
        int rR = (int)(roadCX + roadHalf);
        int eL = (int)(roadCX - edgeHalf);
        int eR = (int)(roadCX + edgeHalf);
        int cL = (int)(roadCX - lineHalf);
        int cR = (int)(roadCX + lineHalf);

        canvas.drawFastHLine(0, y, SCREEN_W, gC);
        if (eL >= 0 && eL < SCREEN_W && rL > eL)
            canvas.drawFastHLine(max(0,eL), y, min(rL,SCREEN_W) - max(0,eL), eC);
        if (rL < SCREEN_W && rR > 0) {
            int rxs = max(0, rL), rxe = min(SCREEN_W, rR);
            canvas.drawFastHLine(rxs, y, rxe - rxs, rC);
            if (!stripe && cL < SCREEN_W && cR > 0) {
                int lxs = max(rxs, cL), lxe = min(rxe, cR);
                if (lxe > lxs)
                    canvas.drawFastHLine(lxs, y, lxe - lxs, lineC);
            }
        }
        if (rR < SCREEN_W && eR > rR)
            canvas.drawFastHLine(max(0,rR), y, min(eR,SCREEN_W) - max(0,rR), eC);
    }
}

// ══════════════════════════════════════════════════════════════
//  Weather Effects
// ══════════════════════════════════════════════════════════════

void Companion::initWeatherParticles() {
    for (int i = 0; i < MAX_RAIN; i++) {
        rainDrops[i].x = random(SCREEN_W);
        rainDrops[i].y = random(SCREEN_H);
    }
    for (int i = 0; i < MAX_SNOW; i++) {
        snowflakes[i].x = random(SCREEN_W);
        snowflakes[i].y = random(SCREEN_H);
        snowflakes[i].drift = random(3) - 1;
    }
    weatherParticlesInit = true;
}

void Companion::drawWeatherEffects(M5Canvas& canvas) {
    if (!weather.valid) return;
    if (!weatherParticlesInit) initWeatherParticles();

    switch (weather.type) {
        case WeatherType::RAIN:
        case WeatherType::DRIZZLE:
        case WeatherType::THUNDER: {
            int count = (weather.type == WeatherType::DRIZZLE) ? 10 : MAX_RAIN;
            int speed = (weather.type == WeatherType::DRIZZLE) ? 3 : 5;
            int len   = (weather.type == WeatherType::DRIZZLE) ? 3 : 5;
            uint16_t rc = rgb565(140, 160, 200);
            for (int i = 0; i < count; i++) {
                rainDrops[i].y += speed;
                rainDrops[i].x += 1;
                if (rainDrops[i].y >= SCREEN_H || rainDrops[i].x >= SCREEN_W) {
                    rainDrops[i].y = random(-10, 0);
                    rainDrops[i].x = random(SCREEN_W);
                }
                if (rainDrops[i].y >= 0) {
                    int sy = max(0, (int)rainDrops[i].y);
                    int ey = min((int)rainDrops[i].y + len, SCREEN_H);
                    canvas.drawFastVLine(rainDrops[i].x, sy, ey - sy, rc);
                }
            }
            if (weather.type == WeatherType::THUNDER) {
                unsigned long now = millis();
                if (!thunderFlashing && now - lastThunderFlash > 3000 + random(2000)) {
                    thunderFlashing = true; lastThunderFlash = now;
                }
                if (thunderFlashing) {
                    if (now - lastThunderFlash < 50) {
                        uint16_t fl = rgb565(210, 210, 225);
                        for (int y = 0; y < SCREEN_H; y += 2)
                            for (int x = (y / 2) % 2; x < SCREEN_W; x += 2)
                                canvas.drawPixel(x, y, fl);
                    } else thunderFlashing = false;
                }
            }
            break;
        }
        case WeatherType::SNOW: {
            uint16_t sc = rgb565(225, 225, 235);
            for (int i = 0; i < MAX_SNOW; i++) {
                snowflakes[i].y += 1;
                snowflakes[i].x += snowflakes[i].drift;
                if (random(20) == 0) snowflakes[i].drift = random(3) - 1;
                if (snowflakes[i].y >= SCREEN_H) { snowflakes[i].y = random(-5, 0); snowflakes[i].x = random(SCREEN_W); }
                if (snowflakes[i].x < 0) snowflakes[i].x = SCREEN_W - 1;
                if (snowflakes[i].x >= SCREEN_W) snowflakes[i].x = 0;
                if (snowflakes[i].y >= 0) {
                    canvas.drawPixel(snowflakes[i].x, snowflakes[i].y, sc);
                    if (i % 3 == 0) canvas.drawPixel(snowflakes[i].x + 1, snowflakes[i].y, sc);
                }
            }
            break;
        }
        case WeatherType::FOG: {
            uint16_t fc = rgb565(165, 165, 170);
            int off = (millis() / 200) % 3;
            for (int y = 5 + off; y < SCREEN_H; y += 4)
                for (int x = (y % 6); x < SCREEN_W; x += 6)
                    canvas.drawPixel(x, y, fc);
            break;
        }
        default: break;
    }
}

// ══════════════════════════════════════════════════════════════
//  Character — 4-frame walk cycle (feet shift left/right)
// ══════════════════════════════════════════════════════════════

void Companion::drawCharacter(M5Canvas& canvas) {
    int phase = walkFrame % WALK_FRAME_COUNT;
    const uint16_t* frame = walk_frames[phase];
    int yOff = (phase == 1 || phase == 3) ? -2 : 0;
    drawSprite(canvas, CHAR_X, CHAR_BASE_Y + yOff, frame);
}

void Companion::drawSprite(M5Canvas& canvas, int x, int y, const uint16_t* data) {
    for (int py = 0; py < CHAR_H; py++) {
        for (int px = 0; px < CHAR_W; px++) {
            uint16_t color = pgm_read_word(&data[py * CHAR_W + px]);
            if (color != Color::TRANSPARENT) {
                canvas.fillRect(x + px * CHAR_SCALE, y + py * CHAR_SCALE,
                               CHAR_SCALE, CHAR_SCALE, color);
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════
//  Top Bar — fully transparent with outlined text
// ══════════════════════════════════════════════════════════════

static void drawOText(M5Canvas& c, const char* t, int x, int y, uint16_t fg, uint16_t bg) {
    c.setTextColor(bg);
    c.drawString(t, x-1, y); c.drawString(t, x+1, y);
    c.drawString(t, x, y-1); c.drawString(t, x, y+1);
    c.setTextColor(fg);
    c.drawString(t, x, y);
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

    if (weather.valid) {
        int t = (int)roundf(weather.temperature);
        char ts[8]; snprintf(ts, sizeof(ts), "%dC", t);
        drawOText(canvas, ts, rx, 3, fg, bg);
        rx -= canvas.textWidth(ts) + 8;
    }

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
    }
    canvas.setTextDatum(TL_DATUM);
}

void Companion::drawSimStatusBar(M5Canvas& canvas) {
    if (!weatherSimMode) return;
    int barY = SCREEN_H - 12;
    canvas.fillRect(0, barY, SCREEN_W, 12, rgb565(20, 20, 20));
    canvas.setTextColor(Color::WHITE); canvas.setTextSize(1);
    char label[32];
    snprintf(label, sizeof(label), "[SIM] %s (%d)", SIM_WEATHER_NAMES[simWeatherIndex], simWeatherIndex + 1);
    int tw = canvas.textWidth(label);
    canvas.drawString(label, (SCREEN_W - tw) / 2, barY + 2);
}

// ── Notification Toast ──

void Companion::showNotification(const char* app, const char* title, const char* body) {
    strncpy(notifyApp, app, sizeof(notifyApp)-1);   notifyApp[sizeof(notifyApp)-1]='\0';
    strncpy(notifyTitle, title, sizeof(notifyTitle)-1); notifyTitle[sizeof(notifyTitle)-1]='\0';
    strncpy(notifyBody, body, sizeof(notifyBody)-1);   notifyBody[sizeof(notifyBody)-1]='\0';
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
    if (notifyApp[0]) snprintf(line1, sizeof(line1), "[%s] %s", notifyApp, notifyTitle);
    else snprintf(line1, sizeof(line1), "%s", notifyTitle);
    canvas.setTextColor(Color::WHITE); canvas.drawString(line1, 4, 2);
    canvas.setTextColor(Color::CLOCK_TEXT); canvas.drawString(notifyBody, 4, 15);
}

// ══════════════════════════════════════════════════════════════
//  Boot Animation
// ══════════════════════════════════════════════════════════════

void playBootAnimation(M5Canvas& canvas) {
    for (int row = 0; row < CHAR_H; row++) {
        canvas.fillScreen(Color::BLACK);
        int scale = 2;
        int ox = (SCREEN_W - CHAR_W * scale) / 2;
        int oy = (SCREEN_H - CHAR_H * scale) / 2 - 10;
        for (int py = 0; py <= row; py++)
            for (int px = 0; px < CHAR_W; px++) {
                uint16_t c = pgm_read_word(&sprite_stand[py * CHAR_W + px]);
                if (c != Color::TRANSPARENT)
                    canvas.fillRect(ox + px * scale, oy + py * scale, scale, scale, c);
            }
        canvas.pushSprite(0, 0);
        delay(50);
    }
    delay(400);
    canvas.setTextColor(Color::CLOCK_TEXT); canvas.setTextSize(1);
    int textY = (SCREEN_H + CHAR_H * 2) / 2 + 4;
    const char* title = "M5Claw";
    canvas.drawString(title, (SCREEN_W - canvas.textWidth(title)) / 2, textY);
    canvas.pushSprite(0, 0);
    delay(800);
    for (int i = 0; i < 8; i++) {
        canvas.fillRect(0, 0, SCREEN_W, SCREEN_H, canvas.color565(0, 0, 0));
        for (int y = 0; y < SCREEN_H; y += 2)
            for (int x = (i % 2 == 0 ? 0 : 1); x < SCREEN_W; x += 2)
                canvas.drawPixel(x, y, Color::BLACK);
        canvas.pushSprite(0, 0); delay(60);
    }
    canvas.fillScreen(Color::BLACK); canvas.pushSprite(0, 0); delay(200);
}

// ══════════════════════════════════════════════════════════════

void playTransition(M5Canvas& canvas, bool toChat) {
    for (int step = 0; step < 8; step++) {
        int h = (step + 1) * (SCREEN_H / 8);
        if (toChat) canvas.fillRect(0, 0, SCREEN_W, h, Color::BLACK);
        else canvas.fillRect(0, SCREEN_H - h, SCREEN_W, h, Color::BLACK);
        canvas.pushSprite(0, 0); delay(25);
    }
}
