// neg-float vA, vB
// Format 12x: B|A|7f
%def op_neg_float():
%  generic_funop(instr="fneg.s ft0, ft0", dst="s", src="s")

// neg-double vA, vB
// Format 12x: B|A|80
%def op_neg_double():
%  generic_funop(instr="fneg.d ft0, ft0", dst="d", src="d")

// int-to-float vA, vB
// Format 12x: B|A|82
// Note: Conversion of int32 to float, using round-to-nearest. This loses precision for some values.
// Note: For ties, the IEEE 754-2008 standard defaults to "roundTiesToEven" for binary floats.
%def op_int_to_float():
%  generic_funop(instr="ffint.s.w ft0, ft1", dst="s", src="w", gtf=True)

// int-to-double vA, vB
// Format 12x: B|A|83
// Note: Conversion of int32 to double.
%def op_int_to_double():
   movgr2fr.w ft15, t1
%  generic_funop(instr="ffint.d.w ft0, ft1", dst="d", src="w", gtf=True)

// long-to-float vA, vB
// Format 12x: B|A|85
// Note: Conversion of int64 to float, using round-to-nearest. This loses precision for some values.
// Note: For ties, the IEEE 754-2008 standard defaults to "roundTiesToEven" for binary floats.
%def op_long_to_float():
   movgr2fr.d ft15, t1
%  generic_funop(instr="ffint.s.l ft0, ft1", dst="s", src="l", gtf=True)

// long-to-double vA, vB
// Format 12x: B|A|86
// Note: Conversion of int64 to double, using round-to-nearest. This loses precision for some values.
// Note: For ties, the IEEE 754-2008 standard defaults to "roundTiesToEven" for binary floats.
%def op_long_to_double():
   movgr2fr.d ft15, t1
%  generic_funop(instr="ffint.d.l ft0, ft1", dst="d", src="l", gtf=True)

// float-to-int vA, vB
// Format 12x: B|A|87
// Note: Conversion of float to int32, using round-toward-zero. NaN and -0.0 (negative zero)
// convert to the integer 0. Infinities and values with too large a magnitude to be represented
// get converted to either 0x7fffffff or -0x80000000 depending on sign.
//
// FCVT.W.S RTZ has the following behavior:
// - NaN rounds to 0x7ffffff - requires check and set to zero.
// - negative zero rounds to zero - matches dex spec.
// - pos inf rounds to 0x7fffffff - matches dex spec.
// - neg inf rounds to 0x80000000 - matches dex spec.
%def op_float_to_int():
%  generic_funop(instr="ftintrz.w.s ft1, ft0", dst="w", src="s", nan_zeroed=True, ftg=True)

// float-to-long vA, vB
// Format 12x: B|A|88
// Note: Conversion of float to int64, using round-toward-zero. The same special case rules as for
// float-to-int apply here, except that out-of-range values get converted to either
// 0x7fffffffffffffff or -0x8000000000000000 depending on sign.
//
// FCVT.L.S RTZ has the following behavior:
// - NaN rounds to 0x7fffffffffffffff - requires check and set to zero.
// - negative zero rounds to zero - matches dex spec.
// - pos inf rounds to 0x7fffffffffffffff - matches dex spec.
// - neg inf rounds to 0x8000000000000000 - matches dex spec.
%def op_float_to_long():
%  generic_funop(instr="ftintrz.l.s ft1, ft0", dst="l", src="s", nan_zeroed=True, ftg=True)

// float-to-double vA, vB
// Format 12x: B|A|89
// Note: Conversion of float to double, preserving the value exactly.
%def op_float_to_double():
%  generic_funop(instr="fcvt.d.s ft0, ft0", dst="d", src="s")

// double-to-int vA, vB
// Format 12x: B|A|8a
// Note: Conversion of double to int32, using round-toward-zero. The same special case rules as for
// float-to-int apply here.
%def op_double_to_int():
%  generic_funop(instr="ftintrz.w.d ft1, ft0", dst="w", src="d", nan_zeroed=True, ftg=True)

// double-to-long vA, vB
// Format 12x: B|A|8b
// Note: Conversion of double to int64, using round-toward-zero. The same special case rules as for
// float-to-long apply here.
%def op_double_to_long():
%  generic_funop(instr="ftintrz.l.d ft1, ft0", dst="l", src="d", nan_zeroed=True, ftg=True)

// double-to-float vA, vB
// Format 12x: B|A|8c
// Note: Conversion of double to float, using round-to-nearest. This loses precision for some values.
// Note: For ties, the IEEE 754-2008 standard defaults to "roundTiesToEven" for binary floats.
%def op_double_to_float():
%  generic_funop(instr="fcvt.s.d ft0, ft0", dst="s", src="d")

// unop boilerplate
// instr: operand held in t1 or ft0, result written to t1 or ft0.
// instr must not clobber t2.
// dst: one of w (int32), l (int64), s (float), d (double)
// src: one of w (int32), l (int64), s (float), d (double)
// Clobbers: ft0, ft1, t0, t1, t2
%def generic_funop(instr, dst, src, nan_zeroed=False, gtf=False, ftg=False):
    srli.w t0, xINST, 12        // t0 := B
    srli.w t2, xINST, 8         // t2 := B|A

%  if src == "w":
%    get_vreg("t1", "t0")     #  t1 := fp[B]
%  elif src == "l":
     GET_VREG_WIDE t1, t0     // t1 := fp[B]
%  elif src == "s":
%    get_vreg_float("ft0", "t0")  #  ft0 := fp[B]
%  elif src == "d":
     GET_VREG_DOUBLE ft0, t0  // ft0 := fp[B]
%  else:
%    assert false, src
%#:
    andi t2, t2, 0xF            // t2 := A
    FETCH_ADVANCE_INST 1       // advance xPC, load xINST
%  if nan_zeroed:
     // Okay to clobber T1. It is not read if nan_zeroed=True.
     fclass.${src} ft1, ft0  // fclass.s or fclass.d on the source register ft0
     movfr2gr.${src} t1, ft1
     // Z-TODO: verify all situations, here QNaN only.
     sltui t1, t1, 0x1    // t1 := 1 if NaN, per dex spec. Skip the conversion.
     bnez t1, 1f
%  if gtf:
%    if src == "w":
       movgr2fr.w ft1, t1
%    elif src == "l":
       movgr2fr.d ft1, t1
%#:
    $instr                     // read operand (from t1|ft0), write result (to t1|ft0)
                               // do not clobber t2!
%  if ftg:
%    if dst == "w":
       movfr2gr.s t1, ft1
%    elif dst == "l":
       movfr2gr.d t1, ft1

1:

%  if dst == "w":
%    set_vreg("t1", "t2", z0="t0")   #  fp[A] := t1
%  elif dst == "l":
     SET_VREG_WIDE t1, t2, z0=t0     // fp[A] := t1
%  elif dst == "s":
%    set_vreg_float("ft0", "t2", z0="t0")  # fp[A] := ft0
%  elif dst == "d":
     SET_VREG_DOUBLE ft0, t2, z0=t0  // fp[B] := ft0
%  else:
%    assert false, dst
%#:

   GET_INST_OPCODE t0         // t0 holds next opcode
   GOTO_OPCODE t0             // continue to next


//
// fbinop vAA, vBB, vCC
// Format 23x: AA|op CC|BB
//

// add-float vAA, vBB, vCC
// Format 23x: AA|a6 CC|BB
%def op_add_float():
%  generic_fbinop(instr="fadd.s fa0, fa0, fa1")

// sub-float vAA, vBB, vCC
// Format 23x: AA|a7 CC|BB
%def op_sub_float():
%  generic_fbinop(instr="fsub.s fa0, fa0, fa1")

// mul-float vAA, vBB, vCC
// Format 23x: AA|a8 CC|BB
%def op_mul_float():
%  generic_fbinop(instr="fmul.s fa0, fa0, fa1")

// div-float vAA, vBB, vCC
// Format 23x: AA|a9 CC|BB
%def op_div_float():
%  generic_fbinop(instr="fdiv.s fa0, fa0, fa1")

// rem-float vAA, vBB, vCC
// Format 23x: AA|aa CC|BB
// Note: Floating point remainder after division. This function is different than IEEE 754 remainder
// and is defined as result == a - roundTowardZero(a / b) * b.
// Note: LOONGARCH does not offer floating point remainder; use fmodf in libm.
%def op_rem_float():
%  generic_fbinop(instr="bl fmodf")

// add-double vAA, vBB, vCC
// Format 23x: AA|ab CC|BB
%def op_add_double():
%  generic_fbinop(instr="fadd.d fa0, fa0, fa1", is_double=True)

// sub-double vAA, vBB, vCC
// Format 23x: AA|ac CC|BB
%def op_sub_double():
%  generic_fbinop(instr="fsub.d fa0, fa0, fa1", is_double=True)

// mul-double vAA, vBB, vCC
// Format 23x: AA|ad CC|BB
%def op_mul_double():
%  generic_fbinop(instr="fmul.d fa0, fa0, fa1", is_double=True)

// div-double vAA, vBB, vCC
// Format 23x: AA|ae CC|BB
%def op_div_double():
%  generic_fbinop(instr="fdiv.d fa0, fa0, fa1", is_double=True)

// rem-double vAA, vBB, vCC
// Format 23x: AA|af CC|BB
// Note: Floating point remainder after division. This function is different than IEEE 754 remainder
// and is defined as result == a - roundTowardZero(a / b) * b.
// Note: LOONGARCH does not offer floating point remainder; use fmod in libm.
%def op_rem_double():
%  generic_fbinop(instr="bl fmod", is_double=True)

// fbinop boilerplate
// instr: operands held in fa0 and fa1, result written to fa0
// instr may be a libm call, so:
//  - avoid caller-save state across instr; s8 is used instead.
//  - fa0 and fa1 are used instead of ft0 and ft1.
//
// The is_double flag ensures vregs are read and written in 64-bit widths.
// Clobbers: t0, t1, fa0, fa1, s8
%def generic_fbinop(instr, is_double=False):
   FETCH t0, count=1     // t0 := CC|BB
   srli.w s8, xINST, 8   // s8 := AA
   srli.w t1, t0, 8       // t1 := CC
   andi t0, t0, 0xFF      // t0 := BB
%  get_vreg_float("fa1", "t1", is_double=is_double)
                         // fa1 := fp[CC]
%  get_vreg_float("fa0", "t0", is_double=is_double)
                         // fa0 := fp[BB]
   FETCH_ADVANCE_INST 2  // advance xPC, load xINST
   $instr                // read fa0 and fa1, write result to fa0.
                         // instr may be a function call.
%  set_vreg_float("fa0", "s8", z0="t0", is_double=is_double)
                         // fp[AA] := fa0
   GET_INST_OPCODE t0    // t0 holds next opcode
   GOTO_OPCODE t0        // continue to next


//
// fbinop/2addr vA, vB
// Format 12x: B|A|op
//

// add-float/2addr vA, vB
// Format 12x: B|A|c6
%def op_add_float_2addr():
%  generic_fbinop_2addr(instr="fadd.s fa0, fa0, fa1")

// sub-float/2addr vA, vB
// Format 12x: B|A|c7
%def op_sub_float_2addr():
%  generic_fbinop_2addr(instr="fsub.s fa0, fa0, fa1")

// mul-float/2addr vA, vB
// Format 12x: B|A|c8
%def op_mul_float_2addr():
%  generic_fbinop_2addr(instr="fmul.s fa0, fa0, fa1")

// div-float/2addr vA, vB
// Format 12x: B|A|c9
%def op_div_float_2addr():
%  generic_fbinop_2addr(instr="fdiv.s fa0, fa0, fa1")

// rem-float/2addr vA, vB
// Format 12x: B|A|ca
// Note: Floating point remainder after division. This function is different than IEEE 754 remainder
// and is defined as result == a - roundTowardZero(a / b) * b.
// Note: LOONGARCH does not offer floating point remainder; use fmodf in libm.
%def op_rem_float_2addr():
%  generic_fbinop_2addr(instr="bl fmodf")

// add-double/2addr vA, vB
// Format 12x: B|A|cb
%def op_add_double_2addr():
%  generic_fbinop_2addr(instr="fadd.d fa0, fa0, fa1", is_double=True)

// sub-double/2addr vA, vB
// Format 12x: B|A|cc
%def op_sub_double_2addr():
%  generic_fbinop_2addr(instr="fsub.d fa0, fa0, fa1", is_double=True)

// mul-double/2addr vA, vB
// Format 12x: B|A|cd
%def op_mul_double_2addr():
%  generic_fbinop_2addr(instr="fmul.d fa0, fa0, fa1", is_double=True)

// div-double/2addr vA, vB
// Format 12x: B|A|ce
%def op_div_double_2addr():
%  generic_fbinop_2addr(instr="fdiv.d fa0, fa0, fa1", is_double=True)

// rem-double/2addr vA, vB
// Format 12x: B|A|cf
// Note: Floating point remainder after division. This function is different than IEEE 754 remainder
// and is defined as result == a - roundTowardZero(a / b) * b.
// Note: LOONGARCH does not offer floating point remainder; use fmod in libm.
%def op_rem_double_2addr():
%  generic_fbinop_2addr(instr="bl fmod", is_double=True)

// fbinop/2addr boilerplate
// instr: operands held in fa0 and fa1, result written to fa0
// instr may be a libm call, so:
//  - avoid caller-save state across instr; s8 is used instead.
//  - use fa0 and fa1 instead of ft0 and ft1.
//
// The is_double flag ensures vregs are read and written in 64-bit widths.
// Clobbers: t0, t1, fa0, fa1, s8
%def generic_fbinop_2addr(instr, is_double=False):
   srli.w t0, xINST, 8       // t0 := B|A
   srli.w t1, xINST, 12      // t1 := B
   andi t0, t0, 0xF          // t0 := A
%  get_vreg_float("fa1", "t1", is_double=is_double)
                             // fa1 := fp[B]
   move s8, t0               // s8 := A
%  get_vreg_float("fa0", "t0", is_double=is_double)
                             // fa0 := fp[A]
   FETCH_ADVANCE_INST 1     // advance xPC, load xINST
   $instr                   // read fa0 and f1, write result to fa0.
                            // instr may be a function call.
   GET_INST_OPCODE t1       // t1 holds next opcode
%  set_vreg_float("fa0", "s8", z0="t0", is_double=is_double)
                            // fp[A] := fa0
   GOTO_OPCODE t1           // continue to next


%def op_cmpl_float():
    break 0x5

%def op_cmpg_float():
    break 0x5

%def op_cmpl_double():
    break 0x5

%def op_cmpg_double():
    break 0x5
