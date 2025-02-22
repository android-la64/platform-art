%def op_invoke_custom():
    break 0x5

%def op_invoke_custom_range():
    break 0x5

%def invoke_direct_or_super(helper="", range="", is_super=""):
    break 0x5

%def op_invoke_direct():
    break 0x5

%def op_invoke_direct_range():
    break 0x5

%def op_invoke_super():
    break 0x5

%def op_invoke_super_range():
    break 0x5

%def op_invoke_polymorphic():
    break 0x5

%def op_invoke_polymorphic_range():
    break 0x5

%def invoke_interface(range=""):
    break 0x5

%def op_invoke_interface_slow_path():
    break 0x5

%def op_invoke_interface():
    break 0x5

%def op_invoke_interface_range():
    break 0x5

%def invoke_static(helper=""):
    break 0x5

%def op_invoke_static():
    break 0x5

%def op_invoke_static_range():
    break 0x5

%def invoke_virtual(helper="", range=""):
    break 0x5

%def op_invoke_virtual():
    break 0x5

%def op_invoke_virtual_range():
    break 0x5


// See runtime/nterp_helpers.cc for a diagram of the setup.
// Hardcoded
// - a0 - ArtMethod*
// Input
// - \cfi_refs: dwarf register number of \refs, for CFI
// - \uniq: string to ensure unique symbolic labels between instantiations
// Output
// - sp: adjusted downward for callee-saves and nterp frame
// - \refs: callee xREFS
// - \fp: callee xFP
// - \pc: callee xPC
// - \regs: register count in \refs
// - \ins: in count
// - \spills_sp: stack pointer after reg spills
%def setup_nterp_frame(cfi_refs="", refs="", fp="", pc="", regs="", ins="zero", spills_sp="", z0="", z1="", z2="", z3="", uniq=""):
   // Check guard page for stack overflow.
   li.d $z0, -STACK_OVERFLOW_RESERVED_BYTES
   add.d $z0, $z0, sp
   ld.d zero, $z0, 0

   INCREASE_FRAME NTERP_SIZE_SAVE_CALLEE_SAVES
                        // sp := sp + callee-saves
   SETUP_NTERP_SAVE_CALLEE_SAVES

   ld.d $pc, a0, ART_METHOD_DATA_OFFSET_64
   FETCH_CODE_ITEM_INFO $pc, $regs, $z0, $ins
                        // pc   := callee dex array
                        // regs := vreg count for fp array and refs array
                        // z0   := vreg count for outs array
                        // ins  := vreg count for ins array

   // Compute required frame size: ((2 * \regs) + \z0) * 4 + 24
   // - The register array and reference array each have \regs number of slots.
   // - The out array has \z0 slots.
   // - Each register slot is 4 bytes.
   // - Additional 24 bytes for 3 fields: saved frame pointer, dex pc, and ArtMethod*.
   alsl.d $z1, $regs, $z0, 1
   slli.d $z1, $z1, 2
   addi.d $z1, $z1, 24    // z1 := frame size, without alignment padding

   // compute new stack pointer
   sub.d $z1, sp, $z1
   // 16-byte alignment.
   li.d t8, ~0xF
   and $z1, $z1, t8       // z1 := new sp

   // Set \refs to base of reference array. Align to pointer size for the frame pointer and dex pc
   // pointer, below the reference array.
   alsl.d $z0, $z0, $z1, 2// z0 := out array size in bytes
   addi.d $z0, $z0, 28    //     + 24 bytes for 3 fields, plus 4 for alignment slack.
   li.d t8, -__SIZEOF_POINTER__
   and $refs, $z0, t8     // refs := refs array base

   // Set \fp to base of register array, above the reference array. This region is already aligned.
   alsl.d $fp, $regs, $refs, 2
                          // fp := fp array base

   // Set up the stack pointer.
   move $spills_sp, sp     // spills_sp := old sp
   .cfi_def_cfa_register $spills_sp
   move sp, $z1            // sp := new sp
   st.d $spills_sp, $refs, -8
   // The CFA rule is now a dwarf expression, because the nterp frame offset for SP is a dynamic
   // value, and thus SP cannot help compute CFA. For the duration of the nterp frame, CFI
   // directives cannot adjust this CFA rule, but may still capture CFI for register spills as
   // "register + offset" with a dwarf expression.
   CFI_DEF_CFA_BREG_PLUS_UCONST $cfi_refs, -8, NTERP_SIZE_SAVE_CALLEE_SAVES

   // Put nulls in reference array.
   beqz $regs, .L${uniq}_ref_zero_done
   move $z0, $refs         // z0 := address iterator
.L${uniq}_ref_zero:
   // Write in 8-byte increments, so fp[0] gets zero'ed too, if \regs is odd.
   st.d zero, $z0, 0
   addi.d $z0, $z0, 8
   bltu $z0, $fp, .L${uniq}_ref_zero
.L${uniq}_ref_zero_done:
   // Save the ArtMethod*.
   st.d a0, sp, 0


//
// Nterp entry point helpers
//


// Hardcoded:
// - a0: ArtMethod*
%def setup_ref_args_and_go(fp="", refs="", refs_end="", spills_sp="", z0="", z1="", done=""):
   // Store managed-ABI register args into fp/refs arrays.
%  store_ref_to_vreg(gpr="a1", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a2", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a3", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a4", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a5", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a6", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a7", fp=fp, refs=refs, refs_end=refs_end, done=done)
   // We drained arg registers, so continue from caller's stack.
   // A ref arg is 4 bytes, so the continuation offset is well known.
   addi.d $z0, $spills_sp, (NTERP_SIZE_SAVE_CALLEE_SAVES + 8 + 7*4)
       // z0 := out array base addr + 7 vreg slots
.Lentry_ref_stack:
   ld.wu $z1, $z0, 0
   st.w $z1, $fp, 0
   st.w $z1, $refs, 0
   addi.d $z0, $z0, 4
   addi.d $fp, $fp, 4
   addi.d $refs, $refs, 4
   bne $refs, $refs_end, .Lentry_ref_stack

   b $done


%def store_ref_to_vreg(gpr="", fp="", refs="", refs_end="", done=""):
   st.w $gpr, $fp, 0
   st.w $gpr, $refs, 0
   addi.d $fp, $fp, 4
   addi.d $refs, $refs, 4
   beq $refs, $refs_end, $done


// \fp and \refs are used as array base addrs, unmodified.
%def store_gpr_to_vreg(gpr="", offset="", shorty="", fp="", refs="", z0="", z1="", D="", F="", J="", L="", next=""):
.Lentry_arg_${gpr}:
   ld.b $z0, $shorty, 0      // z0 := shorty type
   addi.d $shorty, $shorty, 1// Increment char ptr.
   beqz $z0, $next           // z0 = \0: finished shorty pass
   beq $z0, $D, .Lentry_arg_skip_double_${gpr}
   beq $z0, $F, .Lentry_arg_skip_float_${gpr}

   add.d $z1, $offset, $fp
   beq $z0, $J, .Lentry_arg_long_${gpr}
   st.w $gpr, $z1, 0
   bne $z0, $L, .Lentry_arg_finish_${gpr}
   add.d $z1, $offset, $refs
   st.w $gpr, $z1, 0
   b .Lentry_arg_finish_${gpr}
.Lentry_arg_skip_double_${gpr}:
   addi.d $offset, $offset, 4
.Lentry_arg_skip_float_${gpr}:
   addi.d $offset, $offset, 4
   b .Lentry_arg_${gpr}
.Lentry_arg_long_${gpr}:
   st.d $gpr, $z1, 0
   addi.d $offset, $offset, 4
.Lentry_arg_finish_${gpr}:
   addi.d $offset, $offset, 4


// \fp is used as array base addr, unmodified.
%def store_fpr_to_vreg(fpr="", offset="", shorty="", fp="", z0="", z1="", D="", F="", J="", next=""):
.Lentry_farg_${fpr}:
   ld.b $z0, $shorty, 0      // z0 := shorty type
   addi.d $shorty, $shorty, 1// Increment char ptr.
   beqz $z0, $next           // z0 = \0: finished shorty pass
   beq $z0, $D, .Lentry_farg_double_${fpr}
   beq $z0, $F, .Lentry_farg_float_${fpr}
   addi.d $offset, $offset, 4
   bne $z0, $J, .Lentry_farg_${fpr}
   addi.d $offset, $offset, 4
   b .Lentry_farg_${fpr}

.Lentry_farg_float_${fpr}:
   add.d $z1, $offset, $fp
   fst.s $fpr, $z1, 0
   b .Lentry_farg_finish_${fpr}
.Lentry_farg_double_${fpr}:
   add.d $z1, $offset, $fp
   fst.d $fpr, $z1, 0
   addi.d $offset, $offset, 4
.Lentry_farg_finish_${fpr}:
   addi.d $offset, $offset, 4


// \outs, \fp, \refs are used as iterators, modified.
%def store_outs_to_vregs(outs="", shorty="", fp="", refs="", z0="", z1="", D="", F="", J="", L="", next=""):
.Lentry_stack:
   ld.b $z0, $shorty, 0      // z0 := next shorty arg spec
   addi.d $shorty, $shorty, 1// Increment char ptr.
   beqz $z0, $next           // z0 == \0
   beq $z0, $F, .Lentry_stack_next_4
   beq $z0, $D, .Lentry_stack_next_8
   beq $z0, $J, .Lentry_stack_long
   // 32-bit arg
   ld.wu $z1, $outs, 0
   st.w $z1, $fp, 0
   bne $z0, $L, .Lentry_stack_next_4
   // and also a ref
   st.w $z1, $refs, 0
.Lentry_stack_next_4:
   addi.d $outs, $outs, 4
   addi.d $fp, $fp, 4
   addi.d $refs, $refs, 4
   b .Lentry_stack
.Lentry_stack_long:
   ld.d $z1, $outs, 0
   st.d $z1, $fp, 0
.Lentry_stack_next_8:
   addi.d $outs, $outs, 8
   addi.d $fp, $fp, 8
   addi.d $refs, $refs, 8
   b .Lentry_stack


// \outs, \fp are used as iterators, modified.
%def store_float_outs_to_vregs(outs="", shorty="", fp="", z0="", D="", F="", J="", next=""):
.Lentry_fstack:
   ld.b $z0, $shorty, 0      // z0 := next shorty arg spec
   addi.d $shorty, $shorty, 1// Increment char ptr.
   beqz $z0, $next           // z0 == \0
   beq $z0, $F, .Lentry_fstack_float
   beq $z0, $D, .Lentry_fstack_double
   beq $z0, $J, .Lentry_fstack_next_8
   // 32-bit arg
   addi.d $outs, $outs, 4
   addi.d $fp, $fp, 4
   b .Lentry_fstack
.Lentry_fstack_float:
   ld.wu $z0, $outs, 0
   st.w $z0, $fp, 0
   addi.d $outs, $outs, 4
   addi.d $fp, $fp, 4
   b .Lentry_fstack
.Lentry_fstack_double:
   ld.d $z0, $outs, 0
   st.d $z0, $fp, 0
.Lentry_fstack_next_8:
   addi.d $outs, $outs, 8
   addi.d $fp, $fp, 8
   b .Lentry_fstack

