%def header():
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

/*
 * This is a #include, not a %include, because we want the C pre-processor
 * to expand the macros into assembler assignment statements.
 */
#include "asm_support.h"
#include "arch/loongarch64/asm_support_loongarch64.S"

/**
 * LOONGARCH 64 ABI general notes
 *
 * References
 * - https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-cc.adoc
 * - runtime/arch/riscv64/registers_riscv64.h
 *
 * 32 general purposes registers
 * - fixed purpose: zero, ra, sp, gp, tp, s1
 *     gp/scs: shadow call stack - do not clobber!
 *     s1/tr: ART thread register - do not clobber!
 * - temporaries: t0-t8
 * - arguments: a0-a7
 * - return: a0, a1
 * - callee saved: ra, s9/fp, s0, s2-s8
 *     s0 is flexible, available to use as a frame pointer if needed.
 *
 * 32 floating point registers
 * - temporaries: ft0-ft15
 * - arguments: fa0-fa7
 * - return: fa0, fa1
 * - callee saved: fs0-fs7
 */

// Android references
//   Bytecodes: https://source.android.com/docs/core/runtime/dalvik-bytecode
//   Instruction formats: https://source.android.com/docs/core/runtime/instruction-formats

// Fixed register usages in Nterp.
//    nickname  ABI    reg   purpose
#define zero       $$r0
#define ra       $$r1
#define tp       $$r2
#define sp       $$r3
#define a0       $$r4
#define a1       $$r5
#define a2       $$r6
#define a3       $$r7
#define a4       $$r8
#define a5       $$r9
#define a6       $$r10
#define a7       $$r11
#define t0       $$r12
#define t1       $$r13
#define t2       $$r14
#define t3       $$r15
#define t4       $$r16
#define t5       $$r17
#define t6       $$r18
#define t7       $$r19
#define t8       $$r20
#define u0       $$r21
#define fp       $$r22
#define s0       $$r23
#define s1       $$r24
#define s2       $$r25
#define s3       $$r26
#define s4       $$r27
#define s5       $$r28
#define s6       $$r29
#define s7       $$r30
#define s8       $$r31

#define fa0      $$f0
#define fa1      $$f1
#define fa2      $$f2
#define fa3      $$f3
#define fa4      $$f4
#define fa5      $$f5
#define fa6      $$f6
#define fa7      $$f7
#define ft0      $$f8
#define ft1      $$f9
#define ft2      $$f10
#define ft3      $$f11
#define ft4      $$f12
#define ft5      $$f13
#define ft6      $$f14
#define ft7      $$f15
#define ft8      $$f16
#define ft9      $$f17
#define ft10     $$f18
#define ft11     $$f19
#define ft12     $$f20
#define ft13     $$f21
#define ft14     $$f22
#define ft15     $$f23
#define fs0      $$f24
#define fs1      $$f25
#define fs2      $$f26
#define fs3      $$f27
#define fs4      $$f28
#define fs5      $$f29
#define fs6      $$f30
#define fs7      $$f31
#define xSELF    s1  // r24,   Thread* self pointer
#define xFP      s2  // r25,  interpreted frame pointer: to access locals and args
#define xPC      s3  // r26,  interpreted program counter: to fetch instructions
#define xINST    s4  // r27,  first 16-bit code unit of current instruction
#define xIBASE   s5  // r28,  interpreted instruction base pointer: for computed goto
#define xREFS    s6  // r29,  base of object references of dex registers

#define CFI_TMP  4  // DWARF register number for       a0/r4
#define CFI_DEX  26  // DWARF register number for xPC  /s3/r26
#define CFI_REFS 29  // DWARF register number for xREFS/s6/x29

// An assembly entry for nterp.
.macro OAT_ENTRY name
    .type \name, @function
    .hidden \name
    .global \name
    .balign 16
\name:
.endm

.macro SIZE name
    .size \name, .-\name
.endm

// Similar to ENTRY but without the CFI directives.
.macro NAME_START name
    .type \name, @function
    .hidden \name  // Hide this as a global symbol, so we do not incur plt calls.
    .global \name
    /* XXX Cache alignment for function entry */
    .balign 16
\name:
.endm

.macro NAME_END name
  SIZE \name
.endm

// Macro for defining entrypoints into runtime. We don't need to save registers (we're not holding
// references there), but there is no kDontSave runtime method. So just use the kSaveRefsOnly
// runtime method.
.macro NTERP_TRAMPOLINE name, helper
ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME
    bl \helper
    RESTORE_SAVE_REFS_ONLY_FRAME
    ld.d t0, xSELF, THREAD_EXCEPTION_OFFSET
    bnez t0, nterp_deliver_pending_exception
    jr ra
END \name
.endm

// Unpack code items from dex format.
// Input: \code_item
// Output:
//   - \registers: register count
//   - \outs: out count
//   - \ins: in count
//   - \code_item: holds instruction array on exit
// Clobbers: t0
.macro FETCH_CODE_ITEM_INFO code_item, registers, outs, ins
    // Check LSB of \code_item. If 1, it's a compact dex file.
    andi t0, \code_item, 0x1
    beqz t0, 1f  // Regular dex.
    break 0x5  // Compact dex: unimplemented.
1:
    // Unpack values from regular dex format.
    ld.hu \registers, \code_item, CODE_ITEM_REGISTERS_SIZE_OFFSET
    ld.hu \outs, \code_item, CODE_ITEM_OUTS_SIZE_OFFSET
    ld.hu \ins, \code_item, CODE_ITEM_INS_SIZE_OFFSET
    addi.d \code_item, \code_item, CODE_ITEM_INSNS_OFFSET
.endm

// Set up the stack to start executing the method.
// See runtime/nterp_helpers.cc for a diagram of the setup.
// Input:
//   - a0: ArtMethod*
//   - sp
//   - \code_item: CodeItem*
//   - \cfi_refs: DWARF register number for \refs
// Output:
//   - \code_item: pointer to instruction array `insns_*` on exit
//   - \refs: pointer to obj reference array
//   - \fp: pointer to dex register array
//   - \regs: count of dex registers
//   - \ins: count of in-registers
//   - \old_sp: old stack pointer
//   - sp modified
//
// Clobbers: t0, t1
.macro SETUP_STACK_FRAME code_item, cfi_refs, refs, fp, regs, ins, old_sp
    FETCH_CODE_ITEM_INFO \code_item, \regs, /*out count*/ t1, \ins

    // Compute required frame size: ((2 * \regs) + t1) * 4 + 24
    // - The register array and reference array are each \regs in length.
    // - The out array is t1 in length.
    // - Each register is 4 bytes.
    // - Additional 24 bytes for 3 fields: saved frame pointer, dex pc, and ArtMethod*.
    slli.d t0, \regs, 1
    add.d t0, t0, t1
    slli.d t0, t0, 2
    addi.d t0, t0, 24

    // Compute new stack pointer in t0.
    sub.d t0, sp, t0
    // 16-byte alignment.
    li.w t8, ~0xF
    and t0, t0, t8

    // Set \refs to base of reference array. Align to pointer size for the frame pointer and dex pc
    // pointer, below the reference array.
    slli.d t1, t1, 2  // 4 bytes per entry.
    add.d t1, t0, t1
    addi.d t1, t1, 28  // 24 bytes from 3 fields mentioned earlier, plus 4 for alignment slack.
    li.w t8, -__SIZEOF_POINTER__
    and \refs, t1, t8

    // Set \fp to base of register array, above the reference array. This region is already aligned.
    slli.d t1, \regs, 2
    add.d \fp, \refs, t1

    // Set up the stack pointer.
    move \old_sp, sp
    .cfi_def_cfa_register \old_sp
    move sp, t0
    st.w \old_sp, \refs, -8
    CFI_DEF_CFA_BREG_PLUS_UCONST \cfi_refs, -8, FRAME_SIZE_SAVE_ALL_CALLEE_SAVES

    // Put nulls in reference array.
    beqz \regs, 2f
    move t1, \refs  // t1 as iterator
1:
    // Write in 8-byte increments, so vreg(0) gets zero'ed too, if \regs is odd.
    st.d zero, t1, 0
    addi.d t1, t1, 8
    bltu t1, \fp, 1b
2:
    // Save the ArtMethod*.
    st.d a0, sp, 0
.endm

.macro EXPORT_PC
    st.d xPC, xREFS, -16
.endm

.macro DO_SUSPEND_CHECK continue
    ld.wu t0, xSELF, THREAD_FLAGS_OFFSET
    andi t0, t0, THREAD_SUSPEND_OR_CHECKPOINT_REQUEST
    beqz t0, \continue
    EXPORT_PC
    bl art_quick_test_suspend
.endm

// Fetch the next instruction, from xPC into xINST.
// Does not advance xPC.
.macro FETCH_INST
    ld.hu xINST, xPC, 0  // zero in upper 48 bits
.endm

// Clobbers: \reg
.macro GET_INST_OPCODE reg
    andi \reg, xINST, 0xFF
.endm

// Clobbers: \reg
.macro GOTO_OPCODE reg
    slli.w \reg, \reg, ${handler_size_bits}
    add.d \reg, xIBASE, \reg
    jr \reg
.endm

// Inputs:
//   - a0
//   - xSELF
// Clobbers: t0
.macro CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot, if_not_hot
    ld.w t0, a0, ART_METHOD_ACCESS_FLAGS_OFFSET
    // Send flag bit to MSB, branch if bit is unset.
    slli.d t0, t0, 63 - ART_METHOD_IS_MEMORY_SHARED_FLAG_BIT
    bge t0, zero, \if_hot

    ld.w t0, xSELF, THREAD_SHARED_METHOD_HOTNESS_OFFSET
    beqz t0, \if_hot

    addi.d t0, t0, -1  // Reduce hotness
    st.w t0, xSELF, THREAD_SHARED_METHOD_HOTNESS_OFFSET
    b \if_not_hot
.endm

// Increase method hotness before starting the method.
// Hardcoded:
// - a0: ArtMethod*
// Clobbers: t0
.macro START_EXECUTING_INSTRUCTIONS
    ld.d a0, sp, 0
    ld.hu t0, a0, ART_METHOD_HOTNESS_COUNT_OFFSET  // t0 := hotness
#if (NTERP_HOTNESS_VALUE != 0)
#error Expected 0 for hotness value
#endif
    // If the counter is at zero (hot), handle it in the runtime.
    beqz t0, 3f
    addi.d t0, t0, -1  // increase hotness
    st.h t0, a0, ART_METHOD_HOTNESS_COUNT_OFFSET
1:
    DO_SUSPEND_CHECK continue=2f
2:
    FETCH_INST
    GET_INST_OPCODE t0
    GOTO_OPCODE t0
3:
    CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot=4f, if_not_hot=1b
4:
    move a1, zero  // dex_pc_ptr=nullptr
    move a2, zero  // vergs=nullptr
    bl nterp_hot_method
    b 2b
.endm

%def entry():
/*
 * ArtMethod entry point.
 *
 * On entry:
 *  a0     ArtMethod* callee
 *  a1-a7  method parameters
 */

OAT_ENTRY ExecuteNterpWithClinitImpl
    .cfi_startproc
    break 0x5
    .cfi_endproc
    .type EndExecuteNterpWithClinitImpl, @function
    .hidden EndExecuteNterpWithClinitImpl
    .global EndExecuteNterpWithClinitImpl
EndExecuteNterpWithClinitImpl:

OAT_ENTRY ExecuteNterpImpl
    .cfi_startproc

    // Check guard page for stack overflow.
    li.d t0, -STACK_OVERFLOW_RESERVED_BYTES
    add.d t0, t0, sp
    ld.d zero, t0, 0

    INCREASE_FRAME FRAME_SIZE_SAVE_ALL_CALLEE_SAVES
    SAVE_ALL_CALLEE_SAVES

    ld.d xPC, a0, ART_METHOD_DATA_OFFSET_64
    SETUP_STACK_FRAME xPC, CFI_REFS, xREFS, xFP, /*reg count*/ s7, /*in count*/s8, /*old sp*/u0

    beqz s8, .Lsetup_execution  // no args
    break 0x5

.Lsetup_execution:
    CFI_DEFINE_DEX_PC_WITH_OFFSET(/*tmpReg*/CFI_TMP, /*dexReg*/CFI_DEX, /*dexOffset*/0)

    la xIBASE, artNterpAsmInstructionStart
    START_EXECUTING_INSTRUCTIONS
    // NOTE: no fallthrough
    // cfi info continues, and covers the whole nterp implementation.
    SIZE ExecuteNterpImpl

%def fetch_from_thread_cache(dest_reg, miss_label):

%def footer():
/*
 * ===========================================================================
 *  Common subroutines and data
 * ===========================================================================
 */

    .text
    .align  2


// Enclose all code below in a symbol (which gets printed in backtraces).
NAME_START nterp_helper
// This is the logical end of ExecuteNterpImpl, where the frame info applies.
.cfi_endproc
NAME_END nterp_helper

// EndExecuteNterpImpl includes the methods after .cfi_endproc, as we want the runtime to see them
// as part of the Nterp PCs. This label marks the end of PCs contained by the OatQuickMethodHeader
// created for the interpreter entry point.
    .type EndExecuteNterpImpl, @function
    .hidden EndExecuteNterpImpl
    .global EndExecuteNterpImpl
EndExecuteNterpImpl:

// Entrypoints into runtime.
NTERP_TRAMPOLINE nterp_hot_method, NterpHotMethod

ENTRY nterp_deliver_pending_exception
    DELIVER_PENDING_EXCEPTION
END nterp_deliver_pending_exception

// gen_mterp.py will inline the following definitions
// within [ExecuteNterpImpl, EndExecuteNterpImpl).
%def instruction_start():
    .type artNterpAsmInstructionStart, @function
    .hidden artNterpAsmInstructionStart
    .global artNterpAsmInstructionStart
artNterpAsmInstructionStart = .L_op_nop
    .text

%def instruction_end():
    .type artNterpAsmInstructionEnd, @function
    .hidden artNterpAsmInstructionEnd
    .global artNterpAsmInstructionEnd
artNterpAsmInstructionEnd:
    break 0x5

%def opcode_pre():
%   pass
%def opcode_name_prefix():
%   return "nterp_"
%def opcode_start():
    NAME_START nterp_${opcode}
%def opcode_end():
    NAME_END nterp_${opcode}
    break 0x5
%def opcode_slow_path_start(name):
    NAME_START ${name}
%def opcode_slow_path_end(name):
    NAME_END ${name}
