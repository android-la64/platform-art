/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "asm_support_loongarch64.S"

// Wrap ExecuteSwitchImpl in assembly method which specifies DEX PC for unwinding.
//  Argument 0: a0: The context pointer for ExecuteSwitchImpl.
//  Argument 1: a1: Pointer to the templated ExecuteSwitchImpl to call.
//  Argument 2: a2: The value of DEX PC (memory address of the methods bytecode).
ENTRY ExecuteSwitchImplAsm
    INCREASE_FRAME 16
    SAVE_GPR $s1, 0
    SAVE_GPR $ra, 8

    move $s1, $a2     // s1 = DEX PC
    CFI_DEFINE_DEX_PC_WITH_OFFSET(4 /* a0 */, 24 /* s1, a.k.a. x9 */, 0)
    jirl $ra, $a1, 0  // Call the wrapped method.

    RESTORE_GPR $s1, 0
    RESTORE_GPR $ra, 8
    DECREASE_FRAME 16
    jirl $zero, $ra, 0
END ExecuteSwitchImplAsm


.macro INVOKE_STUB_CREATE_FRAME
    // Save RA, FP, xSELF (current thread), A4, A5 (they will be needed in the invoke stub return).
    INCREASE_FRAME 48
    // Slot (8*0) is used for `ArtMethod*` (if no args), args or padding, see below.
    SAVE_GPR $xSELF, (8*1)
    SAVE_GPR $a4,    (8*2)
    SAVE_GPR $a5,    (8*3)
    SAVE_GPR $fp,    (8*4)  // Store FP just under the return address.
    SAVE_GPR $ra,    (8*5)

    // Make the new FP point to the location where we stored the old FP.
    // Some stack-walking tools may rely on this simply-linked list of saved FPs.
    //addi.d $fp, $sp, (8*4)  // save frame pointer
    //.cfi_def_cfa $fp, 48 - (8*4)
    move $fp, $sp
    .cfi_def_cfa_register $fp

    // We already have space for `ArtMethod*` on the stack but we need space for args above
    // the `ArtMethod*`, so add sufficient space now, pushing the `ArtMethod*` slot down.
    addi.d $t0, $a2, (__SIZEOF_POINTER__ + 0xf)    // Reserve space for arguments and
    li.d $t8, ~0xf          // round up for 16-byte stack alignment.
    and $t0, $t0, $t8
    sub.d  $sp, $sp, $t0

    move $xSELF, $a3

    // Copy arguments on stack (4 bytes per slot):
    //   A1: source address
    //   A2: arguments length
    //   T0: destination address if there are any args.
    beqz $a2, 2f        // loop through 4-byte arguments from the last to the first
    addi.d $t0, $sp, 8  // destination address is bottom of the stack + 8 bytes for ArtMethod* (null)
1:
    addi.d $a2, $a2, -4
    add.d  $t1, $a1, $a2  // T1 is the source address of the next copied argument
    ld.w   $t2, $t1, 0    // T2 is the 4 bytes at address T1
    add.d  $t1, $t0, $a2  // T1 is the destination address of the next copied argument
    st.w   $t2, $t1, 0  // save T2 at the destination address T1
    bnez $a2, 1b
2:
    st.d $zero, $sp, 0  // Store null into ArtMethod* at bottom of frame.
.endm


.macro INVOKE_STUB_CALL_AND_RETURN
    // Call the method.
    ld.d   $t0, $a0, ART_METHOD_QUICK_CODE_OFFSET_64
    jirl $ra, $t0, 0

    //addi.d $sp, $fp, -(8*4)  // restore SP (see `INVOKE_STUB_CREATE_FRAME`)
    //.cfi_def_cfa $sp, 48
    move $sp, $fp
    .cfi_def_cfa_register $sp

    // Restore ra, fp, xSELF (current thread) a4 (shorty), a5 (result pointer) from stack.
    RESTORE_GPR $xSELF, (8*1)
    RESTORE_GPR $a4,    (8*2)
    RESTORE_GPR $a5,    (8*3)
    RESTORE_GPR $fp,    (8*4)
    RESTORE_GPR $ra,    (8*5)
    DECREASE_FRAME 48

    // Load result type (1-byte symbol) from a5.
    // Check result type and store the correct register into the jvalue in memory at a4 address.
    ld.bu $t0, $a5, 0

    li.d $t1, 'V'  // void (do not store result at all)
    beq $t1, $t0, 1f

    li.d $t1, 'D'  // double
    beq $t1, $t0, 2f

    li.d $t1, 'F'  // float
    beq $t1, $t0, 3f

    // Otherwise, result is in a0 (either 8 or 4 bytes, but it is fine to store 8 bytes as the
    // upper bytes in a0 in that case are zero, and jvalue has enough space).
    st.d $a0, $a4, 0
1:
    jirl $zero, $ra, 0

2:  // double: result in fa0 (8 bytes)
    fst.d $fa0, $a4, 0
    jirl $zero, $ra, 0

3:  // float: result in fa0 (4 bytes)
    fst.s $fa0, $a4, 0
    jirl $zero, $ra, 0
.endm


ENTRY art_deliver_pending_exception
    DELIVER_PENDING_EXCEPTION
END art_deliver_pending_exception


// The size of the handler emitted by `INVOKE_STUB_LOAD_REG` below.
#define INVOKE_STUB_LOAD_REG_SIZE 12

// The offset within `INVOKE_STUB_LOAD_REG` for skipping arguments.
#define INVOKE_STUB_LOAD_REG_SKIP_OFFSET 8

// Macro for loading an argument into a register.
//  load - instruction used for loading,
//  reg - the register to load,
//  args - pointer to next argument,
//  size - the size of the register - 4 or 8 bytes, used as an offset for the load,
//  handler_reg - the register with the address of the handler (points to this handler on entry),
//  handler_diff - the difference in bytes from the current to the next handler,
//  cont - the base name of the label for continuing the shorty processing loop,
//  sfx - suffix added to all labels to make labels unique for different users.
.macro INVOKE_STUB_LOAD_REG load, reg, args, size, handler_reg, handler_diff, cont, sfx
.Linvoke_stub_\load\reg\sfx:
    \load  \reg, \args, -\size
    addi.d \handler_reg, \handler_reg, \handler_diff
.org .Linvoke_stub_\load\reg\sfx + INVOKE_STUB_LOAD_REG_SKIP_OFFSET  // Enforce skip offset.
    b \cont\sfx
.org .Linvoke_stub_\load\reg\sfx + INVOKE_STUB_LOAD_REG_SIZE  // Enforce handler size.
.endm


// Fill registers a1 to a7 and fa0 to fa7 with parameters.
// Parse the passed shorty to determine which register to load.
//  a5 - shorty,
//  t0 - points to arguments on the stack if any (undefined for static method without args),
//  sfx - suffix added to all labels to make labels unique for different users.
.macro INVOKE_STUB_LOAD_ALL_ARGS sfx
    addi.d $t1, $a5, 1                     // Load shorty address, plus one to skip the return type.

    // Load this (if instance method) and record the number of GPRs to fill.
    .ifc \sfx, _instance
        ld.w   $a1, $t0, 0                  // Load "this" parameter,
        addi.d $t0, $t0, 4                 // and increment arg pointer.
        .equ NUM_GPRS_TO_FILL, 6
    .else
        .equ NUM_GPRS_TO_FILL, 7
    .endif
    .equ NUM_FPRS_TO_FILL, 8

    // Load addresses for routines that load argument GPRs and FPRs.
    //lla  t4, .Lreg_handlers_start\sfx  // First handler for non-FP args. XC-TODO: verify
    la $t4, .Lreg_handlers_start\sfx
    addi.d $t5, $t4, (3 * NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)  // First handler for FP args.

    // Loop to fill registers.
.Lfill_regs\sfx:
    ld.b   $t2, $t1, 0                  // Load next character in signature,
    addi.d $t1, $t1, 1                  // and increment.
    beqz $t2, .Lcall_method\sfx         // Exit at end of signature. Shorty 0 terminated.

    li.d   $t3, 'L'
    beq  $t2, $t3, .Lload_reference\sfx  // Is this a reference?

    li.d   $t3, 'J'
    beq  $t2, $t3, .Lload_long\sfx       // Is this a long?

    li.d   $t3, 'F'
    beq  $t2, $t3, .Lload_float\sfx      // Is this a float?

    li.d   $t3, 'D'
    beq  $t2, $t3, .Lload_double\sfx     // Is this a double?

    // Everything else uses a 4-byte value sign-extened to a 64 bit GPR.
    addi.d $t0, $t0, 4
    jirl $zero, $t4, 0

.Lload_reference\sfx:
    addi.d $t0, $t0, 4
    //jirl $zero, $t4, (NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    li.d $t8, (NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    add.d $t8, $t4, $t8
    jirl $zero, $t8, 0

.Lload_long\sfx:
    addi.d $t0, $t0, 8
    //jirl $zero, $t4, (2 * NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    li.d $t8, (2 * NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    add.d $t8, $t4, $t8
    jirl $zero, $t8, 0

.Lload_float\sfx:
    addi.d $t0, $t0, 4
    jirl $zero, $t5, 0

.Lload_double\sfx:
    addi.d $t0, $t0, 8
    //jirl $zero, $t5, (NUM_FPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    li.d $t8, (NUM_FPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    add.d $t8, $t5, $t8
    jirl $zero, $t8, 0

.Lreg_handlers_start\sfx:

// Handlers for loading other args (not reference/long/float/double) into GPRs.
    .ifnc \sfx, _instance
        INVOKE_STUB_LOAD_REG ld.w, $a1, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    .endif
    INVOKE_STUB_LOAD_REG ld.w, $a2, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.w, $a3, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.w, $a4, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.w, $a5, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.w, $a6, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.w, $a7, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

// Handlers for loading reference args into GPRs.
    .ifnc \sfx, _instance
        INVOKE_STUB_LOAD_REG ld.wu, $a1, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    .endif
    INVOKE_STUB_LOAD_REG ld.wu, $a2, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.wu, $a3, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.wu, $a4, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.wu, $a5, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.wu, $a6, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.wu, $a7, $t0, 4, $t4, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

// Handlers for loading long args into GPRs.
    .ifnc \sfx, _instance
        INVOKE_STUB_LOAD_REG ld.d, $a1, $t0, 8, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    .endif
    INVOKE_STUB_LOAD_REG ld.d, $a2, $t0, 8, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.d, $a3, $t0, 8, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.d, $a4, $t0, 8, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.d, $a5, $t0, 8, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.d, $a6, $t0, 8, $t4, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG ld.d, $a7, $t0, 8, $t4, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

// Handlers for loading floats into FPRs.
    INVOKE_STUB_LOAD_REG fld.s, $fa0, $t0, 4, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.s, $fa1, $t0, 4, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.s, $fa2, $t0, 4, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.s, $fa3, $t0, 4, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.s, $fa4, $t0, 4, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.s, $fa5, $t0, 4, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.s, $fa6, $t0, 4, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.s, $fa7, $t0, 4, $t5, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

// Handlers for loading doubles into FPRs.
    INVOKE_STUB_LOAD_REG fld.d, $fa0, $t0, 8, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.d, $fa1, $t0, 8, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.d, $fa2, $t0, 8, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.d, $fa3, $t0, 8, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.d, $fa4, $t0, 8, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.d, $fa5, $t0, 8, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.d, $fa6, $t0, 8, $t5, INVOKE_STUB_LOAD_REG_SIZE, .Lfill_regs, \sfx
    INVOKE_STUB_LOAD_REG fld.d, $fa7, $t0, 8, $t5, INVOKE_STUB_LOAD_REG_SKIP_OFFSET, .Lfill_regs, \sfx

.Lcall_method\sfx:
.endm


// void art_quick_invoke_stub(ArtMethod* method,   // a0
//                            uint32_t*  args,     // a1
//                            uint32_t   argsize,  // a2
//                            Thread*    self,     // a3
//                            JValue*    result,   // a4
//                            char*      shorty)   // a5
ENTRY art_quick_invoke_stub
    INVOKE_STUB_CREATE_FRAME

    // Load args into registers.
    INVOKE_STUB_LOAD_ALL_ARGS _instance

    // Call the method and return.
    INVOKE_STUB_CALL_AND_RETURN
END art_quick_invoke_stub


// void art_quick_invoke_static_stub(ArtMethod* method,   // a0
//                                   uint32_t*  args,     // a1
//                                   uint32_t   argsize,  // a2
//                                   Thread*    self,     // a3
//                                   JValue*    result,   // a4
//                                   char*      shorty)   // a5
ENTRY art_quick_invoke_static_stub
    INVOKE_STUB_CREATE_FRAME

    // Load args into registers.
    INVOKE_STUB_LOAD_ALL_ARGS _static

    // Call the method and return.
    INVOKE_STUB_CALL_AND_RETURN
END art_quick_invoke_static_stub


ENTRY art_quick_generic_jni_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0

    // Save sp, so we can have static CFI info.
    move   $fp, $sp
    .cfi_def_cfa_register $fp

    //li.d   $t0, GENERIC_JNI_TRAMPOLINE_RESERVED_AREA  //XC-TODO why not compile faield?
    li.d   $t0, 5120
    sub.d  $sp, $sp, $t0

    move   $a0, $xSELF    // Thread*
    move   $a1, $fp       // SP for the managed frame.
    move   $a2, $sp       // reserved area for arguments and other saved data (up to managed frame)
    bl artQuickGenericJniTrampoline

    // Check for error (class init check or locking for synchronized native method can throw).
    beqz $a0, .Lexception_in_native

    move   $t2, $a0      // save pointer to native method code into temporary

    // Load argument GPRs from stack (saved there by artQuickGenericJniTrampoline).
    ld.d  $a0, $sp, 8*0   // JniEnv* for the native method
    ld.d  $a1, $sp, 8*1
    ld.d  $a2, $sp, 8*2
    ld.d  $a3, $sp, 8*3
    ld.d  $a4, $sp, 8*4
    ld.d  $a5, $sp, 8*5
    ld.d  $a6, $sp, 8*6
    ld.d  $a7, $sp, 8*7

    // Load argument FPRs from stack (saved there by artQuickGenericJniTrampoline).
    fld.d  $fa0, $sp, 8*8
    fld.d  $fa1, $sp, 8*9
    fld.d  $fa2, $sp, 8*10
    fld.d  $fa3, $sp, 8*11
    fld.d  $fa4, $sp, 8*12
    fld.d  $fa5, $sp, 8*13
    fld.d  $fa6, $sp, 8*14
    fld.d  $fa7, $sp, 8*15

    ld.d  $t0, $sp, 8*16  // @CriticalNative arg, used by art_jni_dlsym_lookup_critical_stub

    ld.d  $t1, $sp, 8*17  // restore stack
    move  $sp, $t1

    jirl  $ra, $t2, 0  // call native method

    // result sign extension is handled in C code, prepare for artQuickGenericJniEndTrampoline call:
    // uint64_t artQuickGenericJniEndTrampoline(Thread* self,       // a0
    //                                          jvalue result,      // a1 (need to move from a0)
    //                                          uint64_t result_f)  // a2 (need to move from fa0)
    move  $a1, $a0
    move  $a0, $xSELF
    movfr2gr.d  $a2, $fa0
    bl artQuickGenericJniEndTrampoline

    // Pending exceptions possible.
    ld.d  $t0, $xSELF, THREAD_EXCEPTION_OFFSET
    bnez  $t0, .Lexception_in_native

    // Tear down the alloca.
    move  $sp, $fp
    CFI_REMEMBER_STATE
    .cfi_def_cfa_register $sp

    // This does not clobber the result register a0. a1 is not used for result as the managed code
    // does not have a 128-bit type. Alternatively we could restore a subset of these registers.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    movgr2fr.d  $fa0, $a0
    jirl $zero, $ra, 0
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, FRAME_SIZE_SAVE_REFS_AND_ARGS

.Lexception_in_native:
    // Move to a1 then sp to please assembler.
    ld.d   $a1, $xSELF, THREAD_TOP_QUICK_FRAME_OFFSET
    addi.d $sp, $a1, -1  // Remove the GenericJNI tag.
    bl art_deliver_pending_exception
END art_quick_generic_jni_trampoline


ENTRY art_quick_to_interpreter_bridge
    SETUP_SAVE_REFS_AND_ARGS_FRAME

    // uint64_t artQuickToInterpreterBridge(ArtMethod* method, Thread* self, ArtMethod** sp)
    // a0 will contain ArtMethod*
    move   $a1, $xSELF
    move   $a2, $sp
    bl artQuickToInterpreterBridge

    // TODO: no need to restore arguments in this case.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME

    movgr2fr.d $fa0, $a0  // copy the result to FP result register

    RETURN_OR_DELIVER_PENDING_EXCEPTION_REG $t0
END art_quick_to_interpreter_bridge


UNDEFINED art_quick_imt_conflict_trampoline
UNDEFINED art_quick_method_exit_hook


// Called by managed code that is attempting to call a method on a proxy class. On entry a0 holds
// the proxy method and a1 holds the receiver. The frame size of the invoked proxy method agrees
// with kSaveRefsAndArgs frame.
.extern artQuickProxyInvokeHandler
ENTRY art_quick_proxy_invoke_handler
    SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0

    // uint64_t artQuickProxyInvokeHandler(ArtMethod* proxy_method,   // a0
    //                                     mirror::Object* receiver,  // a1
    //                                     Thread* self,              // a2
    //                                     ArtMethod** sp)            // a3
    move    $a2, $xSELF                 // pass Thread::Current
    move    $a3, $sp                    // pass sp
    bl  artQuickProxyInvokeHandler      // (Method* proxy method, receiver, Thread*, sp)

    ld.d    $a2, $xSELF, THREAD_EXCEPTION_OFFSET
    bnez    $a2, .Lexception_in_proxy   // success if no exception is pending
    CFI_REMEMBER_STATE
    RESTORE_SAVE_REFS_AND_ARGS_FRAME    // Restore frame
    movgr2fr.d  $fa0, $a0               // Store result in fa0 in case it was float or double
    jirl $zero, $ra, 0                  // return on success

.Lexception_in_proxy:
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, FRAME_SIZE_SAVE_REFS_AND_ARGS
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_proxy_invoke_handler


ENTRY art_quick_resolution_trampoline
    SETUP_SAVE_REFS_AND_ARGS_FRAME

    // const void* artQuickResolutionTrampoline(ArtMethod* called,         // a0
    //                                          mirror::Object* receiver,  // a1
    //                                          Thread* self,              // a2
    //                                          ArtMethod** sp)            // a3
    move   $a2, $xSELF
    move   $a3, $sp
    bl artQuickResolutionTrampoline
    CFI_REMEMBER_STATE
    beqz $a0, 1f
    move   $t0, $a0     // Remember returned code pointer in t0.
    ld.d   $a0, $sp, 0  // artQuickResolutionTrampoline puts called method in *sp.

    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    jr   $t0
1:
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, FRAME_SIZE_SAVE_REFS_AND_ARGS
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_resolution_trampoline


UNDEFINED art_quick_deoptimize_from_compiled_code
UNDEFINED art_quick_string_builder_append
UNDEFINED art_quick_compile_optimized
UNDEFINED art_quick_method_entry_hook
UNDEFINED art_quick_check_instance_of


.macro N_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING n, c_name, cxx_name
.extern \cxx_name
ENTRY \c_name
    SETUP_SAVE_EVERYTHING_FRAME       // save all registers as basis for long jump context.
    move    $a\n, $xSELF              // pass Thread::Current.
    bl      \cxx_name                 // \cxx_name(args..., Thread*).
    break
END \c_name
.endm


.macro NO_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING c_name, cxx_name
    N_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING 0, \c_name, \cxx_name
.endm


.macro TWO_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING c_name, cxx_name
    N_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING 2, \c_name, \cxx_name
.endm


.macro N_ARG_RUNTIME_EXCEPTION n, c_name, cxx_name
.extern \cxx_name
ENTRY \c_name
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME // save all registers as basis for long jump context.
    move    $a\n, $xSELF              // pass Thread::Current.
    bl      \cxx_name                 // \cxx_name(args..., Thread*).
    break
END \c_name
.endm

.macro NO_ARG_RUNTIME_EXCEPTION c_name, cxx_name
    N_ARG_RUNTIME_EXCEPTION 0, \c_name, \cxx_name
.endm


.macro ONE_ARG_RUNTIME_EXCEPTION c_name, cxx_name
    N_ARG_RUNTIME_EXCEPTION 1, \c_name, \cxx_name
.endm


// Called by managed code to create and deliver a NullPointerException.
NO_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING \
        art_quick_throw_null_pointer_exception, artThrowNullPointerExceptionFromCode


// Call installed by a signal handler to create and deliver a NullPointerException.
.extern artThrowNullPointerExceptionFromSignal
ENTRY art_quick_throw_null_pointer_exception_from_signal
    // The fault handler pushes the gc map address, i.e. "return address", to stack
    // and passes the fault address in RA. So we need to set up the CFI info accordingly.
    .cfi_def_cfa_offset __SIZEOF_POINTER__
    .cfi_rel_offset $ra, 0
   // Save all registers as basis for long jump context.
    INCREASE_FRAME (FRAME_SIZE_SAVE_EVERYTHING - __SIZEOF_POINTER__)
    SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP_SKIP_RA
    move    $a0, $ra                      // pass the fault address stored in RA by the fault handler.
    move    $a1, $xSELF                   // pass Thread::Current.
    bl  artThrowNullPointerExceptionFromSignal  // (arg, Thread*).
    break
END art_quick_throw_null_pointer_exception_from_signal


/*
 * Called to attempt to execute an obsolete method.
 */
ONE_ARG_RUNTIME_EXCEPTION art_invoke_obsolete_method_stub, artInvokeObsoleteMethod


// On entry a0 is uintptr_t* gprs_ and a1 is uint64_t* fprs_.
// Both must reside on the stack, between current sp and target sp.
ENTRY art_quick_do_long_jump
    // Load FPRs
    fld.d  $fa0,  $a1, 8*0   // f0
    fld.d  $fa1,  $a1, 8*1   // f1
    fld.d  $fa2,  $a1, 8*2   // f2
    fld.d  $fa3,  $a1, 8*3   // f3
    fld.d  $fa4,  $a1, 8*4   // f4
    fld.d  $fa5,  $a1, 8*5   // f5
    fld.d  $fa6,  $a1, 8*6   // f6
    fld.d  $fa7,  $a1, 8*7   // f7
    fld.d  $ft0,  $a1, 8*8   // f8
    fld.d  $ft1,  $a1, 8*9   // f9
    fld.d  $ft2,  $a1, 8*10  // f10
    fld.d  $ft3,  $a1, 8*11  // f11
    fld.d  $ft4,  $a1, 8*12  // f12
    fld.d  $ft5,  $a1, 8*13  // f13
    fld.d  $ft6,  $a1, 8*14  // f14
    fld.d  $ft7,  $a1, 8*15  // f15
    fld.d  $ft8,  $a1, 8*16  // f16
    fld.d  $ft9,  $a1, 8*17  // f17
    fld.d  $ft10, $a1, 8*18  // f18
    fld.d  $ft11, $a1, 8*19  // f19
    fld.d  $ft12, $a1, 8*20  // f20
    fld.d  $ft13, $a1, 8*21  // f21
    fld.d  $ft14, $a1, 8*22  // f22
    fld.d  $ft15, $a1, 8*23  // f23
    fld.d  $fs0,  $a1, 8*24  // f24
    fld.d  $fs1,  $a1, 8*25  // f25
    fld.d  $fs2,  $a1, 8*26  // f26
    fld.d  $fs3,  $a1, 8*27  // f27
    fld.d  $fs4,  $a1, 8*28  // f28
    fld.d  $fs5,  $a1, 8*29  // f29
    fld.d  $fs6,  $a1, 8*30  // f30
    fld.d  $fs7,  $a1, 8*31  // f31

    // Load GPRs.
    // Skip slot 8*0(a0) for zero/x0 as it is hard-wired zero.
    ld.d  $ra,  $a0, 8*1   // x1
    // Skip slot 8*2(a0) for platform-specific global pointer tp/x2.
    // Skip slot 8*3(a0) for sp/x3 as it is set below.
    // Delay loading a0 from 8*4(a0) as the base is in a0.
    ld.d  $a1,  $a0, 8*5   // x5
    ld.d  $a2,  $a0, 8*6   // x6
    ld.d  $a3,  $a0, 8*7   // x7
    ld.d  $a4,  $a0, 8*8   // x8
    ld.d  $a5,  $a0, 8*9   // x9
    ld.d  $a6,  $a0, 8*10  // x10
    ld.d  $a7,  $a0, 8*11  // x11
    // Skip slot 8*12(a0) for t0/x12 as it is clobbered below.
    // Skip slot 8*13(a0) for t1/x13 as it is clobbered below.
    ld.d  $t2,  $a0, 8*14  // x14
    ld.d  $t3,  $a0, 8*15  // x15
    ld.d  $t4,  $a0, 8*16  // x16
    ld.d  $t5,  $a0, 8*17  // x17
    ld.d  $t6,  $a0, 8*18  // x18
    ld.d  $t7,  $a0, 8*19  // x19
    ld.d  $t8,  $a0, 8*20  // x20
    // Skip slot 8*21(a0) for x21 as it is reserved.
    ld.d  $fp,  $a0, 8*22  // x22
    ld.d  $s0,  $a0, 8*23  // x23
    ld.d  $s1,  $a0, 8*24  // x24
    ld.d  $s2,  $a0, 8*25  // x25
    ld.d  $s3,  $a0, 8*26  // x26
    ld.d  $s4,  $a0, 8*27  // x27
    ld.d  $s5,  $a0, 8*28  // x28
    ld.d  $s6,  $a0, 8*29  // x29
    ld.d  $s7,  $a0, 8*30  // x30
    ld.d  $s8,  $a0, 8*31  // x31

    // Load sp to t0.
    ld.d  $t0, $a0, 8*3

    // Load PC to t1, it is in the last stack slot.
    ld.d  $t1, $a0, 8*32

    // Now load a0.
    ld.d  $a0,  $a0, 8*4  // x4

    // Set sp. Do not access fprs_ and gprs_ from now, they are below sp.
    move $sp, $t0

    jr  $t1
END art_quick_do_long_jump


UNDEFINED art_quick_osr_stub

UNDEFINED art_quick_alloc_array_resolved_dlmalloc
UNDEFINED art_quick_alloc_array_resolved_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved8_dlmalloc
UNDEFINED art_quick_alloc_array_resolved8_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved16_dlmalloc
UNDEFINED art_quick_alloc_array_resolved16_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved32_dlmalloc
UNDEFINED art_quick_alloc_array_resolved32_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved64_dlmalloc
UNDEFINED art_quick_alloc_array_resolved64_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_resolved_dlmalloc
UNDEFINED art_quick_alloc_object_resolved_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_initialized_dlmalloc
UNDEFINED art_quick_alloc_object_initialized_dlmalloc_instrumented
UNDEFINED art_quick_alloc_object_with_checks_dlmalloc
UNDEFINED art_quick_alloc_object_with_checks_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_object_dlmalloc
UNDEFINED art_quick_alloc_string_object_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_dlmalloc
UNDEFINED art_quick_alloc_string_from_bytes_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_chars_dlmalloc
UNDEFINED art_quick_alloc_string_from_chars_dlmalloc_instrumented
UNDEFINED art_quick_alloc_string_from_string_dlmalloc
UNDEFINED art_quick_alloc_string_from_string_dlmalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved_rosalloc
UNDEFINED art_quick_alloc_array_resolved_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved8_rosalloc
UNDEFINED art_quick_alloc_array_resolved8_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved16_rosalloc
UNDEFINED art_quick_alloc_array_resolved16_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved32_rosalloc
UNDEFINED art_quick_alloc_array_resolved32_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved64_rosalloc
UNDEFINED art_quick_alloc_array_resolved64_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_resolved_rosalloc
UNDEFINED art_quick_alloc_object_resolved_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_initialized_rosalloc
UNDEFINED art_quick_alloc_object_initialized_rosalloc_instrumented
UNDEFINED art_quick_alloc_object_with_checks_rosalloc
UNDEFINED art_quick_alloc_object_with_checks_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_object_rosalloc
UNDEFINED art_quick_alloc_string_object_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_rosalloc
UNDEFINED art_quick_alloc_string_from_bytes_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_chars_rosalloc
UNDEFINED art_quick_alloc_string_from_chars_rosalloc_instrumented
UNDEFINED art_quick_alloc_string_from_string_rosalloc
UNDEFINED art_quick_alloc_string_from_string_rosalloc_instrumented
UNDEFINED art_quick_alloc_array_resolved_bump_pointer
UNDEFINED art_quick_alloc_array_resolved_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved8_bump_pointer
UNDEFINED art_quick_alloc_array_resolved8_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved16_bump_pointer
UNDEFINED art_quick_alloc_array_resolved16_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved32_bump_pointer
UNDEFINED art_quick_alloc_array_resolved32_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved64_bump_pointer
UNDEFINED art_quick_alloc_array_resolved64_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_resolved_bump_pointer
UNDEFINED art_quick_alloc_object_resolved_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_initialized_bump_pointer
UNDEFINED art_quick_alloc_object_initialized_bump_pointer_instrumented
UNDEFINED art_quick_alloc_object_with_checks_bump_pointer
UNDEFINED art_quick_alloc_object_with_checks_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_object_bump_pointer
UNDEFINED art_quick_alloc_string_object_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_bump_pointer
UNDEFINED art_quick_alloc_string_from_bytes_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_chars_bump_pointer
UNDEFINED art_quick_alloc_string_from_chars_bump_pointer_instrumented
UNDEFINED art_quick_alloc_string_from_string_bump_pointer
UNDEFINED art_quick_alloc_string_from_string_bump_pointer_instrumented
UNDEFINED art_quick_alloc_array_resolved_tlab
UNDEFINED art_quick_alloc_array_resolved_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved8_tlab
UNDEFINED art_quick_alloc_array_resolved8_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved16_tlab
UNDEFINED art_quick_alloc_array_resolved16_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved32_tlab
UNDEFINED art_quick_alloc_array_resolved32_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved64_tlab
UNDEFINED art_quick_alloc_array_resolved64_tlab_instrumented
UNDEFINED art_quick_alloc_object_resolved_tlab
UNDEFINED art_quick_alloc_object_resolved_tlab_instrumented
UNDEFINED art_quick_alloc_object_initialized_tlab
UNDEFINED art_quick_alloc_object_initialized_tlab_instrumented
UNDEFINED art_quick_alloc_object_with_checks_tlab
UNDEFINED art_quick_alloc_object_with_checks_tlab_instrumented
UNDEFINED art_quick_alloc_string_object_tlab
UNDEFINED art_quick_alloc_string_object_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_tlab
UNDEFINED art_quick_alloc_string_from_bytes_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_chars_tlab
UNDEFINED art_quick_alloc_string_from_chars_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_string_tlab
UNDEFINED art_quick_alloc_string_from_string_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved_region
UNDEFINED art_quick_alloc_array_resolved_region_instrumented
UNDEFINED art_quick_alloc_array_resolved8_region
UNDEFINED art_quick_alloc_array_resolved8_region_instrumented
UNDEFINED art_quick_alloc_array_resolved16_region
UNDEFINED art_quick_alloc_array_resolved16_region_instrumented
UNDEFINED art_quick_alloc_array_resolved32_region
UNDEFINED art_quick_alloc_array_resolved32_region_instrumented
UNDEFINED art_quick_alloc_array_resolved64_region
UNDEFINED art_quick_alloc_array_resolved64_region_instrumented
UNDEFINED art_quick_alloc_object_resolved_region
UNDEFINED art_quick_alloc_object_resolved_region_instrumented
UNDEFINED art_quick_alloc_object_initialized_region
UNDEFINED art_quick_alloc_object_initialized_region_instrumented
UNDEFINED art_quick_alloc_object_with_checks_region
UNDEFINED art_quick_alloc_object_with_checks_region_instrumented
UNDEFINED art_quick_alloc_string_object_region
UNDEFINED art_quick_alloc_string_object_region_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_region
UNDEFINED art_quick_alloc_string_from_bytes_region_instrumented
UNDEFINED art_quick_alloc_string_from_chars_region
UNDEFINED art_quick_alloc_string_from_chars_region_instrumented
UNDEFINED art_quick_alloc_string_from_string_region
UNDEFINED art_quick_alloc_string_from_string_region_instrumented
UNDEFINED art_quick_alloc_array_resolved_region_tlab
UNDEFINED art_quick_alloc_array_resolved_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved8_region_tlab
UNDEFINED art_quick_alloc_array_resolved8_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved16_region_tlab
UNDEFINED art_quick_alloc_array_resolved16_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved32_region_tlab
UNDEFINED art_quick_alloc_array_resolved32_region_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved64_region_tlab
UNDEFINED art_quick_alloc_array_resolved64_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_resolved_region_tlab
UNDEFINED art_quick_alloc_object_resolved_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_initialized_region_tlab
UNDEFINED art_quick_alloc_object_initialized_region_tlab_instrumented
UNDEFINED art_quick_alloc_object_with_checks_region_tlab
UNDEFINED art_quick_alloc_object_with_checks_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_object_region_tlab
UNDEFINED art_quick_alloc_string_object_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_bytes_region_tlab
UNDEFINED art_quick_alloc_string_from_bytes_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_chars_region_tlab
UNDEFINED art_quick_alloc_string_from_chars_region_tlab_instrumented
UNDEFINED art_quick_alloc_string_from_string_region_tlab
UNDEFINED art_quick_alloc_string_from_string_region_tlab_instrumented

UNDEFINED art_quick_initialize_static_storage
UNDEFINED art_quick_resolve_type_and_verify_access
UNDEFINED art_quick_resolve_type
UNDEFINED art_quick_resolve_method_handle
UNDEFINED art_quick_resolve_method_type
UNDEFINED art_quick_resolve_string
UNDEFINED art_quick_set8_instance
UNDEFINED art_quick_set8_static
UNDEFINED art_quick_set16_instance
UNDEFINED art_quick_set16_static
UNDEFINED art_quick_set32_instance
UNDEFINED art_quick_set32_static
UNDEFINED art_quick_set64_instance
UNDEFINED art_quick_set64_static
UNDEFINED art_quick_set_obj_instance
UNDEFINED art_quick_set_obj_static
UNDEFINED art_quick_get_byte_instance
UNDEFINED art_quick_get_boolean_instance
UNDEFINED art_quick_get_short_instance
UNDEFINED art_quick_get_char_instance
UNDEFINED art_quick_get32_instance
UNDEFINED art_quick_get64_instance
UNDEFINED art_quick_get_obj_instance
UNDEFINED art_quick_get_byte_static
UNDEFINED art_quick_get_boolean_static
UNDEFINED art_quick_get_short_static
UNDEFINED art_quick_get_char_static
UNDEFINED art_quick_get32_static
UNDEFINED art_quick_get64_static
UNDEFINED art_quick_get_obj_static
UNDEFINED art_quick_aput_obj
UNDEFINED art_quick_lock_object_no_inline
UNDEFINED art_quick_lock_object
UNDEFINED art_quick_unlock_object_no_inline
UNDEFINED art_quick_unlock_object
UNDEFINED art_quick_invoke_direct_trampoline_with_access_check
UNDEFINED art_quick_invoke_interface_trampoline_with_access_check
UNDEFINED art_quick_invoke_static_trampoline_with_access_check
UNDEFINED art_quick_invoke_super_trampoline_with_access_check
UNDEFINED art_quick_invoke_virtual_trampoline_with_access_check
UNDEFINED art_quick_invoke_polymorphic
UNDEFINED art_quick_invoke_custom
UNDEFINED art_quick_test_suspend
UNDEFINED art_quick_deliver_exception
UNDEFINED art_quick_throw_array_bounds
UNDEFINED art_quick_throw_div_zero
UNDEFINED art_quick_throw_stack_overflow
UNDEFINED art_quick_throw_string_bounds
UNDEFINED art_quick_update_inline_cache
UNDEFINED art_jni_monitored_method_start
UNDEFINED art_jni_monitored_method_end
UNDEFINED art_quick_indexof


ENTRY art_quick_instrumentation_entry
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    // Preserve $a0 knowing there is a spare slot in kSaveRefsAndArgs.
    st.d    $a0, $sp, 8     // Save arg0.
    move    $a2, $xSELF     // pass Thread::Current
    move    $a3, $sp        // Pass $sp.
    bl  artInstrumentationMethodEntryFromCode  // (Method*, Object*, Thread*, SP)
    beq     $a0, $zero, .Ldeliver_instrumentation_entry_exception
                            // Deliver exception if we got nullptr as function.
    move    $t6, $a0        // $t6 holds reference to code
    ld.d    $a0, $sp, 8     // Restore arg0.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    la      $ra, art_quick_instrumentation_exit
    jirl    $zero, $t6, 0   // call method, returning to art_quick_instrumentation_exit
.Ldeliver_instrumentation_entry_exception:
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    DELIVER_PENDING_EXCEPTION
END art_quick_instrumentation_entry


ENTRY art_quick_instrumentation_exit
    move    $ra, $zero      // RA points here, so clobber with 0 for later checks.
    SETUP_SAVE_EVERYTHING_FRAME

    addi.d    $a3, $sp, SAVE_EVERYTHING_FRAME_OFFSET_FA0    // offset Pass fpr_res pointer ($fa0 in SAVE_EVERYTHING_FRAME).
    addi.d    $a2, $sp, SAVE_EVERYTHING_FRAME_OFFSET_A0     // offset Pass gpr_res pointer ($a0 in SAVE_EVERYTHING_FRAME).
    move      $a1, $sp      // Pass $sp.
    move      $a0, $xSELF   // pass Thread::Current
    bl     artInstrumentationMethodExitFromCode  // (Thread*, SP, gpr_res*, fpr_res*)

    beq     $a0, $zero, .Ldo_deliver_instrumentation_exception
                            // Deliver exception if we got nullptr as function.
    bne     $a1, $zero, .Ldeoptimize

    // Normal return.
    st.d    $a0, $sp, (FRAME_SIZE_SAVE_EVERYTHING-8)  // Set return pc.
    RESTORE_SAVE_EVERYTHING_FRAME
    jirl    $zero, $ra, 0
.Ldo_deliver_instrumentation_exception:
    DELIVER_PENDING_EXCEPTION_FRAME_READY
.Ldeoptimize:
    st.d    $a1, $sp, (FRAME_SIZE_SAVE_EVERYTHING-8)
                            // Fake a call from instrumentation return pc.
    RESTORE_SAVE_EVERYTHING_FRAME
    la      $t6, art_quick_deoptimize
    jirl    $zero, $t6, 0
END art_quick_instrumentation_exit


UNDEFINED art_quick_deoptimize
