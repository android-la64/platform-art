// Theory of operation. These invoke-X opcodes bounce to code labels in main.S which attempt a
// variety of fast paths; the full asm doesn't fit in the per-opcode handler's size limit.
//
// Calling convention. There are three argument transfer types.
// (A) Managed ABI -> Nterp. The ExecuteNterpImpl handles this case. We set up a fresh nterp frame
//     and move arguments from machine arg registers (and sometimes stack) into the frame.
// (B) Nterp -> Nterp. An invoke op's fast path handles this case. If we can stay in nterp, then
//     we set up a fresh nterp frame, and copy the register slots from caller to callee.
// (C) Nterp -> Managed ABI. Invoke op's remaining cases. To leave nterp, we read out arguments from
//     the caller's nterp frame and place them into machine arg registers (and sometimes stack).
//     Doing so requires obtaining and deciphering the method's shorty for arg type, width, and
//     order info.
//
// Fast path structure.
// (0) If the next method's "quick code" is nterp, then set up a fresh nterp frame and perform a
//     vreg->vreg transfer. Jump to handler for the next method's first opcode.
// - The following paths leave nterp. -
// (1) If the next method is guaranteed to be only object refs, then the managed ABI is very simple:
//     just place all arguments in the native arg registers using LWU. Call the quick code.
// (2) The next method might have an arg/return shape that can avoid the shorty, or at least avoid
//     most complications of the managed ABI arg setup.
// (2.1) If the next method has 0 args, then peek ahead in dex: if no scalar return, then call the
//       quick code. (Even when the next opcode is move-result-object, nterp will expect the
//       reference at a0, matching where the managed ABI leaves it after the call.)
// (2.2) If the next method has 0 args and scalar return, or has 1 arg, then obtain the shorty.
// (2.2.1) Post-shorty: if 0 args, call the quick code. (After the call, a returned float must be
//         copied from fa0 into a0.)
// (2.2.2) Post-shorty: check the arg's shorty type. If 'L', we must load it with LWU. Otherwise, we
//         load it with LW and store a copy into FA0 (to avoid another branch). Call the quick code.
// - The fully pessimistic case. -
// (3) The next method has 2+ arguments with a mix of float/double/long, OR it is polymorphic OR
//     custom. Obtain the shorty and perform the full setup for managed ABI. Polymorphic and
//     custom invokes are specially shunted to the runtime. Otherwise we call the quick code.
//
// Code organization. These functions are organized in a three tier structure to aid readability.
// (P) The "front end" is an opcode handler, such as op_invoke_virtual(). They are defined in
//     invoke.S. Since all the invoke code cannot fit in the allotted handler region, every invoke
//     handler has code extending into a "back end".
// (Q) The opcode handler calls a "back end" label that is located in main.S. The code for that
//     label is defined in invoke.S. As a convention, the label in main.S is NterpInvokeVirtual. The
//     code in invoke.S is nterp_invoke_virtual().
// (R) For the Nterp to Nterp fast path case, the back end calls a label located in main.S, the code
//     for which is defined in invoke.S. As a convention, the label in main.S is
//     NterpToNterpInstance, and the code in invoke.S is nterp_to_nterp_instance().
// Helpers for each tier are placed just after the functions of each tier.

//
// invoke-kind {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|op BBBB F|E|D|C
//

// invoke-virtual {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|6e BBBB F|E|D|C
//
// Note: invoke-virtual is used to invoke a normal virtual method (a method that is not private,
// static, or final, and is also not a constructor).
%def op_invoke_virtual(range=""):
   EXPORT_PC
   FETCH s7, count=2                // s7 := F|E|D|C or CCCC (range)
   FETCH_FROM_THREAD_CACHE a0, /*slow path*/2f, t0, t1
                                    // a0 := method idx of resolved virtual method
1:
%  fetch_receiver(reg="a1", vreg="s7", range=range)
                                    // a1 := fp[C] (this)
   // Note: null case handled by SEGV handler.
   ld.wu t0, a1, MIRROR_OBJECT_CLASS_OFFSET
                                    // t0 := klass object (32-bit addr)
   UNPOISON_HEAP_REF t0
   // Entry address = entry's byte offset in vtable + vtable's byte offset in klass object.
   alsl.d a0, a0, t0, 3                // a0 := entry's byte offset
   ld.d a0, a0, MIRROR_CLASS_VTABLE_OFFSET_64
                                    // a0 := ArtMethod*
   b NterpInvokeVirtual${range}  // args a0, a1, s7
2:
%  resolve_method_into_a0()
   b 1b

// invoke-super {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|6f BBBB F|E|D|C
//
// Note: When the method_id references a method of a non-interface class, invoke-super is used to
// invoke the closest superclass's virtual method (as opposed to the one with the same method_id in
// the calling class).
// Note: In Dex files version 037 or later, if the method_id refers to an interface method,
// invoke-super is used to invoke the most specific, non-overridden version of that method defined
// on that interface. The same method restrictions hold as for invoke-virtual. In Dex files prior to
// version 037, having an interface method_id is illegal and undefined.
%def op_invoke_super(range=""):
   EXPORT_PC
   FETCH s7, count=2              // s7 := F|E|D|C or CCCC (range)
   FETCH_FROM_THREAD_CACHE a0, /*slow path*/2f, t0, t1
                                  // a0 := ArtMethod*
1:
%  fetch_receiver(reg="a1", vreg="s7", range=range)
                                  // a1 := fp[C] (this)
   beqz a1, 3f                    // throw if null
   b NterpInvokeSuper${range}  // args a0, a1, s7
2:
%  resolve_method_into_a0()
   b 1b
3:
   b common_errNullObject

// invoke-direct {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|70 BBBB F|E|D|C
//
// Note: invoke-direct is used to invoke a non-static direct method (that is, an instance method
// that is by its nature non-overridable, namely either a private instance method or a constructor).
//
// For additional context on string init, see b/28555675. The object reference is replaced after
// the string factory call, so we disable thread-caching the resolution of string init, and skip
// fast paths out to managed ABI calls.
%def op_invoke_direct(range=""):
   EXPORT_PC
   FETCH s7, count=2               // s7 := F|E|D|C or CCCC (range)
   FETCH_FROM_THREAD_CACHE a0, /*slow path*/2f, t0, t1
                                   // a0 := ArtMethod*, never String.<init>
1:
%  fetch_receiver(reg="a1", vreg="s7", range=range)
                                   // a1 := fp[C] (this)
   beqz a1, 3f                     // throw if null
   b NterpInvokeDirect${range}    // args a0, a1, s7
2:
%  resolve_method_into_a0()        #  a0 := ArtMethod* or String.<init>
   andi t0, a0, 0x1                // t0 := string-init bit
   beqz t0, 1b                     // not string init
   bstrins.d a0, zero, 0, 0        // clear string-init bit
   b NterpInvokeStringInit${range}// args a0, s7
3:
   b common_errNullObject

// invoke-static {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|71 BBBB F|E|D|C
//
// Note: invoke-static is used to invoke a static method (which is always considered a direct
// method).
%def op_invoke_static(range=""):
   EXPORT_PC
   // TODO: Unnecessary if A=0, and unnecessary if nterp-to-nterp.
   FETCH s7, count=2               // s7 := F|E|D|C or CCCC (range)
   FETCH_FROM_THREAD_CACHE a0, /*slow path*/1f, t0, t1
                                   // a0 := ArtMethod*
   b NterpInvokeStatic${range}  // arg a0, s7
1:
%  resolve_method_into_a0()
   b NterpInvokeStatic${range}  // arg a0, s7

// invoke-interface {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|72 BBBB F|E|D|C
//
// Note: invoke-interface is used to invoke an interface method, that is, on an object whose
// concrete class isn't known, using a method_id that refers to an interface.
%def op_invoke_interface(range=""):
   //EXPORT_PC
   //FETCH s7, count=2               // s7 := F|E|D|C or CCCC (range)
   // T0 is eventually used to carry the "hidden argument" in the managed ABI.
   // This handler is tight on space, so we cache this arg in A0 and move it to T0 later.
   // Here, A0 is one of
   // (1) ArtMethod*
   // (2) ArtMethod* with LSB #1 set (default method)
   // (3) method index << 16 with LSB #0 set (j.l.Object method)
   la.local t5, NTERP_INVOKE_INTERFACE_HELPER
   jirl t6, t5, 0
   bne xPC, t1, 5f
   ld.d a0, t0, 8
   //FETCH_FROM_THREAD_CACHE a0, /*slow path*/5f, t0, t1
1:
%  fetch_receiver(reg="a1", vreg="s7", range=range)
                          // a1 := fp[C] (this)
   // Note: null case handled by SEGV handler.
   ld.wu t0, a1, MIRROR_OBJECT_CLASS_OFFSET
                          // t0 := klass object (32-bit addr)
   UNPOISON_HEAP_REF t0
   slli.w t1, a0, 30      // test LSB #0 and #1
   blt t1, zero, 3f       // LSB #1 is set; handle default method
   blt zero, t1, 4f       // LSB #0 is set; handle object method
   // no signal bits; it is a clean ArtMethod*
   ld.hu t1, a0, ART_METHOD_IMT_INDEX_OFFSET
                          // t1 := idx into interface method table (16-bit value)
2:
   ld.d t0, t0, MIRROR_CLASS_IMT_PTR_OFFSET_64
                          // t0 := base address of imt
   alsl.d t0, t1, t0, 3   // t0 := entry's address in imt
   ld.d a2, t0, 0         // a2 := ArtMethod*
   b NterpInvokeInterface${range}  // a0 (hidden arg), a1 (this), a2 (ArtMethod*), s7 (vregs)
3:
   bstrins.d a0, zero, 1, 1 // a0 := default ArtMethod*, LSB #1 cleared
   ld.hu t1, a0, ART_METHOD_METHOD_INDEX_OFFSET
                          // t1 := method_index_ (16-bit value)
   // Default methods have a contract with art::IMTable.
   andi t1, t1, ART_METHOD_IMT_MASK
                          // t1 := idx into interface method table
   b 2b
4:
   // Interface methods on j.l.Object have a contract with NterpGetMethod.
   srli.w t1, a0, 16       // t3 := method index
   alsl.d t0, t1, t0, 3    // t0 := entry's byte offset, before vtable offset adjustment
   ld.d a0, t0, MIRROR_CLASS_VTABLE_OFFSET_64
   b NterpInvokeDirect${range}  // args a0, a1, s7
5:
%  resolve_method_into_a0()
   b 1b

//
// invoke-kind/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|op BBBB CCCC
// where NNNN = CCCC + AA - 1, that is A determines the count 0..255, and C determines the first
// register.
//

// invoke-virtual/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|74 BBBB CCCC
//
// Note: invoke-virtual/range is used to invoke a normal virtual method (a method that is not
// private, static, or final, and is also not a constructor).
%def op_invoke_virtual_range():
%   op_invoke_virtual(range="Range")

// invoke-super/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|75 BBBB CCCC
//
// Note: When the method_id references a method of a non-interface class, invoke-super/range is used
// to invoke the closest superclass's virtual method (as opposed to the one with the same method_id
// in the calling class).
// Note: In Dex files version 037 or later, if the method_id refers to an interface method,
// invoke-super/range is used to invoke the most specific, non-overridden version of that method
// defined on that interface. In Dex files prior to version 037, having an interface method_id is
// illegal and undefined.
%def op_invoke_super_range():
%   op_invoke_super(range="Range")

// invoke-direct/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|76 BBBB CCCC
//
// Note: invoke-direct/range is used to invoke a non-static direct method (that is, an instance
// method that is by its nature non-overridable, namely either a private instance method or a
// constructor).
%def op_invoke_direct_range():
%   op_invoke_direct(range="Range")

// invoke-static/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|77 BBBB CCCC
//
// Note: invoke-static/range is used to invoke a static method (which is always considered a direct
// method).
%def op_invoke_static_range():
%   op_invoke_static(range="Range")

// invoke-interface/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|78 BBBB CCCC
//
// Note: invoke-interface/range is used to invoke an interface method, that is, on an object whose
// concrete class isn't known, using a method_id that refers to an interface.
%def op_invoke_interface_range():
%   op_invoke_interface(range="Range")

// invoke-polymorphic {vC, vD, vE, vF, vG}, meth@BBBB, proto@HHHH
// Format 45cc: A|G|fa BBBB F|E|D|C HHHH
//
// Note: Invoke the indicated signature polymorphic method. The result (if any) may be stored with
// an appropriate move-result* variant as the immediately subsequent instruction.
//
// The method reference must be to a signature polymorphic method, such as
// java.lang.invoke.MethodHandle.invoke or java.lang.invoke.MethodHandle.invokeExact.
//
// The receiver must be an object supporting the signature polymorphic method being invoked.
//
// The prototype reference describes the argument types provided and the expected return type.
//
// The invoke-polymorphic bytecode may raise exceptions when it executes. The exceptions are
// described in the API documentation for the signature polymorphic method being invoked.
//
// Present in Dex files from version 038 onwards.
%def op_invoke_polymorphic(range=""):
   EXPORT_PC
   FETCH s7, count=2  // s7 := F|E|D|C or CCCC (range)
   // No need to fetch the target method; the runtime handles it.
%  fetch_receiver(reg="s8", vreg="s7", range=range)
   beqz s8, 1f        // throw if null

   ld.d a0, sp, 0        // a0 := caller ArtMethod*
   move a1, xPC
   bl NterpGetShortyFromInvokePolymorphic  // args a0, a1
   move a1, s8
   b NterpInvokePolymorphic${range}  // args a0 (shorty), a1 (this), s7 (vregs)
1:
   b common_errNullObject

// invoke-polymorphic/range {vCCCC .. vNNNN}, meth@BBBB, proto@HHHH
// Format 4rcc: AA|fb BBBB CCCC HHHH
// where NNNN = CCCC + AA - 1, that is A determines the count 0..255, and C determines the first
// register.
//
// Note: Invoke the indicated method handle. See the invoke-polymorphic description above for
// details.
//
// Present in Dex files from version 038 onwards.
%def op_invoke_polymorphic_range():
%   op_invoke_polymorphic(range="Range")

// invoke-custom {vC, vD, vE, vF, vG}, call_site@BBBB
// Format 35c: A|G|fc BBBB F|E|D|C
//
// Note: Resolves and invokes the indicated call site. The result from the invocation (if any) may
// be stored with an appropriate move-result* variant as the immediately subsequent instruction.
//
// This instruction executes in two phases: call site resolution and call site invocation.
//
// Call site resolution checks whether the indicated call site has an associated
// java.lang.invoke.CallSite instance. If not, the bootstrap linker method for the indicated call
// site is invoked using arguments present in the DEX file (see call_site_item). The bootstrap
// linker method returns a java.lang.invoke.CallSite instance that will then be associated with the
// indicated call site if no association exists. Another thread may have already made the
// association first, and if so execution of the instruction continues with the first associated
// java.lang.invoke.CallSite instance.
//
// Call site invocation is made on the java.lang.invoke.MethodHandle target of the resolved
// java.lang.invoke.CallSite instance. The target is invoked as if executing invoke-polymorphic
// (described above) using the method handle and arguments to the invoke-custom instruction as the
// arguments to an exact method handle invocation.
//
// Exceptions raised by the bootstrap linker method are wrapped in a java.lang.BootstrapMethodError.
// A BootstrapMethodError is also raised if:
// - the bootstrap linker method fails to return a java.lang.invoke.CallSite instance.
// - the returned java.lang.invoke.CallSite has a null method handle target.
// - the method handle target is not of the requested type.
//
// Present in Dex files from version 038 onwards.
%def op_invoke_custom(range=""):
   EXPORT_PC
   ld.d a0, sp, 0  // a0 := caller ArtMethod*
   move a1, xPC
   bl NterpGetShortyFromInvokeCustom  // args a0, a1
   move s7, a0    // s7 := shorty
   FETCH a0, 1  // a0 := BBBB
   FETCH s8, 2  // s8 := F|E|D|C or CCCC (range)
   b NterpInvokeCustom${range}  // args a0 (BBBB), s7 (shorty), s8 (vregs)


// invoke-custom/range {vCCCC .. vNNNN}, call_site@BBBB
// Format 3rc: AA|fd BBBB CCCC
// where NNNN = CCCC + AA - 1, that is A determines the count 0..255, and C determines the first
// register.
//
// Note: Resolve and invoke a call site. See the invoke-custom description above for details.
//
// Present in Dex files from version 038 onwards.
%def op_invoke_custom_range():
%  op_invoke_custom(range="Range")

// handler helpers

%def resolve_method_into_a0():
   move a0, xSELF
   ld.d a1, sp, 0  // We can't always rely on a0 = ArtMethod*.
   move a2, xPC
   bl nterp_get_method


%def fetch_receiver(reg="", vreg="", range=""):
%  if range == 'Range':
     GET_VREG_OBJECT $reg, $vreg           // reg := refs[CCCC]
%  else:
     andi $reg, $vreg, 0xF                 // reg := C
     GET_VREG_OBJECT $reg, $reg            // reg := refs[C]


//
// These asm blocks are positioned in main.S for visibility to stack walking.
//


// NterpInvokeVirtual
// a0: ArtMethod*
// a1: this
// s7: vreg ids F|E|D|C
%def nterp_invoke_virtual():
%  nterp_invoke_direct(uniq="invoke_virtual")

// NterpInvokeSuper
// a0: ArtMethod*
// a1: this
// s7: vreg ids F|E|D|C
%def nterp_invoke_super():
%  nterp_invoke_direct(uniq="invoke_super")

// NterpInvokeDirect
// a0: ArtMethod*
// a1: this
// s7: (regular) vreg ids F|E|D|C, (range) vreg id CCCC
%def nterp_invoke_direct(uniq="invoke_direct", range=""):
    break 0x5

// NterpInvokeStringInit
// a0: ArtMethod*
// s7: (regular) vreg ids F|E|D|C, (range) vreg id CCCC
%def nterp_invoke_string_init(uniq="invoke_string_init", range=""):
    break 0x5

// NterpInvokeStatic
// a0: ArtMethod*
// s7: (regular) vreg ids F|E|D|C, (range) vreg id CCCC
%def nterp_invoke_static(uniq="invoke_static", range=""):
   ld.d s8, a0, ART_METHOD_QUICK_CODE_OFFSET_64
                               // s8 := quick code
%  try_nterp(quick="s8", z0="t0", skip=f".L{uniq}_simple")
   bl NterpToNterpStatic${range}  // arg a0
   b .L${uniq}_next_op

.L${uniq}_simple:
%  if range == 'Range':
%    try_simple_args_range(vC="s7", z0="t0", z1="t1", z2="t2", z3="t3", z4="t4", arg_start="0", skip=f".L{uniq}_01", uniq=uniq)
%  else:
%    try_simple_args(v_fedc="s7", z0="t0", z1="t1", arg_start="0", skip=f".L{uniq}_01", uniq=uniq)
%#:
   jirl ra, s8, 0                     // args (regular) a0 - a5, (range) a0 - a7 and maybe stack
   b .L${uniq}_next_op

.L${uniq}_01:
   move u0, zero                 // initialize shorty reg
%  try_01_args_static(vreg="s7", shorty="u0", z0="t0", z1="t1", z2="t2", y0="t7", y1="t8", skip=f".L{uniq}_slow", call=f".L{uniq}_01_call", uniq=uniq, range=range)
                               // if u0 := shorty, then maybe (a2, fa0) := fp[C] or fp[CCCC]
.L${uniq}_01_call:
   jirl ra, s8, 0                     // args a0, and maybe a1, fa0
   beqz u0, .L${uniq}_next_op  // no shorty, no scalar return
%  maybe_float_returned(shorty="u0", z0="t0", z1="t1", uniq=f"{uniq}_0")
                               // a0 := fa0 if float return
   b .L${uniq}_next_op

.L${uniq}_slow:
%  get_shorty_save_a0(shorty="u0", y0="t7")
%  if range == 'Range':
%    slow_setup_args_range(shorty="u0", vC="s7", z0="t0", z1="t1", z2="t2", z3="t3", z4="t4", z5="t5", z6="t6", z7="t7", arg_start="0", uniq=uniq)
%  else:
%    slow_setup_args(shorty="u0", vregs="s7", z0="t0", z1="t1", z2="t2", z3="t3", z4="t4", z5="t5", z6="t6", arg_start="0", uniq=uniq)
%#:
   jirl ra, s8, 0                     // args (regular) a0 - a5 and fa0 - fa4, (range) a0 - a7 and fa0 - fa7 and maybe stack
%  maybe_float_returned(shorty="u0", z0="t0", z1="t1", uniq=f"{uniq}_1")
                               // a0 := fa0 if float return
.L${uniq}_next_op:
   FETCH_ADVANCE_INST 3
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

// NterpInvokeInterface
// a0: the target interface method
//     - ignored in nterp-to-nterp transfer
//     - preserved through shorty calls
//     - side-loaded in T0 as a "hidden argument" in managed ABI transfer
// a1: this
// a2: ArtMethod*
// s7: vreg ids F|E|D|C
%def nterp_invoke_interface(uniq="invoke_interface", range=""):
    break 0x5

// NterpInvokePolymorphic
// a0: shorty
// a1: receiver this
// s7: (regular) vreg ids F|E|D|C, (range) vreg id CCCC
%def nterp_invoke_polymorphic(uniq="invoke_polymorphic", range=""):
    break 0x5

// NterpInvokeCustom
// a0: BBBB
// s7: shorty
// s8: (regular) vreg ids F|E|D|C, (range) vreg id CCCC
%def nterp_invoke_custom(uniq="invoke_custom", range=""):
    break 0x5

// NterpInvokeVirtualRange
// a0: ArtMethod*
// a1: this
// s7: vreg id CCCC
%def nterp_invoke_virtual_range():
%  nterp_invoke_direct(uniq="invoke_virtual_range", range="Range")

// NterpInvokeSuperRange
// a0: ArtMethod*
// a1: this
// s7: vreg id CCCC
%def nterp_invoke_super_range():
%  nterp_invoke_direct(uniq="invoke_super_range", range="Range")

// NterpInvokeDirectRange
// Hardcoded:
// a0: ArtMethod*
// a1: this
// s7: vreg id CCCC
%def nterp_invoke_direct_range():
%  nterp_invoke_direct(uniq="invoke_direct_range", range="Range")

// NterpInvokeStringInitRange
// a0: ArtMethod*
// s7: vreg id CCCC
%def nterp_invoke_string_init_range():
    break 0x5

// NterpInvokeStaticRange
// a0: ArtMethod*
// s7: vreg id CCCC
%def nterp_invoke_static_range():
    break 0x5

// NterpInvokeInterfaceRange
// a0: the target interface method
//     - ignored in nterp-to-nterp transfer
//     - preserved through shorty calls
//     - side-loaded in T0 as a "hidden argument" in managed ABI transfer
// a1: this
// a2: ArtMethod*
// s7: vreg id CCCC
%def nterp_invoke_interface_range():
    break 0x5

// NterpInvokePolymorphicRange
%def nterp_invoke_polymorphic_range():
    break 0x5

// NterpInvokeCustomRange
%def nterp_invoke_custom_range():
    break 0x5


// fast path and slow path helpers


// Input
// - quick: quick code ptr
// Temporaries: z0
%def try_nterp(quick="", z0="", skip=""):
   la.local $z0, ExecuteNterpImpl
   bne $z0, $quick, $skip

// Hardcoded
// - a0: ArtMethod*
// - xINST
// Input
// - v_fedc: vreg ids F|E|D|C
// Temporaries: z0, z1
%def try_simple_args(v_fedc="", z0="", z1="", arg_start="1", skip="", uniq=""):
   ld.wu $z0, a0, ART_METHOD_ACCESS_FLAGS_OFFSET
   // The meaning of nterp-invoke-fast-path-flag for LOONGARCH diverges from other ISAs.
   BRANCH_IF_BIT_CLEAR $z0, $z0, ART_METHOD_NTERP_INVOKE_FAST_PATH_FLAG_BIT, $skip

   srli.w $z0, xINST, 12              // z0 := A
%  if arg_start == "0":
     beqz $z0, .L${uniq}_simple_done  // A = 0: no further args.
%#:
   li.d $z1, 2
   blt $z0, $z1, .L${uniq}_simple_1  // A = 1
   beq $z0, $z1, .L${uniq}_simple_2  // A = 2
   li.d $z1, 4
   blt $z0, $z1, .L${uniq}_simple_3  // A = 3
   beq $z0, $z1, .L${uniq}_simple_4  // A = 4
   // A = 5
   srli.w $z1, xINST, 8              // z1 := A|G
   andi $z1, $z1, 0xF                // z1 := G
   GET_VREG_OBJECT a5, $z1
.L${uniq}_simple_4:
   srli.w $z1, $v_fedc, 12           // z1 := F
   GET_VREG_OBJECT a4, $z1
.L${uniq}_simple_3:
   srli.w $z1, $v_fedc, 8            // z1 := F|E
   andi $z1, $z1, 0xF                // z1 := E
   GET_VREG_OBJECT a3, $z1
.L${uniq}_simple_2:
   srli.w $z1, $v_fedc, 4            // z1 := F|E|D
   andi $z1, $z1, 0xF                // z1 := D
   GET_VREG_OBJECT a2, $z1
.L${uniq}_simple_1:
%  if arg_start == "0":
     andi $z1, $v_fedc, 0xF          // z1 := C
     GET_VREG_OBJECT a1, $z1
   // instance: a1 already set to "this"
.L${uniq}_simple_done:


// Range variant.
%def try_simple_args_range(vC="", z0="", z1="", z2="", z3="", z4="", skip="", arg_start="1", uniq=""):
   ld.wu $z0, a0, ART_METHOD_ACCESS_FLAGS_OFFSET
   // The meaning of nterp-invoke-fast-path-flag for RISC-V diverges from other ISAs.
   BRANCH_IF_BIT_CLEAR $z0, $z0, ART_METHOD_NTERP_INVOKE_FAST_PATH_FLAG_BIT, $skip

   srli.w $z0, xINST, 8                // z0 := AA
%  if arg_start == "0":  # static:
     beqz $z0, .L${uniq}_simple_done   // AA = 0: no further args.
     alsl.d $z1, $vC, xFP, 2           // z1 := &FP[CCCC]
     li.d $z2, 2
     blt $z0, $z2, .L${uniq}_simple_1  // AA = 1
%  else:  # instance:
     li.d $z2, 2
     blt $z0, $z2, .L${uniq}_simple_done  // AA = 1, and a1 already loaded.
     alsl.d $z1, $vC, xFP, 2              // z1 := &FP[CCCC]
%#:
   // Here: z0, z1, z2 same values for static vs instance.
   beq $z0, $z2, .L${uniq}_simple_2  // AA = 2
   li.d $z2, 4
   blt $z0, $z2, .L${uniq}_simple_3  // AA = 3
   beq $z0, $z2, .L${uniq}_simple_4  // AA = 4
   li.d $z2, 6
   blt $z0, $z2, .L${uniq}_simple_5  // AA = 5
   beq $z0, $z2, .L${uniq}_simple_6  // AA = 6
   li.d $z2, 7
   beq $z0, $z2, .L${uniq}_simple_7  // AA = 7

   // AA >= 8: store in stack. Load/store from FP[CCCC + 7] upwards.
   slli.d $z2, $z0, 63               // z2 := negative if z0 bit #0 is set (odd)
   alsl.d $z0, $z0, $z1, 2           // z0 := loop guard at top of stack
   addi.d $z3, $z1, 7*4              // z3 := &FP[CCCC + 7]
   addi.d $z4, sp, __SIZEOF_POINTER__ + 7*4
                                     // z4 := &OUT[CCCC + 7]
   blt $z2, zero, .L${uniq}_simple_loop_wide
                                     // if AA odd, branch to wide-copy
   ld.wu $z2, $z3, 0
   st.w $z2, $z4, 0
   addi.d $z3, $z3, 4
   addi.d $z4, $z4, 4

.L${uniq}_simple_loop_wide:
   // TODO: Consider ensuring 64-bit stores are aligned.
   beq $z3, $z0, .L${uniq}_simple_7
   ld.d $z2, $z3, 0
   st.d $z2, $z4, 0
   addi.d $z3, $z3, 8
   addi.d $z4, $z4, 8
   b .L${uniq}_simple_loop_wide

   // Bottom 7 slots of OUT array never written; first args are passed with a1-a7.
.L${uniq}_simple_7:
   ld.wu a7, $z1, 6*4
.L${uniq}_simple_6:
   ld.wu a6, $z1, 5*4
.L${uniq}_simple_5:
   ld.wu a5, $z1, 4*4
.L${uniq}_simple_4:
   ld.wu a4, $z1, 3*4
.L${uniq}_simple_3:
   ld.wu a3, $z1, 2*4
.L${uniq}_simple_2:
   ld.wu a2, $z1, 1*4
.L${uniq}_simple_1:
%  if arg_start == "0":  # static:
     ld.wu a1, $z1, 0*4
%#:
.L${uniq}_simple_done:


// Check if a 0/1 arg invoke form is possible, set up a2 and fa0 if needed.
// If a return value expected, move possible float return to a0.
// Hardcoded: xINST, xPC, xFP, a0, a1, t0, fa0
// NOTE xINST clobbered if interface=True and we're taking the fast path.
// zN are temporaries, yN are callee-save
%def try_01_args(vreg="", shorty="", z0="", z1="", z2="", y0="", y1="", y2="", interface=False, skip="", call="", uniq="", range=""):
%  if range == 'Range':
     srli.w $y0, xINST, 8   // y0 := AA
%  else:
     srli.w $y0, xINST, 12  // y0 := A
%#:
   addi.d $y0, $y0, -2      // y0 := A - 2 or (range) AA - 2
   blt zero, $y0, $skip     // 2+ args: slow path
   beqz $y0, .L${uniq}_01_shorty  // this and 1 arg: determine arg type with shorty
   // 0 args
%  try_01_args_peek_next(z0=z0)  # z0 is zero if invoke has scalar return
   bnez $z0, $call         // Non-scalar return, 0 args: make the call.
   // Scalar return, 0 args: determine return type with shorty

.L${uniq}_01_shorty:
   // Get shorty, stash in callee-save to be available on return.
   // When getting shorty, stash this fast path's A0 and A1, then restore.
%  if interface:
     // xINST is a regular callee save. Safe: orig xINST value unused before FETCH_ADVANCE_INST.
%    get_shorty_for_interface_save_a0_a1_t0(shorty=shorty, y0=y1, y1=y2, y2="xINST")
%  else:
%    get_shorty_save_a0_a1(shorty=shorty, y0=y1, y1=y2)
%#:
   // shorty assigned
   blt $y0, zero, $call         // Scalar return, 0 args: make the call.
   // ins = 2: this and 1 arg. Load arg type.
   ld.b $z0, $shorty, 1      // z0 := first arg
   li.d $z1, 'L'             // ref type
%  if range == 'Range':
     alsl.d $z2, $vreg, xFP, 2  // z2 := &fp[CCCC]
     ld.wu a2, $z2, 4        // a2 := fp[CCCC + 1], zext
%  else:
     srli.w $z2, $vreg, 4   // z2 := F|E|D
     andi $z2, $z2, 0xF    // z2 := D
     alsl.d $z2, $z2, xFP, 2  // z2 := &fp[D]
     ld.wu a2, $z2, 0         // a2 := fp[D], zext
%#:
   beq $z0, $z1, $call     // ref type: LWU into a2
   // non-'L' type
   movgr2fr.w fa0, a2         // overload of managed ABI, for one arg
   addi.w a2, a2, 0           // scalar type: LW into a2
   // immediately followed by call


// Static variant.
%def try_01_args_static(vreg="", shorty="", z0="", z1="", z2="", y0="", y1="", skip="", call="", uniq="", range=""):
%  if range == 'Range':
     srli.w $y0, xINST, 8     // y0 := AA
%  else:
     srli.w $y0, xINST, 12    // y0 := A
%#:
   addi.d $y0, $y0, -1         // y0 := A - 1 or (range) AA - 1
   blt zero, $y0, $skip           // 2+ args: slow path
   beqz $y0, .L${uniq}_01_shorty  // 1 arg: determine arg type with shorty
   // 0 args
%  try_01_args_peek_next(z0=z0)  # z0 is zero if invoke has scalar return
   bnez $z0, $call           // Non-scalar return, 0 args: make the call.
   // Scalar return, 0 args: determine return type with shorty.

.L${uniq}_01_shorty:
   // Get shorty, stash in callee-save to be available on return.
   // When getting shorty, stash this fast path's A0 then restore.
%  get_shorty_save_a0(shorty=shorty, y0=y1)
   // shorty assigned
   blt $y0, zero, $call           // Scalar return, 0 args: make the call.
   // ins = 1: load arg type
   ld.b $z0, $shorty, 1        // z0 := first arg
   li.d $z1, 'L'               // ref type
%  if range == 'Range':
     alsl.d $z2, $vreg, xFP, 2  // z2 := &fp[CCCC]
%  else:
     andi $z2, $vreg, 0xF    // z2 := C
     alsl.d $z2, $z2, xFP, 2 // z2 := &fp[C]
%#:
   ld.wu a1, $z2, 0          // a1 := fp[C] or (range) fp[CCCC], zext
   beq $z0, $z1, $call       // ref type: LWU into a1
   // non-'L' type
   movgr2fr.w fa0, a1        // overload of managed ABI, for one arg
   addi.w a1, a1, 0          // scalar type: LW into a1
   // immediately followed by call


%def try_01_args_peek_next(z0=""):
   FETCH $z0, count=3, width=8, byte=0
                                // z0 := next op
   bstrins.d $z0, zero, 0, 0   // clear bit #0
   addi.d $z0, $z0, -0x0A      // z0 := zero if op is 0x0A or 0x0B


// The invoked method might return in FA0, via managed ABI.
// The next opcode, MOVE-RESULT{-WIDE}, expects the value in A0.
%def maybe_float_returned(shorty="", z0="", z1="", uniq=""):
   ld.b $z0, $shorty, 0  // z0 := first byte of shorty; type of return
   li.d $z1, 'F'         //
   beq $z0, $z1, .L${uniq}_float_return_move
   li.d $z1, 'D'         //
   bne $z0, $z1, .L${uniq}_float_return_done
.L${uniq}_float_return_move:
   // If fa0 carries a 32-bit float, the hi bits of fa0 will contain all 1's (NaN boxing).
   // The use of movfr2gr.d will transfer those hi bits into a0, and that's okay, because the next
   // opcode, move-result, will only read the lo 32-bits of a0 - the box bits are correctly ignored.
   // If fa0 carries a 64-bit float, then movfr2gr.d works as expected.
   movfr2gr.d  a0, fa0
.L${uniq}_float_return_done:


// Hardcoded:
// - a0: ArtMethod*
// - a1: this
// Callee-saves: y0, y1
%def get_shorty_save_a0_a1(shorty="", y0="", y1=""):
   move $y1, a1
   move $y0, a0
   bl NterpGetShorty  // arg a0
   move $shorty, a0
   move a0, $y0
   move a1, $y1


// Static variant.
// Hardcoded:
// - a0: ArtMethod*
// Callee-saves: y0
%def get_shorty_save_a0(shorty="", y0=""):
   move $y0, a0
   bl NterpGetShorty  // arg a0
   move $shorty, a0
   move a0, $y0


// Interface variant.
// Hardcoded:
// - a0: ArtMethod*
// - a1: this
// - t0: "hidden argument"
// Callee-saves: y0, y1, y2
%def get_shorty_for_interface_save_a0_a1_t0(shorty="", y0="", y1="", y2=""):
   move $y2, t0
   move $y1, a1
   move $y0, a0
   ld.d a0, sp, 0            // a0 := caller ArtMethod*
   FETCH reg=a1, count=1  // a1 := BBBB method idx
   bl NterpGetShortyFromMethodId
   move $shorty, a0
   move a0, $y0
   move a1, $y1
   move t0, $y2


// Hardcoded: xFP, xREFS
// Starting with vreg index 0, replace any old reference with new reference.
%def subst_vreg_references(old="", new="", z0="", z1="", z2="", uniq=""):
   move $z0, xFP               // z0 := &fp[0]
   move $z1, xREFS             // z1 := &refs[0]
.L${uniq}_subst_try:
   ld.wu $z2, $z1, 0
   bne $z2, $old, .L${uniq}_subst_next
   st.w $new, $z0, 0
   st.w $new, $z1, 0
.L${uniq}_subst_next:
   addi.d $z0, $z0, 4
   addi.d $z1, $z1, 4
   bne $z1, xFP, .L${uniq}_subst_try


// Hardcoded
// - a0: ArtMethod*
// - a1: this
// Input
// - vregs: F|E|D|C from dex
%def slow_setup_args(shorty="", vregs="", z0="", z1="", z2="", z3="", z4="", z5="", z6="", arg_start="1", uniq=""):
   srli.w $z0, xINST, 12     // z0 := A
   li.d $z1, 5
   blt $z0, $z1, .L${uniq}_slow_gpr
   // A = 5: need vreg G
   srli.w $z1, xINST, 8      // z1 := A|G
   andi $z1, $z1, 0xF        // z1 := G
   slli.w $z1, $z1, 16       // z1 := G0000
   add.d $vregs, $z1, $vregs // vregs := G|F|E|D|C

.L${uniq}_slow_gpr:
   addi.d $z0, $shorty, 1     // z0 := first arg of shorty
   srli.w $z1, $vregs, 4*$arg_start
                            // z1 := (instance) F|E|D or G|F|E|D, (static) F|E|D|C or G|F|E|D|C
   li.d $z2, 'D'              // double
   li.d $z3, 'F'              // float
   li.d $z4, 'J'              // long
   li.d $z5, 'L'              // ref
   // linear scan through shorty: extract non-float vregs
%  if arg_start == "0":  # static can place vC into a1; instance already loaded "this" into a1.
%    load_vreg_in_gpr(gpr="a1", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, L=z5, z0=z6, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_0")
%  load_vreg_in_gpr(gpr="a2", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, L=z5, z0=z6, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_1")
%  load_vreg_in_gpr(gpr="a3", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, L=z5, z0=z6, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_2")
%  load_vreg_in_gpr(gpr="a4", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, L=z5, z0=z6, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_3")
%  load_vreg_in_gpr(gpr="a5", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, L=z5, z0=z6, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_4")

.L${uniq}_slow_fpr:
   addi.d $z0, $shorty, 1     // z0 := first arg of shorty
   srli.w $z1, $vregs, 4*$arg_start
                            // z1 := (instance) F|E|D or G|F|E|D, (static) F|E|D|C or G|F|E|D|C
   // linear scan through shorty: extract float/double vregs
%  load_vreg_in_fpr(fpr="fa0", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, z0=z5, done=f".L{uniq}_slow_done", uniq=f"{uniq}_0")
%  load_vreg_in_fpr(fpr="fa1", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, z0=z5, done=f".L{uniq}_slow_done", uniq=f"{uniq}_1")
%  load_vreg_in_fpr(fpr="fa2", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, z0=z5, done=f".L{uniq}_slow_done", uniq=f"{uniq}_2")
%  load_vreg_in_fpr(fpr="fa3", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, z0=z5, done=f".L{uniq}_slow_done", uniq=f"{uniq}_3")
%  if arg_start == "0":  # static can place G into fa4; instance has only 4 args.
%    load_vreg_in_fpr(fpr="fa4", shorty=z0, vregs=z1, D=z2, F=z3, J=z4, z0=z5, done=f".L{uniq}_slow_done", uniq=f"{uniq}_4")
%#:
.L${uniq}_slow_done:


// Range and static-range variant.
// Hardcoded
// - (caller) xPC, xINST, xFP
// - (callee) sp
// Input
// - vC: CCCC from dex
%def slow_setup_args_range(shorty="", vC="", z0="", z1="", z2="", z3="", z4="", z5="", z6="", z7="", arg_start="1", uniq=""):
   addi.d $z0, $shorty, 1       // z0 := first arg of shorty
   addi.d $z1, $vC, $arg_start  // z1 := (instance) CCCC+1, (static) CCCC
   move $z2, zero               // z2 := is_out_stack_needed false
   li.d $z3, 'D'                // double
   li.d $z4, 'F'                // float
   li.d $z5, 'J'                // long
   li.d $z6, 'L'                // ref

   // linear scan through shorty: extract non-float vregs
%  if arg_start == "0":  # static can place vCCCC into a1; instance already loaded "this" into a1.
%    load_vreg_in_gpr_range(gpr="a1", shorty=z0, idx=z1, D=z3, F=z4, J=z5, L=z6, z0=z7, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_1")
%  load_vreg_in_gpr_range(gpr="a2", shorty=z0, idx=z1, D=z3, F=z4, J=z5, L=z6, z0=z7, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_2")
%  load_vreg_in_gpr_range(gpr="a3", shorty=z0, idx=z1, D=z3, F=z4, J=z5, L=z6, z0=z7, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_3")
%  load_vreg_in_gpr_range(gpr="a4", shorty=z0, idx=z1, D=z3, F=z4, J=z5, L=z6, z0=z7, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_4")
%  load_vreg_in_gpr_range(gpr="a5", shorty=z0, idx=z1, D=z3, F=z4, J=z5, L=z6, z0=z7, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_5")
%  load_vreg_in_gpr_range(gpr="a6", shorty=z0, idx=z1, D=z3, F=z4, J=z5, L=z6, z0=z7, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_6")
%  load_vreg_in_gpr_range(gpr="a7", shorty=z0, idx=z1, D=z3, F=z4, J=z5, L=z6, z0=z7, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_7")
%  is_out_stack_needed(needed=z2, shorty=z0, D=z3, F=z4, z0=z1, uniq=uniq)

.L${uniq}_slow_fpr:
   addi.d $z0, $shorty, 1       // z0 := first arg of shorty
   addi.d $z1, $vC, $arg_start  // z1 := (instance) CCCC+1, (static) CCCC
   // linear scan through shorty: extract float/double vregs
%  load_vreg_in_fpr_range(fpr="fa0", shorty=z0, idx=z1, D=z3, F=z4, J=z5, z0=z6, done=f".L{uniq}_slow_stack", uniq=f"{uniq}_0")
%  load_vreg_in_fpr_range(fpr="fa1", shorty=z0, idx=z1, D=z3, F=z4, J=z5, z0=z6, done=f".L{uniq}_slow_stack", uniq=f"{uniq}_1")
%  load_vreg_in_fpr_range(fpr="fa2", shorty=z0, idx=z1, D=z3, F=z4, J=z5, z0=z6, done=f".L{uniq}_slow_stack", uniq=f"{uniq}_2")
%  load_vreg_in_fpr_range(fpr="fa3", shorty=z0, idx=z1, D=z3, F=z4, J=z5, z0=z6, done=f".L{uniq}_slow_stack", uniq=f"{uniq}_3")
%  load_vreg_in_fpr_range(fpr="fa4", shorty=z0, idx=z1, D=z3, F=z4, J=z5, z0=z6, done=f".L{uniq}_slow_stack", uniq=f"{uniq}_4")
%  load_vreg_in_fpr_range(fpr="fa5", shorty=z0, idx=z1, D=z3, F=z4, J=z5, z0=z6, done=f".L{uniq}_slow_stack", uniq=f"{uniq}_5")
%  load_vreg_in_fpr_range(fpr="fa6", shorty=z0, idx=z1, D=z3, F=z4, J=z5, z0=z6, done=f".L{uniq}_slow_stack", uniq=f"{uniq}_6")
%  load_vreg_in_fpr_range(fpr="fa7", shorty=z0, idx=z1, D=z3, F=z4, J=z5, z0=z6, done=f".L{uniq}_slow_stack", uniq=f"{uniq}_7")
%  is_out_stack_needed_float(needed=z2, shorty=z0, D=z3, F=z4, z0=z1, uniq=uniq)

.L${uniq}_slow_stack:
   beqz $z2, .L${uniq}_slow_done  // No stack needed, skip it. Otherwise copy-paste it all with LD/SD.
   addi.d $z0, sp, 8             // z0 := base addr of out array
   alsl.d $z1, $vC, xFP, 2       // z1 := base addr of FP[CCCC]
   srli.w $z2, xINST, 8          // z2 := AA, vreg count
   alsl.d $z2, $z2, $z1, 2       // z2 := loop guard, addr of one slot past top of xFP array
%  copy_vregs_to_out(out=z0, fp=z1, fp_top=z2, z0=z3, uniq=uniq)
.L${uniq}_slow_done:


// Iterate through 4-bit vreg ids in the "vregs" register, load a non-FP value
// into one argument register.
%def load_vreg_in_gpr(gpr="", shorty="", vregs="", D="", F="", J="", L="", z0="", done="", uniq=""):
.L${uniq}_gpr_find:
   ld.b $z0, $shorty, 0         // z0 := next shorty arg spec
   addi.d $shorty, $shorty, 1  // increment char ptr
   beqz $z0, $done           // z0 == \0
   beq $z0, $F, .L${uniq}_gpr_skip_4_bytes
   beq $z0, $D, .L${uniq}_gpr_skip_8_bytes

   andi $gpr, $vregs, 0xF    // gpr := vreg id
   beq $z0, $J, .L${uniq}_gpr_load_8_bytes
%  get_vreg(gpr, gpr)        #  gpr := 32-bit load
   bne $z0, $L, .L${uniq}_gpr_load_common
   bstrpick.d $gpr, $gpr, 31, 0
.L${uniq}_gpr_load_common:
   srli.w $vregs, $vregs, 4   // shift out the processed arg, one vreg
   b .L${uniq}_gpr_set       // and exit
.L${uniq}_gpr_load_8_bytes:
   GET_VREG_WIDE $gpr, $gpr  // gpr := 64-bit load
   srli.w $vregs, $vregs, 8   // shift out the processed arg, a vreg pair
   b .L${uniq}_gpr_set       // and exit

.L${uniq}_gpr_skip_8_bytes:
   srli.w $vregs, $vregs, 4   // shift out a skipped arg
.L${uniq}_gpr_skip_4_bytes:
   srli.w $vregs, $vregs, 4   // shift out a skipped arg
   b .L${uniq}_gpr_find
.L${uniq}_gpr_set:


// Iterate through 4-bit vreg ids in the "vregs" register, load a float or double
// value into one floating point argument register.
%def load_vreg_in_fpr(fpr="", shorty="", vregs="", D="", F="", J="", z0="", done="", uniq=""):
.L${uniq}_fpr_find:
   ld.b $z0, $shorty, 0         // z0 := next shorty arg spec
   addi.d $shorty, $shorty, 1  // increment char ptr
   beqz $z0, $done           // z0 == \0
   beq $z0, $F, .L${uniq}_fpr_load_4_bytes
   beq $z0, $D, .L${uniq}_fpr_load_8_bytes

   srli.w $vregs, $vregs, 4   // shift out a skipped arg, one vreg
   bne $z0, $J, .L${uniq}_fpr_find
   srli.w $vregs, $vregs, 4   // shift out one more skipped arg, for J
   b .L${uniq}_fpr_find

.L${uniq}_fpr_load_4_bytes:
   andi $z0, $vregs, 0xF
%  get_vreg_float(fpr, z0)
   srli.w $vregs, $vregs, 4   // shift out the processed arg, one vreg
   b .L${uniq}_fpr_set
.L${uniq}_fpr_load_8_bytes:
   andi $z0, $vregs, 0xF
   GET_VREG_DOUBLE $fpr, $z0
   srli.w $vregs, $vregs, 8   // shift out the processed arg, a vreg pair
.L${uniq}_fpr_set:


// Range variant
%def load_vreg_in_gpr_range(gpr="", shorty="", idx="", D="", F="", J="", L="", z0="", done="", uniq=""):
.L${uniq}_gpr_range_find:
   ld.b $z0, $shorty, 0           // z0 := next shorty arg
   addi.d $shorty, $shorty, 1    // increment char ptr
   beqz $z0, $done             // z0 == \0
   beq $z0, $F, .L${uniq}_gpr_range_skip_1_vreg
   beq $z0, $D, .L${uniq}_gpr_range_skip_2_vreg

   beq $z0, $J, .L${uniq}_gpr_range_load_2_vreg
%  get_vreg(gpr, idx)
   bne $z0, $L, .L${uniq}_gpr_range_load_common
   bstrpick.d $gpr, $gpr, 31, 0
.L${uniq}_gpr_range_load_common:
   addi.d $idx, $idx, 1
   b .L${uniq}_gpr_range_done
.L${uniq}_gpr_range_load_2_vreg:
   GET_VREG_WIDE $gpr, $idx
   addi.d $idx, $idx, 2
   b .L${uniq}_gpr_range_done

.L${uniq}_gpr_range_skip_2_vreg:
   addi.d $idx, $idx, 1
.L${uniq}_gpr_range_skip_1_vreg:
   addi.d $idx, $idx, 1
   b .L${uniq}_gpr_range_find
.L${uniq}_gpr_range_done:


// Range variant.
%def load_vreg_in_fpr_range(fpr="", shorty="", idx="", D="", F="", J="", z0="", done="", uniq=""):
.L${uniq}_fpr_range_find:
   ld.b $z0, $shorty, 0         // z0 := next shorty arg
   addi.d $shorty, $shorty, 1  // increment char ptr
   beqz $z0, $done           // z0 == \0
   beq $z0, $F, .L${uniq}_fpr_range_load_4_bytes
   beq $z0, $D, .L${uniq}_fpr_range_load_8_bytes

   addi.d $idx, $idx, 1        // increment idx
   bne $z0, $J, .L${uniq}_fpr_range_find
   addi.d $idx, $idx, 1        // increment once more for J
   b .L${uniq}_fpr_range_find

.L${uniq}_fpr_range_load_4_bytes:
   move $z0, $idx
%  get_vreg_float(fpr, z0)
   addi.d $idx, $idx, 1
   b .L${uniq}_fpr_range_set
.L${uniq}_fpr_range_load_8_bytes:
   move $z0, $idx
   GET_VREG_DOUBLE $fpr, $z0
   addi.d $idx, $idx, 2
.L${uniq}_fpr_range_set:


%def is_out_stack_needed(needed="", shorty="", D="", F="", z0="", uniq=""):
.L${uniq}_scan_arg:
   ld.b $z0, $shorty, 0
   addi.d $shorty, $shorty, 1
   beqz $z0, .L${uniq}_scan_done
   beq $z0, $F, .L${uniq}_scan_arg
   beq $z0, $D, .L${uniq}_scan_arg
   li.d $needed, 1
.L${uniq}_scan_done:


%def is_out_stack_needed_float(needed="", shorty="", D="", F="", z0="", uniq=""):
   bnez $needed, .L${uniq}_scan_float_done
.L${uniq}_scan_float_arg:
   ld.b $z0, $shorty, 0
   addi.d $shorty, $shorty, 1
   beqz $z0, .L${uniq}_scan_float_done
   beq $z0, $F, .L${uniq}_scan_float_found
   beq $z0, $D, .L${uniq}_scan_float_found
   b .L${uniq}_scan_float_arg
.L${uniq}_scan_float_found:
   li.d $needed, 1
.L${uniq}_scan_float_done:


%def copy_vregs_to_out(out="", fp="", fp_top="", z0="", uniq=""):
   sub.d $z0, $fp_top, $fp  // z0 := byte range
   BRANCH_IF_BIT_CLEAR $z0, $z0, 2, .L${uniq}_copy_wide
                          // branch if odd count of slots
   ld.wu $z0, $fp, 0
   st.w $z0, $out, 0
   addi.d $fp, $fp, 4
   addi.d $out, $out, 4
.L${uniq}_copy_wide:
   beq $fp, $fp_top, .L${uniq}_copy_done
   ld.d $z0, $fp, 0
   st.d $z0, $out, 0
   addi.d $fp, $fp, 8
   addi.d $out, $out, 8
   b .L${uniq}_copy_wide
.L${uniq}_copy_done:



// NterpToNterpInstance
// a0: ArtMethod*
// a1: this
%def nterp_to_nterp_instance():
%  nterp_to_nterp(how_vC="in_a1", uniq="n2n_instance")


// NterpToNterpStringInit
// a0: ArtMethod*
%def nterp_to_nterp_string_init():
%  nterp_to_nterp(how_vC="skip", uniq="n2n_string_init")


// NterpToNterpStatic
// a0: ArtMethod*
%def nterp_to_nterp_static():
%  nterp_to_nterp(a1_instance=False, how_vC="load", uniq="n2n_static")


// NterpToNterpInstanceRange
%def nterp_to_nterp_instance_range():
%  nterp_to_nterp(how_vC="in_a1", uniq="n2n_instance_range", range="Range")


// NterpToNterpStringInitRange
%def nterp_to_nterp_string_init_range():
%  nterp_to_nterp(how_vC="skip", uniq="n2n_string_init_range", range="Range")


// NterpToNterpStaticRange
%def nterp_to_nterp_static_range():
%  nterp_to_nterp(a1_instance=False, how_vC="load", uniq="n2n_static_range", range="Range")


// helpers


%def nterp_to_nterp(a1_instance=True, how_vC="", uniq="", range=""):
   .cfi_startproc
%  setup_nterp_frame(cfi_refs="23", refs="s8", fp="u0", pc="t7", regs="t8", spills_sp="t0", z0="t1", z1="t2", z2="t3", z3="t4", uniq=uniq)
       // s8  := callee xREFS
       // u0  := callee xFP
       // t7 := callee xPC
       // t8 := fp/refs vreg count
       // t0  := post-spills pre-frame sp (unused here)
       // sp  := post-frame callee sp
%  if range == 'Range':
%    n2n_arg_move_range(refs="s8", fp="u0", regs="t8", vC="s7", z0="t0", z1="t1", z2="t2", z3="t3", z4="t4", z5="t5", a1_instance=a1_instance, how_vC=how_vC, uniq=uniq)
%  else:
%    n2n_arg_move(refs="s8", fp="u0", pc="t7", regs="t8", v_fedc="s7", z0="t0", z1="t1", z2="t2", z3="t3", a1_instance=a1_instance, how_vC=how_vC, uniq=uniq)
%#:
   move xREFS, s8
   move xFP, u0
   move xPC, t7
   CFI_DEFINE_DEX_PC_WITH_OFFSET(/*tmpReg*/CFI_TMP, /*dexReg*/CFI_DEX, /*dexOffset*/0)

   START_EXECUTING_INSTRUCTIONS
   .cfi_endproc

%def op_invoke_interface_slow_path():
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
   bstrins.d $z1, zero, 3, 0 // z1 := new sp

   // Set \refs to base of reference array. Align to pointer size for the frame pointer and dex pc
   // pointer, below the reference array.
   alsl.d $z0, $z0, $z1, 2// z0 := out array size in bytes
   addi.d $z0, $z0, 28    //     + 24 bytes for 3 fields, plus 4 for alignment slack.
   li.d t6, -__SIZEOF_POINTER__ // use t6 to not clobber refs and z0
   and $refs, $z0, t6     // refs := refs array base

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


// Hardcoded
// - (caller) xINST, xFP, xREFS, xPC
// - a0: ArtMethod*
// - a1: this, for instance invoke
%def n2n_arg_move(refs="", fp="", regs="", pc="", v_fedc="", z0="", z1="", z2="", z3="", a1_instance=True, how_vC="", uniq=""):
   srli.w $z0, xINST, 12       // z0 := A (arg count)

%  if not a1_instance:
     beqz $z0, .L${uniq}_arg_done
%#:
   // A >= 1, decide and branch
   li.d $z1, 2
   sub.d $z2, $regs, $z0         // z2 := regs - A; vC's index in fp
   alsl.d $z3, $z2, $fp, 2       // z3 := addr of fp[C]
   alsl.d $z2, $z2, $refs, 2     // z2 := addr of refs[C]
   blt $z0, $z1, .L${uniq}_arg_1
   beq $z0, $z1, .L${uniq}_arg_2
   li.d $z1, 4
   blt $z0, $z1, .L${uniq}_arg_3
   beq $z0, $z1, .L${uniq}_arg_4

   // A = 5
   srli.w $z0, xINST, 8
   andi $z0, $z0, 0xF         // z0 := G
%  get_vreg(z1, z0)           #  z1 := xFP[G]
   st.w $z1, $z3, (4*4)       // fp[G] := z1
   GET_VREG_OBJECT $z0, $z0   // z0 := xREFS[G]
   st.w $z0, $z2, (4*4)       // refs[G] := z0
.L${uniq}_arg_4:
   srli.w $z0, $v_fedc, 12    // z0 := F
%  get_vreg(z1, z0)           #  z1 := xFP[F]
   st.w $z1, $z3, (3*4)       // fp[F] := z1
   GET_VREG_OBJECT $z0, $z0   // z0 := xREFS[F]
   st.w $z0, $z2, (3*4)       // refs[F] := z0
.L${uniq}_arg_3:
   srli.w $z0, $v_fedc, 8     // z0 := F|E
   andi $z0, $z0, 0xF         // z0 := E
%  get_vreg(z1, z0)           #  z1 := xFP[E]
   st.w $z1, $z3, (2*4)       // fp[E] := z1
   GET_VREG_OBJECT $z0, $z0   // z0 := xREFS[E]
   st.w $z0, $z2, (2*4)       // refs[E] := z0
.L${uniq}_arg_2:
   srli.w $z0, $v_fedc, 4     // z0 := F|E|D
   andi $z0, $z0, 0xF         // z0 := D
%  get_vreg(z1, z0)           #  z1 := xFP[D]
   st.w $z1, $z3, (1*4)       // fp[D] := z1
   GET_VREG_OBJECT $z0, $z0   // z0 := xREFS[D]
   st.w $z0, $z2, (1*4)       // refs[D] := z0
.L${uniq}_arg_1:
%  if how_vC == "in_a1":
     // a1 = xFP[C] from earlier stage of instance invoke
     st.w a1, $z3, (0*4)       // fp[C] := a1
     st.w a1, $z2, (0*4)       // refs[C] := a1
%  elif how_vC == "skip":
     // string init doesn't read "this"
%  elif how_vC == "load":
     // static method loads vC just like other vregs
     andi $z0, $v_fedc, 0xF   // z0 := C
%    get_vreg(z1, z0)         #  z1 := xFP[C]
     st.w $z1, $z3, (0*4)     // fp[C] := z1
     GET_VREG_OBJECT $z0, $z0 // z0 := xREFS[C]
     st.w $z0, $z2, (0*4)     // refs[C] := z0
%#:
.L${uniq}_arg_done:


%def n2n_arg_move_range(refs="", fp="", regs="", vC="", z0="", z1="", z2="", z3="", z4="", z5="", a1_instance=True, how_vC="", uniq=""):
   srli.w $z0, xINST, 8     // z0 := AA (arg count)

%  if not a1_instance:
     beqz $z0, .L${uniq}_arg_range_done
%#:
   // AA >= 1, iterator setup
   sub.d $z4, $regs, $z0      // z4 := regs - AA; starting idx in fp and refs
   alsl.d $z1, $vC, xREFS, 2  // z1 := addr of xREFS[CCCC]
   alsl.d $z2, $vC, xFP, 2    // z2 := addr of xFP[CCCC]
   alsl.d $z3, $z4, $refs, 2  // z3 := addr of refs[z4]
   alsl.d $z4, $z4, $fp, 2    // z4 := addr of fp[z4]

   BRANCH_IF_BIT_CLEAR $z0, $z0, 0, .L${uniq}_arg_range_copy_wide
                           // branch if AA is even
   // AA is odd, transfer one slot. Apply some optimizations.
%  if how_vC == "in_a1":
     st.w a1, $z3, 0
     st.w a1, $z4, 0
%  elif how_vC == "skip":
     // string init doesn't read "this"
%  elif how_vC == "load":
     ld.w $z0, $z1, 0
     ld.w $z5, $z2, 0
     st.w $z0, $z3, 0
     st.w $z5, $z4, 0
%#:
   addi.d $z1, $z1, 4
   addi.d $z2, $z2, 4
   addi.d $z3, $z3, 4
   addi.d $z4, $z4, 4
.L${uniq}_arg_range_copy_wide:
   // Even count of vreg slots, apply LD/SD.
   beq $z3, $fp, .L${uniq}_arg_range_done  // terminate loop if refs[regs] == fp[0]
   ld.d $z0, $z1, 0
   ld.d $z5, $z2, 0
   st.d $z0, $z3, 0
   st.d $z5, $z4, 0
   addi.d $z1, $z1, 8
   addi.d $z2, $z2, 8
   addi.d $z3, $z3, 8
   addi.d $z4, $z4, 8
   b .L${uniq}_arg_range_copy_wide
.L${uniq}_arg_range_done:


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

