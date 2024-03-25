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


UNDEFINED art_jni_method_start
UNDEFINED art_jni_method_end
UNDEFINED art_jni_read_barrier
UNDEFINED art_jni_method_entry_hook
UNDEFINED art_jni_lock_object_no_inline
UNDEFINED art_jni_lock_object
UNDEFINED art_jni_unlock_object_no_inline
UNDEFINED art_jni_unlock_object


// 8 argument GPRS: a0 - a7 and 8 argument FPRs: fa0 - fa7
#define ALL_ARGS_SIZE (8 * (8 + 8))


.macro SAVE_ALL_ARGS_INCREASE_FRAME extra_space
    // Reserve space for all argument registers, plus the extra space.
    INCREASE_FRAME (ALL_ARGS_SIZE + \extra_space)

    // Argument GPRs a0 - a7.
    st.d    $a0, $sp, (8*0)
    st.d    $a1, $sp, (8*1)
    st.d    $a2, $sp, (8*2)
    st.d    $a3, $sp, (8*3)
    st.d    $a4, $sp, (8*4)
    st.d    $a5, $sp, (8*5)
    st.d    $a6, $sp, (8*6)
    st.d    $a7, $sp, (8*7)

    // Argument FPRs fa0 - fa7.
    fst.d   $fa0, $sp, (8*8)
    fst.d   $fa1, $sp, (8*9)
    fst.d   $fa2, $sp, (8*10)
    fst.d   $fa3, $sp, (8*11)
    fst.d   $fa4, $sp, (8*12)
    fst.d   $fa5, $sp, (8*13)
    fst.d   $fa6, $sp, (8*14)
    fst.d   $fa7, $sp, (8*15)
.endm


.macro RESTORE_ALL_ARGS_DECREASE_FRAME extra_space
    // Argument GPRs a0 - a7.
    ld.d    $a0, $sp, (8*0)
    ld.d    $a1, $sp, (8*1)
    ld.d    $a2, $sp, (8*2)
    ld.d    $a3, $sp, (8*3)
    ld.d    $a4, $sp, (8*4)
    ld.d    $a5, $sp, (8*5)
    ld.d    $a6, $sp, (8*6)
    ld.d    $a7, $sp, (8*7)

    // Argument FPRs fa0 - fa7.
    fld.d   $fa0, $sp, (8*8)
    fld.d   $fa1, $sp, (8*9)
    fld.d   $fa2, $sp, (8*10)
    fld.d   $fa3, $sp, (8*11)
    fld.d   $fa4, $sp, (8*12)
    fld.d   $fa5, $sp, (8*13)
    fld.d   $fa6, $sp, (8*14)
    fld.d   $fa7, $sp, (8*15)

    DECREASE_FRAME (ALL_ARGS_SIZE + \extra_space)
.endm


// JNI dlsym lookup stub.
.extern artFindNativeMethod
.extern artFindNativeMethodRunnable
ENTRY art_jni_dlsym_lookup_stub
    SAVE_ALL_ARGS_INCREASE_FRAME 2*8
    SAVE_GPR $fp, (ALL_ARGS_SIZE + 0)
    SAVE_GPR $ra, (ALL_ARGS_SIZE + 8)
    addi.d  $fp, $sp, ALL_ARGS_SIZE

    // Call artFindNativeMethod for normal native.
    // Call artFindNativeMethodRunnable for @FastNative or @CriticalNative.
    // Both functions have a single argument: Thread::Current() in a0.
    move   $a0, $xSELF
    ld.d   $t0, $a0, THREAD_TOP_QUICK_FRAME_OFFSET   // uintptr_t tagged_quick_frame
    li.d   $t1, -2                                   // ArtMethod** sp
    and    $t0, $t0, $t1
    ld.d   $t0, $t0, 0                               // ArtMethod* method
    ld.w   $t0, $t0, ART_METHOD_ACCESS_FLAGS_OFFSET  // uint32_t access_flags
    li.d   $t1, (ACCESS_FLAGS_METHOD_IS_FAST_NATIVE | ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE)
    and    $t0, $t0, $t1
    bnez $t0, .Llookup_stub_fast_or_critical_native
    bl  artFindNativeMethod
    b   .Llookup_stub_continue

.Llookup_stub_fast_or_critical_native:
    bl  artFindNativeMethodRunnable

.Llookup_stub_continue:
    move    $t0, $a0  // store result in a temp reg.
    RESTORE_GPR $fp, (ALL_ARGS_SIZE + 0)
    RESTORE_GPR $ra, (ALL_ARGS_SIZE + 8)
    RESTORE_ALL_ARGS_DECREASE_FRAME 2*8

    beqz  $t0, 1f  // is method code null?
    jr    $t0      // if non-null, tail call to method code.
1:
    jirl $zero, $ra, 0           // restore regs and return to caller to handle exception.
END art_jni_dlsym_lookup_stub


// JNI dlsym lookup stub for @CriticalNative.
ENTRY art_jni_dlsym_lookup_critical_stub
    // The hidden arg holding the tagged method is t0 (loaded by compiled JNI stub, compiled
    // managed code, or `art_quick_generic_jni_trampoline`). Bit 0 set means generic JNI.
    // For generic JNI we already have a managed frame, so we reuse the art_jni_dlsym_lookup_stub.
    andi  $t6, $t0, 1
    bnez  $t6, art_jni_dlsym_lookup_stub

    // Save args, the hidden arg and caller PC. No CFI needed for args and the hidden arg.
    SAVE_ALL_ARGS_INCREASE_FRAME 2*8
    SAVE_GPR $t0, (ALL_ARGS_SIZE + 0)
    SAVE_GPR $ra, (ALL_ARGS_SIZE + 8)

    // Call artCriticalNativeFrameSize(method, caller_pc)
    move  $a0, $t0  // a0 := method (from hidden arg)
    move  $a1, $ra  // a1 := caller_pc
    bl    artCriticalNativeFrameSize

    // Move frame size to T2.
    move  $t2, $a0

    // Restore args, the hidden arg and caller PC.
    RESTORE_GPR $t0, (ALL_ARGS_SIZE + 0)
    RESTORE_GPR $ra, (ALL_ARGS_SIZE + 8)
    RESTORE_ALL_ARGS_DECREASE_FRAME 2*8

    // Reserve space for a SaveRefsAndArgs managed frame, either for the actual runtime
    // method or for a GenericJNI frame which is similar but has a native method and a tag.
    // Add space for RA and padding to keep the stack 16-byte aligned.
    INCREASE_FRAME (FRAME_SIZE_SAVE_REFS_AND_ARGS + 16)

    // Prepare the return address for managed stack walk of the SaveRefsAndArgs frame.
    // If we're coming from JNI stub with tail call, it is RA. If we're coming from
    // JNI stub that saved the return address, it will be the last value we copy below.
    // If we're coming directly from compiled code, it is RA, set further down.
    move  $t4, $ra

    // Move the stack args if any. Calculate the base address of the managed frame in the process.
    addi.d  $t1, $sp, 16
    beqz  $t2, .Lcritical_skip_copy_args
.Lcritical_copy_args_loop:
    ld.d    $t3, $t1, FRAME_SIZE_SAVE_REFS_AND_ARGS+0
    ld.d    $t4, $t1, FRAME_SIZE_SAVE_REFS_AND_ARGS+8
    addi.d  $t2, $t2, -16
    st.d    $t3, $t1, 0-16
    st.d    $t4, $t1, 8-16
    addi.d  $t1, $t1, 16
    bnez  $t2, .Lcritical_copy_args_loop
.Lcritical_skip_copy_args:

    // Spill registers for the SaveRefsAndArgs frame above the stack args.
    // Note that the runtime shall not examine the args here, otherwise we would have to
    // move them in registers and stack to account for the difference between managed and
    // native ABIs. Do not update CFI while we hold the frame address in T1 and the values
    // in registers are unchanged.
    // stack slot (0*8)(t1) is for ArtMethod*
    fst.d   $fa0, $t1, (1*8)
    fst.d   $fa1, $t1, (2*8)
    fst.d   $fa2, $t1, (3*8)
    fst.d   $fa3, $t1, (4*8)
    fst.d   $fa4, $t1, (5*8)
    fst.d   $fa5, $t1, (6*8)
    fst.d   $fa6, $t1, (7*8)
    fst.d   $fa7, $t1, (8*8)

    // a0 (x10) is the method pointer
    st.d    $a1, $t1, (9*8)   // x11
    st.d    $a2, $t1, (10*8)  // x12
    st.d    $a3, $t1, (11*8)  // x13
    st.d    $a4, $t1, (12*8)  // x14
    st.d    $a5, $t1, (13*8)  // x15
    st.d    $a6, $t1, (14*8)  // x16
    st.d    $a7, $t1, (15*8)  // x17

    st.d    $fp, $t1, (16*8)  // x22, s9, frame pointer

    st.d    $s0, $t1, (17*8)  // x23
    // s1 (x24) is the ART thread register
    st.d    $s2, $t1, (18*8)  // x25
    st.d    $s3, $t1, (19*8)  // x26
    st.d    $s4, $t1, (20*8)  // x27
    st.d    $s5, $t1, (21*8)  // x28
    st.d    $s6, $t1, (22*8)  // x29
    st.d    $s7, $t1, (23*8)  // x30
    st.d    $s8, $t1, (24*8)  // x31
    st.d    $t4, $t1, (25*8)  // t4: Save return address for tail call from JNI stub.
    // (If there were any stack args, we're storing the value that's already there.
    // For direct calls from compiled managed code, we shall overwrite this below.)

    // Move the managed frame address to native callee-save register fp (x8) and update CFI.
    move    $fp, $t1
    // Skip args FA0-FA7, A1-A7
    CFI_EXPRESSION_BREG 22, 8, (16*8)
    CFI_EXPRESSION_BREG 23, 8, (17*8)
    CFI_EXPRESSION_BREG 25, 8, (18*8)
    CFI_EXPRESSION_BREG 26, 8, (19*8)
    CFI_EXPRESSION_BREG 27, 8, (20*8)
    CFI_EXPRESSION_BREG 28, 8, (21*8)
    CFI_EXPRESSION_BREG 29, 8, (22*8)
    CFI_EXPRESSION_BREG 30, 8, (23*8)
    CFI_EXPRESSION_BREG 31, 8, (24*8)
    // The saved return PC for managed stack walk is not necessarily our RA.

    // Save our return PC below the managed frame.
    st.d    $ra, $fp, -__SIZEOF_POINTER__
    CFI_EXPRESSION_BREG 1, 8, -__SIZEOF_POINTER__

    ld.w    $t2, $t0, ART_METHOD_ACCESS_FLAGS_OFFSET  // Load access flags.
    addi.d  $t1, $fp, 1        // Prepare managed SP tagged for a GenericJNI frame.
    //slliw t2, t2, 31 - ACCESS_FLAGS_METHOD_IS_NATIVE_BIT
    //bltz  t2, .Lcritical_skip_prepare_runtime_method
    andi    $t2, $t2, ACCESS_FLAGS_METHOD_IS_NATIVE
    bnez    $t2, .Lcritical_skip_prepare_runtime_method

    // When coming from a compiled method, the return PC for managed stack walk is RA.
    // (When coming from a compiled stub, the correct return PC is already stored above.)
    st.d    $ra, $fp, (FRAME_SIZE_SAVE_REFS_AND_ARGS - __SIZEOF_POINTER__)

    // Replace the target method with the SaveRefsAndArgs runtime method.
    LOAD_RUNTIME_INSTANCE $t0
    ld.d    $t0, $t0, RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET

    move    $t1, $fp           // Prepare untagged managed SP for the runtime method.

.Lcritical_skip_prepare_runtime_method:
    // Store the method on the bottom of the managed frame.
    st.d    $t0, $fp, 0

    // Place (maybe tagged) managed SP in Thread::Current()->top_quick_frame.
    st.d    $t1, $xSELF, THREAD_TOP_QUICK_FRAME_OFFSET

    // Preserve the native arg register A0 in callee-save register S2 (x18) which was saved above.
    move    $s2, $a0

    // Call artFindNativeMethodRunnable()
    move    $a0, $xSELF   // pass Thread::Current()
    bl  artFindNativeMethodRunnable

    // Store result in scratch reg.
    move    $t0, $a0

    // Restore the native arg register A0.
    move    $a0, $s2

    // Restore our return PC.
    RESTORE_GPR_BASE $fp, $ra, -__SIZEOF_POINTER__

    // Remember the end of out args before restoring FP.
    addi.d  $t1, $fp, -16

    // Restore arg registers.
    fld.d   $fa0, $t1, (1*8)
    fld.d   $fa1, $t1, (2*8)
    fld.d   $fa2, $t1, (3*8)
    fld.d   $fa3, $t1, (4*8)
    fld.d   $fa4, $t1, (5*8)
    fld.d   $fa5, $t1, (6*8)
    fld.d   $fa6, $t1, (7*8)
    fld.d   $fa7, $t1, (8*8)
    // fp (x8) is restored last to keep CFI data valid until then.
    // s1 (x9) is the ART thread register
    // a0 (x10) is the method pointer
    ld.d    $a1, $t1, (9*8)   // x11
    ld.d    $a2, $t1, (10*8)  // x12
    ld.d    $a3, $t1, (11*8)  // x13
    ld.d    $a4, $t1, (12*8)  // x14
    ld.d    $a5, $t1, (13*8)  // x15
    ld.d    $a6, $t1, (14*8)  // x16
    ld.d    $a7, $t1, (15*8)  // x17
    RESTORE_GPR_BASE $fp, $s0,  (17*8)  // x23
    RESTORE_GPR_BASE $fp, $s2,  (18*8)  // x25
    RESTORE_GPR_BASE $fp, $s3,  (19*8)  // x26
    RESTORE_GPR_BASE $fp, $s4,  (20*8)  // x27
    RESTORE_GPR_BASE $fp, $s5,  (21*8)  // x28
    RESTORE_GPR_BASE $fp, $s6,  (22*8)  // x29
    RESTORE_GPR_BASE $fp, $s7,  (23*8)  // x30
    RESTORE_GPR_BASE $fp, $s8,  (24*8)  // x31
    RESTORE_GPR_BASE $fp, $fp,  (16*8)  // fp (x22) is restored last

    // Check for exception before moving args back to keep the return PC for managed stack walk.
    CFI_REMEMBER_STATE
    beqz  $t0, .Lcritical_deliver_exception

    // Move stack args to their original place.
    beq     $t1, $sp, .Lcritical_skip_copy_args_back
    sub.d   $t2, $t1, $sp
.Lcritical_copy_args_back_loop:
    ld.d    $t3, $t1, 0-16
    ld.d    $t4, $t1, 8-16
    addi.d  $t2, $t2, -16
    st.d    $t3, $t1, FRAME_SIZE_SAVE_REFS_AND_ARGS+0
    st.d    $t4, $t1, FRAME_SIZE_SAVE_REFS_AND_ARGS+8
    addi.d  $t1, $t1, -16
    bnez  $t2, .Lcritical_copy_args_back_loop
.Lcritical_skip_copy_args_back:

    // Remove the frame reservation.
    DECREASE_FRAME (FRAME_SIZE_SAVE_REFS_AND_ARGS + 16)

    // Do the tail call.
    jr    $t0

.Lcritical_deliver_exception:
    CFI_RESTORE_STATE_AND_DEF_CFA $sp, FRAME_SIZE_SAVE_REFS_AND_ARGS + 16
    // If this is called from a method that catches the exception, all callee-save registers need
    // to be saved, so that the exception handling code can read them in case they contain live
    // values later used by that method. This includes callee-save FP registers which are not
    // saved in a SaveRefsAndArgs frame, so we cannot reuse the managed frame we have built above.
    // That's why we checked for exception after restoring registers from that frame.
    // We need to build a SaveAllCalleeSaves frame instead. Args are irrelevant at this
    // point but keep the area allocated for stack args to keep CFA definition simple.
#if FRAME_SIZE_SAVE_ALL_CALLEE_SAVES > FRAME_SIZE_SAVE_REFS_AND_ARGS
#error "Expanding stack frame from kSaveRefsAndArgs to kSaveAllCalleeSaves is not implemented."
#endif
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS - FRAME_SIZE_SAVE_ALL_CALLEE_SAVES

    // Calculate the base address of the managed frame.
    addi.d  $t1, $t1, 16 + FRAME_SIZE_SAVE_REFS_AND_ARGS - FRAME_SIZE_SAVE_ALL_CALLEE_SAVES

    // Spill registers for the SaveAllCalleeSaves frame above the stack args area. Do not update
    // CFI while we hold the frame address in T1 and the values in registers are unchanged.
    // stack slot (0*8)(t1) is for ArtMethod*
    // stack slot (1*8)(t1) is for padding
    // FP callee-saves.
    fst.d   $fs0,  $t1, (8*2)  // f23
    fst.d   $fs1,  $t1, (8*3)  // f24
    fst.d   $fs2,  $t1, (8*4)  // f25
    fst.d   $fs3,  $t1, (8*5)  // f26
    fst.d   $fs4,  $t1, (8*6)  // f27
    fst.d   $fs5,  $t1, (8*7)  // f28
    fst.d   $fs6,  $t1, (8*8)  // f29
    fst.d   $fs7,  $t1, (8*9)  // f30

    // GP callee-saves
    st.d    $fp,  $t1, (8*10)  // x22/s9, frame pointer
    st.d    $s0,  $t1, (8*11)  // x23
    // s1 (x24) is the ART thread register
    st.d    $s2,  $t1, (8*12)  // x25
    st.d    $s3,  $t1, (8*13)  // x26
    st.d    $s4,  $t1, (8*14)  // x27
    st.d    $s5,  $t1, (8*15)  // x28
    st.d    $s6,  $t1, (8*16)  // x29
    st.d    $s7,  $t1, (8*17)  // x30
    st.d    $s8,  $t1, (8*18)  // x31
    // Keep the caller PC for managed stack walk.

    // Move the managed frame address to native callee-save register fp (x8) and update CFI.
    move    $fp, $t1
    CFI_EXPRESSION_BREG  22, 8, (10*8)  // fp/x22: The base register for these CFI expressions.
    CFI_EXPRESSION_BREG  /*FP reg*/ 32 + 23, 8, (8*2)   // fs0/f23
    CFI_EXPRESSION_BREG  /*FP reg*/ 32 + 24, 8, (8*3)   // fs1/f24
    CFI_EXPRESSION_BREG  /*FP reg*/ 32 + 25, 8, (8*4)   // fs2/f25
    CFI_EXPRESSION_BREG  /*FP reg*/ 32 + 26, 8, (8*5)   // fs3/f26
    CFI_EXPRESSION_BREG  /*FP reg*/ 32 + 27, 8, (8*6)   // fs4/f27
    CFI_EXPRESSION_BREG  /*FP reg*/ 32 + 28, 8, (8*7)   // fs5/f28
    CFI_EXPRESSION_BREG  /*FP reg*/ 32 + 29, 8, (8*8)   // fs6/f29
    CFI_EXPRESSION_BREG  /*FP reg*/ 32 + 30, 8, (8*9)   // fs7/f30
    // CFI expression for fp (x22) already emitted above.
    CFI_EXPRESSION_BREG 23, 8, (11*8)  // s2/x18
    CFI_EXPRESSION_BREG 25, 8, (12*8)  // s2/x18
    CFI_EXPRESSION_BREG 26, 8, (13*8)  // s3/x19
    CFI_EXPRESSION_BREG 27, 8, (14*8)  // s4/x20
    CFI_EXPRESSION_BREG 28, 8, (15*8)  // s5/x21
    CFI_EXPRESSION_BREG 29, 8, (16*8)  // s6/x22
    CFI_EXPRESSION_BREG 30, 8, (17*8)  // s7/x23
    CFI_EXPRESSION_BREG 31, 8, (18*8)  // s8/x24
    // The saved return PC for managed stack walk is not necessarily our RA.

    // Save our return PC below the managed frame.
    st.d    $ra, $fp, -__SIZEOF_POINTER__
    CFI_EXPRESSION_BREG 1, 8, -__SIZEOF_POINTER__

    // Store ArtMethod* Runtime::callee_save_methods_[kSaveAllCalleeSaves] to the managed frame.
    LOAD_RUNTIME_INSTANCE $t0
    ld.d    $t0, $t0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET
    st.d    $t0, $fp, 0

    // Place the managed frame SP in Thread::Current()->top_quick_frame.
    st.d    $fp, $xSELF, THREAD_TOP_QUICK_FRAME_OFFSET

    DELIVER_PENDING_EXCEPTION_FRAME_READY
END art_jni_dlsym_lookup_critical_stub