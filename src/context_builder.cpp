#include "context_builder.h"
#include "memory_store.h"
#include "skill_loader.h"
#include "m5claw_config.h"

void ContextBuilder::buildSystemPrompt(char* buf, size_t bufSize) {
    String soul = MemoryStore::readSoul();
    String user = MemoryStore::readUser();
    String memory = MemoryStore::readMemory();

    size_t off = 0;

    off += snprintf(buf + off, bufSize - off,
        "# M5Claw\n\n"
        "You are MiMo, the Xiaomi AI assistant, running as M5Claw on an M5Stack Cardputer (ESP32-S3, 240x135 screen).\n"
        "You communicate through local keyboard, local voice, and WeChat messenger.\n"
        "You may receive text, images, or audio in the current user turn.\n\n"
        "Be helpful, accurate, and concise. Keep responses under 200 characters when on local screen.\n\n");

    // Personality
    if (soul.length() > 0) {
        off += snprintf(buf + off, bufSize - off, "## Personality\n\n%s\n\n", soul.c_str());
    }

    // User info
    if (user.length() > 0) {
        off += snprintf(buf + off, bufSize - off, "## User Info\n\n%s\n\n", user.c_str());
    }

    // Tools
    off += snprintf(buf + off, bufSize - off,
        "## Available Tools\n"
        "- get_current_time: Get date and time. You have NO internal clock, always use this.\n"
        "- read_file: Read a file (path relative to storage root).\n"
        "- write_file: Write/overwrite a file.\n"
        "- edit_file: Find-and-replace edit a file.\n"
        "- list_dir: List files, optionally filter by prefix.\n"
        "- cron_add: Schedule a recurring (every) or one-shot (at) task.\n"
        "- cron_list: List all scheduled cron jobs.\n"
        "- cron_remove: Remove a cron job by ID.\n"
        "- wechat_send: Send a message to a WeChat user proactively.\n"
        "- Built-in MiMo web_search is available for current information when needed.\n\n"
        "cron_add automatically delivers notifications back to the channel and chat where the request originated.\n\n");

    // Memory
    off += snprintf(buf + off, bufSize - off,
        "## Memory\n"
        "Persistent storage on local flash:\n"
        "- Long-term: memory/MEMORY.md\n"
        "- Daily notes: memory/daily/<YYYY-MM-DD>.md\n\n"
        "IMPORTANT: Actively use memory. When you learn something new, write to MEMORY.md.\n"
        "Always read_file MEMORY.md before writing so you can edit_file without losing content.\n"
        "Use get_current_time for today's date before writing daily notes.\n\n");

    if (memory.length() > 0) {
        int maxMem = (int)(bufSize - off - 512);
        if (maxMem > 0) {
            if ((int)memory.length() > maxMem) memory = memory.substring(0, maxMem);
            off += snprintf(buf + off, bufSize - off, "## Long-term Memory\n\n%s\n\n", memory.c_str());
        }
    }

    // Skills
    char skillsBuf[1024];
    size_t skillsLen = SkillLoader::buildSummary(skillsBuf, sizeof(skillsBuf));
    if (skillsLen > 0 && off < bufSize - 256) {
        off += snprintf(buf + off, bufSize - off,
            "## Skills\n"
            "Specialized instruction files in skills/. Read full file for details.\n"
            "You can create new skills via write_file to skills/<name>.md.\n%s\n",
            skillsBuf);
    }

    // Channels
    off += snprintf(buf + off, bufSize - off,
        "## Channels\n"
        "- local: M5Cardputer screen + keyboard + voice. Keep replies short.\n"
        "- wechat: WeChat messenger. Longer replies OK. Use wechat_send to push messages.\n"
        "- system: Internal triggers (cron, heartbeat). Route response to appropriate channel.\n\n"
        "Respond in the user's language (Chinese or English).\n"
        "Use tools when needed. Provide final answer as text after tool use.\n");
}
