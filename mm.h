//
// Created by matthew on 10/11/19.
//

#ifndef JVM_MM_H
#define JVM_MM_H

#include "classfile.h"
#include "dataTypes.h"
#include "object.h"

slot_t newObject(class_t *class);
slot_t newArray(uint8_t numDimensions, int32_t *sizes, class_t *class);

object_t *getObject(slot_t slot);

#endif //JVM_MM_H
