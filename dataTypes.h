//
// Created by matthew on 10/3/19.
//



#ifndef JVM_DATATYPES_H
#define JVM_DATATYPES_H

#include <stdint.h>
#include <stdbool.h>

#define TYPE_BOOLEAN            0
#define TYPE_CHAR               1
#define TYPE_BYTE               2
#define TYPE_SHORT              3
#define TYPE_INT                4
#define TYPE_LONG               5
#define TYPE_FLOAT              6
#define TYPE_DOUBLE             7
#define TYPE_REFERENCE          8
#define TYPE_RETURN_ADDRESS     9

#define slot_t uint32_t

// used for accessing cells
typedef union cell {
    uint8_t z;
    int8_t b;
    uint16_t c;
    int16_t s;
    int32_t i;
    float f;
    slot_t a;
    uint32_t r; // return address used with the JSR and RET instructions
} cell_t;

typedef union double_cell {
    int64_t l;
    double d;
} double_cell_t;

uint8_t getTypeFromFieldDescriptor(char *descriptor);
uint8_t getTypeFromMethodDescriptor(char *descriptor, uint16_t index, bool isStatic);
uint16_t getSizeOfTypeFromFieldDescriptor(char *descriptor);
uint16_t countNumParametersFromMethodDescriptor(char *descriptor, bool isStatic);

#endif //JVM_DATATYPES_H
