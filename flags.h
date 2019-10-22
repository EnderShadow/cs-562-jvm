//
// Created by matthew on 10/10/19.
//

#ifndef JVM_FLAGS_H
#define JVM_FLAGS_H

#define CLASS_ACC_PUBLIC        0x0001u
#define CLASS_ACC_PRIVATE       0x0002u
#define CLASS_ACC_PROTECTED     0x0004u
#define CLASS_ACC_STATIC        0x0008u
#define CLASS_ACC_FINAL         0x0010u
#define CLASS_ACC_SUPER         0x0020u
#define CLASS_ACC_INTERFACE     0x0200u
#define CLASS_ACC_ABSTRACT      0x0400u
#define CLASS_ACC_SYNTHETIC     0x1000u
#define CLASS_ACC_ANNOTATION    0x2000u
#define CLASS_ACC_ENUM          0x4000u

#define FIELD_ACC_PUBLIC        0x0001u
#define FIELD_ACC_PRIVATE       0x0002u
#define FIELD_ACC_PROTECTED     0x0004u
#define FIELD_ACC_STATIC        0x0008u
#define FIELD_ACC_FINAL         0x0010u
#define FIELD_ACC_VOLATILE      0x0040u
#define FIELD_ACC_TRANSIENT     0x0080u
#define FIELD_ACC_SYNTHETIC     0x1000u
#define FIELD_ACC_ENUM          0x4000u

#define METHOD_ACC_PUBLIC       0x0001u
#define METHOD_ACC_PRIVATE      0x0002u
#define METHOD_ACC_PROTECTED    0x0004u
#define METHOD_ACC_STATIC       0x0008u
#define METHOD_ACC_FINAL        0x0010u
#define METHOD_ACC_SYNCHRONIZED 0x0020u
#define METHOD_ACC_BRIDGE       0x0040u
#define METHOD_ACC_VARARGS      0x0080u
#define METHOD_ACC_NATIVE       0x0100u
#define METHOD_ACC_ABSTRACT     0x0400u
#define METHOD_ACC_STRICT       0x0800u
#define METHOD_ACC_SYNTHETIC    0x1000u

#define CONSTANT_utf8               1
#define CONSTANT_Integer            3
#define CONSTANT_Float              4
#define CONSTANT_Long               5
#define CONSTANT_Double             6
#define CONSTANT_Class              7
#define CONSTANT_String             8
#define CONSTANT_Fieldref           9
#define CONSTANT_Methodref          10
#define CONSTANT_InterfaceMethodref 11
#define CONSTANT_NameAndType        12
#define CONSTANT_MethodHandle       15
#define CONSTANT_MethodType         16
#define CONSTANT_InvokeDynamic      18

#endif //JVM_FLAGS_H
