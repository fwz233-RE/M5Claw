#include "config.h"
#include "m5claw_config.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPIFFS.h>

static Preferences prefs;
static String ssid, password, ssid2, password2, city;
static String llmApiKey, llmModel;
static String wechatToken, wechatApiHost;

static bool applyBootstrapValue(const JsonVariantConst& value, String& target) {
    if (value.isNull()) return false;
    const char* next = value.as<const char*>();
    if (!next) return false;
    if (target == next) return false;
    target = next;
    return true;
}

bool Config::load() {
    prefs.begin("m5claw", true);
    ssid            = prefs.getString("ssid", "");
    password        = prefs.getString("pass", "");
    ssid2           = prefs.getString("ssid2", "");
    password2       = prefs.getString("pass2", "");
    city            = prefs.getString("city", "Beijing");
    llmApiKey       = prefs.getString("llm_key", "");
    llmModel        = prefs.getString("llm_model", "");
    wechatToken     = prefs.getString("wc_token", "");
    wechatApiHost   = prefs.getString("wc_host", "");
    prefs.end();
    return ssid.length() > 0;
}

void Config::save() {
    prefs.begin("m5claw", false);
    prefs.putString("ssid",      ssid);
    prefs.putString("pass",      password);
    prefs.putString("ssid2",     ssid2);
    prefs.putString("pass2",     password2);
    prefs.putString("city",      city);
    prefs.putString("llm_key",   llmApiKey);
    prefs.putString("llm_model", llmModel);
    prefs.putString("wc_token",  wechatToken);
    prefs.putString("wc_host",   wechatApiHost);
    prefs.end();
}

void Config::reset() {
    prefs.begin("m5claw", false);
    prefs.clear();
    prefs.end();
    ssid = password = ssid2 = password2 = city = "";
    llmApiKey = llmModel = "";
    wechatToken = wechatApiHost = "";
}

bool Config::importBootstrapFile() {
    File f = SPIFFS.open(M5CLAW_BOOTSTRAP_CONFIG_FILE, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[CONFIG] Bootstrap parse error: %s\n", err.c_str());
        return false;
    }

    int changed = 0;
    changed += applyBootstrapValue(doc["wifi_ssid"], ssid) ? 1 : 0;
    changed += applyBootstrapValue(doc["wifi_pass"], password) ? 1 : 0;
    changed += applyBootstrapValue(doc["mimo_api_key"], llmApiKey) ? 1 : 0;
    changed += applyBootstrapValue(doc["llm_api_key"], llmApiKey) ? 1 : 0;
    changed += applyBootstrapValue(doc["mimo_model"], llmModel) ? 1 : 0;
    changed += applyBootstrapValue(doc["llm_model"], llmModel) ? 1 : 0;
    changed += applyBootstrapValue(doc["city"], city) ? 1 : 0;
    changed += applyBootstrapValue(doc["wechat_token"], wechatToken) ? 1 : 0;
    changed += applyBootstrapValue(doc["wechat_api_host"], wechatApiHost) ? 1 : 0;

    if (changed > 0) save();

    if (SPIFFS.remove(M5CLAW_BOOTSTRAP_CONFIG_FILE)) {
        Serial.printf("[CONFIG] Imported bootstrap config (%d values)\n", changed);
    } else {
        Serial.println("[CONFIG] Bootstrap config consumed, but cleanup failed");
    }
    return true;
}

void Config::applyDefaults() {
    if (llmModel.length() == 0) llmModel = M5CLAW_LLM_DEFAULT_MODEL;
    if (city.length() == 0) city = "Beijing";
}

const String& Config::getSSID()            { return ssid; }
const String& Config::getPassword()        { return password; }
const String& Config::getSSID2()           { return ssid2; }
const String& Config::getPassword2()       { return password2; }
const String& Config::getCity()            { return city; }
const String& Config::getLlmApiKey()       { return llmApiKey; }
const String& Config::getLlmModel()        { return llmModel; }
const String& Config::getWechatToken()     { return wechatToken; }
const String& Config::getWechatApiHost()   { return wechatApiHost; }

void Config::setSSID(const String& s)            { ssid = s; }
void Config::setPassword(const String& p)        { password = p; }
void Config::setSSID2(const String& s)           { ssid2 = s; }
void Config::setPassword2(const String& p)       { password2 = p; }
void Config::setCity(const String& c)            { city = c; }
void Config::setLlmApiKey(const String& k)       { llmApiKey = k; }
void Config::setLlmModel(const String& m)        { llmModel = m; }
void Config::setWechatToken(const String& t)     { wechatToken = t; }
void Config::setWechatApiHost(const String& h)   { wechatApiHost = h; }

bool Config::isValid() { return ssid.length() > 0 && llmApiKey.length() > 0; }
