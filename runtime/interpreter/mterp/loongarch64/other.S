%def unused():
    break 0x1
%def op_const():
    break 0x5

%def op_const_16():
    break 0x5

// const/4 vA, #+B
// Format 11n: B|A|12
// Clobbers: t0, t1, t2, t3
%def op_const_4():
   slli.w t1, xINST, 16     // B as MSB of word
   srai.w t1, t1, 28        // t1 := sssssssB
   slli.w t2, xINST, 20     // A as MSB of word
   srli.w t2, t2, 28        // t2 := A
   FETCH_ADVANCE_INST 1    // advance xPC, load xINST
   GET_INST_OPCODE t3      // t3 holds next opcode
%  set_vreg("t1", "t2", z0="t0")  # fp[A] := sssssssB
   GOTO_OPCODE t3          // continue to next

%def op_const_high16():
    break 0x5
%def op_const_object(jumbo="0", helper="nterp_load_object"):
    break 0x5
%def op_const_class():
    break 0x5
%def op_const_method_handle():
    break 0x5
%def op_const_method_type():
    break 0x5
%def op_const_string():
    break 0x5
%def op_const_string_jumbo():
    break 0x5
%def op_const_wide():
    break 0x5
%def op_const_wide_16():
    break 0x5
%def op_const_wide_32():
    break 0x5
%def op_const_wide_high16():
    break 0x5
%def op_monitor_enter():
    break 0x5
%def op_monitor_exit():
    break 0x5
%def op_move(is_object="0"):
    break 0x5
%def op_move_16(is_object="0"):
    break 0x5
%def op_move_exception():
    break 0x5
%def op_move_from16(is_object="0"):
    break 0x5
%def op_move_object():
    break 0x5
%def op_move_object_16():
    break 0x5
%def op_move_object_from16():
    break 0x5
%def op_move_result(is_object="0"):
    break 0x5
%def op_move_result_object():
    break 0x5
%def op_move_result_wide():
    break 0x5
%def op_move_wide():
    break 0x5
%def op_move_wide_16():
    break 0x5
%def op_move_wide_from16():
    break 0x5

%def op_nop():
    break 0x5

%def op_unused_3e():
%  unused()

%def op_unused_3f():
%  unused()

%def op_unused_40():
%  unused()

%def op_unused_41():
%  unused()

%def op_unused_42():
%  unused()

%def op_unused_43():
%  unused()

%def op_unused_73():
%  unused()

%def op_unused_79():
%  unused()

%def op_unused_7a():
%  unused()

%def op_unused_e3():
%  unused()

%def op_unused_e4():
%  unused()

%def op_unused_e5():
%  unused()

%def op_unused_e6():
%  unused()

%def op_unused_e7():
%  unused()

%def op_unused_e8():
%  unused()

%def op_unused_e9():
%  unused()

%def op_unused_ea():
%  unused()

%def op_unused_eb():
%  unused()

%def op_unused_ec():
%  unused()

%def op_unused_ed():
%  unused()

%def op_unused_ee():
%  unused()

%def op_unused_ef():
%  unused()

%def op_unused_f0():
%  unused()

%def op_unused_f1():
%  unused()

%def op_unused_f2():
%  unused()

%def op_unused_f3():
%  unused()

%def op_unused_f4():
%  unused()

%def op_unused_f5():
%  unused()

%def op_unused_f6():
%  unused()

%def op_unused_f7():
%  unused()

%def op_unused_f8():
%  unused()

%def op_unused_f9():
%  unused()

%def op_unused_fc():
%  unused()

%def op_unused_fd():
%  unused()

