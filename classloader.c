//
// Created by matthew on 10/16/19.
//

#include "classloader.h"
#include <stdlib.h>
#include "hashmap.h"
#include <pthread.h>
#include "stringutils.h"
#include "constantpool.h"
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include "utils.h"
#include "flags.h"
#include "dataTypes.h"
#include "attributes.h"
#include "object.h"
#include "jthread.h"
#include "jvmSettings.h"

char **classpath = NULL;
int classpathLength = 0;
int classpathUsed = 0;
size_t maxClassPathLen = 0;

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

bool str_equality_fn(char *str1, char *str2) {
    return strcmp(str1, str2) == 0;
}

bool initClassLoader() {
    classpath = malloc(8 * sizeof(char *));
    if(!classpath)
        return false;
    // char * is the same size as void * so this cast just gets rid of the warning
    loadedClasses = ht_createHashmap((size_t (*)(void *)) &str_hash_fn, (bool (*)(void *, void *)) &str_equality_fn, 0.75f);
    if(!loadedClasses) {
        free(classpath);
        return false;
    }
    
    // default class paths
    classpath[0] = "./";
    classpath[1] = "runtime";
    classpathLength = 8;
    classpathUsed = 2;
    for(int i = 0; i < classpathUsed; ++i)
        maxClassPathLen = MAX(maxClassPathLen, strlen(classpath[i]));
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
    maxClassPathLen = MAX(maxClassPathLen, strlen(classpathLocation));
    return true;
}

void combinePath(char *dest, char *dir, char *subpath) {
    strcpy(dest, dir);
    if(!endsWith(dir, "/"))
        strcat(dest, "/");
    if(startsWith(subpath, "/"))
        subpath++;
    strcat(dest, subpath);
}

FILE *findClassFile(char *className) {
    size_t subPathLen = strlen(className) + 7;
    size_t maxPathLen = maxClassPathLen + subPathLen + 2;
    
    char *classSubPath = malloc(subPathLen);
    if(!classSubPath)
        return NULL;
    
    char *classLoc = malloc(maxPathLen);
    if(!classLoc) {
        free(classSubPath);
        return NULL;
    }
    
    strcpy(classSubPath, className);
    strcat(classSubPath, ".class");
    
    for(int i = 0; i < classpathUsed; i++) {
        combinePath(classLoc, classpath[i], classSubPath);
        FILE *f = fopen(classLoc, "r");
        if(f) {
            free(classLoc);
            free(classSubPath);
            return f;
        }
    }
    free(classLoc);
    free(classSubPath);
    return NULL;
}

// declared above loadArrayClass since both functions call each other
class_t *loadClass0(char *className);

class_t *loadPrimitiveClass0(char primitive) {
    char className[2] = {'\0', '\0'};
    className[0] = primitive;
    class_t *class = ht_get(loadedClasses, className);
    if(class)
        return class;
    
    class = calloc(1, sizeof(class_t));
    if(!class)
        return NULL;
    class->name = calloc(2, sizeof(char));
    if(!class->name) {
        free(class);
        return NULL;
    }
    class->name[0] = primitive;
    class->status = CLASS_STATUS_INITIALIZED;
    class->thisClass = class;
    class->flags = CLASS_ACC_FINAL | CLASS_ACC_PUBLIC | CLASS_ACC_SYNTHETIC;
    jlock_init(&class->jlock);
    ht_put(loadedClasses, class->name, class);
    return class;
}

// Used specifically when parsing field, method parameter, and method return types
class_t *loadPrimitiveClass(char className) {
    pthread_mutex_lock(&classLoadingLock);
    class_t *class = loadPrimitiveClass0(className);
    pthread_mutex_unlock(&classLoadingLock);
    return class;
}

class_t *loadArrayClass(char *className) {
    class_t *superclass = loadClass0("java/lang/Object");
    if(!superclass)
        return NULL;
    class_t *class = calloc(1, sizeof(class_t));
    if(!class)
        return NULL;
    class->name = malloc(strlen(className) + 1);
    if(!class->name) {
        free(class);
        return NULL;
    }
    strcpy(class->name, className);
    class->status = CLASS_STATUS_INITIALIZED;
    class->thisClass = class;
    class->superClass = superclass;
    class->objectSize = sizeof(object_t);
    class->flags = CLASS_ACC_FINAL | CLASS_ACC_PUBLIC | CLASS_ACC_SYNTHETIC;
    jlock_init(&class->jlock);
    ht_put(loadedClasses, class->name, class);
    return class;
}

int fieldCompare(const void *a, const void *b) {
    // comparison is done as b - a so that qsort sorts them in descending order
    return ((field_t *) b)->dataSize - ((field_t *) a)->dataSize;
}

class_t *parseClassFile(void *classData) {
    if(readu4(classData) != 0xCAFEBABEu) {
        printf("Invalid class magic: %X\n", readu4(classData));
        return NULL;
    }
    classData += 4;
    
    // skip major and minor version;
    classData += 4;
    
    class_t *class = malloc(sizeof(class_t));
    if(!class)
        return NULL;
    
    class->status = CLASS_STATUS_LOADING;
    
    // ============================================
    // parse constant pool
    // ============================================
    
    classData = parseConstantPool(class, classData);
    if(!classData)
        goto fail1;
    
    // ============================================
    // parse flags, this class, and super class
    // ============================================
    
    class->flags = readu2(classData);
    classData += 2;
    class->thisClass = class;
    
    // scope is so that the variable space isn't polluted with one-off variables
    {
        uint16_t thisIndex = readu2(classData);
        thisIndex = class->constantPool[thisIndex]->classInfo.nameIndex;
        class->name = class->constantPool[thisIndex]->utf8Info.chars;
        classData += 2;
    }
    
    ht_put(loadedClasses, class->name, class);
    
    uint16_t superClassIndex = readu2(classData);
    if(superClassIndex == 0) {
        class->superClass = NULL;
    }
    else {
        superClassIndex = class->constantPool[superClassIndex]->classInfo.nameIndex;
        char *superClassName = class->constantPool[superClassIndex]->utf8Info.chars;
        class->superClass = loadClass0(superClassName);
        if(!class->superClass)
            goto fail2;
    }
    classData += 2;
    
    // ============================================
    // parse interfaces
    // ============================================
    
    class->numInterfaces = readu2(classData);
    classData += 2;
    class->interfaces = malloc(sizeof(class_t *) * class->numInterfaces);
    if(!class->interfaces)
        goto fail2;
    for(size_t i = 0; i < class->numInterfaces; i++) {
        uint16_t index = readu2(classData + 2 * i);
        index = class->constantPool[index]->classInfo.nameIndex;
        char *interfaceName = class->constantPool[index]->utf8Info.chars;
        class->interfaces[i] = loadClass0(interfaceName);
        if(!class->interfaces[i])
            goto fail3;
    }
    classData += class->numInterfaces * 2;
    
    // ============================================
    // parse fields
    // ============================================
    
    class->numFields = readu2(classData);
    classData += 2;
    class->fields = calloc(class->numFields, sizeof(field_t));
    for(size_t i = 0; i < class->numFields; ++i) {
        field_t *field = class->fields + i;
        field->flags = readu2(classData);
        uint16_t nameIndex = readu2(classData + 2);
        field->name = class->constantPool[nameIndex]->utf8Info.chars;
        uint16_t descriptorIndex = readu2(classData + 4);
        field->descriptor = class->constantPool[descriptorIndex]->utf8Info.chars;
        field->class = class;
        field->dataSize = getSizeOfTypeFromFieldDescriptor(field->descriptor);
        field->numAttributes = readu2(classData + 6);
        classData += 8;
        field->attributes = calloc(field->numAttributes, sizeof(attribute_info_t *));
        if(!field->attributes)
            goto fail4;
        classData = parseAttributes(field->numAttributes, field->attributes, class, classData);
        if(!classData)
            goto fail4;
    }
    qsort(class->fields, class->numFields, sizeof(field_t), fieldCompare);
    
    // ============================================
    // parse methods
    // ============================================
    
    class->numMethods = readu2(classData);
    classData += 2;
    class->methods = calloc(class->numMethods, sizeof(method_t));
    if(!class->methods)
        goto fail4;
    for(int i = 0; i < class->numMethods; ++i) {
        method_t *method = class->methods + i;
        method->flags = readu2(classData);
        uint16_t nameIndex = readu2(classData + 2);
        method->name = class->constantPool[nameIndex]->utf8Info.chars;
        uint16_t descriptorIndex = readu2(classData + 4);
        method->descriptor = class->constantPool[descriptorIndex]->utf8Info.chars;
        method->class = class;
        method->numParameters = countNumParametersFromMethodDescriptor(method->descriptor, method->flags & METHOD_ACC_STATIC);
        method->numAttributes = readu2(classData + 6);
        classData += 8;
        method->attributes = calloc(method->numAttributes, sizeof(attribute_info_t *));
        if(!method->attributes)
            goto fail5;
        classData = parseAttributes(method->numAttributes, method->attributes, class, classData);
        if(!classData)
            goto fail5;
        
        if((method->flags & (METHOD_ACC_NATIVE | METHOD_ACC_ABSTRACT)) == 0) {
            for(int j = 0; j < method->numAttributes; ++j) {
                if(strcmp(method->attributes[j]->codeAttribute.name, "Code") == 0) {
                    method->codeAttribute = &(method->attributes[j]->codeAttribute);
                    break;
                }
            }
        }
    }
    
    // ============================================
    // parse class attributes
    // ============================================
    
    class->numAttributes = readu2(classData);
    classData += 2;
    class->attributes = calloc(class->numAttributes, sizeof(attribute_info_t *));
    if(!class->attributes)
        goto fail5;
    classData = parseAttributes(class->numAttributes, class->attributes, class, classData);
    if(!classData)
        goto fail6;
    
    class->staticDataSize = 0;
    if(class->superClass)
        class->objectSize = class->superClass->objectSize;
    else
        class->objectSize = sizeof(object_t);
    
    for(int i = 0; i < class->numFields; ++i) {
        field_t *field = class->fields + i;
        if(field->flags & FIELD_ACC_STATIC) {
            field->objectOffset = class->staticDataSize;
            class->staticDataSize += field->dataSize;
        }
        else {
            field->objectOffset = class->objectSize;
            class->objectSize += field->dataSize;
        }
    }
    class->staticFieldData = calloc(1, class->staticDataSize);
    if(!class->staticFieldData)
        goto fail6;
    
    class->status = CLASS_STATUS_LOADED;
    return class;
    
    fail6:
    for(int i = 0; i < class->numAttributes; ++i) {
        if(!class->attributes[i])
            break;
        free(class->attributes[i]);
    }
    free(class->attributes);
    fail5:
    for(int i = 0; i < class->numMethods; ++i) {
        method_t *method = class->methods + i;
        if(!method)
            break;
        for(int j = 0; j < method->numAttributes; ++j) {
            if(!method->attributes[j])
                break;
            free(method->attributes[j]);
        }
        free(method->attributes);
    }
    free(class->methods);
    fail4:
    for(int i = 0; i < class->numFields; ++i) {
        field_t *field = class->fields + i;
        if(!field)
            break;
        for(int j = 0; j < field->numAttributes; ++j) {
            if(!field->attributes[j])
                break;
            free(field->attributes[j]);
        }
        free(field->attributes);
    }
    free(class->fields);
    fail3: free(class->interfaces);
    fail2: ht_delete(loadedClasses, class->name);
    for(int i = 0; i < class->numConstants; i++) {
        if(class->constantPool[i]->utf8Info.tag == CONSTANT_utf8)
            free(class->constantPool[i]->utf8Info.chars);
        free(class->constantPool[i]);
    }
    free(class->constantPool);
    fail1: free(class);
    return NULL;
}

class_t *loadClass0(char *className) {
    // check if class is loaded
    class_t *class = ht_get(loadedClasses, className);
    if(class) {
        if(class->status != CLASS_STATUS_LOADING)
            return class;
        // the class was already partially loaded. This means cyclic classes
        printf("Failed to load class due to a cyclic dependency: %s\n", className);
        return NULL;
    }
    
    // check if we're loading an array class
    if(className[0] == '[')
        return loadArrayClass(className);
    
    FILE *file = findClassFile(className);
    struct stat s;
    if(fstat(fileno(file), &s) == -1) {
        printf("Failed to load class: %s\n", className);
        fclose(file);
        return NULL;
    }
    
    void *classData = mmap(NULL, s.st_size, PROT_READ, MAP_PRIVATE, fileno(file), 0);
    fclose(file);
    if(classData == MAP_FAILED) {
        printf("Failed to load class: %s\n", className);
        return NULL;
    }
    class_t *classFile = parseClassFile(classData);
    
    // I forget if I copied all relevant data out of the class file or not, so I might not need it mapped anymore, but just in case, I'll keep it.
    if(!classFile)
        munmap(classData, s.st_size);
    else
        jlock_init(&classFile->jlock);
    
    return classFile;
}

class_t *loadClass(char *className) {
    pthread_mutex_lock(&classLoadingLock);
    class_t *class = loadClass0(className);
    pthread_mutex_unlock(&classLoadingLock);
    return class;
}