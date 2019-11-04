//
// Created by matthew on 10/11/19.
//

#include "mm.h"
#include "heap.h"
#include <string.h>
#include "classloader.h"

/**
 *
 * @param class
 * @return a slot containing an uninitialized instance of the class. If the returned value is 0 then no object was created because of a lack of memory
 */
slot_t newObject(class_t *class) {
    slot_t slot = allocateSlot(addrIndInfo);
    if(slot) {
        object_t *object = allocateObject(class);
        if(!object) {
            freeSlot(addrIndInfo, slot);
            return 0;
        }
        setRawAddress(addrIndInfo, slot, object);
        object->class = class;
        jlock_init(&object->jlock);
        object->slot = slot;
    }
    return slot;
}

slot_t newArray(uint8_t numDimensions, int32_t *sizes, class_t *class) {
    if(numDimensions == 0)
        return 0;
    slot_t slot = allocateSlot(addrIndInfo);
    if(!slot)
        return 0;
    char *className = malloc(numDimensions + 3u + strlen(class->name));
    if(!className)
        goto fail1;
    int32_t i = 0;
    while(i < numDimensions)
        className[i++] = '[';
    if(!isArrayClass(class) && !isPrimitiveClass(class))
        className[i++] = 'L';
    className[i] = '\0';
    className = strcat(className, class->name);
    if(!isArrayClass(class) && !isPrimitiveClass(class))
        className = strcat(className, ";");
    class_t *arrayClass = loadClass(className);
    if(!arrayClass)
        goto fail2;
    int elementSize = arrayElementSize(arrayClass);
    object_t *arrayObj = allocateArrayObject(arrayClass, elementSize, sizes[0], numDimensions == 1);
    if(!arrayObj)
        goto fail2;
    setRawAddress(addrIndInfo, slot, arrayObj);
    arrayObj->class = arrayClass;
    jlock_init(&arrayObj->jlock);
    arrayObj->slot = slot;
    arrayObj->length = sizes[0];
    
    if(numDimensions > 1) {
        slot_t *elements = (slot_t *) (arrayObj + 1);
        for(i = 0; i < sizes[0]; i++) {
            slot_t subArray = newArray(numDimensions - 1, sizes + 1, class);
            if(!subArray)
                // garbage collector will free all the objects
                return 0;
            elements[i] = subArray;
        }
    }
    
    return slot;
    
    fail2: free(className);
    fail1: freeSlot(addrIndInfo, slot);
    return 0;
}

object_t *getObject(slot_t slot) {
    return getRawAddress(addrIndInfo, slot);
}