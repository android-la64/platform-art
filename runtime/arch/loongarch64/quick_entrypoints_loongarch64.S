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
    addi.d $fp, $sp, (8*4)  // save frame pointer
    .cfi_def_cfa $fp, 48 - (8*4)

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
    ld.d   $ra, $a0, ART_METHOD_QUICK_CODE_OFFSET_64
    jirl $ra, $ra, 0

    addi.d $sp, $fp, -(8*4)  // restore SP (see `INVOKE_STUB_CREATE_FRAME`)
    .cfi_def_cfa $sp, 48

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
        ld.wu  $a1, $t0, 0                  // Load "this" parameter,
        addi.d $t0, $t0, 4                 // and increment arg pointer.
        .equ NUM_GPRS_TO_FILL, 6
    .else
        .equ NUM_GPRS_TO_FILL, 7
    .endif
    .equ NUM_FPRS_TO_FILL, 8

    // Load addresses for routines that load argument GPRs and FPRs.
    //lla  t4, .Lreg_handlers_start\sfx  // First handler for non-FP args. XC-TODO: verify
    la.local $t4, .Lreg_handlers_start\sfx
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
    jirl $zero, $t4, (NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    //li.d $t8, (NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    //add.d $t8, $t4, $t8
    //jirl $zero, $t8, 0

.Lload_long\sfx:
    addi.d $t0, $t0, 8
    jirl $zero, $t4, (2 * NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    //li.d $t8, (2 * NUM_GPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    //add.d $t8, $t4, $t8
    //jirl $zero, $t8, 0

.Lload_float\sfx:
    addi.d $t0, $t0, 4
    jirl $zero, $t5, 0

.Lload_double\sfx:
    addi.d $t0, $t0, 8
    jirl $zero, $t5, (NUM_FPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    //li.d $t8, (NUM_FPRS_TO_FILL * INVOKE_STUB_LOAD_REG_SIZE)
    //add.d $t8, $t5, $t8
    //jirl $zero, $t8, 0

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


    /*
     * All generated callsites for interface invokes and invocation slow paths will load arguments
     * as usual - except instead of loading arg0/A0 with the target Method*, arg0/A0 will contain
     * the method_idx. This wrapper will call the appropriate C++ helper while preserving arguments
     * and allowing a moving GC to update references in callee-save registers.
     * NOTE: "this" is the first visible argument of the target, and so can be found in arg1/A1.
     *
     * The helper will attempt to locate the target and return a 128-bit result consisting of the
     * target `ArtMethod*` in A0 and its `entry_point_from_quick_compiled_code_` in A1.
     *
     * If unsuccessful, the helper will return null/null. There will be a pending exception
     * to deliver in the thread.
     *
     * On success this wrapper will restore arguments and *jump* to the target, leaving the RA
     * pointing back to the original caller.
     */
.macro INVOKE_TRAMPOLINE_BODY cxx_name
    .extern \cxx_name
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    move    $a2, $xSELF                           // Pass Thread::Current().
    move    $a3, $sp                              // Pass pointer to the saved frame context.
    bl   \cxx_name                          // (method_idx, this, Thread*, $sp)
    move    $t0, $a1                              // Save method's code pointer in T0.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    beq   $a0, $zero, 1f
    jr    $t0
1:
    DELIVER_PENDING_EXCEPTION
.endm

.macro INVOKE_TRAMPOLINE c_name, cxx_name
ENTRY \c_name
    INVOKE_TRAMPOLINE_BODY \cxx_name
END \c_name
.endm


.macro DEOPT_OR_RETURN temp, is_ref = 0
    ld.wu   \temp, $xSELF, THREAD_DEOPT_CHECK_REQUIRED_OFFSET
    bnez  \temp, 2f
    jirl $zero, $ra, 0
2:
    SETUP_SAVE_EVERYTHING_FRAME
    li.d    $a2, \is_ref            // pass if result is a reference
    move    $a1, $a0                // pass the result
    move    $a0, $xSELF             // pass Thread::Current
    bl  artDeoptimizeIfNeeded       // (Thread*, uintptr_t, bool)
    RESTORE_SAVE_EVERYTHING_FRAME
    jirl $zero, $ra, 0
.endm


.macro RETURN_OR_DEOPT_IF_RESULT_IS_NON_NULL_OR_DELIVER
    beqz  $a0, 1f
    DEOPT_OR_RETURN $a1, /*is_ref=*/ 1
1:
    DELIVER_PENDING_EXCEPTION
.endm


.macro RETURN_OR_DEOPT_IF_INT_RESULT_IS_ZERO_OR_DELIVER
    bnez  $a0, 1f
    DEOPT_OR_RETURN $a1
1:
    DELIVER_PENDING_EXCEPTION
.endm


// Entry from managed code that tries to lock the object in a fast path and
// calls `artLockObjectFromCode()` for the difficult cases, may block for GC.
// A0 holds the possibly null object to lock.
ENTRY art_quick_lock_object
    LOCK_OBJECT_FAST_PATH $a0, art_quick_lock_object_no_inline, /*can_be_null*/ 1
END art_quick_lock_object


// Entry from managed code that calls `artLockObjectFromCode()`, may block for GC.
// A0 holds the possibly null object to lock.
    .extern artLockObjectFromCode
ENTRY art_quick_lock_object_no_inline
    // This is also the slow path for `art_quick_lock_object`.
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case we block
    move    $a1, $xSELF               // pass Thread::Current
    bl  artLockObjectFromCode         // (Object*, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_OR_DEOPT_IF_INT_RESULT_IS_ZERO_OR_DELIVER
END art_quick_lock_object_no_inline


// Entry from managed code that tries to unlock the object in a fast path and calls
// `artUnlockObjectFromCode()` for the difficult cases and delivers exception on failure.
// A0 holds the possibly null object to unlock.
ENTRY art_quick_unlock_object
    UNLOCK_OBJECT_FAST_PATH $a0, art_quick_unlock_object_no_inline, /*can_be_null*/ 1
END art_quick_unlock_object


// Entry from managed code that calls `artUnlockObjectFromCode()`
// and delivers exception on failure.
// A0 holds the possibly null object to unlock.
    .extern artUnlockObjectFromCode
ENTRY art_quick_unlock_object_no_inline
    // This is also the slow path for `art_quick_unlock_object`.
    SETUP_SAVE_REFS_ONLY_FRAME        // save callee saves in case exception allocation triggers GC
    move    $a1, $xSELF                   // pass Thread::Current
    bl  artUnlockObjectFromCode     // (Object*, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_OR_DEOPT_IF_INT_RESULT_IS_ZERO_OR_DELIVER
END art_quick_unlock_object_no_inline


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

    LOAD_RUNTIME_INSTANCE $a1
    ld.b $a1, $a1, RUN_EXIT_HOOKS_OFFSET_FROM_RUNTIME_INSTANCE
    bnez $a1, .Lcall_method_exit_hook

.Lcall_method_exit_hook_done:
    // This does not clobber the result register a0. a1 is not used for result as the managed code
    // does not have a 128-bit type. Alternatively we could restore a subset of these registers.
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    movgr2fr.d  $fa0, $a0
    jirl $zero, $ra, 0
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, FRAME_SIZE_SAVE_REFS_AND_ARGS

.Lcall_method_exit_hook:
    movgr2fr.d $fa0, $a0
    li.d   $a4, FRAME_SIZE_SAVE_REFS_AND_ARGS
    bl  art_quick_method_exit_hook
    b    .Lcall_method_exit_hook_done

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


    .extern artMethodEntryHook
ENTRY art_quick_method_entry_hook
    SETUP_SAVE_EVERYTHING_FRAME

    ld.d   $a0, $sp, FRAME_SIZE_SAVE_EVERYTHING   // Pass ArtMethod*.
    move   $a1, $xSELF                            // Pass Thread::Current().
    move   $a2, $sp                               // pass SP
    bl artMethodEntryHook                   // (ArtMethod*, Thread*, SP)

    RESTORE_SAVE_EVERYTHING_FRAME
    jirl $zero, $ra, 0
END art_quick_method_entry_hook


    .extern artMethodExitHook
ENTRY art_quick_method_exit_hook
    SETUP_SAVE_EVERYTHING_FRAME \
        RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET

    // frame_size is passed in A4 from JITed code and `art_quick_generic_jni_trampoline`.
    addi.d $a3, $sp, SAVE_EVERYTHING_FRAME_OFFSET_FA0  // FP result ptr in kSaveEverything frame
    addi.d $a2, $sp, SAVE_EVERYTHING_FRAME_OFFSET_A0   // integer result ptr in kSaveEverything frame
    addi.d $a1, $sp, FRAME_SIZE_SAVE_EVERYTHING        // ArtMethod**
    move   $a0, $xSELF                                 // Thread::Current
    bl artMethodExitHook                           // (Thread*, ArtMethod**, gpr_res*, fpr_res*,
                                                   // frame_size)

    // Normal return.
    RESTORE_SAVE_EVERYTHING_FRAME
    jirl $zero, $ra, 0
END art_quick_method_exit_hook


    /*
     * Polymorphic method invocation.
     * On entry:
     *   A0 = unused
     *   A1 = receiver
     */
.extern artInvokePolymorphic
ENTRY art_quick_invoke_polymorphic
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    move      $a0, $a1                      // Pass the receiver.
    move      $a1, $xSELF                   // Pass Thread::Current().
    move      $a2, $sp                      // Pass pointer to the saved frame context.
    bl    artInvokePolymorphic              // artInvokePolymorphic(receiver, Thread*, context)
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    movgr2fr.d $fa0, $a0                    // copy the result to FP result register
    RETURN_OR_DELIVER_PENDING_EXCEPTION_REG $t0
END art_quick_invoke_polymorphic


/*
     * InvokeCustom invocation.
     * On entry:
     *   A0 = call_site_idx
     */
.extern artInvokeCustom
ENTRY art_quick_invoke_custom
    SETUP_SAVE_REFS_AND_ARGS_FRAME
    move      $a1, $xSELF                   // Pass Thread::Current().
    move      $a2, $sp                      // Pass pointer to the saved frame context.
    bl    artInvokeCustom                   // artInvokeCustom(call_site_idx, Thread*, context)
    RESTORE_SAVE_REFS_AND_ARGS_FRAME
    movgr2fr.d $fa0, $a0                    // copy the result to FP result register
    RETURN_OR_DELIVER_PENDING_EXCEPTION_REG $t0
END art_quick_invoke_custom


    /*
     * Called to resolve an imt conflict.
     * On entry:
     *   A0 is the conflict ArtMethod.
     *   T0 is a hidden argument that holds the target interface method's dex method index.
     */
ENTRY art_quick_imt_conflict_trampoline
    ld.d      $t1, $a0, ART_METHOD_JNI_OFFSET_64  // Load ImtConflictTable
    ld.d      $a0, $t1, 0                         // Load first entry in ImtConflictTable.
.Limt_table_iterate:
    // Branch if found.
    beq     $a0, $t0, .Limt_table_found

    // If the entry is null, the interface method is not in the ImtConflictTable.
    beqz    $a0, .Lconflict_trampoline
    // Iterate over the entries of the ImtConflictTable.
    addi.d  $t1, $t1, (2 * __SIZEOF_POINTER__)
    ld.d    $a0, $t1, 0
    b       .Limt_table_iterate
.Limt_table_found:
    // We successfully hit an entry in the table. Load the target method and jump to it.
    ld.d    $a0, $t1, __SIZEOF_POINTER__
    ld.d    $t1, $a0, ART_METHOD_QUICK_CODE_OFFSET_64
    jr      $t1
.Lconflict_trampoline:
    // Call the runtime stub to populate the ImtConflictTable and jump to the
    // resolved method.
    move $a0, $t0  // Load interface method
    INVOKE_TRAMPOLINE_BODY artInvokeInterfaceTrampoline
END art_quick_imt_conflict_trampoline


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


// Entry from managed code that calls artInstanceOfFromCode and on failure calls
// artThrowClassCastExceptionForObject.
    .extern artInstanceOfFromCode
    .extern artThrowClassCastExceptionForObject
ENTRY art_quick_check_instance_of
    // Type check using the bit string passes null as the target class. In that case just throw.
    beqz  $a1, .Lthrow_class_cast_exception_for_bitstring_check

    // Store arguments and return address register.
    // Stack needs to be 16B aligned on calls.
    INCREASE_FRAME 32
    st.d    $a0, $sp, 0*8
    st.d    $a1, $sp, 1*8
    SAVE_GPR $ra, 3*8

    // Call runtime code.
    bl  artInstanceOfFromCode

    // Restore RA.
    RESTORE_GPR $ra, 3*8

    // Check for exception.
    CFI_REMEMBER_STATE
    beqz $a0, .Lthrow_class_cast_exception

    // Remove spill area and return (no need to restore A0 and A1).
    DECREASE_FRAME 32
    ret

.Lthrow_class_cast_exception:
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, 32
    // Restore A0 and remove spill area.
    ld.d    $a0, $sp, 0*8
    ld.d    $a1, $sp, 1*8
    DECREASE_FRAME 32

.Lthrow_class_cast_exception_for_bitstring_check:
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME         // Save all registers as basis for long jump context.
    move    $a2, $xSELF                       // Pass Thread::Current().
    bl  artThrowClassCastExceptionForObject   // (Object*, Class*, Thread*)
    break 0x5                                 // We should not return here...
END art_quick_check_instance_of


.macro N_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING n, c_name, cxx_name
.extern \cxx_name
ENTRY \c_name
    SETUP_SAVE_EVERYTHING_FRAME       // save all registers as basis for long jump context.
    move    $a\n, $xSELF              // pass Thread::Current.
    bl      \cxx_name                 // \cxx_name(args..., Thread*).
    break 0x5
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
    break 0x5
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
    break 0x5
END art_quick_throw_null_pointer_exception_from_signal


// Called by managed code to deliver an ArithmeticException.
NO_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING art_quick_throw_div_zero, artThrowDivZeroFromCode

// Called by managed code to create and deliver an ArrayIndexOutOfBoundsException.
// Arg0 holds index, arg1 holds limit.
TWO_ARG_RUNTIME_EXCEPTION_SAVE_EVERYTHING art_quick_throw_array_bounds, artThrowArrayBoundsFromCode

// Called by managed code to deliver an exception.
ONE_ARG_RUNTIME_EXCEPTION art_quick_deliver_exception, artDeliverExceptionFromCode

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


ENTRY art_quick_test_suspend
    SETUP_SAVE_EVERYTHING_FRAME \
        RUNTIME_SAVE_EVERYTHING_FOR_SUSPEND_CHECK_METHOD_OFFSET
    move   $a0, $xSELF
    bl artTestSuspendFromCode
    RESTORE_SAVE_EVERYTHING_FRAME
    jr $ra
END art_quick_test_suspend


// Helper macros for `art_quick_aput_obj`.
#ifdef USE_READ_BARRIER
#ifdef USE_BAKER_READ_BARRIER
.macro BAKER_RB_CHECK_GRAY_BIT_AND_LOAD dest, obj, offset, gray_slow_path_label
    ld.w    $t6, \obj, MIRROR_OBJECT_LOCK_WORD_OFFSET
    slli.w $t6, $t6, 31 - LOCK_WORD_READ_BARRIER_STATE_SHIFT  // Shift the state bit to sign bit.
    bltz  $t6, \gray_slow_path_label
    // False dependency to avoid needing load/load fence.
    xor   $t6, $t6, $t6
    add.d   \obj, \obj, $t6
    ld.wu   \dest, \obj, \offset                     // Heap reference = 32b; zero-extends to `dest`.
    UNPOISON_HEAP_REF \dest
.endm

.macro BAKER_RB_LOAD_AND_MARK dest, obj, offset, mark_function
    ld.wu   \dest, \obj, \offset                     // Heap reference = 32b; zero-extends to `dest`.
    UNPOISON_HEAP_REF \dest
    // Save RA in a register preserved by `art_quick_read_barrier_mark_regNN`
    // and unused by the `art_quick_aput_obj`.
    move    $t2, $ra
    bl  \mark_function
    move    $ra, $t2                                   // Restore RA.
.endm
#else  // USE_BAKER_READ_BARRIER
    .extern artReadBarrierSlow
.macro READ_BARRIER_SLOW dest, obj, offset
    // Store registers used in art_quick_aput_obj (a0-a4, RA), stack is 16B aligned.
    INCREASE_FRAME 48
    SAVE_GPR $a0, 0*8
    SAVE_GPR $a1, 1*8
    SAVE_GPR $a2, 2*8
    SAVE_GPR $a3, 3*8
    SAVE_GPR $a4, 4*8
    SAVE_GPR $ra, 5*8

    // move a0, \ref              // Pass ref in A0 (no-op for now since parameter ref is unused).
    .ifnc \obj, $a1
        move $a1, \obj            // Pass `obj`.
    .endif
    li.d    $a2, \offset          // Pass offset.
    bl  artReadBarrierSlow        // artReadBarrierSlow(ref, obj, offset)
    // No need to unpoison return value in A0, `artReadBarrierSlow()` would do the unpoisoning.
    .ifnc \dest, $a0
        move \dest, $a0           // save return value in dest
    .endif

    // Conditionally restore saved registers
    RESTORE_GPR_NE $a0, 0*8, \dest
    RESTORE_GPR_NE $a1, 1*8, \dest
    RESTORE_GPR_NE $a2, 2*8, \dest
    RESTORE_GPR_NE $a3, 3*8, \dest
    RESTORE_GPR_NE $a4, 4*8, \dest
    RESTORE_GPR $ra, 5*8
    DECREASE_FRAME 48
.endm
#endif  // USE_BAKER_READ_BARRIER
#endif  // USE_READ_BARRIER


ENTRY art_quick_aput_obj
    beqz  $a2, .Laput_obj_null
#if defined(USE_READ_BARRIER) && !defined(USE_BAKER_READ_BARRIER)
    READ_BARRIER_SLOW $a3, $a0, MIRROR_OBJECT_CLASS_OFFSET
    READ_BARRIER_SLOW $a3, $a3, MIRROR_CLASS_COMPONENT_TYPE_OFFSET
    READ_BARRIER_SLOW $a4, $a2, MIRROR_OBJECT_CLASS_OFFSET
#else  // !defined(USE_READ_BARRIER) || defined(USE_BAKER_READ_BARRIER)
#ifdef USE_READ_BARRIER
    // TODO(loongarch64): Define marking register to avoid this load.
    ld.w  $t6, $xSELF, THREAD_IS_GC_MARKING_OFFSET
    bnez  $t6, .Laput_obj_gc_marking
#endif  // USE_READ_BARRIER
    ld.wu $a3, $a0, MIRROR_OBJECT_CLASS_OFFSET         // Heap reference = 32b; zero-extends to a3.
    UNPOISON_HEAP_REF $a3
    ld.wu $a3, $a3, MIRROR_CLASS_COMPONENT_TYPE_OFFSET // Heap reference = 32b; zero-extends to a3.
    UNPOISON_HEAP_REF $a3
    ld.wu $a4, $a2, MIRROR_OBJECT_CLASS_OFFSET         // Heap reference = 32b; zero-extends to a4.
    UNPOISON_HEAP_REF $a4
#endif  // !defined(USE_READ_BARRIER) || defined(USE_BAKER_READ_BARRIER)
    // value's type == array's component type - trivial assignability
    bne   $a3, $a4, .Laput_obj_check_assignability
.Laput_obj_store:
    alsl.d $a3, $a1, $a0, 2
    POISON_HEAP_REF $a2
    st.w  $a2, $a3, MIRROR_OBJECT_ARRAY_DATA_OFFSET    // Heap reference = 32b.
    ld.d  $a3, $xSELF, THREAD_CARD_TABLE_OFFSET
    srli.d $a0, $a0, CARD_TABLE_CARD_SHIFT
    add.d $a0, $a0, $a3
    st.b  $a3, $a0, 0
    ret

.Laput_obj_null:
    alsl.d $a3, $a1, $a0, 2
    st.w   $a2, $a3, MIRROR_OBJECT_ARRAY_DATA_OFFSET      // Heap reference = 32b.
    ret

.Laput_obj_check_assignability:
    // Store arguments and return register
    INCREASE_FRAME 32
    SAVE_GPR $a0, 0*8
    SAVE_GPR $a1, 1*8
    SAVE_GPR $a2, 2*8
    SAVE_GPR $ra, 3*8

    // Call runtime code
    move   $a0, $a3          // Heap reference, 32b, "uncompress" = do nothing, already zero-extended.
    move   $a1, $a4          // Heap reference, 32b, "uncompress" = do nothing, already zero-extended.
    bl  artIsAssignableFromCode

    // Check for exception
    CFI_REMEMBER_STATE
    beqz $a0, .Laput_obj_throw_array_store_exception

    // Restore
    RESTORE_GPR $a0, 0*8
    RESTORE_GPR $a1, 1*8
    RESTORE_GPR $a2, 2*8
    RESTORE_GPR $ra, 3*8
    DECREASE_FRAME 32

    alsl.d $a3, $a1, $a0, 2
    POISON_HEAP_REF $a2
    st.w   $a2, $a3, MIRROR_OBJECT_ARRAY_DATA_OFFSET      // Heap reference = 32b.
    ld.d   $a3, $xSELF, THREAD_CARD_TABLE_OFFSET
    srli.d $a0, $a0, CARD_TABLE_CARD_SHIFT
    add.d  $a0, $a0, $a3
    st.b   $a3, $a0, 0
    ret

.Laput_obj_throw_array_store_exception:
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, 32
    RESTORE_GPR $a0, 0*8
    RESTORE_GPR $a1, 1*8
    RESTORE_GPR $a2, 2*8
    RESTORE_GPR $ra, 3*8
    DECREASE_FRAME 32

#if defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
    CFI_REMEMBER_STATE
#endif  // defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    move    $a1, $a2                    // Pass value.
    move    $a2, $xSELF                 // Pass Thread::Current().
    bl  artThrowArrayStoreException     // (Object*, Object*, Thread*).
    break 0x5                           // Unreachable.

#if defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, 0
.Laput_obj_gc_marking:
    BAKER_RB_CHECK_GRAY_BIT_AND_LOAD \
        $a3, $a0, MIRROR_OBJECT_CLASS_OFFSET, .Laput_obj_mark_array_class
.Laput_obj_mark_array_class_continue:
    BAKER_RB_CHECK_GRAY_BIT_AND_LOAD \
        $a3, $a3, MIRROR_CLASS_COMPONENT_TYPE_OFFSET, .Laput_obj_mark_array_element
.Laput_obj_mark_array_element_continue:
    BAKER_RB_CHECK_GRAY_BIT_AND_LOAD \
        $a4, $a2, MIRROR_OBJECT_CLASS_OFFSET, .Laput_obj_mark_object_class
.Laput_obj_mark_object_class_continue:
    // value's type == array's component type - trivial assignability
    bne   $a3, $a4, .Laput_obj_check_assignability
    b     .Laput_obj_store

.Laput_obj_mark_array_class:
    BAKER_RB_LOAD_AND_MARK $a3, $a0, MIRROR_OBJECT_CLASS_OFFSET, art_quick_read_barrier_mark_reg13
    b     .Laput_obj_mark_array_class_continue

.Laput_obj_mark_array_element:
    BAKER_RB_LOAD_AND_MARK \
        $a3, $a3, MIRROR_CLASS_COMPONENT_TYPE_OFFSET, art_quick_read_barrier_mark_reg13
    b     .Laput_obj_mark_array_element_continue

.Laput_obj_mark_object_class:
    BAKER_RB_LOAD_AND_MARK $a4, $a2, MIRROR_OBJECT_CLASS_OFFSET, art_quick_read_barrier_mark_reg14
    b     .Laput_obj_mark_object_class_continue
#endif  // defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
END art_quick_aput_obj


// Create a function `name` calling the art::ReadBarrier::Mark routine, getting its argument and
// returning its result through \reg, saving and restoring all caller-save registers.
//
// The generated function follows a non-standard calling convention:
// - register `reg` is used to pass the singleton argument,
// - register `reg` is used to return the result,
// - all other registers are callee-save (the values they hold are preserved).
.macro READ_BARRIER_MARK_REG name, reg
ENTRY \name
    beqz \reg, .Lrb_return_\name  // early return if null

    // Save t5 and t6 onto stack to honor caller-save calling convention.
    INCREASE_FRAME 16
    SAVE_GPR $t5, (8*0)
    SAVE_GPR $t6, (8*1)

    ld.w $t5, \reg, MIRROR_OBJECT_LOCK_WORD_OFFSET  // t5 := lock word
    slli.w $t6, $t5, 31-LOCK_WORD_MARK_BIT_SHIFT    // mark bit into MSB
    blt $t6, $zero, .Lrb_tmp_restore_\name
    // Check if the top two bits are set. If so, it is a forwarding address.
    slli.w $t6, $t5, 1
    and $t6, $t6, $t5
    CFI_REMEMBER_STATE
    bge $t6, $zero, .Lrb_full_\name
    // Extract and zero-extend the forwarding address.
    slli.d \reg, $t5, (LOCK_WORD_STATE_FORWARDING_ADDRESS_SHIFT + 32)
    srli.d \reg, \reg, 32
    .ifc \reg, t5
      st.d $t5, $sp, 8*0
    .endif
    .ifc \reg, $t6
      st.d $t6, $sp, 8*1
    .endif
.Lrb_tmp_restore_\name:
    RESTORE_GPR $t5, (8*0)
    RESTORE_GPR $t6, (8*1)
    DECREASE_FRAME 16
.Lrb_return_\name:
    jirl $zero, $ra, 0
.Lrb_full_\name:
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, 16
    // Save remaining caller-save registers on stack. t5 and t6 already saved.
    // fa0-fa7, ft0-ft15, a0-a7, t0-t4, t7, t8 ra: 8 * (8 + 16 + 8 + 7 + 1) = 8 * 40 = 320 bytes
    INCREASE_FRAME 320
    SAVE_FPR $fa0,  (8*0)
    SAVE_FPR $fa1,  (8*1)
    SAVE_FPR $fa2,  (8*2)
    SAVE_FPR $fa3,  (8*3)
    SAVE_FPR $fa4,  (8*4)
    SAVE_FPR $fa5,  (8*5)
    SAVE_FPR $fa6,  (8*6)
    SAVE_FPR $fa7,  (8*7)
    SAVE_FPR $ft0,  (8*8)
    SAVE_FPR $ft1,  (8*9)
    SAVE_FPR $ft2,  (8*10)
    SAVE_FPR $ft3,  (8*11)
    SAVE_FPR $ft4,  (8*12)
    SAVE_FPR $ft5,  (8*13)
    SAVE_FPR $ft6,  (8*14)
    SAVE_FPR $ft7,  (8*15)
    SAVE_FPR $ft8,  (8*16)
    SAVE_FPR $ft9,  (8*17)
    SAVE_FPR $ft10, (8*18)
    SAVE_FPR $ft11, (8*19)
    SAVE_FPR $ft12, (8*20)
    SAVE_FPR $ft13, (8*21)
    SAVE_FPR $ft14, (8*22)
    SAVE_FPR $ft15, (8*23)

    SAVE_GPR $a0,   (8*24)
    SAVE_GPR $a1,   (8*25)
    SAVE_GPR $a2,   (8*26)
    SAVE_GPR $a3,   (8*27)
    SAVE_GPR $a4,   (8*28)
    SAVE_GPR $a5,   (8*29)
    SAVE_GPR $a6,   (8*30)
    SAVE_GPR $a7,   (8*31)
    SAVE_GPR $t0,   (8*32)
    SAVE_GPR $t1,   (8*33)
    SAVE_GPR $t2,   (8*34)
    SAVE_GPR $t3,   (8*35)
    SAVE_GPR $t4,   (8*36)
    SAVE_GPR $t7,   (8*37)
    SAVE_GPR $t8,   (8*38)
    SAVE_GPR $ra,   (8*39)


    .ifc \reg, $t5
      ld.d $a0, $sp, (8*34)
    .else
      .ifc \reg, $t6
        ld.d $a0, $sp, (8*35)
      .else
        .ifnc \reg, $a0
          move $a0, \reg
        .endif
      .endif
    .endif
    bl artReadBarrierMark
    .ifnc \reg, $a0
      move \reg, $a0
    .endif

    // Restore all caller-save registers from stack, including t5 and t6.
    // fa0-fa7, ft0-ft15, a0-a7, t0-t8 ra: 8 * (8 + 16 + 8 + 9 + 1) = 8 * 40 = 336 bytes
    RESTORE_FPR $fa0,  (8*0)
    RESTORE_FPR $fa1,  (8*1)
    RESTORE_FPR $fa2,  (8*2)
    RESTORE_FPR $fa3,  (8*3)
    RESTORE_FPR $fa4,  (8*4)
    RESTORE_FPR $fa5,  (8*5)
    RESTORE_FPR $fa6,  (8*6)
    RESTORE_FPR $fa7,  (8*7)
    RESTORE_FPR $ft0,  (8*8)
    RESTORE_FPR $ft1,  (8*9)
    RESTORE_FPR $ft2,  (8*10)
    RESTORE_FPR $ft3,  (8*11)
    RESTORE_FPR $ft4,  (8*12)
    RESTORE_FPR $ft5,  (8*13)
    RESTORE_FPR $ft6,  (8*14)
    RESTORE_FPR $ft7,  (8*15)
    RESTORE_FPR $ft8,  (8*16)
    RESTORE_FPR $ft9,  (8*17)
    RESTORE_FPR $ft10, (8*18)
    RESTORE_FPR $ft11, (8*19)
    RESTORE_FPR $ft12, (8*20)
    RESTORE_FPR $ft13, (8*21)
    RESTORE_FPR $ft14, (8*22)
    RESTORE_FPR $ft15, (8*23)
    RESTORE_GPR_NE \reg, $a0, (8*24)
    RESTORE_GPR_NE \reg, $a1, (8*25)
    RESTORE_GPR_NE \reg, $a2, (8*26)
    RESTORE_GPR_NE \reg, $a3, (8*27)
    RESTORE_GPR_NE \reg, $a4, (8*28)
    RESTORE_GPR_NE \reg, $a5, (8*29)
    RESTORE_GPR_NE \reg, $a6, (8*30)
    RESTORE_GPR_NE \reg, $a7, (8*31)
    RESTORE_GPR_NE \reg, $t0, (8*32)
    RESTORE_GPR_NE \reg, $t1, (8*33)
    RESTORE_GPR_NE \reg, $t2, (8*34)
    RESTORE_GPR_NE \reg, $t3, (8*35)
    RESTORE_GPR_NE \reg, $t4, (8*36)
    RESTORE_GPR_NE \reg, $t7, (8*37)
    RESTORE_GPR_NE \reg, $t8, (8*38)
    RESTORE_GPR_NE \reg, $ra, (8*39)
    RESTORE_GPR_NE \reg, $t5, (8*40)
    RESTORE_GPR_NE \reg, $t6, (8*41)
    DECREASE_FRAME 336
    jirl $zero, $ra, 0
END \name
.endm



// Z-TODO: match la abi.
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg10, $a0
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg11, $a1
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg13, $a3
READ_BARRIER_MARK_REG art_quick_read_barrier_mark_reg14, $a4


.macro N_ARG_DOWNCALL n, name, entrypoint, return
    .extern \entrypoint
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME        // Save callee saves in case of GC.
    move    $a\n, $xSELF                  // Pass Thread::Current().
    bl  \entrypoint                 // (<n args>, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    \return
END \name
.endm


.macro ONE_ARG_DOWNCALL name, entrypoint, return
    N_ARG_DOWNCALL 1, \name, \entrypoint, \return
.endm


.macro TWO_ARG_DOWNCALL name, entrypoint, return
    N_ARG_DOWNCALL 2, \name, \entrypoint, \return
.endm


.macro THREE_ARG_DOWNCALL name, entrypoint, return
    N_ARG_DOWNCALL 3, \name, \entrypoint, \return
.endm


.macro FOUR_ARG_DOWNCALL name, entrypoint, return
    N_ARG_DOWNCALL 4, \name, \entrypoint, \return
.endm


.macro ALLOC_ARRAY_TLAB_FAST_PATH_RESOLVED_WITH_SIZE \
    slowPathLabel, class, count, temp0, temp1, temp2
    li.d $t7, OBJECT_ALIGNMENT_MASK_TOGGLED64
    and  \temp1, \temp1, $t7                                // Apply alignment mask
                                                            // (addr + 7) & ~7. The mask must
                                                            // be 64 bits to keep high bits in
                                                            // case of overflow.
    // Negative sized arrays are handled here since xCount holds a zero extended 32 bit value.
    // Negative ints become large 64 bit unsigned ints which will always be larger than max signed
    // 32 bit int. Since the max shift for arrays is 3, it can not become a negative 64 bit int.
    li.d    \temp2, MIN_LARGE_OBJECT_THRESHOLD              // Possibly a large object, go slow
    bgeu  \temp1, \temp2, \slowPathLabel                    // path.

    ld.d    \temp0, $xSELF, THREAD_LOCAL_POS_OFFSET          // Check tlab for space, note that
                                                            // we use (end - begin) to handle
                                                            // negative size arrays. It is
                                                            // assumed that a negative size will
                                                            // always be greater unsigned than
                                                            // region size.
    ld.d    \temp2, $xSELF, THREAD_LOCAL_END_OFFSET
    sub.d   \temp2, \temp2, \temp0

    // The array class is always initialized here. Unlike new-instance,
    // this does not act as a double test.
    bgtu  \temp1, \temp2, \slowPathLabel
    // "Point of no slow path". Won't go to the slow path from here on. OK to clobber x0 and x1.
                                                            // Move old thread_local_pos to x0
                                                            // for the return value.
    move    $a0, \temp0
    add.d   \temp0, \temp0, \temp1
    st.d    \temp0, $xSELF, THREAD_LOCAL_POS_OFFSET            // Store new thread_local_pos.
    POISON_HEAP_REF \class
    st.w    \class, $a0, MIRROR_OBJECT_CLASS_OFFSET            // Store the class pointer.
    st.w    \count, $a0, MIRROR_ARRAY_LENGTH_OFFSET            // Store the array length.
// new-array is special. The class is loaded and immediately goes to the Initialized state
// before it is published. Therefore the only fence needed is for the publication of the object.
// See ClassLinker::CreateArrayClass() for more details.

// For publication of the new array, we don't need a 'fence w, w' here.
// The compiler generates 'fence w, w' for all new-array insts.
    ret
.endm

// Caller must execute a constructor fence after this.
.macro GENERATE_ALLOC_ARRAY_TLAB name, entrypoint, size_setup
ENTRY \name
    // Fast path array allocation for region tlab allocation.
    // a0: mirror::Class* type
    // a1: int32_t component_count
    // a2-a7: free.
    move    $a3, $a0
    \size_setup $a3, $a1, $a4, $a5, $a6
    ALLOC_ARRAY_TLAB_FAST_PATH_RESOLVED_WITH_SIZE .Lslow_path\name, $a3, $a1, $a4, $a5, $a6
.Lslow_path\name:
    // a0: mirror::Class* klass
    // a1: int32_t component_count
    // a2: Thread* self
    SETUP_SAVE_REFS_ONLY_FRAME                              // Save callee saves in case of GC.
    move    $a2, $xSELF                                         // Pass Thread::Current().
    bl  \entrypoint
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_OR_DEOPT_IF_RESULT_IS_NON_NULL_OR_DELIVER
END \name
.endm


// If isInitialized=1 then the compiler assumes the object's class has already been initialized.
// If isInitialized=0 the compiler can only assume it's been at least resolved.
.macro ALLOC_OBJECT_TLAB_FAST_PATH_RESOLVED slowPathLabel isInitialized
    ld.d    $a4, $xSELF, THREAD_LOCAL_POS_OFFSET
    ld.d    $a5, $xSELF, THREAD_LOCAL_END_OFFSET
    ld.wu   $a7, $a0, MIRROR_CLASS_OBJECT_SIZE_ALLOC_FAST_PATH_OFFSET  // Load the object size (a7).
    add.d   $a6, $a4, $a7                                   // Add object size to tlab pos.
                                                            // Check if it fits, overflow works
                                                            // since the tlab pos and end are 32
                                                            // bit values.

    // If the class is not yet visibly initialized, or it is finalizable,
    // the object size will be very large to force the branch below to be taken.
    //
    // See Class::SetStatus() in class.cc for more details.
    bgtu  $a6, $a5, \slowPathLabel
    st.d    $a6, $xSELF, THREAD_LOCAL_POS_OFFSET            // Store new thread_local_pos.
    POISON_HEAP_REF $a0
    st.w    $a0, $a4, MIRROR_OBJECT_CLASS_OFFSET                // Store the class pointer.
    move    $a0, $a4
    // No barrier. The class is already observably initialized (otherwise the fast
    // path size check above would fail) and new-instance allocations are protected
    // from publishing by the compiler which inserts its own StoreStore barrier.
    ret
.endm

// The common code for art_quick_alloc_object_*region_tlab
// Currently the implementation ignores isInitialized. TODO(b/172087402): clean this up.
// Caller must execute a constructor fence after this.
.macro GENERATE_ALLOC_OBJECT_RESOLVED_TLAB name, entrypoint, isInitialized
ENTRY \name
    // Fast path region tlab allocation.
    // a0: type, xSELF(s1): Thread::Current
    // a1-a7: free.
    ALLOC_OBJECT_TLAB_FAST_PATH_RESOLVED .Lslow_path\name, \isInitialized
.Lslow_path\name:
    SETUP_SAVE_REFS_ONLY_FRAME                            // Save callee saves in case of GC.
    move    $a1, $xSELF                                   // Pass Thread::Current().
    bl  \entrypoint                                       // (mirror::Class*, Thread*)
    RESTORE_SAVE_REFS_ONLY_FRAME
    RETURN_OR_DEOPT_IF_RESULT_IS_NON_NULL_OR_DELIVER
END \name
.endm

GENERATE_ALLOC_OBJECT_RESOLVED_TLAB \
    art_quick_alloc_object_resolved_region_tlab, \
    artAllocObjectFromCodeResolvedRegionTLAB, /* isInitialized */ 0
GENERATE_ALLOC_OBJECT_RESOLVED_TLAB \
    art_quick_alloc_object_initialized_region_tlab, \
    artAllocObjectFromCodeInitializedRegionTLAB, /* isInitialized */ 1
GENERATE_ALLOC_OBJECT_RESOLVED_TLAB \
    art_quick_alloc_object_resolved_tlab, \
    artAllocObjectFromCodeResolvedTLAB, /* isInitialized */ 0
GENERATE_ALLOC_OBJECT_RESOLVED_TLAB \
    art_quick_alloc_object_initialized_tlab, \
    artAllocObjectFromCodeInitializedTLAB, /* isInitialized */ 1


.macro COMPUTE_ARRAY_SIZE_UNKNOWN class, count, temp0, temp1, temp2
    // Array classes are never finalizable or uninitialized, no need to check.
    ld.wu   \temp0, \class, MIRROR_CLASS_COMPONENT_TYPE_OFFSET// Load component type
    UNPOISON_HEAP_REF \temp0
    ld.wu   \temp0, \temp0, MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET
    srli.d  \temp0, \temp0, PRIMITIVE_TYPE_SIZE_SHIFT_SHIFT   // Component size shift is in high 16
                                                            // bits.
    bstrpick.d \temp1, \count, 31, 0                        // From \count we use a 32 bit value,
                                                            // it can not overflow.
    sll.d   \temp1, \temp1, \temp0                          // Calculate data size
    // Add array data offset and alignment.
    addi.d   \temp1, \temp1, (MIRROR_INT_ARRAY_DATA_OFFSET + OBJECT_ALIGNMENT_MASK)
#if MIRROR_LONG_ARRAY_DATA_OFFSET != MIRROR_INT_ARRAY_DATA_OFFSET + 4
#error Long array data offset must be 4 greater than int array data offset.
#endif

    addi.d  \temp0, \temp0, 1                               // Add 4 to the length only if the
                                                            // component size shift is 3
                                                            // (for 64 bit alignment).
    andi  \temp0, \temp0, 4
    add.d   \temp1, \temp1, \temp0
.endm


// Entry from managed code that calls artHandleFillArrayDataFromCode and
// delivers exception on failure.
TWO_ARG_DOWNCALL art_quick_handle_fill_data, \
                 artHandleFillArrayDataFromCode, \
                 RETURN_OR_DEOPT_IF_INT_RESULT_IS_ZERO_OR_DELIVER


// Called by managed code to create and deliver a StackOverflowError.
NO_ARG_RUNTIME_EXCEPTION art_quick_throw_stack_overflow, artThrowStackOverflowFromCode

// TODO(ngeoffray): art_quick_alloc_array_resolved_region_tlab is not used for arm64, remove
// the entrypoint once all backends have been updated to use the size variants.
GENERATE_ALLOC_ARRAY_TLAB art_quick_alloc_array_resolved_tlab, \
                          artAllocArrayFromCodeResolvedTLAB, \
                          COMPUTE_ARRAY_SIZE_UNKNOWN

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
UNDEFINED art_quick_alloc_array_resolved_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved8_tlab
UNDEFINED art_quick_alloc_array_resolved8_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved16_tlab
UNDEFINED art_quick_alloc_array_resolved16_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved32_tlab
UNDEFINED art_quick_alloc_array_resolved32_tlab_instrumented
UNDEFINED art_quick_alloc_array_resolved64_tlab
UNDEFINED art_quick_alloc_array_resolved64_tlab_instrumented
UNDEFINED art_quick_alloc_object_resolved_tlab_instrumented
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
UNDEFINED art_quick_alloc_object_resolved_region_tlab_instrumented
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
UNDEFINED art_quick_invoke_direct_trampoline_with_access_check
UNDEFINED art_quick_invoke_interface_trampoline_with_access_check
UNDEFINED art_quick_invoke_static_trampoline_with_access_check
UNDEFINED art_quick_invoke_super_trampoline_with_access_check
UNDEFINED art_quick_invoke_virtual_trampoline_with_access_check
UNDEFINED art_quick_throw_string_bounds
UNDEFINED art_quick_update_inline_cache
UNDEFINED art_jni_monitored_method_start
UNDEFINED art_jni_monitored_method_end
UNDEFINED art_quick_indexof
UNDEFINED art_quick_deoptimize
