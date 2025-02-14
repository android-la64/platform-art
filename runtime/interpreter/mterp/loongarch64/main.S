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

// Fixed register usages in Nterp.
//    nickname  ABI    reg   purpose
#define xSELF    $$s1  // r24,   Thread* self pointer
#define xFP      $$s3  // r26,  interpreted frame pointer: to access locals and args
#define xPC      $$s4  // r27,  interpreted program counter: to fetch instructions
#define xINST    $$s5  // r28,  first 16-bit code unit of current instruction
#define xIBASE   $$s6  // r29,  interpreted instruction base pointer: for computed goto
#define xREFS    $$s7  // r30,  base of object references of dex registers

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

%def entry():
/*
 * ArtMethod entry point.
 *
 * On entry:
 *  XXX   ArtMethod* callee
 *  rest  method parameters
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
    break 0x5

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
// EndExecuteNterpImpl includes the methods below as we want the runtime to
// see them as part of the Nterp PCs.
.cfi_endproc
NAME_END nterp_helper

// This is the end of PCs contained by the OatQuickMethodHeader created for the interpreter
// entry point.
    .type EndExecuteNterpImpl, @function
    .hidden EndExecuteNterpImpl
    .global EndExecuteNterpImpl
EndExecuteNterpImpl:

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
