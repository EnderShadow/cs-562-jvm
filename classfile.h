//
// Created by matthew on 10/7/19.
//

#ifndef JVM_CLASSFILE_H
#define JVM_CLASSFILE_H

#include <stdint.h>
#include "jlock.h"

typedef struct class_info {
    uint8_t tag;
    uint16_t nameIndex;
} class_info_t;

typedef struct field_method_interface_method_ref_info {
    uint8_t tag;
    uint16_t classIndex;
    uint16_t nameAndTypeIndex;
} field_method_interface_method_ref_info_t;

typedef struct string_info {
    uint8_t tag;
    uint16_t stringIndex;
} string_info_t;

typedef struct integer_float_info {
    uint8_t tag;
    uint32_t bytes;
} integer_float_info_t;

typedef struct long_double_info {
    uint8_t tag;
    uint64_t bytes;
} long_double_info_t;

typedef struct name_and_type_info {
    uint8_t tag;
    uint16_t nameIndex;
    uint16_t descriptorIndex;
} name_and_type_info_t;

typedef struct utf8_info {
    uint8_t tag;
    uint16_t length;
    char *chars; // this is a null terminated string
} utf8_info_t;

typedef struct method_handle_info {
    uint8_t tag;
    uint8_t referenceKind;
    uint16_t referenceIndex;
} method_handle_info_t;

typedef struct method_type_info {
    uint8_t tag;
    uint16_t descriptorIndex;
} method_type_info_t;

typedef struct invoke_dynamic_info {
    uint8_t tag;
    uint16_t bootstrapMethodAttrIndex;
    uint16_t nameAndTypeIndex;
} invoke_dynamic_info_t;

typedef union constant_info {
    class_info_t classInfo;
    field_method_interface_method_ref_info_t fieldMethodInterfaceMethodRefInfo;
    string_info_t stringInfo;
    integer_float_info_t integerFloatInfo;
    long_double_info_t longDoubleInfo;
    name_and_type_info_t nameAndTypeInfo;
    utf8_info_t utf8Info;
    method_handle_info_t methodHandleInfo;
    method_type_info_t methodTypeInfo;
    invoke_dynamic_info_t invokeDynamicInfo;
} constant_info_t;

typedef struct class class_t;

typedef union attribute_info attribute_info_t;

typedef struct constant_value_attribute {
    char *name;
    uint16_t constantIndex;
} constant_value_attribute_t;

typedef struct exception_table {
    uint16_t startPC;
    uint16_t endPC;
    uint16_t handlerPC;
    uint16_t catchType;
} exception_table_t;

typedef struct code_attribute {
    char *name;
    uint16_t maxStack;
    uint16_t maxLocals;
    uint32_t codeLength;
    void *code;
    uint16_t exceptionTableLength;
    exception_table_t *exceptionHandlers;
    uint16_t attributeCount;
    attribute_info_t **attributes;
} code_attribute_t;

// This attribute represents any skipped attribute
typedef struct skipped_attribute {
    char *name;
} skipped_attribute_t;

typedef struct signature_attribute {
    char *name;
    uint16_t signatureIndex;
} signature_attribute_t;

union attribute_info {
    constant_value_attribute_t constantValueAttribute;
    code_attribute_t codeAttribute;
    skipped_attribute_t skippedAttribute;
    signature_attribute_t signatureAttribute;
};

typedef struct type_info {
    class_t *classPointer;
    uint8_t type;
} type_t;

typedef struct field {
    char *name;
    char *descriptor;
    class_t *class;
    attribute_info_t **attributes;
    uint32_t objectOffset;
    uint16_t dataSize;
    uint16_t numAttributes;
    uint16_t flags;
} field_t;

typedef struct method {
    char *name;
    char *descriptor;
    class_t *class;
    code_attribute_t *codeAttribute;
    attribute_info_t **attributes;
    uint16_t numAttributes;
    uint16_t flags;
    uint8_t numParameters;
} method_t;

#define CLASS_STATUS_LOADING 0
#define CLASS_STATUS_LOADED 1
#define CLASS_STATUS_INITIALIZING 2
#define CLASS_STATUS_INITIALIZED 3

struct class {
    char *name;
    constant_info_t **constantPool;
    struct class *thisClass;
    struct class *superClass;
    struct class **interfaces;
    method_t *methods;
    field_t *fields;
    attribute_info_t **attributes;
    void *staticFieldData;
    jlock_t jlock;
    uint16_t numConstants;
    uint16_t numInterfaces;
    uint16_t numMethods;
    uint16_t numFields;
    uint16_t numAttributes;
    uint16_t objectSize;
    uint16_t staticDataSize;
    uint16_t flags;
    volatile uint8_t status;
};

/**
 *
 * @return 1 if the class represents an array, otherwise 0
 */
int isArrayClass(class_t *class);

/**
 *
 * @param class
 * @return 1 if the class represents a primitive type, otherwise 0
 */
int isPrimitiveClass(class_t *class);

/**
 *
 * @param class
 * @return the number of dimensions the array represented by this class has or 0 if it's not an array
 */
int numArrayDimensions(class_t *class);

/**
 *
 * @param class
 * @return the size of each array element in bytes for this class if this class represents an array, otherwise 0
 */
int arrayElementSize(class_t *class);

#endif //JVM_CLASSFILE_H
