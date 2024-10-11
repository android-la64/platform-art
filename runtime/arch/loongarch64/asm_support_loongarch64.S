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
    // ART-compiled functions have OatQuickMethodHeader but assembly functions do not.
    // Prefix the assembly code with 0xFFs, which means there is no method header.
    .byte 0xFF, 0xFF, 0xFF, 0xFF
    // Cache alignment for function entry.
    // Use 0xFF as the last 4 bytes of alignment stand for OatQuickMethodHeader.
    .balign 16, 0xFF
\name:
    .cfi_startproc
.endm


.macro END name
    .cfi_endproc
.endm


.macro UNDEFINED name
    ENTRY \name
        //unimp
        break
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


.macro SAVE_FPR reg, offset
    fst.d \reg, $sp, \offset
    .cfi_rel_offset \reg, (\offset)
.endm


.macro RESTORE_FPR reg, offset
    fld.d \reg, $sp, \offset
    .cfi_restore \reg
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
#error "FRAME_SIZE_SAVE_REFS_AND_ARGS(RISCV64) size not as expected."
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
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(RISCV64) size not as expected."
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
#error "FRAME_SIZE_SAVE_ALL_CALLEE_SAVES(RISCV64) size not as expected."
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


.macro SETUP_SAVE_ALL_CALLEE_SAVES_FRAME
    INCREASE_FRAME FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
    SAVE_ALL_CALLEE_SAVES
    SETUP_CALLEE_SAVE_FRAME_COMMON $t0, RUNTIME_SAVE_ALL_CALLEE_SAVES_METHOD_OFFSET
.endm


// Macro that calls through to artDeliverPendingExceptionFromCode, where the pending exception is
// Thread::Current()->exception_ when the runtime method frame is ready.
.macro DELIVER_PENDING_EXCEPTION_FRAME_READY
    move $a0, $xSELF
    bl artDeliverPendingExceptionFromCode  // Point of no return.
    break                                  // Unreachable.
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


#endif  // ART_RUNTIME_ARCH_LOONGARCH64_ASM_SUPPORT_LOONGARCH64_S_
