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
#include "message_bus.h"
#include "wechat_bot.h"
#include "cron_service.h"
#include "heartbeat.h"
#include "skill_loader.h"
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include "soc/rtc_cntl_reg.h"

M5Canvas canvas(&M5Cardputer.Display);
Companion companion;
Chat chat;
WeatherClient weatherClient;

enum class AppMode { SETUP, COMPANION, CHAT, WECHAT_STATUS };
static AppMode appMode = AppMode::SETUP;
static bool offlineMode = false;

enum class SetupStep {
    SSID, PASSWORD, LLM_KEY, LLM_MODEL, CITY, CONNECTING
};
static SetupStep setupStep = SetupStep::SSID;
static String setupInput;

static bool voiceRecording = false;
static unsigned long recordingStartMs = 0;

static int16_t* sttChunkBuf = nullptr;
static bool s_wechatPausedForVoice = false;
static File s_voiceFile;
static size_t s_voicePcmBytes = 0;
static bool s_voiceWriteFailed = false;
static bool s_playTtsForNextLocalReply = false;

void enterSetupMode();
void updateSetupMode();
void handleSetupKey(char key, bool enter, bool backspace, bool tab);
bool tryConnect(const String& ssid, const String& pass);
void connectWiFi();
void initOnlineServices();
void enterCompanionMode();
void enterChatMode();
void startVoiceRecording();
String stopVoiceRecording();
void streamVoiceData();
void processSerialCommands();
void dispatchOutbound();

static void onAgentResponse(const char* text);
static void onAgentToken(const char* token);
static volatile bool agentResponseReady = false;
static char* agentResponseText = nullptr;
static bool s_discardAgentResponse = false;
static bool s_waitingForWechatResponse = false;
static QueueHandle_t s_tokenQueue = nullptr;
static volatile bool s_hasStreamedTokens = false;

static bool hasPreconfiguredOnlineSettings() {
    return Config::getLlmApiKey().length() > 0
        && Config::getLlmModel().length() > 0;
}

static bool isSensitiveNvsKey(const char* key) {
    return strcmp(key, "pass") == 0
        || strcmp(key, "llm_key") == 0
        || strcmp(key, "wc_token") == 0;
}

// ── M5Burner NVS Configure protocol ──────────────────────────
static const char* const NVS_KEYS[] = {
    "ssid", "pass", "llm_key", "llm_model",
    "city", "wc_token", "wc_host"
};

static String nvsGet(const char* key) {
    if (isSensitiveNvsKey(key)) return "";
    if (strcmp(key, "ssid") == 0)      return Config::getSSID();
    if (strcmp(key, "pass") == 0)      return Config::getPassword();
    if (strcmp(key, "llm_key") == 0)   return Config::getLlmApiKey();
    if (strcmp(key, "llm_model") == 0) return Config::getLlmModel();
    if (strcmp(key, "city") == 0)      return Config::getCity();
    if (strcmp(key, "wc_token") == 0)  return Config::getWechatToken();
    if (strcmp(key, "wc_host") == 0)   return Config::getWechatApiHost();
    return "";
}

static void nvsSet(const char* key, const char* value) {
    if (strcmp(key, "ssid") == 0)      Config::setSSID(value);
    else if (strcmp(key, "pass") == 0)      Config::setPassword(value);
    else if (strcmp(key, "llm_key") == 0)   Config::setLlmApiKey(value);
    else if (strcmp(key, "llm_model") == 0) Config::setLlmModel(value);
    else if (strcmp(key, "city") == 0)      Config::setCity(value);
    else if (strcmp(key, "wc_token") == 0)  Config::setWechatToken(value);
    else if (strcmp(key, "wc_host") == 0)   Config::setWechatApiHost(value);
    Config::save();
}

static bool handleBurnerNVS(const String& line) {
    if (!line.startsWith("CMD::")) return false;

    if (line.startsWith("CMD::INIT:")) {
        Serial.println("__NVS_EXIST__");
    } else if (line.startsWith("CMD::LIST:")) {
        String keys;
        for (auto k : NVS_KEYS) { keys += k; keys += '/'; }
        Serial.println(keys);
    } else if (line.startsWith("CMD::GET:")) {
        String key = line.substring(9);
        if (isSensitiveNvsKey(key.c_str())) Serial.println("__PROTECTED__");
        else Serial.println(nvsGet(key.c_str()));
    } else if (line.startsWith("CMD::SET:")) {
        String payload = line.substring(9);
        int eq = payload.indexOf('=');
        if (eq > 0) {
            String key = payload.substring(0, eq);
            String val = payload.substring(eq + 1);
            nvsSet(key.c_str(), val.c_str());
        }
        Serial.println("OK");
    }
    return true;
}

// ── Serial CLI ───────────────────────────────────────────────
void processSerialCommands() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    if (handleBurnerNVS(line)) return;

    int sep = line.indexOf(' ');
    String cmd = (sep > 0) ? line.substring(0, sep) : line;
    String val = (sep > 0) ? line.substring(sep + 1) : "";
    val.trim();

    if (cmd == "help") {
        Serial.println("=== M5Claw Serial Config ===");
        Serial.println("  set_wifi <ssid> <pass>    - Set WiFi");
        Serial.println("  set_mimo_key <key>        - Set Xiaomi MiMo API key");
        Serial.println("  set_mimo_model <model>    - Default: mimo-v2-omni");
        Serial.println("  set_city <city>           - e.g. Beijing");
        Serial.println("  set_wechat <token> <host> - Set WeChat bot credentials");
        Serial.println("  show_config               - Show current config");
        Serial.println("  reset_config              - Clear all config");
        Serial.println("  reboot                    - Restart device");
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
    } else if (cmd == "set_mimo_key" || cmd == "set_llm_key") {
        Config::setLlmApiKey(val); Config::save();
        Serial.println("MiMo key saved");
    } else if (cmd == "set_mimo_model" || cmd == "set_llm_model") {
        Config::setLlmModel(val); Config::save();
        Serial.printf("Model: %s\n", val.c_str());
    } else if (cmd == "set_city") {
        Config::setCity(val); Config::save();
        Serial.printf("City: %s\n", val.c_str());
    } else if (cmd == "set_wechat") {
        int sp = val.indexOf(' ');
        if (sp > 0) {
            Config::setWechatToken(val.substring(0, sp));
            Config::setWechatApiHost(val.substring(sp + 1));
            Config::save();
            Serial.println("WeChat credentials saved. Reboot to activate.");
        } else {
            Serial.println("Usage: set_wechat <bearer_token> <api_host>");
        }
    } else if (cmd == "show_config") {
        Serial.println("=== Current Config ===");
        Serial.printf("  WiFi SSID:     %s\n", Config::getSSID().c_str());
        Serial.printf("  WiFi Pass:     [%d chars]\n", Config::getPassword().length());
        Serial.printf("  MiMo Model:    %s\n", Config::getLlmModel().c_str());
        Serial.printf("  MiMo Key:      [%d chars]\n", Config::getLlmApiKey().length());
        Serial.printf("  City:          %s\n", Config::getCity().c_str());
        Serial.printf("  WeChat Token:  [%d chars]\n", Config::getWechatToken().length());
        Serial.printf("  WeChat Host:   %s\n", Config::getWechatApiHost().c_str());
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

void dispatchOutbound() {
    if (WiFi.status() != WL_CONNECTED) return;
    BusMessage msg;
    while (MessageBus::popOutbound(&msg, 0)) {
        if (strcmp(msg.channel, M5CLAW_CHAN_WECHAT) == 0) {
            if (WechatBot::isPaired()) {
                WechatBot::sendMessage(msg.chat_id, msg.content);
            } else {
                Serial.printf("[BUS] WeChat offline, dropped reply to %s\n", msg.chat_id);
            }
        }
        free(msg.content);
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

    Config::load();
    MemoryStore::init();
    Config::importBootstrapFile();
    Config::applyDefaults();
    Config::save();
    Serial.println("[BOOT] Config loaded");
    Serial.println("[BOOT] Type 'help' in serial monitor for config commands");
    Serial.println("[BOOT] SPIFFS OK");

    MessageBus::init();
    SessionMgr::init();
    ToolRegistry::init();
    SkillLoader::init();
    CronService::init();
    Heartbeat::init();
    WechatBot::init();
    Agent::init();
    s_tokenQueue = xQueueCreate(32, sizeof(char*));
    Serial.println("[BOOT] All services initialized");

    playBootAnimation(canvas);

    if (Config::isValid()) {
        connectWiFi();
    } else {
        enterSetupMode();
    }
}

void loop() {
    M5Cardputer.update();

    // Drain streaming tokens from agent → chat UI
    if (s_tokenQueue) {
        char* token;
        while (xQueueReceive(s_tokenQueue, &token, 0) == pdTRUE) {
            if (!s_discardAgentResponse) {
                chat.appendAIToken(token);
            }
            free(token);
        }
    }

    // WeChat incoming → show one at a time, queue the rest
    if (WechatBot::hasIncomingForDisplay() && !s_waitingForWechatResponse) {
        char* incoming = WechatBot::takeIncomingForDisplay();
        if (incoming) {
            if (appMode != AppMode::CHAT) {
                appMode = AppMode::CHAT;
                chat.begin(canvas);
            }
            String label = "[wechat] ";
            label += incoming;
            chat.addMessage(label, true);
            chat.addMessage("thinking...", false);
            chat.scrollToBottom();
            s_waitingForWechatResponse = true;
            free(incoming);
        }
    }

    // Local agent response (from keyboard/voice)
    if (agentResponseReady && agentResponseText) {
        if (s_discardAgentResponse) {
            Serial.println("[MAIN] Discarding cancelled agent response");
            s_discardAgentResponse = false;
        } else {
            if (!s_hasStreamedTokens) {
                chat.appendAIToken(agentResponseText);
            }
            chat.onAIResponseComplete();
            if (s_playTtsForNextLocalReply) {
                llm_speak_text(agentResponseText);
            } else {
                companion.triggerIdle();
            }
        }
        free(agentResponseText);
        agentResponseText = nullptr;
        agentResponseReady = false;
        s_hasStreamedTokens = false;
        s_playTtsForNextLocalReply = false;
    }

    // External AI response (wechat reply / cron notification / heartbeat)
    if (Agent::hasExternalConv()) {
        ExternalConv conv = Agent::takeExternalConv();
        if (s_discardAgentResponse) {
            Serial.println("[MAIN] Discarding cancelled external conv");
            s_discardAgentResponse = false;
        } else if (conv.aiText) {
            if (appMode != AppMode::CHAT) {
                appMode = AppMode::CHAT;
                chat.begin(canvas);
            }
            if (s_waitingForWechatResponse) {
                chat.appendAIToken(conv.aiText);
                chat.onAIResponseComplete();
            } else if (conv.userText) {
                String label = "[";
                label += conv.channel;
                label += "] ";
                label += conv.userText;
                chat.addMessage(label, true);
                chat.addMessage(conv.aiText, false);
                chat.scrollToBottom();
            } else {
                chat.addMessage(conv.aiText, false);
                chat.scrollToBottom();
            }
        }
        s_waitingForWechatResponse = false;
        free(conv.userText);
        free(conv.aiText);
    }

    dispatchOutbound();

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

            static bool prevFn = false;
            static bool fnComboUsed = false;

            bool fnDown = ks.fn && !prevFn;
            bool fnUp   = !ks.fn && prevFn;

            if (fnDown) fnComboUsed = false;

            if (keyPressed) {
                if (ks.tab) {
                    prevFn = ks.fn;
                    playTransition(canvas, true);
                    enterChatMode();
                    break;
                }
                if (ks.ctrl) {
                    prevFn = ks.fn;
                    playTransitionVertical(canvas, true);
                    appMode = AppMode::WECHAT_STATUS;
                    break;
                }
                if (ks.fn && ks.word.size() > 0) {
                    fnComboUsed = true;
                    if (ks.word[0] == 'r') {
                        WiFi.disconnect(true);
                        Config::setSSID("");
                        Config::setPassword("");
                        Config::save();
                        prevFn = ks.fn;
                        enterSetupMode();
                        break;
                    }
                }
                if (!ks.fn) Companion::playKeyClick();
            }

            if (fnUp && !fnComboUsed) {
                companion.cycleSunset();
                Companion::playKeyClick();
            }

            prevFn = ks.fn;

            if (!offlineMode) weatherClient.update();
            companion.setWeather(weatherClient.getData());
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
                && !Agent::isBusy()) {
                startVoiceRecording();
            }
            if (fnUp && voiceRecording) {
                String audioPath = stopVoiceRecording();
                if (!offlineMode && audioPath.length() > 0) {
                    chat.addMessage("[voice]", true);
                    chat.addMessage("thinking...", false);
                    s_hasStreamedTokens = false;
                    s_playTtsForNextLocalReply = true;
                    Agent::sendVoiceMessage(audioPath.c_str(), "audio/wav", onAgentResponse, onAgentToken);
                } else if (audioPath.length() > 0) {
                    SPIFFS.remove(audioPath.c_str());
                }
                didBlock = true;
            }

            if (!didBlock && voiceRecording) {
                streamVoiceData();
                if (millis() - recordingStartMs >= M5CLAW_AUDIO_MAX_SECONDS * 1000UL) {
                    String audioPath = stopVoiceRecording();
                    if (!offlineMode && audioPath.length() > 0) {
                        chat.addMessage("[voice]", true);
                        chat.addMessage("thinking...", false);
                        s_hasStreamedTokens = false;
                        s_playTtsForNextLocalReply = true;
                        Agent::sendVoiceMessage(audioPath.c_str(), "audio/wav", onAgentResponse, onAgentToken);
                    } else if (audioPath.length() > 0) {
                        SPIFFS.remove(audioPath.c_str());
                    }
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
                // Fn+C: cancel ongoing agent generation
                if (ks.fn && charDown && curWordChar == 'c'
                    && (Agent::isBusy() || chat.isWaitingForAI())) {
                    Serial.println("[MAIN] Fn+C: cancelling generation");
                    Agent::requestAbort();
                    chat.cancelWaiting();
                    s_discardAgentResponse = true;
                    if (s_waitingForWechatResponse) {
                        s_waitingForWechatResponse = false;
                    }
                } else if (altDown) {
                    playTransition(canvas, false);
                    enterCompanionMode();
                    break;
                } else if (tabDown) {
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

                    s_hasStreamedTokens = false;
                    s_playTtsForNextLocalReply = false;
                    Agent::sendMessage(msg.c_str(), onAgentResponse, onAgentToken);

                    M5Cardputer.update();
                    ks = M5Cardputer.Keyboard.keysState();
                    pFn = ks.fn; pEnter = ks.enter; pDel = ks.del; pTab = ks.tab;
                    pAlt = ks.alt; pCtrl = ks.ctrl;
                    pWordChar = (ks.word.size() > 0) ? ks.word[0] : 0;
                    enterDown = delDown = tabDown = altDown = ctrlDown = charDown = false;
                }
            }

            chat.update(canvas);
            if (voiceRecording) {
                float dur = (float)(millis() - recordingStartMs) / 1000.0f;
                canvas.fillRect(0, SCREEN_H - 16, SCREEN_W, 16, rgb565(200, 50, 50));
                canvas.setTextColor(Color::WHITE);
                canvas.setTextSize(1);
                char recLabel[40];
                snprintf(recLabel, sizeof(recLabel), "Recording... %.1fs", dur);
                canvas.drawString(recLabel, 62, SCREEN_H - 12);
            } else if (Agent::isBusy()) {
                canvas.fillRect(0, SCREEN_H - 16, SCREEN_W, 16, rgb565(80, 80, 80));
                canvas.setTextColor(rgb565(200, 200, 200));
                canvas.setTextSize(1);
                canvas.drawString("Processing...", 78, SCREEN_H - 12);
            }
            canvas.pushSprite(0, 0);
            break;
        }

        case AppMode::WECHAT_STATUS: {
            static bool pairStarted = false;
            static unsigned long lastPollMs = 0;
            static const char* qrContent = nullptr;

            auto ks = M5Cardputer.Keyboard.keysState();
            bool keyPressed = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();

            if (keyPressed) {
                if (ks.tab) {
                    WechatBot::cancelPairing();
                    pairStarted = false;
                    qrContent = nullptr;
                    playTransitionVertical(canvas, false);
                    enterCompanionMode();
                    break;
                }
                if (!WechatBot::isPaired() && ks.enter) {
                    pairStarted = false;
                    qrContent = nullptr;
                }
            }

            if (!WechatBot::isPaired() && !pairStarted) {
                qrContent = WechatBot::startPairing();
                pairStarted = true;
                lastPollMs = millis();
            }

            if (WechatBot::getPairState() == WechatBot::PAIR_WAITING
                && millis() - lastPollMs > 3000) {
                if (WechatBot::pollPairing()) {
                    WechatBot::start();
                    pairStarted = false;
                    qrContent = nullptr;
                }
                lastPollMs = millis();
            }

            canvas.fillScreen(Color::BG_DAY);
            canvas.setTextSize(1);

            constexpr int BAR_H = 16;
            canvas.fillRect(0, 0, SCREEN_W, BAR_H, Color::INPUT_BG);
            canvas.drawFastHLine(0, BAR_H - 1, SCREEN_W, Color::GROUND_TOP);
            canvas.setTextColor(Color::STATUS_DIM);
            canvas.drawString("[Tab]back", 3, 2);
            if (!WechatBot::isPaired() && WechatBot::getPairState() != WechatBot::PAIR_IDLE) {
                canvas.drawString("[Enter]retry", SCREEN_W - 78, 2);
            }

            int cy = BAR_H + 2;

            if (WechatBot::isPaired()) {
                constexpr int LINES = 5;
                constexpr int LH = 16;
                int contentH = LINES * LH;
                int areaH = SCREEN_H - BAR_H;
                cy = BAR_H + (areaH - contentH) / 2;
                int cx = SCREEN_W / 2;
                canvas.setTextDatum(TC_DATUM);

                canvas.setTextColor(rgb565(60, 200, 80));
                canvas.fillCircle(cx - 30, cy + 5, 4, rgb565(60, 200, 80));
                canvas.drawString("Online", cx + 4, cy);
                cy += LH;

                canvas.setTextColor(Color::CLOCK_TEXT);
                char cronBuf[24];
                snprintf(cronBuf, sizeof(cronBuf), "Cron  %d jobs", CronService::getJobCount());
                canvas.drawString(cronBuf, cx, cy);
                cy += LH;

                unsigned long hbRemain = Heartbeat::getRemainingMs();
                int hbMin = hbRemain / 60000;
                int hbSec = (hbRemain % 60000) / 1000;
                char hbBuf[24];
                snprintf(hbBuf, sizeof(hbBuf), "Heartbeat  %02d:%02d", hbMin, hbSec);
                canvas.drawString(hbBuf, cx, cy);
                cy += LH;

                canvas.setTextColor(Color::STATUS_DIM);
                canvas.drawString(WechatBot::getApiHost(), cx, cy);
                cy += LH;
                canvas.drawString(WiFi.localIP().toString().c_str(), cx, cy);

                canvas.setTextDatum(TL_DATUM);

            } else if (qrContent && qrContent[0]) {
                constexpr int QR_SZ = SCREEN_H - BAR_H - 6;
                constexpr int QR_X = 4;
                int qrY = BAR_H + 3;
                canvas.qrcode(qrContent, QR_X, qrY, QR_SZ, 6);

                int tx = QR_X + QR_SZ + 8;
                int qrMidY = qrY + QR_SZ / 2;

                canvas.setTextColor(Color::CLOCK_TEXT);
                canvas.drawString("Scan with", tx, qrMidY - 20);
                canvas.drawString("WeChat", tx, qrMidY - 6);

                canvas.setTextColor(Color::STATUS_DIM);
                canvas.drawString("Waiting...", tx, qrMidY + 14);
            } else {
                canvas.setTextColor(rgb565(200, 80, 60));
                canvas.drawString("Pairing failed", 70, cy + 10);

                canvas.setTextColor(Color::CLOCK_TEXT);
                canvas.drawString("Serial:", 30, cy + 34);
                canvas.setTextColor(rgb565(120, 180, 220));
                canvas.drawString("set_wechat <tok> <host>", 30, cy + 50);
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

static void onAgentToken(const char* token) {
    if (!s_tokenQueue) return;
    char* copy = strdup(token);
    if (copy) {
        s_hasStreamedTokens = true;
        if (xQueueSend(s_tokenQueue, &copy, 0) != pdTRUE) {
            free(copy);
        }
    }
}

static void writeWavHeader(File& f, uint32_t pcmBytes, uint32_t sampleRate) {
    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    const uint16_t blockAlign = channels * bitsPerSample / 8;
    const uint32_t riffSize = 36 + pcmBytes;

    f.seek(0);
    f.write((const uint8_t*)"RIFF", 4);
    f.write((const uint8_t*)&riffSize, 4);
    f.write((const uint8_t*)"WAVEfmt ", 8);
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;
    f.write((const uint8_t*)&fmtSize, 4);
    f.write((const uint8_t*)&audioFormat, 2);
    f.write((const uint8_t*)&channels, 2);
    f.write((const uint8_t*)&sampleRate, 4);
    f.write((const uint8_t*)&byteRate, 4);
    f.write((const uint8_t*)&blockAlign, 2);
    f.write((const uint8_t*)&bitsPerSample, 2);
    f.write((const uint8_t*)"data", 4);
    f.write((const uint8_t*)&pcmBytes, 4);
}

static bool writeVoiceBytes(const void* data, size_t len) {
    if (!s_voiceFile || !data || len == 0) return false;
    size_t written = s_voiceFile.write((const uint8_t*)data, len);
    if (written != len) {
        s_voiceWriteFailed = true;
        Serial.printf("[VOICE] File write short: %u/%u bytes, spiffs used=%u total=%u\n",
                      (unsigned)written, (unsigned)len,
                      (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
    }
    s_voicePcmBytes += written;
    return written == len;
}

void startVoiceRecording() {
    if (WechatBot::isRunning()) {
        WechatBot::requestPause();
        s_wechatPausedForVoice = true;
        Serial.printf("[VOICE] WeChat paused for audio capture, heap=%d\n", ESP.getFreeHeap());
    }

    M5Cardputer.Speaker.stop();

    if (!sttChunkBuf) {
        sttChunkBuf = (int16_t*)malloc(M5CLAW_AUDIO_CHUNK_SAMPLES * sizeof(int16_t));
    }
    if (!sttChunkBuf) {
        Serial.println("[VOICE] Failed to alloc chunk buffer");
        if (s_wechatPausedForVoice) { WechatBot::resume(); s_wechatPausedForVoice = false; }
        return;
    }

    auto micCfg = M5Cardputer.Mic.config();
    micCfg.sample_rate = M5CLAW_AUDIO_RECORD_SAMPLE_RATE;
    micCfg.magnification = 64;
    micCfg.noise_filter_level = 64;
    micCfg.task_priority = 1;
    M5Cardputer.Mic.config(micCfg);
    M5Cardputer.Mic.begin();

    if (s_voiceFile) s_voiceFile.close();
    SPIFFS.remove(M5CLAW_AUDIO_TEMP_FILE);
    s_voiceFile = SPIFFS.open(M5CLAW_AUDIO_TEMP_FILE, "w");
    if (!s_voiceFile) {
        Serial.println("[VOICE] Failed to open temp wav file");
        M5Cardputer.Mic.end();
        free(sttChunkBuf); sttChunkBuf = nullptr;
        if (s_wechatPausedForVoice) { WechatBot::resume(); s_wechatPausedForVoice = false; }
        return;
    }
    uint8_t blankHeader[44] = {0};
    s_voiceWriteFailed = false;
    if (s_voiceFile.write(blankHeader, sizeof(blankHeader)) != sizeof(blankHeader)) {
        s_voiceWriteFailed = true;
        Serial.printf("[VOICE] Header write failed, spiffs used=%u total=%u\n",
                      (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
    }
    s_voicePcmBytes = 0;

    chat.update(canvas);
    canvas.fillRect(0, SCREEN_H - 16, SCREEN_W, 16, rgb565(50, 50, 200));
    canvas.setTextColor(Color::WHITE);
    canvas.setTextSize(1);
    canvas.drawString("Recording...", 82, SCREEN_H - 12);
    canvas.pushSprite(0, 0);

    memset(sttChunkBuf, 0, M5CLAW_AUDIO_CHUNK_SAMPLES * sizeof(int16_t));
    M5Cardputer.Mic.record(sttChunkBuf, M5CLAW_AUDIO_CHUNK_SAMPLES, M5CLAW_AUDIO_RECORD_SAMPLE_RATE);
    voiceRecording = true;
    recordingStartMs = millis();
    Serial.printf("[VOICE] Recording started, heap=%d\n", ESP.getFreeHeap());
}

void streamVoiceData() {
    if (!voiceRecording || !sttChunkBuf || !s_voiceFile) return;

    if (!M5Cardputer.Mic.isRecording()) {
        size_t chunkBytes = M5CLAW_AUDIO_CHUNK_SAMPLES * sizeof(int16_t);
        writeVoiceBytes(sttChunkBuf, chunkBytes);
        memset(sttChunkBuf, 0, chunkBytes);
        M5Cardputer.Mic.record(sttChunkBuf, M5CLAW_AUDIO_CHUNK_SAMPLES, M5CLAW_AUDIO_RECORD_SAMPLE_RATE);
    }
}

String stopVoiceRecording() {
    if (!voiceRecording) return "";
    voiceRecording = false;

    M5Cardputer.Mic.end();

    if (sttChunkBuf && s_voiceFile) {
        size_t chunkBytes = M5CLAW_AUDIO_CHUNK_SAMPLES * sizeof(int16_t);
        writeVoiceBytes(sttChunkBuf, chunkBytes);
        writeWavHeader(s_voiceFile, s_voicePcmBytes, M5CLAW_AUDIO_RECORD_SAMPLE_RATE);
        s_voiceFile.flush();
        s_voiceFile.close();
    }

    free(sttChunkBuf);
    sttChunkBuf = nullptr;

    if (s_wechatPausedForVoice) {
        WechatBot::resume();
        s_wechatPausedForVoice = false;
        Serial.printf("[VOICE] WeChat resumed, heap=%d\n", ESP.getFreeHeap());
    }

    float duration = (float)(millis() - recordingStartMs) / 1000.0f;
    if (duration < 0.3f || s_voicePcmBytes == 0) {
        SPIFFS.remove(M5CLAW_AUDIO_TEMP_FILE);
        Serial.printf("[VOICE] Discarded short recording %.2fs\n", duration);
        return "";
    }

    size_t savedFileBytes = 0;
    File verify = SPIFFS.open(M5CLAW_AUDIO_TEMP_FILE, "r");
    if (verify) {
        savedFileBytes = verify.size();
        verify.close();
    }

    Serial.printf("[VOICE] Saved %.2fs audio to %s (%u bytes pcm, file=%u, writeFailed=%d)\n",
                  duration, M5CLAW_AUDIO_TEMP_FILE, (unsigned)s_voicePcmBytes,
                  (unsigned)savedFileBytes, s_voiceWriteFailed);

    if (s_voiceWriteFailed || savedFileBytes <= 44 || savedFileBytes != s_voicePcmBytes + 44) {
        Serial.printf("[VOICE] Invalid temp wav, expected=%u actual=%u\n",
                      (unsigned)(s_voicePcmBytes + 44), (unsigned)savedFileBytes);
        SPIFFS.remove(M5CLAW_AUDIO_TEMP_FILE);
        return "";
    }

    return M5CLAW_AUDIO_TEMP_FILE;
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
        case SetupStep::LLM_KEY:     label = "MiMo API Key:";  currentVal = Config::getLlmApiKey();    isPass = true; break;
        case SetupStep::LLM_MODEL:   label = "MiMo Model:";    currentVal = Config::getLlmModel();     break;
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
    bool wifiOnly = hasPreconfiguredOnlineSettings();
    if (wifiOnly) {
        canvas.drawString("[Enter] confirm  [Tab] skip", 10, 70);
        canvas.drawString("AI config already set", 10, 84);
    } else {
        canvas.drawString("[Enter] confirm  [Tab] skip/cancel", 10, 70);
    }

    int stepNum, totalSteps;
    if (wifiOnly) {
        stepNum = (setupStep == SetupStep::SSID) ? 1 : 2;
        totalSteps = 2;
    } else {
        stepNum = (int)setupStep + 1;
        totalSteps = 5;
    }
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
            setupInput = ""; setupStep = SetupStep::LLM_MODEL; break;
        case SetupStep::LLM_MODEL:
            if (setupInput.length() > 0) Config::setLlmModel(setupInput);
            if (Config::getLlmModel().length() == 0) Config::setLlmModel(M5CLAW_LLM_DEFAULT_MODEL);
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
        canvas.drawString("[Fn+R]   Change WiFi", 60, 63);
        canvas.drawString("[Tab]    Offline mode", 60, 78);
        canvas.pushSprite(0, 0);

        while (true) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
                auto ks = M5Cardputer.Keyboard.keysState();
                if (ks.enter) break;
                if (ks.fn && ks.word.size() > 0 && ks.word[0] == 'r') {
                    Config::setSSID("");
                    Config::setPassword("");
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
    canvas.drawString("WiFi connected!", 70, 40);
    canvas.drawString(WiFi.localIP().toString().c_str(), 80, 55);
    canvas.drawString("Initializing services...", 55, 75);
    canvas.pushSprite(0, 0);

    llm_client_init(Config::getLlmApiKey().c_str(),
                    Config::getLlmModel().c_str(),
                    nullptr, nullptr, nullptr);

    weatherClient.begin(Config::getCity());
    Agent::start();

    // Start WeChat bot (lazy: only if credentials configured)
    WechatBot::start();

    // Start background services
    CronService::start();
    Heartbeat::start();

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
