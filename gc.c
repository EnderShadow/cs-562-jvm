//
// Created by matthew on 10/9/19.
//

#include "gc.h"
#include "heap.h"
#include "indirection_impl.h"
#include "garbage_collection.h"
#include "jvmSettings.h"
#include "mm.h"
#include "utils.h"

volatile bool gcWantsToRun = false;
volatile atomic_uint_fast32_t numThreads = 0;
volatile atomic_uint_fast32_t numThreadsWaiting = 0;
pthread_mutex_t gcRunningMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t threadRegistrationMutex = PTHREAD_MUTEX_INITIALIZER;
jthread_t **jthreads = NULL;
size_t maxNumThreads = 0;

size_t gcCycle = 0;
volatile clock_t lastGC;

enum gcMode requestedMode = GC_MODE_NORMAL;

void runGC();

void *gcLoop(void *arg) {
    while(true) {
        // Run a full GC every gcInterval milliseconds
        if((clock() - lastGC) * 1000 / CLOCKS_PER_SEC >= gcInterval || gcWantsToRun)
            runGC();
        else
            sched_yield();
    }
}

void savePoint() {
    if(gcWantsToRun)
        ++numThreadsWaiting;
    while(gcWantsToRun)
        sched_yield();
    pthread_mutex_lock(&gcRunningMutex);
    numThreadsWaiting--;
    pthread_mutex_unlock(&gcRunningMutex);
}

bool registerThread(jthread_t *jthread) {
    pthread_mutex_lock(&threadRegistrationMutex);
    if(numThreads == maxNumThreads) {
        jthread_t **newArray = realloc(jthreads, sizeof(jthread_t *) * maxNumThreads * 2);
        if(!newArray) {
            pthread_mutex_unlock(&threadRegistrationMutex);
            return false;
        }
        jthreads = newArray;
        maxNumThreads *= 2;
    }
    jthreads[numThreads++] = jthread;
    pthread_mutex_unlock(&threadRegistrationMutex);
    return true;
}

void unregisterThread(jthread_t *jthread) {
    pthread_mutex_lock(&threadRegistrationMutex);
    size_t i = 0;
    while(i < numThreads) {
        if(jthreads[i] == jthread)
            break;
        ++i;
    }
    if(i < numThreads) {
        jthreads[i] = jthreads[numThreads - 1];
        --numThreads;
    }
    pthread_mutex_unlock(&threadRegistrationMutex);
}

pthread_t *initGC() {
    lastGC = clock();
    jthreads = malloc(sizeof(jthread_t *) * 4);
    if(!jthreads)
        return NULL;
    maxNumThreads = 4;
    pthread_t *thread = malloc(sizeof(pthread_t));
    if(!thread) {
        free(jthreads);
        return NULL;
    }
    if(pthread_create(thread, NULL, gcLoop, NULL)) {
        free(thread);
        free(jthreads);
        return NULL;
    }
    
    return thread;
}

void requestGC(enum gcMode gcMode) {
    pthread_mutex_lock(&gcRunningMutex);
    gcWantsToRun = true;
    requestedMode = MAX(requestedMode, gcMode);
    pthread_mutex_unlock(&gcRunningMutex);
}

void _youngHeapGC(size_t numObjs, object_t **obj) {
    // TODO swap active side
    // TODO move eden to active side and update indirection table
    // TODO move inactive side to active side and update indirection table (promote oldest objects if no room in active side)
}

void _oldHeapGC(size_t numObjs, object_t **obj) {
    // TODO compact objects and update indirection table
}

void runGC() {
    gcWantsToRun = true;
    pthread_mutex_lock(&gcRunningMutex);
    while(numThreads != numThreadsWaiting) {
        pthread_mutex_unlock(&gcRunningMutex);
        sched_yield();
        pthread_mutex_lock(&gcRunningMutex);
    }
    
    enum gcMode gcMode = requestedMode;
    gcCycle++;
    
    size_t numLiveObjects = 0;
    object_t **liveObjects = NULL;
    
    // TODO build live object list
    // TODO sort live object list
    
    // every 8 gc cycles run gc on the old heap
    if(gcMode != GC_MODE_MINOR_ONLY && (gcMode == GC_MODE_FORCE_MAJOR || (gcCycle & 0x7u) == 0))
        _oldHeapGC(numLiveObjects, liveObjects);
    _youngHeapGC(numLiveObjects, liveObjects);
    
    if(addrIndInfo->numFragmentedFree >= 8192) {
        // get rid of any extra free nodes
        rebuildFreeList(addrIndInfo);
        
        // this commented out section allows the jvm to compact the indirection table, but since that adds unneeded complexity, I'm not using it.
        // The compaction code is also not completed
        
        //compact_res_t * compactionResult = compactAddressTable(addrIndInfo);
        //while(compactionResult) {
        //    ((object_t *) addrIndInfo->addressTable[compactionResult->newSlot])->slot = compactionResult->newSlot;
        //    for(size_t i = 0; i < numLiveObjects; i++) {
        //        object_t *obj = liveObjects[i];
        //        size_t numFields = obj->class->numFields;
        //        field_t *fields = obj->class->fields;
        //        for(size_t j = 0; j < numFields; j++) {
        //            field_t *fieldInfo = fields + j;
        //            if(fieldInfo->type.type == TYPE_REFERENCE) {
        //                uint32_t offset = fieldInfo->objectOffset;
        //                slot_t *field;
        //                if(fields[j].flags & FIELD_ACC_STATIC)
        //                    field = obj->class->staticFieldData + offset;
        //                else
        //                    field = ((void *) obj) + offset;
        //
        //                if(*field == compactionResult->oldSlot)
        //                    *field = compactionResult->newSlot;
        //            }
        //        }
        //    }
        //
        //    // TODO update static fields in all classes
        //    // TODO update stack for each thread
        //
        //    compact_res_t * temp = compactionResult;
        //    compactionResult = temp->next;
        //    free(temp);
        //}
    }
    
    lastGC = clock();
    
    gcWantsToRun = false;
    requestedMode = GC_MODE_NORMAL;
    pthread_mutex_unlock(&gcRunningMutex);
}