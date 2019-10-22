//
// Created by matthew on 10/11/19.
//

#include "jthread.h"
#include <sys/mman.h>
#include "utils.h"
#include "gc.h"
#include "bytecode_interpreter.h"
#include <stdio.h>
#include <stdatomic.h>

atomic_int nextThreadId = 1;

/**
 *
 * @param name the name of the thread
 * @param method must have either 0 or 1 arguments
 * @param arg the argument for the method if required (only used by main)
 * @return
 */
jthread_t *createThread(char *name, method_t *method, object_t *arg, size_t stackSize) {
    jthread_t *jthread = malloc(sizeof(jthread_t));
    if(!jthread)
        return NULL;
    jthread->stack = mmap(NULL, stackSize, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if(jthread->stack == MAP_FAILED) {
        free(jthread);
        return NULL;
    }
    jthread->stackSize = stackSize;
    if(method->numParameters)
        ((cell_t *) jthread->stack)->a = arg->slot;
    jthread->currentStackFrame = jthread->stack + ALIGN(method->numLocals * sizeof(cell_t));
    stack_frame_t *stackFrame = jthread->currentStackFrame;
    stackFrame->currentMethod = method;
    stackFrame->previousStackFrame = NULL;
    stackFrame->prevFramePC = NULL;
    stackFrame->localVariableBase = jthread->stack;
    stackFrame->operandStackTypeBase = (void *) stackFrame + sizeof(stackFrame);
    stackFrame->operandStackBase = (void *) stackFrame->operandStackTypeBase + ALIGN(method->maxStack);
    stackFrame->topOfStack = 0;
    jthread->pc = method->codeLocation;
    jthread->id = nextThreadId++;
    
    return jthread;
}

void destroyThread(jthread_t *jthread) {
    --numThreads;
    munmap(jthread->stack, jthread->stackSize);
    free(jthread);
}

void *threadRoutine(void *jthread) {
    pthread_detach(pthread_self());
    int result = run(jthread);
    // TODO check result
    destroyThread(jthread);
    return jthread;
}

void threadStart(jthread_t *jthread) {
    ++numThreads;
    // TODO start thread
    if(pthread_create(&jthread->pthread, NULL, threadRoutine, jthread)) {
        destroyThread(jthread);
        printf("Failed to start thread. Thread has been destroyed\n");
    }
}

cell_t readLocal(stack_frame_t *stackFrame, uint16_t index, uint8_t *type) {
    if(type)
        *type = stackFrame->currentMethod->parameterTypes[index].type;
    return stackFrame->localVariableBase[index];
}

double_cell_t readLocal2(stack_frame_t *stackFrame, uint16_t index, uint8_t *type) {
    if(type)
        *type = stackFrame->currentMethod->parameterTypes[index].type;
    return *(double_cell_t *) (stackFrame->localVariableBase + index);
}

void writeLocal(stack_frame_t *stackFrame, uint16_t index, cell_t value, uint8_t type) {
    stackFrame->localVariableBase[index] = value;
}

void writeLocal2(stack_frame_t *stackFrame, uint16_t index, double_cell_t value, uint8_t type) {
    *(double_cell_t *) (stackFrame->localVariableBase + index) = value;
}

cell_t popOperand(stack_frame_t *stackFrame, uint8_t *type) {
    if(type)
        *type = stackFrame->operandStackTypeBase[stackFrame->topOfStack - 1];
    return *(stackFrame->operandStackBase + --stackFrame->topOfStack);
}

double_cell_t popOperand2(stack_frame_t *stackFrame, uint8_t *type) {
    if(type)
        *type = stackFrame->operandStackTypeBase[stackFrame->topOfStack - 2];
    return *(double_cell_t *) (stackFrame->operandStackBase + (stackFrame->topOfStack -= 2));
}

cell_t peekOperand(stack_frame_t *stackFrame, uint16_t index, uint8_t *type) {
    if(type)
        *type = stackFrame->operandStackTypeBase[stackFrame->topOfStack - 1 - index];
    return *(stackFrame->operandStackBase - 1 - index);
}

double_cell_t peekOperand2(stack_frame_t *stackFrame, uint16_t index, uint8_t *type) {
    if(type)
        *type = stackFrame->operandStackTypeBase[stackFrame->topOfStack - 2 - index];
    return *(double_cell_t *) (stackFrame->operandStackBase - 2 - index);
}

uint8_t peekOperandType(stack_frame_t *stackFrame, uint16_t index) {
    return stackFrame->operandStackTypeBase[stackFrame->topOfStack - 1 - index];
}

void pushOperand(stack_frame_t *stackFrame, cell_t value, uint8_t type) {
    stackFrame->operandStackTypeBase[stackFrame->topOfStack] = type;
    *(stackFrame->operandStackBase + stackFrame->topOfStack++) = value;
}

void pushOperand2(stack_frame_t *stackFrame, double_cell_t value, uint8_t type) {
    stackFrame->operandStackTypeBase[stackFrame->topOfStack] = type;
    stackFrame->operandStackTypeBase[stackFrame->topOfStack + 1] = type;
    *(double_cell_t *) (stackFrame->operandStackBase + stackFrame->topOfStack) = value;
    stackFrame->topOfStack += 2;
}