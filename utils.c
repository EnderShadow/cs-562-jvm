//
// Created by matthew on 11/4/19.
//

#include "utils.h"
#include "dataTypes.h"
#include "classloader.h"
#include "mm.h"
#include <string.h>

slot_t convertToJavaString(char *arg) {
    size_t length = strlen(arg);
    if(length > INT32_MAX)
        return 0;
    
    // TODO convert to UTF-16
    
    class_t *stringClass = loadClass("java/lang/String");
    if(!stringClass)
        return 0;
    
    int32_t stringLength = (int32_t) length;
    slot_t charArraySlot = newArray(1, &stringLength, stringClass);
    if(!charArraySlot)
        return 0;
    
    object_t *charArray = getObject(charArraySlot);
    for(int i = 0; i < stringLength; i++) {
        cell_t cell = {.c = arg[i]};
        setArrayElement(charArray, i, cell);
    }
    
    slot_t stringSlot = newObject(stringClass);
    if(!stringSlot)
        return 0;
    
    object_t *stringObject = getObject(stringSlot);
    for(int i = 0; i < stringClass->numFields; i++) {
        field_t *valueField = stringClass->fields + i;
        if(!strcmp(valueField->name, "value") && strcmp(valueField->descriptor, "[C") == 0) {
            uint32_t offset = valueField->objectOffset;
            slot_t *value = ((void *) stringObject) + offset;
            *value = charArraySlot;
            return stringSlot;
        }
    }
    
    // failed to find the value field which means something is wrong. This shouldn't happen
    return 0;
}