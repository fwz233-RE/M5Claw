#include "config.h"
#include <Preferences.h>

static Preferences prefs;
static String ssid, password, ssid2, password2, city;
static String llmApiKey, llmModel, llmProvider, llmHost, llmPath;
static String dashScopeKey, glmSearchKey;
static String feishuAppId, feishuAppSecret;

bool Config::load() {
    prefs.begin("m5claw", true);
    ssid            = prefs.getString("ssid", "");
    password        = prefs.getString("pass", "");
    ssid2           = prefs.getString("ssid2", "");
    password2       = prefs.getString("pass2", "");
    city            = prefs.getString("city", "Beijing");
    llmApiKey       = prefs.getString("llm_key", "");
    llmModel        = prefs.getString("llm_model", "");
    llmProvider     = prefs.getString("llm_prov", "");
    llmHost         = prefs.getString("llm_host", "");
    llmPath         = prefs.getString("llm_path", "");
    dashScopeKey    = prefs.getString("ds_key", "");
    glmSearchKey    = prefs.getString("glm_key", "");
    feishuAppId     = prefs.getString("fs_appid", "");
    feishuAppSecret = prefs.getString("fs_secret", "");
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
    prefs.putString("llm_prov",  llmProvider);
    prefs.putString("llm_host",  llmHost);
    prefs.putString("llm_path",  llmPath);
    prefs.putString("ds_key",    dashScopeKey);
    prefs.putString("glm_key",   glmSearchKey);
    prefs.putString("fs_appid",  feishuAppId);
    prefs.putString("fs_secret", feishuAppSecret);
    prefs.end();
}

void Config::reset() {
    prefs.begin("m5claw", false);
    prefs.clear();
    prefs.end();
    ssid = password = ssid2 = password2 = city = "";
    llmApiKey = llmModel = llmProvider = llmHost = llmPath = "";
    dashScopeKey = glmSearchKey = "";
    feishuAppId = feishuAppSecret = "";
}

const String& Config::getSSID()            { return ssid; }
const String& Config::getPassword()        { return password; }
const String& Config::getSSID2()           { return ssid2; }
const String& Config::getPassword2()       { return password2; }
const String& Config::getCity()            { return city; }
const String& Config::getLlmApiKey()       { return llmApiKey; }
const String& Config::getLlmModel()        { return llmModel; }
const String& Config::getLlmProvider()     { return llmProvider; }
const String& Config::getLlmHost()         { return llmHost; }
const String& Config::getLlmPath()         { return llmPath; }
const String& Config::getDashScopeKey()    { return dashScopeKey; }
const String& Config::getGlmSearchKey()    { return glmSearchKey; }
const String& Config::getFeishuAppId()     { return feishuAppId; }
const String& Config::getFeishuAppSecret() { return feishuAppSecret; }

void Config::setSSID(const String& s)            { ssid = s; }
void Config::setPassword(const String& p)        { password = p; }
void Config::setSSID2(const String& s)           { ssid2 = s; }
void Config::setPassword2(const String& p)       { password2 = p; }
void Config::setCity(const String& c)            { city = c; }
void Config::setLlmApiKey(const String& k)       { llmApiKey = k; }
void Config::setLlmModel(const String& m)        { llmModel = m; }
void Config::setLlmProvider(const String& p)     { llmProvider = p; }
void Config::setLlmHost(const String& h)         { llmHost = h; }
void Config::setLlmPath(const String& p)         { llmPath = p; }
void Config::setDashScopeKey(const String& k)    { dashScopeKey = k; }
void Config::setGlmSearchKey(const String& k)    { glmSearchKey = k; }
void Config::setFeishuAppId(const String& id)    { feishuAppId = id; }
void Config::setFeishuAppSecret(const String& s) { feishuAppSecret = s; }

bool Config::isValid() { return ssid.length() > 0 && llmApiKey.length() > 0; }
