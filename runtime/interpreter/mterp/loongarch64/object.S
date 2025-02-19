%def op_check_cast():
    break 0x5

%def op_check_cast_slow_path():
    break 0x5

%def op_instance_of():
    break 0x5

%def op_instance_of_slow_path():
    break 0x5

%def op_iget_boolean():
    break 0x5

%def op_iget_byte():
    break 0x5

%def op_iget_char():
    break 0x5

%def op_iget_short():
    break 0x5

%def op_iget(load="", wide="", is_object=""):
    break 0x5

%def op_iget_slow_path(load, wide, is_object):
    break 0x5

%def op_iget_wide():
    break 0x5

%def op_iget_object():
    break 0x5

%def op_iput_boolean():
    break 0x5

%def op_iput_byte():
    break 0x5

%def op_iput_char():
    break 0x5

%def op_iput_short():
    break 0x5

%def op_iput(store="", wide="", is_object=""):
    break 0x5

%def op_iput_slow_path(store, wide, is_object):
    break 0x5

%def op_iput_wide():
    break 0x5

%def op_iput_object():
    break 0x5

%def op_sget_boolean():
    break 0x5

%def op_sget_byte():
    break 0x5

%def op_sget_char():
    break 0x5

%def op_sget_short():
    break 0x5

%def op_sget(load="", wide="", is_object=""):
    break 0x5

%def op_sget_slow_path(load, wide, is_object):
    break 0x5

%def op_sget_wide():
    break 0x5

%def op_sget_object():
    break 0x5

%def op_sput_boolean():
    break 0x5

%def op_sput_byte():
    break 0x5

%def op_sput_char():
    break 0x5

%def op_sput_short():
    break 0x5


%def store(src, dst, width):
%  if width == 8:
     st.b $src, $dst, 0
%  elif width == 16:
     st.h $src, $dst, 0
%  elif width == 32:
     st.w $src, $dst, 0
%  elif width == 64:
     st.d $src, $dst, 0
%  else:
%    assert False, width
%#:


.macro CLEAR_STATIC_VOLATILE_MARKER reg
    // andi ~0x1
    srli.d \reg, \reg, 1
    slli.d \reg, \reg, 1
.endm


// sput vAA, field@BBBB
// Format 21c: AA|67 BBBB
// klass.field := vAA
%def op_sput(width=32):
   srli.w t2, xINST, 8  // t2 := AA
%  get_vreg("s7", "t2", width=width)
                       // s7 := value, held across slow path call

   // Fast path: NterpGetStaticField's resolved_field from thread-local cache.
   // Stores cache value in a0 to match slow path's return from NterpGetStaticField.
   FETCH_FROM_THREAD_CACHE /*resolved_field*/a0, .L${opcode}_slow, t0, t1
.L${opcode}_slow_resume:

   ld.wu t0, a0, ART_FIELD_OFFSET_OFFSET
   ld.wu a0, a0, ART_FIELD_DECLARING_CLASS_OFFSET  // a0 := holder
   TEST_IF_MARKING t1, .L${opcode}_mark
.L${opcode}_mark_resume:

   add.d t0, t0, a0  // t0 := field addr, after possible a0 update
   FETCH_ADVANCE_INST 2
%  store(src="s7", dst="t0", width=width)
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, holder
   b .L${opcode}_mark_resume

.L${opcode}_slow:
   move a0, xSELF
   ld.d a1, sp, 0
   move a2, xPC
   move a3, zero
   EXPORT_PC
   bl nterp_get_static_field  // result a0 := resolved_field

   // Test for volatile bit
   slli.d t0, a0, 63
   bgez t0, .L${opcode}_slow_resume
%  volatile_path = add_slow_path(op_sput_volatile, width, "s7", "t0", "t1")
   b $volatile_path
   //jirl zero, t1, 0


// Volatile static store.
// Temporaries: z0, z1
%def op_sput_volatile(width, value, z0, z1):
   CLEAR_STATIC_VOLATILE_MARKER a0
   ld.wu $z0, a0, ART_FIELD_OFFSET_OFFSET          // z0 := field offset
   ld.wu a0, a0, ART_FIELD_DECLARING_CLASS_OFFSET  // a0 := holder
   TEST_IF_MARKING $z1, .L${opcode}_volatile_mark
.L${opcode}_volatile_mark_resume:

   add.d $z0, $z0, a0                              // z0 := field addr, after possible a0 update
   // Ensure the volatile store is released.
%  if width == 8:
     amswap.b zero, $value, $z0
%  elif width == 16:
     amswap.h zero, $value, $z0
%  elif width == 32:
     amswap.w zero, $value, $z0
%  elif width == 64:
     amswap.d zero, $value, $z0
%  else:
%    assert False, width
%#:

   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_volatile_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, holder
   b .L${opcode}_volatile_mark_resume


// sput-object vAA, field@BBBB
// Format 21c: AA|69 BBBB
%def op_sput_object():
   srli.w s7, xINST, 8      // s7 := AA (live through slow path)
%  get_vreg("s8", "s7", is_unsigned=True)  # s8 := reference, replaced in slow path

   // Fast path: NterpGetStaticField's resolved_field from thread-local cache.
   // Stores cache value in a0 to match slow path's return from NterpGetStaticField.
   FETCH_FROM_THREAD_CACHE /*resolved_field*/a0, .L${opcode}_slow, t0, t1
.L${opcode}_slow_resume:   // s8 := reference (slow path only)

   ld.wu t0, a0, ART_FIELD_OFFSET_OFFSET
   ld.wu a0, a0, ART_FIELD_DECLARING_CLASS_OFFSET  // a0 := holder
   TEST_IF_MARKING t1, .L${opcode}_mark
.L${opcode}_mark_resume:

   add.d t0, t0, a0          // t0 := field addr, after possible a0 update
   POISON_HEAP_REF s8      // Poisoning maps null to null for the null check in write barrier.
   st.w s8, t0, 0          // store reference
%  object_write_barrier(value="s8", holder="a0", z0="t0", z1="t1", uniq=f"{opcode}")
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, holder
   b .L${opcode}_mark_resume

.L${opcode}_slow:
%  slow_path = add_slow_path(op_sput_object_slow_path, "s7", "s8", "t0", "t1")
   b $slow_path  // slow path offset exceeds regular branch imm in FETCH_FROM_THREAD_CACHE
   //jirl zero, t1, 0


// Static store, object variant. Contains both slow path and volatile path
// due to handler size limitation in op_sput_object.
// Hardcoded: a0, a1, a2, a3, xSELF, xPC, xINST, xFP, xREFS
// Temporaries z0, z1
%def op_sput_object_slow_path(src_vreg, value, z0, z1):
   // Args for nterp_get_static_field
   move a0, xSELF
   ld.d a1, sp, 0
   move a2, xPC
   move a3, $value
   EXPORT_PC
   bl nterp_get_static_field  // result a0 := resolved_field

   // Reload value, it may have moved.
%  get_vreg(value, src_vreg, is_unsigned=True)  # value := fp[AA], zext

   // Test for volatile bit
   slli.d $z0, a0, 63
   blt $z0, zero, .L${opcode}_volatile
   b .L${opcode}_slow_resume  // resume offset exceeds branch imm

.L${opcode}_volatile:
   CLEAR_STATIC_VOLATILE_MARKER a0
   ld.wu $z0, a0, ART_FIELD_OFFSET_OFFSET          // z0 := field offset
   ld.wu a0, a0, ART_FIELD_DECLARING_CLASS_OFFSET  // a0 := holder
   TEST_IF_MARKING $z1, .L${opcode}_volatile_mark
.L${opcode}_volatile_mark_resume:

   add.d $z0, $z0, a0  // z0 := field addr, after possible a0 update
   // Ensure the volatile store is released.
   // \value must NOT be the destination register, the destination gets clobbered!
   // \value's original value is used in the write barrier below.
   POISON_HEAP_REF $value  // Poisoning maps null to null for the null check in write barrier.
   amswap.w zero, $value, $z0
%  object_write_barrier(value=value, holder="a0", z0=z0, z1=z1, uniq=f"slow_{opcode}")

   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_volatile_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, holder
   b .L${opcode}_volatile_mark_resume


%def object_write_barrier(value, holder, z0, z1, uniq):
   beqz $value, .L${uniq}_skip_write_barrier  // No object, skip out.
   ld.d $z0, xSELF, THREAD_CARD_TABLE_OFFSET
   srli.d $z1, $holder, CARD_TABLE_CARD_SHIFT
   add.d $z1, $z0, $z1
   st.b $z0, $z1, 0
.L${uniq}_skip_write_barrier:


%def op_sput_slow_path(store, wide, is_object):
    break 0x5

%def op_sput_wide():
    break 0x5

%def op_new_instance():
    break 0x5
