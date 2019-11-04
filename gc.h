//
// Created by matthew on 10/9/19.
//

#ifndef JVM_GC_H
#define JVM_GC_H

#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "jthread.h"

enum gcMode {
    GC_MODE_NORMAL,
    GC_MODE_MINOR_ONLY,
    GC_MODE_FORCE_MAJOR
};

extern volatile bool gcWantsToRun;
extern volatile atomic_uint_fast32_t numThreads;
extern volatile atomic_uint_fast32_t numThreadsWaiting;
extern pthread_mutex_t gcRunningMutex;

void savePoint();

bool registerThread(jthread_t *jthread);
void unregisterThread(jthread_t *jthread);

pthread_t *initGC();

void requestGC(enum gcMode gcMode);

#endif //JVM_GC_H
