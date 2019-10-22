#include <stdio.h>
#include <inttypes.h>
#include "stringutils.h"
#include "utils.h"
#include "jvmSettings.h"
#include "heap.h"
#include "gc.h"
#include "jthread.h"
#include "mm.h"
#include "classloader.h"
#include "flags.h"

slot_t convertToJavaString(char *arg) {
    size_t length = strlen(arg);
    if(length > INT32_MAX)
        return 0;
    
    // TODO convert to UTF-16
    
    class_t *stringClass = loadClass("java/lang/String");
    if(!stringClass)
        return 0;
    
    int32_t stringLength = (int32_t) length;
    slot_t charArraySlot = newArray(1, &stringLength, stringClass);
    if(!charArraySlot)
        return 0;
    
    array_object_t *charArray = (array_object_t *) getObject(charArraySlot);
    for(int i = 0; i < stringLength; i++) {
        cell_t cell = {.c = arg[i]};
        setArrayElement(charArray, i, cell);
    }
    
    slot_t stringSlot = newObject(stringClass);
    if(!stringSlot)
        return 0;
    
    object_t *stringObject = getObject(stringSlot);
    for(int i = 0; i < stringClass->numFields; i++) {
        field_t *valueField = stringClass->fields + i;
        if(!strcmp(valueField->name, "value") && valueField->type.type == TYPE_REFERENCE && valueField->type.classPointer == charArray->class) {
            uint32_t offset = valueField->objectOffset;
            slot_t *value = ((void *) stringObject) + offset;
            *value = charArraySlot;
            return stringSlot;
        }
    }
    
    // failed to find the value field which means something is wrong. This shouldn't happen
    return 0;
}

array_object_t *convertToJavaArgs(int numArgs, char **args) {
    class_t *stringClass = loadClass("java/lang/String");
    if(!stringClass)
        return NULL;
    
    slot_t stringArraySlot = newArray(1, &numArgs, stringClass);
    if(!stringArraySlot)
        return NULL;
    
    array_object_t *stringArray = (array_object_t *) getObject(stringArraySlot);
    for(int i = 0; i < numArgs; i++) {
        slot_t stringSlot = convertToJavaString(args[i]);
        if(!stringSlot)
            return NULL;
        cell_t cell = {.a = stringSlot};
        setArrayElement(stringArray, i, cell);
    }
    
    return stringArray;
}

int main(int argc, char **args) {
    char *classOrJar = NULL;
    bool isJar = false;
    int numArgs = 0;
    char **progArgs = NULL;
    
    if(argc <= 1) {
        printf("JVM [options] classfile [args]\n    [options] -jar jarfile [args]\n\nOptions:\n    -Xmx<size>\t\t\t\tsize in bytes of the heap\n    -Xss<size>\t\t\t\tsize in bytes of each thread's stack\n    -Xgci<millis>\t\t\tinterval between each garbage collection cycle\n\t-classpath=<classpath>\tadditional classpath to look for classes. Can be a directory, jar, or zip file. This option can be specified multiple times.\n\n\t<size> must be a multiple of 4096 bytes. It can be suffixed with k, m, or g to specify a size in kibibytes, mebibytes, or gibibytes");
        return 0;
    }
    
    // parse parameters
    for(int i = 1; i < argc; i++) {
        size_t strLen = strlen(args[i]);
        if(startsWith(args[i], "-Xmx")) {
            if(strLen > 4) {
                char suffix = args[i][strLen - 1];
                size_t numBytes;
                char *numEnd;
                if(suffix >= '0' && suffix <= '9')
                    numBytes = strtoumax(args[i] + 4, &numEnd, 10);
                else if(suffix == 'k' || suffix == 'K')
                    numBytes = KIBIBYTES(strtoumax(args[i] + 4, &numEnd, 10));
                else if(suffix == 'm' || suffix == 'M')
                    numBytes = MEBIBYTES(strtoumax(args[i] + 4, &numEnd, 10));
                else if(suffix == 'g' || suffix == 'G')
                    numBytes = GIBIBYTES(strtoumax(args[i] + 4, &numEnd, 10));
                else {
                    printf("Could not parse argument: %s", args[i]);
                    return 1;
                }
                if(numEnd < args[i] + strLen - 1) {
                    printf("Could not parse argument: %s", args[i]);
                    return 1;
                }
                if(numBytes & 0xFFFu) {
                    printf("Xmx must be a multiple of 4096\n");
                    return 1;
                }
                
                maxHeap = numBytes;
            }
            else {
                printf("Could not parse argument: %s", args[i]);
                return 1;
            }
        }
        else if(startsWith(args[i], "-Xss")) {
            if(strLen > 4) {
                char suffix = args[i][strLen - 1];
                size_t numBytes;
                char *numEnd;
                if(suffix >= '0' && suffix <= '9')
                    numBytes = strtoumax(args[i] + 4, &numEnd, 10);
                else if(suffix == 'k' || suffix == 'K')
                    numBytes = KIBIBYTES(strtoumax(args[i] + 4, &numEnd, 10));
                else if(suffix == 'm' || suffix == 'M')
                    numBytes = MEBIBYTES(strtoumax(args[i] + 4, &numEnd, 10));
                else if(suffix == 'g' || suffix == 'G')
                    numBytes = GIBIBYTES(strtoumax(args[i] + 4, &numEnd, 10));
                else {
                    printf("Could not parse argument: %s", args[i]);
                    return 1;
                }
                if(numEnd < args[i] + strLen - 1) {
                    printf("Could not parse argument: %s", args[i]);
                    return 1;
                }
                if(numBytes & 0xFFFu) {
                    printf("Xss must be a multiple of 4096\n");
                    return 1;
                }
        
                stackSize = numBytes;
            }
            else {
                printf("Could not parse argument: %s", args[i]);
                return 1;
            }
        }
        else if(startsWith(args[i], "-Xgci")) {
            if(strLen > 5) {
                char *numEnd;
                size_t numMillis = strtoumax(args[i] + 5, &numEnd, 10);
                if(numEnd < args[i] + strLen) {
                    printf("Could not parse argument: %s", args[i]);
                    return 1;
                }
        
                gcInterval = numMillis;
            }
            else {
                printf("Could not parse argument: %s", args[i]);
                return 1;
            }
        }
        else if(startsWith(args[i], "-classpath=")) {
            if(strLen > 11) {
                addToClasspath(args[i] + 11);
            }
            else {
                printf("Could not parse argument: %s", args[i]);
                return 1;
            }
        }
        else if(startsWith(args[i], "-jar")) {
            if(i + 1 < argc) {
                isJar = true;
                classOrJar = args[i + 1];
                numArgs = argc - i - 2;
                progArgs = args + i + 2;
                break;
            }
            else {
                printf("Jar flag was passed without specifying a jar file\n");
                return 1;
            }
        }
        else {
            classOrJar = args[i];
            numArgs = argc - i - 1;
            progArgs = args + i + 1;
            break;
        }
    }
    
    if(!classOrJar) {
        printf("No class or jar was provided\n");
        return 1;
    }
    
    if(!initClassLoader()) {
        printf("Failed to initialize class loader\n");
        return 1;
    }
    
    if(!initHeap()) {
        printf("Failed to initialize heap\n");
        return 1;
    }
    
    pthread_t *gcThread = initGC();
    if(!gcThread) {
        printf("Failed to start GC thread\n");
        return 1;
    }
    
    class_t *mainClass;
    if(isJar) {
        printf("Jar loading is not yet implemented\n");
        return 1;
    }
    else {
        mainClass = loadClass(classOrJar);
        if(!mainClass) {
            printf("Failed to find class: %s\n", classOrJar);
            return 1;
        }
    }
    
    array_object_t *javaArgs = convertToJavaArgs(numArgs, progArgs);
    
    method_t *main = NULL;
    for(int i = 0; i < mainClass->numMethods; i++) {
        method_t *method = mainClass->methods + i;
        if(!strcmp(method->name, "main") && (method->flags & METHOD_ACC_STATIC) && method->numParameters == 1 && method->parameterTypes[0].type == TYPE_REFERENCE && method->parameterTypes[0].classPointer == javaArgs->class) {
            main = method;
            break;
        }
    }
    if(!main) {
        printf("Failed to find main method\n");
        return 1;
    }
    
    jthread_t *mainThread = createThread("Main", main, (object_t *) javaArgs, stackSize);
    threadStart(mainThread);
    
    // Now we're done, so this thread can pause until all threads are done
    while(numThreads)
        sched_yield();
    
    return 0;
}