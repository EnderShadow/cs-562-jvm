//
// Created by matthew on 10/16/19.
//

#include "classloader.h"
#include <stdlib.h>

char **classpath = NULL;
size_t classpathLength = 0;
size_t classpathUsed = 0;

bool initClassLoader() {
    classpath = malloc(8 * sizeof(char *));
    if(!classpath)
        return false;
    classpathLength = 8;
    classpathUsed = 0;
}

bool addToClasspath(char *classpathLocation) {
    if(classpathUsed == classpathLength) {
        char **newClasspath = realloc(classpath, classpathLength * sizeof(char *) * 2);
        if(!newClasspath)
            return false;
        classpath = newClasspath;
        classpathLength *= 2;
    }
    
    classpath[classpathUsed++] = classpathLocation;
    return true;
}

class_t *loadClass(char *className) {
    // TODO load class
    return NULL;
}