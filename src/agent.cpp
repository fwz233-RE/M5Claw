#include "agent.h"
#include "llm_client.h"
#include "tool_registry.h"
#include "context_builder.h"
#include "session_mgr.h"
#include "message_bus.h"
#include "wechat_bot.h"
#include "m5claw_config.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

struct AgentRequest {
    char* text;
    char channel[16];
    char chatId[96];
    char msgId[64];
    AgentResponseCallback callback;
    AgentResponseExCallback exCallback;
    AgentTokenCallback tokenCallback;
};

static QueueHandle_t s_queue = nullptr;
static volatile bool s_busy = false;
static volatile bool s_ready = false;
static volatile bool s_abortRequested = false;

static volatile bool s_extConvReady = false;
static char* s_extConvUser = nullptr;
static char* s_extConvAI = nullptr;
static char s_extConvChannel[16] = {0};

static JsonDocument** s_freeable_messages = nullptr;
static AgentTokenCallback s_activeTokenCb = nullptr;

static void* alloc_prefer_psram(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void* calloc_prefer_psram(size_t count, size_t size) {
    void* p = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(count, size, MALLOC_CAP_8BIT);
    return p;
}

static void llmStreamForwarder(const char* token) {
    if (s_activeTokenCb) s_activeTokenCb(token);
}

static void preSwapToSPIFFS() {
    if (s_freeable_messages && *s_freeable_messages) {
        JsonDocument* doc = *s_freeable_messages;
        File f = SPIFFS.open(M5CLAW_AGENT_SWAP_FILE, "w");
        if (f) {
            serializeJson(*doc, f);
            f.close();
        }
        doc->~JsonDocument();
        heap_caps_free(doc);
        *s_freeable_messages = nullptr;
        Serial.printf("[AGENT] Swapped messages to SPIFFS, heap=%d\n", ESP.getFreeHeap());
    }
}

static bool restoreMessagesFromSwap(JsonDocument** messagesPtr) {
    File f = SPIFFS.open(M5CLAW_AGENT_SWAP_FILE, "r");
    if (!f) return false;
    void* mem = alloc_prefer_psram(sizeof(JsonDocument));
    JsonDocument* doc = mem ? new (mem) JsonDocument : nullptr;
    if (!doc) { f.close(); return false; }
    DeserializationError err = deserializeJson(*doc, f);
    f.close();
    SPIFFS.remove(M5CLAW_AGENT_SWAP_FILE);
    if (err) {
        Serial.printf("[AGENT] Swap file parse error: %s\n", err.c_str());
        doc->~JsonDocument();
        heap_caps_free(doc);
        return false;
    }
    *messagesPtr = doc;
    Serial.printf("[AGENT] Restored messages from SPIFFS, heap=%d\n", ESP.getFreeHeap());
    return true;
}

static char* stripMarkdownBold(const char* text) {
    if (!text) return nullptr;
    size_t len = strlen(text);
    char* out = (char*)malloc(len + 1);
    if (!out) return nullptr;
    size_t j = 0;
    for (size_t i = 0; i < len; ) {
        if (i + 1 < len && text[i] == '*' && text[i + 1] == '*') {
            i += 2;
        } else if (text[i] == '#') {
            i++;
        } else {
            out[j++] = text[i++];
        }
    }
    out[j] = '\0';
    return out;
}

#define TOOL_OUTPUT_SIZE (8 * 1024)

static void processRequest(AgentRequest& req, char* system_prompt, char* tool_output) {
    Serial.printf("[AGENT] Processing (ch=%s): %.200s\n", req.channel, req.text);

    if (s_abortRequested) {
        Serial.println("[AGENT] Aborted before processing");
        if (req.callback) req.callback("[cancelled]");
        if (strcmp(req.channel, M5CLAW_CHAN_LOCAL) != 0) {
            free(s_extConvUser);
            free(s_extConvAI);
            s_extConvUser = strdup(req.text);
            s_extConvAI = strdup("[cancelled]");
            strlcpy(s_extConvChannel, req.channel, sizeof(s_extConvChannel));
            s_extConvReady = true;
        }
        return;
    }

    ContextBuilder::buildSystemPrompt(system_prompt, M5CLAW_CONTEXT_BUF_SIZE);

    const char* sessionId = req.chatId[0] ? req.chatId : "local";
    String histJson = SessionMgr::getHistoryJson(sessionId, M5CLAW_AGENT_MAX_HISTORY);

    void* messagesMem = alloc_prefer_psram(sizeof(JsonDocument));
    JsonDocument* messages = messagesMem ? new (messagesMem) JsonDocument : nullptr;
    if (!messages) {
        if (req.callback) req.callback("Agent out of memory.");
        if (req.exCallback) {
            AgentResponseInfo info = {"Agent out of memory.", req.channel, req.chatId};
            req.exCallback(&info);
        }
        return;
    }

    deserializeJson(*messages, histJson);
    histJson = "";

    JsonObject userMsg = messages->as<JsonArray>().add<JsonObject>();
    userMsg["role"] = "user";
    userMsg["content"] = req.text;

    const char* tools_json = ToolRegistry::getToolsJson();
    char* final_text = nullptr;
    int iteration = 0;
    int retryCount = 0;

    while (iteration < M5CLAW_AGENT_MAX_TOOL_ITER) {
        if (s_abortRequested) {
            Serial.println("[AGENT] Aborted before LLM call");
            break;
        }

        LlmResponse resp;
        memset(&resp, 0, sizeof(resp));
        bool ok = false;

        for (;;) {
            if (s_abortRequested) break;

            if (retryCount > 0) {
                char retryMsg[64];
                snprintf(retryMsg, sizeof(retryMsg),
                         "\xe9\x87\x8d\xe8\xaf\x95\xe4\xb8\xad... (%d/10)", retryCount);
                Serial.printf("[AGENT] %s\n", retryMsg);
                if (req.callback) req.callback(retryMsg);
                
                delay(2000);
            }

            if (messages == nullptr) {
                if (!restoreMessagesFromSwap(&messages)) {
                    void* newMem = alloc_prefer_psram(sizeof(JsonDocument));
                    messages = newMem ? new (newMem) JsonDocument : nullptr;
                    if (!messages) {
                        Serial.println("[AGENT] OOM reconstructing messages for retry");
                        break;
                    }
                    String freshHist = SessionMgr::getHistoryJson(sessionId, M5CLAW_AGENT_MAX_HISTORY);
                    deserializeJson(*messages, freshHist);
                    JsonObject rebuiltUser = messages->as<JsonArray>().add<JsonObject>();
                    rebuiltUser["role"] = "user";
                    rebuiltUser["content"] = req.text;
                }
            }

            const char* iter_tools = (iteration >= M5CLAW_AGENT_MAX_TOOL_ITER - 2)
                                     ? nullptr : tools_json;
            s_freeable_messages = &messages;
            llm_client_set_pre_read_free(preSwapToSPIFFS);
            s_activeTokenCb = req.tokenCallback;
            ok = llm_chat_tools(system_prompt, *messages, iter_tools, &resp,
                                req.tokenCallback ? llmStreamForwarder : nullptr);
            s_activeTokenCb = nullptr;
            llm_client_set_pre_read_free(nullptr);
            s_freeable_messages = nullptr;

            if (ok || s_abortRequested) break;

            retryCount++;
            Serial.printf("[AGENT] LLM failed, retry %d/10\n", retryCount);
            llm_response_free(&resp);
            memset(&resp, 0, sizeof(resp));

            if (retryCount >= 10) break;
        }

        if (s_abortRequested) {
            llm_response_free(&resp);
            Serial.println("[AGENT] Aborted");
            break;
        }

        if (!ok) {
            Serial.println("[AGENT] LLM failed after all retries");
            break;
        }

        if (!resp.tool_use) {
            if (resp.text && resp.text_len > 0) {
                final_text = strdup(resp.text);
            }
            llm_response_free(&resp);
            break;
        }

        Serial.printf("[AGENT] Tool iteration %d: %d calls\n", iteration + 1, resp.call_count);

        if (req.tokenCallback) {
            for (int i = 0; i < resp.call_count; i++) {
                char status[64];
                snprintf(status, sizeof(status), "\n[tool: %s]\n", resp.calls[i].name);
                req.tokenCallback(status);
            }
        }

        if (messages == nullptr) {
            if (!restoreMessagesFromSwap(&messages)) {
                void* newMem = alloc_prefer_psram(sizeof(JsonDocument));
                messages = newMem ? new (newMem) JsonDocument : nullptr;
                if (!messages) {
                    Serial.println("[AGENT] OOM reconstructing messages for tool iteration");
                    llm_response_free(&resp);
                    break;
                }
                String freshHist = SessionMgr::getHistoryJson(sessionId, M5CLAW_AGENT_MAX_HISTORY);
                deserializeJson(*messages, freshHist);
                JsonObject rebuiltUser = messages->as<JsonArray>().add<JsonObject>();
                rebuiltUser["role"] = "user";
                rebuiltUser["content"] = req.text;
            }
        }

        JsonObject asstMsg = messages->as<JsonArray>().add<JsonObject>();
        asstMsg["role"] = "assistant";
        JsonArray asstContent = asstMsg["content"].to<JsonArray>();

        bool copiedRawContent = false;
        if (resp.raw_content_json && resp.raw_content_json[0]) {
            JsonDocument rawContentDoc;
            if (deserializeJson(rawContentDoc, resp.raw_content_json) == DeserializationError::Ok) {
                JsonArray rawContent = rawContentDoc.as<JsonArray>();
                if (!rawContent.isNull()) {
                    for (JsonVariant block : rawContent) asstContent.add(block);
                    copiedRawContent = true;
                }
            }
        }

        if (!copiedRawContent) {
            if (resp.text && resp.text_len > 0) {
                JsonObject tb = asstContent.add<JsonObject>();
                tb["type"] = "text";
                tb["text"] = resp.text;
            }
            for (int i = 0; i < resp.call_count; i++) {
                JsonObject tu = asstContent.add<JsonObject>();
                tu["type"] = "tool_use";
                tu["id"] = resp.calls[i].id;
                tu["name"] = resp.calls[i].name;
                JsonDocument inputDoc;
                deserializeJson(inputDoc, resp.calls[i].input ? resp.calls[i].input : "{}");
                tu["input"] = inputDoc;
            }
        }

        JsonObject resultMsg = messages->as<JsonArray>().add<JsonObject>();
        resultMsg["role"] = "user";
        JsonArray resultContent = resultMsg["content"].to<JsonArray>();

        bool searchDone = false;
        for (int i = 0; i < resp.call_count; i++) {
            tool_output[0] = '\0';
            bool isSearch = strcmp(resp.calls[i].name, "web_search") == 0;
            if (isSearch && searchDone) {
                strlcpy(tool_output,
                        "\xe8\xaf\xb7\xe9\x80\x90\xe4\xb8\xaa\xe6\x90\x9c\xe7\xb4\xa2\xef\xbc\x8c"
                        "\xe6\xaf\x8f\xe6\xac\xa1\xe5\x8f\xaa\xe6\x90\x9c\xe7\xb4\xa2\xe4\xb8\x80"
                        "\xe4\xb8\xaa\xe4\xb8\xbb\xe9\xa2\x98\xe3\x80\x82",
                        TOOL_OUTPUT_SIZE);
            } else {
                ToolRegistry::execute(resp.calls[i].name,
                                      resp.calls[i].input ? resp.calls[i].input : "{}",
                                      tool_output, TOOL_OUTPUT_SIZE);
                if (isSearch) searchDone = true;
            }
            JsonObject tr = resultContent.add<JsonObject>();
            tr["type"] = "tool_result";
            tr["tool_use_id"] = resp.calls[i].id;
            tr["content"] = tool_output;
        }

        llm_response_free(&resp);
        iteration++;
    }

    if (messages) {
        messages->~JsonDocument();
        heap_caps_free(messages);
    }
    SPIFFS.remove(M5CLAW_AGENT_SWAP_FILE);

    bool wasAborted = s_abortRequested;
    if (wasAborted) {
        free(final_text);
        final_text = strdup("[cancelled]");
        Serial.println("[AGENT] Request was aborted");
    }

    const char* responseText = (final_text && final_text[0]) ? final_text : "\xe8\xaf\xb7\xe6\xb1\x82\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x8c\xe8\xaf\xb7\xe9\x87\x8d\xe8\xaf\x95\xe3\x80\x82";

    if (!wasAborted && final_text && final_text[0]) {
        SessionMgr::appendMessage(sessionId, "user", req.text);
        SessionMgr::appendMessage(sessionId, "assistant", final_text);
    }

    if (req.callback) req.callback(responseText);

    if (!wasAborted && req.exCallback) {
        AgentResponseInfo info = {responseText, req.channel, req.chatId};
        req.exCallback(&info);
    }

    bool fromBus = (req.callback == nullptr);
    if (fromBus || strcmp(req.channel, M5CLAW_CHAN_LOCAL) != 0) {
        free(s_extConvUser);
        free(s_extConvAI);
        s_extConvUser = strdup(req.text);
        s_extConvAI = wasAborted ? strdup("[cancelled]") : strdup(responseText);
        strlcpy(s_extConvChannel, req.channel, sizeof(s_extConvChannel));
        s_extConvReady = true;
    }

    free(final_text);
}

static void busResponseHandler(const AgentResponseInfo* info) {
    if (strcmp(info->channel, M5CLAW_CHAN_LOCAL) == 0) return;

    if (strcmp(info->channel, M5CLAW_CHAN_WECHAT) == 0) {
        char* cleaned = stripMarkdownBold(info->text);
        WechatBot::sendMessage(info->chatId, cleaned ? cleaned : info->text);
        free(cleaned);
        return;
    }
}

static void agent_task(void* arg) {
    Serial.printf("[AGENT] Task started on core %d\n", xPortGetCoreID());

    char* system_prompt = (char*)calloc_prefer_psram(1, M5CLAW_CONTEXT_BUF_SIZE);
    char* tool_output = (char*)calloc_prefer_psram(1, TOOL_OUTPUT_SIZE);

    if (!system_prompt || !tool_output) {
        Serial.println("[AGENT] Failed to allocate working buffers");
        free(system_prompt); free(tool_output);
        s_ready = false;
        vTaskDelete(nullptr);
        return;
    }

    llm_client_set_abort_flag(&s_abortRequested);
    s_ready = true;
    Serial.printf("[AGENT] Ready. Free heap=%d psram=%d stackHW=%u\n",
                  ESP.getFreeHeap(),
                  (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));

    while (true) {
        AgentRequest req;
        bool hasReq = false;

        // Check queue first (local messages have priority)
        if (xQueueReceive(s_queue, &req, pdMS_TO_TICKS(100)) == pdTRUE) {
            hasReq = true;
        } else {
            // Check message bus (wechat, cron, heartbeat)
            BusMessage busMsg;
            if (MessageBus::popInbound(&busMsg, 100)) {
                memset(&req, 0, sizeof(req));
                req.text = busMsg.content;
                strlcpy(req.channel, busMsg.channel, sizeof(req.channel));
                strlcpy(req.chatId, busMsg.chat_id, sizeof(req.chatId));
                strlcpy(req.msgId, busMsg.msg_id, sizeof(req.msgId));
                req.callback = nullptr;
                req.exCallback = busResponseHandler;
                hasReq = true;
            }
        }

        if (!hasReq) continue;

        s_busy = true;
        s_abortRequested = false;

        bool isCronNotify = (req.text && strncmp(req.text, "[Cron:", 6) == 0);
        if (isCronNotify && req.callback == nullptr) {
            Serial.printf("[AGENT] Cron notify (ch=%s): %.80s\n", req.channel, req.text);

            if (strcmp(req.channel, M5CLAW_CHAN_WECHAT) == 0 && req.chatId[0]) {
                bool wxa = WechatBot::isRunning();
                if (wxa) WechatBot::stop();
                char* cleaned = stripMarkdownBold(req.text);
                WechatBot::sendMessage(req.chatId, cleaned ? cleaned : req.text);
                free(cleaned);
                if (wxa) WechatBot::resume();
            }

            free(s_extConvUser);
            free(s_extConvAI);
            s_extConvUser = nullptr;
            s_extConvAI = strdup(req.text);
            strlcpy(s_extConvChannel, req.channel, sizeof(s_extConvChannel));
            s_extConvReady = true;

            free(req.text);
            s_busy = false;
            continue;
        }

        bool wechatWasActive = WechatBot::isRunning();
        if (wechatWasActive) {
            WechatBot::stop();
            Serial.printf("[AGENT] WeChat paused, heap=%d\n", ESP.getFreeHeap());
        }

        ToolRegistry::setRequestContext(req.channel, req.chatId);

        bool isWechat = strcmp(req.channel, M5CLAW_CHAN_WECHAT) == 0;
        if (isWechat && req.chatId[0]) {
            WechatBot::sendTyping(req.chatId, 1);
        }

        processRequest(req, system_prompt, tool_output);

        if (isWechat && req.chatId[0]) {
            WechatBot::sendTyping(req.chatId, 2);
        }

        free(req.text);

        if (wechatWasActive) {
            WechatBot::resume();
        }

        s_busy = false;

        Serial.printf("[AGENT] Done. Free heap=%d psram=%d stackHW=%u\n",
                      ESP.getFreeHeap(),
                      (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
}

void Agent::init() {
    s_queue = xQueueCreate(4, sizeof(AgentRequest));
}

void Agent::start() {
    xTaskCreatePinnedToCore(agent_task, "agent", M5CLAW_AGENT_STACK, nullptr,
                            M5CLAW_AGENT_PRIO, nullptr, M5CLAW_AGENT_CORE);
    Serial.println("[AGENT] Started");
}

void Agent::sendMessage(const char* text, AgentResponseCallback onResponse,
                        AgentTokenCallback onToken) {
    if (!s_ready) {
        if (onResponse) onResponse("Agent unavailable: memory init failed.");
        return;
    }
    AgentRequest req;
    memset(&req, 0, sizeof(req));
    req.text = strdup(text);
    strlcpy(req.channel, M5CLAW_CHAN_LOCAL, sizeof(req.channel));
    strlcpy(req.chatId, "local", sizeof(req.chatId));
    req.callback = onResponse;
    req.exCallback = nullptr;
    req.tokenCallback = onToken;
    if (!req.text || xQueueSend(s_queue, &req, 0) != pdTRUE) {
        free(req.text);
        if (onResponse) onResponse("Agent queue full, try again.");
    }
}

bool Agent::isBusy() { return s_busy; }
void Agent::requestAbort() { s_abortRequested = true; }

bool Agent::hasExternalConv() { return s_extConvReady; }

ExternalConv Agent::takeExternalConv() {
    ExternalConv conv = {};
    conv.userText = s_extConvUser;
    conv.aiText = s_extConvAI;
    strlcpy(conv.channel, s_extConvChannel, sizeof(conv.channel));
    s_extConvUser = nullptr;
    s_extConvAI = nullptr;
    s_extConvReady = false;
    return conv;
}
