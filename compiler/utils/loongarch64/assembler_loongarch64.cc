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

#include "base/bit_utils.h"
#include "base/casts.h"
#include "base/memory_region.h"

namespace art {
namespace loongarch64 {

static_assert(static_cast<size_t>(kLoongarch64PointerSize) == kLoongarch64DoublewordSize,
              "Unexpected Loongarch64 pointer size.");
static_assert(kLoongarch64PointerSize == PointerSize::k64, "Unexpected Loongarch64 pointer size.");

void Loongarch64Assembler::FinalizeCode() {
}

void Loongarch64Assembler::FinalizeInstructions(const MemoryRegion& region) {
  Assembler::FinalizeInstructions(region);
}

void Loongarch64Assembler::Emit(uint32_t value) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  buffer_.Emit<uint32_t>(value);
}

/////////////////////////////// LOONGARCH64 VARIANTS extension ///////////////////////////////


/////////////////////////////// LOONGARCH64 "2R-Type" Instructions ///////////////////////////////


/////////////////////////////// LOONGARCH64 "3R-Type" Instructions ///////////////////////////////

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


/////////////////////////////// LOONGARCH64 "4R-Type" Instructions ///////////////////////////////

// 4R-Type Float
// FP FM(ultiply)A instructions : opcode from 0000 1000 0001 
//                                          ~ 0000 1000 1110
// void Loongarch64Assembler::FMAdd_S(
//     FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
//   Emit4R(0x81, rs3, rs2, rs1, rd);
// }

/////////////////////////////// LOONGARCH64 "2RI8-Type" Instructions ///////////////////////////////






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
void Loongarch64Assembler::Lu52i_d(XRegister rd, XRegister rs1, int32_t imm12) {
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


// 2RI12-Type Float
//  FP load/store instructions, opcode from 00 1010 1100
//                                        ~ 00 1010 1111
// void Loongarch64Assembler::Fld_s(FRegister rd, XRegister rs1, int32_t offset) {
//   Emit2RI12(0xac, offset, rs1, rd);
// }
// 
// void Loongarch64Assembler::Fst_s(FRegister rd, XRegister rs1, int32_t offset) {
//   Emit2RI12(0xad, offset, rs1, rd);
// }
// 
// void Loongarch64Assembler::Fld_d(FRegister rd, XRegister rs1, int32_t offset) {
//   Emit2RI12(0xae, offset, rs1, rd);
// }
// 
// void Loongarch64Assembler::Fst_d(FRegister rd, XRegister rs1, int32_t offset) {
//   Emit2RI12(0xaf, offset, rs1, rd);
// }





/////////////////////////////// LOONGARCH64 "2RI14-Type" Instructions ///////////////////////////////


/////////////////////////////// LOONGARCH64 "2RI16-Type" Instructions ///////////////////////////////


/////////////////////////////// LOONGARCH64 "1RI21-Type" Instructions ///////////////////////////////

void Loongarch64Assembler::Beqz(XRegister rs, int32_t offset) {
  Emit1RI21(0x10, rs, offset);
}


/////////////////////////////// LOONGARCH64 "I26-Type" Instructions ///////////////////////////////







/////////////////////////////// LOONGARCH64 VARIANTS extension end ////////////

}  // namespace loongarch64
}  // namespace art
