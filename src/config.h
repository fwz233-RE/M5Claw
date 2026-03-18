#pragma once
#include <Arduino.h>

namespace Config {
    bool load();
    void save();
    void reset();

    const String& getSSID();
    const String& getPassword();
    const String& getSSID2();
    const String& getPassword2();
    const String& getCity();

    // LLM config
    const String& getLlmApiKey();
    const String& getLlmModel();
    const String& getLlmProvider();
    const String& getLlmHost();
    const String& getLlmPath();

    // DashScope config (STT + TTS)
    const String& getDashScopeKey();

    // Search
    const String& getSearchKey();
    const String& getGlmSearchKey();

    // Feishu
    const String& getFeishuAppId();
    const String& getFeishuAppSecret();

    void setSSID(const String& ssid);
    void setPassword(const String& password);
    void setSSID2(const String& ssid);
    void setPassword2(const String& password);
    void setCity(const String& city);
    void setLlmApiKey(const String& key);
    void setLlmModel(const String& model);
    void setLlmProvider(const String& provider);
    void setLlmHost(const String& host);
    void setLlmPath(const String& path);
    void setDashScopeKey(const String& key);
    void setSearchKey(const String& key);
    void setGlmSearchKey(const String& key);
    void setFeishuAppId(const String& id);
    void setFeishuAppSecret(const String& secret);

    bool isValid();
}
