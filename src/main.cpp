#include <M5Cardputer.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <time.h>
#include "utils.h"
#include "m5claw_config.h"
#include "config.h"
#include "companion.h"
#include "chat.h"
#include "weather_client.h"
#include "agent.h"
#include "llm_client.h"
#include "tool_registry.h"
#include "memory_store.h"
#include "session_mgr.h"
#include "context_builder.h"
#include "dashscope_stt.h"
#include "dashscope_tts.h"
#include <esp_heap_caps.h>
#include "soc/rtc_cntl_reg.h"

#ifndef USER_WIFI_SSID
#define USER_WIFI_SSID ""
#endif
#ifndef USER_WIFI_PASS
#define USER_WIFI_PASS ""
#endif
#ifndef USER_LLM_KEY
#define USER_LLM_KEY ""
#endif
#ifndef USER_LLM_PROVIDER
#define USER_LLM_PROVIDER ""
#endif
#ifndef USER_LLM_MODEL
#define USER_LLM_MODEL ""
#endif
#ifndef USER_DS_KEY
#define USER_DS_KEY ""
#endif
#ifndef USER_CITY
#define USER_CITY ""
#endif
#ifndef USER_LLM_HOST
#define USER_LLM_HOST ""
#endif
#ifndef USER_LLM_PATH
#define USER_LLM_PATH ""
#endif

M5Canvas canvas(&M5Cardputer.Display);
Companion companion;
Chat chat;
WeatherClient weatherClient;

enum class AppMode { SETUP, COMPANION, CHAT };
static AppMode appMode = AppMode::SETUP;
static bool offlineMode = false;

enum class SetupStep {
    SSID, PASSWORD, LLM_KEY, LLM_PROVIDER, LLM_MODEL,
    DASHSCOPE_KEY, CITY, CONNECTING
};
static SetupStep setupStep = SetupStep::SSID;
static String setupInput;

static int16_t* voiceBuffer = nullptr;
static size_t voiceBufferSamples = 0;
static bool voiceRecording = false;
static size_t recordedSamples = 0;
static unsigned long recordingStartMs = 0;
static bool ttsPlaying = false;
static size_t ttsSamples = 0;

static const size_t STT_CHUNK_SAMPLES = 1600; // 100ms at 16kHz = 3.2KB
static int16_t* sttChunkBuf = nullptr;

void enterSetupMode();
void updateSetupMode();
void handleSetupKey(char key, bool enter, bool backspace, bool tab);
bool tryConnect(const String& ssid, const String& pass);
void connectWiFi();
void initOnlineServices();
void enterCompanionMode();
void enterChatMode();
void initVoiceBuffer();
void releaseVoiceBuffer();
void startVoiceRecording();
String stopVoiceRecording();
void streamVoiceData();
void fillBuildTimeDefaults();
void processSerialCommands();

static void onAgentResponse(const char* text);
static volatile bool agentResponseReady = false;
static char* agentResponseText = nullptr;

static bool hasPreconfiguredOnlineSettings() {
    bool hasLlm = Config::getLlmApiKey().length() > 0
               && Config::getLlmProvider().length() > 0
               && Config::getLlmModel().length() > 0;
    bool hasSpeech = Config::getDashScopeKey().length() > 0;
    return hasLlm || hasSpeech;
}

static void setIfEmpty(void (*setter)(const String&), const String& current, const char* buildVal) {
    if (current.length() == 0 && buildVal && buildVal[0])
        setter(String(buildVal));
}

void fillBuildTimeDefaults() {
    setIfEmpty(Config::setSSID,         Config::getSSID(),         USER_WIFI_SSID);
    setIfEmpty(Config::setPassword,     Config::getPassword(),     USER_WIFI_PASS);
    setIfEmpty(Config::setLlmApiKey,    Config::getLlmApiKey(),    USER_LLM_KEY);
    setIfEmpty(Config::setLlmProvider,  Config::getLlmProvider(),  USER_LLM_PROVIDER);
    setIfEmpty(Config::setLlmModel,     Config::getLlmModel(),     USER_LLM_MODEL);
    setIfEmpty(Config::setDashScopeKey, Config::getDashScopeKey(), USER_DS_KEY);
    setIfEmpty(Config::setCity,         Config::getCity(),         USER_CITY);
    setIfEmpty(Config::setLlmHost,      Config::getLlmHost(),      USER_LLM_HOST);
    setIfEmpty(Config::setLlmPath,      Config::getLlmPath(),      USER_LLM_PATH);
    if (Config::getLlmProvider().length() == 0) Config::setLlmProvider("anthropic");
    if (Config::getLlmModel().length() == 0) Config::setLlmModel(M5CLAW_LLM_DEFAULT_MODEL);
    if (Config::getCity().length() == 0) Config::setCity("Beijing");
}

void processSerialCommands() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    int sep = line.indexOf(' ');
    String cmd = (sep > 0) ? line.substring(0, sep) : line;
    String val = (sep > 0) ? line.substring(sep + 1) : "";
    val.trim();

    if (cmd == "help") {
        Serial.println("=== M5Claw Serial Config ===");
        Serial.println("  set_wifi <ssid> <pass>   - Set WiFi");
        Serial.println("  set_llm_key <key>        - Set LLM API key");
        Serial.println("  set_llm_provider <p>     - anthropic or openai");
        Serial.println("  set_llm_model <model>    - e.g. claude-sonnet-4-20250514");
        Serial.println("  set_ds_key <key>         - Set DashScope key");
        Serial.println("  set_city <city>          - e.g. Beijing");
        Serial.println("  show_config              - Show current config");
        Serial.println("  reset_config             - Clear all config");
        Serial.println("  reboot                   - Restart device");
    } else if (cmd == "set_wifi") {
        int sp = val.indexOf(' ');
        if (sp > 0) {
            Config::setSSID(val.substring(0, sp));
            Config::setPassword(val.substring(sp + 1));
            Config::save();
            Serial.printf("WiFi set: %s\n", Config::getSSID().c_str());
        } else {
            Serial.println("Usage: set_wifi <ssid> <password>");
        }
    } else if (cmd == "set_llm_key") {
        Config::setLlmApiKey(val); Config::save();
        Serial.println("LLM key saved");
    } else if (cmd == "set_llm_provider") {
        Config::setLlmProvider(val); Config::save();
        Serial.printf("Provider: %s\n", val.c_str());
    } else if (cmd == "set_llm_model") {
        Config::setLlmModel(val); Config::save();
        Serial.printf("Model: %s\n", val.c_str());
    } else if (cmd == "set_ds_key") {
        Config::setDashScopeKey(val); Config::save();
        Serial.println("DashScope key saved");
    } else if (cmd == "set_city") {
        Config::setCity(val); Config::save();
        Serial.printf("City: %s\n", val.c_str());
    } else if (cmd == "show_config") {
        Serial.println("=== Current Config ===");
        Serial.printf("  WiFi SSID:     %s\n", Config::getSSID().c_str());
        Serial.printf("  WiFi Pass:     [%d chars]\n", Config::getPassword().length());
        Serial.printf("  LLM Provider:  %s\n", Config::getLlmProvider().c_str());
        Serial.printf("  LLM Model:     %s\n", Config::getLlmModel().c_str());
        Serial.printf("  LLM Key:       [%d chars]\n", Config::getLlmApiKey().length());
        Serial.printf("  DashScope Key: [%d chars]\n", Config::getDashScopeKey().length());
        Serial.printf("  City:          %s\n", Config::getCity().c_str());
        Serial.printf("  Valid:         %s\n", Config::isValid() ? "YES" : "NO");
    } else if (cmd == "reset_config") {
        Config::reset(); Config::save();
        Serial.println("Config cleared. Reboot to enter setup.");
    } else if (cmd == "reboot") {
        Serial.println("Rebooting...");
        delay(100);
        ESP.restart();
    } else {
        Serial.printf("Unknown command: %s (type 'help')\n", cmd.c_str());
    }
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    auto cfg = M5.config();
    cfg.output_power = true;
    M5Cardputer.begin(cfg, true);

    Serial.begin(115200);
    delay(500);
    Serial.println("[BOOT] M5Claw starting...");

    int32_t battLevel = M5Cardputer.Power.getBatteryLevel();
    int16_t battVolt = M5Cardputer.Power.getBatteryVoltage();
    Serial.printf("[POWER] Board=%d BattLevel=%d%% BattVolt=%dmV\n",
                  (int)M5.getBoard(), battLevel, battVolt);
    if (battVolt < 3000) {
        Serial.println("[POWER] WARNING: Low/no battery. Turn ON the battery switch on the side of M5Cardputer.");
    }

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(80);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.setTextWrap(false);

    canvas.fillScreen(Color::BLACK);
    canvas.setTextColor(Color::WHITE);
    canvas.setTextSize(1);
    canvas.drawString("M5Claw booting...", 60, 60);
    canvas.pushSprite(0, 0);
    Serial.println("[BOOT] Display OK");

    M5Cardputer.Speaker.setVolume(255);

    Config::load();
    fillBuildTimeDefaults();
    Config::save();
    Serial.println("[BOOT] Config loaded");
    Serial.println("[BOOT] Type 'help' in serial monitor for config commands");

    MemoryStore::init();
    Serial.println("[BOOT] SPIFFS OK");

    SessionMgr::init();
    ToolRegistry::init();
    Agent::init();
    Serial.println("[BOOT] Agent initialized");

    playBootAnimation(canvas);

    if (Config::isValid()) {
        connectWiFi();
    } else {
        enterSetupMode();
    }
}

void loop() {
    M5Cardputer.update();

    if (agentResponseReady && agentResponseText) {
        char filtered[512];
        filterForDisplayBuf(agentResponseText, filtered, sizeof(filtered));
        chat.appendAIToken(filtered);
        chat.onAIResponseComplete();

        if (strlen(agentResponseText) > 0
            && Config::getDashScopeKey().length() > 0) {
            const char* shortText = agentResponseText;
            size_t textLen = strlen(shortText);
            if (textLen > 200) textLen = 200;
            char ttsText[201];
            memcpy(ttsText, shortText, textLen);
            ttsText[textLen] = '\0';

            if (!voiceBuffer) initVoiceBuffer();
            size_t heapAfterBuf = ESP.getFreeHeap();
            if (voiceBuffer && heapAfterBuf > 80000) {
                M5Cardputer.Speaker.end();
                delay(50);
                M5Cardputer.Mic.end();
                auto spkCfg = M5Cardputer.Speaker.config();
                M5Cardputer.Speaker.config(spkCfg);
                M5Cardputer.Speaker.begin();

                ttsSamples = DashScopeTTS::synthesize(ttsText, voiceBuffer, voiceBufferSamples);
                if (ttsSamples > 0) {
                    ttsPlaying = true;
                    M5Cardputer.Speaker.playRaw(voiceBuffer, ttsSamples, M5CLAW_TTS_SAMPLE_RATE, false);
                } else {
                    releaseVoiceBuffer();
                }
            } else {
                Serial.printf("[TTS] Skipped: heap=%d after buf alloc (need >80000)\n", (int)heapAfterBuf);
                releaseVoiceBuffer();
            }
        }

        companion.triggerIdle();
        free(agentResponseText);
        agentResponseText = nullptr;
        agentResponseReady = false;
    }

    if (ttsPlaying && !M5Cardputer.Speaker.isPlaying()) {
        ttsPlaying = false;
        releaseVoiceBuffer();
    }

    switch (appMode) {
        case AppMode::SETUP:
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
                auto keys = M5Cardputer.Keyboard.keysState();
                bool enter = keys.enter;
                bool backspace = keys.del;
                bool tab = keys.tab;
                char key = 0;
                if (keys.word.size() > 0) key = keys.word[0];
                handleSetupKey(key, enter, backspace, tab);
            }
            updateSetupMode();
            break;

        case AppMode::COMPANION: {
            auto ks = M5Cardputer.Keyboard.keysState();
            bool keyPressed = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();

            if (keyPressed) {
                if (ks.tab) {
                    playTransition(canvas, true);
                    enterChatMode();
                    break;
                }
                if (ks.fn && ks.word.size() > 0 && ks.word[0] == 'w') {
                    companion.toggleWeatherSim();
                    break;
                }
                if (ks.fn && ks.word.size() > 0 && ks.word[0] == 'r') {
                    WiFi.disconnect(true);
                    Config::reset();
                    Config::save();
                    enterSetupMode();
                    break;
                }
                if (companion.isWeatherSimMode() && ks.word.size() > 0) {
                    char ch = ks.word[0];
                    if (ch >= '1' && ch <= '8') {
                        companion.setSimWeatherType(ch - '0');
                        break;
                    }
                }
                char key = 0;
                if (ks.enter) key = '\n';
                else if (ks.word.size() > 0) {
                    char ch = ks.word[0];
                    if (ch != ';' && ch != '.' && ch != ',' && ch != '/')
                        key = ch;
                }
                if (key) companion.handleKey(key);
            }

            for (char ch : ks.word) {
                switch (ch) {
                    case ';': companion.move(0, -1); break;
                    case '.': companion.move(0,  1); break;
                    case ',': companion.move(-1, 0); break;
                    case '/': companion.move( 1, 0); break;
                }
            }

            if (!offlineMode) weatherClient.update();
            if (!companion.isWeatherSimMode()) {
                companion.setWeather(weatherClient.getData());
            }
            companion.update(canvas);
            companion.drawNotificationOverlay(canvas);
            canvas.pushSprite(0, 0);
            break;
        }

        case AppMode::CHAT: {
            auto ks = M5Cardputer.Keyboard.keysState();
            static bool pFn = false, pEnter = false, pDel = false, pTab = false;
            static bool pAlt = false, pCtrl = false;
            static char pWordChar = 0;
            bool didBlock = false;

            bool fnDown = ks.fn && !pFn;
            bool fnUp = !ks.fn && pFn;
            bool fnAlone = ks.fn && ks.word.size() == 0
                           && !ks.tab && !ks.enter && !ks.del;

            if (!offlineMode && fnDown && fnAlone && !voiceRecording
                && !Agent::isBusy() && !ttsPlaying
                && Config::getDashScopeKey().length() > 0) {
                chat.update(canvas);
                canvas.fillRect(0, SCREEN_H - 16, SCREEN_W, 16, rgb565(50, 50, 200));
                canvas.setTextColor(Color::WHITE);
                canvas.setTextSize(1);
                canvas.drawString("Connecting...", 80, SCREEN_H - 12);
                canvas.pushSprite(0, 0);
                startVoiceRecording();
            }
            if (fnUp && voiceRecording) {
                String text = stopVoiceRecording();
                if (text.length() > 0) chat.setInput(text);
                didBlock = true;
            }

            if (!didBlock && voiceRecording) {
                streamVoiceData();
                if (!DashScopeSTT::isStreaming()) {
                    stopVoiceRecording();
                    didBlock = true;
                }
            }

            if (didBlock) {
                M5Cardputer.update();
                ks = M5Cardputer.Keyboard.keysState();
            }

            bool enterDown = !didBlock && ks.enter && !pEnter;
            bool delDown   = !didBlock && ks.del && !pDel;
            bool tabDown   = !didBlock && ks.tab && !pTab;
            bool altDown   = !didBlock && ks.alt && !pAlt;
            bool ctrlDown  = !didBlock && ks.ctrl && !pCtrl;
            char curWordChar = (ks.word.size() > 0) ? ks.word[0] : 0;
            bool charDown  = !didBlock && curWordChar != 0 && curWordChar != pWordChar;

            pFn = ks.fn; pEnter = ks.enter; pDel = ks.del; pTab = ks.tab;
            pAlt = ks.alt; pCtrl = ks.ctrl;
            pWordChar = curWordChar;

            if (!voiceRecording) {
                if (altDown) {
                    playTransition(canvas, false);
                    enterCompanionMode();
                    break;
                }
                if (tabDown) {
                    chat.scrollUp();
                } else if (ctrlDown) {
                    if (chat.isAtBottom()) {
                        playTransition(canvas, false);
                        enterCompanionMode();
                        break;
                    }
                    chat.scrollDown();
                } else if (enterDown) {
                    chat.handleEnter();
                } else if (delDown) {
                    chat.handleBackspace();
                } else if (charDown && !ks.fn) {
                    chat.handleKey(ks.word[0]);
                }
            }

            if (chat.hasPendingMessage() && !Agent::isBusy()) {
                if (offlineMode) {
                    String msg = chat.takePendingMessage();
                    chat.appendAIToken("[Offline] No network");
                    chat.onAIResponseComplete();
                } else {
                    String msg = chat.takePendingMessage();
                    Serial.printf("[CHAT] Sending: %s\n", msg.c_str());
                    companion.triggerTalk();
                    releaseVoiceBuffer();

                    Agent::sendMessage(msg.c_str(), onAgentResponse);

                    M5Cardputer.update();
                    ks = M5Cardputer.Keyboard.keysState();
                    pFn = ks.fn; pEnter = ks.enter; pDel = ks.del; pTab = ks.tab;
                    pAlt = ks.alt; pCtrl = ks.ctrl;
                    pWordChar = (ks.word.size() > 0) ? ks.word[0] : 0;
                    enterDown = delDown = tabDown = altDown = ctrlDown = charDown = false;
                }
            }

            if (ttsPlaying && (enterDown || delDown || tabDown || altDown || charDown || fnDown)) {
                M5Cardputer.Speaker.stop();
                ttsPlaying = false;
                releaseVoiceBuffer();
            }

            chat.update(canvas);
            if (voiceRecording) {
                float dur = (float)(millis() - recordingStartMs) / 1000.0f;
                canvas.fillRect(0, SCREEN_H - 16, SCREEN_W, 16, rgb565(200, 50, 50));
                canvas.setTextColor(Color::WHITE);
                canvas.setTextSize(1);
                String partial = DashScopeSTT::getPartialText();
                if (partial.length() > 0) {
                    char recLabel[128];
                    snprintf(recLabel, sizeof(recLabel), "%.0fs %s", dur, partial.c_str());
                    canvas.drawString(recLabel, 4, SCREEN_H - 12);
                } else {
                    char recLabel[32];
                    snprintf(recLabel, sizeof(recLabel), "Recording... %.1fs", dur);
                    canvas.drawString(recLabel, 70, SCREEN_H - 12);
                }
            } else if (ttsPlaying) {
                canvas.fillRect(0, SCREEN_H - 16, SCREEN_W, 16, rgb565(50, 120, 200));
                canvas.setTextColor(Color::WHITE);
                canvas.setTextSize(1);
                canvas.drawString("Speaking...", 85, SCREEN_H - 12);
            }
            canvas.pushSprite(0, 0);
            break;
        }
    }

    processSerialCommands();
    delay(16);
}

static void onAgentResponse(const char* text) {
    agentResponseText = strdup(text);
    agentResponseReady = true;
}

void initVoiceBuffer() {
    if (voiceBuffer) return;

    const size_t TLS_RESERVE = 50 * 1024;
    const size_t MAX_SAMPLES = 80000;
    const size_t MIN_SAMPLES = 8000;

    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap <= TLS_RESERVE) {
        Serial.printf("[VOICE] Buffer: FAIL, heap=%d too low (need %d reserved)\n",
                      (int)freeHeap, (int)TLS_RESERVE);
        return;
    }

    size_t maxBytes = freeHeap - TLS_RESERVE;
    size_t maxSamples = maxBytes / sizeof(int16_t);
    if (maxSamples > MAX_SAMPLES) maxSamples = MAX_SAMPLES;
    maxSamples = (maxSamples / 1000) * 1000;

    if (maxSamples < MIN_SAMPLES) {
        Serial.printf("[VOICE] Buffer: FAIL, only %d samples possible\n", (int)maxSamples);
        return;
    }

    size_t bytes = maxSamples * sizeof(int16_t);
    voiceBuffer = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    if (voiceBuffer) {
        voiceBufferSamples = maxSamples;
        Serial.printf("[VOICE] Buffer: OK, %d samples (%.2fs @24k / %.2fs @16k) heap=%d\n",
                      (int)voiceBufferSamples,
                      (float)voiceBufferSamples / M5CLAW_TTS_SAMPLE_RATE,
                      (float)voiceBufferSamples / M5CLAW_STT_SAMPLE_RATE,
                      ESP.getFreeHeap());
    } else {
        voiceBufferSamples = 0;
        Serial.printf("[VOICE] Buffer: FAIL, malloc(%d) failed, heap=%d\n",
                      (int)bytes, (int)freeHeap);
    }
}

void releaseVoiceBuffer() {
    if (!voiceBuffer) return;
    free(voiceBuffer);
    voiceBuffer = nullptr;
    voiceBufferSamples = 0;
    recordedSamples = 0;
}

void startVoiceRecording() {
    M5Cardputer.Speaker.end();

    if (!sttChunkBuf) {
        sttChunkBuf = (int16_t*)malloc(STT_CHUNK_SAMPLES * sizeof(int16_t));
    }
    if (!sttChunkBuf) {
        Serial.println("[VOICE] Failed to alloc chunk buffer");
        return;
    }

    auto micCfg = M5Cardputer.Mic.config();
    micCfg.sample_rate = M5CLAW_STT_SAMPLE_RATE;
    micCfg.magnification = 64;
    micCfg.noise_filter_level = 64;
    micCfg.task_priority = 1;
    M5Cardputer.Mic.config(micCfg);
    M5Cardputer.Mic.begin();

    if (!DashScopeSTT::beginStream()) {
        Serial.println("[VOICE] STT stream failed");
        M5Cardputer.Mic.end();
        M5Cardputer.Speaker.begin();
        free(sttChunkBuf); sttChunkBuf = nullptr;
        return;
    }

    memset(sttChunkBuf, 0, STT_CHUNK_SAMPLES * sizeof(int16_t));
    M5Cardputer.Mic.record(sttChunkBuf, STT_CHUNK_SAMPLES, M5CLAW_STT_SAMPLE_RATE);
    voiceRecording = true;
    recordedSamples = 0;
    recordingStartMs = millis();
    Serial.printf("[VOICE] Streaming started, heap=%d\n", ESP.getFreeHeap());
}

void streamVoiceData() {
    if (!voiceRecording || !sttChunkBuf) return;

    if (!M5Cardputer.Mic.isRecording()) {
        DashScopeSTT::feedAudio(sttChunkBuf, STT_CHUNK_SAMPLES);
        recordedSamples += STT_CHUNK_SAMPLES;
        memset(sttChunkBuf, 0, STT_CHUNK_SAMPLES * sizeof(int16_t));
        M5Cardputer.Mic.record(sttChunkBuf, STT_CHUNK_SAMPLES, M5CLAW_STT_SAMPLE_RATE);
    }

    DashScopeSTT::poll();
}

String stopVoiceRecording() {
    if (!voiceRecording) return "";
    voiceRecording = false;

    M5Cardputer.Mic.end();

    if (sttChunkBuf) {
        DashScopeSTT::feedAudio(sttChunkBuf, STT_CHUNK_SAMPLES);
    }

    String result = DashScopeSTT::endStream();

    free(sttChunkBuf);
    sttChunkBuf = nullptr;

    M5Cardputer.Speaker.begin();

    float duration = (float)(millis() - recordingStartMs) / 1000.0f;
    Serial.printf("[VOICE] Stopped streaming, %.2fs, result: %s\n", duration, result.c_str());
    return result;
}

void enterSetupMode() {
    appMode = AppMode::SETUP;
    setupStep = SetupStep::SSID;
    setupInput = "";
}

static void getDefaultHint(char* buf, int bufSize, const String& value, bool isPassword) {
    if (value.length() == 0) {
        snprintf(buf, bufSize, "(empty)");
    } else if (isPassword) {
        snprintf(buf, bufSize, "[%d chars set]", value.length());
    } else {
        int maxShow = bufSize - 3;
        if ((int)value.length() > maxShow) {
            snprintf(buf, bufSize, "[...%s]", value.c_str() + value.length() - maxShow + 4);
        } else {
            snprintf(buf, bufSize, "[%s]", value.c_str());
        }
    }
}

void updateSetupMode() {
    canvas.fillScreen(Color::BG_DAY);
    canvas.setTextColor(Color::CLOCK_TEXT);
    canvas.setTextSize(1);
    canvas.drawString("=== M5Claw Setup ===", 55, 4);

    char hint[64];
    const char* label = "";
    String currentVal;
    bool isPass = false;

    switch (setupStep) {
        case SetupStep::SSID:        label = "WiFi SSID:";     currentVal = Config::getSSID();         break;
        case SetupStep::PASSWORD:    label = "WiFi Password:";  currentVal = Config::getPassword();     isPass = true; break;
        case SetupStep::LLM_KEY:     label = "LLM API Key:";   currentVal = Config::getLlmApiKey();    isPass = true; break;
        case SetupStep::LLM_PROVIDER:label = "Provider(anthropic/openai):"; currentVal = Config::getLlmProvider(); break;
        case SetupStep::LLM_MODEL:   label = "LLM Model:";     currentVal = Config::getLlmModel();     break;
        case SetupStep::DASHSCOPE_KEY:label= "DashScope Key:";  currentVal = Config::getDashScopeKey(); isPass = true; break;
        case SetupStep::CITY:        label = "City:";           currentVal = Config::getCity();          break;
        case SetupStep::CONNECTING:
            canvas.drawString("Connecting to WiFi...", 50, 55);
            canvas.pushSprite(0, 0);
            return;
    }

    canvas.drawString(label, 10, 25);
    getDefaultHint(hint, sizeof(hint), currentVal, isPass);
    canvas.setTextColor(Color::STATUS_DIM);
    int labelW = canvas.textWidth(label);
    canvas.drawString(hint, 10 + labelW + 4, 25);

    canvas.setTextColor(Color::WHITE);
    if (isPass && setupInput.length() > 0) {
        char masked[64];
        int len = setupInput.length();
        if (len > 62) len = 62;
        memset(masked, '*', len);
        masked[len] = '_';
        masked[len + 1] = '\0';
        canvas.drawString(masked, 10, 45);
    } else {
        String display = setupInput + "_";
        if (display.length() > 35) {
            display = "..." + display.substring(display.length() - 32);
        }
        canvas.drawString(display.c_str(), 10, 45);
    }

    canvas.setTextColor(Color::STATUS_DIM);
    canvas.drawString("[Enter] confirm  [Tab] skip/cancel", 10, 70);
    if ((setupStep == SetupStep::SSID || setupStep == SetupStep::PASSWORD) && hasPreconfiguredOnlineSettings()) {
        canvas.drawString("[default AI config loaded]", 10, 84);
    }

    int stepNum = (int)setupStep + 1;
    int totalSteps = 7;
    char progress[16];
    snprintf(progress, sizeof(progress), "Step %d/%d", stepNum, totalSteps);
    canvas.drawString(progress, SCREEN_W - 60, 4);

    canvas.pushSprite(0, 0);
}

void handleSetupKey(char key, bool enter, bool backspace, bool tab) {
    if (tab) {
        if (WiFi.status() != WL_CONNECTED) offlineMode = true;
        Config::save();
        enterCompanionMode();
        return;
    }

    if (backspace && setupInput.length() > 0) {
        setupInput.remove(setupInput.length() - 1);
        return;
    }

    if (key && !enter) {
        setupInput += key;
        return;
    }

    if (!enter) return;

    switch (setupStep) {
        case SetupStep::SSID:
            if (setupInput.length() > 0) Config::setSSID(setupInput);
            if (Config::getSSID().length() == 0) break;
            setupInput = ""; setupStep = SetupStep::PASSWORD; break;
        case SetupStep::PASSWORD:
            if (setupInput.length() > 0) Config::setPassword(setupInput);
            setupInput = "";
            if (hasPreconfiguredOnlineSettings()) {
                if (Config::getCity().length() == 0) Config::setCity("Beijing");
                Config::save();
                setupStep = SetupStep::CONNECTING;
                connectWiFi();
            } else {
                setupStep = SetupStep::LLM_KEY;
            }
            break;
        case SetupStep::LLM_KEY:
            if (setupInput.length() > 0) Config::setLlmApiKey(setupInput);
            setupInput = ""; setupStep = SetupStep::LLM_PROVIDER; break;
        case SetupStep::LLM_PROVIDER:
            if (setupInput.length() > 0) Config::setLlmProvider(setupInput);
            if (Config::getLlmProvider().length() == 0) Config::setLlmProvider("anthropic");
            setupInput = ""; setupStep = SetupStep::LLM_MODEL; break;
        case SetupStep::LLM_MODEL:
            if (setupInput.length() > 0) Config::setLlmModel(setupInput);
            if (Config::getLlmModel().length() == 0) Config::setLlmModel(M5CLAW_LLM_DEFAULT_MODEL);
            setupInput = ""; setupStep = SetupStep::DASHSCOPE_KEY; break;
        case SetupStep::DASHSCOPE_KEY:
            if (setupInput.length() > 0) Config::setDashScopeKey(setupInput);
            setupInput = ""; setupStep = SetupStep::CITY; break;
        case SetupStep::CITY:
            if (setupInput.length() > 0) Config::setCity(setupInput);
            if (Config::getCity().length() == 0) Config::setCity("Beijing");
            Config::save();
            setupInput = "";
            setupStep = SetupStep::CONNECTING;
            connectWiFi();
            break;
        default: break;
    }
}

bool tryConnect(const String& ssid, const String& pass) {
    Serial.printf("[WIFI] Trying %s...\n", ssid.c_str());
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(ssid.c_str(), pass.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
        canvas.fillScreen(Color::BG_DAY);
        canvas.setTextColor(Color::CLOCK_TEXT);
        canvas.setTextSize(1);
        char msg[64];
        static const char* dots[] = {".", "..", "...", "...."};
        snprintf(msg, sizeof(msg), "Connecting to %s%s", ssid.c_str(), dots[attempts % 4]);
        if (strlen(msg) > 38) msg[38] = '\0';
        canvas.drawString(msg, 10, 55);
        canvas.pushSprite(0, 0);
    }

    bool ok = (WiFi.status() == WL_CONNECTED);
    Serial.printf("[WIFI] %s: %s\n", ssid.c_str(), ok ? "OK" : "FAILED");
    return ok;
}

void connectWiFi() {
    offlineMode = false;

    while (true) {
        bool connected = tryConnect(Config::getSSID(), Config::getPassword());

        if (!connected && Config::getSSID2().length() > 0) {
            connected = tryConnect(Config::getSSID2(), Config::getPassword2());
        }

        if (connected) {
            initOnlineServices();
            enterCompanionMode();
            return;
        }

        canvas.fillScreen(Color::BG_DAY);
        canvas.setTextColor(rgb565(220, 80, 80));
        canvas.setTextSize(1);
        canvas.drawString("WiFi failed!", 80, 20);
        canvas.setTextColor(Color::CLOCK_TEXT);
        canvas.drawString("[Enter]  Retry", 60, 48);
        canvas.drawString("[Fn+R]   Setup wizard", 60, 63);
        canvas.drawString("[Tab]    Offline mode", 60, 78);
        canvas.pushSprite(0, 0);

        while (true) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
                auto ks = M5Cardputer.Keyboard.keysState();
                if (ks.enter) break;
                if (ks.fn && ks.word.size() > 0 && ks.word[0] == 'r') {
                    Config::reset();
                    Config::save();
                    enterSetupMode();
                    return;
                }
                if (ks.tab) {
                    offlineMode = true;
                    enterCompanionMode();
                    return;
                }
            }
            delay(50);
        }
    }
}

void initOnlineServices() {
    configTime(M5CLAW_GMT_OFFSET_SEC, M5CLAW_DAYLIGHT_OFFSET_SEC, M5CLAW_NTP_SERVER);

    canvas.fillScreen(Color::BG_DAY);
    canvas.setTextColor(Color::CHAT_AI);
    canvas.setTextSize(1);
    canvas.drawString("WiFi connected!", 70, 45);
    canvas.drawString(WiFi.localIP().toString().c_str(), 80, 60);
    canvas.drawString("Initializing...", 75, 80);
    canvas.pushSprite(0, 0);

    llm_client_init(Config::getLlmApiKey().c_str(),
                    Config::getLlmModel().c_str(),
                    Config::getLlmProvider().c_str(),
                    Config::getLlmHost().c_str(),
                    Config::getLlmPath().c_str());

    if (Config::getDashScopeKey().length() > 0) {
        DashScopeSTT::init(Config::getDashScopeKey().c_str());
        DashScopeTTS::init(Config::getDashScopeKey().c_str());
    }

    weatherClient.begin(Config::getCity());
    Agent::start();

    delay(500);
}

void enterCompanionMode() {
    appMode = AppMode::COMPANION;
    companion.begin(canvas);
}

void enterChatMode() {
    appMode = AppMode::CHAT;
    chat.begin(canvas);
}
