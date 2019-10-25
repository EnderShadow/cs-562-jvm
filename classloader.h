//
// Created by matthew on 10/16/19.
//

#ifndef JVM_CLASSLOADER_H
#define JVM_CLASSLOADER_H

#include "classfile.h"
#include <stdbool.h>

#define E_CYCLIC_CLASS (void *) -1

bool initClassLoader();

bool addToClasspath(char *classpathLocation);

class_t *loadClass(char *className);

#endif //JVM_CLASSLOADER_H
