#include "agent.h"
#include "llm_client.h"
#include "tool_registry.h"
#include "context_builder.h"
#include "session_mgr.h"
#include "message_bus.h"
#include "media_utils.h"
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
    char mediaPath[96];
    char mediaMime[32];
    uint8_t mediaKind;
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

static const char* kMediaPlaceholderPrefix = "__M5CLAW_MEDIA|";
static const char* kMediaPlaceholderSuffix = "__";

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
        bool swapped = false;
        if (f) {
            size_t written = serializeJson(*doc, f);
            f.close();
            swapped = written > 0;
        }
        if (!swapped) {
            Serial.println("[AGENT] Failed to swap messages to SPIFFS");
            SPIFFS.remove(M5CLAW_AGENT_SWAP_FILE);
            return;
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

static bool buildMediaPlaceholder(const char* path, const char* mimeType,
                                  char* out, size_t outSize) {
    if (!path || !path[0] || !out || outSize == 0) return false;
    const char* mime = (mimeType && mimeType[0]) ? mimeType : MediaUtils::guessMimeType(path);
    int written = snprintf(out, outSize, "%s%s|%s%s",
                           kMediaPlaceholderPrefix, mime, path, kMediaPlaceholderSuffix);
    return written > 0 && (size_t)written < outSize;
}

static String summarizeRequest(const AgentRequest& req) {
    String summary = (req.text && req.text[0]) ? String(req.text) : String();
    if (req.mediaKind == BUS_MEDIA_AUDIO) {
        if (summary.length() > 0) summary += " ";
        summary += "[voice]";
    } else if (req.mediaKind == BUS_MEDIA_IMAGE) {
        if (summary.length() > 0) summary += " ";
        summary += "[image]";
    }
    if (summary.length() == 0) summary = "[input]";
    return summary;
}

static bool appendRequestUserMessage(JsonDocument& messages, const AgentRequest& req) {
    JsonObject userMsg = messages.as<JsonArray>().add<JsonObject>();
    userMsg["role"] = "user";

    if (req.mediaKind == BUS_MEDIA_NONE) {
        userMsg["content"] = (req.text && req.text[0]) ? req.text : "";
        return true;
    }

    JsonArray content = userMsg["content"].to<JsonArray>();
    if (req.text && req.text[0]) {
        JsonObject textPart = content.add<JsonObject>();
        textPart["type"] = "text";
        textPart["text"] = req.text;
    }

    char mediaPlaceholder[196];
    if (!buildMediaPlaceholder(req.mediaPath, req.mediaMime,
                               mediaPlaceholder, sizeof(mediaPlaceholder))) {
        Serial.printf("[AGENT] Failed to build media placeholder for %s\n", req.mediaPath);
        return false;
    }

    if (req.mediaKind == BUS_MEDIA_AUDIO) {
        JsonObject audioPart = content.add<JsonObject>();
        audioPart["type"] = "input_audio";
        JsonObject inputAudio = audioPart["input_audio"].to<JsonObject>();
        inputAudio["data"] = mediaPlaceholder;
    } else if (req.mediaKind == BUS_MEDIA_IMAGE) {
        JsonObject imagePart = content.add<JsonObject>();
        imagePart["type"] = "image_url";
        JsonObject imageUrl = imagePart["image_url"].to<JsonObject>();
        imageUrl["url"] = mediaPlaceholder;
    }
    return content.size() > 0;
}

static bool buildInitialMessages(const char* sessionId, const AgentRequest& req, JsonDocument** outMessages) {
    *outMessages = nullptr;
    String histJson = SessionMgr::getHistoryJson(sessionId, M5CLAW_AGENT_MAX_HISTORY);
    void* messagesMem = alloc_prefer_psram(sizeof(JsonDocument));
    JsonDocument* messages = messagesMem ? new (messagesMem) JsonDocument : nullptr;
    if (!messages) return false;
    if (deserializeJson(*messages, histJson) != DeserializationError::Ok || !messages->is<JsonArray>()) {
        messages->clear();
        messages->to<JsonArray>();
    }
    histJson = "";
    if (!appendRequestUserMessage(*messages, req)) {
        messages->~JsonDocument();
        heap_caps_free(messages);
        return false;
    }
    *outMessages = messages;
    return true;
}

#define TOOL_OUTPUT_SIZE (8 * 1024)

static void processRequest(AgentRequest& req, char* system_prompt, char* tool_output) {
    String reqSummary = summarizeRequest(req);
    Serial.printf("[AGENT] Processing (ch=%s): %.200s\n", req.channel, reqSummary.c_str());

    if (s_abortRequested) {
        Serial.println("[AGENT] Aborted before processing");
        if (req.callback) req.callback("[cancelled]");
        if (strcmp(req.channel, M5CLAW_CHAN_LOCAL) != 0) {
            free(s_extConvUser);
            free(s_extConvAI);
            s_extConvUser = strdup(reqSummary.c_str());
            s_extConvAI = strdup("[cancelled]");
            strlcpy(s_extConvChannel, req.channel, sizeof(s_extConvChannel));
            s_extConvReady = true;
        }
        return;
    }

    ContextBuilder::buildSystemPrompt(system_prompt, M5CLAW_CONTEXT_BUF_SIZE);

    const char* sessionId = req.chatId[0] ? req.chatId : "local";
    JsonDocument* messages = nullptr;
    if (!buildInitialMessages(sessionId, req, &messages)) {
        const char* errText = "Failed to prepare MiMo request.";
        if (req.callback) req.callback(errText);
        if (req.exCallback) {
            AgentResponseInfo info = {errText, req.channel, req.chatId};
            req.exCallback(&info);
        }
        return;
    }

    const char* tools_json = ToolRegistry::getToolsJson();
    char* final_text = nullptr;
    int iteration = 0;

    while (iteration < M5CLAW_AGENT_MAX_TOOL_ITER) {
        if (s_abortRequested) {
            Serial.println("[AGENT] Aborted before LLM call");
            break;
        }

        LlmResponse resp;
        memset(&resp, 0, sizeof(resp));
        bool ok = false;
        int retryCount = 0;

        for (;;) {
            if (s_abortRequested) break;

            if (retryCount > 0) {
                Serial.printf("[AGENT] Retrying MiMo request (%d/10)\n", retryCount);
                delay(2000);
            }

            if (messages == nullptr) {
                if (!restoreMessagesFromSwap(&messages)) {
                    if (!buildInitialMessages(sessionId, req, &messages)) {
                        Serial.println("[AGENT] OOM reconstructing messages for retry");
                        break;
                    }
                }
            }

            s_freeable_messages = &messages;
            llm_client_set_pre_read_free(preSwapToSPIFFS);
            s_activeTokenCb = req.tokenCallback;
            ok = llm_chat_tools(system_prompt, *messages, tools_json, &resp,
                                req.tokenCallback ? llmStreamForwarder : nullptr);
            s_activeTokenCb = nullptr;
            llm_client_set_pre_read_free(nullptr);
            s_freeable_messages = nullptr;

            if (ok || s_abortRequested) break;

            retryCount++;
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
            Serial.println("[AGENT] MiMo failed after all retries");
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
                if (!buildInitialMessages(sessionId, req, &messages)) {
                    Serial.println("[AGENT] OOM reconstructing messages for tool iteration");
                    llm_response_free(&resp);
                    break;
                }
            }
        }

        JsonObject asstMsg = messages->as<JsonArray>().add<JsonObject>();
        asstMsg["role"] = "assistant";
        if (resp.text && resp.text_len > 0) {
            asstMsg["content"] = resp.text;
        } else {
            asstMsg["content"] = "";
        }
        JsonArray toolCalls = asstMsg["tool_calls"].to<JsonArray>();

        for (int i = 0; i < resp.call_count; i++) {
            JsonObject tc = toolCalls.add<JsonObject>();
            tc["id"] = resp.calls[i].id;
            tc["type"] = "function";
            JsonObject func = tc["function"].to<JsonObject>();
            func["name"] = resp.calls[i].name;
            func["arguments"] = resp.calls[i].input ? resp.calls[i].input : "{}";

            tool_output[0] = '\0';
            ToolRegistry::execute(resp.calls[i].name,
                                  resp.calls[i].input ? resp.calls[i].input : "{}",
                                  tool_output, TOOL_OUTPUT_SIZE);

            JsonObject toolMsg = messages->as<JsonArray>().add<JsonObject>();
            toolMsg["role"] = "tool";
            toolMsg["tool_call_id"] = resp.calls[i].id;
            toolMsg["content"] = tool_output;
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

    const char* responseText = (final_text && final_text[0])
        ? final_text
        : "Request failed, please try again.";

    if (!wasAborted && final_text && final_text[0]) {
        SessionMgr::appendMessage(sessionId, "user", reqSummary.c_str());
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
        s_extConvUser = strdup(reqSummary.c_str());
        s_extConvAI = wasAborted ? strdup("[cancelled]") : strdup(responseText);
        strlcpy(s_extConvChannel, req.channel, sizeof(s_extConvChannel));
        s_extConvReady = true;
    }

    free(final_text);
}

static void busResponseHandler(const AgentResponseInfo* info) {
    if (strcmp(info->channel, M5CLAW_CHAN_LOCAL) == 0) return;

    if (strcmp(info->channel, M5CLAW_CHAN_WECHAT) == 0) {
        BusMessage msg = {};
        strlcpy(msg.channel, M5CLAW_CHAN_WECHAT, sizeof(msg.channel));
        strlcpy(msg.chat_id, info->chatId ? info->chatId : "", sizeof(msg.chat_id));
        msg.content = info->text ? strdup(info->text) : nullptr;

        bool sent = WechatBot::sendMessage(info->chatId, info->text);
        if (!sent && msg.content) {
            Serial.println("[AGENT] Direct WeChat send failed, queueing outbound retry");
            if (!MessageBus::pushOutbound(&msg)) {
                free(msg.content);
            }
        } else {
            free(msg.content);
        }
        return;
    }
}

static void cleanupRequestMedia(const AgentRequest& req) {
    if (req.mediaPath[0]) SPIFFS.remove(req.mediaPath);
}

static void agent_task(void* arg) {
    Serial.printf("[AGENT] Task started on core %d\n", xPortGetCoreID());

    char* system_prompt = (char*)calloc_prefer_psram(1, M5CLAW_CONTEXT_BUF_SIZE);
    char* tool_output = (char*)calloc_prefer_psram(1, TOOL_OUTPUT_SIZE);

    if (!system_prompt || !tool_output) {
        Serial.println("[AGENT] Failed to allocate working buffers");
        free(system_prompt);
        free(tool_output);
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
        AgentRequest req = {};
        bool hasReq = false;

        if (xQueueReceive(s_queue, &req, pdMS_TO_TICKS(100)) == pdTRUE) {
            hasReq = true;
        } else {
            BusMessage busMsg = {};
            if (MessageBus::popInbound(&busMsg, 100)) {
                req.text = busMsg.content;
                strlcpy(req.mediaPath, busMsg.media_path, sizeof(req.mediaPath));
                strlcpy(req.mediaMime, busMsg.media_mime, sizeof(req.mediaMime));
                req.mediaKind = busMsg.media_kind;
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

        bool isCronNotify = (req.mediaKind == BUS_MEDIA_NONE &&
                             req.text && strncmp(req.text, "[Cron:", 6) == 0);
        if (isCronNotify && req.callback == nullptr) {
            Serial.printf("[AGENT] Cron notify (ch=%s): %.80s\n", req.channel, req.text);

            if (strcmp(req.channel, M5CLAW_CHAN_WECHAT) == 0 && req.chatId[0]) {
                bool wxa = WechatBot::isRunning();
                if (wxa) WechatBot::stop();
                bool sent = WechatBot::sendMessage(req.chatId, req.text);
                if (!sent) {
                    BusMessage out = {};
                    strlcpy(out.channel, M5CLAW_CHAN_WECHAT, sizeof(out.channel));
                    strlcpy(out.chat_id, req.chatId, sizeof(out.chat_id));
                    out.content = strdup(req.text ? req.text : "");
                    if (out.content) {
                        Serial.println("[AGENT] Cron WeChat send failed, queueing outbound retry");
                        if (!MessageBus::pushOutbound(&out)) {
                            free(out.content);
                        }
                    }
                }
                if (wxa) WechatBot::resume();
            }

            free(s_extConvUser);
            free(s_extConvAI);
            s_extConvUser = nullptr;
            s_extConvAI = strdup(req.text);
            strlcpy(s_extConvChannel, req.channel, sizeof(s_extConvChannel));
            s_extConvReady = true;

            free(req.text);
            cleanupRequestMedia(req);
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
        cleanupRequestMedia(req);

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
    AgentRequest req = {};
    req.text = text ? strdup(text) : nullptr;
    req.mediaKind = BUS_MEDIA_NONE;
    strlcpy(req.channel, M5CLAW_CHAN_LOCAL, sizeof(req.channel));
    strlcpy(req.chatId, "local", sizeof(req.chatId));
    req.callback = onResponse;
    req.exCallback = nullptr;
    req.tokenCallback = onToken;
    if ((text && !req.text) || xQueueSend(s_queue, &req, 0) != pdTRUE) {
        free(req.text);
        if (onResponse) onResponse("Agent queue full, try again.");
    }
}

void Agent::sendVoiceMessage(const char* audioPath, const char* mimeType,
                             AgentResponseCallback onResponse,
                             AgentTokenCallback onToken) {
    if (!s_ready) {
        if (onResponse) onResponse("Agent unavailable: memory init failed.");
        return;
    }
    AgentRequest req = {};
    req.mediaKind = BUS_MEDIA_AUDIO;
    strlcpy(req.mediaPath, audioPath ? audioPath : "", sizeof(req.mediaPath));
    strlcpy(req.mediaMime, mimeType ? mimeType : "audio/wav", sizeof(req.mediaMime));
    strlcpy(req.channel, M5CLAW_CHAN_LOCAL, sizeof(req.channel));
    strlcpy(req.chatId, "local", sizeof(req.chatId));
    req.callback = onResponse;
    req.exCallback = nullptr;
    req.tokenCallback = onToken;
    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        cleanupRequestMedia(req);
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
