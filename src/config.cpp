#include "config.h"
#include "m5claw_config.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <stdint.h>

static Preferences prefs;

namespace {
constexpr const char* kEncPrefix = "enc:v1:";

static uint64_t getDeviceSecretSeed() {
    uint64_t seed = ESP.getEfuseMac();
    seed ^= 0x9E3779B97F4A7C15ULL;
    if (seed == 0) seed = 0xA5A5A5A5A5A5A5A5ULL;
    return seed;
}

static uint64_t fnv1a64(const char* s) {
    uint64_t h = 1469598103934665603ULL;
    while (s && *s) {
        h ^= (uint8_t)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static uint8_t nextMaskByte(uint64_t& state) {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return (uint8_t)((state * 2685821657736338717ULL) >> 56);
}

static char nibbleToHex(uint8_t v) {
    return (v < 10) ? char('0' + v) : char('A' + (v - 10));
}

static int hexToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static String encodeSecret(const String& plain, const char* slot) {
    if (plain.length() == 0) return "";

    uint64_t state = getDeviceSecretSeed() ^ fnv1a64(slot);
    String out = kEncPrefix;
    out.reserve(strlen(kEncPrefix) + plain.length() * 2);

    for (size_t i = 0; i < plain.length(); ++i) {
        uint8_t b = ((const uint8_t*)plain.c_str())[i] ^ nextMaskByte(state);
        out += nibbleToHex((b >> 4) & 0x0F);
        out += nibbleToHex(b & 0x0F);
    }
    return out;
}

static String decodeSecret(const String& stored, const char* slot) {
    if (!stored.startsWith(kEncPrefix)) return stored;

    const String hex = stored.substring(strlen(kEncPrefix));
    if ((hex.length() % 2) != 0) return "";

    uint64_t state = getDeviceSecretSeed() ^ fnv1a64(slot);
    String out;
    out.reserve(hex.length() / 2);

    for (size_t i = 0; i < hex.length(); i += 2) {
        int hi = hexToNibble(hex[i]);
        int lo = hexToNibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return "";
        uint8_t enc = uint8_t((hi << 4) | lo);
        out += char(enc ^ nextMaskByte(state));
    }
    return out;
}

static String readSecret(const char* key) {
    return decodeSecret(prefs.getString(key, ""), key);
}

static void writeSecret(const char* key, const String& value) {
    prefs.putString(key, encodeSecret(value, key));
}
}

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
    password        = readSecret("pass");
    ssid2           = prefs.getString("ssid2", "");
    password2       = readSecret("pass2");
    city            = prefs.getString("city", "Beijing");
    llmApiKey       = readSecret("llm_key");
    llmModel        = prefs.getString("llm_model", "");
    wechatToken     = readSecret("wc_token");
    wechatApiHost   = prefs.getString("wc_host", "");
    prefs.end();
    return ssid.length() > 0;
}

void Config::save() {
    prefs.begin("m5claw", false);
    prefs.putString("ssid",      ssid);
    writeSecret("pass",          password);
    prefs.putString("ssid2",     ssid2);
    writeSecret("pass2",         password2);
    prefs.putString("city",      city);
    writeSecret("llm_key",       llmApiKey);
    prefs.putString("llm_model", llmModel);
    writeSecret("wc_token",      wechatToken);
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
