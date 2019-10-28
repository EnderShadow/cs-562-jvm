//
// Created by matthew on 10/26/19.
//

#include "constantpool.h"
#include <stdlib.h>
#include "flags.h"
#include <stdio.h>
#include <string.h>

void *parseConstantPool(class_t *class, void *classData) {
    uint16_t constantPoolLength = readu2(classData);
    classData += 2;
    constant_info_t **constantPool = malloc(sizeof(constant_info_t *) * constantPoolLength);
    if(!constantPool)
        return NULL;
    
    constantPool[0] = NULL;
    for(uint16_t i = 1; i < constantPoolLength; ++i) {
        uint8_t tag = readu1(classData++);
        constant_info_t *constantPoolEntry = malloc(sizeof(constant_info_t));
        if(!constantPoolEntry) {
            fail: // this label is used by the CONSTANT_utf8 case and default case from the switch statement below
            while(--i > 0) {
                if(constantPool[i]->utf8Info.tag == CONSTANT_utf8)
                    free(constantPool[i]->utf8Info.chars);
                free(constantPool[i]);
            }
            free(constantPool);
            return NULL;
        }
        constantPool[i] = constantPoolEntry;
        switch(tag) {
            case CONSTANT_utf8:
                constantPoolEntry->utf8Info.tag = tag;
                constantPoolEntry->utf8Info.length = readu2(classData);
                char *chars = malloc(constantPoolEntry->utf8Info.length + 1);
                if(!chars)
                    goto fail;
                memcpy(chars, classData + 2, constantPoolEntry->utf8Info.length);
                chars[constantPoolEntry->utf8Info.length] = '\0';
                constantPoolEntry->utf8Info.chars = chars;
                classData += constantPoolEntry->utf8Info.length + 2;
                break;
            case CONSTANT_Integer:
            case CONSTANT_Float:
                constantPoolEntry->integerFloatInfo.tag = tag;
                constantPoolEntry->integerFloatInfo.bytes = readu4(classData);
                classData += 4;
                break;
            case CONSTANT_Long:
            case CONSTANT_Double:
                constantPoolEntry->longDoubleInfo.tag = tag;
                constantPoolEntry->longDoubleInfo.bytes = readu8(classData);
                classData += 8;
                break;
            case CONSTANT_Class:
                constantPoolEntry->classInfo.tag = tag;
                constantPoolEntry->classInfo.nameIndex = readu2(classData);
                classData += 2;
                break;
            case CONSTANT_String:
                constantPoolEntry->stringInfo.tag = tag;
                constantPoolEntry->stringInfo.stringIndex = readu2(classData);
                classData += 2;
                break;
            case CONSTANT_Fieldref:
            case CONSTANT_Methodref:
            case CONSTANT_InterfaceMethodref:
                constantPoolEntry->fieldMethodInterfaceMethodRefInfo.tag = tag;
                constantPoolEntry->fieldMethodInterfaceMethodRefInfo.classIndex = readu2(classData);
                constantPoolEntry->fieldMethodInterfaceMethodRefInfo.nameAndTypeIndex = readu2(classData + 2);
                classData += 4;
                break;
            case CONSTANT_NameAndType:
                constantPoolEntry->nameAndTypeInfo.tag = tag;
                constantPoolEntry->nameAndTypeInfo.nameIndex = readu2(classData);
                constantPoolEntry->nameAndTypeInfo.descriptorIndex = readu2(classData + 2);
                classData += 4;
                break;
            case CONSTANT_MethodHandle:
                constantPoolEntry->methodHandleInfo.tag = tag;
                constantPoolEntry->methodHandleInfo.referenceKind = readu1(classData);
                constantPoolEntry->methodHandleInfo.referenceIndex = readu2(classData + 1);
                classData += 3;
                break;
            case CONSTANT_MethodType:
                constantPoolEntry->methodTypeInfo.tag = tag;
                constantPoolEntry->methodTypeInfo.descriptorIndex = readu2(classData);
                classData += 2;
                break;
            case CONSTANT_InvokeDynamic:
                constantPoolEntry->invokeDynamicInfo.tag = tag;
                constantPoolEntry->invokeDynamicInfo.bootstrapMethodAttrIndex = readu2(classData);
                constantPoolEntry->invokeDynamicInfo.nameAndTypeIndex = readu2(classData + 2);
                classData += 4;
                break;
            default:
                printf("Unknown constant pool element with tag %d\n", tag);
                free(constantPoolEntry);
                goto fail;
        }
    }
    
    class->constantPool = constantPool;
    class->numConstants = constantPoolLength;
    
    return classData;
}

uint8_t readu1(void *data) {
    return *(uint8_t *) data;
}

uint16_t readu2(void *data) {
    uint8_t *bytes = data;
    uint16_t result = *bytes++;
    result = (result << 8) | *bytes;
    return result;
}

uint32_t readu4(void *data) {
    uint8_t *bytes = data;
    uint32_t result = *bytes++;
    result = (result << 8) | *bytes++;
    result = (result << 8) | *bytes++;
    result = (result << 8) | *bytes;
    return result;
}

uint64_t readu8(void *data) {
    uint8_t *bytes = data;
    uint64_t result = *bytes++;
    result = (result << 8) | *bytes++;
    result = (result << 8) | *bytes++;
    result = (result << 8) | *bytes++;
    result = (result << 8) | *bytes++;
    result = (result << 8) | *bytes++;
    result = (result << 8) | *bytes++;
    result = (result << 8) | *bytes;
    return result;
}