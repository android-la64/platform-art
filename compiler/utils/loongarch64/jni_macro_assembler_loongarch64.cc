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

#include "base/bit_utils_iterator.h"
#include "dwarf/register.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "gc_root.h"
#include "indirect_reference_table.h"
#include "lock_word.h"
#include "managed_register_loongarch64.h"
#include "offsets.h"
#include "stack_reference.h"
#include "thread.h"

namespace art HIDDEN {
namespace loongarch64 {

static constexpr size_t kSpillSize = 8;  // Both GPRs and FPRs

static __attribute__((unused)) std::pair<uint32_t, uint32_t> GetCoreAndFpSpillMasks(
    ArrayRef<const ManagedRegister> callee_save_regs) {
// TODO：This function is used by BuildFrame/RemoveFrame for calculating callee-save mask.
// Temporarily marked as 'unused' to avoid -Wunused-function/-Werror build failure
// because those functions are not yet implemented. Remove __attribute__((unused))
// after BuildFrame/RemoveFrame are implemented and use this function.
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

Loongarch64JNIMacroAssembler::~Loongarch64JNIMacroAssembler() {}

void Loongarch64JNIMacroAssembler::FinalizeCode() {
  // TODO(loongarch64): Implement this.
}

void Loongarch64JNIMacroAssembler::BuildFrame(size_t frame_size,
                                              ManagedRegister method_reg,
                                              ArrayRef<const ManagedRegister> callee_save_regs) {
  // TODO(loongarch64): Implement this.
  // Increase frame to required size.
  // Must at least have space for Method* if we're going to spill it.
  UNUSED(frame_size, method_reg, callee_save_regs);
}

void Loongarch64JNIMacroAssembler::RemoveFrame(size_t frame_size,
                                               ArrayRef<const ManagedRegister> callee_save_regs,
                                               [[maybe_unused]] bool may_suspend) {
  // TODO(loongarch64): Implement this.
  // Restore callee-saves.
  UNUSED(frame_size, callee_save_regs, may_suspend);
}

void Loongarch64JNIMacroAssembler::IncreaseFrameSize(size_t adjust) {
  // TODO(loongarch64): Implement this.
  UNUSED(adjust);
}

void Loongarch64JNIMacroAssembler::DecreaseFrameSize(size_t adjust) {
  // TODO(loongarch64): Implement this.
  UNUSED(adjust);
}

ManagedRegister Loongarch64JNIMacroAssembler::CoreRegisterWithSize(ManagedRegister src, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(src, size);
  return src;
}

void Loongarch64JNIMacroAssembler::Store(FrameOffset offs, ManagedRegister m_src, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(offs, m_src, size);
}

void Loongarch64JNIMacroAssembler::Store(ManagedRegister m_base,
                                         MemberOffset offs,
                                         ManagedRegister m_src,
                                         size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_base, offs, m_src, size);
}

void Loongarch64JNIMacroAssembler::StoreRawPtr(FrameOffset offs, ManagedRegister m_src) {
  // TODO(loongarch64): Implement this.
  UNUSED(offs, m_src);
}

void Loongarch64JNIMacroAssembler::StoreStackPointerToThread(ThreadOffset64 offs, bool tag_sp) {
  // TODO(loongarch64): Implement this.
  UNUSED(offs, tag_sp);
}

void Loongarch64JNIMacroAssembler::Load(ManagedRegister m_dest, FrameOffset offs, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, offs, size);
}

void Loongarch64JNIMacroAssembler::Load(ManagedRegister m_dest,
                                        ManagedRegister m_base,
                                        MemberOffset offs,
                                        size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, m_base, offs, size);
}

void Loongarch64JNIMacroAssembler::LoadRawPtrFromThread(ManagedRegister m_dest, ThreadOffset64 offs) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, offs);
}

void Loongarch64JNIMacroAssembler::LoadGcRootWithoutReadBarrier(ManagedRegister m_dest,
                                                                ManagedRegister m_base,
                                                                MemberOffset offs) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, m_base, offs);
}

void Loongarch64JNIMacroAssembler::LoadStackReference(ManagedRegister m_dest, FrameOffset offs) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, offs);
}

void Loongarch64JNIMacroAssembler::MoveArguments(ArrayRef<ArgumentLocation> dests,
                                                 ArrayRef<ArgumentLocation> srcs,
                                                 ArrayRef<FrameOffset> refs) {
  // TODO(loongarch64): Implement this.
  UNUSED(dests, srcs, refs);
}

void Loongarch64JNIMacroAssembler::Move(ManagedRegister m_dest, ManagedRegister m_src, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, m_src, size);
}

void Loongarch64JNIMacroAssembler::Move(ManagedRegister m_dest, size_t value) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, value);
}

void Loongarch64JNIMacroAssembler::SignExtend([[maybe_unused]] ManagedRegister mreg,
                                              [[maybe_unused]] size_t size) {
  // TODO(loongarch64): Implement this.
}

void Loongarch64JNIMacroAssembler::ZeroExtend([[maybe_unused]] ManagedRegister mreg,
                                              [[maybe_unused]] size_t size) {
  // TODO(loongarch64): Implement this.
}

void Loongarch64JNIMacroAssembler::GetCurrentThread(ManagedRegister dest) {
  // TODO(loongarch64): Implement this.
  UNUSED(dest);
}

void Loongarch64JNIMacroAssembler::GetCurrentThread(FrameOffset offset) {
  // TODO(loongarch64): Implement this.
  UNUSED(offset);
}

void Loongarch64JNIMacroAssembler::DecodeJNITransitionOrLocalJObject(ManagedRegister m_reg,
                                                                     JNIMacroLabel* slow_path,
                                                                     JNIMacroLabel* resume) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_reg, slow_path, resume);
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
  // TODO(loongarch64): Implement this.
  UNUSED(m_base, offs);
}

void Loongarch64JNIMacroAssembler::Call(ManagedRegister m_base, Offset offs) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_base, offs);
}

void Loongarch64JNIMacroAssembler::CallFromThread(ThreadOffset64 offset) {
  // TODO(loongarch64): Implement this.
  UNUSED(offset);
}

void Loongarch64JNIMacroAssembler::TryToTransitionFromRunnableToNative(
    JNIMacroLabel* label,
    ArrayRef<const ManagedRegister> scratch_regs) {
  // TODO(loongarch64): Implement this.
  UNUSED(label, scratch_regs);
}

void Loongarch64JNIMacroAssembler::TryToTransitionFromNativeToRunnable(
    JNIMacroLabel* label,
    ArrayRef<const ManagedRegister> scratch_regs,
    ManagedRegister return_reg) {
  // TODO(loongarch64): Implement this.
  UNUSED(label, scratch_regs, return_reg);
}

void Loongarch64JNIMacroAssembler::SuspendCheck(JNIMacroLabel* label) {
  // TODO(loongarch64): Implement this.
  UNUSED(label);
}

void Loongarch64JNIMacroAssembler::ExceptionPoll(JNIMacroLabel* label) {
  // TODO(loongarch64): Implement this.
  UNUSED(label);
}

void Loongarch64JNIMacroAssembler::DeliverPendingException() {
  // TODO(loongarch64): Implement this.
}

std::unique_ptr<JNIMacroLabel> Loongarch64JNIMacroAssembler::CreateLabel() {
  // TODO(loongarch64): Implement this.
  return std::unique_ptr<JNIMacroLabel>(nullptr);
}

void Loongarch64JNIMacroAssembler::Jump(JNIMacroLabel* label) {
  // TODO(loongarch64): Implement this.
  UNUSED(label);
}

void Loongarch64JNIMacroAssembler::TestGcMarking(JNIMacroLabel* label, JNIMacroUnaryCondition cond) {
  // TODO(loongarch64): Implement this.
  UNUSED(label, cond);
}

void Loongarch64JNIMacroAssembler::TestMarkBit(ManagedRegister m_ref,
                                               JNIMacroLabel* label,
                                               JNIMacroUnaryCondition cond) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_ref, label, cond);
}

void Loongarch64JNIMacroAssembler::TestByteAndJumpIfNotZero(uintptr_t address, JNIMacroLabel* label) {
  // TODO(loongarch64): Implement this.
  UNUSED(address, label);
}

void Loongarch64JNIMacroAssembler::Bind(JNIMacroLabel* label) {
  // TODO(loongarch64): Implement this.
  UNUSED(label);
}

void Loongarch64JNIMacroAssembler::CreateJObject(ManagedRegister m_dest,
                                                 FrameOffset spilled_reference_offset,
                                                 ManagedRegister m_ref,
                                                 bool null_allowed) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, spilled_reference_offset, m_ref, null_allowed);
}

#undef __

}  // namespace loongarch64
}  // namespace art
