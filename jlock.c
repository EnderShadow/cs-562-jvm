//
// Created by matthew on 11/3/19.
//

#include "jlock.h"
#include <errno.h>

void jlock_init(jlock_t *jlock) {
    jlock->owner = 0;
    jlock->acquiredCount = 0;
    pthread_mutex_init(&jlock->conditionMutex, NULL);
    pthread_cond_init(&jlock->conditionVariable, NULL);
}

void jlock_lock(int threadId, jlock_t *jlock) {
    if(jlock->owner == threadId) {
        ++jlock->acquiredCount;
    }
    else {
        pthread_mutex_lock(&jlock->conditionMutex);
        jlock->owner = threadId;
        jlock->acquiredCount = 1;
    }
}

void jlock_unlock(int threadId, jlock_t *jlock) {
    // sanity check to make sure it's not called on an unowned lock
    if(jlock->owner == threadId) {
        // at this point no other thread should act on jlock->acquiredCount so we don't need to atomically modify it.
        --jlock->acquiredCount;
        
        if(!jlock->acquiredCount) {
            // since we no longer have the lock, we should give up ownership
            jlock->owner = 0;
            pthread_mutex_unlock(&jlock->conditionMutex);
        }
    }
}

int jlock_wait(jlock_t *jlock, uint64_t millis) {
    struct timespec timespec;
    clock_gettime(CLOCK_REALTIME, &timespec);
    timespec.tv_sec += millis / 1000;
    timespec.tv_nsec += (millis % 1000) * 1000000;
    if(timespec.tv_nsec >= 1000000000) {
        timespec.tv_nsec -= 1000000000;
        ++timespec.tv_sec;
    }
    
    int result;
    if(millis)
        result = pthread_cond_timedwait(&jlock->conditionVariable, &jlock->conditionMutex, &timespec);
    else
        result = pthread_cond_wait(&jlock->conditionVariable, &jlock->conditionMutex);
    
    if(result == ETIMEDOUT)
        return 1;
    return 0;
}

void jlock_notify(jlock_t *jlock) {
    pthread_cond_signal(&jlock->conditionVariable);
}

void jlock_notifyAll(jlock_t *jlock) {
    pthread_cond_broadcast(&jlock->conditionVariable);
}