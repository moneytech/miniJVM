

#include "../utils/d_type.h"
#include "jvm.h"
#include "jvm_util.h"
#include "jit.h"


/* ==================================opcode implementation =============================*/




static inline void _op_load_1_slot(RuntimeStack *stack, Runtime *runtime, s32 i) {
    push_int(stack, runtime->localvar[i].ivalue);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    StackEntry entry;
    peek_entry(stack->sp - 1, &entry);
    invoke_deepth(runtime);
    jvm_printf("load_1slot : load localvar[%d] value %lld into stack\n", i, entry.lvalue);
#endif
}

static inline void _op_load_refer(RuntimeStack *stack, Runtime *runtime, s32 i) {
    push_ref(stack, localvar_getRefer(runtime->localvar, i));
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    StackEntry entry;
    peek_entry(stack->sp - 1, &entry);
    invoke_deepth(runtime);
    jvm_printf("load_ref : load localvar[%d] value [%llx] into stack\n", i, entry.rvalue);
#endif
}

static inline void _op_load_2_slot(RuntimeStack *stack, Runtime *runtime, s32 i) {
    push_long(stack, localvar_getLong(runtime->localvar, i));
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    localvar_getLong(runtime->localvar, i)StackEntry entry;
    peek_entry(stack->sp - 1, &entry);
    invoke_deepth(runtime);
    jvm_printf("load_2slot : load localvar[%d] value %lld/[%llx] into stack\n", i, entry.lvalue, entry.rvalue);
#endif
}


static inline void _op_store_1_slot(RuntimeStack *stack, Runtime *runtime, s32 i) {
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    StackEntry entry;
    peek_entry(stack->sp - 1, &entry);
    invoke_deepth(runtime);
    jvm_printf("store_1slot : save %llx/%lld into localvar[%d]\n", entry.rvalue, entry.lvalue, i);
#endif
    localvar_setInt(runtime->localvar, i, (--stack->sp)->ivalue);
}

static inline void _op_store_refer(RuntimeStack *stack, Runtime *runtime, s32 i) {
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    StackEntry entry;
    peek_entry(stack->sp - 1, &entry);
    invoke_deepth(runtime);
    jvm_printf("store_ref : save [%llx] into localvar[%d]\n", entry.rvalue, i);
#endif
    //localvar_setRefer(runtime->localvar, i, pop_ref(stack));//must pop_ref
    //MUST process returnaddress
    runtime->localvar[i].rvalue = pop_ref(stack);
    runtime->localvar[i].type = stack->sp->type; //the type maybe reference or returnaddress type

}


static inline void _op_store_2_slot(RuntimeStack *stack, Runtime *runtime, s32 i) {
    s64 v = pop_long(stack);
    localvar_setLong(runtime->localvar, i, v);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    StackEntry entry;
    peek_entry(stack->sp - 2, &entry);
    invoke_deepth(runtime);
    jvm_printf("store_2slot : save %llx/%lld into localvar[%d]\n", entry.rvalue, entry.lvalue, i);
#endif
}

static inline void _op_ldc_impl(RuntimeStack *stack, JClass *clazz, Runtime *runtime, s32 index) {

    ConstantItem *item = class_get_constant_item(clazz, index);
    switch (item->tag) {
        case CONSTANT_INTEGER:
        case CONSTANT_FLOAT: {
            s32 v = class_get_constant_integer(clazz, index);
            push_int(stack, v);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
            invoke_deepth(runtime);
            jvm_printf("ldc: [%x] \n", v);
#endif
            break;
        }
        case CONSTANT_STRING_REF: {
            ConstantUTF8 *cutf = class_get_constant_utf8(clazz, class_get_constant_stringref(clazz, index)->stringIndex);
            push_ref(stack, (__refer) cutf->jstr);


#if _JVM_DEBUG_BYTECODE_DETAIL > 5
            invoke_deepth(runtime);
            jvm_printf("ldc: [%llx] =\"%s\"\n", (s64) (intptr_t) cutf->jstr, utf8_cstr(cutf->utfstr));
#endif
            break;
        }
        case CONSTANT_CLASS: {
            JClass *cl = classes_load_get(class_get_constant_classref(clazz, index)->name, runtime);
            if (!cl->ins_class) {
                cl->ins_class = insOfJavaLangClass_create_get(runtime, cl);
            }
            push_ref(stack, cl->ins_class);
            break;
        }
        default: {
            push_long(stack, 0);
            jvm_printf("ldc: something not implemention \n");
        }
    }
}

static inline void _op_iconst_n(RuntimeStack *stack, Runtime *runtime, s32 i) {
    push_int(stack, i);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    invoke_deepth(runtime);
    jvm_printf("iconst_%d: push %d into stack\n", i, i);
#endif
}

static inline void _op_dconst_n(RuntimeStack *stack, Runtime *runtime, f64 d) {
    push_double(stack, d);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    invoke_deepth(runtime);
    jvm_printf("dconst_%d: push %lf into stack\n", (s32) (d), d);
#endif
}

static inline void _op_fconst_n(RuntimeStack *stack, Runtime *runtime, f32 f) {
    push_float(stack, f);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    invoke_deepth(runtime);
    jvm_printf("fconst_%f: push %f into stack\n", (s32) f, f);
#endif
}

static inline void _op_lconst_n(RuntimeStack *stack, Runtime *runtime, s64 i) {

    push_long(stack, i);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
    invoke_deepth(runtime);
    jvm_printf("lconst_%lld: push %lld into stack\n", i, i);
#endif
}


void _op_notsupport(u8 *opCode, Runtime *runtime) {
    invoke_deepth(runtime);
    jvm_printf("not support instruct [%x]\n", opCode[0]);
    exit(-3);
}

//----------------------------------  tool func  ------------------------------------------

ExceptionTable *
_find_exception_handler(Runtime *runtime, Instance *exception, CodeAttribute *ca, s32 offset, s32 *ret_index) {

    s32 i;
    ExceptionTable *e = ca->exception_table;
    for (i = 0; i < ca->exception_table_length; i++) {

        if (offset >= (e + i)->start_pc
            && offset <= (e + i)->end_pc) {
            if (!(e + i)->catch_type) {
                *ret_index = i;
                return e + i;
            }
            ConstantClassRef *ccr = class_get_constant_classref(runtime->clazz, (e + i)->catch_type);
            JClass *catchClass = classes_load_get(ccr->name, runtime);
            if (instance_of(catchClass, exception, runtime)) {
                *ret_index = i;
                return e + i;
            }
        }
    }
    return NULL;
}


s32 exception_handle(RuntimeStack *stack, Runtime *runtime) {

    StackEntry entry;
    peek_entry(stack->sp - 1, &entry);
    Instance *ins = entry_2_refer(&entry);
    CodeAttribute *ca = runtime->method->converted_code;

#if _JVM_DEBUG_BYTECODE_DETAIL > 3
    JClass *clazz = runtime->clazz;
    s32 lineNum = getLineNumByIndex(runtime->method->converted_code, (s32) (runtime->pc - runtime->method->converted_code->code));
    jvm_printf("Exception   at %s.%s(%s.java:%d)\n",
               utf8_cstr(clazz->name), utf8_cstr(runtime->method->name),
               utf8_cstr(clazz->name),
               lineNum
    );
#endif
    s32 index = 0;
    ExceptionTable *et = _find_exception_handler(runtime, ins, ca, (s32) (runtime->pc - ca->code), &index);
    if (et == NULL) {
        pop_empty(stack);
        localvar_dispose(runtime);
        push_ref(stack, ins);
        return 0;
    } else {
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
        jvm_printf("Exception : %s\n", utf8_cstr(ins->mb.clazz->name));
#endif
        runtime->pc = (ca->code + et->handler_pc);
        jit_set_exception_jump_addr(runtime, ca, index);
        return 1;
    }

}

s32 _jarray_check_exception(Instance *arr, s32 index, Runtime *runtime) {
    if (!arr) {
        Instance *exception = exception_create(JVM_EXCEPTION_NULLPOINTER, runtime);
        push_ref(runtime->stack, (__refer) exception);
    } else if (index >= arr->arr_length || index < 0) {
        Instance *exception = exception_create(JVM_EXCEPTION_ARRAYINDEXOUTOFBOUNDS, runtime);
        push_ref(runtime->stack, (__refer) exception);
    } else {
        return RUNTIME_STATUS_NORMAL;
    }
    return RUNTIME_STATUS_EXCEPTION;
}

void _null_throw_exception(RuntimeStack *stack, Runtime *runtime) {
    Instance *exception = exception_create(JVM_EXCEPTION_NULLPOINTER, runtime);
    push_ref(stack, (__refer) exception);
}

void _nosuchmethod_check_exception(c8 *mn, RuntimeStack *stack, Runtime *runtime) {
    Instance *exception = exception_create_str(JVM_EXCEPTION_NOSUCHMETHOD, runtime, mn);
    push_ref(stack, (__refer) exception);
}

void _nosuchfield_check_exception(c8 *mn, RuntimeStack *stack, Runtime *runtime) {
    Instance *exception = exception_create_str(JVM_EXCEPTION_NOSUCHFIELD, runtime, mn);
    push_ref(stack, (__refer) exception);
}

void _arrithmetic_throw_exception(RuntimeStack *stack, Runtime *runtime) {
    Instance *exception = exception_create(JVM_EXCEPTION_ARRITHMETIC, runtime);
    push_ref(stack, (__refer) exception);
}

void _checkcast_throw_exception(RuntimeStack *stack, Runtime *runtime) {
    Instance *exception = exception_create(JVM_EXCEPTION_CLASSCAST, runtime);
    push_ref(stack, (__refer) exception);
}


static s32 filterClassName(Utf8String *clsName) {
    if (utf8_indexof_c(clsName, "com/sun") < 0
        && utf8_indexof_c(clsName, "java/") < 0
        && utf8_indexof_c(clsName, "javax/") < 0) {
        return 1;
    }
    return 0;
}

s32 invokedynamic_prepare(Runtime *runtime, BootstrapMethod *bootMethod, ConstantInvokeDynamic *cid) {
    // =====================================================================
    //         run bootstrap method java.lang.invoke.LambdaMetafactory
    //
    //         public static CallSite metafactory(
    //                   MethodHandles.Lookup caller,
    //                   String invokedName,
    //                   MethodType invokedType,
    //                   MethodType samMethodType,
    //                   MethodHandle implMethod,
    //                   MethodType instantiatedMethodType
    //                   )
    //
    //          to generate Lambda Class implementation specify interface
    //          and new a callsite
    // =====================================================================
    JClass *clazz = runtime->clazz;
    RuntimeStack *stack = runtime->stack;

    //parper bootMethod parameter
    Instance *lookup = method_handles_lookup_create(runtime, clazz);
    push_ref(stack, lookup); //lookup

    Utf8String *ustr_invokeName = class_get_constant_utf8(clazz, class_get_constant_name_and_type(clazz, cid->nameAndTypeIndex)->nameIndex)->utfstr;
    Instance *jstr_invokeName = jstring_create(ustr_invokeName, runtime);
    push_ref(stack, jstr_invokeName); //invokeName

    Utf8String *ustr_invokeType = class_get_constant_utf8(clazz, class_get_constant_name_and_type(clazz, cid->nameAndTypeIndex)->typeIndex)->utfstr;
    Instance *mt_invokeType = method_type_create(runtime, ustr_invokeType);
    push_ref(stack, mt_invokeType); //invokeMethodType

    //other bootMethod parameter

    s32 i;
    for (i = 0; i < bootMethod->num_bootstrap_arguments; i++) {
        ConstantItem *item = class_get_constant_item(clazz, bootMethod->bootstrap_arguments[i]);
        switch (item->tag) {
            case CONSTANT_METHOD_TYPE: {
                ConstantMethodType *cmt = (ConstantMethodType *) item;
                Utf8String *arg = class_get_constant_utf8(clazz, cmt->descriptor_index)->utfstr;
                Instance *mt = method_type_create(runtime, arg);
                push_ref(stack, mt);
                break;
            }
            case CONSTANT_STRING_REF: {
                ConstantStringRef *csr = (ConstantStringRef *) item;
                Utf8String *arg = class_get_constant_utf8(clazz, csr->stringIndex)->utfstr;
                Instance *spec = jstring_create(arg, runtime);
                push_ref(stack, spec);
                break;
            }
            case CONSTANT_METHOD_HANDLE: {
                ConstantMethodHandle *cmh = (ConstantMethodHandle *) item;
                MethodInfo *mip = find_methodInfo_by_methodref(clazz, cmh->reference_index, runtime);
                Instance *mh = method_handle_create(runtime, mip, cmh->reference_kind);
                push_ref(stack, mh);
                break;
            }
            default: {
                jvm_printf("invokedynamic para parse error.");
            }
        }

    }

    //get bootmethod
    MethodInfo *boot_m = find_methodInfo_by_methodref(clazz, class_get_method_handle(clazz, bootMethod->bootstrap_method_ref)->reference_index, runtime);
    if (!boot_m) {
        ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, class_get_method_handle(clazz, bootMethod->bootstrap_method_ref)->reference_index);
        _nosuchmethod_check_exception(utf8_cstr(cmr->name), stack, runtime);
        return RUNTIME_STATUS_EXCEPTION;
    } else {
        s32 ret = execute_method_impl(boot_m, runtime);
        if (ret == RUNTIME_STATUS_NORMAL) {
            MethodInfo *finder = find_methodInfo_by_name_c("org/mini/reflect/vm/LambdaUtil",
                                                           "getMethodInfoHandle",
                                                           "(Ljava/lang/invoke/CallSite;)J",
                                                           runtime);
            if (!finder) {
                _nosuchmethod_check_exception("getMethodInfoHandle", stack, runtime);
                return RUNTIME_STATUS_EXCEPTION;
            } else {
                //run finder to convert calsite.target(MethodHandle) to MethodInfo * pointer
                ret = execute_method_impl(finder, runtime);
                if (ret == RUNTIME_STATUS_NORMAL) {
                    MethodInfo *make = (MethodInfo *) (intptr_t) pop_long(stack);
                    bootMethod->make = make;
                }
            }
        } else {
            return ret;
        }
    }
    return RUNTIME_STATUS_NORMAL;
}

s32 checkcast(Runtime *runtime, Instance *ins, s32 typeIdx) {

    JClass *clazz = runtime->clazz;
    if (ins != NULL) {
        if (ins->mb.type == MEM_TYPE_INS) {
            JClass *cl = getClassByConstantClassRef(clazz, typeIdx, runtime);
            if (instance_of(cl, ins, runtime)) {
                return 1;
            }
        } else if (ins->mb.type == MEM_TYPE_ARR) {
            Utf8String *utf = class_get_constant_classref(clazz, typeIdx)->name;
            u8 ch = utf8_char_at(utf, 1);
            if (getDataTypeIndex(ch) == ins->mb.clazz->mb.arr_type_index) {
                return 1;
            }
        } else if (ins->mb.type == MEM_TYPE_CLASS) {
            Utf8String *utf = class_get_constant_classref(clazz, typeIdx)->name;
            if (utf8_equals_c(utf, STR_CLASS_JAVA_LANG_CLASS)) {
                return 1;
            }
        }
    } else {
        return 1;
    }
    return 0;
}

/**
* 把堆栈中的方法调用参数存入方法本地变量
* 调用方法前，父程序把函数参数推入堆栈，方法调用时，需要把堆栈中的参数存到本地变量
* @param method  method
* @param father  runtime of father
* @param son     runtime of son
*/



static inline void _synchronized_lock_method(MethodInfo *method, Runtime *runtime) {
    //synchronized process
    {
        if (method->is_static) {
            runtime->lock = (MemoryBlock *) runtime->clazz;
        } else {
            runtime->lock = (MemoryBlock *) localvar_getRefer(runtime->localvar, 0);
        }
        jthread_lock(runtime->lock, runtime);
    }
}

static inline void _synchronized_unlock_method(MethodInfo *method, Runtime *runtime) {
    //synchronized process
    {
        jthread_unlock(runtime->lock, runtime);
        runtime->lock = NULL;
    }
}

/**
 *    only static and special can be optimize , invokevirtual and invokeinterface may called by diff instance
 * @param subm
 * @param parent_method_code
 */
static inline void _optimize_empty_method_call(MethodInfo *subm, CodeAttribute *parent_ca, u8 *parent_method_code) {
    CodeAttribute *ca = subm->converted_code;
    u8 *parent_jit_code = &parent_ca->bytecode_for_jit[parent_method_code - parent_ca->code];
    if (ca && ca->code_length == 1 && *ca->code == op_return) {//empty method, do nothing
        s32 paras = subm->para_slots;//

        if (paras == 0) {
            *parent_method_code = op_nop;
            *(parent_method_code + 1) = op_nop;
            *(parent_method_code + 2) = op_nop;

            *parent_jit_code = op_nop;
            *(parent_jit_code + 1) = op_nop;
            *(parent_jit_code + 2) = op_nop;
        } else if (paras == 1) {
            *parent_method_code = op_pop;
            *(parent_method_code + 1) = op_nop;
            *(parent_method_code + 2) = op_nop;

            *parent_jit_code = op_pop;
            *(parent_jit_code + 1) = op_nop;
            *(parent_jit_code + 2) = op_nop;
        } else if (paras == 2) {
            *parent_method_code = op_pop2;
            *(parent_method_code + 1) = op_nop;
            *(parent_method_code + 2) = op_nop;

            *parent_jit_code = op_pop2;
            *(parent_jit_code + 1) = op_nop;
            *(parent_jit_code + 2) = op_nop;
        }
    }
}


s32 execute_method_impl(MethodInfo *method, Runtime *pruntime) {


    Runtime *runtime;
    register u8 *ip;
    JClass *clazz;
    RuntimeStack *stack;


    s32 ret = RUNTIME_STATUS_NORMAL;
    runtime = runtime_create_inl(pruntime);
    clazz = method->_this_class;
    runtime->clazz = clazz;
    runtime->method = method;
    while (clazz->status < CLASS_STATUS_CLINITING) {
        class_clinit(clazz, runtime);
    }


//    if (utf8_equals_c(method->name, "printerr")
//                && utf8_equals_c(clazz->name, "java/lang/SpecTest")
//            ) {
//        s32 debug = 1;
//    }

    stack = runtime->stack;

    if (!(method->is_native)) {
        CodeAttribute *ca = method->converted_code;
        if (ca) {

            if (stack->max_size < (stack->sp - stack->store) + ca->max_stack) {
                Utf8String *ustr = utf8_create();
                getRuntimeStack(runtime, ustr);
                jvm_printf("Stack overflow :\n %s\n", utf8_cstr(ustr));
                utf8_destory(ustr);
                exit(1);
            }
            localvar_init(runtime, ca->max_locals, method->para_slots);
            if (method->is_sync)_synchronized_lock_method(method, runtime);



//            if (utf8_equals_c(method->_this_class->name, "test/SpecTest")) {
//                jvm_printf("call %s\n", method->name->data);
//            }
            if (JIT_ENABLE && ca->jit.state == JIT_GEN_SUCCESS) {
                //jvm_printf("jit call %s.%s()\n", method->_this_class->name->data, method->name->data);
                ca->jit.func(method, runtime);
                switch (method->return_slots) {
                    case 0: {// V
                        localvar_dispose(runtime);
                        break;
                    }
                    case 1: { // F I R
                        StackEntry entry;
                        peek_entry(stack->sp - method->return_slots, &entry);
                        localvar_dispose(runtime);
                        push_entry(stack, &entry);
                        break;
                    }
                    case 2: {//J D return type , 2slots
                        s64 v = pop_long(stack);
                        localvar_dispose(runtime);
                        push_long(stack, v);
                        break;
                    }
                    default: {
                        break;
                    }
                }
            } else {
                if (JIT_ENABLE && ca->jit.state == JIT_GEN_UNKNOW) {
                    if (ca->jit.interpreted_count++ > JIT_COMPILE_EXEC_COUNT) {
                        construct_jit(method, runtime);
                    }
                }

                //
                ip = ca->code;

                do {
                    runtime->pc = ip;
                    u8 cur_inst = *ip;

                    JavaThreadInfo *threadInfo = runtime->threadInfo;
                    if (jdwp_enable) {
                        //breakpoint
                        if (method->breakpoint) {
                            jdwp_check_breakpoint(runtime);
                        }
                        //debug step
                        if (threadInfo->jdwp_step.active) {//单步状态
                            threadInfo->jdwp_step.bytecode_count++;
                            jdwp_check_debug_step(runtime);

                        }
                    }
                    //process thread suspend
                    if (threadInfo->suspend_count) {
                        if (threadInfo->is_interrupt) {
                            ret = RUNTIME_STATUS_INTERRUPT;
                            break;
                        }
                        check_suspend_and_pause(runtime);
                    }


#if _JVM_DEBUG_PROFILE
                    s64 spent = 0;
                    s64 start_at = nanoTime();
#endif

                    /* ==================================opcode start =============================*/
#ifdef __JVM_DEBUG__
                    s64 inst_pc = runtime->pc - ca->code;
#endif
                    switch (cur_inst) {

                        case op_nop: {
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("nop\n");
#endif
                            ip++;

                            break;
                        }

                        case op_aconst_null: {
                            push_ref(stack, NULL);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("aconst_null: push %d into stack\n", 0);
#endif
                            ip++;

                            break;
                        }

                        case op_iconst_m1: {
                            _op_iconst_n(stack, runtime, -1);
                            ip++;
                            break;
                        }


                        case op_iconst_0: {
                            _op_iconst_n(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_iconst_1: {
                            _op_iconst_n(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_iconst_2: {
                            _op_iconst_n(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_iconst_3: {
                            _op_iconst_n(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_iconst_4: {
                            _op_iconst_n(stack, runtime, 4);
                            ip++;
                            break;
                        }


                        case op_iconst_5: {
                            _op_iconst_n(stack, runtime, 5);
                            ip++;
                            break;
                        }


                        case op_lconst_0: {
                            _op_lconst_n(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_lconst_1: {
                            _op_lconst_n(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_fconst_0: {
                            _op_fconst_n(stack, runtime, 0.0f);
                            ip++;
                            break;
                        }


                        case op_fconst_1: {
                            _op_fconst_n(stack, runtime, 1.0f);
                            ip++;
                            break;
                        }


                        case op_fconst_2: {
                            _op_fconst_n(stack, runtime, 2.0f);
                            ip++;
                            break;
                        }


                        case op_dconst_0: {
                            _op_dconst_n(stack, runtime, 0.0f);
                            ip++;
                            break;
                        }


                        case op_dconst_1: {
                            _op_dconst_n(stack, runtime, 1.0f);
                            ip++;
                            break;
                        }


                        case op_bipush: {

                            s32 value = (s8) ip[1];
                            push_int(stack, value);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("bipush a byte %d onto the stack \n", value);
#endif
                            ip += 2;

                            break;
                        }


                        case op_sipush: {
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("sipush value %d\n", *((s16 *) (ip + 1)));
#endif
                            push_int(stack, *((s16 *) (ip + 1)));
                            ip += 3;

                            break;
                        }


                        case op_ldc: {
                            s32 index = ip[1];
                            ip += 2;
                            _op_ldc_impl(stack, clazz, runtime, index);
                            break;
                        }


                        case op_ldc_w: {
                            _op_ldc_impl(stack, clazz, runtime, *((u16 *) (ip + 1)));
                            ip += 3;
                            break;
                        }


                        case op_ldc2_w: {

                            s64 value = class_get_constant_long(clazz, *((u16 *) (ip + 1)));//long or double

                            push_long(stack, value);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ldc2_w: push a constant(%d) [%llx] onto the stack \n", *((u16 *) (ip + 1)), value);
#endif
                            ip += 3;

                            break;
                        }


                        case op_iload:
                        case op_fload: {
                            _op_load_1_slot(stack, runtime, (u8) ip[1]);
                            ip += 2;
                            break;
                        }

                        case op_aload: {
                            _op_load_refer(stack, runtime, (u8) ip[1]);
                            ip += 2;
                            break;
                        }


                        case op_lload:
                        case op_dload: {
                            _op_load_2_slot(stack, runtime, (u8) ip[1]);
                            ip += 2;
                            break;
                        }


                        case op_iload_0: {
                            _op_load_1_slot(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_iload_1: {
                            _op_load_1_slot(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_iload_2: {
                            _op_load_1_slot(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_iload_3: {
                            _op_load_1_slot(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_lload_0: {
                            _op_load_2_slot(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_lload_1: {
                            _op_load_2_slot(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_lload_2: {
                            _op_load_2_slot(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_lload_3: {
                            _op_load_2_slot(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_fload_0: {
                            _op_load_1_slot(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_fload_1: {
                            _op_load_1_slot(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_fload_2: {
                            _op_load_1_slot(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_fload_3: {
                            _op_load_1_slot(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_dload_0: {
                            _op_load_2_slot(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_dload_1: {
                            _op_load_2_slot(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_dload_2: {
                            _op_load_2_slot(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_dload_3: {
                            _op_load_2_slot(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_aload_0: {
                            _op_load_refer(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_aload_1: {
                            _op_load_refer(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_aload_2: {
                            _op_load_refer(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_aload_3: {
                            _op_load_refer(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_iaload:
                        case op_faload: {
                            s32 index = pop_int(stack);
                            Instance *arr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(arr, index, runtime);
                            if (!ret) {
                                s32 s = *((s32 *) (arr->arr_body) + index);
                                push_int(stack, s);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("if_aload push arr[%llx].(%d)=%x:%d:%lf into stack\n", (u64) (intptr_t) arr, index,
                                           s, s, *(f32 *) &s);
#endif
                                ip++;
                            } else {
                                goto label_exception_handle;
                            }

                            break;
                        }


                        case op_laload:
                        case op_daload: {
                            s32 index = pop_int(stack);
                            Instance *arr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(arr, index, runtime);
                            if (!ret) {
                                s64 s = *(((s64 *) arr->arr_body) + index);
                                push_long(stack, s);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("ld_aload push arr[%llx].(%d)=%llx:%lld:%lf into stack\n", (u64) (intptr_t) arr, index,
                                           s, s, *(f64 *) &s);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_aaload: {
                            s32 index = pop_int(stack);
                            Instance *arr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(arr, index, runtime);
                            if (!ret) {
                                __refer s = *(((__refer *) arr->arr_body) + index);
                                push_ref(stack, s);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("aaload push arr[%llx].(%d)=%llx:%lld into stack\n", (u64) (intptr_t) arr, index,
                                           (s64) (intptr_t) s, (s64) (intptr_t) s);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_baload: {
                            s32 index = pop_int(stack);
                            Instance *arr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(arr, index, runtime);
                            if (!ret) {
                                s32 s = *(((s8 *) arr->arr_body) + index);
                                push_int(stack, s);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iaload push arr[%llx].(%d)=%x:%d:%lf into stack\n", (u64) (intptr_t) arr, index,
                                           s, s, *(f32 *) &s);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_caload: {
                            s32 index = pop_int(stack);
                            Instance *arr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(arr, index, runtime);
                            if (!ret) {
                                s32 s = *(((u16 *) arr->arr_body) + index);
                                push_int(stack, s);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iaload push arr[%llx].(%d)=%x:%d:%lf into stack\n", (u64) (intptr_t) arr, index,
                                           s, s, *(f32 *) &s);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_saload: {
                            s32 index = pop_int(stack);
                            Instance *arr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(arr, index, runtime);
                            if (!ret) {
                                s32 s = *(((s16 *) arr->arr_body) + index);
                                push_int(stack, s);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iaload push arr[%llx].(%d)=%x:%d:%lf into stack\n", (u64) (intptr_t) arr, index,
                                           s, s, *(f32 *) &s);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_istore:

                        case op_fstore: {
                            _op_store_1_slot(stack, runtime, (u8) ip[1]);
                            ip += 2;
                            break;
                        }

                        case op_astore: {
                            _op_store_refer(stack, runtime, (u8) ip[1]);
                            ip += 2;
                            break;
                        }


                        case op_lstore:

                        case op_dstore: {
                            _op_store_2_slot(stack, runtime, (u8) ip[1]);
                            ip += 2;

                            break;
                        }


                        case op_istore_0: {
                            _op_store_1_slot(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_istore_1: {
                            _op_store_1_slot(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_istore_2: {
                            _op_store_1_slot(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_istore_3: {
                            _op_store_1_slot(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_lstore_0: {
                            _op_store_2_slot(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_lstore_1: {
                            _op_store_2_slot(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_lstore_2: {
                            _op_store_2_slot(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_lstore_3: {
                            _op_store_2_slot(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_fstore_0: {
                            _op_store_1_slot(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_fstore_1: {
                            _op_store_1_slot(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_fstore_2: {
                            _op_store_1_slot(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_fstore_3: {
                            _op_store_1_slot(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_dstore_0: {
                            _op_store_2_slot(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_dstore_1: {
                            _op_store_2_slot(stack, runtime, 1);
                            ip++;
                            break;
                        }

                        case op_dstore_2: {

                            _op_store_2_slot(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_dstore_3: {
                            _op_store_2_slot(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_astore_0: {
                            _op_store_refer(stack, runtime, 0);
                            ip++;
                            break;
                        }


                        case op_astore_1: {
                            _op_store_refer(stack, runtime, 1);
                            ip++;
                            break;
                        }


                        case op_astore_2: {
                            _op_store_refer(stack, runtime, 2);
                            ip++;
                            break;
                        }


                        case op_astore_3: {
                            _op_store_refer(stack, runtime, 3);
                            ip++;
                            break;
                        }


                        case op_fastore:
                        case op_iastore: {
                            s32 i = pop_int(stack);
                            s32 index = pop_int(stack);
                            Instance *jarr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(jarr, index, runtime);
                            if (!ret) {
                                *(((s32 *) jarr->arr_body) + index) = i;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iastore: save array[%llx].(%d)=%d)\n",
                                           (s64) (intptr_t) jarr, index, i);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_dastore:
                        case op_lastore: {
                            s64 j = pop_long(stack);
                            s32 index = pop_int(stack);
                            Instance *jarr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(jarr, index, runtime);
                            if (!ret) {
                                *(((s64 *) jarr->arr_body) + index) = j;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iastore: save array[%llx].(%d)=%lld)\n",
                                           (s64) (intptr_t) jarr, index, j);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_aastore: {
                            __refer r = pop_ref(stack);
                            s32 index = pop_int(stack);
                            Instance *jarr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(jarr, index, runtime);
                            if (!ret) {
                                *(((__refer *) jarr->arr_body) + index) = r;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iastore: save array[%llx].(%d)=%llx)\n",
                                           (s64) (intptr_t) jarr, index, (s64) (intptr_t) r);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_bastore: {
                            s32 i = pop_int(stack);
                            s32 index = pop_int(stack);
                            Instance *jarr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(jarr, index, runtime);
                            if (!ret) {
                                *(((s8 *) jarr->arr_body) + index) = (s8) i;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iastore: save array[%llx].(%d)=%d)\n",
                                           (s64) (intptr_t) jarr, index, i);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_castore: {
                            s32 i = pop_int(stack);
                            s32 index = pop_int(stack);
                            Instance *jarr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(jarr, index, runtime);
                            if (!ret) {
                                *(((u16 *) jarr->arr_body) + index) = (u16) i;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iastore: save array[%llx].(%d)=%d)\n",
                                           (s64) (intptr_t) jarr, index, i);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_sastore: {
                            s32 i = pop_int(stack);
                            s32 index = pop_int(stack);
                            Instance *jarr = (Instance *) pop_ref(stack);
                            ret = _jarray_check_exception(jarr, index, runtime);
                            if (!ret) {
                                *(((s16 *) jarr->arr_body) + index) = (s16) i;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("iastore: save array[%llx].(%d)=%d)\n",
                                           (s64) (intptr_t) jarr, index, i);
#endif

                                ip++;
                            } else {
                                goto label_exception_handle;
                            }
                            break;
                        }


                        case op_pop: {
                            pop_empty(stack);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("pop\n");
#endif
                            ip++;

                            break;
                        }


                        case op_pop2: {
                            pop_empty(stack);
                            pop_empty(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("pop2\n");
#endif
                            ip++;

                            break;
                        }


                        case op_dup: {

                            push_entry(stack, stack->sp - 1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dup\n");
#endif
                            ip++;

                            break;
                        }


                        case op_dup_x1: {
                            StackEntry entry1;
                            pop_entry(stack, &entry1);
                            StackEntry entry2;
                            pop_entry(stack, &entry2);

                            push_entry(stack, &entry1);
                            push_entry(stack, &entry2);
                            push_entry(stack, &entry1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dup_x1\n");
#endif
                            ip++;

                            break;
                        }


                        case op_dup_x2: {
                            StackEntry entry1;
                            pop_entry(stack, &entry1);
                            StackEntry entry2;
                            pop_entry(stack, &entry2);
                            StackEntry entry3;
                            pop_entry(stack, &entry3);

                            push_entry(stack, &entry1);
                            push_entry(stack, &entry3);
                            push_entry(stack, &entry2);
                            push_entry(stack, &entry1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dup_x2 \n");
#endif
                            ip++;

                            break;
                        }


                        case op_dup2: {
                            StackEntry entry1;
                            peek_entry(stack->sp - 1, &entry1);
                            StackEntry entry2;
                            peek_entry(stack->sp - 2, &entry2);

                            push_entry(stack, &entry2);
                            push_entry(stack, &entry1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dup2\n");
#endif
                            ip++;

                            break;
                        }


                        case op_dup2_x1: {
                            StackEntry entry1;
                            pop_entry(stack, &entry1);
                            StackEntry entry2;
                            pop_entry(stack, &entry2);
                            StackEntry entry3;
                            pop_entry(stack, &entry3);

                            push_entry(stack, &entry2);
                            push_entry(stack, &entry1);
                            push_entry(stack, &entry3);
                            push_entry(stack, &entry2);
                            push_entry(stack, &entry1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dup2_x1\n");
#endif
                            ip++;

                            break;
                        }


                        case op_dup2_x2: {
                            StackEntry entry1;
                            pop_entry(stack, &entry1);
                            StackEntry entry2;
                            pop_entry(stack, &entry2);
                            StackEntry entry3;
                            pop_entry(stack, &entry3);
                            StackEntry entry4;
                            pop_entry(stack, &entry4);

                            push_entry(stack, &entry2);
                            push_entry(stack, &entry1);
                            push_entry(stack, &entry4);
                            push_entry(stack, &entry3);
                            push_entry(stack, &entry2);
                            push_entry(stack, &entry1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dup2_x2\n");
#endif
                            ip++;

                            break;
                        }


                        case op_swap: {

                            StackEntry entry1;
                            pop_entry(stack, &entry1);
                            StackEntry entry2;
                            pop_entry(stack, &entry2);

                            push_entry(stack, &entry1);
                            push_entry(stack, &entry2);


#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("swap\n");
#endif
                            ip++;

                            break;
                        }


                        case op_iadd: {

                            s32 value1 = pop_int(stack);
                            s32 value2 = pop_int(stack);
                            s32 result = value1 + value2;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("iadd: %d + %d = %d\n", value1, value2, result);
#endif
                            push_int(stack, result);
                            ip++;

                            break;
                        }


                        case op_ladd: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);
                            s64 result = value2 + value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ladd: %lld + %lld = %lld\n", value2, value1, result);
#endif
                            push_long(stack, result);
                            ip++;

                            break;
                        }


                        case op_fadd: {
                            f32 value1 = pop_float(stack);
                            f32 value2 = pop_float(stack);
                            f32 result = value2 + value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("fadd: %lf + %lf = %lf\n", value2, value1, result);
#endif
                            push_float(stack, result);
                            ip++;

                            break;
                        }


                        case op_dadd: {

                            f64 value1 = pop_double(stack);
                            f64 value2 = pop_double(stack);
                            f64 result = value1 + value2;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dadd: %lf + %lf = %lf\n", value1, value2, result);
#endif
                            push_double(stack, result);
                            ip++;

                            break;
                        }


                        case op_isub: {
                            s32 value2 = pop_int(stack);
                            s32 value1 = pop_int(stack);
                            s32 result = value1 - value2;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("isub : %d - %d = %d\n", value1, value2, result);
#endif
                            push_int(stack, result);
                            ip++;

                            break;
                        }


                        case op_lsub: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);
                            s64 result = value2 - value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lsub: %lld - %lld = %lld\n", value2, value1, result);
#endif
                            push_long(stack, result);
                            ip++;

                            break;
                        }


                        case op_fsub: {
                            f32 value1 = pop_float(stack);
                            f32 value2 = pop_float(stack);
                            f32 result = value2 - value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("fsub: %f - %f = %f\n", value2, value1, result);
#endif
                            push_float(stack, result);
                            ip++;

                            break;
                        }


                        case op_dsub: {
                            f64 value1 = pop_double(stack);
                            f64 value2 = pop_double(stack);
                            f64 result = value2 - value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dsub: %lf - %lf = %lf\n", value2, value1, result);
#endif
                            push_double(stack, result);
                            ip++;

                            break;
                        }


                        case op_imul: {

                            s32 value1 = pop_int(stack);
                            s32 value2 = pop_int(stack);
                            s32 result = value1 * value2;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("imul: %d * %d = %d\n", value1, value2, result);
#endif
                            push_int(stack, result);
                            ip++;

                            break;
                        }


                        case op_lmul: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);
                            s64 result = value2 * value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lmul: %lld * %lld = %lld\n", value2, value1, result);
#endif
                            push_long(stack, result);
                            ip++;

                            break;
                        }


                        case op_fmul: {
                            f32 value1 = pop_float(stack);
                            f32 value2 = pop_float(stack);
                            f32 result = value1 * value2;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("fmul: %f * %f = %f\n", value1, value2, result);
#endif
                            push_float(stack, result);
                            ip++;

                            break;
                        }


                        case op_dmul: {
                            f64 value1 = pop_double(stack);
                            f64 value2 = pop_double(stack);
                            f64 result = value1 * value2;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dmul: %lf * %lf = %lf\n", value1, value2, result);
#endif
                            push_double(stack, result);
                            ip++;

                            break;
                        }


                        case op_idiv: {

                            s32 value1 = pop_int(stack);
                            s32 value2 = pop_int(stack);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("idiv: %d / %d = %d\n", value1, value2, value2 / value1);
#endif
                            if (!value1) {
                                _arrithmetic_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                s32 result = value2 / value1;
                                push_int(stack, result);
                                ip++;
                            }

                            break;
                        }


                        case op_ldiv: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ldiv: %lld / %lld = %lld\n", value2, value1, value2 / value1);
#endif
                            if (!value1) {
                                _arrithmetic_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                s64 result = value2 / value1;
                                push_long(stack, result);
                                ip++;
                            }

                            break;
                        }


                        case op_fdiv: {
                            f32 value1 = pop_float(stack);
                            f32 value2 = pop_float(stack);
                            f32 result = value2 / value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("fdiv: %f / %f = %f\n", value2, value1, result);
#endif
                            push_float(stack, result);
                            ip++;

                            break;
                        }

                        case op_ddiv: {
                            f64 value1 = pop_double(stack);
                            f64 value2 = pop_double(stack);
                            f64 result = value2 / value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ddiv: %f / %f = %f\n", value2, value1, result);
#endif
                            push_double(stack, result);
                            ip++;

                            break;
                        }


                        case op_irem: {
                            s32 value1 = pop_int(stack);
                            s32 value2 = pop_int(stack);
                            s32 result = value2 % value1;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("irem: %d % %d = %d\n", value2, value1, result);
#endif
                            push_int(stack, result);
                            ip++;

                            break;
                        }


                        case op_lrem: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);
                            s64 result = value2 % value1;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lrem: %lld mod %lld = %lld\n", value2, value1, result);
#endif
                            push_long(stack, result);
                            ip++;

                            break;
                        }


                        case op_frem: {
                            f32 value1 = pop_float(stack);
                            f32 value2 = pop_float(stack);
                            f32 result = value2 - ((int) (value2 / value1) * value1);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("frem: %f % %f = %f\n", value2, value1, result);
#endif
                            push_float(stack, result);
                            ip++;

                            break;
                        }


                        case op_drem: {
                            f64 value1 = pop_double(stack);
                            f64 value2 = pop_double(stack);
                            f64 result = value2 - ((s64) (value2 / value1) * value1);;

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("drem: %lf mod %lf = %lf\n", value2, value1, result);
#endif
                            push_double(stack, result);
                            ip++;

                            break;
                        }


                        case op_ineg: {
                            s32 value1 = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ineg: -(%d) = %d\n", value1, -value1);
#endif
                            push_int(stack, -value1);
                            ip++;

                            break;
                        }


                        case op_lneg: {
                            s64 value1 = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lneg: -(%lld) = %lld\n", value1, -value1);
#endif
                            push_long(stack, -value1);
                            ip++;

                            break;
                        }


                        case op_fneg: {
                            f32 value1 = pop_float(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("fneg: -(%f) = %f\n", value1, -value1);
#endif
                            push_float(stack, -value1);
                            ip++;

                            break;
                        }


                        case op_dneg: {
                            f64 value1 = pop_double(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dneg: -(%lf) = %lf\n", value1, -value1);
#endif
                            push_double(stack, -value1);
                            ip++;

                            break;
                        }


                        case op_ishl: {
                            s32 value1 = pop_int(stack) & 0x1f;
                            s32 value2 = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ishl: %x << %x =%x \n", value2, value1, value2 << value1);
#endif
                            push_int(stack, value2 << value1);
                            ip++;

                            break;
                        }


                        case op_lshl: {
                            s32 value1 = pop_int(stack) & 0x3f;
                            s64 value2 = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lshl: %llx << %x =%llx \n", value2, value1, (value2 << value1));
#endif
                            push_long(stack, value2 << value1);
                            ip++;

                            break;
                        }


                        case op_ishr: {
                            s32 value1 = pop_int(stack) & 0x1f;
                            s32 value2 = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ishr: %x >> %x =%x \n", value2, value1, value2 >> value1);
#endif
                            push_int(stack, value2 >> value1);
                            ip++;

                            break;
                        }


                        case op_lshr: {
                            s32 value1 = pop_int(stack) & 0x3f;
                            s64 value2 = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lshr: %llx >> %x =%llx \n", value2, value1, value2 >> value1);
#endif
                            push_long(stack, value2 >> value1);
                            ip++;

                            break;
                        }


                        case op_iushr: {
                            s32 value1 = pop_int(stack) & 0x1F;
                            u32 value2 = (u32) pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("iushr: %x >>> %x =%x \n", value2, value1, value2 >> value1);
#endif
                            push_int(stack, value2 >> value1);
                            ip++;

                            break;
                        }


                        case op_lushr: {
                            s32 value1 = pop_int(stack) & 0x3f;
                            u64 value2 = (u64) pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lushr: %llx >>> %x =%llx \n", value2, value1, value2 >> value1);
#endif
                            push_long(stack, value2 >> value1);
                            ip++;

                            break;
                        }


                        case op_iand: {
                            s32 value1 = pop_int(stack);
                            s32 value2 = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("iand: %x & %x =%x \n", value2, value1, value2 & value1);
#endif
                            push_int(stack, value2 & value1);
                            ip++;

                            break;
                        }


                        case op_land: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("land: %llx  &  %llx =%llx \n", value2, value1, value2 & value1);
#endif
                            push_long(stack, value2 & value1);
                            ip++;

                            break;
                        }


                        case op_ior: {
                            s32 value1 = pop_int(stack);
                            s32 value2 = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ior: %x & %x =%x \n", value2, value1, value2 | value1);
#endif
                            push_int(stack, value2 | value1);
                            ip++;

                            break;
                        }


                        case op_lor: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lor: %llx  |  %llx =%llx \n", value2, value1, value2 | value1);
#endif
                            push_long(stack, value2 | value1);
                            ip++;

                            break;
                        }


                        case op_ixor: {
                            s32 value1 = pop_int(stack);
                            s32 value2 = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ixor: %x ^ %x =%x \n", value2, value1, value2 ^ value1);
#endif
                            push_int(stack, value2 ^ value1);
                            ip++;

                            break;
                        }


                        case op_lxor: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lxor: %llx  ^  %llx =%llx \n", value2, value1, value2 ^ value1);
#endif
                            push_long(stack, value2 ^ value1);
                            ip++;

                            break;
                        }


                        case op_iinc: {
                            runtime->localvar[(u8) ip[1]].ivalue += (s8) ip[2];
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("iinc: localvar(%d) = %d , inc %d\n", (u8) ip[1], runtime->localvar[(u8) ip[1]].ivalue, (s8) ip[2]);
#endif

                            break;
                        }


                        case op_i2l: {
                            s32 value = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("i2l: %d --> %lld\n", (s32) value, (s64) value);
#endif
                            push_long(stack, (s64) value);
                            ip++;

                            break;
                        }


                        case op_i2f: {
                            s32 value = pop_int(stack);
                            f32 result = (f32) value;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("i2f: %d --> %f\n", (s32) value, result);
#endif
                            push_float(stack, result);
                            ip++;

                            break;
                        }


                        case op_i2d: {
                            s32 value = pop_int(stack);
                            f64 result = value;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("i2d: %d --> %lf\n", (s32) value, result);
#endif
                            push_double(stack, result);
                            ip++;

                            break;
                        }


                        case op_l2i: {
                            s64 value = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("l2i: %d <-- %lld\n", (s32) value, value);
#endif
                            push_int(stack, (s32) value);
                            ip++;

                            break;
                        }


                        case op_l2f: {
                            s64 value = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("l2f: %f <-- %lld\n", (f32) value, value);
#endif
                            push_float(stack, (f32) value);
                            ip++;

                            break;
                        }


                        case op_l2d: {
                            s64 value = pop_long(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("l2d: %lf <-- %lld\n", (f64) value, value);
#endif
                            push_double(stack, (f64) value);
                            ip++;

                            break;
                        }


                        case op_f2i: {
                            f32 value1 = pop_float(stack);
                            s32 result = (s32) value1;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("f2i: %d <-- %f\n", result, value1);
#endif
                            push_int(stack, result);
                            ip++;

                            break;
                        }


                        case op_f2l: {
                            f32 value1 = pop_float(stack);
                            s64 result = (s64) value1;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("f2l: %lld <-- %f\n", result, value1);
#endif
                            push_long(stack, result);
                            ip++;

                            break;
                        }


                        case op_f2d: {
                            f32 value1 = pop_float(stack);
                            f64 result = value1;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("f2d: %f <-- %f\n", result, value1);
#endif
                            push_double(stack, result);
                            ip++;

                            break;
                        }


                        case op_d2i: {
                            f64 value1 = pop_double(stack);
                            s32 result = (s32) value1;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("d2i: %d <-- %lf\n", result, value1);
#endif
                            push_int(stack, result);
                            ip++;

                            break;
                        }


                        case op_d2l: {
                            f64 value1 = pop_double(stack);
                            s64 result = (s64) value1;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("d2l: %lld <-- %lf\n", result, value1);
#endif
                            push_long(stack, result);
                            ip++;

                            break;
                        }


                        case op_d2f: {
                            f64 value1 = pop_double(stack);
                            f32 result = (f32) value1;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("d2f: %f <-- %lf\n", result, value1);
#endif
                            push_float(stack, result);
                            ip++;

                            break;
                        }


                        case op_i2b: {
                            s32 value = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("i2b: %d --> %d\n", (s8) value, value);
#endif
                            push_int(stack, (s8) value);
                            ip++;

                            break;
                        }


                        case op_i2c: {
                            s32 value = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("i2c: %d --> %d\n", (s16) value, value);
#endif
                            push_int(stack, (u16) value);
                            ip++;

                            break;
                        }

                        case op_i2s: {
                            s32 value = pop_int(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("i2s: %d --> %d\n", (s16) value, value);
#endif
                            push_int(stack, (s16) value);
                            ip++;

                            break;
                        }


                        case op_lcmp: {
                            s64 value1 = pop_long(stack);
                            s64 value2 = pop_long(stack);
                            s32 result = value2 == value1 ? 0 : (value2 > value1 ? 1 : -1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("lcmp: %llx cmp %llx = %d\n", value2, value1, result);
#endif
                            push_int(stack, result);

                            ip++;

                            break;
                        }


                        case op_fcmpl: {
                            f32 value1 = pop_float(stack);
                            f32 value2 = pop_float(stack);
                            s32 result = value2 == value1 ? 0 : (value2 > value1 ? 1 : -1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("fcmpl: %f < %f = %d\n", value2, value1, result);
#endif
                            push_int(stack, result);

                            ip++;

                            break;
                        }


                        case op_fcmpg: {
                            f32 value1 = pop_float(stack);
                            f32 value2 = pop_float(stack);
                            s32 result = value2 == value1 ? 0 : (value2 > value1 ? 1 : -1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("fcmpg: %f > %f = %d\n", value2, value1, result);
#endif
                            push_int(stack, result);

                            ip++;

                            break;
                        }


                        case op_dcmpl: {
                            f64 value1 = pop_double(stack);
                            f64 value2 = pop_double(stack);
                            s32 result = value2 == value1 ? 0 : (value2 > value1 ? 1 : -1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dcmpl: %lf < %lf = %d\n", value2, value1, result);
#endif
                            push_int(stack, result);

                            ip++;

                            break;
                        }


                        case op_dcmpg: {
                            f64 value1 = pop_double(stack);
                            f64 value2 = pop_double(stack);
                            s32 result = value2 == value1 ? 0 : (value2 > value1 ? 1 : -1);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("dcmpg: %lf > %lf = %d\n", value2, value1, result);
#endif
                            push_int(stack, result);

                            ip++;

                            break;
                        }


                        case op_ifeq: {
                            s32 val = pop_int(stack);
                            if (val == 0) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ifeq: %d != 0  then jump \n", val);
#endif


                            break;
                        }


                        case op_ifne: {
                            s32 val = pop_int(stack);
                            if (val != 0) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ifne: %d != 0  then jump\n", val);
#endif


                            break;
                        }


                        case op_iflt: {
                            s32 val = pop_int(stack);
                            if (val < 0) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("iflt: %d < 0  then jump  \n", val);
#endif


                            break;
                        }


                        case op_ifge: {
                            s32 val = pop_int(stack);
                            if (val >= 0) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ifge: %d >= 0  then jump \n", val);
#endif


                            break;
                        }


                        case op_ifgt: {
                            s32 val = pop_int(stack);
                            if (val > 0) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ifgt: %d > 0  then jump \n", val);
#endif


                            break;
                        }


                        case op_ifle: {
                            s32 val = pop_int(stack);
                            if (val <= 0) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ifle: %d <= 0  then jump \n", val);
#endif


                            break;
                        }


                        case op_if_icmpeq: {
                            s32 v2 = pop_int(stack);
                            s32 v1 = pop_int(stack);
                            if (v1 == v2) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("if_icmpeq: %lld == %lld \n", (s64) (intptr_t) v1, (s64) (intptr_t) v2);
#endif

                            break;
                        }


                        case op_if_icmpne: {
                            s32 v2 = pop_int(stack);
                            s32 v1 = pop_int(stack);
                            if (v1 != v2) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("if_icmpne: %lld != %lld \n", (s64) (intptr_t) v1, (s64) (intptr_t) v2);
#endif

                            break;
                        }


                        case op_if_icmplt: {
                            s32 v2 = pop_int(stack);
                            s32 v1 = pop_int(stack);
                            if (v1 < v2) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("if_icmplt: %lld < %lld \n", (s64) (intptr_t) v1, (s64) (intptr_t) v2);
#endif

                            break;
                        }


                        case op_if_icmpge: {
                            s32 v2 = pop_int(stack);
                            s32 v1 = pop_int(stack);
                            if (v1 >= v2) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("if_icmpge: %lld >= %lld \n", (s64) (intptr_t) v1, (s64) (intptr_t) v2);
#endif

                            break;
                        }


                        case op_if_icmpgt: {
                            s32 v2 = pop_int(stack);
                            s32 v1 = pop_int(stack);
                            if (v1 > v2) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("if_icmpgt: %lld > %lld \n", (s64) (intptr_t) v1, (s64) (intptr_t) v2);
#endif

                            break;
                        }


                        case op_if_icmple: {
                            s32 v2 = pop_int(stack);
                            s32 v1 = pop_int(stack);
                            if (v1 <= v2) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("if_icmple: %lld <= %lld \n", (s64) (intptr_t) v1, (s64) (intptr_t) v2);
#endif

                            break;
                        }


                        case op_if_acmpeq: {
                            __refer v2 = pop_ref(stack);
                            __refer v1 = pop_ref(stack);
                            if (v1 == v2) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("if_acmpeq: %lld == %lld \n", (s64) (intptr_t) v1, (s64) (intptr_t) v2);
#endif

                            break;
                        }


                        case op_if_acmpne: {
                            __refer v2 = pop_ref(stack);
                            __refer v1 = pop_ref(stack);
                            if (v1 != v2) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("if_acmpne: %lld != %lld \n", (s64) (intptr_t) v1, (s64) (intptr_t) v2);
#endif

                            break;
                        }


                        case op_goto: {

                            s32 branchoffset = *((s16 *) (ip + 1));

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("goto: %d\n", branchoffset);
#endif
                            ip += branchoffset;
                            break;
                        }


                        case op_jsr: {
                            s32 branchoffset = *((s16 *) (ip + 1));
                            push_ra(stack, (__refer) (ip + 3));
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("jsr: %d\n", branchoffset);
#endif
                            ip += branchoffset;
                            break;
                        }


                        case op_ret: {
                            __returnaddress addr = localvar_getRefer(runtime->localvar, (u8) ip[1]);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ret: %x\n", (s64) (intptr_t) addr);
#endif
                            ip = (u8 *) addr;
                            break;
                        }


                        case op_tableswitch: {
                            s32 pos = 0;
                            pos = (s32) (4 - ((((u64) (intptr_t) ip) - (u64) (intptr_t) (ca->code)) % 4));//4 byte对齐


                            s32 default_offset = *((s32 *) (ip + pos));
                            pos += 4;
                            s32 low = *((s32 *) (ip + pos));
                            pos += 4;
                            s32 high = *((s32 *) (ip + pos));
                            pos += 4;

                            int val = pop_int(stack);// pop an int from the stack
                            int offset = 0;
                            if (val < low || val > high) {  // if its less than <low> or greater than <high>,
                                offset = default_offset;              // branch to default
                            } else {                        // otherwise
                                pos += (val - low) * 4;

                                offset = *((s32 *) (ip + pos));     // branch to entry in table
                            }

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("tableswitch: val=%d, offset=%d\n", val, offset);
#endif
                            ip += offset;


                            break;
                        }


                        case op_lookupswitch: {
                            s32 pos = 0;
                            pos = (s32) (4 - ((((u64) (intptr_t) ip) - (u64) (intptr_t) (ca->code)) % 4));//4 byte对齐

                            s32 default_offset = *((s32 *) (ip + pos));
                            pos += 4;
                            s32 n = *((s32 *) (ip + pos));
                            pos += 4;
                            s32 i, key;

                            int val = pop_int(stack);// pop an int from the stack
                            int offset = default_offset;
                            for (i = 0; i < n; i++) {

                                key = *((s32 *) (ip + pos));
                                pos += 4;
                                if (key == val) {
                                    offset = *((s32 *) (ip + pos));
                                    break;
                                } else {
                                    pos += 4;
                                }
                            }

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("tableswitch: val=%d, offset=%d\n", val, offset);
#endif
                            ip += offset;

                            break;
                        }


                        case op_lreturn:
                        case op_dreturn: {
#if _JVM_DEBUG_BYTECODE_DETAIL > 5

                            StackEntry entry;
                            peek_entry(stack->sp - 1, &entry);
                            invoke_deepth(runtime);
                            jvm_printf("ld_return=%lld/[%llx]\n", entry_2_long(&entry), entry_2_long(&entry));
#endif
                            s64 v = pop_long(stack);
                            localvar_dispose(runtime);
                            push_long(stack, v);
                            goto label_exit_while;
                            break;
                        }


                        case op_ireturn:
                        case op_freturn:
                        case op_areturn: {
                            StackEntry entry;
                            pop_entry(stack, &entry);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ifa_return=%d/[%llx]\n", entry_2_int(&entry), entry_2_long(&entry));
#endif
                            localvar_dispose(runtime);
                            push_entry(stack, &entry);
                            goto label_exit_while;
                            break;
                        }


                        case op_return: {
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("return: \n");
#endif
                            localvar_dispose(runtime);
                            goto label_exit_while;
                            break;
                        }


                        case op_getstatic: {

                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            if (!fi) {
                                ConstantFieldRef *cfr = class_get_constant_fieldref(clazz, idx);
                                fi = find_fieldInfo_by_fieldref(clazz, cfr->item.index, runtime);
                                cfr->fieldInfo = fi;
                                if (!fi) {
                                    _nosuchfield_check_exception(utf8_cstr(cfr->name), stack, runtime);
                                    ret = RUNTIME_STATUS_EXCEPTION;
                                    goto label_exception_handle;
                                }
                            }

                            if (fi->isrefer) {
                                *ip = op_getstatic_ref;
                            } else {
                                // check variable type to determine s64/s32/f64/f32
                                s32 data_bytes = fi->datatype_bytes;
                                switch (data_bytes) {
                                    case 4: {
                                        *ip = op_getstatic_int;
                                        break;
                                    }
                                    case 1: {
                                        *ip = op_getstatic_byte;
                                        break;
                                    }
                                    case 8: {
                                        *ip = op_getstatic_long;
                                        break;
                                    }
                                    case 2: {
                                        if (fi->datatype_idx == DATATYPE_JCHAR) {
                                            *ip = op_getstatic_jchar;
                                        } else {
                                            *ip = op_getstatic_short;
                                        }
                                        break;
                                    }
                                    default: {
                                        break;
                                    }
                                }
                            }
                            break;
                        }


                        case op_putstatic: {
                            u16 idx = *((u16 *) (ip + 1));

                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            if (!fi) {
                                ConstantFieldRef *cfr = class_get_constant_fieldref(clazz, idx);
                                fi = find_fieldInfo_by_fieldref(clazz, cfr->item.index, runtime);
                                cfr->fieldInfo = fi;
                                if (!fi) {
                                    _nosuchfield_check_exception(utf8_cstr(cfr->name), stack, runtime);
                                    ret = RUNTIME_STATUS_EXCEPTION;
                                    goto label_exception_handle;
                                }
                            }
                            if (fi->isrefer) {//垃圾回收标识
                                *ip = op_putstatic_ref;
                            } else {
                                // check variable type to determain long/s32/f64/f32
                                s32 data_bytes = fi->datatype_bytes;
                                //非引用类型
                                switch (data_bytes) {
                                    case 4: {
                                        *ip = op_putstatic_int;
                                        break;
                                    }
                                    case 1: {
                                        *ip = op_putstatic_byte;
                                        break;
                                    }
                                    case 8: {
                                        *ip = op_putstatic_long;
                                        break;
                                    }
                                    case 2: {
                                        *ip = op_putstatic_short;
                                        break;
                                    }
                                    default: {
                                        break;
                                    }
                                }
                            }
                            break;
                        }


                        case op_getfield: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            if (!fi) {
                                ConstantFieldRef *cfr = class_get_constant_fieldref(clazz, idx);
                                fi = find_fieldInfo_by_fieldref(clazz, cfr->item.index, runtime);
                                cfr->fieldInfo = fi;
                                if (!fi) {
                                    _nosuchfield_check_exception(utf8_cstr(cfr->name), stack, runtime);
                                    ret = RUNTIME_STATUS_EXCEPTION;
                                    goto label_exception_handle;
                                }
                            }
                            if (fi->isrefer) {
                                *ip = op_getfield_ref;
                            } else {
                                // check variable type to determine s64/s32/f64/f32
                                s32 data_bytes = fi->datatype_bytes;
                                switch (data_bytes) {
                                    case 4: {
                                        *ip = op_getfield_int;
                                        break;
                                    }
                                    case 1: {
                                        *ip = op_getfield_byte;
                                        break;
                                    }
                                    case 8: {
                                        *ip = op_getfield_long;
                                        break;
                                    }
                                    case 2: {
                                        if (fi->datatype_idx == DATATYPE_JCHAR) {
                                            *ip = op_getfield_jchar;
                                        } else {
                                            *ip = op_getfield_short;
                                        }
                                        break;
                                    }
                                    default: {
                                        break;
                                    }
                                }
                            }
                            break;
                        }


                        case op_putfield: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            if (!fi) {
                                ConstantFieldRef *cfr = class_get_constant_fieldref(clazz, idx);
                                fi = find_fieldInfo_by_fieldref(clazz, cfr->item.index, runtime);
                                cfr->fieldInfo = fi;
                                if (!fi) {
                                    _nosuchfield_check_exception(utf8_cstr(cfr->name), stack, runtime);
                                    ret = RUNTIME_STATUS_EXCEPTION;
                                    goto label_exception_handle;
                                }
                            }

                            if (fi->isrefer) {//垃圾回收标识
                                *ip = op_putfield_ref;
                            } else {
                                s32 data_bytes = fi->datatype_bytes;
                                //非引用类型
                                switch (data_bytes) {
                                    case 4: {
                                        *ip = op_putfield_int;
                                        break;
                                    }
                                    case 1: {
                                        *ip = op_putfield_byte;
                                        break;
                                    }
                                    case 8: {
                                        *ip = op_putfield_long;
                                        break;
                                    }
                                    case 2: {
                                        *ip = op_putfield_short;
                                        break;
                                    }
                                    default: {
                                        break;
                                    }
                                }
                            }

                            break;
                        }


                        case op_invokevirtual: {
                            //此cmr所描述的方法，对于不同的实例，有不同的method
                            ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, *((u16 *) (ip + 1)));

                            Instance *ins = getInstanceInStack(cmr, stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                MethodInfo *m = (MethodInfo *) pairlist_get(cmr->virtual_methods, ins->mb.clazz);
                                if (!m) {
                                    m = find_instance_methodInfo_by_name(ins, cmr->name, cmr->descriptor, runtime);
                                    pairlist_put(cmr->virtual_methods, ins->mb.clazz, m);//放入缓存，以便下次直接调用
                                }

                                if (!m) {
                                    _nosuchmethod_check_exception(utf8_cstr(cmr->name), stack, runtime);
                                    ret = RUNTIME_STATUS_EXCEPTION;
                                    goto label_exception_handle;
                                } else {
                                    *ip = op_invokevirtual_fast;
                                }
                            }
                            break;
                        }


                        case op_invokespecial: {

                            ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, *((u16 *) (ip + 1)));
                            MethodInfo *m = cmr->methodInfo;

                            if (!m) {
                                _nosuchmethod_check_exception(utf8_cstr(cmr->name), stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                *ip = op_invokespecial_fast;
                                _optimize_empty_method_call(m, ca, ip);//if method is empty ,bytecode would replaced 'nop' and 'pop' para
                            }
                            break;
                        }


                        case op_invokestatic: {
                            ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, *((u16 *) (ip + 1)));
                            MethodInfo *m = cmr->methodInfo;

                            if (!m) {
                                _nosuchmethod_check_exception(utf8_cstr(cmr->name), stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                *ip = op_invokestatic_fast;
                                _optimize_empty_method_call(m, ca, ip);
                            }
                            break;
                        }


                        case op_invokeinterface: {
                            //s32 paraCount = (u8) ip[3];
                            ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, *((u16 *) (ip + 1)));
                            Instance *ins = getInstanceInStack(cmr, stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                MethodInfo *m = (MethodInfo *) pairlist_get(cmr->virtual_methods, ins->mb.clazz);
                                if (!m) {
                                    m = find_instance_methodInfo_by_name(ins, cmr->name, cmr->descriptor, runtime);
                                    pairlist_put(cmr->virtual_methods, ins->mb.clazz, m);//放入缓存，以便下次直接调用
                                }
                                if (!m) {
                                    _nosuchmethod_check_exception(utf8_cstr(cmr->name), stack, runtime);
                                    ret = RUNTIME_STATUS_EXCEPTION;
                                    goto label_exception_handle;
                                } else {
                                    *ip = op_invokeinterface_fast;
                                }
                            }
                            break;
                        }


                        case op_invokedynamic: {
                            //get bootMethod struct
                            ConstantInvokeDynamic *cid = class_get_invoke_dynamic(clazz, *((u16 *) (ip + 1)));
                            BootstrapMethod *bootMethod = &clazz->bootstrapMethodAttr->bootstrap_methods[cid->bootstrap_method_attr_index];//Boot

                            if (bootMethod->make == NULL) {
                                ret = invokedynamic_prepare(runtime, bootMethod, cid);
                                if (ret) {
                                    goto label_exception_handle;
                                }
                            }
                            MethodInfo *m = bootMethod->make;

                            if (!m) {
                                _nosuchmethod_check_exception("Lambda generated method", stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                *ip = op_invokedynamic_fast;
                            }
                            break;
                        }


                        case op_new: {

                            ConstantClassRef *ccf = class_get_constant_classref(clazz, *((u16 *) (ip + 1)));
                            if (!ccf->clazz) {
                                Utf8String *clsName = class_get_utf8_string(clazz, ccf->stringIndex);
                                ccf->clazz = classes_load_get(clsName, runtime);
                            }
                            JClass *other = ccf->clazz;
                            Instance *ins = NULL;
                            if (other) {
                                ins = instance_create(runtime, other);
                            }
                            push_ref(stack, (__refer) ins);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("new %s [%llx]\n", utf8_cstr(ccf->name), (s64) (intptr_t) ins);
#endif
                            ip += 3;

                            break;
                        }


                        case op_newarray: {
                            s32 typeIdx = ip[1];

                            s32 count = pop_int(stack);

                            Instance *arr = jarray_create_by_type_index(runtime, count, typeIdx);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("(a)newarray  [%llx] type:%c , count:%d  \n", (s64) (intptr_t) arr, getDataTypeTag(typeIdx), count);
#endif
                            if (!arr) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                push_ref(stack, (__refer) arr);
                                ip += 2;
                            }
                            break;
                        }


                        case op_anewarray: {
                            u16 idx = *((u16 *) (ip + 1));

                            s32 count = pop_int(stack);

                            JClass *arr_class = pairlist_get(clazz->arr_class_type, (__refer) (intptr_t) idx);

                            Instance *arr = NULL;
                            if (!arr_class) {//cache to speed
                                arr_class = array_class_get_by_name(runtime, class_get_utf8_string(clazz, idx));
                                pairlist_put(clazz->arr_class_type, (__refer) (intptr_t) idx, arr_class);
                            }
                            arr = jarray_create_by_class(runtime, count, arr_class);


#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("(a)newarray  [%llx] type:%d , count:%d  \n", (s64) (intptr_t) arr, arr_class->arr_class_type, count);
#endif
                            if (!arr) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                push_ref(stack, (__refer) arr);
                                ip += 3;
                            }
                            break;
                        }


                        case op_arraylength: {
                            Instance *arr = (Instance *) pop_ref(stack);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("arraylength  [%llx].arr_body[%llx] len:%d  \n",
                                       (s64) (intptr_t) arr, (s64) (intptr_t) arr->arr_body, arr->arr_length);
#endif
                            if (!arr) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                push_int(stack, arr->arr_length);
                                ip++;
                            }
                            break;
                        }


                        case op_athrow: {

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            Instance *ins = (Instance *) pop_ref(stack);
                            push_ref(stack, (__refer) ins);
                            invoke_deepth(runtime);
                            jvm_printf("athrow  [%llx].exception throws  \n", (s64) (intptr_t) ins);
#endif
                            ret = RUNTIME_STATUS_EXCEPTION;
                            goto label_exception_handle;
                            break;
                        }


                        case op_checkcast: {
                            s32 typeIdx = *((u16 *) (ip + 1));

                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!checkcast(runtime, ins, typeIdx)) {
                                _checkcast_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                push_ref(stack, (__refer) ins);
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("checkcast  [%llx] instancof %s is:%d \n", (s64) (intptr_t) ins,
                                       utf8_cstr(class_get_constant_classref(clazz, typeIdx)->name),
                                       checkok);
#endif

                            break;
                        }


                        case op_instanceof: {
                            Instance *ins = (Instance *) pop_ref(stack);
                            s32 typeIdx = *((u16 *) (ip + 1));

                            s32 checkok = 0;
                            if (!ins) {
                            } else if (ins->mb.type & (MEM_TYPE_INS | MEM_TYPE_ARR)) {
                                if (instance_of(getClassByConstantClassRef(clazz, typeIdx, runtime), ins, runtime)) {
                                    checkok = 1;
                                }
                            }
                            push_int(stack, checkok);

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("instanceof  [%llx] instancof %s  \n", (s64) (intptr_t) ins, utf8_cstr(class_get_constant_classref(clazz, typeIdx)->name));
#endif
                            ip += 3;
                            break;
                        }


                        case op_monitorenter: {
                            Instance *ins = (Instance *) pop_ref(stack);
                            jthread_lock(&ins->mb, runtime);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("monitorenter  [%llx] %s  \n", (s64) (intptr_t) ins, ins ? utf8_cstr(ins->mb.clazz->name) : "null");
#endif
                            ip++;
                            break;
                        }


                        case op_monitorexit: {
                            Instance *ins = (Instance *) pop_ref(stack);
                            jthread_unlock(&ins->mb, runtime);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("monitorexit  [%llx] %s  \n", (s64) (intptr_t) ins, ins ? utf8_cstr(ins->mb.clazz->name) : "null");
#endif
                            ip++;
                            break;
                        }


                        case op_wide: {
#if _JVM_DEBUG_BYTECODE_DETAIL > 5

                            invoke_deepth(runtime);
                            jvm_printf("wide  \n");
#endif
                            ip++;
                            cur_inst = *ip;
                            switch (cur_inst) {
                                case op_iload:
                                case op_fload: {
                                    _op_load_1_slot(stack, runtime, *((u16 *) (ip + 1)));
                                    ip += 3;
                                    break;
                                }
                                case op_aload: {
                                    _op_load_refer(stack, runtime, *((u16 *) (ip + 1)));
                                    ip += 3;
                                    break;
                                }
                                case op_lload:
                                case op_dload: {
                                    _op_load_2_slot(stack, runtime, *((u16 *) (ip + 1)));
                                    ip += 3;
                                    break;
                                }
                                case op_istore:
                                case op_fstore: {
                                    _op_store_1_slot(stack, runtime, *((u16 *) (ip + 1)));
                                    ip += 3;
                                    break;
                                }
                                case op_astore: {
                                    _op_store_refer(stack, runtime, *((u16 *) (ip + 1)));
                                    ip += 3;
                                    break;
                                }
                                case op_lstore:
                                case op_dstore: {
                                    _op_store_2_slot(stack, runtime, *((u16 *) (ip + 1)));
                                    ip += 3;
                                    break;
                                }
                                case op_ret: {
                                    __refer addr = localvar_getRefer(runtime->localvar, *((u16 *) (ip + 1)));

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                    invoke_deepth(runtime);
                                    jvm_printf("wide ret: %x\n", (s64) (intptr_t) addr);
#endif
                                    ip = (u8 *) addr;
                                    break;
                                }
                                case op_iinc    : {

                                    runtime->localvar[*((u16 *) (ip + 1))].ivalue += *((s16 *) (ip + 3));
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                    invoke_deepth(runtime);
                                    jvm_printf("wide iinc: localvar(%d) = %d , inc %d\n", *((u16 *) (ip + 1)), runtime->localvar[*((u16 *) (ip + 1))].ivalue, *((u16 *) (ip + 3)));
#endif
                                    ip += 5;
                                    break;
                                }
                                default:
                                    _op_notsupport(ip, runtime);
                            }
                            break;
                        }


                        case op_multianewarray: {
                            //data type index

                            Utf8String *desc = class_get_utf8_string(clazz, *((u16 *) (ip + 1)));
                            //array dim
                            s32 count = (u8) ip[3];
#ifdef __JVM_OS_VS__
                            s32 dim[32];
#else
                            s32 dim[count];
#endif
                            int i;
                            for (i = 0; i < count; i++)
                                dim[i] = pop_int(stack);

                            Instance *arr = jarray_multi_create(runtime, dim, count, desc, 0);
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("multianewarray  [%llx] type:%s , count:%d  \n", (s64) (intptr_t) arr,
                                       utf8_cstr(desc), count);
#endif
                            if (!arr) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                push_ref(stack, (__refer) arr);
                                ip += 4;
                            }
                            break;
                        }


                        case op_ifnull: {
                            __refer ref = pop_ref(stack);
                            if (!ref) {

                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ifnonnull: %d/%llx != 0  then jump %d \n", (s32) (intptr_t) ref,
                                       (s64) (intptr_t) ref);
#endif


                            break;
                        }


                        case op_ifnonnull: {
                            __refer ref = pop_ref(stack);
                            if (ref) {
                                ip += *((s16 *) (ip + 1));
                            } else {
                                ip += 3;
                            }
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("ifnonnull: %d/%llx != 0  then \n", (s32) (intptr_t) ref, (s64) (intptr_t) ref);
#endif
                            break;
                        }


                        case op_goto_w: {
                            s32 branchoffset = *((s32 *) (ip + 1));

#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("goto: %d\n", branchoffset);
#endif
                            ip += branchoffset;
                            break;
                        }


                        case op_jsr_w: {

                            s32 branchoffset = *((s32 *) (ip + 1));
                            push_ra(stack, (__refer) (ip + 3));
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("jsr_w: %d\n", branchoffset);
#endif
                            ip += branchoffset;
                            break;
                        }


                        case op_breakpoint: {
#if _JVM_DEBUG_BYTECODE_DETAIL > 5

                            invoke_deepth(runtime);
                            jvm_printf("breakpoint \n");
#endif
                            break;
                        }


                        case op_getstatic_ref: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            if (fi->isvolatile) {
                                barrier();
                            }
                            push_ref(stack, getFieldRefer(ptr));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: ref  %d = %s.%s \n", "getstatic", (s64) (intptr_t) getFieldRefer(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name), getFieldLong(ptr));
#endif
                            break;
                        }

                        case op_getstatic_long: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            if (fi->isvolatile) {
                                barrier();
                            }
                            push_long(stack, getFieldLong(ptr));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: long  %d = %s.%s \n", "getstatic", getFieldLong(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name), getFieldLong(ptr));
#endif
                            break;
                        }

                        case op_getstatic_int: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            if (fi->isvolatile) {
                                barrier();
                            }
                            push_int(stack, getFieldInt(ptr));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: int  %d = %s.%s \n", "getstatic", (s32) getFieldInt(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name), getFieldLong(ptr));
#endif
                            break;
                        }

                        case op_getstatic_short: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            if (fi->isvolatile) {
                                barrier();
                            }
                            push_int(stack, getFieldShort(ptr));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: short  %d = %s.%s \n", "getstatic", (s32) getFieldShort(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name), getFieldLong(ptr));
#endif
                            break;
                        }

                        case op_getstatic_jchar: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            if (fi->isvolatile) {
                                barrier();
                            }
                            push_int(stack, getFieldChar(ptr));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: char  %d = %s.%s \n", "getstatic", (s32) (u16) getFieldChar(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name), getFieldLong(ptr));
#endif
                            break;
                        }

                        case op_getstatic_byte: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            if (fi->isvolatile) {
                                barrier();
                            }
                            push_int(stack, getFieldByte(ptr));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: byte  %d = %s.%s \n", "getstatic", (s32) getFieldByte(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name), getFieldLong(ptr));
#endif
                            break;
                        }


                        case op_putstatic_ref: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            setFieldRefer(ptr, pop_ref(stack));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: ref  %s.%s = %llx \n", "putstatic", utf8_cstr(clazz->name), utf8_cstr(fi->name), (s64) (intptr_t) getFieldRefer(ptr));
#endif
                            break;
                        }


                        case op_putstatic_long: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            setFieldLong(ptr, pop_long(stack));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: long  %s.%s = %lld \n", "putstatic", utf8_cstr(clazz->name), utf8_cstr(fi->name), getFieldLong(ptr));
#endif
                            break;
                        }

                        case op_putstatic_int: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            setFieldInt(ptr, pop_int(stack));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: int  %s.%s = %d \n", "putstatic", utf8_cstr(clazz->name), utf8_cstr(fi->name), (s32) getFieldInt(ptr));
#endif
                            break;
                        }

                        case op_putstatic_short: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            setFieldShort(ptr, (s16) pop_int(stack));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: short  %s.%s = %d \n", "putstatic", utf8_cstr(clazz->name), utf8_cstr(fi->name), (s32) getFieldShort(ptr));
#endif
                            break;
                        }

                        case op_putstatic_byte: {
                            u16 idx = *((u16 *) (ip + 1));
                            FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                            c8 *ptr = getStaticFieldPtr(fi);
                            setFieldByte(ptr, (s8) pop_int(stack));
                            ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                            invoke_deepth(runtime);
                            jvm_printf("%s: byte  %s.%s = %d \n", "putstatic", utf8_cstr(clazz->name), utf8_cstr(fi->name), (s32) getFieldByte(ptr));
#endif
                            break;
                        }


                        case op_getfield_ref: {
                            u16 idx = *((u16 *) (ip + 1));
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);

                                if (fi->isvolatile) {
                                    barrier();
                                }
                                push_ref(stack, getFieldRefer(ptr));
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: ref %llx = %s.%s \n", "getfield", getFieldRefer(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name));
#endif
                            }
                            break;
                        }


                        case op_getfield_long: {
                            u16 idx = *((u16 *) (ip + 1));
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);

                                if (fi->isvolatile) {
                                    barrier();
                                }
                                push_long(stack, getFieldLong(ptr));
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: long %lld = %s.%s \n", "getfield", getFieldLong(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name));
#endif
                            }
                            break;
                        }


                        case op_getfield_int: {
                            u16 idx = *((u16 *) (ip + 1));
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);

                                if (fi->isvolatile) {
                                    barrier();
                                }
                                push_int(stack, getFieldInt(ptr));
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: int %d = %s.%s \n", "getfield", (s32) getFieldInt(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name));
#endif
                            }
                            break;
                        }


                        case op_getfield_short: {
                            u16 idx = *((u16 *) (ip + 1));
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);

                                if (fi->isvolatile) {
                                    barrier();
                                }
                                push_int(stack, getFieldShort(ptr));
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: short %d = %s.%s \n", "getfield", (s32) getFieldShort(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name));
#endif
                            }
                            break;
                        }


                        case op_getfield_jchar: {
                            u16 idx = *((u16 *) (ip + 1));
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);

                                if (fi->isvolatile) {
                                    barrier();
                                }
                                push_int(stack, getFieldChar(ptr));
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: char %d = %s.%s \n", "getfield", (s32) (u16) getFieldChar(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name));
#endif
                            }
                            break;
                        }


                        case op_getfield_byte: {
                            u16 idx = *((u16 *) (ip + 1));
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);

                                if (fi->isvolatile) {
                                    barrier();
                                }
                                push_int(stack, getFieldByte(ptr));
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: byte %d = %s.%s \n", "getfield", (s32) getFieldByte(ptr), utf8_cstr(clazz->name), utf8_cstr(fi->name));
#endif
                            }
                            break;
                        }


                        case op_putfield_ref: {
                            u16 idx = *((u16 *) (ip + 1));
                            __refer ref = pop_ref(stack);
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                // check variable type to determain long/s32/f64/f32
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);
                                setFieldRefer(ptr, ref);
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: ref %s.%s = %llx\n", "putfield", utf8_cstr(clazz->name), utf8_cstr(fi->name), (s64) (intptr_t) ref);
#endif
                            }
                            break;
                        }


                        case op_putfield_long: {
                            u16 idx = *((u16 *) (ip + 1));
                            s64 v = pop_long(stack);
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);
                                setFieldLong(ptr, v);
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: long %s.%s = %lld\n", "putfield", utf8_cstr(clazz->name), utf8_cstr(fi->name), v);
#endif
                            }
                            break;
                        }


                        case op_putfield_int: {
                            u16 idx = *((u16 *) (ip + 1));
                            s32 v = pop_int(stack);
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);
                                setFieldInt(ptr, v);
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: int %s.%s = %d\n", "putfield", utf8_cstr(clazz->name), utf8_cstr(fi->name), v);
#endif
                            }
                            break;
                        }


                        case op_putfield_short: {
                            u16 idx = *((u16 *) (ip + 1));
                            s32 v = pop_int(stack);
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);
                                setFieldShort(ptr, (s16) v);
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: short %s.%s = %d\n", "putfield", utf8_cstr(clazz->name), utf8_cstr(fi->name), v);
#endif
                            }
                            break;
                        }


                        case op_putfield_byte: {
                            u16 idx = *((u16 *) (ip + 1));
                            s32 v = pop_int(stack);
                            Instance *ins = (Instance *) pop_ref(stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                FieldInfo *fi = class_get_constant_fieldref(clazz, idx)->fieldInfo;
                                c8 *ptr = getInstanceFieldPtr(ins, fi);
                                setFieldByte(ptr, (s8) v);
                                ip += 3;
#if _JVM_DEBUG_BYTECODE_DETAIL > 5
                                invoke_deepth(runtime);
                                jvm_printf("%s: byte %s.%s = %d\n", "putfield", utf8_cstr(clazz->name), utf8_cstr(fi->name), v);
#endif
                            }
                            break;
                        }


                        case op_invokevirtual_fast: {
                            ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, *((u16 *) (ip + 1)));
                            Instance *ins = getInstanceInStack(cmr, stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                MethodInfo *m = (MethodInfo *) pairlist_get(cmr->virtual_methods, ins->mb.clazz);
#if _JVM_DEBUG_PROFILE
                                spent = nanoTime() - start_at;
#endif
                                if (!m) {
                                    *ip = op_invokevirtual;
                                } else {
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                                    invoke_deepth(runtime);
                                    jvm_printf("invokevirtual    %s.%s%s  {\n", utf8_cstr(m->_this_class->name), utf8_cstr(m->name), utf8_cstr(m->descriptor));
#endif
                                    ret = execute_method_impl(m, runtime);
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                                    invoke_deepth(runtime);
                                    jvm_printf("}\n");
#endif
                                    if (ret) {
                                        goto label_exception_handle;
                                    }
                                    ip += 3;
                                }
                            }
                            break;
                        }


                        case op_invokespecial_fast: {
                            ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, *((u16 *) (ip + 1)));
                            MethodInfo *m = cmr->methodInfo;
#if _JVM_DEBUG_PROFILE
                            spent = nanoTime() - start_at;
#endif
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                            invoke_deepth(runtime);
                            jvm_printf("invokespecial    %s.%s%s {\n", utf8_cstr(m->_this_class->name), utf8_cstr(m->name), utf8_cstr(m->descriptor));
#endif
                            ret = execute_method_impl(m, runtime);
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                            invoke_deepth(runtime);
                            jvm_printf("}\n");
#endif
                            if (ret) {
                                goto label_exception_handle;
                            }
                            ip += 3;
                            break;
                        }


                        case op_invokestatic_fast: {
                            ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, *((u16 *) (ip + 1)));
                            MethodInfo *m = cmr->methodInfo;
#if _JVM_DEBUG_PROFILE
                            spent = nanoTime() - start_at;
#endif
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                            invoke_deepth(runtime);
                            jvm_printf("invokestatic   | %s.%s%s {\n", utf8_cstr(m->_this_class->name), utf8_cstr(m->name), utf8_cstr(m->descriptor));
#endif
                            ret = execute_method_impl(m, runtime);
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                            invoke_deepth(runtime);
                            jvm_printf("}\n");
#endif
                            if (ret) {
                                goto label_exception_handle;
                            }
                            ip += 3;
                            break;
                        }


                        case op_invokeinterface_fast: {
                            //此cmr所描述的方法，对于不同的实例，有不同的method
                            ConstantMethodRef *cmr = class_get_constant_method_ref(clazz, *((u16 *) (ip + 1)));

                            Instance *ins = getInstanceInStack(cmr, stack);
                            if (!ins) {
                                _null_throw_exception(stack, runtime);
                                ret = RUNTIME_STATUS_EXCEPTION;
                                goto label_exception_handle;
                            } else {
                                MethodInfo *m = (MethodInfo *) pairlist_get(cmr->virtual_methods, ins->mb.clazz);
#if _JVM_DEBUG_PROFILE
                                spent = nanoTime() - start_at;
#endif
                                if (!m) {
                                    *ip = op_invokeinterface;
                                } else {
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                                    invoke_deepth(runtime);
                                    jvm_printf("invokeinterface   | %s.%s%s {\n", utf8_cstr(m->_this_class->name),
                                               utf8_cstr(m->name), utf8_cstr(m->descriptor));
#endif
                                    ret = execute_method_impl(m, runtime);
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                                    invoke_deepth(runtime);
                                    jvm_printf("}\n");
#endif
                                    if (ret) {
                                        goto label_exception_handle;
                                    }
                                    ip += 5;
                                }
                            }
                            break;
                        }


                        case op_invokedynamic_fast: {
                            //get bootMethod struct
                            ConstantInvokeDynamic *cid = class_get_invoke_dynamic(clazz, *((u16 *) (ip + 1)));
                            BootstrapMethod *bootMethod = &clazz->bootstrapMethodAttr->bootstrap_methods[cid->bootstrap_method_attr_index];//Boot
                            MethodInfo *m = bootMethod->make;

#if _JVM_DEBUG_PROFILE
                            spent = nanoTime() - start_at;
#endif
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                            invoke_deepth(runtime);
                            jvm_printf("invokedynamic   | %s.%s%s {\n", utf8_cstr(m->_this_class->name),
                                       utf8_cstr(m->name), utf8_cstr(m->descriptor));
#endif
                            // run make to generate instance of Lambda Class
                            ret = execute_method_impl(m, runtime);
#if _JVM_DEBUG_BYTECODE_DETAIL > 3
                            invoke_deepth(runtime);
                            jvm_printf("}\n");
#endif
                            if (ret) {
                                goto label_exception_handle;
                            }

                            ip += 5;
                            break;
                        }

                        default:
                            _op_notsupport(ip, runtime);
                    }

                    /* ================================== runtime->pc end =============================*/

#if _JVM_DEBUG_PROFILE
                    //time
                    if (!spent) spent = nanoTime() - start_at;
                    profile_put(cur_inst, spent, 1);
#endif
                    continue;

                    label_exception_handle:
                    // there is exception handle, but not error/interrupt handle
                    if (ret == RUNTIME_STATUS_EXCEPTION
                        && exception_handle(runtime->stack, runtime)) {
                        ret = RUNTIME_STATUS_NORMAL;
                        ip = runtime->pc;
                    } else {
                        break;
                    }
#if _JVM_DEBUG_PROFILE
                    //time
                    if (!spent) spent = nanoTime() - start_at;
                    profile_put(cur_inst, spent, 1);
#endif
                    continue;

                    label_exit_while:
#if _JVM_DEBUG_PROFILE
                    //time
                    if (!spent) spent = nanoTime() - start_at;
                    profile_put(cur_inst, spent, 1);
#endif
                    break;

                } while (1);//end while
            }
            if (method->is_sync)_synchronized_unlock_method(method, runtime);

        } else {
            jvm_printf("method code attribute is null.");
        }
    } else {//本地方法
        localvar_init(runtime, method->para_slots, method->para_slots);
        //缓存调用本地方法
        if (!method->native_func) { //把本地方法找出来缓存
            java_native_method *native = find_native_method(utf8_cstr(clazz->name), utf8_cstr(method->name),
                                                            utf8_cstr(method->descriptor));
            if (!native) {
                _nosuchmethod_check_exception(utf8_cstr(method->name), stack, runtime);
                ret = RUNTIME_STATUS_EXCEPTION;
            } else {
                method->native_func = native->func_pointer;
            }
        }

        if (method->native_func) {
            if (method->is_sync)_synchronized_lock_method(method, runtime);
            ret = method->native_func(runtime, clazz);
            if (method->is_sync)_synchronized_unlock_method(method, runtime);
            switch (method->return_slots) {
                case 0: {// V
                    localvar_dispose(runtime);
                    break;
                }
                case 1: { // F I R
                    StackEntry entry;
                    peek_entry(stack->sp - method->return_slots, &entry);
                    localvar_dispose(runtime);
                    push_entry(stack, &entry);
                    break;
                }
                case 2: {//J D return type , 2slots
                    s64 v = pop_long(stack);
                    localvar_dispose(runtime);
                    push_long(stack, v);
                    break;
                }
                default: {
                    break;
                }
            }
        }
    }


#if _JVM_DEBUG_BYTECODE_DETAIL > 3
    invoke_deepth(runtime);
    jvm_printf("stack size  %s.%s%s in:%d out:%d  \n", utf8_cstr(clazz->name), utf8_cstr(method->name), utf8_cstr(method->descriptor), (runtime->stack->sp - runtime->localvar), stack_size(stack));
    if (ret != RUNTIME_STATUS_EXCEPTION) {
        if (method->return_slots) {//无反回值
            if (stack->sp != runtime->localvar + method->return_slots) {
                exit(1);
            }
        }
    }
#endif
    runtime_destory_inl(runtime);
    pruntime->son = NULL;  //need for getLastSon()
    return ret;
}

