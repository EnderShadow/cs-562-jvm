//
// Created by matthew on 10/28/19.
//

#ifndef JVM_ATTRIBUTES_H
#define JVM_ATTRIBUTES_H

#include "classfile.h"

void *parseAttributes(uint16_t length, attribute_info_t **attributes, void *classData);

#endif //JVM_ATTRIBUTES_H