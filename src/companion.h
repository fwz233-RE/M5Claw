#pragma once
#include <M5Cardputer.h>
#include "utils.h"
#include "weather_client.h"

class Companion {
public:
    void begin(M5Canvas& canvas);
    void update(M5Canvas& canvas);

    void setWeather(const WeatherData& wd) { weather = wd; }
    WeatherType getWeatherType() const { return weather.type; }
    float getTemperature() const { return weather.temperature; }
    bool hasValidWeather() const { return weather.valid; }

    void cycleSunset();

    void triggerHappy();
    void triggerTalk();
    void triggerIdle();
    void triggerSleep();

    void showNotification(const char* app, const char* title, const char* body);
    void drawNotificationOverlay(M5Canvas& canvas);
    bool hasActiveNotification() const { return notificationActive; }

    static void playKeyClick();
    static void playNotification();
    static void playHappy();

private:
    WeatherData weather;

    int currentScene = 1;
    int targetScene  = 1;
    bool transitionActive = false;
    int  transitionAlpha  = 0;

    static constexpr int TRANSITION_STEP   = 10;
    static constexpr int STAGGER_RANGE     = 80;
    static constexpr int TRANSITION_END    = 255 + STAGGER_RANGE;

    void drawScene(M5Canvas& canvas);
    void drawTopBar(M5Canvas& canvas);

    bool notificationActive = false;
    unsigned long notificationStartTime = 0;
    static constexpr unsigned long NOTIFICATION_DURATION = 3000;
    char notifyApp[32];
    char notifyTitle[48];
    char notifyBody[64];
};

void playBootAnimation(M5Canvas& canvas);
void playTransition(M5Canvas& canvas, bool toChat);
void playTransitionVertical(M5Canvas& canvas, bool enter);
