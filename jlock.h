//
// Created by matthew on 11/3/19.
//

#ifndef JVM_JLOCK_H
#define JVM_JLOCK_H

#include <stdint.h>
#include <pthread.h>

// jlock_t is a re-entrant lock which is used by the jvm
typedef struct jlock {
    volatile int owner;
    uint32_t acquiredCount;
    pthread_mutex_t conditionMutex;
    pthread_cond_t conditionVariable;
} jlock_t;

void jlock_init(jlock_t *jlock);
void jlock_lock(int threadId, jlock_t *jlock);
void jlock_unlock(int threadId, jlock_t *jlock);

int jlock_wait(jlock_t *jlock, uint64_t millis);
void jlock_notify(jlock_t *jlock);
void jlock_notifyAll(jlock_t *jlock);

#endif //JVM_JLOCK_H