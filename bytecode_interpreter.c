//
// Created by matthew on 10/11/19.
//

#include "bytecode_interpreter.h"
#include "mm.h"
#include "opcodes.h"
#include "gc.h"
#include <math.h>

int run(bc_interpreter_t *interpreter) {
    // safeguard in case I need to make an actual interpreter struct
    jthread_t *jthread = interpreter;
    
    while(true) {
        // allows garbage collection to occur
        savePoint();
        
        uint8_t *bytecode = jthread->pc;
        uint8_t opcode = bytecode[0];
        
        int ret = instr_table[opcode](jthread, false);
        if(ret > 0) {
            jthread->pc += ret;
        }
        else if(ret == -EJUST_RETURNED) {
            // TODO check if we returned from the root of the thread
        }
        else if(ret == -ETHREW_OFF_THREAD) {
            // TODO print exception and kill thread
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

/**
 * used for throwing exceptions that were not caused by the throw instruction
 * @param interpreter
 * @param exceptionClassName
 * @param exceptionMessage
 */
void throwException(bc_interpreter_t *interpreter, char *exceptionClassName, char *exceptionMessage) {
    // TODO
}

int handle_instr_xload(jthread_t *jthread, bool wide, uint8_t type) {
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

int handle_instr_xstore(jthread_t *jthread, bool wide) {
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

int handle_instr_xload_n(jthread_t *jthread, uint16_t index, uint8_t type) {
    if(type == TYPE_LONG || type == TYPE_DOUBLE)
        pushOperand2(jthread->currentStackFrame, readLocal2(jthread->currentStackFrame, index, NULL), type);
    else
        pushOperand(jthread->currentStackFrame, readLocal(jthread->currentStackFrame, index, NULL), type);
    return 1;
}

int handle_instr_xstore_n(jthread_t *jthread, uint16_t index) {
    uint8_t type = peekOperandType(jthread->currentStackFrame, index);
    if(type == TYPE_LONG || type == TYPE_DOUBLE)
        writeLocal2(jthread->currentStackFrame, index, popOperand2(jthread->currentStackFrame, NULL), type);
    else
        writeLocal(jthread->currentStackFrame, index, popOperand(jthread->currentStackFrame, NULL), type);
    return 1;
}

int handle_instr_xaload(jthread_t *jthread) {
    int32_t index = popOperand(jthread->currentStackFrame, NULL).i;
    slot_t slot = popOperand(jthread->currentStackFrame, NULL).a;
    array_object_t *obj = (array_object_t *) getObject(slot);
    
    if(!obj) {
        throwException(jthread, "java/lang/NullPointerException", "Array was null");
        return 0;
    }
    if(index < 0 || index >= obj->length) {
        throwException(jthread, "java/lang/ArrayIndexOutOfBoundsException", "index was out of bounds for the array");
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

int handle_instr_xastore(jthread_t *jthread) {
    uint8_t type = peekOperandType(jthread->currentStackFrame, 0);
    if(type == TYPE_LONG || type == TYPE_DOUBLE) {
        double_cell_t value = popOperand2(jthread->currentStackFrame, NULL);
        int32_t index = popOperand(jthread->currentStackFrame, NULL).i;
        slot_t slot = popOperand(jthread->currentStackFrame, NULL).a;
        array_object_t *obj = (array_object_t *) getObject(slot);
    
        if(!obj) {
            throwException(jthread, "java/lang/NullPointerException", "Array was null");
            return 0;
        }
        if(index < 0 || index >= obj->length) {
            throwException(jthread, "java/lang/ArrayIndexOutOfBoundsException", "index was out of bounds for the array");
            return 0;
        }
    
        setArrayElement2(obj, index, value);
    }
    else {
        cell_t value = popOperand(jthread->currentStackFrame, NULL);
        int32_t index = popOperand(jthread->currentStackFrame, NULL).i;
        slot_t slot = popOperand(jthread->currentStackFrame, NULL).a;
        array_object_t *obj = (array_object_t *) getObject(slot);
    
        if(!obj) {
            throwException(jthread, "java/lang/NullPointerException", "Array was null");
            return 0;
        }
        if(index < 0 || index >= obj->length) {
            throwException(jthread, "java/lang/ArrayIndexOutOfBoundsException", "index was out of bounds for the array");
            return 0;
        }
    
        setArrayElement(obj, index, value);
    }
    return 1;
}

int handle_instr_unknown(jthread_t *jthread, bool wide) {
    throwException(jthread, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}

int handle_instr_nop(jthread_t *jthread, bool wide) {
	return 1;
}

int handle_instr_aconst_null(jthread_t *jthread, bool wide) {
    cell_t cell = {.a = 0};
	pushOperand(jthread->currentStackFrame, cell, TYPE_REFERENCE);
	return 1;
}

int handle_instr_iconst_m1(jthread_t *jthread, bool wide) {
    cell_t cell = {.i = -1};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_0(jthread_t *jthread, bool wide) {
    cell_t cell = {.i = 0};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_1(jthread_t *jthread, bool wide) {
    cell_t cell = {.i = 1};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_2(jthread_t *jthread, bool wide) {
    cell_t cell = {.i = 2};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_3(jthread_t *jthread, bool wide) {
    cell_t cell = {.i = 3};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_4(jthread_t *jthread, bool wide) {
    cell_t cell = {.i = 4};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_iconst_5(jthread_t *jthread, bool wide) {
    cell_t cell = {.i = 5};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_lconst_0(jthread_t *jthread, bool wide) {
    double_cell_t cell = {.l = 0};
    pushOperand2(jthread->currentStackFrame, cell, TYPE_LONG);
    return 1;
}

int handle_instr_lconst_1(jthread_t *jthread, bool wide) {
    double_cell_t cell = {.l = 1};
    pushOperand2(jthread->currentStackFrame, cell, TYPE_LONG);
    return 1;
}

int handle_instr_fconst_0(jthread_t *jthread, bool wide) {
    cell_t cell = {.f = 0.0f};
    pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_fconst_1(jthread_t *jthread, bool wide) {
    cell_t cell = {.f = 1.0f};
    pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_fconst_2(jthread_t *jthread, bool wide) {
    cell_t cell = {.f = 2.0f};
    pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_dconst_0(jthread_t *jthread, bool wide) {
    double_cell_t cell = {.d = 0.0};
    pushOperand2(jthread->currentStackFrame, cell, TYPE_DOUBLE);
    return 1;
}

int handle_instr_dconst_1(jthread_t *jthread, bool wide) {
    double_cell_t cell = {.d = 1.0};
    pushOperand2(jthread->currentStackFrame, cell, TYPE_DOUBLE);
    return 1;
}

int handle_instr_bipush(jthread_t *jthread, bool wide) {
	cell_t cell;
	cell.i = readByteOperand(jthread, 1);
	pushOperand(jthread->currentStackFrame, cell, TYPE_BYTE);
	return 2;
}

int handle_instr_sipush(jthread_t *jthread, bool wide) {
    cell_t cell;
    cell.i = readShortOperand(jthread, 1);
    pushOperand(jthread->currentStackFrame, cell, TYPE_SHORT);
    return 3;
}

int handle_instr_ldc(jthread_t *jthread, bool wide) {
	uint8_t index = readByteOperand(jthread, 1);
	// TODO
	return -1;
}

int handle_instr_ldc_w(jthread_t *jthread, bool wide) {
    uint16_t index = readShortOperand(jthread, 1);
    // TODO
    return -1;
}

int handle_instr_ldc2_w(jthread_t *jthread, bool wide) {
    uint16_t index = readShortOperand(jthread, 1);
    // TODO
    return -1;
}

int handle_instr_iload(jthread_t *jthread, bool wide) {
    return handle_instr_xload(jthread, wide, TYPE_INT);
}

int handle_instr_lload(jthread_t *jthread, bool wide) {
    return handle_instr_xload(jthread, wide, TYPE_LONG);
}

int handle_instr_fload(jthread_t *jthread, bool wide) {
    return handle_instr_xload(jthread, wide, TYPE_FLOAT);
}

int handle_instr_dload(jthread_t *jthread, bool wide) {
    return handle_instr_xload(jthread, wide, TYPE_DOUBLE);
}

int handle_instr_aload(jthread_t *jthread, bool wide) {
    return handle_instr_xload(jthread, wide, TYPE_REFERENCE);
}

int handle_instr_iload_0(jthread_t *jthread, bool wide) {
	return handle_instr_xload_n(jthread, 0, TYPE_INT);
}

int handle_instr_iload_1(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 1, TYPE_INT);
}

int handle_instr_iload_2(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 2, TYPE_INT);
}

int handle_instr_iload_3(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 3, TYPE_INT);
}

int handle_instr_lload_0(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 0, TYPE_LONG);
}

int handle_instr_lload_1(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 1, TYPE_LONG);
}

int handle_instr_lload_2(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 2, TYPE_LONG);
}

int handle_instr_lload_3(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 3, TYPE_LONG);
}

int handle_instr_fload_0(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 0, TYPE_FLOAT);
}

int handle_instr_fload_1(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 1, TYPE_FLOAT);
}

int handle_instr_fload_2(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 2, TYPE_FLOAT);
}

int handle_instr_fload_3(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 3, TYPE_FLOAT);
}

int handle_instr_dload_0(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 0, TYPE_DOUBLE);
}

int handle_instr_dload_1(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 1, TYPE_DOUBLE);
}

int handle_instr_dload_2(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 2, TYPE_DOUBLE);
}

int handle_instr_dload_3(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 3, TYPE_DOUBLE);
}

int handle_instr_aload_0(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 0, TYPE_REFERENCE);
}

int handle_instr_aload_1(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 1, TYPE_REFERENCE);
}

int handle_instr_aload_2(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 2, TYPE_REFERENCE);
}

int handle_instr_aload_3(jthread_t *jthread, bool wide) {
    return handle_instr_xload_n(jthread, 3, TYPE_REFERENCE);
}

int handle_instr_iaload(jthread_t *jthread, bool wide) {
    return handle_instr_xaload(jthread);
}

int handle_instr_laload(jthread_t *jthread, bool wide) {
    return handle_instr_xaload(jthread);
}

int handle_instr_faload(jthread_t *jthread, bool wide) {
    return handle_instr_xaload(jthread);
}

int handle_instr_daload(jthread_t *jthread, bool wide) {
    return handle_instr_xaload(jthread);
}

int handle_instr_aaload(jthread_t *jthread, bool wide) {
    return handle_instr_xaload(jthread);
}

int handle_instr_baload(jthread_t *jthread, bool wide) {
    return handle_instr_xaload(jthread);
}

int handle_instr_caload(jthread_t *jthread, bool wide) {
    return handle_instr_xaload(jthread);
}

int handle_instr_saload(jthread_t *jthread, bool wide) {
    return handle_instr_xaload(jthread);
}

int handle_instr_istore(jthread_t *jthread, bool wide) {
    return handle_instr_xstore(jthread, wide);
}

int handle_instr_lstore(jthread_t *jthread, bool wide) {
    return handle_instr_xstore(jthread, wide);
}

int handle_instr_fstore(jthread_t *jthread, bool wide) {
    return handle_instr_xstore(jthread, wide);
}

int handle_instr_dstore(jthread_t *jthread, bool wide) {
    return handle_instr_xstore(jthread, wide);
}

int handle_instr_astore(jthread_t *jthread, bool wide) {
    return handle_instr_xstore(jthread, wide);
}

int handle_instr_istore_0(jthread_t *jthread, bool wide) {
	return handle_instr_xstore_n(jthread, 0);
}

int handle_instr_istore_1(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 1);
}

int handle_instr_istore_2(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 2);
}

int handle_instr_istore_3(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 3);
}

int handle_instr_lstore_0(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 0);
}

int handle_instr_lstore_1(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 1);
}

int handle_instr_lstore_2(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 2);
}

int handle_instr_lstore_3(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 3);
}

int handle_instr_fstore_0(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 0);
}

int handle_instr_fstore_1(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 1);
}

int handle_instr_fstore_2(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 2);
}

int handle_instr_fstore_3(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 3);
}

int handle_instr_dstore_0(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 0);
}

int handle_instr_dstore_1(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 1);
}

int handle_instr_dstore_2(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 2);
}

int handle_instr_dstore_3(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 3);
}

int handle_instr_astore_0(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 0);
}

int handle_instr_astore_1(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 1);
}

int handle_instr_astore_2(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 2);
}

int handle_instr_astore_3(jthread_t *jthread, bool wide) {
    return handle_instr_xstore_n(jthread, 3);
}

int handle_instr_iastore(jthread_t *jthread, bool wide) {
    return handle_instr_xastore(jthread);
}

int handle_instr_lastore(jthread_t *jthread, bool wide) {
    return handle_instr_xastore(jthread);
}

int handle_instr_fastore(jthread_t *jthread, bool wide) {
    return handle_instr_xastore(jthread);
}

int handle_instr_dastore(jthread_t *jthread, bool wide) {
    return handle_instr_xastore(jthread);
}

int handle_instr_aastore(jthread_t *jthread, bool wide) {
    return handle_instr_xastore(jthread);
}

int handle_instr_bastore(jthread_t *jthread, bool wide) {
    return handle_instr_xastore(jthread);
}

int handle_instr_castore(jthread_t *jthread, bool wide) {
    return handle_instr_xastore(jthread);
}

int handle_instr_sastore(jthread_t *jthread, bool wide) {
    return handle_instr_xastore(jthread);
}

int handle_instr_pop(jthread_t *jthread, bool wide) {
	--jthread->currentStackFrame->topOfStack;
	return 1;
}

int handle_instr_pop2(jthread_t *jthread, bool wide) {
    jthread->currentStackFrame->topOfStack -= 2;
    return 1;
}

int handle_instr_dup(jthread_t *jthread, bool wide) {
    uint8_t tosType;
	pushOperand(jthread->currentStackFrame, peekOperand(jthread->currentStackFrame, 0, &tosType), tosType);
	return 1;
}

int handle_instr_dup_x1(jthread_t *jthread, bool wide) {
    uint8_t tosType;
    uint8_t nextType;
	cell_t top = popOperand(jthread->currentStackFrame, &tosType);
	cell_t next = popOperand(jthread->currentStackFrame, &nextType);
	pushOperand(jthread->currentStackFrame, top, tosType);
    pushOperand(jthread->currentStackFrame, next, nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    return 1;
}

int handle_instr_dup_x2(jthread_t *jthread, bool wide) {
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

int handle_instr_dup2(jthread_t *jthread, bool wide) {
    uint8_t tosType;
    uint8_t nextType;
    pushOperand(jthread->currentStackFrame, peekOperand(jthread->currentStackFrame, 1, &nextType), nextType);
    pushOperand(jthread->currentStackFrame, peekOperand(jthread->currentStackFrame, 1, &tosType), tosType);
    return 1;
}

int handle_instr_dup2_x1(jthread_t *jthread, bool wide) {
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

int handle_instr_dup2_x2(jthread_t *jthread, bool wide) {
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

int handle_instr_swap(jthread_t *jthread, bool wide) {
    uint8_t tosType;
    uint8_t nextType;
    cell_t top = popOperand(jthread->currentStackFrame, &tosType);
    cell_t next = popOperand(jthread->currentStackFrame, &nextType);
    pushOperand(jthread->currentStackFrame, top, tosType);
    pushOperand(jthread->currentStackFrame, next, nextType);
    return 1;
}

int handle_instr_iadd(jthread_t *jthread, bool wide) {
	cell_t top = popOperand(jthread->currentStackFrame, NULL);
	cell_t next = popOperand(jthread->currentStackFrame, NULL);
	top.i += next.i;
	pushOperand(jthread->currentStackFrame, top, TYPE_INT);
	return 1;
}

int handle_instr_ladd(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l += next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_fadd(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.f += next.f;
    pushOperand(jthread->currentStackFrame, top, TYPE_FLOAT);
    return 1;
}

int handle_instr_dadd(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.d += next.d;
    pushOperand2(jthread->currentStackFrame, top, TYPE_DOUBLE);
    return 1;
}

int handle_instr_isub(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.i -= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lsub(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.l -= top.l;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_fsub(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.f -= top.f;
    pushOperand(jthread->currentStackFrame, next, TYPE_FLOAT);
    return 1;
}

int handle_instr_dsub(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.d -= top.d;
    pushOperand2(jthread->currentStackFrame, next, TYPE_DOUBLE);
    return 1;
}

int handle_instr_imul(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.i *= next.i;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_lmul(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l *= next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_fmul(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.f *= next.f;
    pushOperand(jthread->currentStackFrame, top, TYPE_FLOAT);
    return 1;
}

int handle_instr_dmul(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.d *= next.d;
    pushOperand2(jthread->currentStackFrame, top, TYPE_DOUBLE);
    return 1;
}

int handle_instr_idiv(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    if(top.i == 0) {
        throwException(jthread, "java/lang/ArithmeticException", "Cannot divide by 0");
        return 0;
    }
    next.i /= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_ldiv(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    if(top.l == 0) {
        throwException(jthread, "java/lang/ArithmeticException", "Cannot divide by 0");
        return 0;
    }
    next.l /= top.l;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_fdiv(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.f /= top.f;
    pushOperand(jthread->currentStackFrame, next, TYPE_FLOAT);
    return 1;
}

int handle_instr_ddiv(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.d /= top.d;
    pushOperand2(jthread->currentStackFrame, next, TYPE_DOUBLE);
    return 1;
}

int handle_instr_irem(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    if(top.i == 0) {
        throwException(jthread, "java/lang/ArithmeticException", "Cannot divide by 0");
        return 0;
    }
    next.i %= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lrem(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    if(top.l == 0) {
        throwException(jthread, "java/lang/ArithmeticException", "Cannot divide by 0");
        return 0;
    }
    next.l %= top.l;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_frem(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.f = fmodf(next.f, top.f);
    pushOperand(jthread->currentStackFrame, next, TYPE_FLOAT);
    return 1;
}

int handle_instr_drem(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.d = fmod(next.d, top.d);
    pushOperand2(jthread->currentStackFrame, next, TYPE_DOUBLE);
    return 1;
}

int handle_instr_ineg(jthread_t *jthread, bool wide) {
	cell_t top = popOperand(jthread->currentStackFrame, NULL);
	top.i = -top.i;
	pushOperand(jthread->currentStackFrame, top, TYPE_INT);
	return 1;
}

int handle_instr_lneg(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    top.l = -top.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_fneg(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    top.f = -top.f;
    pushOperand(jthread->currentStackFrame, top, TYPE_FLOAT);
    return 1;
}

int handle_instr_dneg(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    top.d = -top.d;
    pushOperand2(jthread->currentStackFrame, top, TYPE_DOUBLE);
    return 1;
}

int handle_instr_ishl(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.i <<= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lshl(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.l <<= top.i;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_ishr(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.i >>= top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lshr(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.l >>= top.i;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_iushr(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    next.i = ((uint32_t) next.i) >> top.i;
    pushOperand(jthread->currentStackFrame, next, TYPE_INT);
    return 1;
}

int handle_instr_lushr(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    next.l = ((uint64_t) next.l) >> top.i;
    pushOperand2(jthread->currentStackFrame, next, TYPE_LONG);
    return 1;
}

int handle_instr_iand(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.i &= next.i;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_land(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l &= next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_ior(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.i |= next.i;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_lor(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l |= next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_ixor(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    top.i ^= next.i;
    pushOperand(jthread->currentStackFrame, top, TYPE_INT);
    return 1;
}

int handle_instr_lxor(jthread_t *jthread, bool wide) {
    double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    top.l ^= next.l;
    pushOperand2(jthread->currentStackFrame, top, TYPE_LONG);
    return 1;
}

int handle_instr_iinc(jthread_t *jthread, bool wide) {
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

int handle_instr_i2l(jthread_t *jthread, bool wide) {
	cell_t cell = popOperand(jthread->currentStackFrame, NULL);
	double_cell_t dcell = {.l = cell.i};
	pushOperand2(jthread->currentStackFrame, dcell, TYPE_LONG);
    return 1;
}

int handle_instr_i2f(jthread_t *jthread, bool wide) {
	cell_t cell = popOperand(jthread->currentStackFrame, NULL);
	cell.f = cell.i;
	pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_i2d(jthread_t *jthread, bool wide) {
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t dcell = {.d = cell.i};
    pushOperand2(jthread->currentStackFrame, dcell, TYPE_DOUBLE);
    return 1;
}

int handle_instr_l2i(jthread_t *jthread, bool wide) {
	double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
	cell_t cell = {.i = dcell.l};
	pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_l2f(jthread_t *jthread, bool wide) {
    double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
    cell_t cell = {.f = dcell.l};
    pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_l2d(jthread_t *jthread, bool wide) {
    double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
    dcell.d = dcell.l;
    pushOperand2(jthread->currentStackFrame, dcell, TYPE_INT);
    return 1;
}

int handle_instr_f2i(jthread_t *jthread, bool wide) {
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    cell.i = cell.f;
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_f2l(jthread_t *jthread, bool wide) {
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t dcell = {.l = cell.f};
    pushOperand2(jthread->currentStackFrame, dcell, TYPE_LONG);
    return 1;
}

int handle_instr_f2d(jthread_t *jthread, bool wide) {
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    double_cell_t dcell = {.d = cell.f};
    pushOperand2(jthread->currentStackFrame, dcell, TYPE_DOUBLE);
    return 1;
}

int handle_instr_d2i(jthread_t *jthread, bool wide) {
    double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
    cell_t cell = {.i = dcell.d};
    pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
    return 1;
}

int handle_instr_d2l(jthread_t *jthread, bool wide) {
	double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
	dcell.l = dcell.d;
	pushOperand2(jthread->currentStackFrame, dcell, TYPE_LONG);
    return 1;
}

int handle_instr_d2f(jthread_t *jthread, bool wide) {
    double_cell_t dcell = popOperand2(jthread->currentStackFrame, NULL);
    cell_t cell = {.f = dcell.d};
    pushOperand(jthread->currentStackFrame, cell, TYPE_FLOAT);
    return 1;
}

int handle_instr_i2b(jthread_t *jthread, bool wide) {
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    cell.i = (int8_t) cell.i;
    pushOperand(jthread->currentStackFrame, cell, TYPE_BYTE);
    return 1;
}

int handle_instr_i2c(jthread_t *jthread, bool wide) {
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    cell.i = cell.i & 0xFFFF;
    pushOperand(jthread->currentStackFrame, cell, TYPE_CHAR);
    return 1;
}

int handle_instr_i2s(jthread_t *jthread, bool wide) {
    cell_t cell = popOperand(jthread->currentStackFrame, NULL);
    cell.i = (int16_t) cell.i;
    pushOperand(jthread->currentStackFrame, cell, TYPE_SHORT);
    return 1;
}

int handle_instr_lcmp(jthread_t *jthread, bool wide) {
	double_cell_t top = popOperand2(jthread->currentStackFrame, NULL);
    double_cell_t next = popOperand2(jthread->currentStackFrame, NULL);
    cell_t result = {.i = MAX(-1, MIN(1, next.l - top.l))};
    pushOperand(jthread->currentStackFrame, result, TYPE_INT);
    return 1;
}

int handle_instr_fcmpl(jthread_t *jthread, bool wide) {
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

int handle_instr_fcmpg(jthread_t *jthread, bool wide) {
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

int handle_instr_dcmpl(jthread_t *jthread, bool wide) {
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

int handle_instr_dcmpg(jthread_t *jthread, bool wide) {
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

int handle_instr_ifeq(jthread_t *jthread, bool wide) {
	cell_t top = popOperand(jthread->currentStackFrame, NULL);
	int16_t offset = readShortOperand(jthread, 1);
	if(top.i == 0) {
	    jthread->pc += offset;
	    return 0;
	}
	return 3;
}

int handle_instr_ifne(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i != 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_iflt(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i < 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_ifge(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i >= 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_ifgt(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i > 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_ifle(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.i <= 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmpeq(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i == top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmpne(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i != top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmplt(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i < top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmpge(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i >= top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmpgt(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i > top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_icmple(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.i <= top.i) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_acmpeq(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.a == top.a) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_if_acmpne(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    cell_t next = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(next.a != top.a) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_goto(jthread_t *jthread, bool wide) {
    int16_t offset = readShortOperand(jthread, 1);
    jthread->pc += offset;
    return 0;
}

int handle_instr_jsr(jthread_t *jthread, bool wide) {
    int16_t offset = readShortOperand(jthread, 1);
    cell_t returnAddress =  {.r = jthread->pc + 1 - jthread->currentStackFrame->currentMethod->codeLocation};
    jthread->pc += offset;
    pushOperand(jthread->currentStackFrame, returnAddress, TYPE_RETURN_ADDRESS);
    return 0;
}

int handle_instr_ret(jthread_t *jthread, bool wide) {
	uint16_t index;
	if(wide)
	    index = readShortOperand(jthread, 2);
	else
	    index = (uint8_t) readByteOperand(jthread, 1);
	cell_t returnAddress = readLocal(jthread->currentStackFrame, index, NULL);
	jthread->pc = jthread->currentStackFrame->currentMethod->codeLocation + returnAddress.r;
	return 0;
}

int handle_instr_tableswitch(jthread_t *jthread, bool wide) {
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

int handle_instr_lookupswitch(jthread_t *jthread, bool wide) {
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

int handle_instr_ireturn(jthread_t *jthread, bool wide) {
	cell_t returnValue = popOperand(jthread->currentStackFrame, NULL);
	jthread->pc = jthread->currentStackFrame->prevFramePC;
	jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
	pushOperand(jthread->currentStackFrame, returnValue, TYPE_INT);
	return 0;
}

int handle_instr_lreturn(jthread_t *jthread, bool wide) {
    double_cell_t returnValue = popOperand2(jthread->currentStackFrame, NULL);
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    pushOperand2(jthread->currentStackFrame, returnValue, TYPE_LONG);
}

int handle_instr_freturn(jthread_t *jthread, bool wide) {
    cell_t returnValue = popOperand(jthread->currentStackFrame, NULL);
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    pushOperand(jthread->currentStackFrame, returnValue, TYPE_FLOAT);
    return 0;
}

int handle_instr_dreturn(jthread_t *jthread, bool wide) {
    double_cell_t returnValue = popOperand2(jthread->currentStackFrame, NULL);
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    pushOperand2(jthread->currentStackFrame, returnValue, TYPE_DOUBLE);
}

int handle_instr_areturn(jthread_t *jthread, bool wide) {
    cell_t returnValue = popOperand(jthread->currentStackFrame, NULL);
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    pushOperand(jthread->currentStackFrame, returnValue, TYPE_REFERENCE);
    return 0;
}

int handle_instr_return(jthread_t *jthread, bool wide) {
    jthread->pc = jthread->currentStackFrame->prevFramePC;
    jthread->currentStackFrame = jthread->currentStackFrame->previousStackFrame;
    
    // only void methods can be the root method of a thread
    return -EJUST_RETURNED;
}

int handle_instr_getstatic(jthread_t *jthread, bool wide) {
	
}

int handle_instr_putstatic(jthread_t *jthread, bool wide) {
	
}

int handle_instr_getfield(jthread_t *jthread, bool wide) {
	
}

int handle_instr_putfield(jthread_t *jthread, bool wide) {
	
}

int handle_instr_invokevirtual(jthread_t *jthread, bool wide) {
	
}

int handle_instr_invokespecial(jthread_t *jthread, bool wide) {
	
}

int handle_instr_invokestatic(jthread_t *jthread, bool wide) {
	
}

int handle_instr_invokeinterface(jthread_t *jthread, bool wide) {
	
}

int handle_instr_invokedynamic(jthread_t *jthread, bool wide) {
    throwException(jthread, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}

int handle_instr_new(jthread_t *jthread, bool wide) {
	
}

int handle_instr_newarray(jthread_t *jthread, bool wide) {
	
}

int handle_instr_anewarray(jthread_t *jthread, bool wide) {
	
}

int handle_instr_arraylength(jthread_t *jthread, bool wide) {
	cell_t cell = popOperand(jthread->currentStackFrame, NULL);
	array_object_t *array = (array_object_t *) getObject(cell.a);
	if(!array) {
	    throwException(jthread, "java/lang/NullPointerException", "Array cannot be null");
	    return 0;
	}
	cell.i = array->length;
	pushOperand(jthread->currentStackFrame, cell, TYPE_INT);
}

int handle_instr_athrow(jthread_t *jthread, bool wide) {
	
}

int handle_instr_checkcast(jthread_t *jthread, bool wide) {
	
}

int handle_instr_instanceof(jthread_t *jthread, bool wide) {
	
}

int handle_instr_monitorenter(jthread_t *jthread, bool wide) {
	
}

int handle_instr_monitorexit(jthread_t *jthread, bool wide) {
	
}

int handle_instr_wide(jthread_t *jthread, bool wide) {
    return instr_table[(uint8_t) readByteOperand(jthread, 1)](jthread, true);
}

int handle_instr_multianewarray(jthread_t *jthread, bool wide) {
	
}

int handle_instr_ifnull(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.a == 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_ifnonnull(jthread_t *jthread, bool wide) {
    cell_t top = popOperand(jthread->currentStackFrame, NULL);
    int16_t offset = readShortOperand(jthread, 1);
    if(top.a != 0) {
        jthread->pc += offset;
        return 0;
    }
    return 3;
}

int handle_instr_goto_w(jthread_t *jthread, bool wide) {
    int32_t offset = readIntOperand(jthread, 1);
    jthread->pc += offset;
    return 0;
}

int handle_instr_jsr_w(jthread_t *jthread, bool wide) {
    int32_t offset = readIntOperand(jthread, 1);
    cell_t returnAddress =  {.r = jthread->pc + 1 - jthread->currentStackFrame->currentMethod->codeLocation};
    jthread->pc += offset;
    pushOperand(jthread->currentStackFrame, returnAddress, TYPE_RETURN_ADDRESS);
    return 0;
}

int handle_instr_breakpoint(jthread_t *jthread, bool wide) {
    throwException(jthread, "java/lang/InternalError", "Unimplemented Instruction");
	return 0;
}

int handle_instr_impdep1(jthread_t *jthread, bool wide) {
    throwException(jthread, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}

int handle_instr_impdep2(jthread_t *jthread, bool wide) {
    throwException(jthread, "java/lang/InternalError", "Unimplemented Instruction");
    return 0;
}
