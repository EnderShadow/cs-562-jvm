//
// Created by matthew on 10/26/19.
//

#ifndef JVM_CONSTANTPOOL_H
#define JVM_CONSTANTPOOL_H

#include "classfile.h"

void *parseConstantPool(class_t *class, void *classData);

uint8_t readu1(void *data);
uint16_t readu2(void *data);
uint32_t readu4(void *data);
uint64_t readu8(void *data);

#endif //JVM_CONSTANTPOOL_H