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

#include "jni_macro_assembler_loongarch64.h"

#include "android-base/macros.h"
#include "base/array_ref.h"
#include "base/macros.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "indirect_reference_table.h"
#include "lock_word.h"
#include "managed_register_loongarch64.h"
#include "thread.h"
#include "utils/managed_register.h"
#include "base/bit_utils_iterator.h"

namespace art HIDDEN {
namespace loongarch64 {

static constexpr size_t kSpillSize = 8;  // Both GPRs and FPRs

static std::pair<uint32_t, uint32_t> GetCoreAndFpSpillMasks(
    ArrayRef<const ManagedRegister> callee_save_regs) {
  uint32_t core_spill_mask = 0u;
  uint32_t fp_spill_mask = 0u;
  for (ManagedRegister r : callee_save_regs) {
    Loongarch64ManagedRegister reg = r.AsLoongarch64();
    if (reg.IsXRegister()) {
      core_spill_mask |= 1u << reg.AsXRegister();
    } else {
      DCHECK(reg.IsFRegister());
      fp_spill_mask |= 1u << reg.AsFRegister();
    }
  }
  DCHECK_EQ(callee_save_regs.size(),
            dchecked_integral_cast<size_t>(POPCOUNT(core_spill_mask) + POPCOUNT(fp_spill_mask)));
  return {core_spill_mask, fp_spill_mask};
}

#define __ asm_.

Loongarch64JNIMacroAssembler::~Loongarch64JNIMacroAssembler() {
}

void Loongarch64JNIMacroAssembler::FinalizeCode() {
  __ FinalizeCode();
}

void Loongarch64JNIMacroAssembler::BuildFrame(size_t frame_size,
                                          ManagedRegister method_reg,
                                          ArrayRef<const ManagedRegister> callee_save_regs) {
  // Increase frame to required size.
  DCHECK_ALIGNED(frame_size, kStackAlignment);
  // Must at least have space for Method* if we're going to spill it.
  DCHECK_GE(frame_size,
            (callee_save_regs.size() + (method_reg.IsRegister() ? 1u : 0u)) * kSpillSize);
  IncreaseFrameSize(frame_size);

  // Save callee-saves.
  auto [core_spill_mask, fp_spill_mask] = GetCoreAndFpSpillMasks(callee_save_regs);
  size_t offset = frame_size;
  if ((core_spill_mask & (1u << RA)) != 0u) {
    offset -= kSpillSize;
    __ Store_D(RA, SP, offset);
    __ cfi().RelOffset(dwarf::Reg::Loongarch64Core(RA), offset);
  }
  for (uint32_t reg : HighToLowBits(core_spill_mask & ~(1u << RA))) {
    offset -= kSpillSize;
    __ Store_D(enum_cast<XRegister>(reg), SP, offset);
    __ cfi().RelOffset(dwarf::Reg::Loongarch64Core(enum_cast<XRegister>(reg)), offset);
  }
  for (uint32_t reg : HighToLowBits(fp_spill_mask)) {
    offset -= kSpillSize;
    __ FStore_D(enum_cast<FRegister>(reg), SP, offset);
    __ cfi().RelOffset(dwarf::Reg::Loongarch64Fp(enum_cast<FRegister>(reg)), offset);
  }

  if (method_reg.IsRegister()) {
    // Write ArtMethod*.
    DCHECK_EQ(A0, method_reg.AsLoongarch64().AsXRegister());
    __ Store_D(A0, SP, 0);
  }
}

void Loongarch64JNIMacroAssembler::RemoveFrame(size_t frame_size,
                                           ArrayRef<const ManagedRegister> callee_save_regs,
                                          [[maybe_unused]] bool may_suspend) {
  cfi().RememberState();

  // Restore callee-saves.
  auto [core_spill_mask, fp_spill_mask] = GetCoreAndFpSpillMasks(callee_save_regs);
  size_t offset = frame_size - callee_save_regs.size() * kSpillSize;
  for (uint32_t reg : LowToHighBits(fp_spill_mask)) {
    __ FLoad_D(enum_cast<FRegister>(reg), SP, offset);
    __ cfi().Restore(dwarf::Reg::Loongarch64Fp(enum_cast<FRegister>(reg)));
    offset += kSpillSize;
  }
  for (uint32_t reg : LowToHighBits(core_spill_mask & ~(1u << RA))) {
    __ Load_D(enum_cast<XRegister>(reg), SP, offset);
    __ cfi().Restore(dwarf::Reg::Loongarch64Core(enum_cast<XRegister>(reg)));
    offset += kSpillSize;
  }
  if ((core_spill_mask & (1u << RA)) != 0u) {
    __ Load_D(RA, SP, offset);
    __ cfi().Restore(dwarf::Reg::Loongarch64Core(RA));
    offset += kSpillSize;
  }
  DCHECK_EQ(offset, frame_size);

  // Decrease the frame size.
  DecreaseFrameSize(frame_size);

  // Return to RA.
  __ Jr(RA);

  // The CFI should be restored for any code that follows the exit block.
  __ cfi().RestoreState();
  __ cfi().DefCFAOffset(frame_size);
}

void Loongarch64JNIMacroAssembler::IncreaseFrameSize(size_t adjust) {
  if (adjust != 0u) {
    CHECK_ALIGNED(adjust, kStackAlignment);
    __ AddConst64(SP, SP, -adjust);
    __ cfi().AdjustCFAOffset(adjust);
  }
}

void Loongarch64JNIMacroAssembler::DecreaseFrameSize(size_t adjust) {
  if (adjust != 0u) {
    CHECK_ALIGNED(adjust, kStackAlignment);
    __ AddConst64(SP, SP, adjust);
    __ cfi().AdjustCFAOffset(-adjust);
  }
}

ManagedRegister Loongarch64JNIMacroAssembler::CoreRegisterWithSize(ManagedRegister src, size_t size) {
  DCHECK(src.AsLoongarch64().IsXRegister());
  DCHECK(size == 4u || size == 8u) << size;
  return src;
}

void Loongarch64JNIMacroAssembler::Store(FrameOffset offs, ManagedRegister m_src, size_t size) {
  Store(Loongarch64ManagedRegister::FromXRegister(SP), MemberOffset(offs.Int32Value()), m_src, size);
}

void Loongarch64JNIMacroAssembler::Store(ManagedRegister m_base,
                                     MemberOffset offs,
                                     ManagedRegister m_src,
                                     size_t size) {
  Loongarch64ManagedRegister base = m_base.AsLoongarch64();
  Loongarch64ManagedRegister src = m_src.AsLoongarch64();
  if (src.IsXRegister()) {
    if (size == 4u) {
      __ Store_W(src.AsXRegister(), base.AsXRegister(), offs.Int32Value());
    } else {
      CHECK_EQ(8u, size);
      __ Store_D(src.AsXRegister(), base.AsXRegister(), offs.Int32Value());
    }
  } else {
    CHECK(src.IsFRegister()) << src;
    if (size == 4u) {
      __ FStore_S(src.AsFRegister(), base.AsXRegister(), offs.Int32Value());
    } else {
      CHECK_EQ(8u, size);
      __ FStore_D(src.AsFRegister(), base.AsXRegister(), offs.Int32Value());
    }
  }
}

void Loongarch64JNIMacroAssembler::StoreRawPtr(FrameOffset offs, ManagedRegister m_src) {
  Loongarch64ManagedRegister src = m_src.AsLoongarch64();
  CHECK(src.IsXRegister()) << src;
  __ Store_D(src.AsXRegister(), SP, offs.Int32Value());
}

void Loongarch64JNIMacroAssembler::StoreStackPointerToThread(ThreadOffset64 offs, bool tag_sp) {
  XRegister src = SP;
  ScratchRegisterScope srs(&asm_);
  if (tag_sp) {
    XRegister tmp = srs.AllocateXRegister();
    __ Ori(tmp, SP, 0x2);
    src = tmp;
  }
  __ Store_D(src, TR, offs.Int32Value());
}

void Loongarch64JNIMacroAssembler::Load(ManagedRegister m_dest, FrameOffset src, size_t size) {
  Load(m_dest, Loongarch64ManagedRegister::FromXRegister(SP), MemberOffset(src.Int32Value()), size);
}

void Loongarch64JNIMacroAssembler::Load(ManagedRegister m_dest,
                                    ManagedRegister m_base,
                                    MemberOffset offs,
                                    size_t size) {
  Loongarch64ManagedRegister base = m_base.AsLoongarch64();
  Loongarch64ManagedRegister dest = m_dest.AsLoongarch64();

  if (dest.IsXRegister()) {
    if (size == 4u) {
      // The riscv64 native calling convention specifies that integers narrower than XLEN (64)
      // bits are "widened according to the sign of their type up to 32 bits, then sign-extended
      // to XLEN bits." The managed ABI already passes integral values this way in registers
      // and correctly widened to 32 bits on the stack. The `Load()` must sign-extend narrower
      // types here to pass integral values correctly to the native call.
      // For `float` args, the upper 32 bits are undefined, so this is fine for them as well.
      __ Load_W(dest.AsXRegister(), base.AsXRegister(), offs.Int32Value());
    } else {
      CHECK_EQ(8u, size);
      __ Load_D(dest.AsXRegister(), base.AsXRegister(), offs.Int32Value());
    }
  } else {
    CHECK(dest.IsFRegister()) << dest;
    if (size == 4u) {
      __ FLoad_S(dest.AsFRegister(), base.AsXRegister(), offs.Int32Value());
    } else {
      CHECK_EQ(8u, size);
      __ FLoad_D(dest.AsFRegister(), base.AsXRegister(), offs.Int32Value());
    }
  }
}

void Loongarch64JNIMacroAssembler::LoadRawPtrFromThread(ManagedRegister m_dest, ThreadOffset64 offs) {
  Loongarch64ManagedRegister tr = Loongarch64ManagedRegister::FromXRegister(TR);
  Load(m_dest, tr, MemberOffset(offs.Int32Value()), static_cast<size_t>(kLoongarch64PointerSize));
}

void Loongarch64JNIMacroAssembler::LoadGcRootWithoutReadBarrier(ManagedRegister m_dest,
                                                            ManagedRegister m_base,
                                                            MemberOffset offs) {
  Loongarch64ManagedRegister base = m_base.AsLoongarch64();
  Loongarch64ManagedRegister dest = m_dest.AsLoongarch64();
  static_assert(sizeof(uint32_t) == sizeof(GcRoot<mirror::Object>));
  __ Load_WU(dest.AsXRegister(), base.AsXRegister(), offs.Int32Value());
}

void Loongarch64JNIMacroAssembler::LoadStackReference(ManagedRegister m_dest, FrameOffset offs) {
  // `StackReference<>` and `GcRoot<>` have the same underlying representation, namely
  // `CompressedReference<>`. And `StackReference<>` does not need a read barrier.
  static_assert(sizeof(uint32_t) == sizeof(mirror::CompressedReference<mirror::Object>));
  static_assert(sizeof(uint32_t) == sizeof(StackReference<mirror::Object>));
  static_assert(sizeof(uint32_t) == sizeof(GcRoot<mirror::Object>));
  LoadGcRootWithoutReadBarrier(
      m_dest, Loongarch64ManagedRegister::FromXRegister(SP), MemberOffset(offs.Int32Value()));
}

void Loongarch64JNIMacroAssembler::MoveArguments(ArrayRef<ArgumentLocation> dests,
                                                 ArrayRef<ArgumentLocation> srcs,
                                                 ArrayRef<FrameOffset> refs) {
  size_t arg_count = dests.size();
  DCHECK_EQ(arg_count, srcs.size());
  DCHECK_EQ(arg_count, refs.size());
  auto get_mask = [](ManagedRegister reg) -> uint64_t {
    Loongarch64ManagedRegister loongarch64_reg = reg.AsLoongarch64();
    if (loongarch64_reg.IsXRegister()) {
      size_t core_reg_number = static_cast<size_t>(loongarch64_reg.AsXRegister());
      DCHECK_LT(core_reg_number, 31u);  // xSP, xZR not allowed.
      return UINT64_C(1) << core_reg_number;
    } else {
      DCHECK(loongarch64_reg.IsFRegister());
      size_t fp_reg_number = static_cast<size_t>(loongarch64_reg.AsFRegister());
      DCHECK_LT(fp_reg_number, 32u);
      return (UINT64_C(1) << 32u) << fp_reg_number;
    }
  };
  // Collect registers to move while storing/copying args to stack slots.
  // More than 8 core or FP reg args are very rare, so we do not optimize
  // for that case by using LDP/STP.
  // TODO: LDP/STP will be useful for normal and @FastNative where we need
  // to spill even the leading arguments.
  uint64_t src_regs = 0u;
  uint64_t dest_regs = 0u;
  for (size_t i = 0; i != arg_count; ++i) {
    const ArgumentLocation& src = srcs[i];
    const ArgumentLocation& dest = dests[i];
    const FrameOffset ref = refs[i];
    if (ref != kInvalidReferenceOffset) {
      DCHECK_EQ(src.GetSize(), kObjectReferenceSize);
      DCHECK_EQ(dest.GetSize(), static_cast<size_t>(kRiscv64PointerSize));
    } else {
      DCHECK(src.GetSize() == 4u || src.GetSize() == 8u) << src.GetSize();
      DCHECK(dest.GetSize() == 4u || dest.GetSize() == 8u) << dest.GetSize();
      DCHECK_LE(src.GetSize(), dest.GetSize());
    }
    if (dest.IsRegister()) {
      if (src.IsRegister() && src.GetRegister().Equals(dest.GetRegister())) {
        // No move is necessary but we may need to convert a reference to a `jobject`.
        if (ref != kInvalidReferenceOffset) {
          CreateJObject(dest.GetRegister(), ref, src.GetRegister(), /*null_allowed=*/ i != 0u);
        }
      } else {
        if (src.IsRegister()) {
          src_regs |= get_mask(src.GetRegister());
        }
        dest_regs |= get_mask(dest.GetRegister());
      }
    } else {
      ScratchRegisterScope srs(&asm_);
      Loongarch64ManagedRegister reg = src.IsRegister()
          ? src.GetRegister().AsLoongarch64()
          : Loongarch64ManagedRegister::FromXRegister(srs.AllocateXRegister());
      if (!src.IsRegister()) {
        if (ref != kInvalidReferenceOffset) {
          // We're loading the reference only for comparison with null, so it does not matter
          // if we sign- or zero-extend but let's correctly zero-extend the reference anyway.
          __ Load_WU(reg.AsLoongarch64().AsXRegister(), SP, src.GetFrameOffset().SizeValue());
        } else {
          Load(reg, src.GetFrameOffset(), src.GetSize());
        }
      }
      if (ref != kInvalidReferenceOffset) {
        DCHECK_NE(i, 0u);
        CreateJObject(reg, ref, reg, /*null_allowed=*/ true);
      }
      Store(dest.GetFrameOffset(), reg, dest.GetSize());
    }
  }

  // Fill destination registers.
  // There should be no cycles, so this simple algorithm should make progress.
  while (dest_regs != 0u) {
    uint64_t old_dest_regs = dest_regs;
    for (size_t i = 0; i != arg_count; ++i) {
      const ArgumentLocation& src = srcs[i];
      const ArgumentLocation& dest = dests[i];
      const FrameOffset ref = refs[i];
      if (!dest.IsRegister()) {
        continue;  // Stored in first loop above.
      }
      uint64_t dest_reg_mask = get_mask(dest.GetRegister());
      if ((dest_reg_mask & dest_regs) == 0u) {
        continue;  // Equals source, or already filled in one of previous iterations.
      }
      if ((dest_reg_mask & src_regs) != 0u) {
        continue;  // Cannot clobber this register yet.
      }
      if (src.IsRegister()) {
        if (ref != kInvalidReferenceOffset) {
          DCHECK_NE(i, 0u);  // The `this` arg remains in the same register (handled above).
          CreateJObject(dest.GetRegister(), ref, src.GetRegister(), /*null_allowed=*/ true);
        } else {
          Move(dest.GetRegister(), src.GetRegister(), dest.GetSize());
        }
        src_regs &= ~get_mask(src.GetRegister());  // Allow clobbering source register.
      } else {
        Load(dest.GetRegister(), src.GetFrameOffset(), src.GetSize());
        // No `jobject` conversion needed. There are enough arg registers in managed ABI
        // to hold all references that yield a register arg `jobject` in native ABI.
        DCHECK_EQ(ref, kInvalidReferenceOffset);
      }
      dest_regs &= ~get_mask(dest.GetRegister());  // Destination register was filled.
    }
    CHECK_NE(old_dest_regs, dest_regs);
    DCHECK_EQ(0u, dest_regs & ~old_dest_regs);
  }
}

void Loongarch64JNIMacroAssembler::Move(ManagedRegister m_dest, ManagedRegister m_src, size_t size) {
  // Note: This function is used only for moving between GPRs.
  // FP argument registers hold the same arguments in managed and native ABIs.
  DCHECK(size == 4u || size == 8u) << size;
  Loongarch64ManagedRegister dest = m_dest.AsLoongarch64();
  Loongarch64ManagedRegister src = m_src.AsLoongarch64();
  DCHECK(dest.IsXRegister());
  DCHECK(src.IsXRegister());
  if (!dest.Equals(src)) {
    __ Move(dest.AsXRegister(), src.AsXRegister());
  }
}

void Loongarch64JNIMacroAssembler::Move(ManagedRegister m_dest, size_t value) {
  DCHECK(m_dest.AsLoongarch64().IsXRegister());
  __ LoadConst64(m_dest.AsLoongarch64().AsXRegister(), dchecked_integral_cast<int64_t>(value));
}

void Loongarch64JNIMacroAssembler::SignExtend([[maybe_unused]]ManagedRegister mreg, [[maybe_unused]]size_t size) {
  LOG(FATAL) << "The result is already sign-extended in the native ABI.";
  UNREACHABLE();
}

void Loongarch64JNIMacroAssembler::ZeroExtend([[maybe_unused]]ManagedRegister mreg, [[maybe_unused]]size_t size) {
  LOG(FATAL) << "The result is already zero-extended in the native ABI.";
  UNREACHABLE();
}

void Loongarch64JNIMacroAssembler::GetCurrentThread(ManagedRegister tr) {
  DCHECK(tr.AsLoongarch64().IsXRegister());
  __ Move(tr.AsLoongarch64().AsXRegister(), TR);
}

void Loongarch64JNIMacroAssembler::GetCurrentThread(FrameOffset offset) {
  __ Store_D(TR, SP, offset.Int32Value());
}

void Loongarch64JNIMacroAssembler::DecodeJNITransitionOrLocalJObject(ManagedRegister m_reg,
                                                                 JNIMacroLabel* slow_path,
                                                                 JNIMacroLabel* resume) {
  // This implements the fast-path of `Thread::DecodeJObject()`.
  constexpr int64_t kGlobalOrWeakGlobalMask = IndirectReferenceTable::GetGlobalOrWeakGlobalMask();
  DCHECK(IsInt<12>(kGlobalOrWeakGlobalMask));
  constexpr int64_t kIndirectRefKindMask = IndirectReferenceTable::GetIndirectRefKindMask();
  DCHECK(IsInt<12>(kIndirectRefKindMask));
  XRegister reg = m_reg.AsLoongarch64().AsXRegister();
  __ Beqz(reg, Loongarch64JNIMacroLabel::Cast(resume)->AsLoongarch64());  // Skip test and load for null.
  __ Andi(TMP, reg, kGlobalOrWeakGlobalMask);
  __ Bnez(TMP, Loongarch64JNIMacroLabel::Cast(slow_path)->AsLoongarch64());
  __ Andi(reg, reg, ~kIndirectRefKindMask);
  __ Load_WU(reg, reg, 0);
}

void Loongarch64JNIMacroAssembler::VerifyObject([[maybe_unused]] ManagedRegister m_src,
                                            [[maybe_unused]] bool could_be_null) {
  // TODO: not validating references.
}

void Loongarch64JNIMacroAssembler::VerifyObject([[maybe_unused]] FrameOffset src,
                                            [[maybe_unused]] bool could_be_null) {
  // TODO: not validating references.
}

void Loongarch64JNIMacroAssembler::Jump(ManagedRegister m_base, Offset offs) {
  Loongarch64ManagedRegister base = m_base.AsLoongarch64();
  CHECK(base.IsXRegister()) << base;
  XRegister scratch = TMP;
  __ Load_D(scratch, base.AsXRegister(), offs.Int32Value());
  __ Jr(scratch);
}

void Loongarch64JNIMacroAssembler::Call(ManagedRegister m_base, Offset offs) {
  Loongarch64ManagedRegister base = m_base.AsLoongarch64();
  CHECK(base.IsXRegister()) << base;
  XRegister scratch = TMP2;
  __ Load_D(scratch, base.AsXRegister(), offs.Int32Value());
  __ Jirl(RA, scratch, 0);
}

void Loongarch64JNIMacroAssembler::CallFromThread(ThreadOffset64 offset) {
  Call(Loongarch64ManagedRegister::FromXRegister(TR), offset);
}

void Loongarch64JNIMacroAssembler::TryToTransitionFromRunnableToNative(
    JNIMacroLabel* label,
    ArrayRef<const ManagedRegister> scratch_regs) {
  // TODO(loongarch64): Implement this.
  UNIMPLEMENTED(FATAL) << "TryToTransitionFromRunnableToNative";
  UNUSED(label, scratch_regs);
}

void Loongarch64JNIMacroAssembler::TryToTransitionFromNativeToRunnable(
    JNIMacroLabel* label,
    ArrayRef<const ManagedRegister> scratch_regs,
    ManagedRegister return_reg) {
  // TODO(loongarch64): Implement this.
  UNIMPLEMENTED(FATAL) << "TryToTransitionFromNativeToRunnable";
  UNUSED(label, scratch_regs, return_reg);
}

void Loongarch64JNIMacroAssembler::SuspendCheck(JNIMacroLabel* label) {
  ScratchRegisterScope srs(&asm_);
  XRegister tmp = srs.AllocateXRegister();
  __ Load_W(tmp, TR, Thread::ThreadFlagsOffset<kRiscv64PointerSize>().Int32Value());
  DCHECK(IsInt<12>(dchecked_integral_cast<int32_t>(Thread::SuspendOrCheckpointRequestFlags())));
  __ Andi(tmp, tmp, dchecked_integral_cast<int32_t>(Thread::SuspendOrCheckpointRequestFlags()));
  __ Bnez(tmp, Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64());
}

void Loongarch64JNIMacroAssembler::ExceptionPoll(JNIMacroLabel* label) {
  ScratchRegisterScope srs(&asm_);
  XRegister tmp = srs.AllocateXRegister();
  __ Load_D(tmp, TR, Thread::ExceptionOffset<kRiscv64PointerSize>().Int32Value());
  __ Bnez(tmp, Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64());
}

void Loongarch64JNIMacroAssembler::Bind(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  __ Bind(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64());
}

void Loongarch64JNIMacroAssembler::DeliverPendingException() {
  // Pass exception object as argument.
  // Don't care about preserving A0 as this won't return.
  // Note: The scratch register from `ExceptionPoll()` may have been clobbered.
  __ Load_D(A0, TR, Thread::ExceptionOffset<kRiscv64PointerSize>().Int32Value());
  __ Load_D(RA, TR, QUICK_ENTRYPOINT_OFFSET(kRiscv64PointerSize, pDeliverException).Int32Value());
  __ Jirl(RA, RA, 0);
  // Call should never return.
  __ brk(0x5);
}

std::unique_ptr<JNIMacroLabel> Loongarch64JNIMacroAssembler::CreateLabel() {
  return std::unique_ptr<JNIMacroLabel>(new (asm_.GetAllocator()) Loongarch64JNIMacroLabel());
}

void Loongarch64JNIMacroAssembler::Jump(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  __ B(down_cast<Loongarch64Label*>(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64()));
}

void Loongarch64JNIMacroAssembler::TestGcMarking(JNIMacroLabel* label, JNIMacroUnaryCondition cond) {
  CHECK(label != nullptr);

  DCHECK_EQ(Thread::IsGcMarkingSize(), 4u);

  ScratchRegisterScope srs(&asm_);
  XRegister test_reg = srs.AllocateXRegister();
  int32_t is_gc_marking_offset = Thread::IsGcMarkingOffset<kLoongarch64PointerSize>().Int32Value();
  __ Load_W(test_reg, TR, is_gc_marking_offset);
  switch (cond) {
    case JNIMacroUnaryCondition::kZero:
      __ Beqz(test_reg, down_cast<Loongarch64Label*>(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64()));
      break;
    case JNIMacroUnaryCondition::kNotZero:
      __ Bnez(test_reg, down_cast<Loongarch64Label*>(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64()));
      break;
  }
}

void Loongarch64JNIMacroAssembler::TestMarkBit(ManagedRegister m_ref,
                                           JNIMacroLabel* label,
                                           JNIMacroUnaryCondition cond) {
  XRegister ref = m_ref.AsLoongarch64().AsXRegister();
  ScratchRegisterScope srs(&asm_);
  XRegister tmp = srs.AllocateXRegister();
  __ Load_W(tmp, ref, mirror::Object::MonitorOffset().Int32Value());
  // Move the bit we want to check to the sign bit, so that we can use BGEZ/BLTZ
  // to check it. Extracting the bit for BEQZ/BNEZ would require one more instruction.
  static_assert(LockWord::kMarkBitStateSize == 1u);
  __ Slli_w(tmp, tmp, 31 - LockWord::kMarkBitStateShift);
  switch (cond) {
    case JNIMacroUnaryCondition::kZero:
      __ Bgez(tmp, Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64());
      break;
    case JNIMacroUnaryCondition::kNotZero:
      __ Bltz(tmp, Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64());
      break;
  }
}

void Loongarch64JNIMacroAssembler::TestByteAndJumpIfNotZero(uintptr_t address, JNIMacroLabel* label) {
  int32_t small_offset = dchecked_integral_cast<int32_t>(address & 0xfff) -
                         dchecked_integral_cast<int32_t>((address & 0x800) << 1);
  int64_t remainder = static_cast<int64_t>(address) - small_offset;
  ScratchRegisterScope srs(&asm_);
  XRegister tmp = srs.AllocateXRegister();
  __ LoadConst64(tmp, remainder);
  __ Ld_B(tmp, tmp, small_offset);
  __ Bnez(tmp, down_cast<Loongarch64Label*>(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64()));
}

void Loongarch64JNIMacroAssembler::CreateJObject(ManagedRegister m_dest,
                                             FrameOffset spilled_reference_offset,
                                             ManagedRegister m_ref,
                                             bool null_allowed) {
  Loongarch64ManagedRegister dest = m_dest.AsLoongarch64();
  Loongarch64ManagedRegister ref = m_ref.AsLoongarch64();
  DCHECK(dest.IsXRegister());
  DCHECK(ref.IsXRegister());

  Loongarch64Label null_label;
  if (null_allowed) {
    if (!dest.Equals(ref)) {
      __ Li(dest.AsXRegister(), 0);
    }
    __ Beqz(ref.AsXRegister(), &null_label);
  }
  __ AddConst64(dest.AsXRegister(), SP, spilled_reference_offset.Int32Value());
  if (null_allowed) {
    __ Bind(&null_label);
  }
}

#undef ___

}  // namespace loongarch64
}  // namespace art
