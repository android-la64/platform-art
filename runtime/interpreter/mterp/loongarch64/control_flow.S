// return-void
// Format 10x: 00|0e
%def op_return_void():
%  op_return(is_void=True)


// return vAA
// Format 11x: AA|0f
// Clobbers: t0
%def op_return(is_object=False, is_void=False, is_wide=False):
%  if is_void:
     // Thread fence for constructor
     dbar 0x10
%  else:
     srli.w t0, xINST, 8  // t0 := AA
%    if is_wide:
       GET_VREG_WIDE a0, t0  // a0 := fp[AA:AA+1]
       // The method may return to compiled code, so also place result in fa0.
       movgr2fr.d  fa0, a0
%    elif is_object:
       GET_VREG_OBJECT a0, t0  // a0 := refs[AA]
%    else:
%      get_vreg("a0", "t0")    #  a0 := fp[AA]
       // The method may return to compiled code, so also place result in fa0.
       movgr2fr.w  fa0, a0
%#:

   CFI_REMEMBER_STATE
   ld.d sp, xREFS, -8  // caller's interpreted frame pointer
   .cfi_def_cfa sp, NTERP_SIZE_SAVE_CALLEE_SAVES
   RESTORE_NTERP_SAVE_CALLEE_SAVES
   DECREASE_FRAME NTERP_SIZE_SAVE_CALLEE_SAVES

   jirl zero, ra, 0
   // Since opcode handlers are merely labeled asm chunks within ExecuteNterpImpl's FDE, we must
   // restate the correct CFA rule for subsequent handlers. It is initially stated when setting up
   // the nterp frame (setup_nterp_frame).
   .cfi_restore_state
   CFI_DEF_CFA_BREG_PLUS_UCONST CFI_REFS, -8, NTERP_SIZE_SAVE_CALLEE_SAVES

// return-wide vAA
// Format 11x: AA|10
%def op_return_wide():
%  op_return(is_wide=True)

// return-object vAA
// Format 11x: AA|11
%def op_return_object():
%  op_return(is_object=True)

// throw vAA
// Format 11x: AA|27
// Throw the indicated exception.
%def op_throw():
   EXPORT_PC
   srli.w t0, xINST, 8     // t0 := AA
   GET_VREG_OBJECT a0, t0  // a0 := exception object
   move a1, xSELF
   bl art_quick_deliver_exception  // args a0, a1
   break 0x5

// goto +AA
// Format 10t: AA|28
// Unconditionally jump to the indicated instruction.
// Note: The branch offset must not be 0.
%def op_goto():
   srli.w t0, xINST, 8 // t0 := AA (zext)
   ext.w.b t0, t0      // t0 := +AA (sext)
   BRANCH units=t0

// goto/16 +AAAA
// Format 20t: 00|29 AAAA
// Unconditionally jump to the indicated instruction.
// Note: The branch offset must not be 0.
%def op_goto_16():
   FETCH t0, 1, signed=1  // t0 := +AAAA (sext)
   BRANCH units=t0

// goto/32 +AAAAAAAA
// Format 30t: 00|2a AAAA(lo) AAAA(hi)
// Unconditionally jump to the indicated instruction.
%def op_goto_32():
   FETCH t0, 1, signed=1, width=32  // t0 := +AAAAAAAA (sext)
   BRANCH units=t0

// packed-switch vAA, +BBBBBBBB
// Format 31t: AA|2b BBBB(lo) BBBB(hi)
// Jump to a new instruction based on the value in the given register, using a table of offsets
// corresponding to each value in a particular integral range, or fall through to the next
// instruction if there is no match.
%def op_packed_switch(is_packed=True):
   srli.w t0, xINST, 8         // t0 := AA
   FETCH t1, count=1, signed=1, width=32  // t1 := +BBBBBBBB (sext)
%  get_vreg("a1", "t0")        #  a1 := vAA
   alsl.d a0, t1, xPC, 1       // a0 := +BBBBBBBB * 2 + xPC
%  if is_packed:
     bl NterpDoPackedSwitch  // args a0 (switchData), a1 (value)
%  else:
     bl NterpDoSparseSwitch  // args a0 (switchData), a1 (value)
%#:
   BRANCH units=a0

// sparse-switch vAA, +BBBBBBBB
// Format 31t: AA|2c BBBB(lo) BBBB(hi)
// Jump to a new instruction based on the value in the given register, using an ordered table of
// value-offset pairs, or fall through to the next instruction if there is no match.
%def op_sparse_switch():
%  op_packed_switch(is_packed=False)

// cmp-long vAA, vBB, vCC
// Format 23x: AA|31 CC|BB
%def op_cmp_long():
   FETCH t1, count=1     // t1 := CC|BB
   srli.w t0, xINST, 8   // t0 := AA
   srli.w t2, t1, 8      // t2 := CC
   andi t1, t1, 0xFF     // t1 := BB
   GET_VREG_WIDE t1, t1  // t1 := fp[BB]
   GET_VREG_WIDE t2, t2  // t2 := fp[CC]
   // Note: Formula "(SLT r,l) - (SLT l,r)" lifted from compiler.
   slt t3, t1, t2
   slt t4, t2, t1
   sub.d t4, t4, t3
   FETCH_ADVANCE_INST 2
%  set_vreg("t4", "t0", z0="t1")  # fp[AA] := t4
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

// Common helper for if-test.
// Format 22t: B|A|op CCCC
%def bincmp(op):
   la.local t5, NTERP_BINCMP_HELPER
   jirl t5, t5, 0
   b${op} t0, t1, .L${opcode}_branch

   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE t2
   GOTO_OPCODE t2

.L${opcode}_branch:
   FETCH t2, count=1, signed=1  // t2 := +CCCC (sext)
   BRANCH units=t2

// if-eq vA, vB, +CCCC
// Format 22t: B|A|32 CCCC
// Branch to the given destination if the given two registers' values compare as specified.
// Note: The branch offset must not be 0.
%def op_if_eq():
%  bincmp(op="eq")

// if-ne vA, vB, +CCCC
// Format 22t: B|A|33 CCCC
// Branch to the given destination if the given two registers' values compare as specified.
// Note: The branch offset must not be 0.
%def op_if_ne():
%  bincmp(op="ne")

// if-lt vA, vB, +CCCC
// Format 22t: B|A|34 CCCC
// Branch to the given destination if the given two registers' values compare as specified.
// Note: The branch offset must not be 0.
%def op_if_lt():
%  bincmp(op="lt")

// if-ge vA, vB, +CCCC
// Format 22t: B|A|35 CCCC
// Branch to the given destination if the given two registers' values compare as specified.
// Note: The branch offset must not be 0.
%def op_if_ge():
%  bincmp(op="ge")

// if-gt vA, vB, +CCCC
// Format 22t: B|A|36 CCCC
// Branch to the given destination if the given two registers' values compare as specified.
// Note: The branch offset must not be 0.
%def op_if_gt():
%  bincmp(op="gt")

// if-le vA, vB, +CCCC
// Format 22t: B|A|37 CCCC
// Branch to the given destination if the given two registers' values compare as specified.
// Note: The branch offset must not be 0.
%def op_if_le():
%  bincmp(op="le")

// Common helper for if-testz.
// Format 21t: AA|op BBBB
%def zcmp(op):
   srli.w t0, xINST, 8   // t0 := AA
%  get_vreg("t0", "t0")  # t0 := vAA
   b${op} t0, .L${opcode}_branch

   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE t1
   GOTO_OPCODE t1

.L${opcode}_branch:
   FETCH t1, count=1, signed=1  // t1 := +BBBB (sext)
   BRANCH units=t1

// if-eqz vAA, +BBBB
// Format 21t: AA|38 BBBB
// Branch to the given destination if the given register's value compares with 0 as specified.
// Note: The branch offset must not be 0.
%def op_if_eqz():
%  zcmp(op="eqz")

// if-nez vAA, +BBBB
// Format 21t: AA|39 BBBB
// Branch to the given destination if the given register's value compares with 0 as specified.
// Note: The branch offset must not be 0.
%def op_if_nez():
%  zcmp(op="nez")

// if-ltz vAA, +BBBB
// Format 21t: AA|3a BBBB
// Branch to the given destination if the given register's value compares with 0 as specified.
// Note: The branch offset must not be 0.
%def op_if_ltz():
%  zcmp(op="ltz")

// if-gez vAA, +BBBB
// Format 21t: AA|3b BBBB
// Branch to the given destination if the given register's value compares with 0 as specified.
// Note: The branch offset must not be 0.
%def op_if_gez():
%  zcmp(op="gez")

// if-gtz vAA, +BBBB
// Format 21t: AA|3c BBBB
// Branch to the given destination if the given register's value compares with 0 as specified.
// Note: The branch offset must not be 0.
%def op_if_gtz():
%  zcmp(op="gtz")

// if-lez vAA, +BBBB
// Format 21t: AA|3d BBBB
// Branch to the given destination if the given register's value compares with 0 as specified.
// Note: The branch offset must not be 0.
%def op_if_lez():
%  zcmp(op="lez")
