#include "agent.h"
#include "llm_client.h"
#include "tool_registry.h"
#include "context_builder.h"
#include "session_mgr.h"
#include "message_bus.h"
#include "feishu_bot.h"
#include "m5claw_config.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

struct AgentRequest {
    char* text;
    char channel[16];
    char chatId[96];
    AgentResponseCallback callback;
    AgentResponseExCallback exCallback;
};

static QueueHandle_t s_queue = nullptr;
static volatile bool s_busy = false;
static volatile bool s_ready = false;

static volatile bool s_extConvReady = false;
static char* s_extConvUser = nullptr;
static char* s_extConvAI = nullptr;
static char s_extConvChannel[16] = {0};

#define TOOL_OUTPUT_SIZE (2 * 1024)

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

static void processRequest(AgentRequest& req, char* system_prompt, char* tool_output) {
    Serial.printf("[AGENT] Processing (ch=%s): %.60s\n", req.channel, req.text);

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

    JsonObject userMsg = messages->as<JsonArray>().add<JsonObject>();
    userMsg["role"] = "user";
    userMsg["content"] = req.text;

    const char* tools_json = ToolRegistry::getToolsJson();
    char* final_text = nullptr;
    int iteration = 0;

    while (iteration < M5CLAW_AGENT_MAX_TOOL_ITER) {
        LlmResponse resp;
        bool ok = llm_chat_tools(system_prompt, *messages, tools_json, &resp);

        if (!ok) {
            Serial.println("[AGENT] LLM call failed");
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

        for (int i = 0; i < resp.call_count; i++) {
            tool_output[0] = '\0';
            ToolRegistry::execute(resp.calls[i].name,
                                  resp.calls[i].input ? resp.calls[i].input : "{}",
                                  tool_output, TOOL_OUTPUT_SIZE);
            JsonObject tr = resultContent.add<JsonObject>();
            tr["type"] = "tool_result";
            tr["tool_use_id"] = resp.calls[i].id;
            tr["content"] = tool_output;
        }

        llm_response_free(&resp);
        iteration++;
    }

    messages->~JsonDocument();
    heap_caps_free(messages);

    const char* responseText = (final_text && final_text[0]) ? final_text : "Sorry, an error occurred.";

    if (final_text && final_text[0]) {
        SessionMgr::appendMessage(sessionId, "user", req.text);
        SessionMgr::appendMessage(sessionId, "assistant", final_text);
    }

    if (req.callback) req.callback(responseText);
    if (req.exCallback) {
        AgentResponseInfo info = {responseText, req.channel, req.chatId};
        req.exCallback(&info);
    }

    if (strcmp(req.channel, M5CLAW_CHAN_LOCAL) != 0) {
        free(s_extConvUser);
        free(s_extConvAI);
        s_extConvUser = strdup(req.text);
        s_extConvAI = strdup(responseText);
        strlcpy(s_extConvChannel, req.channel, sizeof(s_extConvChannel));
        s_extConvReady = true;
    }

    free(final_text);
}

static void busResponseHandler(const AgentResponseInfo* info) {
    if (strcmp(info->channel, M5CLAW_CHAN_LOCAL) == 0) return;

    if (strcmp(info->channel, M5CLAW_CHAN_FEISHU) == 0) {
        FeishuBot::sendMessage(info->chatId, info->text);
        return;
    }

    BusMessage outMsg = {};
    strlcpy(outMsg.channel, info->channel, sizeof(outMsg.channel));
    strlcpy(outMsg.chat_id, info->chatId, sizeof(outMsg.chat_id));
    outMsg.content = strdup(info->text);
    if (outMsg.content) {
        if (!MessageBus::pushOutbound(&outMsg)) free(outMsg.content);
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
            // Check message bus (feishu, cron, heartbeat)
            BusMessage busMsg;
            if (MessageBus::popInbound(&busMsg, 100)) {
                memset(&req, 0, sizeof(req));
                req.text = busMsg.content;
                strlcpy(req.channel, busMsg.channel, sizeof(req.channel));
                strlcpy(req.chatId, busMsg.chat_id, sizeof(req.chatId));
                req.callback = nullptr;
                req.exCallback = nullptr;
                hasReq = true;

                req.exCallback = busResponseHandler;
            }
        }

        if (!hasReq) continue;

        s_busy = true;

        bool feishuWasActive = FeishuBot::isRunning();
        if (feishuWasActive) {
            FeishuBot::stop();
            Serial.printf("[AGENT] Feishu paused, heap=%d\n", ESP.getFreeHeap());
        }

        processRequest(req, system_prompt, tool_output);
        free(req.text);

        if (feishuWasActive) {
            FeishuBot::resume();
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

void Agent::sendMessage(const char* text, AgentResponseCallback onResponse) {
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
    if (!req.text || xQueueSend(s_queue, &req, 0) != pdTRUE) {
        free(req.text);
        if (onResponse) onResponse("Agent queue full, try again.");
    }
}

void Agent::sendMessageEx(const char* text, const char* channel, const char* chatId,
                          AgentResponseExCallback onResponse) {
    if (!s_ready) {
        if (onResponse) {
            AgentResponseInfo info = {"Agent unavailable.", channel, chatId};
            onResponse(&info);
        }
        return;
    }
    AgentRequest req;
    memset(&req, 0, sizeof(req));
    req.text = strdup(text);
    strlcpy(req.channel, channel, sizeof(req.channel));
    strlcpy(req.chatId, chatId, sizeof(req.chatId));
    req.callback = nullptr;
    req.exCallback = onResponse;
    if (!req.text || xQueueSend(s_queue, &req, 0) != pdTRUE) {
        free(req.text);
        if (onResponse) {
            AgentResponseInfo info = {"Agent queue full.", channel, chatId};
            onResponse(&info);
        }
    }
}

bool Agent::isBusy() { return s_busy; }
bool Agent::isReady() { return s_ready; }

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
