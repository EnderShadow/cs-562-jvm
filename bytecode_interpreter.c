//
// Created by matthew on 10/11/19.
//

#include "bytecode_interpreter.h"
#include "mm.h"
#include "opcodes.h"
#include "gc.h"
#include "flags.h"
#include "utils.h"
#include "classloader.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

int run(bc_interpreter_t *interpreter) {
    jthread_t *jthread = interpreter->jthread;
    
    while(true) {
        // allows garbage collection to occur
        savePoint();
        
        uint8_t *bytecode = jthread->pc;
        uint8_t opcode = bytecode[0];
        
        int ret = instr_table[opcode](interpreter, false);
        if(ret > 0) {
            jthread->pc += ret;
        }
        else if(ret == -EJUST_RETURNED) {
            if(!interpreter->jthread->currentStackFrame)
                return 0;
        }
        else if(ret == -ETHREW_OFF_THREAD) {
            // TODO print exception
            return 1;
        }
    }
    
    return 0;
}

int8_t readByteOperand(jthread_t *jthread, int offset) {
    return *((uint8_t *) jthread->pc + offset);
}

int16_t readShortOperand(jthread_t *jthread, int offset) {
    int16_t value = *((uint8_t *) jthread->pc + offset);
    value = (value << 8) + *((uint8_t *) jthread->pc + offset + 1);
    return value;
}

int32_t readIntOperand(jthread_t *jthread, int offset) {
    int32_t value = *((uint8_t *) jthread->pc + offset);
    value = (value << 8) + *((uint8_t *) jthread->pc + offset + 1);
    value = (value << 8) + *((uint8_t *) jthread->pc + offset + 2);
    value = (value << 8) + *((uint8_t *) jthread->pc + offset + 3);
    return value;
}

bool initializeClass(bc_interpreter_t *interpreter, class_t *class);

/**
 * used for throwing exceptions that were not caused by the throw instruction
 * @param interpreter
 * @param exceptionClassName
 * @param exceptionMessage
 */
void throwException(bc_interpreter_t *interpreter, char *exceptionClassName, char *exceptionMessage) {
    class_t *exceptionClass = loadClass(exceptionClassName);
    if(!exceptionClass) {
        printf("OutOfMemoryError: Failed to load an internal exception class\n");
        exit(1);
    }
    bool initialized = initializeClass(interpreter, exceptionClass);
    if(!initialized) {
        printf("InternalError: Failed to initialize an internal exception class\n");
        exit(1);
    }
    slot_t exceptionSlot = newObject(exceptionClass);
    if(!exceptionSlot) {
        printf("OutOfMemoryError: Failed to allocate an instance of an internal exception class\n");
        exit(1);
    }
    slot_t messageSlot = convertToJavaString(exceptionMessage);
    
    // TODO call <init>(Ljava/lang/String;)V instead
    
    for(int i = 0; i < exceptionClass->numFields; ++i) {
        field_t *field = exceptionClass->fields + i;
        if(!(field->flags & FIELD_ACC_STATIC) && strcmp(field->descriptor, "Ljava/lang/String;") == 0) {
            *(slot_t *) ((void *) getObject(exceptionSlot) + field->objectOffset) = messageSlot;
            break;
        }
    }
    
    interpreter->exception = getObject(exceptionSlot);
}

bool initializeClass(bc_interpreter_t *interpreter, class_t *class) {
    if(!class)
        return true;
    
    // If we're in the middle of loading the class in this thread, treat the class as loaded
    if(class->status == CLASS_STATUS_INITIALIZING && class->jlock.owner == interpreter->jthread->id)
        return true;
    
    // busy loop to wait for the class to be loaded if another thread is loading it
    while(class->status == CLASS_STATUS_INITIALIZING);
    if(class->status == CLASS_STATUS_INITIALIZED)
        return true;
    
    jlock_lock(interpreter->jthread->id, &class->jlock);
    if(class->status == CLASS_STATUS_INITIALIZED) {
        jlock_unlock(interpreter->jthread->id, &class->jlock);
        return true;
    }
    
    class->status = CLASS_STATUS_INITIALIZING;
    
    if(!initializeClass(interpreter, class->superClass)) {
        class->status = CLASS_STATUS_LOADED;
        jlock_unlock(interpreter->jthread->id, &class->jlock);
        return false;
    }
    
    for(int i = 0; i < class->numFields; ++i) {
        field_t *field = class->fields + i;
        if((field->flags & FIELD_ACC_STATIC)) {
            for(int j = 0; j < field->numAttributes; ++j) {
                attribute_info_t *attr = field->attributes[j];
                if(strcmp(attr->constantValueAttribute.name, "ConstantValue") == 0) {
                    uint16_t index = attr->constantValueAttribute.constantIndex;
                    constant_info_t *constant = class->constantPool[index];
                    uint8_t tag = constant->longDoubleInfo.tag;
                    if(tag == CONSTANT_Integer || tag == CONSTANT_Float)
                        *(uint32_t *) (class->staticFieldData + field->objectOffset) = constant->integerFloatInfo.bytes;
                    else if(tag == CONSTANT_Long || tag == CONSTANT_Double)
                        *(uint64_t *) (class->staticFieldData + field->objectOffset) = constant->longDoubleInfo.bytes;
                    else if(tag == CONSTANT_String) {
                        uint16_t stringIndex = constant->stringInfo.stringIndex;
                        utf8_info_t *utfInfo = &class->constantPool[stringIndex]->utf8Info;
                        slot_t slot = convertToJavaString(utfInfo->chars);
                        if(slot == 0) {
                            class->status = CLASS_STATUS_LOADED;
                            throwException(interpreter, "java/lang/ExceptionInInitializerError", "Failed to load string constant");
                            jlock_unlock(interpreter->jthread->id, &class->jlock);
                            return false;
                        }
                        *(slot_t *) (class->staticFieldData + field->objectOffset) = slot;
                    }
                    break;
                }
            }
        }
    }
    
    method_t *clinit = NULL;
    for(int i = 0; i < class->numMethods; ++i) {
        method_t *method = class->methods + i;
        if((method->flags & METHOD_ACC_STATIC) && strcmp(method->name, "<clinit>") == 0 && strcmp(method->descriptor, "()V") == 0) {
            clinit = method;
            break;
        }
    }
    if(!clinit) {
        class->status = CLASS_STATUS_INITIALIZED;
        jlock_unlock(interpreter->jthread->id, &class->jlock);
        return true;
    }
    
    jthread_t *jthread = interpreter->jthread;
    stack_frame_t *currentFrame = jthread->currentStackFrame;
    void *currentPC = jthread->pc;
    stack_frame_t clinitFrame;
    clinitFrame.currentMethod = clinit;
    clinitFrame.previousStackFrame = NULL;
    clinitFrame.prevFramePC = NULL;
    clinitFrame.topOfStack = 0;
    clinitFrame.localVariableBase = currentFrame->operandStackBase + currentFrame->topOfStack;
    clinitFrame.operandStackTypeBase = (void *) (clinitFrame.localVariableBase + clinit->codeAttribute->maxLocals);
    clinitFrame.operandStackBase = (void *) (clinitFrame.operandStackTypeBase + clinit->codeAttribute->maxStack);
    jthread->currentStackFrame = &clinitFrame;
    jthread->pc = clinit->codeAttribute->code;
    
    int result = run(interpreter);
    jthread->currentStackFrame = currentFrame;
    jthread->pc = currentPC;
    if(result) {
        class->status = CLASS_STATUS_LOADED;
        jlock_unlock(interpreter->jthread->id, &class->jlock);
        return false;
    }
    class->status = CLASS_STATUS_INITIALIZED;
    jlock_unlock(interpreter->jthread->id, &class->jlock);
    return true;
}

field_t *resolveField0(bc_interpreter_t *interpreter, class_t *fieldClass, char *fieldName, char* descriptor, bool isStatic) {
    if(!fieldClass)
        return NULL;
    
    if(!initializeClass(interpreter, fieldClass)) {
        throwException(interpreter, "java/lang/ExceptionInInitializerError", "Failed to initialize class");
        return NULL;
    }
    
    field_t *field = NULL;
    for(int i = 0; i < fieldClass->numFields; ++i) {
        field_t *possibleField = fieldClass->fields + i;
        if((bool) (possibleField->flags & FIELD_ACC_STATIC) == isStatic && strcmp(possibleField->name, fieldName) == 0 && strcmp(possibleField->descriptor, descriptor) == 0) {
            field = possibleField;
            break;
        }
    }
    
    if(!isStatic && !field) {
        for(int i = 0; i < fieldClass->numInterfaces; ++i) {
            class_t *interface = fieldClass->interfaces[i];
            field = resolveField0(interpreter, interface, fieldName, descriptor, isStatic);
            if(field)
                break;
        }
        if(!field)
            field = resolveField0(interpreter, fieldClass->superClass, fieldName, descriptor, isStatic);
    }
    
    if(!field) {
        throwException(interpreter, "java/lang/IncompatibleClassChangeError", "No field found that matches the name and descriptor");
        return NULL;
    }
    
    return field;
}

field_t *resolveField(bc_interpreter_t *interpreter, uint16_t fieldIndex, bool isStatic) {
    constant_info_t **constantPool = interpreter->jthread->currentStackFrame->currentMethod->class->constantPool;
    field_method_interface_method_ref_info_t *fieldRef = &constantPool[fieldIndex]->fieldMethodInterfaceMethodRefInfo;
    
    name_and_type_info_t *nameAndTypeInfo = &constantPool[fieldRef->nameAndTypeIndex]->nameAndTypeInfo;
    char *fieldName = constantPool[nameAndTypeInfo->nameIndex]->utf8Info.chars;
    char *descriptor = constantPool[nameAndTypeInfo->descriptorIndex]->utf8Info.chars;
    
    class_info_t *fieldClassInfo = &constantPool[fieldRef->classIndex]->classInfo;
    char *className = constantPool[fieldClassInfo->nameIndex]->utf8Info.chars;
    
    class_t *fieldClass = loadClass(className);
    if(!fieldClass) {
        throwException(interpreter, "java/lang/NoClassDefFoundError", "Failed to load class");
        return NULL;
    }
    
    return resolveField0(interpreter, fieldClass, fieldName, descriptor, isStatic);
}

method_t *resolveMethod0(bc_interpreter_t *interpreter, class_t *methodClass, char *methodName, char* descriptor, bool isStatic) {

}

method_t *resolveMethod(bc_interpreter_t *interpreter, uint16_t methodIndex, bool isStatic) {
    constant_info_t **constantPool = interpreter->jthread->currentStackFrame->currentMethod->class->constantPool;
    field_method_interface_method_ref_info_t *methodRef = &constantPool[methodIndex]->fieldMethodInterfaceMethodRefInfo;
    
    name_and_type_info_t *nameAndTypeInfo = &constantPool[methodRef->nameAndTypeIndex]->nameAndTypeInfo;
    char *methodName = constantPool[nameAndTypeInfo->nameIndex]->utf8Info.chars;
    char *descriptor = constantPool[nameAndTypeInfo->descriptorIndex]->utf8Info.chars;
    
    class_info_t *methodClassInfo = &constantPool[methodRef->classIndex]->classInfo;
    char *className = constantPool[methodClassInfo->nameIndex]->utf8Info.chars;
    
    class_t *methodClass;
    if(isStatic) {
        methodClass = loadClass(className);
    }
    else {
        uint16_t numArgs = countNumParametersFromMethodDescriptor(descriptor, isStatic);
        object_t *obj = getObject(peekOperand(interpreter->jthread->currentStackFrame, numArgs - 1, NULL).a);
        methodClass = obj->class;
    }
    
    if(!methodClass) {
        throwException(interpreter, "java/lang/NoClassDefFoundError", "Failed to load class");
        return NULL;
    }
    
    return resolveMethod0(interpreter, methodClass, methodName, descriptor, isStatic);
}

int handle_instr_xload(bc_interpreter_t *interpreter, bool wide, uint8_t type) {
    jthread_t *jthread = interpreter->jthread;
    uint16_t index;
    if(wide)
        index = readShortOperand(jthread, 2);
    else
        index = (uint8_t) readByteOperand(jthread, 1);
    if(type == TYPE_LONG || type == TYPE_DOUBLE)
        pushOperand2(jthread->currentStackFrame, readLocal2(jthread->currentStackFrame, index, NULL), type);
    else
        pushOperand(jthread->currentStackFrame, readLocal(jthread->currentStackFrame, index, NULL), type);
    return wide ? 4 : 2;
}

int handle_instr_xstore(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint16_t index;
    if(wide)
        index = readShortOperand(jthread, 2);
    else
        index = (uint8_t) readByteOperand(jthread, 1);
    uint8_t type = peekOperandType(jthread->currentStackFrame, 0);
    if(type == TYPE_LONG || type == TYPE_DOUBLE)
        writeLocal2(jthread->currentStackFrame, index, popOperand2(jthread->currentStackFrame, NULL), type);
    else
        writeLocal(jthread->currentStackFrame, index, popOperand(jthread->currentStackFrame, NULL), type);
    return wide ? 4 : 2;
}

int handle_instr_xload_n(bc_interpreter_t *interpreter, uint16_t index, uint8_t type) {
    jthread_t *jthread = interpreter->jthread;
    if(type == TYPE_LONG || type == TYPE_DOUBLE)
        pushOperand2(jthread->currentStackFrame, readLocal2(jthread->currentStackFrame, index, NULL), type);
    else
        pushOperand(jthread->currentStackFrame, readLocal(jthread->currentStackFrame, index, NULL), type);
    return 1;
}

int handle_instr_xstore_n(bc_interpreter_t *interpreter, uint16_t index) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t type = peekOperandType(jthread->currentStackFrame, index);
    if(type == TYPE_LONG || type == TYPE_DOUBLE)
        writeLocal2(jthread->currentStackFrame, index, popOperand2(jthread->currentStackFrame, NULL), type);
    else
        writeLocal(jthread->currentStackFrame, index, popOperand(jthread->currentStackFrame, NULL), type);
    return 1;
}

int handle_instr_xaload(bc_interpreter_t *interpreter) {
    jthread_t *jthread = interpreter->jthread;
    int32_t index = popOperand(jthread->currentStackFrame, NULL).i;
    slot_t slot = popOperand(jthread->currentStackFrame, NULL).a;
    object_t *obj = getObject(slot);
    
    if(!obj) {
        throwException(interpreter, "java/lang/NullPointerException", "Array was null");
        return 0;
    }
    if(index < 0 || index >= obj->length) {
        throwException(interpreter, "java/lang/ArrayIndexOutOfBoundsException", "index was out of bounds for the array");
        return 0;
    }
    
    uint8_t type = getArrayElementType(obj);
    if(type == TYPE_LONG || type == TYPE_DOUBLE) {
        double_cell_t element = getArrayElement2(obj, index, NULL);
        pushOperand2(jthread->currentStackFrame, element, type);
    }
    else {
        cell_t element = getArrayElement(obj, index, NULL);
        pushOperand(jthread->currentStackFrame, element, type);
    }
    return 1;
}

int handle_instr_xastore(bc_interpreter_t *interpreter) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t type = peekOperandType(jthread->currentStackFrame, 0);
    if(type == TYPE_LONG || type == TYPE_DOUBLE) {
        double_cell_t value = popOperand2(jthread->currentStackFrame, NULL);
        int32_t index = popOperand(jthread->currentStackFrame, NULL).i;
        slot_t slot = popOperand(jthread->currentStackFrame, NULL).a;
        object_t *obj = getObject(slot);
    
        if(!obj) {
            throwException(interpreter, "java/lang/NullPointerException", "Array was null");
            return 0;
        }
        if(index < 0 || index >= obj->length) {
            throwException(interpreter, "java/lang/ArrayIndexOutOfBoundsException", "index was out of bounds for the array");
            return 0;
        }
    
        setArrayElement2(obj, index, value);
    }
    else {
        cell_t value = popOperand(jthread->currentStackFrame, NULL);
        int32_t index = popOperand(jthread->currentStackFrame, NULL).i;
        slot_t slot = popOperand(jthread->currentStackFrame, NULL).a;
        object_t *obj = getObject(slot);
    
        if(!obj) {
            throwException(interpreter, "java/lang/NullPointerException", "Array was null");
            return 0;
        }
        if(index < 0 || index >= obj->length) {
            throwException(interpreter, "java/lang/ArrayIndexOutOfBoundsException", "index was out of bounds for the array");
            return 0;
        }
    
        setArrayElement(obj, index, value);
    }
    return 1;
}

int handle_instr_unknown(bc_interpreter_t *interpreter, bool wide) {
    throwException(interpreter, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}

int handle_instr_nop(bc_interpreter_t *interpreter, bool wide) {
	return 1;
}

int handle_instr_aconst_null(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.a = 0};
	pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_REFERENCE);
	return 1;
}

int handle_instr_iconst_m1(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.i = -1};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_0(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.i = 0};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_1(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.i = 1};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_2(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.i = 2};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_3(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.i = 3};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_4(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.i = 4};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_5(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.i = 5};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_lconst_0(bc_interpreter_t *interpreter, bool wide) {
    double_cell_t cell = {.l = 0};
    pushOperand2(interpreter->jthread->currentStackFrame, cell, TYPE_LONG);
    return 1;
}

int handle_instr_lconst_1(bc_interpreter_t *interpreter, bool wide) {
    double_cell_t cell = {.l = 1};
    pushOperand2(interpreter->jthread->currentStackFrame, cell, TYPE_LONG);
    return 1;
}

int handle_instr_fconst_0(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.f = 0.0f};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_fconst_1(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.f = 1.0f};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_fconst_2(bc_interpreter_t *interpreter, bool wide) {
    cell_t cell = {.f = 2.0f};
    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_dconst_0(bc_interpreter_t *interpreter, bool wide) {
    double_cell_t cell = {.d = 0.0};
    pushOperand2(interpreter->jthread->currentStackFrame, cell, TYPE_DOUBLE);
    return 1;
}

int handle_instr_dconst_1(bc_interpreter_t *interpreter, bool wide) {
    double_cell_t cell = {.d = 1.0};
    pushOperand2(interpreter->jthread->currentStackFrame, cell, TYPE_DOUBLE);
    return 1;
}

int handle_instr_bipush(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t cell;
	cell.i = readByteOperand(jthread, 1);
	pushOperand(jthread->currentStackFrame, cell, TYPE_BYTE);
	return 2;
}

int handle_instr_sipush(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t cell;
    cell.i = readShortOperand(jthread, 1);
    pushOperand(jthread->currentStackFrame, cell, TYPE_SHORT);
    return 3;
}

int handle_instr_ldc(bc_interpreter_t *interpreter, bool wide) {
	uint8_t index = readByteOperand(interpreter->jthread, 1);
	constant_info_t *constant = interpreter->jthread->currentStackFrame->currentMethod->class->constantPool[index];
	uint8_t tag = constant->integerFloatInfo.tag;
	cell_t cell;
	if(tag == CONSTANT_Integer) {
	    cell.a = constant->integerFloatInfo.bytes;
	    pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
	}
	else if(tag == CONSTANT_Float) {
        cell.a = constant->integerFloatInfo.bytes;
        pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_FLOAT);
	}
	else if(tag == CONSTANT_String) {
	    uint16_t stringIndex = constant->stringInfo.stringIndex;
	    char *characters = interpreter->jthread->currentStackFrame->currentMethod->class->constantPool[stringIndex]->utf8Info.chars;
        cell.a = convertToJavaString(characters);
        if(!cell.a) {
            throwException(interpreter, "java/lang/OutOfMemoryError", "Failed to load string from constant pool");
            return 0;
        }
        pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_REFERENCE);
	}
    else {
        // TODO implement remaining constants
        throwException(interpreter, "java/lang/InternalError", "Unsupported ldc constant");
        return 0;
    }
	return 2;
}

int handle_instr_ldc_w(bc_interpreter_t *interpreter, bool wide) {
    uint16_t index = readShortOperand(interpreter->jthread, 1);
    constant_info_t *constant = interpreter->jthread->currentStackFrame->currentMethod->class->constantPool[index];
    uint8_t tag = constant->integerFloatInfo.tag;
    cell_t cell;
    if(tag == CONSTANT_Integer) {
        cell.a = constant->integerFloatInfo.bytes;
        pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_INT);
    }
    else if(tag == CONSTANT_Float) {
        cell.a = constant->integerFloatInfo.bytes;
        pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_FLOAT);
    }
    else if(tag == CONSTANT_String) {
        uint16_t stringIndex = constant->stringInfo.stringIndex;
        char *characters = interpreter->jthread->currentStackFrame->currentMethod->class->constantPool[stringIndex]->utf8Info.chars;
        cell.a = convertToJavaString(characters);
        if(!cell.a) {
            throwException(interpreter, "java/lang/OutOfMemoryError", "Failed to load string from constant pool");
            return 0;
        }
        pushOperand(interpreter->jthread->currentStackFrame, cell, TYPE_REFERENCE);
    }
    else {
        // TODO implement remaining constants
        throwException(interpreter, "java/lang/InternalError", "Unsupported ldc constant");
        return 0;
    }
    return 3;
}

int handle_instr_ldc2_w(bc_interpreter_t *interpreter, bool wide) {
    uint16_t index = readShortOperand(interpreter->jthread, 1);
    constant_info_t *constant = interpreter->jthread->currentStackFrame->currentMethod->class->constantPool[index];
    uint8_t tag = constant->longDoubleInfo.tag;
    double_cell_t cell;
    if(tag == CONSTANT_Long) {
        cell.l = constant->longDoubleInfo.bytes;
        pushOperand2(interpreter->jthread->currentStackFrame, cell, TYPE_LONG);
    }
    else if(tag == CONSTANT_Double) {
        cell.l = constant->longDoubleInfo.bytes;
        pushOperand2(interpreter->jthread->currentStackFrame, cell, TYPE_DOUBLE);
    }
    return 3;
}

int handle_instr_iload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload(interpreter, wide, TYPE_INT);
}

int handle_instr_lload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload(interpreter, wide, TYPE_LONG);
}

int handle_instr_fload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload(interpreter, wide, TYPE_FLOAT);
}

int handle_instr_dload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload(interpreter, wide, TYPE_DOUBLE);
}

int handle_instr_aload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload(interpreter, wide, TYPE_REFERENCE);
}

int handle_instr_iload_0(bc_interpreter_t *interpreter, bool wide) {
	return handle_instr_xload_n(interpreter, 0, TYPE_INT);
}

int handle_instr_iload_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 1, TYPE_INT);
}

int handle_instr_iload_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 2, TYPE_INT);
}

int handle_instr_iload_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 3, TYPE_INT);
}

int handle_instr_lload_0(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 0, TYPE_LONG);
}

int handle_instr_lload_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 1, TYPE_LONG);
}

int handle_instr_lload_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 2, TYPE_LONG);
}

int handle_instr_lload_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 3, TYPE_LONG);
}

int handle_instr_fload_0(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 0, TYPE_FLOAT);
}

int handle_instr_fload_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 1, TYPE_FLOAT);
}

int handle_instr_fload_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 2, TYPE_FLOAT);
}

int handle_instr_fload_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 3, TYPE_FLOAT);
}

int handle_instr_dload_0(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 0, TYPE_DOUBLE);
}

int handle_instr_dload_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 1, TYPE_DOUBLE);
}

int handle_instr_dload_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 2, TYPE_DOUBLE);
}

int handle_instr_dload_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 3, TYPE_DOUBLE);
}

int handle_instr_aload_0(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 0, TYPE_REFERENCE);
}

int handle_instr_aload_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 1, TYPE_REFERENCE);
}

int handle_instr_aload_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 2, TYPE_REFERENCE);
}

int handle_instr_aload_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xload_n(interpreter, 3, TYPE_REFERENCE);
}

int handle_instr_iaload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xaload(interpreter);
}

int handle_instr_laload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xaload(interpreter);
}

int handle_instr_faload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xaload(interpreter);
}

int handle_instr_daload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xaload(interpreter);
}

int handle_instr_aaload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xaload(interpreter);
}

int handle_instr_baload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xaload(interpreter);
}

int handle_instr_caload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xaload(interpreter);
}

int handle_instr_saload(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xaload(interpreter);
}

int handle_instr_istore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore(interpreter, wide);
}

int handle_instr_lstore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore(interpreter, wide);
}

int handle_instr_fstore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore(interpreter, wide);
}

int handle_instr_dstore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore(interpreter, wide);
}

int handle_instr_astore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore(interpreter, wide);
}

int handle_instr_istore_0(bc_interpreter_t *interpreter, bool wide) {
	return handle_instr_xstore_n(interpreter, 0);
}

int handle_instr_istore_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 1);
}

int handle_instr_istore_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 2);
}

int handle_instr_istore_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 3);
}

int handle_instr_lstore_0(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 0);
}

int handle_instr_lstore_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 1);
}

int handle_instr_lstore_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 2);
}

int handle_instr_lstore_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 3);
}

int handle_instr_fstore_0(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 0);
}

int handle_instr_fstore_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 1);
}

int handle_instr_fstore_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 2);
}

int handle_instr_fstore_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 3);
}

int handle_instr_dstore_0(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 0);
}

int handle_instr_dstore_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 1);
}

int handle_instr_dstore_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 2);
}

int handle_instr_dstore_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 3);
}

int handle_instr_astore_0(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 0);
}

int handle_instr_astore_1(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 1);
}

int handle_instr_astore_2(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 2);
}

int handle_instr_astore_3(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xstore_n(interpreter, 3);
}

int handle_instr_iastore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xastore(interpreter);
}

int handle_instr_lastore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xastore(interpreter);
}

int handle_instr_fastore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xastore(interpreter);
}

int handle_instr_dastore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xastore(interpreter);
}

int handle_instr_aastore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xastore(interpreter);
}

int handle_instr_bastore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xastore(interpreter);
}

int handle_instr_castore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xastore(interpreter);
}

int handle_instr_sastore(bc_interpreter_t *interpreter, bool wide) {
    return handle_instr_xastore(interpreter);
}

int handle_instr_pop(bc_interpreter_t *interpreter, bool wide) {
	--interpreter->jthread->currentStackFrame->topOfStack;
	return 1;
}

int handle_instr_pop2(bc_interpreter_t *interpreter, bool wide) {
    interpreter->jthread->currentStackFrame->topOfStack -= 2;
    return 1;
}

int handle_instr_dup(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t tosType;
	pushOperand(jthread->currentStackFrame, peekOperand(jthread->currentStackFrame, 0, &tosType), tosType);
	return 1;
}

int handle_instr_dup_x1(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t tosType;
    uint8_t nextType;
	cell_t top = popOperand(jthread->currentStackFrame, &tosType);
	cell_t next = popOperand(jthread->currentStackFrame, &nextType);
	pushOperand(jthread->currentStackFrame, top, tosType);
    pushOperand(jthread->currentStackFrame, next, nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    return 1;
}

int handle_instr_dup_x2(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t tosType;
    uint8_t nextType;
    uint8_t next2Type;
    cell_t top = popOperand(jthread->currentStackFrame, &tosType);
    cell_t next = popOperand(jthread->currentStackFrame, &nextType);
    cell_t next2 = popOperand(jthread->currentStackFrame, &next2Type);
    pushOperand(jthread->currentStackFrame, top, tosType);
    pushOperand(jthread->currentStackFrame, next2, next2Type);
    pushOperand(jthread->currentStackFrame, next, nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    return 1;
}

int handle_instr_dup2(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t tosType;
    uint8_t nextType;
    pushOperand(jthread->currentStackFrame, peekOperand(jthread->currentStackFrame, 1, &nextType), nextType);
    pushOperand(jthread->currentStackFrame, peekOperand(jthread->currentStackFrame, 1, &tosType), tosType);
    return 1;
}

int handle_instr_dup2_x1(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	uint8_t tosType;
	uint8_t nextType;
    uint8_t next2Type;
	cell_t top = popOperand(jthread->currentStackFrame, &tosType);
	cell_t next = popOperand(jthread->currentStackFrame, &nextType);
    cell_t next2 = popOperand(jthread->currentStackFrame, &next2Type);
    pushOperand(jthread->currentStackFrame, next, nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    pushOperand(jthread->currentStackFrame, next2, next2Type);
    pushOperand(jthread->currentStackFrame, next, nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    return 1;
}

int handle_instr_dup2_x2(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t tosType;
    uint8_t nextType;
    uint8_t nextType2;
    uint8_t nextType3;
    cell_t top = popOperand(jthread->currentStackFrame, &tosType);
    cell_t next = popOperand(jthread->currentStackFrame, &nextType);
    cell_t next2 = popOperand(jthread->currentStackFrame, &nextType2);
    cell_t next3 = popOperand(jthread->currentStackFrame, &nextType3);
    pushOperand(jthread->currentStackFrame, next, nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    pushOperand(jthread->currentStackFrame, next3, nextType3);
    pushOperand(jthread->currentStackFrame, next2, nextType2);
    pushOperand(jthread->currentStackFrame, next, nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    return 1;
}

int handle_instr_swap(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t tosType;
    uint8_t nextType;
    cell_t top = popOperand(jthread->currentStackFrame, &tosType);
    cell_t next = popOperand(jthread->currentStackFrame, &nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    pushOperand(jthread->currentStackFrame, next, nextType);
    return 1;
}

int handle_instr_iadd(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t top = popOperand(jthread->currentStackFrame, NULL);
	cell_t next = popOperand(jthread->currentStackFrame, NULL);
	top.i += next.i;
	pushOperand(jthread->currentStackFrame, top, TYPE_INT);
	return 1;
}

int handle_instr_ladd(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l += next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_fadd(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.f += next.f;
    pushOperand(jthread->currentStackFrame, top, TYPE_FLOAT);
    return 1;
}

int handle_instr_dadd(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.d += next.d;
    pushOperand2(jthread->currentStackFrame, top, TYPE_DOUBLE);
    return 1;
}

int handle_instr_isub(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.i -= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lsub(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.l -= top.l;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_fsub(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.f -= top.f;
    pushOperand(jthread->currentStackFrame, next, TYPE_FLOAT);
    return 1;
}

int handle_instr_dsub(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.d -= top.d;
    pushOperand2(jthread->currentStackFrame, next, TYPE_DOUBLE);
    return 1;
}

int handle_instr_imul(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.i *= next.i;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_lmul(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l *= next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_fmul(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.f *= next.f;
    pushOperand(jthread->currentStackFrame, top, TYPE_FLOAT);
    return 1;
}

int handle_instr_dmul(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.d *= next.d;
    pushOperand2(jthread->currentStackFrame, top, TYPE_DOUBLE);
    return 1;
}

int handle_instr_idiv(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    if(top.i == 0) {
        throwException(interpreter, "java/lang/ArithmeticException", "Cannot divide by 0");
        return 0;
    }
    next.i /= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_ldiv(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    if(top.l == 0) {
        throwException(interpreter, "java/lang/ArithmeticException", "Cannot divide by 0");
        return 0;
    }
    next.l /= top.l;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_fdiv(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.f /= top.f;
    pushOperand(jthread->currentStackFrame, next, TYPE_FLOAT);
    return 1;
}

int handle_instr_ddiv(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.d /= top.d;
    pushOperand2(jthread->currentStackFrame, next, TYPE_DOUBLE);
    return 1;
}

int handle_instr_irem(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    if(top.i == 0) {
        throwException(interpreter, "java/lang/ArithmeticException", "Cannot divide by 0");
        return 0;
    }
    next.i %= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lrem(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    if(top.l == 0) {
        throwException(interpreter, "java/lang/ArithmeticException", "Cannot divide by 0");
        return 0;
    }
    next.l %= top.l;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_frem(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.f = fmodf(next.f, top.f);
    pushOperand(jthread->currentStackFrame, next, TYPE_FLOAT);
    return 1;
}

int handle_instr_drem(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.d = fmod(next.d, top.d);
    pushOperand2(jthread->currentStackFrame, next, TYPE_DOUBLE);
    return 1;
}

int handle_instr_ineg(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t top = popOperand(jthread->currentStackFrame, NULL);
	top.i = -top.i;
	pushOperand(jthread->currentStackFrame, top, TYPE_INT);
	return 1;
}

int handle_instr_lneg(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    top.l = -top.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_fneg(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    top.f = -top.f;
    pushOperand(jthread->currentStackFrame, top, TYPE_FLOAT);
    return 1;
}

int handle_instr_dneg(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    top.d = -top.d;
    pushOperand2(jthread->currentStackFrame, top, TYPE_DOUBLE);
    return 1;
}

int handle_instr_ishl(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.i <<= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lshl(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.l <<= top.i;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_ishr(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.i >>= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lshr(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.l >>= top.i;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_iushr(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.i = ((uint32_t) next.i) >> top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lushr(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.l = ((uint64_t) next.l) >> top.i;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_iand(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.i &= next.i;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_land(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l &= next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_ior(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.i |= next.i;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_lor(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l |= next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_ixor(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.i ^= next.i;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_lxor(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l ^= next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_iinc(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	uint16_t index;
	int16_t amt;
	if(wide) {
	    index = readShortOperand(jthread, 2);
	    amt = readShortOperand(jthread, 4);
	}
	else {
	    index = (uint8_t) readByteOperand(jthread, 1);
	    amt = readByteOperand(jthread, 2);
	}
	cell_t cell = readLocal(jthread->currentStackFrame, index, NULL);
	cell.i += amt;
	writeLocal(jthread->currentStackFrame, index, cell, TYPE_INT);
    return wide ? 6 : 3;
}

int handle_instr_i2l(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t cell = popOperand(jthread->currentStackFrame, NULL);
	double_cell_t dcell = {.l = cell.i};
	pushOperand2(jthread->currentStackFrame, dcell, TYPE_LONG);
    return 1;
}

int handle_instr_i2f(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t cell = popOperand(jthread->currentStackFrame, NULL);
	cell.f = cell.i;
	pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_i2d(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t dcell = {.d = cell.i};
    pushOperand2(jthread->currentStackFrame, dcell, TYPE_DOUBLE);
    return 1;
}

int handle_instr_l2i(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
	cell_t cell = {.i = dcell.l};
	pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_l2f(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
    cell_t cell = {.f = dcell.l};
    pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_l2d(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
    dcell.d = dcell.l;
    pushOperand2(jthread->currentStackFrame, dcell, TYPE_INT);
    return 1;
}

int handle_instr_f2i(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    cell.i = cell.f;
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_f2l(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t dcell = {.l = cell.f};
    pushOperand2(jthread->currentStackFrame, dcell, TYPE_LONG);
    return 1;
}

int handle_instr_f2d(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t dcell = {.d = cell.f};
    pushOperand2(jthread->currentStackFrame, dcell, TYPE_DOUBLE);
    return 1;
}

int handle_instr_d2i(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
    cell_t cell = {.i = dcell.d};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_d2l(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
	dcell.l = dcell.d;
	pushOperand2(jthread->currentStackFrame, dcell, TYPE_LONG);
    return 1;
}

int handle_instr_d2f(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
    cell_t cell = {.f = dcell.d};
    pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_i2b(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    cell.i = (int8_t) cell.i;
    pushOperand(jthread->currentStackFrame, cell, TYPE_BYTE);
    return 1;
}

int handle_instr_i2c(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    cell.i = cell.i & 0xFFFF;
    pushOperand(jthread->currentStackFrame, cell, TYPE_CHAR);
    return 1;
}

int handle_instr_i2s(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    cell.i = (int16_t) cell.i;
    pushOperand(jthread->currentStackFrame, cell, TYPE_SHORT);
    return 1;
}

int handle_instr_lcmp(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    cell_t result = {.i = MAX(-1, MIN(1, next.l - top.l))};
    pushOperand(jthread->currentStackFrame, result, TYPE_INT);
    return 1;
}

int handle_instr_fcmpl(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t top = popOperand(jthread->currentStackFrame, NULL);
	cell_t next = popOperand(jthread->currentStackFrame, NULL);
	if(isnanf(top.f) || isnanf(next.f))
	    top.i = -1;
	else if(next.f > top.f)
	    top.i = 1;
	else if(next.f < top.f)
	    top.i = -1;
	else
	    top.i = 0;
	pushOperand(jthread->currentStackFrame, top, TYPE_INT);
	return 1;
}

int handle_instr_fcmpg(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    if(isnanf(top.f) || isnanf(next.f))
        top.i = 1;
    else if(next.f > top.f)
        top.i = 1;
    else if(next.f < top.f)
        top.i = -1;
    else
        top.i = 0;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_dcmpl(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    cell_t result;
    if(isnan(top.d) || isnan(next.d))
        result.i = -1;
    else if(next.d > top.d)
        result.i = 1;
    else if(next.d < top.d)
        result.i = -1;
    else
        result.i = 0;
    pushOperand(jthread->currentStackFrame, result, TYPE_INT);
    return 1;
}

int handle_instr_dcmpg(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    cell_t result;
    if(isnan(top.d) || isnan(next.d))
        result.i = 1;
    else if(next.d > top.d)
        result.i = 1;
    else if(next.d < top.d)
        result.i = -1;
    else
        result.i = 0;
    pushOperand(jthread->currentStackFrame, result, TYPE_INT);
    return 1;
}

int handle_instr_ifeq(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t top = popOperand(jthread->currentStackFrame, NULL);
	int16_t offset = readShortOperand(jthread, 1);
	if(top.i == 0) {
	    jthread->pc += offset;
	    return 0;
	}
	return 3;
}

int handle_instr_ifne(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i != 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_iflt(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i < 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_ifge(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i >= 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_ifgt(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i > 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_ifle(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i <= 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmpeq(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i == top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmpne(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i != top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmplt(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i < top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmpge(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i >= top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmpgt(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i > top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmple(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i <= top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_acmpeq(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.a == top.a) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_acmpne(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.a != top.a) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_goto(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    int16_t offset = readShortOperand(jthread, 1);
    jthread->pc += offset;
    return 0;
}

int handle_instr_jsr(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    int16_t offset = readShortOperand(jthread, 1);
    cell_t returnAddress =  {.r = jthread->pc + 1 - jthread->currentStackFrame->currentMethod->codeAttribute->code};
    jthread->pc += offset;
    pushOperand(jthread->currentStackFrame, returnAddress, TYPE_RETURN_ADDRESS);
    return 0;
}

int handle_instr_ret(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	uint16_t index;
	if(wide)
	    index = readShortOperand(jthread, 2);
	else
	    index = (uint8_t) readByteOperand(jthread, 1);
	cell_t returnAddress = readLocal(jthread->currentStackFrame, index, NULL);
	jthread->pc = jthread->currentStackFrame->currentMethod->codeAttribute->code + returnAddress.r;
	return 0;
}

int handle_instr_tableswitch(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    // calculate the offset of the integers stored in the tableswitch
    int offset = (((intptr_t) jthread->pc & ~3) + 4) - (intptr_t) jthread->pc;
    
    int32_t defaultOffset = readIntOperand(jthread, offset);
    int32_t low = readIntOperand(jthread, offset + 4);
    int32_t high = readIntOperand(jthread, offset + 8);
    cell_t index = popOperand(jthread->currentStackFrame, NULL);
    if(index.i < low || index.i > high) {
        jthread->pc += defaultOffset;
    }
    else {
        int32_t addressOffset = readIntOperand(jthread, offset + 12 + index.i - low);
        jthread->pc += addressOffset;
    }
    return 0;
}

int lookupswitch_binary_search(jthread_t *jthread, int base, int start, int end, int32_t key) {
    if(start >= end)
        return -1;
    int mid = start + (end - start) / 2;
    int32_t testKey = readIntOperand(jthread, base + 8 * mid);
    if(testKey == key)
        return mid;
    else if(testKey < key)
        return lookupswitch_binary_search(jthread, base, mid, end, key);
    else
        return lookupswitch_binary_search(jthread, base, start, mid, key);
}

int handle_instr_lookupswitch(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    // calculate the offset of the integers stored in the tableswitch
    int offset = (((intptr_t) jthread->pc & ~3) + 4) - (intptr_t) jthread->pc;
    
    int32_t defaultOffset = readIntOperand(jthread, offset);
    int32_t npairs = readIntOperand(jthread, offset + 4);
    cell_t key = popOperand(jthread->currentStackFrame, NULL);
    int pairStart = offset + 8;
    int location = lookupswitch_binary_search(jthread, pairStart, 0, npairs, key.i);
    if(location < 0) {
        jthread->pc += defaultOffset;
    }
    else {
        int32_t addressOffset = readIntOperand(jthread, pairStart + 8 * location + 4);
        jthread->pc += addressOffset;
    }
    return 0;
}

int handle_instr_ireturn(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t returnValue = popOperand(jthread->currentStackFrame, NULL);
	jthread->pc = jthread->currentStackFrame->prevFramePC;
	jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
	pushOperand(jthread->currentStackFrame, returnValue, TYPE_INT);
	return 0;
}

int handle_instr_lreturn(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t returnValue = popOperand2(jthread->currentStackFrame, NULL);
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    pushOperand2(jthread->currentStackFrame, returnValue, TYPE_LONG);
    return 0;
}

int handle_instr_freturn(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t returnValue = popOperand(jthread->currentStackFrame, NULL);
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    pushOperand(jthread->currentStackFrame, returnValue, TYPE_FLOAT);
    return 0;
}

int handle_instr_dreturn(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    double_cell_t returnValue = popOperand2(jthread->currentStackFrame, NULL);
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    pushOperand2(jthread->currentStackFrame, returnValue, TYPE_DOUBLE);
    return 0;
}

int handle_instr_areturn(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t returnValue = popOperand(jthread->currentStackFrame, NULL);
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    pushOperand(jthread->currentStackFrame, returnValue, TYPE_REFERENCE);
    return 0;
}

int handle_instr_return(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    
    // only void methods can be the root method of a thread
    return -EJUST_RETURNED;
}

int handle_instr_getstatic(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	uint16_t fieldIndex = readShortOperand(jthread, 1);
	field_t *field = resolveField(interpreter, fieldIndex, true);
	if(!field)
	    // exception was already thrown by resolveField
	    return 0;
	
	void *data = field->class->staticFieldData + field->objectOffset;
	cell_t cell;
	double_cell_t dcell;
	switch(field->descriptor[0]) {
        case 'B':
            cell.b = *(int8_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_BYTE);
            break;
        case 'C':
            cell.c = *(uint16_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_CHAR);
            break;
        case 'D':
            dcell.d = *(double *) data;
            pushOperand2(jthread->currentStackFrame, dcell, TYPE_DOUBLE);
            break;
        case 'F':
            cell.f = *(float *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
            break;
        case 'I':
            cell.i = *(int32_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
            break;
        case 'J':
            dcell.l = *(int64_t *) data;
            pushOperand2(jthread->currentStackFrame, dcell, TYPE_LONG);
            break;
        case 'S':
            cell.s = *(int16_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_SHORT);
            break;
        case 'Z':
            cell.z = *(uint8_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_BOOLEAN);
            break;
        case 'L':
        case '[':
        default:
            cell.a = *(slot_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_REFERENCE);
            break;
	}
	
	return 3;
}

int handle_instr_putstatic(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint16_t fieldIndex = readShortOperand(jthread, 1);
    field_t *field = resolveField(interpreter, fieldIndex, true);
    if(!field)
        // exception has already been thrown by resolveField
        return 0;
    
    void *data = field->class->staticFieldData + field->objectOffset;
    switch(field->descriptor[0]) {
        case 'B':
            *(int8_t *) data = popOperand(jthread->currentStackFrame, NULL).b;
            break;
        case 'C':
            *(uint16_t *) data = popOperand(jthread->currentStackFrame, NULL).c;
            break;
        case 'D':
            *(double *) data = popOperand2(jthread->currentStackFrame, NULL).d;
            break;
        case 'F':
            *(float *) data = popOperand(jthread->currentStackFrame, NULL).f;
            break;
        case 'I':
            *(int32_t *) data = popOperand(jthread->currentStackFrame, NULL).i;
            break;
        case 'J':
            *(int64_t *) data = popOperand2(jthread->currentStackFrame, NULL).l;
            break;
        case 'S':
            *(int16_t *) data = popOperand(jthread->currentStackFrame, NULL).s;
            break;
        case 'Z':
            *(uint8_t *) data = popOperand(jthread->currentStackFrame, NULL).z;
            break;
        case 'L':
        case '[':
        default:
            *(slot_t *) data = popOperand(jthread->currentStackFrame, NULL).a;
            break;
    }
    
    return 3;
}

int handle_instr_getfield(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    object_t *obj = getObject(popOperand(jthread->currentStackFrame, NULL).a);
    if(!obj) {
        throwException(interpreter, "java/lang/NullPointerException", "Cannot retrieve field from null");
        return 0;
    }
    
    uint16_t fieldIndex = readShortOperand(jthread, 1);
    field_t *field = resolveField(interpreter, fieldIndex, false);
    if(!field)
        // exception was already thrown by resolveField
        return 0;
    
    void *data = obj + field->objectOffset;
    cell_t cell;
    double_cell_t dcell;
    switch(field->descriptor[0]) {
        case 'B':
            cell.b = *(int8_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_BYTE);
            break;
        case 'C':
            cell.c = *(uint16_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_CHAR);
            break;
        case 'D':
            dcell.d = *(double *) data;
            pushOperand2(jthread->currentStackFrame, dcell, TYPE_DOUBLE);
            break;
        case 'F':
            cell.f = *(float *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
            break;
        case 'I':
            cell.i = *(int32_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
            break;
        case 'J':
            dcell.l = *(int64_t *) data;
            pushOperand2(jthread->currentStackFrame, dcell, TYPE_LONG);
            break;
        case 'S':
            cell.s = *(int16_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_SHORT);
            break;
        case 'Z':
            cell.z = *(uint8_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_BOOLEAN);
            break;
        case 'L':
        case '[':
        default:
            cell.a = *(slot_t *) data;
            pushOperand(jthread->currentStackFrame, cell, TYPE_REFERENCE);
            break;
    }
    
    return 3;
}

int handle_instr_putfield(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    object_t *obj = getObject(popOperand(jthread->currentStackFrame, NULL).a);
    if(!obj) {
        throwException(interpreter, "java/lang/NullPointerException", "Cannot retrieve field from null");
        return 0;
    }
    
    uint16_t fieldIndex = readShortOperand(jthread, 1);
    field_t *field = resolveField(interpreter, fieldIndex, false);
    if(!field)
        // exception has already been thrown by resolveField
        return 0;
    
    void *data = obj + field->objectOffset;
    switch(field->descriptor[0]) {
        case 'B':
            *(int8_t *) data = popOperand(jthread->currentStackFrame, NULL).b;
            break;
        case 'C':
            *(uint16_t *) data = popOperand(jthread->currentStackFrame, NULL).c;
            break;
        case 'D':
            *(double *) data = popOperand2(jthread->currentStackFrame, NULL).d;
            break;
        case 'F':
            *(float *) data = popOperand(jthread->currentStackFrame, NULL).f;
            break;
        case 'I':
            *(int32_t *) data = popOperand(jthread->currentStackFrame, NULL).i;
            break;
        case 'J':
            *(int64_t *) data = popOperand2(jthread->currentStackFrame, NULL).l;
            break;
        case 'S':
            *(int16_t *) data = popOperand(jthread->currentStackFrame, NULL).s;
            break;
        case 'Z':
            *(uint8_t *) data = popOperand(jthread->currentStackFrame, NULL).z;
            break;
        case 'L':
        case '[':
        default:
            *(slot_t *) data = popOperand(jthread->currentStackFrame, NULL).a;
            break;
    }
    
    return 3;
}

int handle_instr_invokevirtual(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint16_t methodIndex = readShortOperand(jthread, 1);
	method_t *method = resolveMethod(interpreter, methodIndex, false);
    return 3;
}

int handle_instr_invokespecial(bc_interpreter_t *interpreter, bool wide) {
    return 3;
}

int handle_instr_invokestatic(bc_interpreter_t *interpreter, bool wide) {
	return 3;
}

int handle_instr_invokeinterface(bc_interpreter_t *interpreter, bool wide) {
	return 5;
}

int handle_instr_invokedynamic(bc_interpreter_t *interpreter, bool wide) {
    throwException(interpreter, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}

int handle_instr_new(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t classIndex = readShortOperand(jthread, 1);
    
    uint16_t nameIndex = jthread->currentStackFrame->currentMethod->class->constantPool[classIndex]->classInfo.nameIndex;
    char *className = jthread->currentStackFrame->currentMethod->class->constantPool[nameIndex]->utf8Info.chars;
    class_t *class = loadClass(className);
    if(!class) {
        throwException(interpreter, "NoClassDefFoundError", "Failed to find class");
        return 0;
    }
    if(!initializeClass(interpreter, class)) {
        throwException(interpreter, "java/lang/ExceptionInInitializerError", "Failed to initialize class");
        return 0;
    }
    
    cell_t cell;
    cell.a = newObject(class);
    if(!cell.a) {
        throwException(interpreter, "java/lang/OutOfMemoryError", "Failed to create array");
        return 0;
    }
    pushOperand(jthread->currentStackFrame, cell, TYPE_REFERENCE);
    return 3;
}

int handle_instr_newarray(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t arrayType = readByteOperand(jthread, 1);
    char c;
    switch(arrayType) {
        case TYPE_BOOLEAN:
            c = 'Z';
            break;
        case TYPE_CHAR:
            c = 'C';
            break;
        case TYPE_FLOAT:
            c = 'F';
            break;
        case TYPE_DOUBLE:
            c = 'D';
            break;
        case TYPE_BYTE:
            c = 'B';
            break;
        case TYPE_SHORT:
            c = 'S';
            break;
        case TYPE_INT:
            c = 'I';
            break;
        case TYPE_LONG:
            c = 'J';
            break;
        default:
            c = 'I';
            break;
    }
    
    class_t *class = loadPrimitiveClass(c);
    if(!class) {
        throwException(interpreter, "NoClassDefFoundError", "Failed to load primitive class");
        return 0;
    }
    if(!initializeClass(interpreter, class)) {
        throwException(interpreter, "java/lang/ExceptionInInitializerError", "Failed to initialize class");
        return 0;
    }
    
    int32_t size = popOperand(jthread->currentStackFrame, NULL).i;
    
    cell_t cell;
    cell.a = newArray(1, &size, class);
    if(!cell.a) {
        throwException(interpreter, "java/lang/OutOfMemoryError", "Failed to create array");
        return 0;
    }
    pushOperand(jthread->currentStackFrame, cell, TYPE_REFERENCE);
    return 2;
}

int handle_instr_anewarray(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    uint8_t classIndex = readShortOperand(jthread, 1);
    
    uint16_t nameIndex = jthread->currentStackFrame->currentMethod->class->constantPool[classIndex]->classInfo.nameIndex;
    char *className = jthread->currentStackFrame->currentMethod->class->constantPool[nameIndex]->utf8Info.chars;
    class_t *class = loadClass(className);
    if(!class) {
        throwException(interpreter, "NoClassDefFoundError", "Failed to find class");
        return 0;
    }
    if(!initializeClass(interpreter, class)) {
        throwException(interpreter, "java/lang/ExceptionInInitializerError", "Failed to initialize class");
        return 0;
    }
    
    int32_t size = popOperand(jthread->currentStackFrame, NULL).i;
    
    cell_t cell;
    cell.a = newArray(1, &size, class);
    if(!cell.a) {
        throwException(interpreter, "java/lang/OutOfMemoryError", "Failed to create array");
        return 0;
    }
    pushOperand(jthread->currentStackFrame, cell, TYPE_REFERENCE);
    return 3;
}

int handle_instr_arraylength(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	cell_t cell = popOperand(jthread->currentStackFrame, NULL);
	object_t *array = getObject(cell.a);
	if(!array) {
	    throwException(interpreter, "java/lang/NullPointerException", "Array cannot be null");
	    return 0;
	}
	cell.i = array->length;
	pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
}

int handle_instr_athrow(bc_interpreter_t *interpreter, bool wide) {
    slot_t slot = popOperand(interpreter->jthread->currentStackFrame, NULL).a;
    if(!slot) {
        throwException(interpreter, "java/lang/NullPointerException", "Cannot throw null");
        return 0;
    }
    object_t *obj = getObject(slot);
    interpreter->exception = obj;
    return 0;
}

int handle_instr_checkcast(bc_interpreter_t *interpreter, bool wide) {
    throwException(interpreter, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}

int handle_instr_instanceof(bc_interpreter_t *interpreter, bool wide) {
    throwException(interpreter, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}

int handle_instr_monitorenter(bc_interpreter_t *interpreter, bool wide) {
	slot_t slot = popOperand(interpreter->jthread->currentStackFrame, NULL).a;
	if(!slot) {
	    throwException(interpreter, "java/lang/NullPointerException", "Cannot enter a monitor on a null object");
	    return 0;
	}
	object_t *obj = getObject(slot);
	jlock_lock(interpreter->jthread->id, &obj->jlock);
	return 1;
}

int handle_instr_monitorexit(bc_interpreter_t *interpreter, bool wide) {
    slot_t slot = popOperand(interpreter->jthread->currentStackFrame, NULL).a;
    if(!slot) {
        throwException(interpreter, "java/lang/NullPointerException", "Cannot exit a monitor on a null object");
        return 0;
    }
    object_t *obj = getObject(slot);
    if(interpreter->jthread->id != obj->jlock.owner) {
        throwException(interpreter, "java/lang/IllegalMonitorStateException", "Cannot exit a monitor that you do not own");
        return 0;
    }
    jlock_unlock(interpreter->jthread->id, &obj->jlock);
    return 1;
}

int handle_instr_wide(bc_interpreter_t *interpreter, bool wide) {
    return instr_table[(uint8_t) readByteOperand(interpreter->jthread, 1)](interpreter, true);
}

int handle_instr_multianewarray(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
	uint16_t classIndex = readShortOperand(jthread, 1);
	uint8_t numDimensions = readByteOperand(jthread, 3);
	
	uint16_t nameIndex = jthread->currentStackFrame->currentMethod->class->constantPool[classIndex]->classInfo.nameIndex;
	char *className = jthread->currentStackFrame->currentMethod->class->constantPool[nameIndex]->utf8Info.chars;
	class_t *class = loadClass(className);
	if(!class) {
	    throwException(interpreter, "NoClassDefFoundError", "Failed to find class");
	    return 0;
	}
	if(!initializeClass(interpreter, class)) {
        throwException(interpreter, "java/lang/ExceptionInInitializerError", "Failed to initialize class");
	    return 0;
	}
	
	int32_t *sizes = (int32_t *) (jthread->currentStackFrame->operandStackBase + jthread->currentStackFrame->topOfStack - numDimensions);
	
	cell_t cell;
	cell.a = newArray(numDimensions, sizes, class);
	if(!cell.a) {
	    throwException(interpreter, "java/lang/OutOfMemoryError", "Failed to create array");
	    return 0;
	}
	jthread->currentStackFrame->topOfStack -= numDimensions;
	pushOperand(jthread->currentStackFrame, cell, TYPE_REFERENCE);
    return 4;
}

int handle_instr_ifnull(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.a == 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_ifnonnull(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.a != 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_goto_w(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    int32_t offset = readIntOperand(jthread, 1);
    jthread->pc += offset;
    return 0;
}

int handle_instr_jsr_w(bc_interpreter_t *interpreter, bool wide) {
    jthread_t *jthread = interpreter->jthread;
    int32_t offset = readIntOperand(jthread, 1);
    cell_t returnAddress =  {.r = jthread->pc + 1 - jthread->currentStackFrame->currentMethod->codeAttribute->code};
    jthread->pc += offset;
    pushOperand(jthread->currentStackFrame, returnAddress, TYPE_RETURN_ADDRESS);
    return 0;
}

int handle_instr_breakpoint(bc_interpreter_t *interpreter, bool wide) {
    throwException(interpreter, "java/lang/InternalError", "Unimplemented Instruction");
	return 0;
}

int handle_instr_impdep1(bc_interpreter_t *interpreter, bool wide) {
    throwException(interpreter, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}

int handle_instr_impdep2(bc_interpreter_t *interpreter, bool wide) {
    throwException(interpreter, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}
