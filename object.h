//
// Created by matthew on 10/9/19.
//

#ifndef JVM_OBJECT_H
#define JVM_OBJECT_H

#include "classfile.h"
#include "dataTypes.h"

typedef struct object {
    class_t *class;
    jlock_t jlock;
    slot_t slot;
    int32_t length; // only used for arrays
} object_t;

uint8_t getArrayElementType(object_t *obj);
cell_t getArrayElement(object_t *obj, int32_t index, uint8_t *type);
double_cell_t getArrayElement2(object_t *obj, int32_t index, uint8_t *type);
void setArrayElement(object_t *obj, int32_t index, cell_t value);
void setArrayElement2(object_t *obj, int32_t index, double_cell_t value);

#endif //JVM_OBJECT_H
