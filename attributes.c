//
// Created by matthew on 10/28/19.
//

#include "attributes.h"
#include "constantpool.h"
#include <stdlib.h>
#include <string.h>

void *parseAttributes(uint16_t length, attribute_info_t **attributes, class_t *class, void *classData) {
    for(int i = 0; i < length; ++i) {
        uint16_t nameIndex = readu2(classData);
        char *name = class->constantPool[nameIndex]->utf8Info.chars;
        uint32_t attributeLength = readu4(classData + 2);
        classData += 6;
        if(strcmp(name, "ConstantValue") == 0) {
            // parse constant value
            constant_value_attribute_t *attr = malloc(sizeof(constant_value_attribute_t));
            if(!attr)
                return NULL;
            attr->name = name;
            attr->constantIndex = readu2(classData);
            attributes[i] = (attribute_info_t *) attr;
            classData += 2;
        }
        else if(strcmp(name, "Code") == 0) {
            // parse code
            code_attribute_t *attr = malloc(sizeof(code_attribute_t));
            if(!attr)
                goto fail1;
            attr->name = name;
            attr->maxStack = readu2(classData);
            attr->maxLocals = readu2(classData + 2);
            attr->codeLength = readu4(classData + 4);
            classData += 8;
            attr->code = malloc(attr->codeLength);
            if(!attr->code)
                goto fail2;
            memcpy(attr->code, classData, attr->codeLength);
            classData += attr->codeLength;
            attr->exceptionTableLength = readu2(classData);
            classData += 2;
            attr->exceptionHandlers = malloc(sizeof(exception_table_t) * attr->exceptionTableLength);
            if(!attr->exceptionHandlers)
                goto fail3;
            for(int j = 0; j < attr->exceptionTableLength; ++j) {
                exception_table_t *exceptionTable = attr->exceptionHandlers + j;
                exceptionTable->startPC = readu2(classData);
                exceptionTable->endPC = readu2(classData + 2);
                exceptionTable->handlerPC = readu2(classData + 4);
                exceptionTable->catchType = readu2(classData + 6);
                classData += 8;
            }
            attr->attributeCount = readu2(classData);
            classData += 2;
            attr->attributes = calloc(attr->attributeCount, sizeof(attribute_info_t *));
            if(!attr->attributes)
                goto fail4;
            classData = parseAttributes(attr->attributeCount, attr->attributes, class, classData);
            if(!classData)
                goto fail5;
            
            attributes[i] = (attribute_info_t *) attr;
            
            // continue looping
            continue;
            
            fail5:
            for(int j = 0; j < attr->attributeCount; ++j) {
                if(!attr->attributes[j])
                    break;
                free(attr->attributes[j]);
            }
            free(attr->attributes);
            fail4: free(attr->exceptionHandlers);
            fail3: free(attr->code);
            fail2: free(attr);
            fail1: return NULL;
        }
        else if(strcmp(name, "Signature") == 0) {
            // parse signature
            signature_attribute_t *attr = malloc(sizeof(signature_attribute_t));
            if(!attr)
                return NULL;
            attr->name = name;
            attr->signatureIndex = readu2(classData);
            attributes[i] = (attribute_info_t *) attr;
            classData += 2;
        }
        else {
            // skipped attribute
            skipped_attribute_t *attr = malloc(sizeof(skipped_attribute_t));
            if(!attr)
                return NULL;
            attr->name = name;
            attributes[i] = (attribute_info_t *) attr;
            classData += attributeLength;
        }
    }
    return classData;
}