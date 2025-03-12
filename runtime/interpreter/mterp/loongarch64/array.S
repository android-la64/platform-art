// array-length vA, vB
// Format 12x: B|A|21
// Store in the given destination register the length of the indicated array, in entries
%def op_array_length():
   srli.w t0, xINST, 12     // t0 := B
   GET_VREG_OBJECT t0, t0  // t0 := refs[B]
   beqz t0, 1f
   srli.w t1, xINST, 8      // t1 := B|A
   FETCH_ADVANCE_INST 1
   andi t1, t1, 0xF        // t1 := A
   GET_INST_OPCODE t3
   ld.w t2, t0, MIRROR_ARRAY_LENGTH_OFFSET
%  set_vreg("t2", "t1", z0="t0")
   GOTO_OPCODE t3
1:
   b common_errNullObject


// new-array vA, vB, type@CCCC
// Format 22c: B|A|23 CCCC
// Construct a new array of the indicated type and size. The type must be an array type.
%def op_new_array():
   EXPORT_PC
   srli.w s8, xINST, 8   // s8 := B|A
   srli.w s7, xINST, 12  // s7 := B
   andi s8, s8, 0xF      // s8 := A
   FETCH_FROM_THREAD_CACHE /*resolved klass*/a0, .L${opcode}_miss, t0, t1
   TEST_IF_MARKING t0, .L${opcode}_mark
.L${opcode}_resume:

%  get_vreg("a1", "s7")  # a1 := fp[B] length
   ld.d t0, xSELF, THREAD_ALLOC_ARRAY_ENTRYPOINT_OFFSET
   jirl ra, t0, 0     // args a0 (klass), a1 (length)
                      // return a0 := new-array
   la.local t5, NTERP_NEW_ARRAY_RESUME_HELPER
   jirl zero, t5, 0

.L${opcode}_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, klass
   b .L${opcode}_resume

.L${opcode}_miss:
   EXPORT_PC
   move a0, xSELF
   ld.d a1, sp, 0  // caller ArtMethod*
   move a2, xPC
   bl nterp_get_class
   b .L${opcode}_resume


// filled-new-array {vC, vD, vE, vF, vG}, type@BBBB
// Format 35c: A|G|24 BBBB F|E|D|C
// Construct an array of the given type and size, filling it with the supplied contents. The type
// must be an array type. The array's contents must be single-word (that is, no arrays of long or
// double, but reference types are acceptable). The constructed instance is stored as a "result" in
// the same way that the method invocation instructions store their results, so the constructed
// instance must be moved to a register with an immediately subsequent move-result-object
// instruction (if it is to be used).
%def op_filled_new_array(is_range=False):
   EXPORT_PC
   move a0, xSELF
   ld.d a1, sp, 0  // a1 := caller ArtMethod*
   move a2, xFP    // a2 := vreg array
   move a3, xPC
%  if is_range:
     bl nterp_filled_new_array_range  // args a0, a1, a2, a3
%  else:
     bl nterp_filled_new_array  // args a0, a1, a2, a3
%#:
   FETCH_ADVANCE_INST 3
   GET_INST_OPCODE t0
   GOTO_OPCODE t0


// filled-new-array/range {vCCCC .. vNNNN}, type@BBBB
// where NNNN = CCCC + AA - 1
// Format 3rc: AA|25 BBBB CCCC
// Construct an array of the given type and size, filling it with the supplied contents.
// Clarifications and restrictions are the same as filled-new-array, described above.
%def op_filled_new_array_range():
%  op_filled_new_array(is_range=True)


// fill-array-data vAA, +BBBBBBBB
// Format 31t: AA|26 BBBB(lo) BBBB(hi)
// Fill the given array with the indicated data. The reference must be to an array of primitives,
// and the data table must match it in type and must contain no more elements than will fit in the
// array. That is, the array may be larger than the table, and if so, only the initial elements of
// the array are set, leaving the remainder alone.
%def op_fill_array_data():
   EXPORT_PC
   srli.w t0, xINST, 8     // t0 := AA
   FETCH t1, count=1, signed=1, width=32
                           // t1 := ssssssssBBBBBBBB
   GET_VREG_OBJECT a1, t0  // a1 := refs[AA] (array ref)
   // +BBBBBBBB offset is in code units. Multiply by 2 for byte offset against dex PC.
   alsl.d a0, t1, xPC, 1   // a0 := payload address
   bl art_quick_handle_fill_data  // args a0, a1
   FETCH_ADVANCE_INST 3
   GET_INST_OPCODE t0
   GOTO_OPCODE t0


%def op_aget(load="break 0x5", shift="2", data_offset="MIRROR_INT_ARRAY_DATA_OFFSET", wide="0", is_object="0"):
    break 0x5

%def op_aget_boolean():
%  op_aget(load="break 0x5", shift="0", data_offset="MIRROR_BOOLEAN_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_byte():
%  op_aget(load="break 0x5", shift="0", data_offset="MIRROR_BYTE_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_char():
%  op_aget(load="break 0x5", shift="1", data_offset="MIRROR_CHAR_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_object():
%  op_aget(load="break 0x5", shift="2", data_offset="MIRROR_OBJECT_ARRAY_DATA_OFFSET", wide="0", is_object="1")

%def op_aget_short():
%  op_aget(load="break 0x5", shift="1", data_offset="MIRROR_SHORT_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_wide():
%  op_aget(load="break 0x5", shift="3", data_offset="MIRROR_WIDE_ARRAY_DATA_OFFSET", wide="1", is_object="0")

%def op_aput(store="break 0x5", shift="2", data_offset="MIRROR_INT_ARRAY_DATA_OFFSET", wide="0", is_object="0"):
    break 0x5

%def op_aput_boolean():
%  op_aput(store="break 0x5", shift="0", data_offset="MIRROR_BOOLEAN_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_byte():
%  op_aput(store="break 0x5", shift="0", data_offset="MIRROR_BYTE_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_char():
%  op_aput(store="break 0x5", shift="1", data_offset="MIRROR_CHAR_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_short():
%  op_aput(store="break 0x5", shift="1", data_offset="MIRROR_SHORT_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_wide():
%  op_aput(store="break 0x5", shift="3", data_offset="MIRROR_WIDE_ARRAY_DATA_OFFSET", wide="1", is_object="0")

%def op_aput_object():
%  op_aput(store="break 0x5", shift="2", data_offset="MIRROR_OBJECT_ARRAY_DATA_OFFSET", wide="0", is_object="1")
