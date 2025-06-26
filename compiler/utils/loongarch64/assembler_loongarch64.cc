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

#include "assembler_loongarch64.h"
#include <cstdint>

#include "arch/loongarch64/registers_loongarch64.h"
#include "base/bit_utils.h"
#include "base/casts.h"
#include "base/memory_region.h"

namespace art {
namespace loongarch64 {

static_assert(static_cast<size_t>(kLoongarch64PointerSize) == kLoongarch64DoublewordSize,
              "Unexpected Loongarch64 pointer size.");
static_assert(kLoongarch64PointerSize == PointerSize::k64, "Unexpected Loongarch64 pointer size.");

// Split 32-bit offset into an `imm20` for PCADDU18I/PCALAU12I and
// a signed 12-bit short offset for ADDI/etc.(what's more?)
ALWAYS_INLINE static inline std::pair<uint32_t, int32_t> SplitOffset(bool bits12, int32_t offset) {
  // The highest 0x800 values are out of range.
  DCHECK_LT(offset, 0x7ffff800);
  // Round `offset` to nearest 4KiB or 128KiB offset.
  int32_t near_offset = (offset + (bits12 ? 0x800 : 0x20000)) & (bits12 ? ~0xfff : ~0x3ffff);
  // Calculate the short offset.
  int32_t short_offset = offset - near_offset;
  DCHECK(IsInt<12>(short_offset) || IsInt<18>(short_offset));
  // Extract the `imm20`.
  uint32_t imm20 = near_offset >> (bits12 ? 12 : 18) & 0xfffff;
  // Return the result as a pair.
  return std::make_pair(imm20, short_offset);
}


void Loongarch64Assembler::FinalizeCode() {
  CHECK(!finalized_);
  Assembler::FinalizeCode();
  ReserveJumpTableSpace();
  EmitLiterals();
  PromoteBranches();
  EmitBranches();
  EmitJumpTables();
  PatchCFI();
  finalized_ = true;
}

void Loongarch64Assembler::Emit(uint32_t value) {
  if (overwriting_) {
    // Branches to labels are emitted into their placeholders here.
    buffer_.Store<uint32_t>(overwrite_location_, value);
    overwrite_location_ += sizeof(uint32_t);
  } else {
    // Other instructions are simply appended at the end here.
    AssemblerBuffer::EnsureCapacity ensured(&buffer_);
    buffer_.Emit<uint32_t>(value);
  }
}

/////////////////////////////// LOONGARCH64 VARIANTS extension ///////////////////////////////


/////////////////////////////// LOONGARCH64 Transfer Instructions ///////////////////////////////

void Loongarch64Assembler::Beqz(XRegister rs, int32_t offset21) {
  Emit1RI21(0x10, offset21, rs);
}

void Loongarch64Assembler::Bnez(XRegister rs, int32_t offset21) {
  Emit1RI21(0x11, offset21, rs);
}

void Loongarch64Assembler::Bceqz(FCCRegister rs, int32_t offset21) {
  Emit1RI21(0x12, offset21, (0x0 << 3)|rs);
}

void Loongarch64Assembler::Bcnez(FCCRegister rs, int32_t offset21) {
  Emit1RI21(0x12, offset21, (0x1 << 3)|rs);
}

void Loongarch64Assembler::Jirl(XRegister rd, XRegister rs1, int32_t offset16) {
  Emit2RI16_B(0x13, offset16, rs1, rd);
}

void Loongarch64Assembler::B(int32_t offset26) {
  EmitI26(0x14, offset26);
}

void Loongarch64Assembler::Bl(int32_t offset26) {
  EmitI26(0x15, offset26);
}

void Loongarch64Assembler::Beq(XRegister rs1, XRegister rd, int32_t offset16) {
  Emit2RI16_B(0x16, offset16, rs1, rd);
}

void Loongarch64Assembler::Bne(XRegister rs1, XRegister rd, int32_t offset16) {
  Emit2RI16_B(0x17, offset16, rs1, rd);
}

void Loongarch64Assembler::Blt(XRegister rs1, XRegister rd, int32_t offset16) {
  Emit2RI16_B(0x18, offset16, rs1, rd);
}

void Loongarch64Assembler::Bge(XRegister rs1, XRegister rd, int32_t offset16) {
  Emit2RI16_B(0x19, offset16, rs1, rd);
}

void Loongarch64Assembler::Bltu(XRegister rs1, XRegister rd, int32_t offset16) {
  Emit2RI16_B(0x1a, offset16, rs1, rd);
}

void Loongarch64Assembler::Bgeu(XRegister rs1, XRegister rd, int32_t offset16) {
  Emit2RI16_B(0x1b, offset16, rs1, rd);
}

void Loongarch64Assembler::LoadConst32(XRegister rd, int32_t value) {
  //LoadImmediate(rd, value, /*can_use_tmp=*/ false);  // No need to use TMP for 32-bit values.
  LoadImmediate(rd, value);  // No need to use TMP for 32-bit values.
}

void Loongarch64Assembler::LoadConst64(XRegister rd, int64_t value) {
  CHECK_NE(rd, TMP);
  //LoadImmediate(rd, value, /*can_use_tmp=*/ true);
  LoadImmediate(rd, value);
}

template <typename ValueType, typename Addi, typename AddLarge>
void AddConstImpl(XRegister rd,
                  XRegister rs1,
                  ValueType value,
                  Addi&& addi,
                  AddLarge&& add_large) {
  CHECK_NE(rs1, TMP);
  if (IsInt<12>(value)) {
    addi(rd, rs1, value);
    return;
  }

  constexpr int32_t kPositiveValueSimpleAdjustment = 0x7ff;
  constexpr int32_t kHighestValueForSimpleAdjustment = 2 * kPositiveValueSimpleAdjustment;
  constexpr int32_t kNegativeValueSimpleAdjustment = -0x800;
  constexpr int32_t kLowestValueForSimpleAdjustment = 2 * kNegativeValueSimpleAdjustment;

  if (value >= 0 && value <= kHighestValueForSimpleAdjustment) {
    addi(rd, rs1, kPositiveValueSimpleAdjustment);
    addi(rd, rd, value - kPositiveValueSimpleAdjustment);
  } else if (value < 0 && value >= kLowestValueForSimpleAdjustment) {
    addi(rd, rs1, kNegativeValueSimpleAdjustment);
    addi(rd, rd, value - kNegativeValueSimpleAdjustment);
  } else {
    add_large(rd, rs1, value);
  }
}

void Loongarch64Assembler::AddConst32(XRegister rd, XRegister rs1, int32_t value) {
  auto addiw = [&](XRegister rd, XRegister rs1, int32_t value) { Addi_W(rd, rs1, value); };
  auto add_large = [&](XRegister rd, XRegister rs1, int32_t value) {
    LoadConst32(TMP, value);
    Add_w(rd, rs1, TMP);
  };
  AddConstImpl(rd, rs1, value, addiw, add_large);
}

void Loongarch64Assembler::AddConst64(XRegister rd, XRegister rs1, int64_t value) {
  auto addi = [&](XRegister rd, XRegister rs1, int32_t value) { Addi_D(rd, rs1, value); };
  auto add_large = [&](XRegister rd, XRegister rs1, int64_t value) {
    // We cannot load TMP with `LoadConst64()`, so use `Li()`.
    // TODO(loongarch64): Refactor `LoadImmediate()` so that we can reuse the code to detect
    // when the code path using the `TMP` is beneficial, and use that path with a small
    // modification - instead of adding the two parts togeter, add them individually
    // to the input `rs1`. (This works as long as `rd` is not `TMP`.)
    Li(TMP, value);
    Add_d(rd, rs1, TMP);
  };
  AddConstImpl(rd, rs1, value, addi, add_large);
}

// Jumps and branches to a label
void Loongarch64Assembler::Bceqz(FCCRegister rs, Loongarch64Label* label) {
  FBcond(label, kCondEQZ, rs);
}

void Loongarch64Assembler::Bcnez(FCCRegister rs, Loongarch64Label* label) {
  FBcond(label, kCondNEZ, rs);
}

void Loongarch64Assembler::Beqz(XRegister rs, Loongarch64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondEQZ, rs, Zero);
}

void Loongarch64Assembler::Bnez(XRegister rs, Loongarch64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondNEZ, rs, Zero);
}

void Loongarch64Assembler::Jirl(XRegister rs, XRegister rt, Loongarch64Label* label, bool is_bare) {
  // ZQZ-TODO
  Bcond(label, is_bare, kCondGEU, rs, rt);
}

void Loongarch64Assembler::B(Loongarch64Label* label, bool is_bare) {
  Buncond(label, XRegister::Zero, is_bare);
}

void Loongarch64Assembler::Bl(Loongarch64Label* label, bool is_bare) {
  Buncond(label, XRegister::RA, is_bare);
}

void Loongarch64Assembler::Bgez(XRegister rs, Loongarch64Label* label, bool is_bare) {
  Bge(rs, Zero, label, is_bare);
}

void Loongarch64Assembler::Bltz(XRegister rs, Loongarch64Label* label, bool is_bare) {
  Blt(rs, Zero, label, is_bare);
}

void Loongarch64Assembler::Beq(XRegister rs, XRegister rt, Loongarch64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondEQ, rs, rt);
}

void Loongarch64Assembler::Bne(XRegister rs, XRegister rt, Loongarch64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondNE, rs, rt);
}

void Loongarch64Assembler::Blt(XRegister rs, XRegister rt, Loongarch64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondLT, rs, rt);
}

void Loongarch64Assembler::Bge(XRegister rs, XRegister rt, Loongarch64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondGE, rs, rt);
}

void Loongarch64Assembler::Bltu(XRegister rs, XRegister rt, Loongarch64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondLTU, rs, rt);
}

void Loongarch64Assembler::Bgeu(XRegister rs, XRegister rt, Loongarch64Label* label, bool is_bare) {
  Bcond(label, is_bare, kCondGEU, rs, rt);
}

void Loongarch64Assembler::Load_W(XRegister rd, Literal* literal) {
  DCHECK_EQ(literal->GetSize(), 4u);
  LoadLiteral(literal, rd, Branch::kLiteral);
}

void Loongarch64Assembler::Load_WU(XRegister rd, Literal* literal) {
  DCHECK_EQ(literal->GetSize(), 4u);
  LoadLiteral(literal, rd, Branch::kLiteralUnsigned);
}

void Loongarch64Assembler::Load_D(XRegister rd, Literal* literal) {
  DCHECK_EQ(literal->GetSize(), 8u);
  LoadLiteral(literal, rd, Branch::kLiteralLong);
}


/////////////////////////////// LOONGARCH64 "3R-Type" Instructions ///////////////////////////////
  // low-level ALU instructions : opcode from 000 0000 0000 0010
  //                                        ~ 000 0000 0000 0100
void Loongarch64Assembler::Alsl_w(XRegister rd, XRegister rj, XRegister rk, uint32_t sa2) {
  Emit3RI2(0x2, sa2, rk, rj, rd);
}

void Loongarch64Assembler::Alsl_wu(XRegister rd, XRegister rj, XRegister rk, uint32_t sa2) {
  Emit3RI2(0x3, sa2, rk, rj, rd);
}

void Loongarch64Assembler::Bytepick_w(XRegister rd, XRegister rj, XRegister rk, uint32_t sa2) {
  Emit3RI2(0x4, sa2, rk, rj, rd);
}

// low-level ALU instructions : opcode 000 0000 0001 0110
void Loongarch64Assembler::Alsl_d(XRegister rd, XRegister rj, XRegister rk, uint32_t sa2) {
  Emit3RI2(0x16, sa2, rk, rj, rd);
}

// low-level ALU instructions : opcode from 00 0000 0000 0011
void Loongarch64Assembler::Bytepick_d(XRegister rd, XRegister rj, XRegister rk, uint32_t sa3) {
  Emit3RI3(0x3, sa3, rk, rj, rd);
}

// low-level ALU instructions : opcode from 0 0000 0000 0010 0000 
//                                        ~ 0 0000 0000 0011 0011
void Loongarch64Assembler::Add_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x20, rs2, rs1, rd);
}

void Loongarch64Assembler::Add_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x21, rs2, rs1, rd);
}

void Loongarch64Assembler::Sub_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x22, rs2, rs1, rd);
}

void Loongarch64Assembler::Sub_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x23, rs2, rs1, rd);
}

void Loongarch64Assembler::Slt(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x24, rs2, rs1, rd);
}

void Loongarch64Assembler::Sltu(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x25, rs2, rs1, rd);
}

void Loongarch64Assembler::Maskeqz(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x26, rs2, rs1, rd);
}

void Loongarch64Assembler::Masknez(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x27, rs2, rs1, rd);
}

void Loongarch64Assembler::Nor(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x28, rs2, rs1, rd);
}

void Loongarch64Assembler::And(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x29, rs2, rs1, rd);
}

void Loongarch64Assembler::Or(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x2a, rs2, rs1, rd);
}

void Loongarch64Assembler::Xor(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x2b, rs2, rs1, rd);
}

void Loongarch64Assembler::Orn(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x2c, rs2, rs1, rd);
}

void Loongarch64Assembler::Andn(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x2d, rs2, rs1, rd);
}

void Loongarch64Assembler::Sll_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x2e, rs2, rs1, rd);
}

void Loongarch64Assembler::Srl_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x2f, rs2, rs1, rd);
}

void Loongarch64Assembler::Sra_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x30, rs2, rs1, rd);
}

void Loongarch64Assembler::Sll_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x31, rs2, rs1, rd);
}

void Loongarch64Assembler::Srl_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x32, rs2, rs1, rd);
}

void Loongarch64Assembler::Sra_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x33, rs2, rs1, rd);
}

void  Loongarch64Assembler::Slli_w(XRegister rd, XRegister rj, int ui5) {
  Emit2RI8(0x10, ((0x1 << 5) | ui5), rj, rd);
}

void  Loongarch64Assembler::Slli_d(XRegister rd, XRegister rj, int ui6) {
  Emit2RI8(0x10, ((0x1 << 6) | ui6), rj, rd);
}

void  Loongarch64Assembler::Srli_w(XRegister rd, XRegister rj, int ui5) {
  Emit2RI8(0x11, ((0x1 << 5) | ui5), rj, rd);
}

void  Loongarch64Assembler::Srli_d(XRegister rd, XRegister rj, int ui6) {
  Emit2RI8(0x11, ((0x1 << 6) | ui6), rj, rd);
}

void  Loongarch64Assembler::Srai_w(XRegister rd, XRegister rj, int ui5) {
  Emit2RI8(0x12, ((0x1 << 5) | ui5), rj, rd);
}

void  Loongarch64Assembler::Srai_d(XRegister rd, XRegister rj, int ui6) {
  Emit2RI8(0x12, ((0x1 << 6) | ui6), rj, rd);
}

void  Loongarch64Assembler::Rotri_w(XRegister rd, XRegister rj, int ui5) {
  Emit2RI8(0x13, ((0x1 << 5) | ui5), rj, rd);
}

void  Loongarch64Assembler::Rotri_d(XRegister rd, XRegister rj, int ui6) {
  Emit2RI8(0x13, ((0x1 << 6) | ui6), rj, rd);
}

void Loongarch64Assembler::Bstrins_w(XRegister rd, XRegister rj, int msbw, int lsbw) {
  DCHECK(IsUint<5>(msbw)) << msbw;
  DCHECK(IsUint<5>(lsbw)) << lsbw;
  Emit2RI12_U(0x01, ((1<<11) | (msbw<<6) | (0<<5) | lsbw), rj, rd);
}

void Loongarch64Assembler::Bstrpick_w(XRegister rd, XRegister rj, int msbw, int lsbw) {
  DCHECK(IsUint<5>(msbw)) << msbw;
  DCHECK(IsUint<5>(lsbw)) << lsbw;
  Emit2RI12_U(0x01, ((1<<11) | (msbw<<6) | (1<<5) | lsbw), rj, rd);
}

void Loongarch64Assembler::Bstrins_d(XRegister rd, XRegister rj, int msbd, int lsbd) {
  DCHECK(IsUint<6>(msbd)) << msbd;
  DCHECK(IsUint<6>(lsbd)) << lsbd;
  Emit2RI12_U(0x02, ((msbd<<6) | lsbd), rj, rd);
}

void Loongarch64Assembler::Bstrpick_d(XRegister rd, XRegister rj, int msbd, int lsbd) {
  DCHECK(IsUint<6>(msbd)) << msbd;
  DCHECK(IsUint<6>(lsbd)) << lsbd;
  Emit2RI12_U(0x03, ((msbd<<6) | lsbd), rj, rd);
}

// 3R-Type
// mid-level ALU instructions : opcode from 0 0000 0000 0011 1000 
//                                        ~ 0 0000 0000 0100 0111
void Loongarch64Assembler::Mul_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x38, rs2, rs1, rd);
}

void Loongarch64Assembler::Mulh_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x39, rs2, rs1, rd);
}

void Loongarch64Assembler::Mulh_wu(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x3a, rs2, rs1, rd);
}

void Loongarch64Assembler::Mul_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x3b, rs2, rs1, rd);
}

void Loongarch64Assembler::Mulh_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x3c, rs2, rs1, rd);
}

void Loongarch64Assembler::Mulh_du(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x3d, rs2, rs1, rd);
}

void Loongarch64Assembler::Mulw_d_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x3e, rs2, rs1, rd);
}

void Loongarch64Assembler::Mulw_d_wu(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x3f, rs2, rs1, rd);
}

void Loongarch64Assembler::Div_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x40, rs2, rs1, rd);
}

void Loongarch64Assembler::Mod_w(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x41, rs2, rs1, rd);
}

void Loongarch64Assembler::Div_wu(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x42, rs2, rs1, rd);
}

void Loongarch64Assembler::Mod_wu(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x43, rs2, rs1, rd);
}

void Loongarch64Assembler::Div_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x44, rs2, rs1, rd);
}

void Loongarch64Assembler::Mod_d(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x45, rs2, rs1, rd);
}

void Loongarch64Assembler::Div_du(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x46, rs2, rs1, rd);
}

void Loongarch64Assembler::Mod_du(XRegister rd, XRegister rs1, XRegister rs2) {
  Emit3R(0x47, rs2, rs1, rd);
}

void Loongarch64Assembler::Load_B(XRegister rd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  Ld_B(rd, rs1, offset);
}

void Loongarch64Assembler::Load_H(XRegister rd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  Ld_H(rd, rs1, offset);
}

void Loongarch64Assembler::Load_W(XRegister rd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  Ld_W(rd, rs1, offset);
}

void Loongarch64Assembler::Load_D(XRegister rd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  Ld_D(rd, rs1, offset);
}

void Loongarch64Assembler::Load_BU(XRegister rd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  Ld_BU(rd, rs1, offset);
}

void Loongarch64Assembler::Load_HU(XRegister rd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  Ld_HU(rd, rs1, offset);
}

void Loongarch64Assembler::Load_WU(XRegister rd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  Ld_WU(rd, rs1, offset);
}

void Loongarch64Assembler::Store_B(XRegister rs2, XRegister rs1, int32_t offset) {
  CHECK_NE(rs2, TMP);
  AdjustBaseAndOffset(rs1, offset);
  St_B(rs2, rs1, offset);
}

void Loongarch64Assembler::Store_H(XRegister rs2, XRegister rs1, int32_t offset) {
  CHECK_NE(rs2, TMP);
  AdjustBaseAndOffset(rs1, offset);
  St_H(rs2, rs1, offset);
}

void Loongarch64Assembler::Store_W(XRegister rs2, XRegister rs1, int32_t offset) {
  CHECK_NE(rs2, TMP);
  AdjustBaseAndOffset(rs1, offset);
  St_W(rs2, rs1, offset);
}

void Loongarch64Assembler::Store_D(XRegister rs2, XRegister rs1, int32_t offset) {
  CHECK_NE(rs2, TMP);
  AdjustBaseAndOffset(rs1, offset);
  St_D(rs2, rs1, offset);
}

void Loongarch64Assembler::FLoad_S(FRegister fd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  FLd_s(fd, rs1, offset);
}

void Loongarch64Assembler::FLoad_D(FRegister fd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  FLd_d(fd, rs1, offset);
}

void Loongarch64Assembler::FStore_S(FRegister fd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  FSt_s(fd, rs1, offset);
}

void Loongarch64Assembler::FStore_D(FRegister fd, XRegister rs1, int32_t offset) {
  AdjustBaseAndOffset(rs1, offset);
  FSt_d(fd, rs1, offset);
}
/////////////////////////////// LOONGARCH64 PC_relative Instructions ///////////////////////////////
void Loongarch64Assembler::Lu12i_W(XRegister rd, uint32_t imm20) {
  EmitPC_rel(0xa, imm20, rd);
}

void Loongarch64Assembler::Lu32i_D(XRegister rd, uint32_t imm20) {
  EmitPC_rel(0xb, imm20, rd);
}

void Loongarch64Assembler::Pcaddi(XRegister rd, uint32_t imm20) {
  EmitPC_rel(0xc, imm20, rd);
}

void Loongarch64Assembler::Pcalau12i(XRegister rd, uint32_t imm20) {
  EmitPC_rel(0xd, imm20, rd);
}

void Loongarch64Assembler::Pcaddu12i(XRegister rd, uint32_t imm20) {
  EmitPC_rel(0xe, imm20, rd);
}

void Loongarch64Assembler::Pcaddu18i(XRegister rd, uint32_t imm20) {
  EmitPC_rel(0xf, imm20, rd);
}

// 3R-Type
// Atomic memory access instructions : opcode from 0 0111 0000 1011 0000
//                                               ~ 0 0111 0000 1011 1111
void Loongarch64Assembler::Amcas_b(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b0, rk, rj, rd);
}
void Loongarch64Assembler::Amcas_h(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b1, rk, rj, rd);
}
void Loongarch64Assembler::Amcas_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b2, rk, rj, rd);
}
void Loongarch64Assembler::Amcas_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b3, rk, rj, rd);
}
void Loongarch64Assembler::Amcas_db_b(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b4, rk, rj, rd);
}
void Loongarch64Assembler::Amcas_db_h(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b5, rk, rj, rd);
}
void Loongarch64Assembler::Amcas_db_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b6, rk, rj, rd);
}
void Loongarch64Assembler::Amcas_db_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b7, rk, rj, rd);
}
void Loongarch64Assembler::Amswap_b(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b8, rk, rj, rd);
}
void Loongarch64Assembler::Amswap_h(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70b9, rk, rj, rd);
}
void Loongarch64Assembler::Amadd_b(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70ba, rk, rj, rd);
}
void Loongarch64Assembler::Amadd_h(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70bb, rk, rj, rd);
}
void Loongarch64Assembler::Amswap_db_b(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70bc, rk, rj, rd);
}
void Loongarch64Assembler::Amswap_db_h(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70bd, rk, rj, rd);
}
void Loongarch64Assembler::Amadd_db_b(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70be, rk, rj, rd);
}
void Loongarch64Assembler::Amadd_db_h(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70bf, rk, rj, rd);
}

// 3R-Type
// Atomic memory access instructions : opcode from 0 0111 0000 1100 0000
//                                               ~ 0 0111 0000 1100 1111
void Loongarch64Assembler::Amswap_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c0, rk, rj, rd);
}
void Loongarch64Assembler::Amswap_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c1, rk, rj, rd);
}
void Loongarch64Assembler::Amadd_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c2, rk, rj, rd);
}
void Loongarch64Assembler::Amadd_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c3, rk, rj, rd);
}
void Loongarch64Assembler::Amand_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c4, rk, rj, rd);
}
void Loongarch64Assembler::Amand_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c5, rk, rj, rd);
}
void Loongarch64Assembler::Amor_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c6, rk, rj, rd);
}
void Loongarch64Assembler::Amor_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c7, rk, rj, rd);
}
void Loongarch64Assembler::Amxor_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c8, rk, rj, rd);
}
void Loongarch64Assembler::Amxor_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70c9, rk, rj, rd);
}
void Loongarch64Assembler::Ammax_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70ca, rk, rj, rd);
}
void Loongarch64Assembler::Ammax_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70cb, rk, rj, rd);
}
void Loongarch64Assembler::Ammin_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70cc, rk, rj, rd);
}
void Loongarch64Assembler::Ammin_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70cd, rk, rj, rd);
}
void Loongarch64Assembler::Ammax_wu(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70ce, rk, rj, rd);
}
void Loongarch64Assembler::Ammax_du(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70cf, rk, rj, rd);
}

// 3R-Type
// Atomic memory access instructions : opcode from 0 0111 0000 1101 0000
//                                               ~ 0 0111 0000 1101 1111
void Loongarch64Assembler::Ammin_wu(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d0, rk, rj, rd);
}
void Loongarch64Assembler::Ammin_du(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d1, rk, rj, rd);
}
void Loongarch64Assembler::Amswap_db_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d2, rk, rj, rd);
}
void Loongarch64Assembler::Amswap_db_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d3, rk, rj, rd);
}
void Loongarch64Assembler::Amadd_db_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d4, rk, rj, rd);
}
void Loongarch64Assembler::Amadd_db_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d5, rk, rj, rd);
}
void Loongarch64Assembler::Amand_db_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d6, rk, rj, rd);
}
void Loongarch64Assembler::Amand_db_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d7, rk, rj, rd);
}
void Loongarch64Assembler::Amor_db_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d8, rk, rj, rd);
}
void Loongarch64Assembler::Amor_db_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70d9, rk, rj, rd);
}
void Loongarch64Assembler::Amxor_db_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70da, rk, rj, rd);
}
void Loongarch64Assembler::Amxor_db_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70db, rk, rj, rd);
}
void Loongarch64Assembler::Ammax_db_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70dc, rk, rj, rd);
}
void Loongarch64Assembler::Ammax_db_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70dd, rk, rj, rd);
}
void Loongarch64Assembler::Ammin_db_w(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70de, rk, rj, rd);
}
void Loongarch64Assembler::Ammin_db_d(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70df, rk, rj, rd);
}

// 3R-Type
// Atomic memory access instructions : opcode from 0 0111 0000 1110 0000
//                                               ~ 0 0111 0000 1110 0011
void Loongarch64Assembler::Ammax_db_wu(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70e0, rk, rj, rd);
}
void Loongarch64Assembler::Ammax_db_du(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70e1, rk, rj, rd);
}
void Loongarch64Assembler::Ammin_db_wu(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70e2, rk, rj, rd);
}
void Loongarch64Assembler::Ammin_db_du(XRegister rd, XRegister rk, XRegister rj) {
  Emit3R(0x70e3, rk, rj, rd);
}

// 3R-Type
// Float alu instructions : opcode from 0 0000 0010 0000 0001
//                                    ~ not discontinuous
//                                    ~ 0 0000 0010 0010 0110
void Loongarch64Assembler::FAdd_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x201, fk, fj, fd);
}
void Loongarch64Assembler::FAdd_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x202, fk, fj, fd);
}
void Loongarch64Assembler::FSub_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x205, fk, fj, fd);
}
void Loongarch64Assembler::FSub_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x206, fk, fj, fd);
}
void Loongarch64Assembler::FMul_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x209, fk, fj, fd);
}
void Loongarch64Assembler::FMul_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x20a, fk, fj, fd);
}
void Loongarch64Assembler::FDiv_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x20d, fk, fj, fd);
}
void Loongarch64Assembler::FDiv_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x20e, fk, fj, fd);
}
void Loongarch64Assembler::FMax_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x211, fk, fj, fd);
}
void Loongarch64Assembler::FMax_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x212, fk, fj, fd);
}
void Loongarch64Assembler::FMin_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x215, fk, fj, fd);
}
void Loongarch64Assembler::FMin_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x216, fk, fj, fd);
}
void Loongarch64Assembler::FMaxa_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x219, fk, fj, fd);
}
void Loongarch64Assembler::FMaxa_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x21a, fk, fj, fd);
}
void Loongarch64Assembler::FMina_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x21d, fk, fj, fd);
}
void Loongarch64Assembler::FMina_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x21e, fk, fj, fd);
}
void Loongarch64Assembler::FScaleb_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x221, fk, fj, fd);
}
void Loongarch64Assembler::FScaleb_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x222, fk, fj, fd);
}
void Loongarch64Assembler::FCopysign_s(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x225, fk, fj, fd);
}
void Loongarch64Assembler::FCopysign_d(FRegister fd, FRegister fj, FRegister fk) {
  Emit3R(0x226, fk, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0101 0000 0001
//                                    ~ not discontinuous
//                                    ~ 00 0000 0100 0101 0000 1110
void Loongarch64Assembler::FAbs_s(FRegister fd, FRegister fj) {
  Emit2R(0x4501, fj, fd);
}
void Loongarch64Assembler::FAbs_d(FRegister fd, FRegister fj) {
  Emit2R(0x4502, fj, fd);
}
void Loongarch64Assembler::FNeg_s(FRegister fd, FRegister fj) {
  Emit2R(0x4505, fj, fd);
}
void Loongarch64Assembler::FNeg_d(FRegister fd, FRegister fj) {
  Emit2R(0x4506, fj, fd);
}
void Loongarch64Assembler::FLogb_s(FRegister fd, FRegister fj) {
  Emit2R(0x4509, fj, fd);
}
void Loongarch64Assembler::FLogb_d(FRegister fd, FRegister fj) {
  Emit2R(0x450a, fj, fd);
}
void Loongarch64Assembler::FClass_s(FRegister fd, FRegister fj) {
  Emit2R(0x450d, fj, fd);
}
void Loongarch64Assembler::FClass_d(FRegister fd, FRegister fj) {
  Emit2R(0x450e, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0101 0001 0001
//                                    ~ not discontinuous
//                                    ~ 00 0000 0100 0101 0001 1110
void Loongarch64Assembler::FSqrt_s(FRegister fd, FRegister fj) {
  Emit2R(0x4511, fj, fd);
}
void Loongarch64Assembler::FSqrt_d(FRegister fd, FRegister fj) {
  Emit2R(0x4512, fj, fd);
}
void Loongarch64Assembler::FRecip_s(FRegister fd, FRegister fj) {
  Emit2R(0x4515, fj, fd);
}
void Loongarch64Assembler::FRecip_d(FRegister fd, FRegister fj) {
  Emit2R(0x4516, fj, fd);
}
void Loongarch64Assembler::FRsqrt_s(FRegister fd, FRegister fj) {
  Emit2R(0x459, fj, fd);
}
void Loongarch64Assembler::FRsqrt_d(FRegister fd, FRegister fj) {
  Emit2R(0x451a, fj, fd);
}
void Loongarch64Assembler::FRecipe_s(FRegister fd, FRegister fj) {
  Emit2R(0x451d, fj, fd);
}
void Loongarch64Assembler::FRecipe_d(FRegister fd, FRegister fj) {
  Emit2R(0x451e, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0101 0010 0001
//                                    ~ not discontinuous
//                                    ~ 00 0000 0100 0101 0010 1110
void Loongarch64Assembler::FRsqrte_s(FRegister fd, FRegister fj) {
  Emit2R(0x4521, fj, fd);
}
void Loongarch64Assembler::FRsqrte_d(FRegister fd, FRegister fj) {
  Emit2R(0x4522, fj, fd);
}
void Loongarch64Assembler::FMov_s(FRegister fd, FRegister fj) {
  Emit2R(0x4525, fj, fd);
}
void Loongarch64Assembler::FMov_d(FRegister fd, FRegister fj) {
  Emit2R(0x4526, fj, fd);
}
void Loongarch64Assembler::Movgr2fr_w(FRegister fd, XRegister rj) {
  Emit2R(0x4529, rj, fd);
}
void Loongarch64Assembler::Movgr2fr_d(FRegister fd, XRegister rj) {
  Emit2R(0x452a, rj, fd);
}
void Loongarch64Assembler::Movgr2frh_w(FRegister fd, XRegister rj) {
  Emit2R(0x452b, rj, fd);
}
void Loongarch64Assembler::Movfr2gr_s(XRegister rd, FRegister fj) {
  Emit2R(0x452d, fj, rd);
}
void Loongarch64Assembler::Movfr2gr_d(XRegister rd, FRegister fj) {
  Emit2R(0x452e, fj, rd);
}
void Loongarch64Assembler::Movfrh2gr_s(XRegister rd, FRegister fj) {
  Emit2R(0x452f, fj, rd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0101 0011 0000
//                                    ~ 00 0000 0100 0101 0011 0010
void Loongarch64Assembler::Movgr2fcsr(FRegister fcsr, XRegister rj) {
  Emit2R(0x4530, rj, fcsr);
}
void Loongarch64Assembler::Movfcsr2gr(XRegister rd, FRegister fcsr) {
  Emit2R(0x4532, fcsr, rd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0101 0011 0100
//                                    ~ not discontinuous
//                                    ~ 00 0000 0100 0101 0011 0111
void Loongarch64Assembler::Movfr2cf(FCCRegister cd, FRegister fj) {
  Emit2R(0x4534, fj, cd);
}

void Loongarch64Assembler::Movcf2fr(FRegister fd, FCCRegister cj) {
  Emit2R(0x4535, cj, fd);
}

void Loongarch64Assembler::Movgr2cf(FCCRegister cd, XRegister rj) {
  Emit2R(0x4536, rj, cd);
}

void Loongarch64Assembler::Movcf2gr(XRegister rd, FCCRegister cj) {
  Emit2R(0x4537, cj, rd);
}

// Float alu instructions : opcode 00 0000 0100 0110 0100 0110
void Loongarch64Assembler::FCvt_s_d(FRegister fd, FRegister fj) {
  Emit2R(0x4646, fj, fd);
}
// Float alu instructions : opcode 00 0000 0100 0110 0100 1001
void Loongarch64Assembler::FCvt_d_s(FRegister fd, FRegister fj) {
  Emit2R(0x4649, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1000 0001
//                                    ~ 00 0000 0100 0110 1000 0010
void Loongarch64Assembler::FTintrm_w_s(FRegister fd, FRegister fj) {
  Emit2R(0x4681, fj, fd);
}
void Loongarch64Assembler::FTintrm_w_d(FRegister fd, FRegister fj) {
  Emit2R(0x4682, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1000 1001
//                                    ~ 00 0000 0100 0110 1000 1010
void Loongarch64Assembler::FTintrm_l_s(FRegister fd, FRegister fj) {
  Emit2R(0x4689, fj, fd);
}
void Loongarch64Assembler::FTintrm_l_d(FRegister fd, FRegister fj) {
  Emit2R(0x468a, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1001 0001
//                                    ~ 00 0000 0100 0110 1001 0010
void Loongarch64Assembler::FTintrp_w_s(FRegister fd, FRegister fj) {
  Emit2R(0x4691, fj, fd);
}
void Loongarch64Assembler::FTintrp_w_d(FRegister fd, FRegister fj) {
  Emit2R(0x4692, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1001 1001
//                                    ~ 00 0000 0100 0110 1001 1010
void Loongarch64Assembler::FTintrp_l_s(FRegister fd, FRegister fj) {
  Emit2R(0x4699, fj, fd);
}
void Loongarch64Assembler::FTintrp_l_d(FRegister fd, FRegister fj) {
  Emit2R(0x469a, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1010 0001
//                                    ~ 00 0000 0100 0110 1010 0010
void Loongarch64Assembler::FTintrz_w_s(FRegister fd, FRegister fj) {
  Emit2R(0x46a1, fj, fd);
}
void Loongarch64Assembler::FTintrz_w_d(FRegister fd, FRegister fj) {
  Emit2R(0x46a2, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1010 1001
//                                    ~ 00 0000 0100 0110 1010 1010
void Loongarch64Assembler::FTintrz_l_s(FRegister fd, FRegister fj) {
  Emit2R(0x46a9, fj, fd);
}
void Loongarch64Assembler::FTintrz_l_d(FRegister fd, FRegister fj) {
  Emit2R(0x46aa, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1011 0001
//                                    ~ 00 0000 0100 0110 1011 0010
void Loongarch64Assembler::FTintrne_w_s(FRegister fd, FRegister fj) {
  Emit2R(0x46b1, fj, fd);
}
void Loongarch64Assembler::FTintrne_w_d(FRegister fd, FRegister fj) {
  Emit2R(0x46b2, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1011 1001
//                                    ~ 00 0000 0100 0110 1011 1010
void Loongarch64Assembler::FTintrne_l_s(FRegister fd, FRegister fj) {
  Emit2R(0x46a9, fj, fd);
}
void Loongarch64Assembler::FTintrne_l_d(FRegister fd, FRegister fj) {
  Emit2R(0x46aa, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1100 0001
//                                    ~ 00 0000 0100 0110 1100 0010
void Loongarch64Assembler::FTint_w_s(FRegister fd, FRegister fj) {
  Emit2R(0x46c1, fj, fd);
}
void Loongarch64Assembler::FTint_w_d(FRegister fd, FRegister fj) {
  Emit2R(0x46c2, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0110 1100 1001
//                                    ~ 00 0000 0100 0110 1100 1010
void Loongarch64Assembler::FTint_l_s(FRegister fd, FRegister fj) {
  Emit2R(0x46c9, fj, fd);
}
void Loongarch64Assembler::FTint_l_d(FRegister fd, FRegister fj) {
  Emit2R(0x46ca, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0111 0100 0100
//                                    ~ 00 0000 0100 0111 0100 0110
void Loongarch64Assembler::FFint_s_w(FRegister fd, FRegister fj) {
  Emit2R(0x4744, fj, fd);
}
void Loongarch64Assembler::FFint_s_l(FRegister fd, FRegister fj) {
  Emit2R(0x4746, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0111 0100 1000
//                                    ~ 00 0000 0100 0111 0100 1010
void Loongarch64Assembler::FFint_d_w(FRegister fd, FRegister fj) {
  Emit2R(0x4748, fj, fd);
}
void Loongarch64Assembler::FFint_d_l(FRegister fd, FRegister fj) {
  Emit2R(0x474a, fj, fd);
}

// 2R-Type
// Float alu instructions : opcode from 00 0000 0100 0111 1001 0001
//                                    ~ 00 0000 0100 0111 1001 0010
void Loongarch64Assembler::FRint_s(FRegister fd, FRegister fj) {
  Emit2R(0x4791, fj, fd);
}
void Loongarch64Assembler::FRint_d(FRegister fd, FRegister fj) {
  Emit2R(0x4792, fj, fd);
}

// 2R-Type
// Bit operation instructions : opcode from 00 0000 0000 0000 0001 0110
//                                        ~ 00 0000 0000 0000 0001 0111
void Loongarch64Assembler::Ext_w_h(XRegister rd, XRegister rj) {
  Emit2R(0x16, rj, rd);
}
void Loongarch64Assembler::Ext_w_b(XRegister rd, XRegister rj) {
  Emit2R(0x17, rj, rd);
}

/////////////////////////////// LOONGARCH64 "4R-Type" Instructions ///////////////////////////////

/////////////////////////////// LOONGARCH64 "2RI8-Type" Instructions ///////////////////////////////





/////////////////////////////// LOONGARCH64 "2RI14-Type" Instructions ///////////////////////////////

// 2RI14-Type
// LL/SC and LD/STPTR instructions : opcode from 0010 0000
//                                             ~ 0010 0111
void Loongarch64Assembler::LL_w(XRegister rd, XRegister rj, int32_t si14) {
  Emit2RI14(0x20, si14, rj, rd);
}
void Loongarch64Assembler::SC_w(XRegister rd, XRegister rj, int32_t si14) {
  Emit2RI14(0x21, si14, rj, rd);
}
void Loongarch64Assembler::LL_d(XRegister rd, XRegister rj, int32_t si14) {
  Emit2RI14(0x22, si14, rj, rd);
}
void Loongarch64Assembler::SC_d(XRegister rd, XRegister rj, int32_t si14) {
  Emit2RI14(0x23, si14, rj, rd);
}
void Loongarch64Assembler::Ldptr_w(XRegister rd, XRegister rj, int32_t si14) {
  Emit2RI14(0x24, si14, rj, rd);
}
void Loongarch64Assembler::Stptr_w(XRegister rd, XRegister rj, int32_t si14) {
  Emit2RI14(0x25, si14, rj, rd);
}
void Loongarch64Assembler::Ldptr_d(XRegister rd, XRegister rj, int32_t si14) {
  Emit2RI14(0x26, si14, rj, rd);
}
void Loongarch64Assembler::Stptr_d(XRegister rd, XRegister rj, int32_t si14) {
  Emit2RI14(0x27, si14, rj, rd);
}

/////////////////////////////// LOONGARCH64 "2RI12-Type" Instructions ///////////////////////////////

// 2RI12-Type Integer
// Load signed instructions : opcode from 00 1010 0000 
//                                      ~ 00 1010 0011
void Loongarch64Assembler::Ld_B(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa0, offset, rs1, rd);
}

void Loongarch64Assembler::Ld_H(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa1, offset, rs1, rd);
}

void Loongarch64Assembler::Ld_W(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa2, offset, rs1, rd);
}

void Loongarch64Assembler::Ld_D(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa3, offset, rs1, rd);
}

// Load unsigned instructions : opcode from 00 1010 1000 
//                                        ~ 00 1010 1010
void Loongarch64Assembler::Ld_BU(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa8, offset, rs1, rd);
}

void Loongarch64Assembler::Ld_HU(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa9, offset, rs1, rd);
}

void Loongarch64Assembler::Ld_WU(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xaa, offset, rs1, rd);
}

// F L/S signed instructions : opcode from 00 1010 1100
//                                       ~ 00 1010 1111
void Loongarch64Assembler::FLd_s(FRegister fd, XRegister rs1, int32_t si12) {
  Emit2RI12(0xac, si12, rs1, fd);
}
void Loongarch64Assembler::FSt_s(FRegister fd, XRegister rs1, int32_t si12) {
  Emit2RI12(0xad, si12, rs1, fd);
}
void Loongarch64Assembler::FLd_d(FRegister fd, XRegister rs1, int32_t si12) {
  Emit2RI12(0xae, si12, rs1, fd);
}
void Loongarch64Assembler::FSt_d(FRegister fd, XRegister rs1, int32_t si12) {
  Emit2RI12(0xaf, si12, rs1, fd);
}

// Store instructions : opcode from 00 1010 0100 
//                                ~ 00 1010 0111
void Loongarch64Assembler::St_B(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa4, offset, rs1, rd);
}

void Loongarch64Assembler::St_H(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa5, offset, rs1, rd);
}

void Loongarch64Assembler::St_W(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa6, offset, rs1, rd);
}

void Loongarch64Assembler::St_D(XRegister rd, XRegister rs1, int32_t offset) {
  Emit2RI12(0xa7, offset, rs1, rd);
}

// IMM ALU instructions : opcode from 00 0000 1000 
//                                  ~ 00 0000 1111
void Loongarch64Assembler::Slti(XRegister rd, XRegister rs1, int32_t imm12) {
  Emit2RI12(0x08, imm12, rs1, rd);
}

void Loongarch64Assembler::Sltui(XRegister rd, XRegister rs1, int32_t imm12) {
  Emit2RI12(0x09, imm12, rs1, rd);
}

void Loongarch64Assembler::Addi_W(XRegister rd, XRegister rs1, int32_t imm12) {
  Emit2RI12(0x0a, imm12, rs1, rd);
}

void Loongarch64Assembler::Addi_D(XRegister rd, XRegister rs1, int32_t imm12) {
  Emit2RI12(0x0b, imm12, rs1, rd);
}
void Loongarch64Assembler::Lu52i_D(XRegister rd, XRegister rs1, int32_t imm12) {
  Emit2RI12(0x0c, imm12, rs1, rd);
}

void Loongarch64Assembler::Andi(XRegister rd, XRegister rs1, uint32_t imm12) {
  Emit2RI12_U(0x0d, imm12, rs1, rd);
}

void Loongarch64Assembler::Ori(XRegister rd, XRegister rs1, uint32_t imm12) {
  Emit2RI12_U(0x0e, imm12, rs1, rd);
}

void Loongarch64Assembler::Xori(XRegister rd, XRegister rs1, uint32_t imm12) {
  Emit2RI12_U(0x0f, imm12, rs1, rd);
}

void Loongarch64Assembler::brk(uint32_t imm15) {
  EmitI15(0x54, imm15);
}

void Loongarch64Assembler::Fcmp_caf_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_caf, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_caf_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_caf, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cun_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_cun, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cun_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_cun, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_ceq_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_ceq, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_ceq_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_ceq, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cueq_s (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_cueq, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cueq_d (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_cueq, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_clt_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_clt, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_clt_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_clt, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cult_s (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_cult, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cult_d (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_cult, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cle_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_cle, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cle_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_cle, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cule_s (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_cule, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cule_d (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_cule, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cne_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_cne, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cne_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_cne, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cor_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_cor, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cor_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_cor, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cune_s (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_cune, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_cune_d (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_cune, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_saf_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_saf, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_saf_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_saf, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sun_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_sun, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sun_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_sun, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_seq_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_seq, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_seq_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_seq, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sueq_s (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_sueq, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sueq_d (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_sueq, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_slt_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_slt, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_slt_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_slt, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sult_s (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_sult, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sult_d (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_sult, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sle_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_sle, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sle_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_sle, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sule_s (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_sule, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sule_d (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_sule, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sne_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_sne, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sne_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_sne, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sor_s  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_sor, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sor_d  (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_sor, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sune_s (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc1, fcmp_sule, fk, fj, cd);
}

void Loongarch64Assembler::Fcmp_sune_d (FCCRegister cd, FRegister fj, FRegister fk) {
  Emit4R(0xc2, fcmp_sule, fk, fj, cd);
}


void Loongarch64Assembler::Fsel (FRegister fd, FRegister fj, FRegister fk, FCCRegister ca) {
  Emit4R(0xd0, ca, fk, fj, fd);
}

/////////////////////////////// LOONGARCH64 pseudo Instructions ///////////////////////////////
void Loongarch64Assembler::Move(XRegister rd, XRegister rj) { Or(rd, rj, Zero); }
void Loongarch64Assembler::Jr(XRegister rd) { Jirl(Zero, rd, 0); };
void Loongarch64Assembler::Ret() { Jirl(Zero, RA, 0); };
void Loongarch64Assembler::Nop() { Andi(Zero, Zero, 0); }
void Loongarch64Assembler::Li(XRegister rd, int64_t imm) {
  LoadImmediate(rd, imm);
}

void Loongarch64Assembler::Max(XRegister rd, XRegister rs) {
  Slt(AT, rd, rs);
  Masknez(rd, rd, AT);
  Maskeqz(AT, rs, AT);
  Or(rd, rd, AT);
}

void Loongarch64Assembler::Min(XRegister rd, XRegister rs) {
  Slt(AT, rs, rd);
  Masknez(rd, rd, AT);
  Maskeqz(AT, rs, AT);
  Or(rd, rd, AT);
}

void Loongarch64Assembler::Max(XRegister rd, XRegister rs1, XRegister rs2) {
  // do not rewrite input(rs1 and rs2)
  CHECK_NE(rd, rs1);
  CHECK_NE(rd, rs2);

  Slt(AT, rs1, rs2);
  Masknez(rd, rs1, AT);
  Maskeqz(AT, rs2, AT);
  Or(rd, rd, AT);
}

void Loongarch64Assembler::Min(XRegister rd, XRegister rs1, XRegister rs2) {
  // do not rewrite input(rs1 and rs2)
  CHECK_NE(rd, rs1);
  CHECK_NE(rd, rs2);

  Slt(AT, rs1, rs2);
  Masknez(rd, rs2, AT);
  Maskeqz(AT, rs1, AT);
  Or(rd, rd, AT);
}

void Loongarch64Assembler::FMax(FRegister fd, FRegister fs1, FRegister fs2, bool is_double) {
  CHECK_NE(fd, fs1);
  CHECK_NE(fd, fs2);

/*
 * Java: maxNum(-0.0, 0.0) not Equal but 0.0
 * IEEE754-2008: maxNum(-0.0, 0.0) Equal
*/
  if (is_double) {
    FMax_d(fd, fs1, fs2);
    Fcmp_cun_d(FCC0, fs1, fs1);
    Fsel(fd, fd, fs1, FCC0);
    Fcmp_cun_d(FCC0, fs2, fs2);
    Fsel(fd, fd, fs2, FCC0);
  } else {
    FMax_s(fd, fs1, fs2);
    Fcmp_cun_s(FCC0, fs1, fs1);
    Fsel(fd, fd, fs1, FCC0);
    Fcmp_cun_s(FCC0, fs2, fs2);
    Fsel(fd, fd, fs2, FCC0);
  }
}

void Loongarch64Assembler::FMin(FRegister fd, FRegister fs1, FRegister fs2, bool is_double) {
  CHECK_NE(fd, fs1);
  CHECK_NE(fd, fs2);

  if (is_double) {
    FMin_d(fd, fs1, fs2);
    Fcmp_cun_d(FCC0, fs1, fs1);
    Fsel(fd, fd, fs1, FCC0);
    Fcmp_cun_d(FCC0, fs2, fs2);
    Fsel(fd, fd, fs2, FCC0);
  } else {
    FMin_s(fd, fs1, fs2);
    Fcmp_cun_s(FCC0, fs1, fs1);
    Fsel(fd, fd, fs1, FCC0);
    Fcmp_cun_s(FCC0, fs2, fs2);
    Fsel(fd, fd, fs2, FCC0);
  }
}
/////////////////////////////// LOONGARCH64 barrier Instructions ///////////////////////////////
void Loongarch64Assembler::Dbar(uint32_t imm15) {
  EmitI15(0x70e4, imm15);
}

/////////////////////////////// LOONGARCH64 atom Instructions ///////////////////////////////


const Loongarch64Assembler::Branch::BranchInfo Loongarch64Assembler::Branch::branch_info_[] = {
    // Short branches (can be promoted to longer).
    {4, 0, Loongarch64Assembler::Branch::kOffset18},  // kCondBranch
    {4, 0, Loongarch64Assembler::Branch::kOffset23},  // kUncondBranch
    {4, 0, Loongarch64Assembler::Branch::kOffset23},  // kCall
    // Short branches (can't be promoted to longer).
    {4, 0, Loongarch64Assembler::Branch::kOffset18},  // kBareCondBranch
    {4, 0, Loongarch64Assembler::Branch::kOffset23},  // kBareUncondBranch
    {4, 0, Loongarch64Assembler::Branch::kOffset23},  // kBareCall

    // Medium branch.
    {8, 4, Loongarch64Assembler::Branch::kOffset23},  // kCondBranch23

    // Long branches.
    {12, 4, Loongarch64Assembler::Branch::kOffset32},  // kLongCondBranch
    {8, 0, Loongarch64Assembler::Branch::kOffset32},  // kLongUncondBranch
    {8, 0, Loongarch64Assembler::Branch::kOffset32},  // kLongCall

    // label.
    {8, 0, Loongarch64Assembler::Branch::kOffset32},  // kLabel

    // literals.
    {8, 0, Loongarch64Assembler::Branch::kOffset32},  // kLiteral
    {8, 0, Loongarch64Assembler::Branch::kOffset32},  // kLiteralUnsigned
    {8, 0, Loongarch64Assembler::Branch::kOffset32},  // kLiteralLong
};

void Loongarch64Assembler::Branch::InitShortOrLong(Loongarch64Assembler::Branch::OffsetBits offset_size,
                                               Loongarch64Assembler::Branch::Type short_type,
                                               Loongarch64Assembler::Branch::Type long_type,
                                               Loongarch64Assembler::Branch::Type longest_type) {
  Loongarch64Assembler::Branch::Type type = short_type;
  if (offset_size > branch_info_[type].offset_size) {
    type = long_type;
    if (offset_size > branch_info_[type].offset_size) {
      type = longest_type;
    }
  }
  type_ = type;
}

void Loongarch64Assembler::Branch::InitializeType(Type initial_type) {
  OffsetBits offset_size_needed = GetOffsetSizeNeeded(location_, target_);

  switch (initial_type) {
    case kCondBranch:
      if (condition_ != kUncond) {
        InitShortOrLong(offset_size_needed, kCondBranch, kCondBranch23, kLongCondBranch);
        break;
      }
      FALLTHROUGH_INTENDED;
    case kUncondBranch:
      InitShortOrLong(offset_size_needed, kUncondBranch, kLongUncondBranch, kLongUncondBranch);
      break;
    case kCall:
      InitShortOrLong(offset_size_needed, kCall, kLongCall, kLongCall);
      break;
    case kBareCondBranch:
      if (condition_ != kUncond) {
        type_ = kBareCondBranch;
        CHECK_LE(offset_size_needed, GetOffsetSize());
        break;
      }
      FALLTHROUGH_INTENDED;
    case kBareUncondBranch:
      type_ = kBareUncondBranch;
      CHECK_LE(offset_size_needed, GetOffsetSize());
      break;
    case kBareCall:
      type_ = kBareCall;
      CHECK_LE(offset_size_needed, GetOffsetSize());
      break;
    case kLabel:
      type_ = initial_type;
      break;
    case kLiteral:
    case kLiteralUnsigned:
    case kLiteralLong:
      CHECK(!IsResolved());
      type_ = initial_type;
      break;
    default:
      LOG(FATAL) << "Unexpected branch type " << enum_cast<uint32_t>(initial_type);
      UNREACHABLE();
  }

  old_type_ = type_;
}

bool Loongarch64Assembler::Branch::IsNop(BranchCondition condition, XRegister lhs, XRegister rhs) {
  switch (condition) {
    case kCondNE:
    case kCondLT:
    case kCondLTU:
      return lhs == rhs;
    default:
      return false;
  }
}

bool Loongarch64Assembler::Branch::IsUncond(BranchCondition condition, XRegister lhs, XRegister rhs) {
  switch (condition) {
    case kUncond:
      return true;
    case kCondEQ:
    case kCondGE:
    case kCondGEU:
      return lhs == rhs;
    default:
      return false;
  }
}

Loongarch64Assembler::Branch::Branch(
    uint32_t location, uint32_t target, XRegister rd, bool is_bare)
    : old_location_(location),
      location_(location),
      target_(target),
      lhs_reg_(rd),
      rhs_reg_(Zero),
      freg_(kNoFRegister),
      condition_(kUncond),
      next_branch_id_(0u) {
  InitializeType(
      (rd != Zero ? (is_bare ? kBareCall : kCall) : (is_bare ? kBareUncondBranch : kUncondBranch)));
}

Loongarch64Assembler::Branch::Branch(uint32_t location,
                                 uint32_t target,
                                 Loongarch64Assembler::BranchCondition condition,
                                 XRegister lhs_reg,
                                 XRegister rhs_reg,
                                 bool is_bare,
                                 bool fcc_reg_flag)
    : old_location_(location),
      location_(location),
      target_(target),
      fcc_reg_flag_(fcc_reg_flag),
      lhs_reg_(lhs_reg),
      rhs_reg_(rhs_reg),
      condition_(condition) {
  DCHECK_NE(condition, kUncond);
  DCHECK(!IsNop(condition, lhs_reg, rhs_reg));
  DCHECK(!IsUncond(condition, lhs_reg, rhs_reg));
  InitializeType(is_bare ? kBareCondBranch : kCondBranch);
}

Loongarch64Assembler::Branch::Branch(uint32_t location,
                                 uint32_t target,
                                 XRegister rd,
                                 Type label_or_literal_type)
    : old_location_(location),
      location_(location),
      target_(target),
      fcc_reg_flag_(false),
      lhs_reg_(rd),
      rhs_reg_(Zero),
      condition_(kUncond) {
  CHECK_NE(rd , Zero);
  InitializeType(label_or_literal_type);
}

Loongarch64Assembler::BranchCondition Loongarch64Assembler::Branch::OppositeCondition(
    Loongarch64Assembler::BranchCondition cond) {
  switch (cond) {
    case kCondEQ:
      return kCondNE;
    case kCondNE:
      return kCondEQ;
    case kCondLT:
      return kCondGE;
    case kCondGE:
      return kCondLT;
    case kCondLTU:
      return kCondGEU;
    case kCondGEU:
      return kCondLTU;
    case kCondEQZ:
      return kCondNEZ;
    case kCondNEZ:
      return kCondEQZ;
    case kUncond:
      LOG(FATAL) << "Unexpected branch condition " << enum_cast<uint32_t>(cond);
      UNREACHABLE();
  }
}

Loongarch64Assembler::Branch::Type Loongarch64Assembler::Branch::GetType() const { return type_; }

Loongarch64Assembler::BranchCondition Loongarch64Assembler::Branch::GetCondition() const {
    return condition_;
}

XRegister Loongarch64Assembler::Branch::GetLeftRegister() const { return lhs_reg_; }

XRegister Loongarch64Assembler::Branch::GetRightRegister() const { return rhs_reg_; }

uint32_t Loongarch64Assembler::Branch::GetTarget() const { return target_; }

uint32_t Loongarch64Assembler::Branch::GetLocation() const { return location_; }

uint32_t Loongarch64Assembler::Branch::GetOldLocation() const { return old_location_; }

uint32_t Loongarch64Assembler::Branch::GetLength() const { return branch_info_[type_].length; }

uint32_t Loongarch64Assembler::Branch::GetOldLength() const { return branch_info_[old_type_].length; }

uint32_t Loongarch64Assembler::Branch::GetEndLocation() const { return GetLocation() + GetLength(); }

uint32_t Loongarch64Assembler::Branch::GetOldEndLocation() const {
  return GetOldLocation() + GetOldLength();
}

uint32_t Loongarch64Assembler::Branch::NextBranchId() const { return next_branch_id_; }

bool Loongarch64Assembler::Branch::IsBare() const {
  switch (type_) {
    case kBareUncondBranch:
    case kBareCondBranch:
    case kBareCall:
      return true;
    default:
      return false;
  }
}

bool Loongarch64Assembler::Branch::IsResolved() const { return target_ != kUnresolved; }

Loongarch64Assembler::Branch::OffsetBits Loongarch64Assembler::Branch::GetOffsetSize() const {
  return branch_info_[type_].offset_size;
}

Loongarch64Assembler::Branch::OffsetBits Loongarch64Assembler::Branch::GetOffsetSizeNeeded(
    uint32_t location, uint32_t target) {
  // For unresolved targets assume the shortest encoding
  // (later it will be made longer if needed).
  if (target == kUnresolved) {
    return kOffset18;
  }
  int64_t distance = static_cast<int64_t>(target) - location;
  if (IsInt<kOffset18>(distance)) {
    return kOffset18;
  } else if (IsInt<kOffset23>(distance)) {
    return kOffset23;
  } else {
    return kOffset32;
  }
}

void Loongarch64Assembler::Branch::Resolve(uint32_t target) { target_ = target; }

void Loongarch64Assembler::Branch::Relocate(uint32_t expand_location, uint32_t delta) {
  // All targets should be resolved before we start promoting branches.
  DCHECK(IsResolved());
  if (location_ > expand_location) {
    location_ += delta;
  }
  if (target_ > expand_location) {
    target_ += delta;
  }
}

uint32_t Loongarch64Assembler::Branch::PromoteIfNeeded() {
  // All targets should be resolved before we start promoting branches.
  DCHECK(IsResolved());
  Type old_type = type_;
  switch (type_) {
    // Short branches (can be promoted to longer).
    case kCondBranch: {
      OffsetBits needed_size = GetOffsetSizeNeeded(GetOffsetLocation(), target_);
      if (needed_size <= GetOffsetSize()) {
        return 0u;
      }
      // The offset remains the same for `kCondBranch23` for forward branches.
      DCHECK_EQ(branch_info_[kCondBranch23].length - branch_info_[kCondBranch23].pc_offset,
                branch_info_[kCondBranch].length - branch_info_[kCondBranch].pc_offset);
      if (target_ <= location_) {
        // Calculate the needed size for kCondBranch23.
        needed_size =
            GetOffsetSizeNeeded(location_ + branch_info_[kCondBranch23].pc_offset, target_);
      }
      type_ = (needed_size <= branch_info_[kCondBranch23].offset_size)
          ? kCondBranch23
          : kLongCondBranch;
      break;
    }
    case kUncondBranch:
      if (GetOffsetSizeNeeded(GetOffsetLocation(), target_) <= GetOffsetSize()) {
        return 0u;
      }
      type_ = kLongUncondBranch;
      break;
    case kCall:
      if (GetOffsetSizeNeeded(GetOffsetLocation(), target_) <= GetOffsetSize()) {
        return 0u;
      }
      type_ = kLongCall;
      break;
    // Medium branch (can be promoted to long).
    case kCondBranch23:
      if (GetOffsetSizeNeeded(GetOffsetLocation(), target_) <= GetOffsetSize()) {
        return 0u;
      }
      type_ = kLongCondBranch;
      break;
    default:
      // Other branch types cannot be promoted.
      DCHECK_LE(GetOffsetSizeNeeded(GetOffsetLocation(), target_), GetOffsetSize()) << type_;
      return 0u;
  }
  DCHECK(type_ != old_type);
  // TODO: should length of type_ must greater than old_type?
  DCHECK_GT(branch_info_[type_].length, branch_info_[old_type].length);
  return branch_info_[type_].length - branch_info_[old_type].length;
}

uint32_t Loongarch64Assembler::Branch::GetOffsetLocation() const {
  return location_ + branch_info_[type_].pc_offset;
}

int32_t Loongarch64Assembler::Branch::GetOffset() const {
  CHECK(IsResolved());
  // Calculate the byte distance between instructions and also account for
  // different PC-relative origins.
  uint32_t offset_location = GetOffsetLocation();
  int32_t offset = static_cast<int32_t>(target_ - offset_location);
  DCHECK_EQ(offset, static_cast<int64_t>(target_) - static_cast<int64_t>(offset_location));
  return offset;
}

void Loongarch64Assembler::Branch::LinkToList(uint32_t next_branch_id) {
  next_branch_id_ = next_branch_id;
}

void Loongarch64Assembler::EmitBcond(BranchCondition cond,
                                 XRegister rs,
                                 XRegister rt,
                                 int32_t offset) {
  switch (cond) {
#define DEFINE_CASE(COND, cond) \
    case kCond##COND:           \
      B##cond(rs, rt, offset);  \
      break;
    DEFINE_CASE(EQ, eq)
    DEFINE_CASE(NE, ne)
    DEFINE_CASE(LT, lt)
    DEFINE_CASE(GE, ge)
    DEFINE_CASE(LTU, ltu)
    DEFINE_CASE(GEU, geu)
#undef DEFINE_CASE
#define DEFINE_CASE_Z(COND, cond) \
    case kCond##COND:             \
      B##cond(rs, offset);        \
      break;
    DEFINE_CASE_Z(EQZ, eqz)
    DEFINE_CASE_Z(NEZ, nez)
#undef DEFINE_CASE_Z
    case kUncond:
      LOG(FATAL) << "Unexpected branch condition " << enum_cast<uint32_t>(cond);
      UNREACHABLE();
  }
}

void Loongarch64Assembler::EmitBranch(Loongarch64Assembler::Branch* branch) {
  CHECK(overwriting_);
  overwrite_location_ = branch->GetLocation();
  const int32_t offset = branch->GetOffset();
  BranchCondition condition = branch->GetCondition();
  XRegister lhs = branch->GetLeftRegister();
  XRegister rhs = branch->GetRightRegister();
  enum {BIT18, BIT12};

 // auto emit_pc_handle_and_next = [&](auto Pchandle, auto next) {
 //   CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
 //   auto [imm20, short_offset] = SplitOffset(BIT18, offset);
 //   Pchandle(imm20);
 //   next(short_offset);
 // };

  auto emit_pc_handle_and_next = [&](auto Pchandle, auto next, bool bits) {
    CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
    auto [imm20, short_offset] = SplitOffset(bits, offset);
    // TODO: support long jump
    //if(bits) imm20 = imm20 - ((overwrite_location_ & 0xff800) >> 12);
    Pchandle(imm20);
    next(short_offset);
  };

  // Conditional Branch Expansion handle
  switch (branch->GetType()) {
    // Short branches.
    case Branch::kUncondBranch:
    case Branch::kBareUncondBranch:
      CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
      B(offset);
      break;
    case Branch::kCondBranch:
    case Branch::kBareCondBranch:
      CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
      EmitBcond(condition, lhs, rhs, offset);
      break;
    case Branch::kCall:
    case Branch::kBareCall:
      CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
      DCHECK(lhs != Zero);
      Bl(offset);
      break;

    // Medium branch.
    case Branch::kCondBranch23:
      EmitBcond(Branch::OppositeCondition(condition), lhs, rhs, branch->GetLength());
      CHECK_EQ(overwrite_location_, branch->GetOffsetLocation());
      B(offset);
      break;

    // Long branches.
    case Branch::kLongCondBranch:
      EmitBcond(Branch::OppositeCondition(condition), lhs, rhs, branch->GetLength());
      FALLTHROUGH_INTENDED;
    case Branch::kLongUncondBranch:
      emit_pc_handle_and_next([&](uint32_t imm20) { Pcaddu18i(TMP, imm20); },
                              [&](int32_t short_offset) { Jirl(Zero, TMP, short_offset); },
                              BIT18);
      break;
    case Branch::kLongCall:
      DCHECK(lhs != Zero);
      emit_pc_handle_and_next([&](int32_t imm20) { Pcaddu18i(lhs, imm20); },
                              [&](int32_t short_offset) { Jirl(lhs, lhs, short_offset); },
                              BIT18);
      break;

    // label.
    case Branch::kLabel:
      emit_pc_handle_and_next([&](int32_t imm20) { Pcaddu12i(lhs, imm20); },
                              [&](int32_t short_offset) { Addi_D(lhs, lhs, short_offset); },
                              BIT12);
      break;
    // literals.
    case Branch::kLiteral:
      emit_pc_handle_and_next([&](int32_t imm20) { Pcaddu12i(lhs, imm20); },
                              [&](int32_t short_offset) { Ld_W(lhs, lhs, short_offset); },
                              BIT12);
      break;
    case Branch::kLiteralUnsigned:
      emit_pc_handle_and_next([&](int32_t imm20) { Pcaddu12i(lhs, imm20); },
                              [&](int32_t short_offset) { Ld_WU(lhs, lhs, short_offset); },
                              BIT12);
      break;
    case Branch::kLiteralLong:
      emit_pc_handle_and_next([&](int32_t imm20) { Pcaddu12i(lhs, imm20); },
                              // TODO short_offset should be pc & 0xfff
                              [&](int32_t short_offset) { Ld_D(lhs, lhs, short_offset); },
                              BIT12);
      break;
  }
  CHECK_EQ(overwrite_location_, branch->GetEndLocation());
  CHECK_LE(branch->GetLength(), static_cast<uint32_t>(Branch::kMaxBranchLength));
}

void Loongarch64Assembler::EmitBranches() {
  CHECK(!overwriting_);
  // Switch from appending instructions at the end of the buffer to overwriting
  // existing instructions (branch placeholders) in the buffer.
  overwriting_ = true;
  for (auto& branch : branches_) {
    EmitBranch(&branch);
  }
  overwriting_ = false;
}

void Loongarch64Assembler::FinalizeLabeledBranch(Loongarch64Label* label) {
  DCHECK_ALIGNED(branches_.back().GetLength(), sizeof(uint32_t));
  Branch& this_branch = branches_.back();
  uint32_t branch_length = this_branch.GetLength();
  uint32_t length = branch_length / sizeof(uint32_t);
  //uint32_t length = branches_.back().GetLength() / sizeof(uint32_t);
  if (!label->IsBound()) {
    // Branch forward (to a following label), distance is unknown.
    // The first branch forward will contain 0, serving as the terminator of
    // the list of forward-reaching branches.
    this_branch.LinkToList(label->position_);
    // Now make the label object point to this branch
    // (this forms a linked list of branches preceding this label).
    uint32_t branch_id = branches_.size() - 1;
    label->LinkTo(branch_id);
  }
  // Reserve space for the branch.
  for (; length != 0u; --length) {
    Emit(0);
  }
}

void Loongarch64Assembler::FBcond(
    Loongarch64Label* label, BranchCondition condition, FCCRegister rs) {

  uint32_t target = label->IsBound() ? GetLabelLocation(label) : Branch::kUnresolved;
  branches_.emplace_back(buffer_.Size(), target, condition, (XRegister)rs, Zero, false, true/*fcc_reg_flag_*/);
  FinalizeLabeledBranch(label);
}

void Loongarch64Assembler::Bcond(
    Loongarch64Label* label, bool is_bare, BranchCondition condition, XRegister lhs, XRegister rhs) {
  // TODO(loongarch64): Should an assembler perform these optimizations, or should we remove them?
  // If lhs = rhs, this can be a NOP.
  if (Branch::IsNop(condition, lhs, rhs)) {
    return;
  }
  if (Branch::IsUncond(condition, lhs, rhs)) {
    Buncond(label, Zero, is_bare);
    return;
  }

  uint32_t target = label->IsBound() ? GetLabelLocation(label) : Branch::kUnresolved;
  branches_.emplace_back(buffer_.Size(), target, condition, lhs, rhs, is_bare);
  FinalizeLabeledBranch(label);
}

void Loongarch64Assembler::Buncond(Loongarch64Label* label, XRegister rd, bool is_bare) {
  uint32_t target = label->IsBound() ? GetLabelLocation(label) : Branch::kUnresolved;
  branches_.emplace_back(buffer_.Size(), target, rd, is_bare);
  FinalizeLabeledBranch(label);
}

void Loongarch64Assembler::LoadLiteral(Literal* literal, XRegister rd, Branch::Type literal_type) {
  Loongarch64Label* label = literal->GetLabel();
  DCHECK(!label->IsBound());
  branches_.emplace_back(buffer_.Size(), Branch::kUnresolved, rd, literal_type);
  FinalizeLabeledBranch(label);
}

Loongarch64Assembler::Branch* Loongarch64Assembler::GetBranch(uint32_t branch_id) {
  CHECK_LT(branch_id, branches_.size());
  return &branches_[branch_id];
}

const Loongarch64Assembler::Branch* Loongarch64Assembler::GetBranch(uint32_t branch_id) const {
  CHECK_LT(branch_id, branches_.size());
  return &branches_[branch_id];
}

void Loongarch64Assembler::Bind(Loongarch64Label* label) {
  CHECK(!label->IsBound());
  uint32_t bound_pc = buffer_.Size();

  // Walk the list of branches referring to and preceding this label.
  // Store the previously unknown target addresses in them.
  while (label->IsLinked()) {
    uint32_t branch_id = label->Position();
    Branch* branch = GetBranch(branch_id);
    branch->Resolve(bound_pc);
    // On to the next branch in the list...
    label->position_ = branch->NextBranchId();
  }

  // Now make the label object contain its own location (relative to the end of the preceding
  // branch, if any; it will be used by the branches referring to and following this label).
  uint32_t prev_branch_id = Loongarch64Label::kNoPrevBranchId;
  if (!branches_.empty()) {
    prev_branch_id = branches_.size() - 1u;
    const Branch* prev_branch = GetBranch(prev_branch_id);
    bound_pc -= prev_branch->GetEndLocation();
  }
  label->prev_branch_id_ = prev_branch_id;
  label->BindTo(bound_pc);
}

void Loongarch64Assembler::LoadLabelAddress(XRegister rd, Loongarch64Label* label) {
  DCHECK_NE(rd, Zero);
  uint32_t target = label->IsBound() ? GetLabelLocation(label) : Branch::kUnresolved;
  branches_.emplace_back(buffer_.Size(), target, rd, Branch::kLabel);
  FinalizeLabeledBranch(label);
}

Literal* Loongarch64Assembler::NewLiteral(size_t size, const uint8_t* data) {
  // We don't support byte and half-word literals.
  if (size == 4u) {
    literals_.emplace_back(size, data);
    return &literals_.back();
  } else {
    DCHECK_EQ(size, 8u);
    long_literals_.emplace_back(size, data);
    return &long_literals_.back();
  }
}

JumpTable* Loongarch64Assembler::CreateJumpTable(ArenaVector<Loongarch64Label*>&& labels) {
  jump_tables_.emplace_back(std::move(labels));
  JumpTable* table = &jump_tables_.back();
  DCHECK(!table->GetLabel()->IsBound());
  return table;
}

uint32_t Loongarch64Assembler::GetLabelLocation(const Loongarch64Label* label) const {
  CHECK(label->IsBound());
  uint32_t target = label->Position();
  if (label->prev_branch_id_ != Loongarch64Label::kNoPrevBranchId) {
    // Get label location based on the branch preceding it.
    const Branch* prev_branch = GetBranch(label->prev_branch_id_);
    target += prev_branch->GetEndLocation();
  }
  return target;
}

uint32_t Loongarch64Assembler::GetAdjustedPosition(uint32_t old_position) {
  // We can reconstruct the adjustment by going through all the branches from the beginning
  // up to the `old_position`. Since we expect `GetAdjustedPosition()` to be called in a loop
  // with increasing `old_position`, we can use the data from last `GetAdjustedPosition()` to
  // continue where we left off and the whole loop should be O(m+n) where m is the number
  // of positions to adjust and n is the number of branches.
  if (old_position < last_old_position_) {
    last_position_adjustment_ = 0;
    last_old_position_ = 0;
    last_branch_id_ = 0;
  }
  while (last_branch_id_ != branches_.size()) {
    const Branch* branch = GetBranch(last_branch_id_);
    if (branch->GetLocation() >= old_position + last_position_adjustment_) {
      break;
    }
    last_position_adjustment_ += branch->GetLength() - branch->GetOldLength();
    ++last_branch_id_;
  }
  last_old_position_ = old_position;
  return old_position + last_position_adjustment_;
}

void Loongarch64Assembler::ReserveJumpTableSpace() {
  if (!jump_tables_.empty()) {
    for (JumpTable& table : jump_tables_) {
      Loongarch64Label* label = table.GetLabel();
      Bind(label);

      // Bulk ensure capacity, as this may be large.
      size_t orig_size = buffer_.Size();
      size_t required_capacity = orig_size + table.GetSize();
      if (required_capacity > buffer_.Capacity()) {
        buffer_.ExtendCapacity(required_capacity);
      }
#ifndef NDEBUG
      buffer_.has_ensured_capacity_ = true;
#endif

      // Fill the space with placeholder data as the data is not final
      // until the branches have been promoted. And we shouldn't
      // be moving uninitialized data during branch promotion.
      for (size_t cnt = table.GetData().size(), i = 0; i < cnt; ++i) {
        buffer_.Emit<uint32_t>(0x1abe1234u);
      }

#ifndef NDEBUG
      buffer_.has_ensured_capacity_ = false;
#endif
    }
  }
}

void Loongarch64Assembler::PromoteBranches() {
  // Promote short branches to long as necessary.
  bool changed;
  do {
    changed = false;
    for (auto& branch : branches_) {
      CHECK(branch.IsResolved());
      uint32_t delta = branch.PromoteIfNeeded();
      // If this branch has been promoted and needs to expand in size,
      // relocate all branches by the expansion size.
      if (delta != 0u) {
        changed = true;
        uint32_t expand_location = branch.GetLocation();
        for (auto& branch2 : branches_) {
          branch2.Relocate(expand_location, delta);
        }
      }
    }
  } while (changed);

  // Account for branch expansion by resizing the code buffer
  // and moving the code in it to its final location.
  size_t branch_count = branches_.size();
  if (branch_count > 0) {
    // Resize.
    Branch& last_branch = branches_[branch_count - 1];
    uint32_t size_delta = last_branch.GetEndLocation() - last_branch.GetOldEndLocation();
    uint32_t old_size = buffer_.Size();
    buffer_.Resize(old_size + size_delta);
    // Move the code residing between branch placeholders.
    uint32_t end = old_size;
    for (size_t i = branch_count; i > 0;) {
      Branch& branch = branches_[--i];
      uint32_t size = end - branch.GetOldEndLocation();
      buffer_.Move(branch.GetEndLocation(), branch.GetOldEndLocation(), size);
      end = branch.GetOldLocation();
    }
  }

  // Align 64-bit literals by moving them up by 4 bytes if needed.
  // This can increase the PC-relative distance but all literals are accessed with AUIPC+Load(imm12)
  // without branch promotion, so this late adjustment cannot take them out of instruction range.
  if (!long_literals_.empty()) {
    uint32_t first_literal_location = GetLabelLocation(long_literals_.front().GetLabel());
    size_t lit_size = long_literals_.size() * sizeof(uint64_t);
    size_t buf_size = buffer_.Size();
    // 64-bit literals must be at the very end of the buffer.
    CHECK_EQ(first_literal_location + lit_size, buf_size);
    if (!IsAligned<sizeof(uint64_t)>(first_literal_location)) {
      // Insert the padding.
      buffer_.Resize(buf_size + sizeof(uint32_t));
      buffer_.Move(first_literal_location + sizeof(uint32_t), first_literal_location, lit_size);
      DCHECK(!overwriting_);
      overwriting_ = true;
      overwrite_location_ = first_literal_location;
      Emit(0);  // Illegal instruction.
      overwriting_ = false;
      // Increase target addresses in literal and address loads by 4 bytes in order for correct
      // offsets from PC to be generated.
      for (auto& branch : branches_) {
        uint32_t target = branch.GetTarget();
        if (target >= first_literal_location) {
          branch.Resolve(target + sizeof(uint32_t));
        }
      }
      // If after this we ever call GetLabelLocation() to get the location of a 64-bit literal,
      // we need to adjust the location of the literal's label as well.
      for (Literal& literal : long_literals_) {
        // Bound label's position is negative, hence decrementing it instead of incrementing.
        literal.GetLabel()->position_ -= sizeof(uint32_t);
      }
    }
  }
}

void Loongarch64Assembler::PatchCFI() {
  if (cfi().NumberOfDelayedAdvancePCs() == 0u) {
    return;
  }

  using DelayedAdvancePC = DebugFrameOpCodeWriterForAssembler::DelayedAdvancePC;
  const auto data = cfi().ReleaseStreamAndPrepareForDelayedAdvancePC();
  const std::vector<uint8_t>& old_stream = data.first;
  const std::vector<DelayedAdvancePC>& advances = data.second;

  // Refill our data buffer with patched opcodes.
  static constexpr size_t kExtraSpace = 16;  // Not every PC advance can be encoded in one byte.
  cfi().ReserveCFIStream(old_stream.size() + advances.size() + kExtraSpace);
  size_t stream_pos = 0;
  for (const DelayedAdvancePC& advance : advances) {
    DCHECK_GE(advance.stream_pos, stream_pos);
    // Copy old data up to the point where advance was issued.
    cfi().AppendRawData(old_stream, stream_pos, advance.stream_pos);
    stream_pos = advance.stream_pos;
    // Insert the advance command with its final offset.
    size_t final_pc = GetAdjustedPosition(advance.pc);
    cfi().AdvancePC(final_pc);
  }
  // Copy the final segment if any.
  cfi().AppendRawData(old_stream, stream_pos, old_stream.size());
}

void Loongarch64Assembler::EmitJumpTables() {
  if (!jump_tables_.empty()) {
    CHECK(!overwriting_);
    // Switch from appending instructions at the end of the buffer to overwriting
    // existing instructions (here, jump tables) in the buffer.
    overwriting_ = true;

    for (JumpTable& table : jump_tables_) {
      Loongarch64Label* table_label = table.GetLabel();
      uint32_t start = GetLabelLocation(table_label);
      overwrite_location_ = start;

      for (Loongarch64Label* target : table.GetData()) {
        CHECK_EQ(buffer_.Load<uint32_t>(overwrite_location_), 0x1abe1234u);// what is the address mean?
        // The table will contain target addresses relative to the table start.
        uint32_t offset = GetLabelLocation(target) - start;
        Emit(offset);
      }
    }

    overwriting_ = false;
  }
}

void Loongarch64Assembler::EmitLiterals() {
  if (!literals_.empty()) {
    for (Literal& literal : literals_) {
      Loongarch64Label* label = literal.GetLabel();
      Bind(label);
      AssemblerBuffer::EnsureCapacity ensured(&buffer_);
      DCHECK_EQ(literal.GetSize(), 4u);
      for (size_t i = 0, size = literal.GetSize(); i != size; ++i) {
        buffer_.Emit<uint8_t>(literal.GetData()[i]);
      }
    }
  }
  if (!long_literals_.empty()) {
    // These need to be 8-byte-aligned but we shall add the alignment padding after the branch
    // promotion, if needed. Since all literals are accessed with PCALAU12I+Load(imm12) without branch
    // promotion, this late adjustment cannot take long literals out of instruction range.
    for (Literal& literal : long_literals_) {
      Loongarch64Label* label = literal.GetLabel();
      Bind(label);
      AssemblerBuffer::EnsureCapacity ensured(&buffer_);
      DCHECK_EQ(literal.GetSize(), 8u);
      for (size_t i = 0, size = literal.GetSize(); i != size; ++i) {
        buffer_.Emit<uint8_t>(literal.GetData()[i]);
      }
    }
  }
}

// This method is used to adjust the base register and offset pair for
// a load/store when the offset doesn't fit into 12-bit signed integer.
void Loongarch64Assembler::AdjustBaseAndOffset(XRegister& base, int32_t& offset) {
  CHECK_NE(base, AT);  // The `AT` is reserved for adjustment even if it's not needed.
  if (IsInt<12>(offset)) {
    return;
  }

  constexpr int32_t kPositiveOffsetMaxSimpleAdjustment = 0x7ff;
  constexpr int32_t kHighestOffsetForSimpleAdjustment = 2 * kPositiveOffsetMaxSimpleAdjustment;
  constexpr int32_t kPositiveOffsetSimpleAdjustmentAligned8 =
      RoundDown(kPositiveOffsetMaxSimpleAdjustment, 8);
  constexpr int32_t kPositiveOffsetSimpleAdjustmentAligned4 =
      RoundDown(kPositiveOffsetMaxSimpleAdjustment, 4);
  constexpr int32_t kNegativeOffsetSimpleAdjustment = -0x800;
  constexpr int32_t kLowestOffsetForSimpleAdjustment = 2 * kNegativeOffsetSimpleAdjustment;

  if (offset >= 0 && offset <= kHighestOffsetForSimpleAdjustment) {
    // Make the adjustment 8-byte aligned (0x7f8) except for offsets that cannot be reached
    // with this adjustment, then try 4-byte alignment, then just half of the offset.
    int32_t adjustment = IsInt<12>(offset - kPositiveOffsetSimpleAdjustmentAligned8)
        ? kPositiveOffsetSimpleAdjustmentAligned8
        : IsInt<12>(offset - kPositiveOffsetSimpleAdjustmentAligned4)
            ? kPositiveOffsetSimpleAdjustmentAligned4
            : offset / 2;
    DCHECK(IsInt<12>(adjustment));
    Addi_D(AT, base, adjustment);
    offset -= adjustment;
  } else if (offset < 0 && offset >= kLowestOffsetForSimpleAdjustment) {
    Addi_D(AT, base, kNegativeOffsetSimpleAdjustment);
    offset -= kNegativeOffsetSimpleAdjustment;
  } else if (offset >= 0x7ffff800) {
    // Support even large offsets outside the range supported by `SplitOffset()`.
    LoadConst32(AT, offset);
    Add_d(AT, AT, base);
    offset = 0;
  } else {
    auto [imm20, short_offset] = SplitOffset(1, offset);
    Lu12i_W(AT, imm20);
    Add_d(AT, AT, base);
    offset = short_offset;
  }
  base = AT;
}

void Loongarch64Assembler::LoadImmediate(XRegister rd, int64_t imm) {
  int64_t hi12 = bitfield(imm, 52, 12);
  int64_t lo52 = bitfield(imm,  0, 52);

  if ((hi12 != 0 && hi12 != 0xfff) && lo52 == 0) {
    Lu52i_D(rd, Zero, hi12);
  } else {
    int64_t hi20 = bitfield(imm, 32, 20);
    int64_t lo20 = bitfield(imm, 12, 20);
    int64_t lo12 = bitfield(imm,  0, 12);

    if (lo20 == 0) {
      Ori(rd, Zero, lo12);
    } else if (bitfield(simm12(lo12), 12, 20) == lo20) {
      Addi_W(rd, Zero, simm12(lo12));
    } else {
      Lu12i_W(rd, lo20);
      if (lo12 != 0)
        Ori(rd, rd, lo12);
    }
    if (hi20 != bitfield(simm20(lo20), 20, 20))
      Lu32i_D(rd, hi20);
    if (hi12 != bitfield(simm20(hi20), 20, 12))
      Lu52i_D(rd, rd, hi12);
  }
}

/////////////////////////////// LOONGARCH64 VARIANTS extension end ////////////

}  // namespace loongarch64
}  // namespace art
