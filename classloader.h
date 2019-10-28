//
// Created by matthew on 10/16/19.
//

#ifndef JVM_CLASSLOADER_H
#define JVM_CLASSLOADER_H

#include "classfile.h"
#include <stdbool.h>

bool initClassLoader();

bool addToClasspath(char *classpathLocation);

// Used specifically when parsing field, method parameter, and method return types
class_t *loadPrimitiveClass(char className);

class_t *loadClass(char *className);

#endif //JVM_CLASSLOADER_H
