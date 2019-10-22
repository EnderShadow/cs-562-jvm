//
// Created by matthew on 10/7/19.
//

#include "classfile.h"
#include "dataTypes.h"
#include <string.h>

/**
 *
 * @return 1 if the class represents an array, otherwise 0
 */
int isArrayClass(class_t *class) {
    return class->name[0] == '[';
}

/**
 *
 * @param class
 * @return 1 if the class represents a primitive type, otherwise 0
 */
int isPrimitiveClass(class_t *class) {
    return strlen(class->name) == 1;
}

/**
 *
 * @param class
 * @return the number of dimensions the array represented by this class has or 0 if it's not an array
 */
int numArrayDimensions(class_t *class) {
    int numDimensions = 0;
    while(class->name[numDimensions++] == '[');
    return numDimensions - 1;
}

/**
 *
 * @param class
 * @return the size of each array element in bytes for this class if this class represents an array, otherwise 0
 */
int arrayElementSize(class_t *class) {
    if(!isArrayClass(class))
        return 0;
    
    char elementType = class->name[1];
    switch(elementType) {
        case 'Z': return 1;
        case 'B': return 1;
        case 'C': return 2;
        case 'S': return 2;
        case 'I': return 4;
        case 'F': return 4;
        case 'L': return sizeof(slot_t);
        case '[': return sizeof(slot_t);
        case 'J': return 8;
        case 'D': return 8;
        default: return 0; // This should never happen
    }
}