//
// Created by matthew on 11/1/19.
//

#include "dataTypes.h"
#include <string.h>

uint8_t getTypeFromFieldDescriptor(char *descriptor) {
    if(descriptor[0] == '[' || descriptor[0] == 'L')
        return TYPE_REFERENCE;
    switch(descriptor[0]) {
        case 'B': return TYPE_BYTE;
        case 'C': return TYPE_CHAR;
        case 'D': return TYPE_DOUBLE;
        case 'F': return TYPE_FLOAT;
        case 'I': return TYPE_INT;
        case 'J': return TYPE_LONG;
        case 'S': return TYPE_SHORT;
        case 'Z': return TYPE_BOOLEAN;
        default: return 0;
    }
}

uint8_t getTypeFromMethodDescriptor(char *descriptor, uint16_t index, bool isStatic) {
    if(!isStatic) {
        if(!index)
            return TYPE_REFERENCE;
        --index;
    }
    ++descriptor;
    while(index > 0) {
        if(descriptor[0] == '[') {
            while(*++descriptor == '[');
        }
        if(descriptor[0] == 'L') {
            while(*++descriptor != ';');
        }
        ++descriptor;
        --index;
    }
    return getTypeFromFieldDescriptor(descriptor);
}

uint16_t getSizeOfTypeFromFieldDescriptor(char *descriptor) {
    if(strlen(descriptor) > 1)
        return sizeof(slot_t);
    else if(descriptor[0] == 'J' || descriptor[0] == 'D')
        return sizeof(uint64_t);
    return sizeof(uint32_t);
}

uint16_t countNumParametersFromMethodDescriptor(char *descriptor, bool isStatic) {
    uint16_t numParameters = isStatic ? 0 : 1;
    
    ++descriptor;
    while(descriptor[0] != ')') {
        if(descriptor[0] == '[') {
            while(*++descriptor == '[');
        }
        if(descriptor[0] == 'L') {
            while(*++descriptor != ';');
        }
        ++numParameters;
        if(descriptor[0] == 'D' || descriptor[0] == 'J')
            ++numParameters;
        ++descriptor;
    }
    
    return numParameters;
}

#include <assert.h>

static_assert(sizeof(cell_t) * 2 == sizeof(double_cell_t), "cell_t is not half the size of double_cell_t");