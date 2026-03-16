#include "agent.h"
#include "llm_client.h"
#include "tool_registry.h"
#include "context_builder.h"
#include "session_mgr.h"
#include "m5claw_config.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

struct AgentRequest {
    char* text;
    AgentResponseCallback callback;
};

static QueueHandle_t s_queue = nullptr;
static volatile bool s_busy = false;
static volatile bool s_ready = false;
static const char* CHAT_ID = "local";

#define TOOL_OUTPUT_SIZE (4 * 1024)

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

    s_ready = true;
    Serial.printf("[AGENT] Ready. Free heap=%d psram=%d stackHW=%u\n",
                  ESP.getFreeHeap(),
                  (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));

    while (true) {
        AgentRequest req;
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        s_busy = true;
        Serial.printf("[AGENT] Processing: %.60s\n", req.text);

        ContextBuilder::buildSystemPrompt(system_prompt, M5CLAW_CONTEXT_BUF_SIZE);

        String histJson = SessionMgr::getHistoryJson(CHAT_ID, M5CLAW_AGENT_MAX_HISTORY);

        void* messagesMem = alloc_prefer_psram(sizeof(JsonDocument));
        JsonDocument* messages = messagesMem ? new (messagesMem) JsonDocument : nullptr;
        if (!messages) {
            s_busy = false;
            free(req.text);
            if (req.callback) req.callback("Agent out of memory.");
            continue;
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
                        for (JsonVariant block : rawContent) {
                            asstContent.add(block);
                        }
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

        if (final_text && final_text[0]) {
            SessionMgr::appendMessage(CHAT_ID, "user", req.text);
            SessionMgr::appendMessage(CHAT_ID, "assistant", final_text);
            if (req.callback) req.callback(final_text);
            free(final_text);
        } else {
            free(final_text);
            if (req.callback) req.callback("Sorry, an error occurred.");
        }

        free(req.text);
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
    req.text = strdup(text);
    req.callback = onResponse;
    if (!req.text || xQueueSend(s_queue, &req, 0) != pdTRUE) {
        free(req.text);
        if (onResponse) onResponse("Agent queue full, try again.");
    }
}

bool Agent::isBusy() { return s_busy; }
bool Agent::isReady() { return s_ready; }
