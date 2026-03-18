#pragma once
#include <M5Cardputer.h>
#include "utils.h"
#include "weather_client.h"

class Companion {
public:
    void begin(M5Canvas& canvas);
    void update(M5Canvas& canvas);

    void triggerHappy();
    void triggerTalk();
    void triggerIdle();
    void triggerSleep();

    void setWeather(const WeatherData& wd) { weather = wd; }

    void toggleWeatherSim();
    void setSimWeatherType(int index);
    bool isWeatherSimMode() const { return weatherSimMode; }

    WeatherType getWeatherType() const { return weather.type; }
    float getTemperature() const { return weather.temperature; }
    bool hasValidWeather() const { return weather.valid; }

    void showNotification(const char* app, const char* title, const char* body);
    void drawNotificationOverlay(M5Canvas& canvas);
    bool hasActiveNotification() const { return notificationActive; }

    static void playKeyClick();
    static void playNotification();
    static void playHappy();

private:
    int walkFrame = 0;
    Timer walkTimer{160};
    int scrollX = 0;

    WeatherData weather;
    bool weatherSimMode = false;
    int simWeatherIndex = 0;
    WeatherData simWeatherData;

    struct RainDrop { int16_t x, y; };
    static constexpr int MAX_RAIN = 18;
    RainDrop rainDrops[MAX_RAIN];
    struct Snowflake { int16_t x, y; int8_t drift; };
    static constexpr int MAX_SNOW = 18;
    Snowflake snowflakes[MAX_SNOW];
    bool weatherParticlesInit = false;
    unsigned long lastThunderFlash = 0;
    bool thunderFlashing = false;

    void drawBackground(M5Canvas& canvas);
    void drawWeatherEffects(M5Canvas& canvas);
    void drawCharacter(M5Canvas& canvas);
    void drawTopBar(M5Canvas& canvas);
    void drawSimStatusBar(M5Canvas& canvas);

    void drawSprite(M5Canvas& canvas, int x, int y, const uint16_t* data);
    void initWeatherParticles();

    bool notificationActive = false;
    unsigned long notificationStartTime = 0;
    static constexpr unsigned long NOTIFICATION_DURATION = 3000;
    char notifyApp[32];
    char notifyTitle[48];
    char notifyBody[64];
};

void playBootAnimation(M5Canvas& canvas);
void playTransition(M5Canvas& canvas, bool toChat);
