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
    // TODO create primitive class
    return NULL;
}

// Used specifically when parsing field, method parameter, and method return types
class_t *loadPrimitiveClass(char className) {
    pthread_mutex_lock(&classLoadingLock);
    class_t *class = loadPrimitiveClass0(className);
    pthread_mutex_unlock(&classLoadingLock);
    return class;
}

class_t *loadArrayClass(char *className) {
    // TODO load array class
    return NULL;
}

bool parseMethodDescriptor(method_t *method, class_t *class, char *methodDescriptor) {
    return false;
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
        char *descriptor = class->constantPool[descriptorIndex]->utf8Info.chars;
        if(descriptor[0] == 'L') {
            field->type.type = TYPE_REFERENCE;
            size_t descriptorLength = strlen(descriptor);
            descriptor[descriptorLength - 1] = '\0';
            field->type.classPointer = loadClass0(descriptor + 1);
            descriptor[descriptorLength - 1] = ';';
            if(!field->type.classPointer)
                goto fail4;
        }
        else if(descriptor[0] == '[') {
            field->type.type = TYPE_REFERENCE;
            field->type.classPointer = loadArrayClass(descriptor);
            if(!field->type.classPointer)
                goto fail4;
        }
        else {
            field->type.classPointer = loadPrimitiveClass0(descriptor[0]);
            if(!field->type.classPointer)
                goto fail4;
            switch(descriptor[0]) {
                case 'B':
                    field->type.type = TYPE_BYTE;
                    break;
                case 'C':
                    field->type.type = TYPE_CHAR;
                    break;
                case 'D':
                    field->type.type = TYPE_DOUBLE;
                    break;
                case 'F':
                    field->type.type = TYPE_FLOAT;
                    break;
                case 'I':
                    field->type.type = TYPE_INT;
                    break;
                case 'J':
                    field->type.type = TYPE_LONG;
                    break;
                case 'S':
                    field->type.type = TYPE_SHORT;
                    break;
                case 'Z':
                    field->type.type = TYPE_BOOLEAN;
                    break;
                default:
                    printf("Unknown field type: %s\n", descriptor);
                    goto fail4;
            }
        }
        field->numAttributes = readu2(classData + 6);
        classData += 8;
        field->attributes = calloc(field->numAttributes, sizeof(attribute_info_t *));
        if(!field->attributes)
            goto fail4;
        classData = parseAttributes(field->numAttributes, field->attributes, class, classData);
        if(!classData)
            goto fail4;
    }
    
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
        char *descriptor = class->constantPool[descriptorIndex]->utf8Info.chars;
        bool success = parseMethodDescriptor(method, class, descriptor);
        
        // TODO
    
        method->numAttributes = readu2(classData + 6);
        classData += 8;
        
        // TODO
    }
    
    // ============================================
    // parse attributes
    // ============================================
    
    // TODO
    
    class->status = CLASS_STATUS_LOADED;
    return class;
    
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
        if(class->status == CLASS_STATUS_LOADED)
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
    if(!classFile)
        munmap(classData, s.st_size);
    
    // TODO run <clinit>
    
    return classFile;
}

class_t *loadClass(char *className) {
    pthread_mutex_lock(&classLoadingLock);
    class_t *class = loadClass0(className);
    pthread_mutex_unlock(&classLoadingLock);
    return class;
}