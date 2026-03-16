#include "context_builder.h"
#include "memory_store.h"
#include "m5claw_config.h"

void ContextBuilder::buildSystemPrompt(char* buf, size_t bufSize) {
    String soul = MemoryStore::readSoul();
    String user = MemoryStore::readUser();
    String memory = MemoryStore::readMemory();

    snprintf(buf, bufSize,
        "%s\n\n"
        "## User Info\n%s\n\n"
        "## Memory\n%s\n\n"
        "## Important\n"
        "- You are running on an M5Stack Cardputer with a 240x135 pixel screen.\n"
        "- Keep responses concise (under 200 characters when possible).\n"
        "- You have tools: get_current_time, read_file, write_file, web_search.\n"
        "- Use write_file to update memory/MEMORY.md to remember important info.\n"
        "- Respond in the user's language (Chinese or English).\n",
        soul.c_str(), user.c_str(), memory.c_str());
}
