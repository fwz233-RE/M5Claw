#pragma once
#include <Arduino.h>
#include "m5claw_config.h"

enum CronKind { CRON_KIND_EVERY = 0, CRON_KIND_AT = 1 };

struct CronJob {
    char id[9];
    char name[32];
    bool enabled;
    CronKind kind;
    uint32_t intervalSec;
    int64_t  atEpoch;
    char message[256];
    char channel[16];
    char chatId[96];
    int64_t lastRun;
    int64_t nextRun;
    bool deleteAfterRun;
};

namespace CronService {
    void init();
    void start();
    bool addJob(CronJob* job);
    bool removeJob(const char* jobId);
    void listJobs(const CronJob** jobs, int* count);
    int getJobCount();
}
