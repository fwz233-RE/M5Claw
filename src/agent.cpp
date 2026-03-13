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
static const char* CHAT_ID = "local";

#define TOOL_OUTPUT_SIZE (4 * 1024)

static void agent_task(void* arg) {
    Serial.printf("[AGENT] Task started on core %d\n", xPortGetCoreID());

    char* system_prompt = (char*)heap_caps_calloc(1, M5CLAW_CONTEXT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char* tool_output = (char*)heap_caps_calloc(1, TOOL_OUTPUT_SIZE, MALLOC_CAP_SPIRAM);

    if (!system_prompt || !tool_output) {
        Serial.println("[AGENT] Failed to allocate PSRAM");
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        AgentRequest req;
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        s_busy = true;
        Serial.printf("[AGENT] Processing: %.60s\n", req.text);

        ContextBuilder::buildSystemPrompt(system_prompt, M5CLAW_CONTEXT_BUF_SIZE);

        String histJson = SessionMgr::getHistoryJson(CHAT_ID, M5CLAW_AGENT_MAX_HISTORY);

        JsonDocument* messages = new (heap_caps_malloc(sizeof(JsonDocument), MALLOC_CAP_SPIRAM)) JsonDocument;
        if (!messages) { s_busy = false; free(req.text); continue; }

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
        Serial.printf("[AGENT] Done. Free PSRAM: %d\n",
                      (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
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
    AgentRequest req;
    req.text = strdup(text);
    req.callback = onResponse;
    if (!req.text || xQueueSend(s_queue, &req, 0) != pdTRUE) {
        free(req.text);
        if (onResponse) onResponse("Agent queue full, try again.");
    }
}

bool Agent::isBusy() { return s_busy; }
