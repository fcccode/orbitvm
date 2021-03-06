//===--------------------------------------------------------------------------------------------===
// orbit/runtime/vm.c
// This source is part of Orbit - Runtime
//
// Created on 2017-01-03 by Amy Parent <amy@amyparent.com>
// Copyright (c) 2016-2018 Amy Parent <amy@amyparent.com>
// Available under the MIT License
// =^•.•^=
//===--------------------------------------------------------------------------------------------===
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <orbit/runtime/vm.h>
#include <orbit/runtime/objfile.h>
#include <orbit/runtime/gc.h>
#include <orbit/utils/debug.h>

static bool orbit_vmRun(OrbitVM*, OrbitVMTask*);

OrbitVM* orbit_vmNew() {
    
    OrbitVM* vm = malloc(sizeof(OrbitVM));
    
    vm->task = NULL;
    vm->gcHead = NULL;
    vm->allocated = 0;
    vm->nextGC = ORBIT_FIRST_GC;
    
    vm->dispatchTable = orbit_gcMapNew(vm);
    vm->classes = orbit_gcMapNew(vm);
    vm->modules = orbit_gcMapNew(vm);
    
    vm->gcStackSize = 0;
    
    //orbit_registerStandardLib(vm);
    
    return vm;
}

void orbit_vmDealloc(OrbitVM* vm) {
    assert(vm != NULL && "Null instance error");
    
    // Set those as unreachable so the GC can collect them
    vm->dispatchTable = NULL;
    vm->classes = NULL;
    vm->modules = NULL;
    vm->task = NULL;
    orbit_gcRun(vm);
    
    free(vm);
}

void orbit_vmLoadModule(OrbitVM* vm, const char* moduleName) {
    assert(vm != NULL && "Null instance error");
    assert(moduleName != NULL && "Null string error");
    
    OrbitValue key = MAKE_OBJECT(orbit_gcStringNew(vm, moduleName));
    OrbitValue module = VAL_NIL;
    
    orbit_gcMapGet(vm->modules, key, &module);
    if(IS_MODULE(module)) { return; }
    
    size_t length = strlen(moduleName);
    char path[length+5]; // len + . + omf + \0
    
    strncpy(path, moduleName, length);
    strncpy(path+length, ".omf", 4);
    path[length+4] = '\0';
    
    ORBIT_DLOG("Loading module %s", path);
    
    FILE* in = fopen(path, "rb");
    if(!in) {
        // TODO: error signaling
        return;
    }
    
    OrbitVMModule* moduleObj = orbit_unpackModule(vm, in);
    fclose(in);
    
    if(moduleObj == NULL) {
        // TODO: signal error to the vm.
        return;
    }
    module = MAKE_OBJECT(moduleObj);
    orbit_gcMapAdd(vm, vm->modules, key, module);
}

bool orbit_vmInvoke(OrbitVM* vm, const char* module, const char* entry) {
    assert(vm != NULL && "Null instance error");
    
    orbit_vmLoadModule(vm, module);
    
    OrbitValue signature = MAKE_OBJECT(orbit_gcStringNew(vm, entry));
    OrbitValue fn = VAL_NIL;
    if(!orbit_gcMapGet(vm->dispatchTable, signature, &fn) || !IS_FUNCTION(fn)) {
        fprintf(stderr, "error: cannot find `%s` (entry point)\n", entry);
        return false;
    }
    OrbitVMTask* task = orbit_gcTaskNew(vm, AS_FUNCTION(fn));
    if(!orbit_vmRun(vm, task)) {
        // TODO: print error details. String in fiber/VM?
        return false;
    } else {
        return true;
    }
}

// Checks that [task]'s stack as at least [effect] more slots available. If it
// doesn't grow the stack.
static inline void orbit_vmEnsureStack(OrbitVM* vm, OrbitVMTask* task, uint8_t req) {
    uint64_t stackSize = (task->sp - task->stack);
    uint64_t required = stackSize + req;
    if(required <= task->stackCapacity) { return; }
    
    // First we reallocate the stack. The rest is not so trivial: the tasks
    // keeps a bunch of pointers to different locations in the stack:
    // - the most obvious one is the stack pointer
    // - each frame's base pointer
    // since REALLOC can move memory if it needs to, we need to calculate an
    // offset and (if it's not zero) shift everything.
    
    while(task->stackCapacity < required) {
        task->stackCapacity *= 2;
    }
    OrbitValue* oldStack = task->stack;
    task->stack = REALLOC_ARRAY(vm, task->stack, OrbitValue, task->stackCapacity);
    
    int64_t stackOffset = task->stack - oldStack;
    if(stackOffset == 0) { return; }
    
    task->sp += stackOffset;
    for(uint64_t i = 0; i < task->frameCount; ++i) {
        task->frames[i].stackBase += stackOffset;
    }
}

// checks that a task has enough frames left in the call stack for one more
// to be pushed.
static void orbit_vmEnsureFrames(OrbitVM* vm, OrbitVMTask* task) {
    assert(vm != NULL && "Null instance error");
    assert(task != NULL && "Null instance error");
    
    if(task->frameCount + 1 < task->frameCapacity) return;
    task->frameCapacity *= 2;
    task->frames = REALLOC_ARRAY(vm, task->frames,
                                 OrbitVMFrame, task->frameCapacity);
}

static bool orbit_vmRun(OrbitVM* vm, OrbitVMTask* task) {
    assert(vm != NULL && "Null instance error");
    assert(task != NULL && "Null instance error");
    
    assert(task->frameCount > 0 && "task must have an entry point");
    
    vm->task = task;
    
    // pull stuff in locals so we don't have to follow 10 pointers every
    // two line. This means invoke: and return: will have to update those
    // so that we stay on the same page.
    
    OrbitVMFrame* frame = &task->frames[task->frameCount-1];
    
    register VMCode instruction = CODE_halt;
    register OrbitVMFunction* fn = frame->function;
    register uint8_t* ip = frame->ip;
    register OrbitValue* locals = frame->stackBase;
    
#define PUSH(value) (*(task->sp++) = (value))
#define PEEK() (*(task->sp - 1))
#define POP() (*(--task->sp))
#define DROP() (--task->sp)
    
#define READ8() (*(ip++))
#define READ16() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
    
#ifdef ORBIT_FORBIT_AST_INTERPRET
    #define OPCODE(code, _, __) &&code_##code,
    static void* dispatch[] = {
    #include <orbit/runtime/opcodes.h>
    };
    #undef OPCODE
    #define CASE_OP(code) code_##code
    #define NEXT() goto *dispatch[instruction = (VMCode)READ8()]
    #define START_LOOP() NEXT();
#else
    #define NEXT() goto loop
    #define CASE_OP(val) case CODE_##val
    #define START_LOOP() loop: switch(instruction = (VMCode)READ8())
#endif
    
    // Main loop. Tonnes of opimisations to be done here (obviously)
    START_LOOP()
    {
        CASE_OP(halt):
            return true;
        
        CASE_OP(load_nil):
            PUSH(VAL_NIL);
            NEXT();
        
        CASE_OP(load_true):
            PUSH(VAL_TRUE);
            NEXT();
            
        CASE_OP(load_false):
            PUSH(VAL_FALSE);
            NEXT();
            
        CASE_OP(load_const):
            PUSH(fn->module->constants[READ16()]);
            NEXT();
            
        CASE_OP(load_local):
            PUSH(locals[READ8()]);
            NEXT();
            
        CASE_OP(load_field):
            {
                // TODO: replace POP() by PEEK() ?
                OrbitGCInstance* obj = AS_INST(POP());
                PUSH(obj->fields[READ16()]);
            }
            NEXT();
            
        CASE_OP(load_global):
            {
                uint16_t idx = READ16();
                assert(idx < fn->module->globalCount && "global index out of range");
                PUSH(fn->module->globals[idx].global);
            }
            NEXT();
            
        CASE_OP(store_local):
            locals[READ8()] = POP();
            NEXT();
            
        CASE_OP(store_field):
            {
                OrbitValue val = POP();
                AS_INST(POP())->fields[READ16()] = val;
            }
            NEXT();
            
        CASE_OP(store_global):
            {
                uint16_t idx = READ16();
                assert(idx < fn->module->globalCount && "global index out of range");
                fn->module->globals[idx].global = POP();
            }
            NEXT();
            
        CASE_OP(add):
            {
                double b = AS_NUM(POP());
                double a = AS_NUM(POP());
                PUSH(MAKE_NUM(a + b));
            }
            NEXT();
            
        CASE_OP(sub):
            {
                double b = AS_NUM(POP());
                double a = AS_NUM(POP());
                PUSH(MAKE_NUM(a - b));
            }
            NEXT();
            
        CASE_OP(mul):
            {
                double b = AS_NUM(POP());
                double a = AS_NUM(POP());
                PUSH(MAKE_NUM(a * b));
            }
            NEXT();
            
        CASE_OP(div):
            {
                double b = AS_NUM(POP());
                double a = AS_NUM(POP());
                PUSH(MAKE_NUM(a / b));
            }
            NEXT();
            
        CASE_OP(test_lt):
            {
                double b = AS_NUM(POP());
                double a = AS_NUM(POP());
                PUSH(MAKE_BOOL(a < b));
            }
            NEXT();
        
        CASE_OP(test_gt):
            {
                double b = AS_NUM(POP());
                double a = AS_NUM(POP());
                PUSH(MAKE_BOOL(a > b));
            }
            NEXT();
            
        CASE_OP(test_eq):
            {
                double b = AS_NUM(POP());
                double a = AS_NUM(POP());
                PUSH(MAKE_BOOL(a == b));
            }
            NEXT();
            
        CASE_OP(and):
            // TODO: implementation
            NEXT();
            
        CASE_OP(or):
            // TODO: implementation
            NEXT();

        CASE_OP(jump_if):
            {
                uint16_t offset= READ16();
                OrbitValue condition = POP();
                if(IS_FALSE(condition)) {
                    NEXT();
                }
                ip += offset;
            }
            NEXT();
        
        CASE_OP(jump):
            {
                uint16_t offset= READ16();
                ip += offset;
            }
            NEXT();
            
        CASE_OP(rjump_if):
            {
                uint16_t offset;
                offset = READ16();
                OrbitValue condition = POP();
                if(IS_FALSE(condition)) NEXT();
                ip -= offset;
            }
            NEXT();
            
        CASE_OP(rjump):
            {
                uint16_t offset= READ16();
                ip -= offset;
            }
            NEXT();
            
        CASE_OP(pop):
            DROP();
            NEXT();
            
        CASE_OP(swap):
            {
                OrbitValue a = POP();
                OrbitValue b = POP();
                PUSH(a);
                PUSH(b);
            }
            NEXT();
            
            
        {
            // invoke family of opcodes. When compiled, all invocations are
            // done through `invoke_sym`, and point to a symbolic reference
            // (string in the function's constant pool).
            //
            // The first time an invocation happens, the symbolic reference is
            // resolved (through the module's symbol table). The opcode is
            // replaced with `invoke` and the constant changed to point to the
            // function object in memory. This avoids the overhead of hashmap
            // lookup with every single invocation, but does not require the
            // whole bytecode to be checked and doctored at load time.
            OrbitValue     callee, symbol;
            uint16_t    idx;
            
        CASE_OP(invoke_sym):
            
            idx = READ16();
            symbol = fn->module->constants[idx];
            orbit_gcMapGet(vm->dispatchTable, symbol, &callee);
            
            // replace the opcode in the bytecode stream so that future calls
            // can use the direct reference.
            ip[-3] = CODE_invoke;
            fn->module->constants[idx] = callee;
            
            // Start invocation.
            goto do_invoke;
            
        CASE_OP(invoke):
            // Invoke a function by direct reference: by then, the entry in the
            // run-time constant pool points to a function object rather than
            // a string, and we can just go along.
            callee = fn->module->constants[READ16()];
        do_invoke:
            if(!IS_FUNCTION(callee)) return false;
            // First, we need to store the data brought up into locals back
            // into the task's frame stack.
            frame->ip = ip;
            
            switch(AS_FUNCTION(callee)->kind) {
            case ORBIT_FK_NATIVE:
                // Get the pointer to the function object for convenience
                fn = AS_FUNCTION(callee);
                orbit_vmEnsureFrames(vm, task);
                orbit_vmEnsureStack(vm, task, fn->stackEffect);
                
                // setup a new frame on the task's call stack
                frame = &task->frames[task->frameCount++];
                frame->task = task;
                frame->function = fn;
                frame->ip = fn->native.byteCode;
                
                // The stack base points to the first parameter
                frame->stackBase = task->sp - fn->arity;
                locals = frame->stackBase;
                
                // Move the stack pointer up so we have room reserved for
                // local variables
                task->sp += fn->localCount;
                
                // And now we bring up the new frame's IP into the local.
                // NEXT() will start the new function.
                ip = frame->ip;
                NEXT();
                break;
                
            case ORBIT_FK_FOREIGN:
            
                if(AS_FUNCTION(callee)->foreign(vm, task->sp - AS_FUNCTION(callee)->arity)) {
                    task->sp -= (AS_FUNCTION(callee)->arity - 1);
                } else {
                    task->sp -= AS_FUNCTION(callee)->arity;
                }
                NEXT();
                break;
            }
        }
        
        {
            // When we reach `ret`, the function that has just finished its
            // job might not have left a clean stack. Functions in orbit
            // consume their parameters: they are considered off the stack
            // once the function returns. To do that, we reset the stack
            // pointer to the start of the frame. For ret_val, the return
            // value is popped off the stack before reseting sp, and pushed
            // back on top after.
            
            OrbitValue returnValue;
        CASE_OP(ret_val):
            returnValue = POP();
            task->sp = frame->stackBase;
            PUSH(returnValue);
            goto do_return;
            
        CASE_OP(ret):
            // We reset the stack pointer first, before we loose track of the 
            // ending call frame.
            task->sp = frame->stackBase;
            
        do_return:
            if(--task->frameCount == 0) return true;
            
            // Now we can bring the old frame's pointers back up in the
            // locals. After this, the call to NEXT() will resume execution of
            // the calling function.
            frame = &task->frames[task->frameCount-1];
            fn = frame->function;
            ip = frame->ip;
            locals = frame->stackBase;
            NEXT();
        }
        
        
        {
            OrbitValue class;
            uint16_t idx;
        CASE_OP(init_sym):
            
            idx = READ8();
            OrbitValue symbol = fn->module->constants[idx];
            orbit_gcMapGet(vm->classes, symbol, &class);
            // replace the opcode in the bytecode stream so that future calls
            // can use the direct reference.
            ip[-3] = CODE_init;
            fn->module->constants[idx] = class;
            // Start invocation.
            goto do_init;
            
        CASE_OP(init):
            class = fn->module->constants[READ16()];
            if(!IS_CLASS(class)) return false;
        do_init:
            PUSH(MAKE_OBJECT(orbit_gcInstanceNew(vm, AS_CLASS(class))));
            NEXT();
        }
            
        CASE_OP(debug_prt):
            {
                OrbitValue tos = PEEK();
                if(IS_NUM(tos)) {
                    fprintf(stderr, "TOS: %lf\n", AS_NUM(tos));
                }
                else if(IS_NIL(tos)) {
                    fprintf(stderr, "TOS: nil\n");
                }
                else if(IS_BOOL(tos)) {
                    fprintf(stderr, "TOS: %s\n",
                            IS_TRUE(tos) ? "true" : "false");
                }
                else if(IS_STRING(tos)) {
                    fprintf(stderr, "TOS: \"%.*s\"\n",
                            (int)AS_STRING(tos)->length,
                            AS_STRING(tos)->data);
                }
                else {
                    fprintf(stderr, "TOS: @%p\n", AS_OBJECT(tos));
                }
            }
            NEXT();
    }
    
    return false;
}
