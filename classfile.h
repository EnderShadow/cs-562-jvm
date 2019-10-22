//
// Created by matthew on 10/7/19.
//

#ifndef JVM_CLASSFILE_H
#define JVM_CLASSFILE_H

#include <stdint.h>

typedef struct class class_t;

typedef struct type_info {
    class_t *classPointer;
    uint8_t type;
} type_t;

typedef struct field {
    type_t type;
    char *name;
    uint32_t objectOffset;
    uint16_t flags;
} field_t;

typedef struct exception_table {

} exception_table_t;

typedef struct method {
    char *name;
    type_t *parameterTypes;
    void *codeLocation;
    exception_table_t *exceptionTable;
    uint16_t numLocals;
    uint16_t maxStack;
    uint16_t flags;
    uint8_t numParameters;
} method_t;

struct class {
    char *name;
    struct class *thisClass;
    struct class *superClass;
    struct class **interfaces;
    method_t *methods;
    field_t *fields;
    void *staticFieldData;
    uint16_t numInterfaces;
    uint16_t numMethods;
    uint16_t numFields;
    uint16_t objectSize;
    uint16_t flags;
};

/**
 *
 * @return 1 if the class represents an array, otherwise 0
 */
int isArrayClass(class_t *class);

/**
 *
 * @param class
 * @return 1 if the class represents a primitive type, otherwise 0
 */
int isPrimitiveClass(class_t *class);

/**
 *
 * @param class
 * @return the number of dimensions the array represented by this class has or 0 if it's not an array
 */
int numArrayDimensions(class_t *class);

/**
 *
 * @param class
 * @return the size of each array element in bytes for this class if this class represents an array, otherwise 0
 */
int arrayElementSize(class_t *class);

#endif //JVM_CLASSFILE_H
