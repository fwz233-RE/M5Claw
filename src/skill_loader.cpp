#include "skill_loader.h"
#include "m5claw_config.h"
#include <SPIFFS.h>

#define MAX_SKILLS 16

struct SkillInfo {
    char filename[48];
    char title[64];
    char description[128];
};

static SkillInfo s_skills[MAX_SKILLS];
static int s_skillCount = 0;

static void extractMeta(const char* path, SkillInfo* info) {
    File f = SPIFFS.open(path, "r");
    if (!f) return;

    bool gotTitle = false, gotDesc = false;
    while (f.available() && (!gotTitle || !gotDesc)) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!gotTitle && line.startsWith("# ")) {
            strlcpy(info->title, line.c_str() + 2, sizeof(info->title));
            gotTitle = true;
        } else if (gotTitle && !gotDesc && line.length() > 0 && !line.startsWith("#")) {
            strlcpy(info->description, line.c_str(), sizeof(info->description));
            gotDesc = true;
        }
    }
    f.close();

    if (!gotTitle) {
        const char* base = strrchr(path, '/');
        strlcpy(info->title, base ? base + 1 : path, sizeof(info->title));
    }
}

void SkillLoader::init() {
    s_skillCount = 0;
    File root = SPIFFS.open("/");
    if (!root) return;

    const char* prefix = "/skills/";
    size_t prefixLen = strlen(prefix);

    File f = root.openNextFile();
    while (f && s_skillCount < MAX_SKILLS) {
        const char* name = f.name();
        if (strncmp(name, prefix, prefixLen) == 0 && strstr(name, ".md")) {
            SkillInfo& info = s_skills[s_skillCount];
            memset(&info, 0, sizeof(info));
            strlcpy(info.filename, name, sizeof(info.filename));
            extractMeta(name, &info);
            s_skillCount++;
        }
        f = root.openNextFile();
    }
    Serial.printf("[SKILLS] Found %d skills\n", s_skillCount);
}

size_t SkillLoader::buildSummary(char* buf, size_t size) {
    if (s_skillCount == 0) return 0;

    size_t off = 0;
    for (int i = 0; i < s_skillCount && off < size - 128; i++) {
        SkillInfo& s = s_skills[i];
        int w = snprintf(buf + off, size - off, "- **%s** (%s): %s\n",
                         s.title, s.filename,
                         s.description[0] ? s.description : "No description");
        if (w > 0) off += w;
    }
    return off;
}
