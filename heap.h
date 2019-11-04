//
// Created by matthew on 10/7/19.
//

#ifndef JVM_HEAP_H
#define JVM_HEAP_H

#include <stdbool.h>
#include "object.h"
#include "jvmSettings.h"
#include "indirection.h"

extern addr_ind_info_t *addrIndInfo;

bool initHeap();

void destroyHeap();

object_t *allocateObject(class_t *class);
object_t *allocateArrayObject(class_t *class, int elementSize, int32_t numElements, bool fillZero);
void switchActiveHalf();

bool isInYoungHeap(object_t *obj);
bool isInOldHeap(object_t *obj);
bool isInHeap(object_t *obj);

/**
 *
 * @param obj
 * @return the new pointer to the object or the old one if it didn't change
 */
object_t *moveToActiveHalf(object_t *obj);

/**
 *
 * @param obj
 * @return the new pointer to the object or the old one if it didn't change
 */
object_t *moveToOldGeneration(object_t *obj);

#endif //JVM_HEAP_H
