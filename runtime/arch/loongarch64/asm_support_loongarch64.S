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

#ifndef ART_RUNTIME_ARCH_LOONGARCH64_ASM_SUPPORT_LOONGARCH64_S_
#define ART_RUNTIME_ARCH_LOONGARCH64_ASM_SUPPORT_LOONGARCH64_S_

#include "asm_support_loongarch64.h"
#include "interpreter/cfi_asm_support.h"

// Define special registers.

// Register holding Thread::Current().
#define xSELF s1

.macro ENTRY name
    .hidden \name  // Hide this as a global symbol, so we do not incur plt calls.
    .global \name
    .balign 16
\name:
    .cfi_startproc
.endm


.macro END name
    .cfi_endproc
    .size \name, .-\name
.endm


.macro UNDEFINED name
    ENTRY \name
        break 0x5
    END \name
.endm


.macro CFI_REMEMBER_STATE
    .cfi_remember_state
.endm


// The spec is not clear whether the CFA is part of the saved state and tools differ in the
// behaviour, so explicitly set the CFA to avoid any ambiguity.
// The restored CFA state should match the CFA state during CFI_REMEMBER_STATE.
.macro CFI_RESTORE_STATE_AND_DEF_CFA reg, offset
    .cfi_restore_state
    .cfi_def_cfa \reg, \offset
.endm


.macro CFI_EXPRESSION_BREG n, b, offset
   .if (-0x40 <= (\offset)) && ((\offset) < 0x40)
        CFI_EXPRESSION_BREG_1(\n, \b, \offset)
    .elseif (-0x2000 <= (\offset)) && ((\offset) < 0x2000)
        CFI_EXPRESSION_BREG_2(\n, \b, \offset)
    .else
        .error "Unsupported offset"
    .endif
.endm


.macro CFI_DEF_CFA_BREG_PLUS_UCONST reg, offset, size
    .if (((\offset) < -0x40) || ((\offset) >= 0x40))
        .error "Unsupported offset"
    .endif

    .if ((\size) < 0)
        .error "Unsupported size, negative"
    .elseif ((\size) < 0x80)
        CFI_DEF_CFA_BREG_PLUS_UCONST_1_1(\reg, \offset, \size)
    .elseif ((\size) < 0x4000)
        CFI_DEF_CFA_BREG_PLUS_UCONST_1_2(\reg, \offset, \size)
    .else
        .error "Unsupported size, too large"
    .endif
.endm

// Macro to poison (negate) the reference for heap poisoning.
.macro POISON_HEAP_REF rRef
#ifdef USE_HEAP_POISONING
    sub.d \rRef, $zero, \rRef
#endif  // USE_HEAP_POISONING
.endm

// Macro to unpoison (negate) the reference for heap poisoning.
.macro UNPOISON_HEAP_REF rRef
#ifdef USE_HEAP_POISONING
    sub.d \rRef, $zero, \rRef
#endif  // USE_HEAP_POISONING
.endm

.macro INCREASE_FRAME frame_adjustment
    addi.d $sp, $sp, -(\frame_adjustment)
    .cfi_adjust_cfa_offset (\frame_adjustment)
.endm


.macro DECREASE_FRAME frame_adjustment
    addi.d $sp, $sp, (\frame_adjustment)
    .cfi_adjust_cfa_offset -(\frame_adjustment)
.endm


.macro SAVE_GPR_BASE base, reg, offset
    st.d \reg, \base, \offset
    .cfi_rel_offset \reg, (\offset)
.endm


.macro SAVE_GPR reg, offset
    SAVE_GPR_BASE $sp, \reg, \offset
.endm


.macro RESTORE_GPR_BASE base, reg, offset
    ld.d \reg, \base, \offset
    .cfi_restore \reg
.endm


.macro RESTORE_GPR reg, offset
    RESTORE_GPR_BASE $sp, \reg, \offset
.endm

.macro RESTORE_GPR_NE skip, reg, offset
    .ifnc \skip, \reg
    RESTORE_GPR_BASE $sp, \reg, \offset
    .endif
.endm

.macro SAVE_FPR reg, offset
    fst.d \reg, $sp, \offset
.endm


.macro RESTORE_FPR reg, offset
    fld.d \reg, $sp, \offset
.endm


.macro LOAD_RUNTIME_INSTANCE reg
#if __has_feature(hwaddress_sanitizer)
#error "ART does not support HWASAN on LOONGARCH yet"
#else
    la \reg, _ZN3art7Runtime9instance_E
#endif
    ld.d \reg, \reg, 0
.endm


// We need to save callee-save GPRs on the stack as they may contain references, and must be
// visible to GC (unless the called method holds mutator lock and prevents GC from happening).
// FP callee-saves shall be preserved by whatever runtime function we call, so they do not need
// to be saved.
.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
#if (FRAME_SIZE_SAVE_REFS_AND_ARGS != 8*(1 + 8 + 7 + 9 + 1))
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(LOONGARCH64) size not as expected."
#endif
    // stack slot (0*8)(sp) is for ArtMethod*

    SAVE_FPR $fa0, (1*8)
    SAVE_FPR $fa1, (2*8)
    SAVE_FPR $fa2, (3*8)
    SAVE_FPR $fa3, (4*8)
    SAVE_FPR $fa4, (5*8)
    SAVE_FPR $fa5, (6*8)
    SAVE_FPR $fa6, (7*8)
    SAVE_FPR $fa7, (8*8)

    // a0 (x10) is the method pointer
    SAVE_GPR $a1,  (9*8)   // x11
    SAVE_GPR $a2,  (10*8)  // x12
    SAVE_GPR $a3,  (11*8)  // x13
    SAVE_GPR $a4,  (12*8)  // x14
    SAVE_GPR $a5,  (13*8)  // x15
    SAVE_GPR $a6,  (14*8)  // x16
    SAVE_GPR $a7,  (15*8)  // x17

    SAVE_GPR $fp,  (16*8)  // x22, s9, frame pointer

    SAVE_GPR $s0,  (17*8)  // x23
    // s1 (x24) is the ART thread register
    SAVE_GPR $s2,  (18*8)  // x25
    SAVE_GPR $s3,  (19*8)  // x26
    SAVE_GPR $s4,  (20*8)  // x27
    SAVE_GPR $s5,  (21*8)  // x28
    SAVE_GPR $s6,  (22*8)  // x29
    SAVE_GPR $s7,  (23*8)  // x30
    SAVE_GPR $s8,  (24*8)  // x31

    SAVE_GPR $ra,  (25*8)  // x1, return address
.endm


.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    // stack slot (0*8)(sp) is for ArtMethod*

    RESTORE_FPR $fa0, (1*8)
    RESTORE_FPR $fa1, (2*8)
    RESTORE_FPR $fa2, (3*8)
    RESTORE_FPR $fa3, (4*8)
    RESTORE_FPR $fa4, (5*8)
    RESTORE_FPR $fa5, (6*8)
    RESTORE_FPR $fa6, (7*8)
    RESTORE_FPR $fa7, (8*8)

    // a0 (x10) is the method pointer
    RESTORE_GPR $a1,  (9*8)   // x11
    RESTORE_GPR $a2,  (10*8)  // x12
    RESTORE_GPR $a3,  (11*8)  // x13
    RESTORE_GPR $a4,  (12*8)  // x14
    RESTORE_GPR $a5,  (13*8)  // x15
    RESTORE_GPR $a6,  (14*8)  // x16
    RESTORE_GPR $a7,  (15*8)  // x17

    RESTORE_GPR $fp,  (16*8)  // x22, s9, frame pointer

    RESTORE_GPR $s0,  (17*8)  // x23
    // s1 (x24) is the ART thread register
    RESTORE_GPR $s2,  (18*8)  // x25
    RESTORE_GPR $s3,  (19*8)  // x26
    RESTORE_GPR $s4,  (20*8)  // x27
    RESTORE_GPR $s5,  (21*8)  // x28
    RESTORE_GPR $s6,  (22*8)  // x29
    RESTORE_GPR $s7,  (23*8)  // x30
    RESTORE_GPR $s8,  (24*8)  // x31

    RESTORE_GPR $ra,  (25*8)  // x1, return address
.endm


.macro SETUP_CALLEE_SAVE_FRAME_COMMON_INTERNAL reg
    // ArtMethod* is in reg, store it at the bottom of the stack.
    st.d \reg, $sp, 0

    // Place sp in Thread::Current()->top_quick_frame.
    st.d $sp, $xSELF, THREAD_TOP_QUICK_FRAME_OFFSET
.endm


.macro SETUP_CALLEE_SAVE_FRAME_COMMON tmpreg, runtime_method_offset
    // art::Runtime* tmpreg = art::Runtime::instance_;
    LOAD_RUNTIME_INSTANCE \tmpreg

    // ArtMethod* tmpreg = Runtime::instance_->callee_save_methods_[<callee-save-frame-type>];
    ld.d  \tmpreg, \tmpreg, \runtime_method_offset

    SETUP_CALLEE_SAVE_FRAME_COMMON_INTERNAL \tmpreg
.endm


.macro SETUP_SAVE_REFS_AND_ARGS_FRAME
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    SETUP_CALLEE_SAVE_FRAME_COMMON $t0, RUNTIME_SAVE_REFS_AND_ARGS_METHOD_OFFSET
.endm


.macro SETUP_SAVE_REFS_AND_ARGS_FRAME_WITH_METHOD_IN_A0
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS
    SETUP_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    SETUP_CALLEE_SAVE_FRAME_COMMON_INTERNAL $a0
.endm


.macro RESTORE_SAVE_REFS_AND_ARGS_FRAME
    RESTORE_SAVE_REFS_AND_ARGS_FRAME_INTERNAL
    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_AND_ARGS
.endm


.macro SAVE_ALL_CALLEE_SAVES
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 8*(1 + 1 + 8 + 9 + 1))
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(LOONGARCH64) size not as expected."
#endif
    // stack slot (0*8)(sp) is for ArtMethod*
    // stack slot (1*8)(sp) is for padding

    // FP callee-saves.
    SAVE_FPR $fs0,  (8*2)  // f23
    SAVE_FPR $fs1,  (8*3)  // f24
    SAVE_FPR $fs2,  (8*4)  // f25
    SAVE_FPR $fs3,  (8*5)  // f26
    SAVE_FPR $fs4,  (8*6)  // f27
    SAVE_FPR $fs5,  (8*7)  // f28
    SAVE_FPR $fs6,  (8*8)  // f29
    SAVE_FPR $fs7,  (8*9)  // f30

    // GP callee-saves
    SAVE_GPR $fp,  (8*10)  // x22/s9, frame pointer
    SAVE_GPR $s0,  (8*11)  // x23
    // s1 (x24) is the ART thread register
    SAVE_GPR $s2,  (8*12)  // x25
    SAVE_GPR $s3,  (8*13)  // x26
    SAVE_GPR $s4,  (8*14)  // x27
    SAVE_GPR $s5,  (8*15)  // x28
    SAVE_GPR $s6,  (8*16)  // x29
    SAVE_GPR $s7,  (8*17)  // x30
    SAVE_GPR $s8,  (8*18)  // x31

    SAVE_GPR $ra,  (8*19)  // x1, return address
.endm


.macro RESTORE_ALL_CALLEE_SAVES
#if (FRAME_SIZE_SAVE_ALL_CALLEE_SAVES != 8*(1 + 1 + 8 + 9 + 1))
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(LOONGARCH64) size not as expected."
#endif
    // stack slot (0*8)(sp) is for ArtMethod*
    // stack slot (1*8)(sp) is for padding

    // FP callee-saves.
    RESTORE_FPR $fs0,  (8*2)  // f23
    RESTORE_FPR $fs1,  (8*3)  // f24
    RESTORE_FPR $fs2,  (8*4)  // f25
    RESTORE_FPR $fs3,  (8*5)  // f26
    RESTORE_FPR $fs4,  (8*6)  // f27
    RESTORE_FPR $fs5,  (8*7)  // f28
    RESTORE_FPR $fs6,  (8*8)  // f29
    RESTORE_FPR $fs7,  (8*9)  // f30

    // GP callee-saves
    RESTORE_GPR $fp,  (8*10)  // x22/s9, frame pointer
    RESTORE_GPR $s0,  (8*11)  // x23
    // s1 (x24) is the ART thread register
    RESTORE_GPR $s2,  (8*12)  // x25
    RESTORE_GPR $s3,  (8*13)  // x26
    RESTORE_GPR $s4,  (8*14)  // x27
    RESTORE_GPR $s5,  (8*15)  // x28
    RESTORE_GPR $s6,  (8*16)  // x29
    RESTORE_GPR $s7,  (8*17)  // x30
    RESTORE_GPR $s8,  (8*18)  // x31

    RESTORE_GPR $ra,  (8*19)  // x1, return address
.endm


.macro SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP_SKIP_RA \
        runtime_method_offset = RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET
#if (FRAME_SIZE_SAVE_EVERYTHING != 8*(2 + 32 + 28))
#error "FRAME_SIZE_SAVE_EVERYTHING(LOONGARCH64) size not as expected."
#endif
    // stack slot (8*0)(sp) is for ArtMethod*
    // stack slot (8*1)(sp) is for padding

    // 32 slots for FPRs
#define SAVE_EVERYTHING_FRAME_OFFSET_FA0 (8*2)
    SAVE_FPR $fa0,  8*2   // f0, its offset must equal SAVE_EVERYTHING_FRAME_OFFSET_FA0
    SAVE_FPR $fa1,  8*3   // f1
    SAVE_FPR $fa2,  8*4   // f2
    SAVE_FPR $fa3,  8*5   // f3
    SAVE_FPR $fa4,  8*6   // f4
    SAVE_FPR $fa5,  8*7   // f5
    SAVE_FPR $fa6,  8*8   // f6
    SAVE_FPR $fa7,  8*9   // f7
    SAVE_FPR $ft0,  8*10  // f8
    SAVE_FPR $ft1,  8*11  // f9
    SAVE_FPR $ft2,  8*12  // f10
    SAVE_FPR $ft3,  8*13  // f11
    SAVE_FPR $ft4,  8*14  // f12
    SAVE_FPR $ft5,  8*15  // f13
    SAVE_FPR $ft6,  8*16  // f14
    SAVE_FPR $ft7,  8*17  // f15
    SAVE_FPR $ft8,  8*18  // f16
    SAVE_FPR $ft9,  8*19  // f17
    SAVE_FPR $ft10, 8*20  // f18
    SAVE_FPR $ft11, 8*21  // f19
    SAVE_FPR $ft12, 8*22  // f20
    SAVE_FPR $ft13, 8*23  // f21
    SAVE_FPR $ft14, 8*24  // f22
    SAVE_FPR $ft15, 8*25  // f23
    SAVE_FPR $fs0,  8*26  // f24
    SAVE_FPR $fs1,  8*27  // f25
    SAVE_FPR $fs2,  8*28  // f26
    SAVE_FPR $fs3,  8*29  // f27
    SAVE_FPR $fs4,  8*30  // f28
    SAVE_FPR $fs5,  8*31  // f29
    SAVE_FPR $fs6,  8*32  // f30
    SAVE_FPR $fs7,  8*33  // f31

    // 27 slots for GPRs (excluded: zero/x0, sp/x2, gp/x3, tp/x4, s1/x9 -- the ART thread register)
#define SAVE_EVERYTHING_FRAME_OFFSET_A0 (8*34)
    SAVE_GPR $a0,  8*34  // x4, its offset must equal SAVE_EVERYTHING_FRAME_OFFSET_A0
    SAVE_GPR $a1,  8*35  // x5
    SAVE_GPR $a2,  8*36  // x6
    SAVE_GPR $a3,  8*37  // x7
    SAVE_GPR $a4,  8*38  // x8
    SAVE_GPR $a5,  8*39  // x9
    SAVE_GPR $a6,  8*40  // x10
    SAVE_GPR $a7,  8*41  // x11
    SAVE_GPR $t0,  8*42  // x12
    SAVE_GPR $t1,  8*43  // x13
    SAVE_GPR $t2,  8*44  // x14
    SAVE_GPR $t3,  8*45  // x15
    SAVE_GPR $t4,  8*46  // x16
    SAVE_GPR $t5,  8*47  // x17
    SAVE_GPR $t6,  8*48  // x18
    SAVE_GPR $t7,  8*49  // x19
    SAVE_GPR $t8,  8*50  // x20
    // Skip reserved register x21
    SAVE_GPR $fp,  8*51  // x22
    SAVE_GPR $s0,  8*52  // x23
    SAVE_GPR $s1,  8*53  // x24
    SAVE_GPR $s2,  8*54  // x25
    SAVE_GPR $s3,  8*55  // x26
    SAVE_GPR $s4,  8*56  // x27
    SAVE_GPR $s5,  8*57  // x28
    SAVE_GPR $s6,  8*58  // x29
    SAVE_GPR $s7,  8*59  // x30
    SAVE_GPR $s8,  8*60  // x31

    // RA already saved by the user of this macro.

    SETUP_CALLEE_SAVE_FRAME_COMMON $t0, \runtime_method_offset
.endm


.macro SETUP_SAVE_EVERYTHING_FRAME runtime_method_offset = RUNTIME_SAVE_EVERYTHING_METHOD_OFFSET
#if (FRAME_SIZE_SAVE_EVERYTHING != 8*(2 + 32 + 28))
#error "FRAME_SIZE_SAVE_EVERYTHING(LOONGARCH64) size not as expected."
#endif
    INCREASE_FRAME FRAME_SIZE_SAVE_EVERYTHING
    SAVE_GPR $ra,  8*61  // x1, return address
    SETUP_SAVE_EVERYTHING_FRAME_DECREMENTED_SP_SKIP_RA \runtime_method_offset
.endm


.macro RESTORE_SAVE_EVERYTHING_FRAME load_a0 = 1
    // stack slot (8*0)(sp) is for ArtMethod*
    // stack slot (8*1)(sp) is for padding

    // 32 slots for FPRs
#if SAVE_EVERYTHING_FRAME_OFFSET_FA0 != (8*2)
#error "unexpected SAVE_EVERYTHING_FRAME_OFFSET_FA0"
#endif
    RESTORE_FPR $fa0,  8*2   // f0, its offset must equal SAVE_EVERYTHING_FRAME_OFFSET_FA0
    RESTORE_FPR $fa1,  8*3   // f1
    RESTORE_FPR $fa2,  8*4   // f2
    RESTORE_FPR $fa3,  8*5   // f3
    RESTORE_FPR $fa4,  8*6   // f4
    RESTORE_FPR $fa5,  8*7   // f5
    RESTORE_FPR $fa6,  8*8   // f6
    RESTORE_FPR $fa7,  8*9   // f7
    RESTORE_FPR $ft0,  8*10  // f8
    RESTORE_FPR $ft1,  8*11  // f9
    RESTORE_FPR $ft2,  8*12  // f10
    RESTORE_FPR $ft3,  8*13  // f11
    RESTORE_FPR $ft4,  8*14  // f12
    RESTORE_FPR $ft5,  8*15  // f13
    RESTORE_FPR $ft6,  8*16  // f14
    RESTORE_FPR $ft7,  8*17  // f15
    RESTORE_FPR $ft8,  8*18  // f16
    RESTORE_FPR $ft9,  8*19  // f17
    RESTORE_FPR $ft10, 8*20  // f18
    RESTORE_FPR $ft11, 8*21  // f19
    RESTORE_FPR $ft12, 8*22  // f20
    RESTORE_FPR $ft13, 8*23  // f21
    RESTORE_FPR $ft14, 8*24  // f22
    RESTORE_FPR $ft15, 8*25  // f23
    RESTORE_FPR $fs0,  8*26  // f24
    RESTORE_FPR $fs1,  8*27  // f25
    RESTORE_FPR $fs2,  8*28  // f26
    RESTORE_FPR $fs3,  8*29  // f27
    RESTORE_FPR $fs4,  8*30  // f28
    RESTORE_FPR $fs5,  8*31  // f29
    RESTORE_FPR $fs6,  8*32  // f30
    RESTORE_FPR $fs7,  8*33  // f31

    // 26 slots for GPRs (excluded: zero/x0, sp/x2, gp/x3, tp/x4, s1/x9 -- the ART thread register)
#if SAVE_EVERYTHING_FRAME_OFFSET_A0 != (8*34)
#error "unexpected SAVE_EVERYTHING_FRAME_OFFSET_A0"
#endif
    .if \load_a0
    RESTORE_GPR $a0,  8*34  // x4, offset must equal SAVE_EVERYTHING_FRAME_OFFSET_A0
    .endif
    RESTORE_GPR $a1,  8*35  // x5
    RESTORE_GPR $a2,  8*36  // x6
    RESTORE_GPR $a3,  8*37  // x7
    RESTORE_GPR $a4,  8*38  // x8
    RESTORE_GPR $a5,  8*39  // x9
    RESTORE_GPR $a6,  8*40  // x10
    RESTORE_GPR $a7,  8*41  // x11
    RESTORE_GPR $t0,  8*42  // x12
    RESTORE_GPR $t1,  8*43  // x13
    RESTORE_GPR $t2,  8*44  // x14
    RESTORE_GPR $t3,  8*45  // x15
    RESTORE_GPR $t4,  8*46  // x16
    RESTORE_GPR $t5,  8*47  // x17
    RESTORE_GPR $t6,  8*48  // x18
    RESTORE_GPR $t7,  8*49  // x19
    RESTORE_GPR $t8,  8*50  // x20
     // Skip reserved register x21
    RESTORE_GPR $fp,  8*51  // x22
    RESTORE_GPR $s0,  8*52  // x23
    RESTORE_GPR $s1,  8*53  // x24
    RESTORE_GPR $s2,  8*54  // x25
    RESTORE_GPR $s3,  8*55  // x26
    RESTORE_GPR $s4,  8*56  // x27
    RESTORE_GPR $s5,  8*57  // x28
    RESTORE_GPR $s6,  8*58  // x29
    RESTORE_GPR $s7,  8*59  // x30
    RESTORE_GPR $s8,  8*60  // x31

    RESTORE_GPR $ra,  (8*61)  // x1, return address

    DECREASE_FRAME FRAME_SIZE_SAVE_EVERYTHING
.endm

// This macro must be called at the end of functions implementing
// entrypoints that possibly (directly or indirectly) perform a
// suspend check (before they return).
.macro REFRESH_MARKING_REGISTER
#if defined(USE_READ_BARRIER) && defined(USE_BAKER_READ_BARRIER)
    // TODO(loongarch64): Define marking register to avoid this load.
    ld.w $t6, $xSELF, THREAD_IS_GC_MARKING_OFFSET
#endif
.endm

// For compatibility with Runtime::CreateCalleeSaveMethod(kSaveRefsOnly).
.macro SETUP_SAVE_REFS_ONLY_FRAME
    INCREASE_FRAME FRAME_SIZE_SAVE_REFS_ONLY

    // stack slot (8*0)($sp) is for ArtMethod*
    // stack slot (8*1)($sp) is for padding
    SAVE_GPR $fp,  8*2   // fp
    SAVE_GPR $s0,  8*3   // x23
    SAVE_GPR $s2,  8*4   // x25
    SAVE_GPR $s3,  8*5   // x26
    SAVE_GPR $s4,  8*6   // x27
    SAVE_GPR $s5,  8*7   // x28
    SAVE_GPR $s6,  8*8   // x29
    SAVE_GPR $s7,  8*9   // x30
    SAVE_GPR $s8,  8*10   // x31
    SAVE_GPR $ra,  8*11  // ra must be in stack bottom


    SETUP_CALLEE_SAVE_FRAME_COMMON $t0, RUNTIME_SAVE_REFS_ONLY_METHOD_OFFSET
.endm

.macro RESTORE_SAVE_REFS_ONLY_FRAME
    // stack slot (8*0)($sp) is for ArtMethod*
    // stack slot (8*1)($sp) is for padding
    RESTORE_GPR $fp,  8*2   //
    RESTORE_GPR $s0,  8*3   // x23
    RESTORE_GPR $s2,  8*4   // x25
    RESTORE_GPR $s3,  8*5   // x26
    RESTORE_GPR $s4,  8*6   // x27
    RESTORE_GPR $s5,  8*7   // x28
    RESTORE_GPR $s6,  8*8   // x29
    RESTORE_GPR $s7,  8*9   // x30
    RESTORE_GPR $s8,  8*10  // x31
    RESTORE_GPR $ra,  8*11  // x1 make surn in stack bottom

    DECREASE_FRAME FRAME_SIZE_SAVE_REFS_ONLY
.endm

.macro SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    INCREASE_FRAME FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
    SAVE_ALL_CALLEE_SAVES
    SETUP_CALLEE_SAVE_FRAME_COMMON $t0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET
.endm


.macro SETUP_NTERP_SAVE_CALLEE_SAVES
#if (NTERP_SIZE_SAVE_CALLEE_SAVES != 8*(8 + 9 + 1))
#error "NTERP_SIZE_SAVE_CALLEE_SAVES(LOONGARCH64) size not as expected."
#endif
    // FP callee-saves.
    SAVE_FPR $fs0,  (8*0)   // f24
    SAVE_FPR $fs1,  (8*1)   // f25
    SAVE_FPR $fs2,  (8*2)   // f26
    SAVE_FPR $fs3,  (8*3)   // f27
    SAVE_FPR $fs4,  (8*4)   // f28
    SAVE_FPR $fs5,  (8*5)   // f28
    SAVE_FPR $fs6,  (8*6)   // f30
    SAVE_FPR $fs7,  (8*7)   // f31

    // GP callee-saves
    SAVE_GPR $s9,  (8*8)  // r22/fp, frame pointer
    SAVE_GPR $s0,  (8*9)  // r23
    // s1 (r24) is the ART thread register
    SAVE_GPR $s2,  (8*10)  // r25
    SAVE_GPR $s3,  (8*11)  // r26
    SAVE_GPR $s4,  (8*12)  // r27
    SAVE_GPR $s5,  (8*13)  // r28
    SAVE_GPR $s6,  (8*14)  // r29
    SAVE_GPR $s7,  (8*15)  // r30
    SAVE_GPR $s8,  (8*16)  // r31

    SAVE_GPR $ra,  (8*17)  // r1, return address
.endm


.macro RESTORE_NTERP_SAVE_CALLEE_SAVES
#if (NTERP_SIZE_SAVE_CALLEE_SAVES != 8*(8 + 9 + 1))
#error "NTERP_SIZE_SAVE_CALLEE_SAVES(LOONGARCH64) size not as expected."
#endif
    // FP callee-saves.
    RESTORE_FPR $fs0,  (8*0)   // f24
    RESTORE_FPR $fs1,  (8*1)   // f25
    RESTORE_FPR $fs2,  (8*2)   // f26
    RESTORE_FPR $fs3,  (8*3)   // f27
    RESTORE_FPR $fs4,  (8*4)   // f28
    RESTORE_FPR $fs5,  (8*5)   // f28
    RESTORE_FPR $fs6,  (8*6)   // f30
    RESTORE_FPR $fs7,  (8*7)   // f31

    // GP callee-saves
    RESTORE_GPR $s9,  (8*8)  // r22/fp, frame pointer
    RESTORE_GPR $s0,  (8*9)  // r23
    // s1 (r24) is the ART thread register
    RESTORE_GPR $s2,  (8*10)  // r25
    RESTORE_GPR $s3,  (8*11)  // r26
    RESTORE_GPR $s4,  (8*12)  // r27
    RESTORE_GPR $s5,  (8*13)  // r28
    RESTORE_GPR $s6,  (8*14)  // r29
    RESTORE_GPR $s7,  (8*15)  // r30
    RESTORE_GPR $s8,  (8*16)  // r31

    RESTORE_GPR $ra,  (8*17)  // r1, return address
 .endm

// CFI note. This macro is used where the CFA rule is a dwarf expression, so adjustment of SP does
// not affect CFA computation. We also elide CFI descriptors for the argument registers, because
// they can be recovered from the stack in a debugging scenario.
.macro SPILL_ALL_ARGUMENTS
#if (FRAME_SIZE_SAVE_ARGS_ONLY != 128)
#error "FRAME_SIZE_SAVE_ARGS_ONLY(loongarch64) not as expected."
#endif
    addi.d $sp, $sp, -FRAME_SIZE_SAVE_ARGS_ONLY
    st.d $a0, $sp, (8*0)
    st.d $a1, $sp, (8*1)
    st.d $a2, $sp, (8*2)
    st.d $a3, $sp, (8*3)
    st.d $a4, $sp, (8*4)
    st.d $a5, $sp, (8*5)
    st.d $a6, $sp, (8*6)
    st.d $a7, $sp, (8*7)
    fst.d $fa0, $sp, (8*8)
    fst.d $fa1, $sp, (8*9)
    fst.d $fa2, $sp, (8*10)
    fst.d $fa3, $sp, (8*11)
    fst.d $fa4, $sp, (8*12)
    fst.d $fa5, $sp, (8*13)
    fst.d $fa6, $sp, (8*14)
    fst.d $fa7, $sp, (8*15)
.endm


.macro RESTORE_ALL_ARGUMENTS
    ld.d $a0, $sp, 8*0
    ld.d $a1, $sp, 8*1
    ld.d $a2, $sp, 8*2
    ld.d $a3, $sp, 8*3
    ld.d $a4, $sp, 8*4
    ld.d $a5, $sp, 8*5
    ld.d $a6, $sp, 8*6
    ld.d $a7, $sp, 8*7
    fld.d $fa0, $sp, 8*8
    fld.d $fa1, $sp, 8*9
    fld.d $fa2, $sp, 8*10
    fld.d $fa3, $sp, 8*11
    fld.d $fa4, $sp, 8*12
    fld.d $fa5, $sp, 8*13
    fld.d $fa6, $sp, 8*14
    fld.d $fa7, $sp, 8*15
    addi.d $sp, $sp, FRAME_SIZE_SAVE_ARGS_ONLY
.endm


// Macro that calls through to artDeliverPendingExceptionFromCode, where the pending exception is
// Thread::Current()->exception_ when the runtime method frame is ready.
.macro DELIVER_PENDING_EXCEPTION_FRAME_READY
    move $a0, $xSELF
    bl artDeliverPendingExceptionFromCode  // Point of no return.
    break 0x5                                  // Unreachable.
.endm


// Macro that calls through to artDeliverPendingExceptionFromCode, where the pending exception is
// Thread::Current()->exception_.
.macro DELIVER_PENDING_EXCEPTION
    SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    DELIVER_PENDING_EXCEPTION_FRAME_READY
.endm


.macro RETURN_OR_DELIVER_PENDING_EXCEPTION_REG reg
    ld.d \reg, $xSELF, THREAD_EXCEPTION_OFFSET
    bnez \reg, 1f
    jirl $zero, $ra, 0
1:
    DELIVER_PENDING_EXCEPTION
.endm


// Macro to emit a single LU12I.W to load the given value while checking that the low 12 bits are zero.
.macro LU12I_VALUE reg, value
    .if (\value & 0xfff) != 0
    .error "Cannot use lu12i.w to materialize a value with some of the low 12 bits set."
    .endif
    lu12i.w \reg, (\value) >> 12
.endm


// Locking is needed for both managed code and JNI stubs.
.macro LOCK_OBJECT_FAST_PATH obj, slow_lock, can_be_null
    // Use scratch registers T1-T6 as temporaries.
    // Note: T0 is used as the argument register for `art_jni_lock_object` and passed as `obj`.
    ld.w      $t2, $xSELF, THREAD_ID_OFFSET
    .if \can_be_null
        beqz    \obj, \slow_lock
    .endif
    addi.d    $t1, \obj, MIRROR_OBJECT_LOCK_WORD_OFFSET  // Exclusive load/store has no offset.
1:
    // Note: The LR/SC sequence must be at most 16 instructions, so we cannot have the
    // recursive locking in a slow-path as on other architectures.
    ll.w $t3, $t1, 0                 // Acquire needed only in most common case.
    LU12I_VALUE $t5, LOCK_WORD_GC_STATE_MASK_SHIFTED  // Prepare mask for testing non-gc bits.
    xor     $t4, $t3, $t2             // Prepare the value to store if unlocked
                                      //   (thread id, count of 0 and preserved read barrier bits),
                                      // or prepare to compare thread id for recursive lock check
                                      //   (lock_word.ThreadId() ^ self->ThreadId()).
    or      $t6, $t5, $t3             // Test the non-gc bits.
    beq     $t6, $t5, 2f              // Check if unlocked.
                                      // Check lock word state and thread id together,
    LU12I_VALUE \
        $t5, 0xffffffff ^ (LOCK_WORD_STATE_MASK_SHIFTED | LOCK_WORD_THIN_LOCK_OWNER_MASK_SHIFTED)
    or      $t6, $t5, $t4
    bne     $t6, $t5, \slow_lock
    LU12I_VALUE $t4, LOCK_WORD_THIN_LOCK_COUNT_ONE  // Increment the recursive lock count.
    add.w    $t4, $t3, $t4
    LU12I_VALUE $t5, LOCK_WORD_THIN_LOCK_COUNT_MASK_SHIFTED  // Test the new thin lock count.
    and     $t5, $t4, $t5
    beqz    $t5, \slow_lock            // Zero as the new count indicates overflow, go slow path.
2:
    // Store the prepared value:
    //   - if unlocked, original lock word plus thread id,
    //   - if already locked, original lock word plus incremented lock count.
    sc.w $t4, $t1, 0
    beqz    $t4, 1b                    // If the store failed, retry.
    ret
.endm


// Unlocking is needed for both managed code and JNI stubs.
.macro UNLOCK_OBJECT_FAST_PATH obj, slow_unlock, can_be_null
    // Use scratch registers T1-T6 as temporaries.
    // Note: T0 is used as the argument register for `art_jni_unlock_object` and passed as `obj`.
    ld.w      $t2, $xSELF, THREAD_ID_OFFSET
    .if \can_be_null
        beqz    \obj, \slow_unlock
    .endif
    addi.d    $t1, \obj, MIRROR_OBJECT_LOCK_WORD_OFFSET  // Exclusive load/store has no offset.
1:
    // Note: The LR/SC sequence must be at most 16 instructions, so we cannot have the
    // recursive unlocking in a slow-path as on other architectures.
    ll.w    $t3, $t1, 0
    LU12I_VALUE $t5, LOCK_WORD_GC_STATE_MASK_SHIFTED  // Prepare mask for testing non-gc bits.
    xor     $t4, $t3, $t2             // Prepare the value to store if simply locked
                                      //   (mostly 0s, and preserved read barrier bits),
                                      // or prepare to compare thread id for recursive lock check
                                      //   (lock_word.ThreadId() ^ self->ThreadId()).
    or      $t6, $t5, $t4             // Test the non-gc bits.
    beq     $t6, $t5, 2f              // Simply locked by this thread?
                                      // Check lock word state and thread id together.
    LU12I_VALUE \
        $t5, 0xffffffff ^ (LOCK_WORD_STATE_MASK_SHIFTED | LOCK_WORD_THIN_LOCK_OWNER_MASK_SHIFTED)
    or      $t6, $t5, $t4
    bne     $t6, $t5, \slow_unlock
    LU12I_VALUE $t4, LOCK_WORD_THIN_LOCK_COUNT_ONE  // Decrement the recursive lock count.
    sub.w    $t4, $t3, $t4
2:
    // Store the prepared value:
    //   - if simply locked, original lock word with removed thread id,
    //   - if recursively locked, original lock word plus decremented lock count.
    sc.w $t4, $t1, 0                 // Need to use atomic instructions for read barrier.
    beqz    $t4, 1b                  // If the store failed, retry.
    ret
.endm


// Macros to branch based on the value of a specific bit.
.macro BRANCH_IF_BIT_CLEAR tmp, reg, bit, dest
    slli.d  \tmp, \reg, (63 - \bit) // tested bit => sign bit
    bge     \tmp, $zero, \dest
.endm


.macro BRANCH_IF_BIT_SET tmp, reg, bit, dest
    slli.d  \tmp, \reg, (63 - \bit) // tested bit => sign bit
    bltz    \tmp, \dest
.endm

#endif  // ART_RUNTIME_ARCH_LOONGARCH64_ASM_SUPPORT_LOONGARCH64_S_
