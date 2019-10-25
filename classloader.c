//
// Created by matthew on 10/16/19.
//

#include "classloader.h"
#include <stdlib.h>
#include "hashmap.h"
#include <pthread.h>

char **classpath = NULL;
size_t classpathLength = 0;
size_t classpathUsed = 0;

hashmap_t *loadedClasses;

pthread_mutex_t classLoadingLock = PTHREAD_MUTEX_INITIALIZER;

size_t str_hash_fn(char *str) {
    // modified djb2 hash algorithm from http://www.cse.yorku.ca/~oz/hash.html
    size_t hash = 5381;
    unsigned char c;
    while((c = *str++))
        hash = ((hash << 5u) + hash) ^ c;
    return hash;
}

bool initClassLoader() {
    classpath = malloc(8 * sizeof(char *));
    if(!classpath)
        return false;
    // char * is the same size as void * so this cast just gets rid of the warning
    loadedClasses = ht_createHashmap((size_t (*)(void *)) &str_hash_fn, 0.75f);
    if(!loadedClasses) {
        free(classpath);
        return false;
    }
    
    // default class paths
    classpath[0] = "./";
    classpath[1] = "runtime";
    classpathLength = 8;
    classpathUsed = 2;
    return true;
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

class_t *loadClass0(char *className) {
    // check if class is loaded
    class_t *class = ht_get(loadedClasses, className);
    if(class) {
        if(class->status == CLASS_STATUS_LOADED)
            return class;
        // the class was already partially loaded. This means cyclic classes
        return E_CYCLIC_CLASS;
    }
    // TODO load class
    return NULL;
}

class_t *loadClass(char *className) {
    pthread_mutex_lock(&classLoadingLock);
    class_t *class = loadClass0(className);
    pthread_mutex_unlock(&classLoadingLock);
    return class;
}