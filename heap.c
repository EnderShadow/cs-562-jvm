//
// Created by matthew on 10/7/19.
//

#include "heap.h"
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include "gc.h"

pthread_mutex_t allocationMutex = PTHREAD_MUTEX_INITIALIZER;

addr_ind_info_t *addrIndInfo = NULL;

void *eden, *young1, *young2, *old, *endHeap;
size_t edenSize = 0;
size_t youngSize = 0;
size_t oldSize = 0;

bool usingFirstYoung = true;
size_t edenNextPos = 0;
size_t youngNextPos = 0;
size_t oldNextPos = 0;

bool initHeap() {
    eden = mmap(NULL, maxHeap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(eden == MAP_FAILED)
        return false;
    
    edenSize = maxHeap >> 2u;
    youngSize = edenSize >> 1u;
    oldSize = maxHeap >> 1u;
    
    young1 = eden + edenSize;
    young2 = young1 + youngSize;
    old = young2 + youngSize;
    endHeap = old + oldSize;
    
    addrIndInfo = createAddressIndirectionInfo();
    if(!addrIndInfo)
        return false;
    return true;
}

void destroyHeap() {
    munmap(eden, maxHeap);
}

object_t *allocateObject(class_t *class) {
    pthread_mutex_lock(&allocationMutex);
    if(edenSize - edenNextPos < class->objectSize) {
        // try a minor gc
        requestGC(GC_MODE_MINOR_ONLY);
        savePoint();
        if(edenSize - edenNextPos < class->objectSize) {
            // force a full gc
            requestGC(GC_MODE_FORCE_MAJOR);
            savePoint();
            if(edenSize - edenNextPos < class->objectSize) {
                // We weren't able to get any memory to allocate the object
                return NULL;
            }
        }
    }
    
    void *obj = eden + edenNextPos;
    edenNextPos += ALIGN(class->objectSize);
    
    pthread_mutex_unlock(&allocationMutex);
    
    memset(obj, 0, class->objectSize);
    
    return obj;
}

object_t *allocateArrayObject(class_t *class, int elementSize, int32_t numElements, bool fillZero) {
    size_t objectSize = class->objectSize + elementSize * numElements;
    pthread_mutex_lock(&allocationMutex);
    if(edenSize - edenNextPos < objectSize) {
        // try a minor gc
        requestGC(GC_MODE_MINOR_ONLY);
        savePoint();
        if(edenSize - edenNextPos < objectSize) {
            // force a full gc
            requestGC(GC_MODE_FORCE_MAJOR);
            savePoint();
            if(edenSize - edenNextPos < objectSize) {
                // We weren't able to get any memory to allocate the object
                return NULL;
            }
        }
    }
    
    void *obj = eden + edenNextPos;
    edenNextPos += ALIGN(objectSize);
    
    pthread_mutex_unlock(&allocationMutex);
    
    if(fillZero)
        memset(obj, 0, objectSize);
    
    return obj;
}

void switchActiveHalf() {
    usingFirstYoung = !usingFirstYoung;
    youngNextPos = 0;
}

bool isInYoungHeap(object_t *obj) {
    size_t objectSize = obj->class->objectSize;
    if(obj->class->name[0] == '[')
        objectSize += obj->length * arrayElementSize(obj->class);
    return (void *) obj >= young1 && (void *) obj + objectSize < old;
}

bool isInOldHeap(object_t *obj) {
    size_t objectSize = obj->class->objectSize;
    if(obj->class->name[0] == '[')
        objectSize += obj->length * arrayElementSize(obj->class);
    return (void *) obj >= old && (void *) obj + objectSize < endHeap;
}

bool isInHeap(object_t *obj) {
    size_t objectSize = obj->class->objectSize;
    if(obj->class->name[0] == '[')
        objectSize += obj->length * arrayElementSize(obj->class);
    return (void *) obj >= eden  && (void *) obj + objectSize < endHeap;
}

/**
 *
 * @param obj
 * @return the new pointer to the object or the old one if it didn't change
 */
object_t *moveToActiveHalf(object_t *obj) {
    size_t objSize = obj->class->objectSize;
    if(isArrayClass(obj->class))
        objSize += obj->length * arrayElementSize(obj->class);
    object_t *newObjPointer = obj;
    
    if(usingFirstYoung) {
        if((void *) obj + objSize < young1 || (void *) obj >= young2) {
            newObjPointer = young1 + youngNextPos;
            memcpy(newObjPointer, obj, objSize);
            youngNextPos += objSize;
        }
    }
    else {
        if((void *) obj + objSize < young2 || (void *) obj >= old) {
            newObjPointer = young2 + youngNextPos;
            memcpy(newObjPointer, obj, objSize);
            youngNextPos += objSize;
        }
    }
    
    return newObjPointer;
}

/**
 *
 * @param obj
 * @return the new pointer to the object or the old one if it didn't change
 */
object_t *moveToOldGeneration(object_t *obj) {
    size_t objSize = obj->class->objectSize;
    if(isArrayClass(obj->class))
        objSize += obj->length * arrayElementSize(obj->class);
    object_t *newObjPointer = obj;
    
    if(!isInOldHeap(obj)) {
        if(oldSize - oldNextPos >= objSize) {
            newObjPointer = old + oldNextPos;
            memcpy(newObjPointer, obj, objSize);
            oldNextPos += objSize;
        }
        else {
            // TODO
            // There isn't enough room in the old generation. Is it full? Do we need to GC it?
        }
    }
    
    return newObjPointer;
}