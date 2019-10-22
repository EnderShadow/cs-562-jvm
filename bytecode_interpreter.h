//
// Created by matthew on 10/11/19.
//

#ifndef JVM_BYTECODE_INTERPRETER_H
#define JVM_BYTECODE_INTERPRETER_H

#include "jthread.h"

// used for checking if we returned from the root frame
#define EJUST_RETURNED      2

// used when an exception reaches the top of the stack without encountering an exception handler
#define ETHREW_OFF_THREAD   3

#define bc_interpreter_t jthread_t

int run(bc_interpreter_t *interpreter);

/**
 * used for throwing exceptions that were not caused by the throw instruction
 * @param interpreter
 * @param exceptionClassName
 * @param exceptionMessage
 */
void throwException(bc_interpreter_t *interpreter, char *exceptionClassName, char *exceptionMessage);

#endif //JVM_BYTECODE_INTERPRETER_H
