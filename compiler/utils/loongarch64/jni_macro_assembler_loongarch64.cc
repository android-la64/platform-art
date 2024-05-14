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
#include <cstdint>

#include "android-base/macros.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "managed_register_loongarch64.h"
#include "offsets.h"
#include "thread.h"
#include "utils/managed_register.h"

namespace art {
namespace loongarch64 {

#define __ asm_.

Loongarch64JNIMacroAssembler::~Loongarch64JNIMacroAssembler() {
}

void Loongarch64JNIMacroAssembler::FinalizeCode() {
  __ FinalizeCode();
}

void Loongarch64JNIMacroAssembler::BuildFrame(size_t frame_size,
                                          ManagedRegister method_reg,
                                          ArrayRef<const ManagedRegister> callee_save_regs) {
  // TODO(loongarch64): Implement this.
  UNUSED(frame_size, method_reg, callee_save_regs);
}

void Loongarch64JNIMacroAssembler::RemoveFrame(size_t frame_size,
                                           ArrayRef<const ManagedRegister> callee_save_regs,
                                           bool may_suspend) {
  // TODO(loongarch64): Implement this.
  UNUSED(frame_size, callee_save_regs, may_suspend);
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


void Loongarch64JNIMacroAssembler::Store(FrameOffset offs, ManagedRegister m_src, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(offs, m_src, size);
}

void Loongarch64JNIMacroAssembler::Store(ManagedRegister base,
                                     MemberOffset offs,
                                     ManagedRegister m_src,
                                     size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(base, offs, m_src, size);
}

void Loongarch64JNIMacroAssembler::StoreRawPtr(FrameOffset offs, ManagedRegister m_src) {
  // TODO(loongarch64): Implement this.
  UNUSED(offs, m_src);
}

void Loongarch64JNIMacroAssembler::StoreStackPointerToThread(ThreadOffset64 tr_offs) {
  // TODO(loongarch64): Implement this.
  UNUSED(tr_offs);
}

// unused
void Loongarch64JNIMacroAssembler::StoreRef(FrameOffset dest, ManagedRegister src) {
  UNUSED(dest, src);
}

void Loongarch64JNIMacroAssembler::StoreImmediateToFrame(FrameOffset dest, uint32_t imm) {
  UNUSED(dest, imm);
}

void Loongarch64JNIMacroAssembler::StoreStackOffsetToThread(ThreadOffset64 thr_offs, FrameOffset fr_offs) {
  UNUSED(thr_offs, fr_offs);
}

void Loongarch64JNIMacroAssembler::StoreSpanning(FrameOffset dest, ManagedRegister src, FrameOffset in_off) {
  UNUSED(dest, src, in_off);
}

void Loongarch64JNIMacroAssembler::Load(ManagedRegister m_dest, FrameOffset src, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, src, size);
}

void Loongarch64JNIMacroAssembler::LoadFromThread(ManagedRegister dest, ThreadOffset64 src, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(dest, src, size);
}

void Loongarch64JNIMacroAssembler::LoadRawPtrFromThread(ManagedRegister m_dest, ThreadOffset64 offs) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, offs);
}

// unused
void Loongarch64JNIMacroAssembler::LoadRef(ManagedRegister dest, FrameOffset src) {
  UNUSED(dest, src);
}

void Loongarch64JNIMacroAssembler::LoadRef(ManagedRegister dest, ManagedRegister base, MemberOffset offs, bool unpoison_reference) {
  UNUSED(dest, base, offs, unpoison_reference);
}

void Loongarch64JNIMacroAssembler::LoadRawPtr(ManagedRegister dest, ManagedRegister base, Offset offs) {
  UNUSED(dest, base, offs);
}

void Loongarch64JNIMacroAssembler::MoveArguments(ArrayRef<ArgumentLocation> dests,
                                             ArrayRef<ArgumentLocation> srcs) {
  // TODO(loongarch64): Implement this.
  UNUSED(dests, srcs);
}

void Loongarch64JNIMacroAssembler::Move(ManagedRegister m_dest, ManagedRegister m_src, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_dest, m_src, size);
}

void Loongarch64JNIMacroAssembler::CopyRawPtrFromThread(FrameOffset fr_offs, ThreadOffset64 tr_offs) {
  // TODO(loongarch64): Implement this.
  UNUSED(fr_offs, tr_offs);
}

void Loongarch64JNIMacroAssembler::Copy(FrameOffset dest, FrameOffset src, size_t size) {
  DCHECK(size == 4 || size == 8) << size;
  if(size == 8) {
    __ Load_D(TMP2, SP, src.Int32Value());
    __ Store_D(TMP2, SP, dest.Int32Value());
  } else {
    __ Load_W(TMP2, SP, src.Int32Value());
    __ Store_W(TMP2, SP, dest.Int32Value());
  }
}

void Loongarch64JNIMacroAssembler::MemoryBarrier(ManagedRegister m_scratch) {
  // TODO(loongarch64): Implement this.
  UNUSED(m_scratch);
}

// unused
void Loongarch64JNIMacroAssembler::CopyRawPtrToThread(ThreadOffset64 thr_offs, FrameOffset fr_offs, ManagedRegister scratch) {
  UNUSED(thr_offs, fr_offs, scratch);
}

void Loongarch64JNIMacroAssembler::CopyRef(FrameOffset dest, FrameOffset src) {
  UNUSED(dest, src);
}

void Loongarch64JNIMacroAssembler::CopyRef(FrameOffset dest, ManagedRegister base, MemberOffset offs, bool unpoison_reference) {
  UNUSED(dest, base, offs, unpoison_reference);
}

void Loongarch64JNIMacroAssembler::Copy(FrameOffset dest, ManagedRegister src_base, Offset src_offset, ManagedRegister scratch, size_t size) {
  UNUSED(dest, src_base, src_offset, scratch, size);
}

void Loongarch64JNIMacroAssembler::Copy(ManagedRegister dest_base, Offset dest_offset, FrameOffset src, ManagedRegister scratch,
            size_t size) {
  UNUSED(dest_base, dest_offset, src, scratch, size);
}

void Loongarch64JNIMacroAssembler::Copy(FrameOffset dest, FrameOffset src_base, Offset src_offset, ManagedRegister scratch, size_t size) {
  UNUSED(dest, src_base, src_offset, scratch, size);
}

void Loongarch64JNIMacroAssembler::Copy(ManagedRegister dest, Offset dest_offset, ManagedRegister src, Offset src_offset, ManagedRegister scratch, size_t size) {
  UNUSED(dest, dest_offset, src, src_offset, scratch, size);
}

void Loongarch64JNIMacroAssembler::Copy(FrameOffset dest, Offset dest_offset, FrameOffset src, Offset src_offset, ManagedRegister scratch, size_t size) {
  UNUSED(dest, dest_offset, src, src_offset, scratch, size);
}

void Loongarch64JNIMacroAssembler::SignExtend(ManagedRegister mreg, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(mreg, size);
}

void Loongarch64JNIMacroAssembler::ZeroExtend(ManagedRegister mreg, size_t size) {
  // TODO(loongarch64): Implement this.
  UNUSED(mreg, size);
}

void Loongarch64JNIMacroAssembler::GetCurrentThread(ManagedRegister tr) {
  // TODO(loongarch64): Implement this.
  UNUSED(tr);
}

void Loongarch64JNIMacroAssembler::GetCurrentThread(FrameOffset offset) {
  // TODO(loongarch64): Implement this.
  UNUSED(offset);
}

// unused
void Loongarch64JNIMacroAssembler::CreateJObject(ManagedRegister out_reg, FrameOffset spilled_reference_offset, ManagedRegister in_reg, bool null_allowed) {
  UNUSED(out_reg, spilled_reference_offset, in_reg, null_allowed);
}

void Loongarch64JNIMacroAssembler::CreateJObject(FrameOffset out_off, FrameOffset spilled_reference_offset, bool null_allowed) {
  UNUSED(out_off, spilled_reference_offset, null_allowed);
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

void Loongarch64JNIMacroAssembler::Call(FrameOffset base, Offset offs) {
  // Call *(*(SP + base) + offset)
  __ Load_D(TMP2, SP, base.Int32Value());
  __ Load_D(TMP2, TMP2, offs.Int32Value());
  __ Jirl(RA, TMP2, 0);
}

void Loongarch64JNIMacroAssembler::CallFromThread(ThreadOffset64 offset) {
  Call(Loongarch64ManagedRegister::FromXRegister(TR), offset);
}

void Loongarch64JNIMacroAssembler::ExceptionPoll(size_t stack_adjust) {
  UNUSED(stack_adjust);
}

std::unique_ptr<JNIMacroLabel> Loongarch64JNIMacroAssembler::CreateLabel() {
  return std::unique_ptr<JNIMacroLabel>(new Loongarch64JNIMacroLabel());
}

void Loongarch64JNIMacroAssembler::Jump(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  __ B(down_cast<Loongarch64Label*>(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64()));
}

void Loongarch64JNIMacroAssembler::TestGcMarking(JNIMacroLabel* label, JNIMacroUnaryCondition cond) {
  CHECK(label != nullptr);

  DCHECK_EQ(Thread::IsGcMarkingSize(), 4u);
  DCHECK(kUseReadBarrier);

  XRegister test_reg = TMP;
  int32_t is_gc_marking_offset = Thread::IsGcMarkingOffset<kArm64PointerSize>().Int32Value();
  __ Load_W(test_reg, TR, is_gc_marking_offset);
  switch (cond) {
    case JNIMacroUnaryCondition::kZero:
      __ Beqz(test_reg, down_cast<Loongarch64Label*>(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64()));
      break;
    case JNIMacroUnaryCondition::kNotZero:
      __ Bnez(test_reg, down_cast<Loongarch64Label*>(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64()));
      break;
    default:
      LOG(FATAL) << "Not implemented unary condition: " << static_cast<int>(cond);
      UNREACHABLE();
  }
}

void Loongarch64JNIMacroAssembler::Bind(JNIMacroLabel* label) {
  CHECK(label != nullptr);
  __ Bind(Loongarch64JNIMacroLabel::Cast(label)->AsLoongarch64());
}

#undef ___

}  // namespace loongarch64
}  // namespace art
