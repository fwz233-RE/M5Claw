#include "cron_service.h"
#include "message_bus.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <time.h>

static CronJob s_jobs[M5CLAW_CRON_MAX_JOBS];
static int s_jobCount = 0;
static TimerHandle_t s_timer = nullptr;

static void scheduleNextCheck(uint32_t delayMs);
static void rescheduleNextCheck(bool soon = false);

static int compactJobs() {
    int active = 0;
    for (int i = 0; i < s_jobCount; i++) {
        if (!s_jobs[i].enabled) continue;
        if (active != i) s_jobs[active] = s_jobs[i];
        active++;
    }
    for (int i = active; i < s_jobCount; i++) {
        memset(&s_jobs[i], 0, sizeof(s_jobs[i]));
    }
    s_jobCount = active;
    return s_jobCount;
}

static int countEnabledJobs() {
    int count = 0;
    for (int i = 0; i < s_jobCount; i++) {
        if (s_jobs[i].enabled) count++;
    }
    return count;
}

static void generateId(char* buf) {
    uint32_t r = esp_random();
    snprintf(buf, 9, "%08x", r);
}

static void saveJobs() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < s_jobCount; i++) {
        CronJob& j = s_jobs[i];
        if (!j.enabled) continue;
        JsonObject o = arr.add<JsonObject>();
        o["id"] = j.id;
        o["name"] = j.name;
        o["kind"] = (int)j.kind;
        o["interval_s"] = j.intervalSec;
        o["at_epoch"] = j.atEpoch;
        o["message"] = j.message;
        o["channel"] = j.channel;
        o["chat_id"] = j.chatId;
        o["last_run"] = j.lastRun;
        o["next_run"] = j.nextRun;
        o["delete_after"] = j.deleteAfterRun;
    }
    File f = SPIFFS.open(M5CLAW_CRON_FILE, "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

static void loadJobs() {
    File f = SPIFFS.open(M5CLAW_CRON_FILE, "r");
    if (!f) return;

    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return; }
    f.close();

    JsonArray arr = doc.as<JsonArray>();
    for (JsonVariant v : arr) {
        if (s_jobCount >= M5CLAW_CRON_MAX_JOBS) break;
        CronJob& j = s_jobs[s_jobCount];
        memset(&j, 0, sizeof(j));
        strlcpy(j.id, v["id"] | "", sizeof(j.id));
        strlcpy(j.name, v["name"] | "", sizeof(j.name));
        j.kind = (CronKind)(v["kind"] | 0);
        j.intervalSec = v["interval_s"] | 3600;
        j.atEpoch = v["at_epoch"] | (int64_t)0;
        strlcpy(j.message, v["message"] | "", sizeof(j.message));
        strlcpy(j.channel, v["channel"] | M5CLAW_CHAN_LOCAL, sizeof(j.channel));
        strlcpy(j.chatId, v["chat_id"] | "cron", sizeof(j.chatId));
        j.lastRun = v["last_run"] | (int64_t)0;
        j.nextRun = v["next_run"] | (int64_t)0;
        j.deleteAfterRun = v["delete_after"] | false;
        j.enabled = true;
        s_jobCount++;
    }
    Serial.printf("[CRON] Loaded %d jobs\n", s_jobCount);
}

static int64_t nowEpoch() {
    struct tm ti;
    if (!getLocalTime(&ti, 0)) return 0;
    time_t t = mktime(&ti);
    return (int64_t)t;
}

static void scheduleNextCheck(uint32_t delayMs) {
    if (!s_timer) return;
    if (delayMs == 0) delayMs = 1000;

    TickType_t ticks = pdMS_TO_TICKS(delayMs);
    if (ticks == 0) ticks = 1;

    xTimerStop(s_timer, 0);
    xTimerChangePeriod(s_timer, ticks, 0);
    xTimerStart(s_timer, 0);
}

static void rescheduleNextCheck(bool soon) {
    if (!s_timer) return;

    if (soon) {
        scheduleNextCheck(1000);
        return;
    }

    int enabledCount = countEnabledJobs();
    if (enabledCount == 0) {
        xTimerStop(s_timer, 0);
        return;
    }

    int64_t now = nowEpoch();
    if (now == 0) {
        scheduleNextCheck(5000);
        return;
    }

    int64_t earliest = 0;
    for (int i = 0; i < s_jobCount; i++) {
        CronJob& j = s_jobs[i];
        if (!j.enabled) continue;

        int64_t dueAt = 0;
        if (j.kind == CRON_KIND_EVERY) {
            if (j.nextRun <= 0) {
                j.nextRun = now + j.intervalSec;
            }
            dueAt = j.nextRun;
        } else {
            dueAt = (j.lastRun == 0) ? j.atEpoch : 0;
        }

        if (dueAt <= 0) continue;
        if (earliest == 0 || dueAt < earliest) earliest = dueAt;
    }

    if (earliest == 0) {
        scheduleNextCheck(5000);
        return;
    }

    int64_t delaySec = earliest - now;
    if (delaySec < 1) delaySec = 1;
    if (delaySec > 24 * 3600) delaySec = 24 * 3600;
    scheduleNextCheck((uint32_t)delaySec * 1000U);
}

static void checkJobs(TimerHandle_t timer) {
    (void)timer;
    int64_t now = nowEpoch();
    if (now == 0) {
        scheduleNextCheck(5000);
        return;
    }

    bool needSave = false;
    for (int i = 0; i < s_jobCount; i++) {
        CronJob& j = s_jobs[i];
        if (!j.enabled) continue;

        bool shouldFire = false;
        if (j.kind == CRON_KIND_EVERY) {
            if (j.nextRun == 0) j.nextRun = now + j.intervalSec;
            shouldFire = now >= j.nextRun;
        } else {
            shouldFire = now >= j.atEpoch && j.lastRun == 0;
        }

        if (shouldFire) {
            BusMessage msg = {};
            strlcpy(msg.channel, j.channel, sizeof(msg.channel));
            strlcpy(msg.chat_id, j.chatId, sizeof(msg.chat_id));
            char* content = (char*)malloc(strlen(j.message) + 32);
            if (content) {
                snprintf(content, strlen(j.message) + 32, "[Cron:%s] %s", j.name, j.message);
                msg.content = content;
                if (!MessageBus::pushInbound(&msg)) free(content);
            }

            j.lastRun = now;
            if (j.kind == CRON_KIND_EVERY) {
                j.nextRun = now + j.intervalSec;
            }
            if (j.deleteAfterRun) {
                j.enabled = false;
            }
            needSave = true;
            Serial.printf("[CRON] Fired: %s\n", j.name);
        }
    }
    if (needSave) {
        compactJobs();
        saveJobs();
    }
    rescheduleNextCheck(false);
}

void CronService::init() {
    memset(s_jobs, 0, sizeof(s_jobs));
    s_jobCount = 0;
    loadJobs();
}

void CronService::start() {
    if (s_timer) return;
    s_timer = xTimerCreate("cron", pdMS_TO_TICKS(M5CLAW_CRON_CHECK_MS),
                           pdFALSE, nullptr, checkJobs);
    if (s_timer) {
        Serial.println("[CRON] Timer started");
        rescheduleNextCheck(false);
    }
}

bool CronService::addJob(CronJob* job) {
    compactJobs();

    if (!job) return false;
    if (s_jobCount >= M5CLAW_CRON_MAX_JOBS) return false;
    if (job->kind == CRON_KIND_EVERY && job->intervalSec == 0) return false;

    generateId(job->id);
    int64_t now = nowEpoch();
    if (now == 0) return false;
    job->lastRun = 0;
    if (job->kind == CRON_KIND_EVERY) {
        job->nextRun = now + job->intervalSec;
    } else if (job->atEpoch <= 0) {
        return false;
    }
    s_jobs[s_jobCount++] = *job;
    saveJobs();
    rescheduleNextCheck(false);
    return true;
}

bool CronService::removeJob(const char* jobId) {
    for (int i = 0; i < s_jobCount; i++) {
        if (strcmp(s_jobs[i].id, jobId) == 0) {
            s_jobs[i].enabled = false;
            compactJobs();
            saveJobs();
            rescheduleNextCheck(false);
            return true;
        }
    }
    return false;
}

void CronService::listJobs(const CronJob** jobs, int* count) {
    *jobs = s_jobs;
    *count = s_jobCount;
}

int CronService::getJobCount() {
    return countEnabledJobs();
}
