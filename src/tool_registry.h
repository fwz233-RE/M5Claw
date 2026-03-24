#pragma once
#include <Arduino.h>

typedef bool (*ToolExecuteFn)(const char* input_json, char* output, size_t output_size);

struct ToolDef {
    const char* name;
    const char* description;
    const char* input_schema_json;
    ToolExecuteFn execute;
};

namespace ToolRegistry {
    void init();
    const char* getToolsJson();
    bool execute(const char* name, const char* input, char* output, size_t output_size);
    void setRequestContext(const char* channel, const char* chatId);
    const char* getCtxChannel();
    const char* getCtxChatId();
}

