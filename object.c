//
// Created by matthew on 10/9/19.
//

#include "object.h"

uint8_t getArrayElementType(array_object_t *obj) {
    char elementType = obj->class->name[1];
    switch(elementType) {
        case 'Z': return TYPE_BOOLEAN;
        case 'B': return TYPE_BYTE;
        case 'C': return TYPE_CHAR;
        case 'S': return TYPE_SHORT;
        case 'I': return TYPE_INT;
        case 'F': return TYPE_FLOAT;
        case 'L': return TYPE_REFERENCE;
        case '[': return TYPE_REFERENCE;
        case 'J': return TYPE_LONG;
        case 'D': return TYPE_DOUBLE;
        default: return TYPE_INT;
    }
}

cell_t getArrayElement(array_object_t *obj, int32_t index, uint8_t *type) {
    cell_t cell;
    void *arrayData = obj + 1;
    
    char elementType = obj->class->name[1];
    switch(elementType) {
        case 'Z':
            if(type)
                *type = TYPE_BOOLEAN;
            cell.i = ((uint8_t *) arrayData)[index];
            break;
        case 'B':
            if(type)
                *type = TYPE_BYTE;
            cell.i = ((int8_t *) arrayData)[index];
            break;
        case 'C':
            if(type)
                *type = TYPE_CHAR;
            cell.i = ((uint16_t *) arrayData)[index];
            break;
        case 'S':
            if(type)
                *type = TYPE_SHORT;
            cell.i = ((int16_t *) arrayData)[index];
            break;
        case 'I':
            if(type)
                *type = TYPE_INT;
            cell.i = ((int32_t *) arrayData)[index];
            break;
        case 'F':
            if(type)
                *type = TYPE_FLOAT;
            cell.f = ((float *) arrayData)[index];
            break;
        case 'L':
            if(type)
                *type = TYPE_REFERENCE;
            cell.a = ((slot_t *) arrayData)[index];
            break;
        case '[':
            if(type)
                *type = TYPE_REFERENCE;
            cell.a = ((slot_t *) arrayData)[index];
            break;
        default:
            if(type)
                *type = TYPE_INT;
            cell.i = 0;
    }
    
    return cell;
}

double_cell_t getArrayElement2(array_object_t *obj, int32_t index, uint8_t *type) {
    double_cell_t cell;
    void *arrayData = obj + 1;
    
    char elementType = obj->class->name[1];
    switch(elementType) {
        case 'J':
            if(type)
                *type = TYPE_LONG;
            cell.l = ((int64_t *) arrayData)[index];
            break;
        case 'D':
            if(type)
                *type = TYPE_DOUBLE;
            cell.d = ((double *) arrayData)[index];
            break;
        default:
            if(type)
                *type = TYPE_LONG;
            cell.l = 0;
    }
    
    return cell;
}

void setArrayElement(array_object_t *obj, int32_t index, cell_t value) {
    void *arrayData = obj + 1;
    
    char elementType = obj->class->name[1];
    switch(elementType) {
        case 'Z':
            ((uint8_t *) arrayData)[index] = value.z;
            break;
        case 'B':
            ((int8_t *) arrayData)[index] = value.b;
            break;
        case 'C':
            ((uint16_t *) arrayData)[index] = value.c;
            break;
        case 'S':
            ((int16_t *) arrayData)[index] = value.s;
            break;
        case 'I':
            ((int32_t *) arrayData)[index] = value.i;
            break;
        case 'F':
            ((float *) arrayData)[index] = value.f;
            break;
        case 'L':
            ((slot_t *) arrayData)[index] = value.a;
            break;
        case '[':
            ((slot_t *) arrayData)[index] = value.a;
            break;
        default:
            break;
    }
}

void setArrayElement2(array_object_t *obj, int32_t index, double_cell_t value) {
    void *arrayData = obj + 1;
    
    char elementType = obj->class->name[1];
    switch(elementType) {
        case 'J':
            ((int64_t *) arrayData)[index] = value.l;
            break;
        case 'D':
            ((double *) arrayData)[index] = value.d;
            break;
        default:
            break;
    }
}