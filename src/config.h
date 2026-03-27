#pragma once
#include <Arduino.h>

namespace Config {
    bool load();
    void save();
    void reset();
    bool importBootstrapFile();
    void applyDefaults();

    const String& getSSID();
    const String& getPassword();
    const String& getSSID2();
    const String& getPassword2();
    const String& getCity();

    // LLM config
    const String& getLlmApiKey();
    const String& getLlmModel();

    // WeChat
    const String& getWechatToken();
    const String& getWechatApiHost();

    void setSSID(const String& ssid);
    void setPassword(const String& password);
    void setSSID2(const String& ssid);
    void setPassword2(const String& password);
    void setCity(const String& city);
    void setLlmApiKey(const String& key);
    void setLlmModel(const String& model);
    void setWechatToken(const String& token);
    void setWechatApiHost(const String& host);

    bool isValid();
}
