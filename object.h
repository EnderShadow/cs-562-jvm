//
// Created by matthew on 10/9/19.
//

#ifndef JVM_OBJECT_H
#define JVM_OBJECT_H

#include "classfile.h"
#include "dataTypes.h"

typedef struct object {
    class_t *class;
    slot_t slot;
} object_t;

typedef struct array_object {
    class_t *class;
    slot_t slot;
    int32_t length;
} array_object_t;

uint8_t getArrayElementType(array_object_t *obj);
cell_t getArrayElement(array_object_t *obj, int32_t index, uint8_t *type);
double_cell_t getArrayElement2(array_object_t *obj, int32_t index, uint8_t *type);
void setArrayElement(array_object_t *obj, int32_t index, cell_t value);
void setArrayElement2(array_object_t *obj, int32_t index, double_cell_t value);

#endif //JVM_OBJECT_H
