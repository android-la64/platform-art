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


%def bincmp(condition=""):
    break 0x5

%def zcmp(condition=""):
    break 0x5

%def op_goto():
    break 0x5

%def op_goto_16():
    break 0x5

%def op_goto_32():
    break 0x5

%def op_if_eq():
%  bincmp(condition="eq")

%def op_if_eqz():
%  zcmp(condition="eq")

%def op_if_ge():
%  bincmp(condition="ge")

%def op_if_gez():
%  zcmp(condition="ge")

%def op_if_gt():
%  bincmp(condition="gt")

%def op_if_gtz():
%  zcmp(condition="gt")

%def op_if_le():
%  bincmp(condition="le")

%def op_if_lez():
%  zcmp(condition="le")

%def op_if_lt():
%  bincmp(condition="lt")

%def op_if_ltz():
%  zcmp(condition="lt")

%def op_if_ne():
%  bincmp(condition="ne")

%def op_if_nez():
%  zcmp(condition="ne")

%def op_packed_switch(func="NterpDoPackedSwitch"):
    break 0x5

%def op_sparse_switch():
%  op_packed_switch(func="NterpDoSparseSwitch")

%def op_throw():
    break 0x5

%def op_cmp_long():
    break 0x5
