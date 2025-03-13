// check-cast vAA, type@BBBB
// Format 21c: AA|1f BBBB
// Throw a ClassCastException if the reference in the given register cannot be cast to the indicated
// type.
%def op_check_cast():
   FETCH_FROM_THREAD_CACHE /*expected klass*/a1, .L${opcode}_miss, t0, t1
.L${opcode}_miss_resume:

   srli.w t0, xINST, 8         // t0 := AA
%  get_vreg("a0", "t0", is_unsigned=True)  # a0 := fp[AA], zext
   beqz a0, .L${opcode}_next  // null
   ld.wu a2, a0, MIRROR_OBJECT_CLASS_OFFSET  // a2 := actual klass
   UNPOISON_HEAP_REF a2
   // Fast path: compare without read barrier.
   bne a1, a2, .L${opcode}_slow

.L${opcode}_next:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_miss:
   EXPORT_PC
   move a0, xSELF
   ld.d a1, sp, 0  // caller ArtMethod*
   move a2, xPC
   bl nterp_get_class
   move a1, a0
   b .L${opcode}_miss_resume

.L${opcode}_slow:
   // A0 and A1 in position for quick call.
%  slow_path = add_slow_path(op_check_cast_slow_path, "t0", "t1", "t2")
   b $slow_path  // slow offset exceeds branch imm
                    // args a0, a1, a2


.macro CLEAR_STATIC_VOLATILE_MARKER reg
    bstrins.d \reg, zero, 0, 0
.endm


// Checks cases for (1) interface, (2) array, and (3) super classes.
// Hardcoded: a0 (obj), a1 (expected klass), a2 (actual klass)
//
// Note. We don't do read barriers for simplicity. However, this means that fetched objects may be a
// from-space reference. That's OK as we only fetch constant information from the references. This
// also means that some of the comparisons below may lead to false negative due to stale data, so
// all negative cases must pass through the runtime, via potential read barrier.
%def op_check_cast_slow_path(z0, z1, z2):
   // Interface check: cut to runtime.
   ld.wu $z0, a1, MIRROR_CLASS_ACCESS_FLAGS_OFFSET
   BRANCH_IF_BIT_SET $z0, $z0, MIRROR_CLASS_IS_INTERFACE_FLAG_BIT, .L${opcode}_runtime

   // Array check handled below.
   ld.wu $z0, a1, MIRROR_CLASS_COMPONENT_TYPE_OFFSET
   // Defer z0 unpoison to array path.
   bnez $z0, .L${opcode}_array

   // Super check: find expected class, else cut to runtime.
.L${opcode}_super:
   ld.wu a2, a2, MIRROR_CLASS_SUPER_CLASS_OFFSET
   UNPOISON_HEAP_REF a2
   beq a2, a1, .L${opcode}_slow_next
   bnez a2, .L${opcode}_super

.L${opcode}_runtime:
   TEST_IF_MARKING $z0, .L${opcode}_mark
.L${opcode}_mark_resume:
   EXPORT_PC
   bl art_quick_check_instance_of  // args a0 (obj), a1 (expected klass)

   // Advancement logic replicated here for branch distance.
.L${opcode}_slow_next:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_mark:
   bl art_quick_read_barrier_mark_reg11  // a1, expected klass
   b .L${opcode}_mark_resume

.L${opcode}_array:
   UNPOISON_HEAP_REF $z0          // z0 = expected.component
   ld.wu $z1, a2, MIRROR_CLASS_COMPONENT_TYPE_OFFSET  // z1 := actual.component
   beqz $z1, .L${opcode}_runtime  // null: actual not an array
   UNPOISON_HEAP_REF $z1
   ld.wu $z2, $z0, MIRROR_CLASS_SUPER_CLASS_OFFSET  // z2 := expected.component.super
   // z2 can skip unpoison for null check
   bnez $z2, .L${opcode}_runtime  // super type exists
   // expected.component.super is null: expected is either Object[] or primitive array.
   ld.hu $z2, $z0, MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET  // z2 := expected.component.primitive
   bnez $z2, .L${opcode}_runtime  // expected's component is primitive
   ld.wu $z2, $z1, MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET  // z2 := actual.component.primitive
   bnez $z2, .L${opcode}_runtime  // actual's component is primitive
   // Here, z0 is Object, and z1 is a subclass of Object.
   b .L${opcode}_slow_next


// instance-of vA, vB, type@CCCC
// Format 22c: B|A|20 CCCC
// vA := 1 if vB instance-of CCCC, else 0
// Store in the given destination register 1 if the indicated reference is an instance of the given
// type, or 0 if not.
%def op_instance_of():
   srli.w s7, xINST, 8    // s7 := B|A
   srli.w s8, xINST, 12   // s8 := B
   andi s7, s7, 0xF      // s7 := A, used in slow path
   FETCH_FROM_THREAD_CACHE /*expected klass*/ a1, .L${opcode}_miss, t0, t1
.L${opcode}_miss_resume:

%  get_vreg("a0", "s8", is_unsigned=True)  # a0 := fp[B], zext
   beqz a0, .L${opcode}_next  // a0 = null = dst value "false"
   ld.wu a2, a0, MIRROR_OBJECT_CLASS_OFFSET  // a2 := actual klass
   UNPOISON_HEAP_REF a2
   // Fast path: compare without read barrier.
   bne a1, a2, .L${opcode}_slow

   li.d a0, 1  // dst value "true"

.L${opcode}_next:
   la.local t5, NTERP_INSTANCE_OF_HELPER
   jr t5

.L${opcode}_miss:
   EXPORT_PC
   move a0, xSELF
   ld.d a1, sp, 0  // caller ArtMethod*
   move a2, xPC
   bl nterp_get_class
   move a1, a0
   b .L${opcode}_miss_resume

.L${opcode}_slow:
   // A0 and A1 in position for quick call.
%  slow_path = add_slow_path(op_instance_of_slow_path, "s7", "t0", "t1", "t2")
   b $slow_path  // slow offset exceeds branch imm
                    // args a0, a1, a2


// Checks cases for (1) interface, (2) array, and (3) super classes.
// Hardcoded: a0 (obj), a1 (expected klass), a2 (actual klass)
//
// Npte. If marking, don't bother with read barrier calls - cut to runtime. This arrangement allows
// the (non marking) super class fast path's negative case to skip the read barrier and runtime
// call, and correctly diagnose the situation with fp[A] := 0.
%def op_instance_of_slow_path(vA, z0, z1, z2):
   TEST_IF_MARKING $z0, .L${opcode}_runtime_with_read_barrier

   // Interface check: cut to runtime.
   ld.wu $z0, a1, MIRROR_CLASS_ACCESS_FLAGS_OFFSET
   BRANCH_IF_BIT_SET $z0, $z0, MIRROR_CLASS_IS_INTERFACE_FLAG_BIT, .L${opcode}_runtime

   // Array check handled below.
   ld.wu $z0, a1, MIRROR_CLASS_COMPONENT_TYPE_OFFSET
   // Defer z0 unpoison to array path.
   bnez $z0, .L${opcode}_array

   // Super check: find klass up the hierarchy.
.L${opcode}_super:
   ld.wu a2, a2, MIRROR_CLASS_SUPER_CLASS_OFFSET
   UNPOISON_HEAP_REF a2
   beq a2, a1, .L${opcode}_super_exit
   bnez a2, .L${opcode}_super
.L${opcode}_super_exit:
   sltu a0, zero, a2  // a0 := 1 if (a1 = a2 != null), else 0 (because a2 = null)

.L${opcode}_slow_next:
%  set_vreg("a0", vA, z0=z0)  # fp[A] := a0
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_runtime_with_read_barrier:
   bl art_quick_read_barrier_mark_reg11  // a1, expected klass
.L${opcode}_runtime:
   EXPORT_PC
   bl artInstanceOfFromCode  // args a0 (obj), a1 (expected klass)
       // return a0: 1 if true, else 0
   b .L${opcode}_slow_next

.L${opcode}_array:
   UNPOISON_HEAP_REF $z0           // z0 = expected.component
   ld.wu $z1, $z0, MIRROR_CLASS_SUPER_CLASS_OFFSET  // z1 := expected.component.super
   // z1 can skip unpoison for null check
   bnez $z1, .L${opcode}_runtime   // super type exists
   // Here, expected.component.super is null: expected is either Object[] or primitive array.
   ld.wu a0, a2, MIRROR_CLASS_COMPONENT_TYPE_OFFSET  // a0 := actual.component
   beqz a0, .L${opcode}_slow_next  // actual not an array, a0 = null = dst value "false"
   UNPOISON_HEAP_REF a0
   ld.hu $z1, $z0, MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET  // z1 := expected.component.primitive
   ld.hu $z2, a0, MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET   // z2 := actual.component.primitive
   or a0, $z1, $z2  // a0 := 0 if z1 = z2 = 0, else non-zero (Primitive::Type enum)
   sltui a0, a0, 1  // a0 := 1 if both are class types, else 0
   // Here, when a0 = 1, expected.component is Object, and actual.component is a subclass of Object.
   b .L${opcode}_slow_next


// new-instance vAA, type@BBBB
// Format 21c: AA|22 BBBB
// Construct a new instance of the indicated type, storing a reference to it in the destination. The
// type must refer to a non-array class.
%def op_new_instance():
   EXPORT_PC
   srli.w s7, xINST, 8  // s7 := AA
   FETCH_FROM_THREAD_CACHE /*resolved klass*/a0, .L${opcode}_miss, t0, t1
   TEST_IF_MARKING t0, .L${opcode}_mark
.L${opcode}_mark_resume:

   ld.d t0, xSELF, THREAD_ALLOC_OBJECT_ENTRYPOINT_OFFSET
   jirl ra, t0, 0     // arg a0 (klass)
               // return a0 := new-obj
   dbar 0xa    // fence w, w; main.S has details

.L${opcode}_miss_resume:
   SET_VREG_OBJECT a0, s7, z0=t0  // refs[AA] := new-obj
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, klass
   b .L${opcode}_mark_resume

.L${opcode}_miss:
   EXPORT_PC
   move a0, xSELF
   ld.d a1, sp, 0  // caller ArtMethod*
   move a2, xPC
   bl nterp_allocate_object
       // return a0 := new-obj, plus cache entry is updated with the resolved klass
   b .L${opcode}_miss_resume


// *** sget ***

// sget vAA, field@BBBB
// Format 21c: AA|60 BBBB
// vAA := klass.field
%def op_sget(width=32, zext=False):
   srli.w s7, xINST, 8  // s7 := AA (live through volatile path)
   // Fast path: NterpGetStaticField's resolved_field from thread-local cache.
   // Stores cache value in a0 to match slow path's return from NterpGetStaticField.
   FETCH_FROM_THREAD_CACHE /*resolved_field*/a0, .L${opcode}_slow, t0, t1
.L${opcode}_slow_resume:

   ld.wu t0, a0, ART_FIELD_OFFSET_OFFSET           // t0 := field offset
   ld.wu a0, a0, ART_FIELD_DECLARING_CLASS_OFFSET  // a0 := holder
   TEST_IF_MARKING t2, .L${opcode}_mark
.L${opcode}_mark_resume:

   add.d t0, t0, a0      // t0 := field addr, after possible a0 update
%  load(dst="t1", src="t0", width=width, zext=zext)
                       // t1 := value
   FETCH_ADVANCE_INST 2
%  set_vreg("t1", "s7", z0="t0", width=width)
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, holder
   b .L${opcode}_mark_resume

.L${opcode}_slow:
   la.local t5, NTERP_SPUT_SLOW_HELPER
   jirl t5, t5, 0
   bl nterp_get_static_field  // result a0 := resolved_field

   // Test for volatile bit
   slli.d t0, a0, 63
   bgez t0, .L${opcode}_slow_resume
%  volatile_path = add_slow_path(op_sget_volatile, width, zext, "s7", "t0", "t1")
   b $volatile_path


// Volatile static load.
// Temporaries: z0, z1, z2
%def op_sget_volatile(width, zext, dst_vreg, z0, z1):
   CLEAR_STATIC_VOLATILE_MARKER a0
   ld.wu $z0, a0, ART_FIELD_OFFSET_OFFSET          // z0 := field offset
   ld.wu a0, a0, ART_FIELD_DECLARING_CLASS_OFFSET  // a0 := holder
   TEST_IF_MARKING $z1, .L${opcode}_volatile_mark
.L${opcode}_volatile_mark_resume:

   add.d $z0, $z0, a0  // z0 := field addr, after possible a0 update
   // Atomic load: "fence rw,rw ; LOAD ; fence r,rw"
   dbar 0x0
%  load(dst=z1, src=z0, width=width, zext=zext)
   dbar 0x4
       // z1 := value
   FETCH_ADVANCE_INST 2
%  set_vreg(z1, dst_vreg, z0=z0, width=width)
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_volatile_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, holder
   b .L${opcode}_volatile_mark_resume


// sget-wide vAA, field@BBBB
// Format 21c: AA|61 BBBB
%def op_sget_wide():
%  op_sget(width=64)


// sget-object vAA, field@BBBB
// Format 21c: AA|62 BBBB
// Variant for object load contains extra logic for GC mark.
%def op_sget_object():
   srli.w s7, xINST, 8  // s7 := AA (live through volatile path)
   // Fast path: NterpGetStaticField's resolved_field from thread-local cache.
   // Stores cache value in a0 to match slow path's return from NterpGetStaticField.
   FETCH_FROM_THREAD_CACHE /*resolved_field*/a0, .L${opcode}_slow, t0, t1
.L${opcode}_slow_resume:

   ld.wu t0, a0, ART_FIELD_OFFSET_OFFSET           // t0 := field offset
   ld.wu a0, a0, ART_FIELD_DECLARING_CLASS_OFFSET  // a0 := holder
   TEST_IF_MARKING t1, .L${opcode}_mark

   add.d t0, t0, a0      // t0 := field addr
   ld.wu a0, t0, 0        // a0 := value (ref)
   UNPOISON_HEAP_REF a0

.L${opcode}_mark_resume:
   FETCH_ADVANCE_INST 2
   SET_VREG_OBJECT a0, s7, z0=t0
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, holder
   add.d t0, t0, a0      // t0 := field addr, after a0 update
   ld.wu a0, t0, 0        // a0 := value (ref)
   UNPOISON_HEAP_REF a0
   bl art_quick_read_barrier_mark_reg10  // a0, object
   b .L${opcode}_mark_resume

.L${opcode}_slow:
%  slow_path = add_slow_path(op_sget_object_slow_path, "s7", "t0", "t1")
   b $slow_path


// Static load, object variant. Contains both slow path and volatile path
// due to handler size limitation in op_sget_object.
// Hardcoded: a0, a1, a2, a3, xSELF, xPC, xINST, xFP, xREFS
// Temporaries: z0, z1
%def op_sget_object_slow_path(dst_vreg, z0, z1):
   move a0, xSELF
   ld.d a1, sp, 0  // a1 := caller ArtMethod*
   move a2, xPC
   move a3, zero
   EXPORT_PC
   bl nterp_get_static_field  // result a0 := resolved_field

   // Test for volatile bit
   slli.d $z0, a0, 63
   bltz $z0, .L${opcode}_volatile
   b .L${opcode}_slow_resume  // resume offset exceeds branch imm

.L${opcode}_volatile:
   CLEAR_STATIC_VOLATILE_MARKER a0
   ld.wu $z0, a0, ART_FIELD_OFFSET_OFFSET          // z0 := field offset
   ld.wu a0, a0, ART_FIELD_DECLARING_CLASS_OFFSET  // a0 := holder
   TEST_IF_MARKING $z1, .L${opcode}_volatile_mark

   add.d $z0, $z0, a0  // z0 := field addr
   dbar 0x0
   ld.wu a0, $z0, 0  // Atomic ref load: "fence rw,rw, ; LOAD ; fence r,rw"
   dbar 0x4
   UNPOISON_HEAP_REF a0

.L${opcode}_volatile_mark_resume:
   FETCH_ADVANCE_INST 2
   SET_VREG_OBJECT a0, $dst_vreg, z0=$z0
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_volatile_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, holder
   add.d $z0, $z0, a0  // z0 := field addr, after a0 update
   dbar 0x0
   ld.wu a0, $z0, 0  // Atomic ref load: "fence rw,rw, ; LOAD ; fence r,rw"
   dbar 0x4
   UNPOISON_HEAP_REF a0
   bl art_quick_read_barrier_mark_reg10  // a0, object
   b .L${opcode}_volatile_mark_resume


// sget-boolean vAA, field@BBBB
// Format 21c: AA|63 BBBB
%def op_sget_boolean():
%  op_sget(width=8, zext=True)


// sget-byte vAA, field@BBBB
// Format 21c: AA|64 BBBB
%def op_sget_byte():
%  op_sget(width=8)


// sget-char vAA, field@BBBB
// Format 21c: AA|65 BBBB
%def op_sget_char():
%  op_sget(width=16, zext=True)


// sget-short vAA, field@BBBB
// Format 21c: AA|66 BBBB
%def op_sget_short():
%  op_sget(width=16)


// sput-object vAA, field@BBBB
// Format 21c: AA|6a BBBB
%def op_sput_boolean():
%  op_sput(width=8)


// sput-object vAA, field@BBBB
// Format 21c: AA|6b BBBB
%def op_sput_byte():
%  op_sput(width=8)


// sput-object vAA, field@BBBB
// Format 21c: AA|6c BBBB
%def op_sput_char():
%  op_sput(width=16)


// sput-object vAA, field@BBBB
// Format 21c: AA|6d BBBB
%def op_sput_short():
%  op_sput(width=16)


// sput-wide vAA, field@BBBB
// Format 21c: AA|68 BBBB
%def op_sput_wide():
%  op_sput(width=64)


// *** iget ***

%def load(dst, src, width, zext):
%  if width == 8 and zext:
     ld.bu $dst, $src, 0
%  elif width == 8:
     ld.b $dst, $src, 0
%  elif width == 16 and zext:
     ld.hu $dst, $src, 0
%  elif width == 16:
     ld.h $dst, $src, 0
%  elif width == 32:
     ld.w $dst, $src, 0
%  elif width == 64:
     ld.d $dst, $src, 0
%  else:
%    assert False, width
%#:


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


// iget vA, vB, field@CCCC
// Format 22c: B|A|52 CCCC
// vA := vB.field
%def op_iget(width=32, zext=False):
   srli.w s8, xINST, 8
   srli.w s7, xINST, 12     // s7 := B
   andi s8, s8, 0xF        // s8 := A

   // Fast path: NterpGetInstanceFieldOffset's byte offset from thread-local cache.
   FETCH_FROM_THREAD_CACHE /*field_offset*/a0, .L${opcode}_slow, t1, t2
.L${opcode}_slow_resume:

%  get_vreg("t0", "s7", is_unsigned=True)  # t0 := holder
   beqz t0, .L${opcode}_null
   add.d t0, a0, t0          // t0 := field addr
%  load(dst="t1", src="t0", width=width, zext=zext)
                           // t1 := value
   FETCH_ADVANCE_INST 2
%  set_vreg("t1", "s8", z0="t0", width=width)
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_slow:
   la.local t5, NTERP_IGET_SLOW_HELPER
   jirl t5, t5, 0
   bl nterp_get_instance_field_offset  // result a0 := field_offset

   // Test for volatile (negative value).
   bgez a0, .L${opcode}_slow_resume
%  volatile_path = add_slow_path(op_iget_volatile, width, zext, "s7", "s8", "t0", "t1")
   b $volatile_path

.L${opcode}_null:
   b common_errNullObject


%def op_iget_volatile(width, zext, holder, dst, z0, z1):
%  get_vreg(z0, holder, is_unsigned=True)  # z0 := holder
   beqz $z0, .L${opcode}_volatile_null
   sub.d $z0, $z0, a0           // z0 := field addr (holder - (-offset))
   // Atomic load: "fence rw,rw ; LOAD ; fence r,rw"
   dbar 0x0
%  load(dst=z1, src=z0, width=width, zext=zext)
   dbar 0x4
                              // t1 := value
   FETCH_ADVANCE_INST 2
%  set_vreg(z1, dst, z0=z0, width=width)
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_volatile_null:
   b common_errNullObject


// iget-wide vA, vB, field@CCCC
// Format 22c: B|A|53 CCCC
%def op_iget_wide():
%  op_iget(width=64)


// iget-object vA, vB, field@CCCC
// Format 22c: B|A|54 CCCC
%def op_iget_object():
   srli.w s8, xINST, 8
   srli.w s7, xINST, 12     // s7 := B
   andi s8, s8, 0xF        // s8 := A

   // Fast path: NterpGetInstanceFieldOffset's byte offset from thread-local cache.
   FETCH_FROM_THREAD_CACHE /*field_offset*/a0, .L${opcode}_slow, t1, t2
.L${opcode}_slow_resume:

%  get_vreg("t0", "s7", is_unsigned=True)  # t0 := holder
   beqz t0, .L${opcode}_null
   add.d t0, a0, t0          // t0 := field addr
   ld.wu a0, t0, 0            // a0 := object
   UNPOISON_HEAP_REF a0
   TEST_IF_MARKING t1, .L${opcode}_mark
.L${opcode}_mark_resume:

   FETCH_ADVANCE_INST 2
   SET_VREG_OBJECT a0, s8, z0=t0
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, object
   b .L${opcode}_mark_resume

.L${opcode}_slow:
%  slow_path = add_slow_path(op_iget_object_slow_path, "s7", "s8", "t0", "t1")
   b $slow_path

.L${opcode}_null:
   b common_errNullObject


%def op_iget_object_slow_path(holder, dst, z0, z1):
   move a0, xSELF
   ld.d a1, sp, 0  // a1 := caller ArtMethod*
   move a2, xPC
   move a3, zero
   EXPORT_PC
   bl nterp_get_instance_field_offset  // result a0 := field_offset

   // Test for volatile (negative value).
   bltz a0, .L${opcode}_volatile
   b .L${opcode}_slow_resume  // resume offset exceeds branch imm

.L${opcode}_volatile:
%  get_vreg(z0, holder, is_unsigned=True)  # z0 := holder
   beqz $z0, .L${opcode}_volatile_null
   sub.d $z0, $z0, a0           // z0 := field addr (holder - (-offset))
   // Atomic load: "fence rw,rw ; LOAD ; fence r,rw"
   dbar 0x0
   ld.wu a0, $z0, 0              // a0 := object
   dbar 0x4
   UNPOISON_HEAP_REF a0
   TEST_IF_MARKING t1, .L${opcode}_volatile_mark
.L${opcode}_volatile_mark_resume:

   FETCH_ADVANCE_INST 2
   SET_VREG_OBJECT a0, $dst, z0=$z0
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_volatile_mark:
   bl art_quick_read_barrier_mark_reg10  // a0, object
   b .L${opcode}_volatile_mark_resume

.L${opcode}_volatile_null:
   b common_errNullObject


// iget-boolean vA, vB, field@CCCC
// Format 22c: B|A|55 CCCC
%def op_iget_boolean():
%  op_iget(width=8, zext=True)


// iget-byte vA, vB, field@CCCC
// Format 22c: B|A|56 CCCC
%def op_iget_byte():
%  op_iget(width=8)


// iget-char vA, vB, field@CCCC
// Format 22c: B|A|57 CCCC
%def op_iget_char():
%  op_iget(width=16, zext=True)


// iget-short vA, vB, field@CCCC
// Format 22c: B|A|58 CCCC
%def op_iget_short():
%  op_iget(width=16)


// *** iput ***

// iput vA, vB, field@CCCC
// Format 22c: B|A|59 CCCC
// vB.field := vA
%def op_iput(width=32):
   srli.w s8, xINST, 8
   srli.w s7, xINST, 12     // s7 := B
   andi s8, s8, 0xF        // s8 := A
%  get_vreg("s8", "s8", width=width)
                           // s8 := value, held across slow path call

   // Fast path: NterpGetInstanceFieldOffset's byte offset from thread-local cache.
   FETCH_FROM_THREAD_CACHE /*resolved_field*/a0, .L${opcode}_slow, t0, t1
.L${opcode}_slow_resume:

%  get_vreg("t0", "s7", is_unsigned=True)  # t0 := holder
   beqz t0, .L${opcode}_null
   add.d t0, a0, t0          // t0 := field addr
   FETCH_ADVANCE_INST 2
%  store(src="s8", dst="t0", width=width)
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_slow:
   move a0, xSELF
   ld.d a1, sp, 0  // a1 := caller ArtMethod*
   move a2, xPC
   move a3, zero
   EXPORT_PC
   bl nterp_get_instance_field_offset  // result a0 := field_offset

   // Test for volatile (negative value).
   bgez a0, .L${opcode}_slow_resume
%  volatile_path = add_slow_path(op_iput_volatile, width, "s7", "s8", "t0", "t1")
   b $volatile_path

.L${opcode}_null:
   b common_errNullObject


%def op_iput_volatile(width, holder, value, z0, z1):
%  get_vreg(z0, holder, is_unsigned=True)  # z0 := holder
   beqz $z0, .L${opcode}_volatile_null
   sub.d $z0, $z0, a0              // z0 := field addr (holder - (-offset))
   // Ensure the volatile store is released.
%  if width == 8:
     dbar 0x2
     st.b $value, $z0, 0
     dbar 0x0
%  elif width == 16:
     dbar 0x2
     st.h $value, $z0, 0
     dbar 0x0
%  elif width == 32:
     amswap_db.w zero, $value, $z0
%  elif width == 64:
     amswap_db.d zero, $value, $z0
%  else:
%    assert False, width
%#:

   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_volatile_null:
   b common_errNullObject


// iput-wide vA, vB, field@CCCC
// Format 22c: B|A|5a CCCC
%def op_iput_wide():
%  op_iput(width=64)


// iput-object vA, vB, field@CCCC
// Format 22c: B|A|5b CCCC
%def op_iput_object():
   srli.w s8, xINST, 8
   srli.w s7, xINST, 12    // s7 := B
   andi s8, s8, 0xF        // s8 := A
%  get_vreg("s9", "s8", is_unsigned=True)  # s9 := reference

   // Fast path: NterpGetInstanceFieldOffset's byte offset from thread-local cache.
   FETCH_FROM_THREAD_CACHE /*resolved_field*/a0, .L${opcode}_slow, t0, t1
.L${opcode}_slow_resume:   // s9 := reference (slow path only)

%  get_vreg("t0", "s7", is_unsigned=True)  # t0 := holder
   beqz t0, .L${opcode}_null
   add.d t1, a0, t0        // t1 := field addr
   POISON_HEAP_REF s9      // Poisoning maps null to null for the null check in write barrier.
   // Z-TODO: why u0 failed?
   st.w s9, t1, 0
%  object_write_barrier(value="s9", holder="t0", z0="t1", z1="t2", uniq=f"{opcode}")

   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_slow:
%  slow_path = add_slow_path(op_iput_object_slow_path, "s7", "s8", "s9", "t0", "t1", "t2")
   b $slow_path

.L${opcode}_null:
   b common_errNullObject


%def op_iput_object_slow_path(holder, src, value, z0, z1, z2):
   move a0, xSELF
   ld.d a1, sp, 0  // a1 := caller ArtMethod*
   move a2, xPC
   move a3, $value
   EXPORT_PC
   bl nterp_get_instance_field_offset  // result a0 := field_offset

   // Reload value, object may have moved.
%  get_vreg(value, src, is_unsigned=True)  # value := fp[A] zext

   // Test for volatile (negative value).
   bltz a0, .L${opcode}_volatile
   b .L${opcode}_slow_resume  // resume offset exceeds branch imm

.L${opcode}_volatile:
%  get_vreg(z0, holder, is_unsigned=True)  # z0 := holder
   beqz $z0, .L${opcode}_volatile_null
   sub.d $z1, $z0, a0              // z1 := field addr (holder - (-offset))
   // Ensure the volatile store is released.
   POISON_HEAP_REF $value  // Poisoning maps null to null for the null check in write barrier.
   amswap_db.w zero, $value, $z1
%  object_write_barrier(value=value, holder=z0, z0=z1, z1=z2, uniq=f"slow_{opcode}")

   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0

.L${opcode}_volatile_null:
   b common_errNullObject


// iput-boolean vA, vB, field@CCCC
// Format 22c: B|A|5c CCCC
%def op_iput_boolean():
%  op_iput(width=8)


// iput-byte vA, vB, field@CCCC
// Format 22c: B|A|5d CCCC
%def op_iput_byte():
%  op_iput(width=8)


// iput-char vA, vB, field@CCCC
// Format 22c: B|A|5e CCCC
%def op_iput_char():
%  op_iput(width=16)


// iput-short vA, vB, field@CCCC
// Format 22c: B|A|5f CCCC
%def op_iput_short():
%  op_iput(width=16)


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
     dbar 0x2 // rw, w
     st.b $value, $z0, 0
     dbar 0x0 // rw, rw
%  elif width == 16:
     dbar 0x2 // rw, w
     st.h $value, $z0, 0
     dbar 0x0 // rw, rw
%  elif width == 32:
     amswap_db.w zero, $value, $z0
%  elif width == 64:
     amswap_db.d zero, $value, $z0
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
