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
 *
 * 32 general purposes registers
 * - fixed purpose: zero, ra, sp, gp, tp, s1
 *     gp/scs: shadow call stack - do not clobber!
 *     s1/tr: ART thread register - do not clobber!
 * - temporaries: t0-t8
 * - arguments: a0-a7
 * - return: a0, a1
 * - callee saved: ra, s9/fp, s0, s2-s8
 *     s9 is flexible, available to use as a frame pointer if needed.
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
#define s9       $$r22
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

.macro TEST_IF_MARKING reg, label
    ld.b \reg, xSELF, THREAD_IS_GC_MARKING_OFFSET
    bnez \reg, \label
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

// Fetch one or more half-word units from an offset past the current PC.
// The offset is specified in 16-bit code units.
//
// A \width flag allows reading 32 bits (2 units) or 64 bits (4 units) from the offset.
// The LOONGARCH ISA supports unaligned accesses for these wider loads.
//
// If \width=8, \byte={0,1} indexes into the code unit at the offset.
//
// Default behavior loads one code unit with unsigned zext.
// The \signed flag is for signed sext, for shorter loads.
//
// Does not advance xPC.
.macro FETCH reg, count, signed=0, width=16, byte=0
    .if \width == 8
      .if \signed
        ld.b  \reg, xPC, (\count*2 + \byte)
      .else
        ld.bu \reg, xPC, (\count*2 + \byte)
      .endif
    .elseif \width == 16
      .if \signed
        ld.h  \reg, xPC, (\count*2)
      .else
        ld.hu \reg, xPC, (\count*2)
      .endif
    .elseif \width == 32
      .if \signed
        ld.w  \reg, xPC, (\count*2)
      .else
        ld.wu \reg, xPC, (\count*2)
      .endif
    .elseif \width == 64
      ld.d  \reg, xPC, (\count*2)
    .else
      break 0x5 // impossible
    .endif
.endm

// Fetch the next instruction, from xPC into xINST.
// Does not advance xPC.
.macro FETCH_INST
    ld.hu xINST, xPC, 0  // zero in upper 48 bits
.endm

// Fetch the next instruction, from xPC into xINST. Advance xPC by \count units, each 2 bytes.
//
// Immediates have a 12-bit offset range from xPC. Thus, \count can range from -1024 to 1023.
//
// Note: Must be placed AFTER anything that can throw an exception, or the exception catch may miss.
// Thus, this macro must be placed after EXPORT_PC.
.macro FETCH_ADVANCE_INST count
    ld.hu xINST, xPC, (\count*2)  // zero in upper 48 bits
    addi.d xPC, xPC, (\count*2)
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

.macro FETCH_FROM_THREAD_CACHE reg, miss_label, z0, z1
    // See art::InterpreterCache::IndexOf() for computing index of key within cache array.
    // Entry address:
    //   xSELF + OFFSET + ((xPC>>2 & xFF) << 4)
    // = xSELF + OFFSET + ((xPC & xFF<<2) << 2)
    // = xSELF + ((OFFSET>>2 + (xPC & xFF<<2)) << 2)
    // => ANDI, ADD, alsl.d2
#if (THREAD_INTERPRETER_CACHE_SIZE_LOG2 != 8)
#error Expected interpreter cache array size = 256 elements
#endif
#if (THREAD_INTERPRETER_CACHE_SIZE_SHIFT != 2)
#error Expected interpreter cache entry size = 16 bytes
#endif
#if ((THREAD_INTERPRETER_CACHE_OFFSET & 0x3) != 0)
#error Expected interpreter cache offset to be 4-byte aligned
#endif
    andi \z0, xPC, 0xFF << 2
    addi.d \z0, \z0, THREAD_INTERPRETER_CACHE_OFFSET >> 2
    alsl.d \z0, \z0, xSELF, 2  // z0 := entry's address
    ld.d \z1, \z0, 0           // z1 := dex PC
    bne xPC, \z1, \miss_label
    ld.d \reg, \z0, 8         // value: depends on context; see call site
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

// 64 bit read
// Clobbers: \reg
// Safe if \reg == \vreg.
.macro GET_VREG_WIDE reg, vreg
    alsl.d \reg, \vreg, xFP, 2  // vreg addr in register array
    ld.d \reg, \reg, 0          // reg := fp[vreg](lo) | fp[vreg+1](hi)
.endm

// 64 bit write
// Clobbers: z0
.macro SET_VREG_WIDE reg, vreg, z0
    alsl.d \z0, \vreg, xFP, 2    // vreg addr in register array
    st.d \reg, \z0, 0            // fp[vreg] := reg(lo) ; fp[vreg+1] := reg(hi)
    alsl.d \z0, \vreg, xREFS, 2  // vreg addr in reference array
    st.d zero, \z0, 0            // refs[vreg] := null ; refs[vreg+1] := null
.endm

// 32 bit write
// Clobbers: z0
.macro SET_VREG_WORD reg, vreg, z0
    alsl.d \z0, \vreg, xFP, 2    // vreg addr in register array
    st.w \reg, \z0, 0            // fp[vreg] := reg(lo) ; fp[vreg+1] := reg(hi)
    alsl.d \z0, \vreg, xREFS, 2  // vreg addr in reference array
    st.w zero, \z0, 0            // refs[vreg] := null ; refs[vreg+1] := null
    //slli.d \vreg, \vreg, 2
    //add.d \z0, \vreg, xFP     // vreg addr in register array
    //st.w \reg, \z0, 0         // fp[vreg] := reg
    //add.d \z0, \vreg, xREFS   // vreg addr in reference array
    //st.w zero, \z0, 0         // refs[vreg] := null
.endm

// Object read
// Clobbers: \reg
// Safe if \reg == \vreg.
.macro GET_VREG_OBJECT reg, vreg
    alsl.d \reg, \vreg, xREFS, 2  // vreg addr in register array
    ld.wu \reg, \reg, 0           // reg := refs[vreg]
.endm

// Object write
// Clobbers: z0
.macro SET_VREG_OBJECT reg, vreg, z0
    alsl.d \z0, \vreg, xFP, 2   // vreg addr in register array
    st.w \reg, \z0, 0            // fp[vreg] := reg
    alsl.d \z0, \vreg, xREFS, 2 // vreg addr in register array
    st.w \reg, \z0, 0            // refs[vreg] := reg
.endm

// Floating-point 64 bit read
// Clobbers: \reg, \vreg
.macro GET_VREG_DOUBLE reg, vreg
    alsl.d \vreg, \vreg, xFP, 2  // vreg addr in register array
    fld.d \reg, \vreg, 0         // reg := fp[vreg](lo) | fp[vreg+1](hi)
.endm

// Floating-point 64 bit write
// Clobbers: \reg, z0
.macro SET_VREG_DOUBLE reg, vreg, z0
    alsl.d \z0, \vreg, xFP, 2    // vreg addr in register array
    fst.d \reg, \z0, 0           // fp[vreg] := reg(lo) ; fp[vreg+1] := reg(hi)
    alsl.d \z0, \vreg, xREFS, 2  // vreg addr in reference array
    st.d zero, \z0, 0            // refs[vreg] := null ; refs[vreg+1] := null
.endm

// Put "%def" definitions after ".macro" definitions for proper expansion. %def is greedy.

// Typed read, defaults to 32-bit read
// Note: An object ref requires LWU, or LW;ZEXT.W.
// Clobbers: \reg
// Safe if \reg == \vreg.
%def get_vreg(reg, vreg, width=32, is_wide=False, is_unsigned=False):
%  if is_wide or width == 64:
     GET_VREG_WIDE $reg, $vreg
%  elif is_unsigned:
     alsl.d $reg, $vreg, xFP, 2  // vreg addr in register array
     ld.wu $reg, $reg, 0         // reg := fp[vreg], zext
%  else:
     alsl.d $reg, $vreg, xFP, 2  // vreg addr in register array
     ld.w $reg, $reg, 0          // reg := fp[vreg]
%#:

// Typed write, defaults to 32-bit write.
// Note: Incorrect for an object ref; it requires 2nd SW into xREFS.
// Clobbers: z0
%def set_vreg(reg, vreg, z0, width=32, is_wide=False):
%  if is_wide or width == 64:
     SET_VREG_WIDE $reg, $vreg, $z0
%  else:
     SET_VREG_WORD $reg, $vreg, $z0
%#:

// Floating-point read, defaults to 32-bit read.
// Clobbers: reg, vreg
%def get_vreg_float(reg, vreg, is_double=False):
%  if is_double:
     GET_VREG_DOUBLE $reg, $vreg
%  else:
     alsl.d $vreg, $vreg, xFP, 2  // vreg addr in register array
     fld.s $reg, $vreg, 0         // reg := fp[vreg]
%#:

// Floating-point write, defaults to 32-bit write.
// Clobbers: reg, z0
%def set_vreg_float(reg, vreg, z0, is_double=False):
%  if is_double:
     SET_VREG_DOUBLE $reg, $vreg, $z0
%  else:
     alsl.d $z0, $vreg, xFP, 2    // vreg addr in register array
     fst.s $reg, $z0, 0           // fp[vreg] := reg
     alsl.d $z0, $vreg, xREFS, 2  // vreg addr in reference array
     st.w zero, $z0, 0            // refs[vreg] := null
%#:

%def entry():
/*
 * ArtMethod entry point.
 *
 * On entry:
 *  a0     ArtMethod* callee
 *  a1-a7  method parameters
 */
OAT_ENTRY ExecuteNterpWithClinitImpl
#if MIRROR_CLASS_STATUS_SHIFT < 12
#error mirror class status bits cannot use lu12i.w load technique
#endif
    .cfi_startproc
    // For simplicity, we don't do a read barrier here, but instead rely
    // on art_quick_resolution_trampoline to always have a suspend point before
    // calling back here.
    ld.wu t0, a0, ART_METHOD_DECLARING_CLASS_OFFSET
    ld.w t1, t0, MIRROR_CLASS_STATUS_OFFSET  // t1 := status word, sext
    srli.w t1, t1, MIRROR_CLASS_STATUS_SHIFT
    li.w t2, MIRROR_CLASS_STATUS_VISIBLY_INITIALIZED
    bgeu t1, t2, ExecuteNterpImpl // compare class init status
    li.w t2, MIRROR_CLASS_STATUS_INITIALIZED
    bltu t1, t2, .Linitializing_check
    dbar 0x10
    b ExecuteNterpImpl
.Linitializing_check:
    li.w t2, MIRROR_CLASS_STATUS_INITIALIZING
    bltu t1, t2, .Lresolution_trampoline
    ld.wu t1, t0, MIRROR_CLASS_CLINIT_THREAD_ID_OFFSET
    ld.wu t0, xSELF, THREAD_TID_OFFSET
    beq t0, t1, ExecuteNterpImpl
.Lresolution_trampoline:
    b art_quick_resolution_trampoline
    .cfi_endproc
    .type EndExecuteNterpWithClinitImpl, @function
    .hidden EndExecuteNterpWithClinitImpl
    .global EndExecuteNterpWithClinitImpl
EndExecuteNterpWithClinitImpl:

OAT_ENTRY ExecuteNterpImpl
// u0 as s9, t7 as s10, t8 as s11 in RISCV
   .cfi_startproc
%  setup_nterp_frame(cfi_refs="CFI_REFS", refs="xREFS", fp="xFP", pc="xPC", regs="s7", ins="s8", spills_sp="u0", z0="t0", z1="t1", z2="t2", z3="t3", uniq="entry")
                            // xREFS := callee refs array
                            // xFP   := callee fp array
                            // xPC   := callee dex array
                            // s7    := refs/fp vreg count
                            // s8    := ins count
                            // u0    := post-spills pre-frame sp
                            // sp    := post-frame sp
   CFI_DEFINE_DEX_PC_WITH_OFFSET(/*tmpReg*/CFI_TMP, /*dexReg*/CFI_DEX, /*dexOffset*/0)

   // Fast path: zero args.
   beqz s8, .Lentry_go

   sub.d s7, s7, s8           // s7 := a1 index in fp/refs
   ld.wu t7, a0, ART_METHOD_ACCESS_FLAGS_OFFSET
                            // t7 := method flags

   // Fast path: all reference args.
   alsl.d t0, s7, xFP, 2    // t0 := &xFP[a1]
   alsl.d t1, s7, xREFS, 2  // t1 := &xREFS[a1]
   BRANCH_IF_BIT_CLEAR t2, t7, ART_METHOD_NTERP_ENTRY_POINT_FAST_PATH_FLAG_BIT, .Lentry_a1
%  setup_ref_args_and_go(fp="t0", refs="t1", refs_end="xFP", spills_sp="u0", z0="t2", z1="t3", done=".Lentry_go")

   // Fast path: instance with zero args.
.Lentry_a1:
   bstrpick.w t7, t7, ART_METHOD_IS_STATIC_FLAG_BIT, ART_METHOD_IS_STATIC_FLAG_BIT
                            // t7 := 1 if static, 0 if instance
   bnez t7, .Lentry_shorty
   st.w a1, t0, 0
   st.w a1, t1, 0
   li.d t2, 1
   beq s8, t2, .Lentry_go

   // Slow path: runtime call to obtain shorty, full setup from managed ABI.
.Lentry_shorty:
   SPILL_ALL_ARGUMENTS
   // TODO: Better way to get shorty
   bl NterpGetShorty      // arg a0
   move t8, a0               // t8 := shorty
   RESTORE_ALL_ARGUMENTS

   // temporaries are trashed, recompute some values
   alsl.d t0, s7, xFP, 2    // t0 := &xFP[a1]
   alsl.d t1, s7, xREFS, 2  // t1 := &xREFS[a1]
   addi.d t2, t8, 1       // t2 := shorty arg (skip return type)
   xori t7, t7, 1         // t7 := 0 if static, 1 if instance
   slli.w t3, t7, 2         // t3 := (static) 0, (instance) 4: fp/refs/outs byte offset
   // constant setup for gpr/fpr shorty comparisons
   li.d s0, 'D'               // s0 := double char (unused fp)
   li.d s4, 'F'               // s4 := float char (unused xINST)
   li.d s5, 'J'               // s5 := long char (unused xIBASE)
   li.d s8, 'L'               // s8 := ref char (unused ins count)
   bnez t7, .Lentry_args   // instance a1 already stored into callee's xFP and xREFS

%  store_gpr_to_vreg(gpr="a1", offset="t3", shorty="t2", fp="t0", refs="t1", z0="t4", z1="t5", D="s0", F="s4", J="s5", L="s8", next=".Lentry_fargs")

.Lentry_args:
   // linear scan through shorty: extract non-float args
%  store_gpr_to_vreg(gpr="a2", offset="t3", shorty="t2", fp="t0", refs="t1", z0="t4", z1="t5", D="s0", F="s4", J="s5", L="s8", next=".Lentry_fargs")
%  store_gpr_to_vreg(gpr="a3", offset="t3", shorty="t2", fp="t0", refs="t1", z0="t4", z1="t5", D="s0", F="s4", J="s5", L="s8", next=".Lentry_fargs")
%  store_gpr_to_vreg(gpr="a4", offset="t3", shorty="t2", fp="t0", refs="t1", z0="t4", z1="t5", D="s0", F="s4", J="s5", L="s8", next=".Lentry_fargs")
%  store_gpr_to_vreg(gpr="a5", offset="t3", shorty="t2", fp="t0", refs="t1", z0="t4", z1="t5", D="s0", F="s4", J="s5", L="s8", next=".Lentry_fargs")
%  store_gpr_to_vreg(gpr="a6", offset="t3", shorty="t2", fp="t0", refs="t1", z0="t4", z1="t5", D="s0", F="s4", J="s5", L="s8", next=".Lentry_fargs")
%  store_gpr_to_vreg(gpr="a7", offset="t3", shorty="t2", fp="t0", refs="t1", z0="t4", z1="t5", D="s0", F="s4", J="s5", L="s8", next=".Lentry_fargs")
   // We drained arg registers, so continue from caller stack's out array. Unlike the reference-only
   // fast-path, the continuation offset in the out array can vary, depending on the presence of
   // 64-bit values in the arg registers. \offset tracks this value as a byte offset.
   addi.d t5, u0, (NTERP_SIZE_SAVE_CALLEE_SAVES + 8)
                            // t5 := (caller) outs array base address
   add.d t4, t3, t0         // t4 := (callee) &FP[next]
   add.d t1, t3, t1         // t1 := (callee) &REFS[next]
   add.d t3, t3, t5         // t3 := (caller) &OUTS[next]
%  store_outs_to_vregs(outs="t3", shorty="t2", fp="t4", refs="t1", z0="t5", z1="t6", D="s0", F="s4", J="s5", L="s8", next=".Lentry_fargs")
                            // t0 = &xFP[a1], unclobbered
.Lentry_fargs:
   addi.d t1, t8, 1         // t1 := shorty arg (skip return type)
   slli.w t2, t7, 2         // t2 := starting byte offset for fp/outs, static and instance
   // linear scan through shorty: extract float args
%  store_fpr_to_vreg(fpr="fa0", offset="t2", shorty="t1", fp="t0", z0="t3", z1="t4", D="s0", F="s4", J="s5", next=".Lentry_go")
%  store_fpr_to_vreg(fpr="fa1", offset="t2", shorty="t1", fp="t0", z0="t3", z1="t4", D="s0", F="s4", J="s5", next=".Lentry_go")
%  store_fpr_to_vreg(fpr="fa2", offset="t2", shorty="t1", fp="t0", z0="t3", z1="t4", D="s0", F="s4", J="s5", next=".Lentry_go")
%  store_fpr_to_vreg(fpr="fa3", offset="t2", shorty="t1", fp="t0", z0="t3", z1="t4", D="s0", F="s4", J="s5", next=".Lentry_go")
%  store_fpr_to_vreg(fpr="fa4", offset="t2", shorty="t1", fp="t0", z0="t3", z1="t4", D="s0", F="s4", J="s5", next=".Lentry_go")
%  store_fpr_to_vreg(fpr="fa5", offset="t2", shorty="t1", fp="t0", z0="t3", z1="t4", D="s0", F="s4", J="s5", next=".Lentry_go")
%  store_fpr_to_vreg(fpr="fa6", offset="t2", shorty="t1", fp="t0", z0="t3", z1="t4", D="s0", F="s4", J="s5", next=".Lentry_go")
%  store_fpr_to_vreg(fpr="fa7", offset="t2", shorty="t1", fp="t0", z0="t3", z1="t4", D="s0", F="s4", J="s5", next=".Lentry_go")
   addi.d t3, u0, (NTERP_SIZE_SAVE_CALLEE_SAVES + 8)
                            // t3 := (caller) outs array base address
   add.d t0, t2, t0           // t0 := (callee) &FP[next]
   add.d t2, t2, t3           // t2 := (caller) &OUTS[next]
%  store_float_outs_to_vregs(outs="t2", shorty="t1", fp="t0", z0="t3", D="s0", F="s4", J="s5", next=".Lentry_go")

.Lentry_go:
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

common_errArrayIndex:
    EXPORT_PC
    // CALL preserves RA for stack walking.
    bl art_quick_throw_array_bounds  // args a0 (index), a1 (length)

common_errDivideByZero:
    EXPORT_PC
    // CALL preserves RA for stack walking.
    bl art_quick_throw_div_zero

common_errNullObject:
    EXPORT_PC
    // CALL preserves RA for stack walking.
    bl art_quick_throw_null_pointer_exception

NterpInvokeVirtual:
%  nterp_invoke_virtual()
NterpInvokeSuper:
%  nterp_invoke_super()
NterpInvokeDirect:
%  nterp_invoke_direct()
NterpInvokeStringInit:
%  nterp_invoke_string_init()
NterpInvokeStatic:
%  nterp_invoke_static()
NterpInvokeInterface:
%  nterp_invoke_interface()
NterpInvokePolymorphic:
%  nterp_invoke_polymorphic()
NterpInvokeCustom:
%  nterp_invoke_custom()
NterpInvokeVirtualRange:
%  nterp_invoke_virtual_range()
NterpInvokeSuperRange:
%  nterp_invoke_super_range()
NterpInvokeDirectRange:
%  nterp_invoke_direct_range()
NterpInvokeStringInitRange:
%  nterp_invoke_string_init_range()
NterpInvokeStaticRange:
%  nterp_invoke_static_range()
NterpInvokeInterfaceRange:
%  nterp_invoke_interface_range()
NterpInvokePolymorphicRange:
%  nterp_invoke_polymorphic_range()
NterpInvokeCustomRange:
%  nterp_invoke_custom_range()
NTERP_INVOKE_INTERFACE_HELPER:
    st.d s3, s6, -16 //EXPORT_PC
    ld.hu s7, s3, 4
    andi t0, s3, 0xFF << 2
    addi.d t0, t0, THREAD_INTERPRETER_CACHE_OFFSET >> 2
    alsl.d t0, t0, s1, 2
    ld.d t1, t0, 0
    jirl zero, t6, 0
NTERP_SPUT_SLOW_HELPER:
    move a0, xSELF
    ld.d a1, sp, 0  // a1 := caller ArtMethod*
    move a2, xPC
    move a3, zero
    st.d s3, s6, -16 //EXPORT_PC
    jirl zero, t5, 0
NTERP_NEW_ARRAY_RESUME_HELPER:
    dbar            0x5  // fence w, w; main.S has details
    alsl.d          t0, s8, s2, 0x2  // SET_VREG_OBJECT a0, s8, z0=t0
    st.w            a0, t0, 0
    alsl.d          t0, s8, s6, 0x2
    st.w            a0, t0, 0
    ld.hu           s4, s3, 4     // FETCH_ADVANCE_INST 2
    addi.d          s3, s3, 4
    andi            t0, s4, 0xff  // GET_INST_OPCODE t0
    slli.w          t0, t0, 0x7
    add.d           t0, s5, t0
    jr              t0    // GOTO_OPCODE t0
NTERP_IGET_SLOW_HELPER:
    move a0, s1
    ld.d a1, sp, 0  // a1 := caller ArtMethod*
    move a2, s3
    move a3, zero
    st.d s3, s6, -16
    jr t5
NTERP_INSTANCE_OF_HELPER:
    alsl.d          t1, s7, s2, 0x2  // set_vreg("a0", "s7", z0="t1")
    st.w            a0, t1, 0
    alsl.d          t1, s7, s6, 0x2
    st.w            zero, t1, 0
    ld.hu           s4, s3, 4        // FETCH_ADVANCE_INST 2
    addi.d          s3, s3, 4
    andi            t0, s4, 0xff     // GET_INST_OPCODE t0
    slli.w          t0, t0, 0x7      // GOTO_OPCODE t0
    add.d           t0, s5, t0
    jr              t0


// This is the logical end of ExecuteNterpImpl, where the frame info applies.
.cfi_endproc

NterpToNterpInstance:
%  nterp_to_nterp_instance()
NterpToNterpStringInit:
%  nterp_to_nterp_string_init()
NterpToNterpStatic:
%  nterp_to_nterp_static()
NterpToNterpInstanceRange:
%  nterp_to_nterp_instance_range()
NterpToNterpStringInitRange:
%  nterp_to_nterp_string_init_range()
NterpToNterpStaticRange:
%  nterp_to_nterp_static_range()

// This is the logical end of ExecuteNterpImpl, where the frame info applies.
NAME_END nterp_helper

// EndExecuteNterpImpl includes the methods after .cfi_endproc, as we want the runtime to see them
// as part of the Nterp PCs. This label marks the end of PCs contained by the OatQuickMethodHeader
// created for the interpreter entry point.
    .type EndExecuteNterpImpl, @function
    .hidden EndExecuteNterpImpl
    .global EndExecuteNterpImpl
EndExecuteNterpImpl:

// Entrypoints into runtime.
NTERP_TRAMPOLINE nterp_allocate_object, NterpAllocateObject
NTERP_TRAMPOLINE nterp_filled_new_array, NterpFilledNewArray
NTERP_TRAMPOLINE nterp_filled_new_array_range, NterpFilledNewArrayRange
NTERP_TRAMPOLINE nterp_get_class, NterpGetClass
NTERP_TRAMPOLINE nterp_get_instance_field_offset, NterpGetInstanceFieldOffset
NTERP_TRAMPOLINE nterp_get_method, NterpGetMethod
NTERP_TRAMPOLINE nterp_get_static_field, NterpGetStaticField
NTERP_TRAMPOLINE nterp_hot_method, NterpHotMethod
NTERP_TRAMPOLINE nterp_load_object, NterpLoadObject

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
%def opcode_slow_path_start(name):
    NAME_START ${name}
%def opcode_slow_path_end(name):
    NAME_END ${name}
