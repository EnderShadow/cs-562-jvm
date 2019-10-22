//
// Created by matthew on 10/11/19.
//

#ifndef JVM_JTHREAD_H
#define JVM_JTHREAD_H

#include <stdint.h>
#include <stdlib.h>
#include "object.h"
#include "dataTypes.h"

typedef struct stack_frame {
    struct stack_frame *previousStackFrame;
    void *prevFramePC;
    method_t *currentMethod;
    cell_t *localVariableBase;
    uint8_t *operandStackTypeBase;
    cell_t *operandStackBase;
    uint16_t topOfStack;
} stack_frame_t;

typedef struct jthread {
    pthread_t pthread;
    void *stack;
    stack_frame_t *currentStackFrame;
    void *pc;
    size_t stackSize;
    int id;
} jthread_t;

/**
 *
 * @param name the name of the thread
 * @param method
 * @param arg the argument for the method if required (only used by main)
 * @return
 */
jthread_t *createThread(char *name, method_t *method, object_t *arg, size_t stackSize);

void destroyThread(jthread_t *jthread);

void threadStart(jthread_t *jthread);

cell_t readLocal(stack_frame_t *stackFrame, uint16_t index, uint8_t *type);
double_cell_t readLocal2(stack_frame_t *stackFrame, uint16_t index, uint8_t *type);

void writeLocal(stack_frame_t *stackFrame, uint16_t index, cell_t value, uint8_t type);
void writeLocal2(stack_frame_t *stackFrame, uint16_t index, double_cell_t value, uint8_t type);

cell_t popOperand(stack_frame_t *stackFrame, uint8_t *type);
double_cell_t popOperand2(stack_frame_t *stackFrame, uint8_t *type);

cell_t peekOperand(stack_frame_t *stackFrame, uint16_t index, uint8_t *type);
double_cell_t peekOperand2(stack_frame_t *stackFrame, uint16_t index, uint8_t *type);

uint8_t peekOperandType(stack_frame_t *stackFrame, uint16_t index);

void pushOperand(stack_frame_t *stackFrame, cell_t value, uint8_t type);
void pushOperand2(stack_frame_t *stackFrame, double_cell_t value, uint8_t type);

#endif //JVM_JTHREAD_H
