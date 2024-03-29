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

#ifndef ART_COMPILER_UTILS_RISCV64_ASSEMBLER_RISCV64_H_
#define ART_COMPILER_UTILS_RISCV64_ASSEMBLER_RISCV64_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "arch/loongarch64/instruction_set_features_loongarch64.h"
#include "arch/loongarch64/registers_loongarch64.h"
#include "base/arena_containers.h"
#include "base/enums.h"
#include "base/globals.h"
#include "base/macros.h"
#include "managed_register_loongarch64.h"
#include "utils/assembler.h"
#include "utils/label.h"

namespace art {
namespace loongarch64 {

// Due to the limited accuracy of floating-point numbers
// controlled by fcsr0 9:8
enum class FPRoundingMode : uint32_t {
    kRNE = 0x0,  // Round to Nearest, ties to Even
    kRZ = 0x1,  // Round towards Zero
    kRP = 0x2,  // Round towards Positive
    kRM = 0x3,  // Round towards Negative
    kDefault = kRNE
};

static constexpr size_t kLoongarch64HalfwordSize = 2;
static constexpr size_t kLoongarch64WordSize = 4;
static constexpr size_t kLoongarch64DoublewordSize = 8;

class Loongarch64Label : public Label {
};

class Loongarch64Assembler final : public Assembler {
 public:
  explicit Loongarch64Assembler(ArenaAllocator* allocator,
                            const Loongarch64InstructionSetFeatures* instruction_set_features = nullptr)
      : Assembler(allocator) {
    UNUSED(instruction_set_features);
  }

  virtual ~Loongarch64Assembler() {
  }

  size_t CodeSize() const override { return Assembler::CodeSize(); }
  DebugFrameOpCodeWriterForAssembler& cfi() { return Assembler::cfi(); }

  // 2RI12-Type
  // Load signed instructions : opcode from 00 1010 0000 
  //                                      ~ 00 1010 0011
  void Ld_B(XRegister rd, XRegister rs1, int32_t offset);
  void Ld_H(XRegister rd, XRegister rs1, int32_t offset);
  void Ld_W(XRegister rd, XRegister rs1, int32_t offset);
  void Ld_D(XRegister rd, XRegister rs1, int32_t offset);
  // Load unsigned instructions : opcode from 00 1010 1000 
  //                                        ~ 00 1010 1010
  void Ld_BU(XRegister rd, XRegister rs1, int32_t offset);
  void Ld_HU(XRegister rd, XRegister rs1, int32_t offset);
  void Ld_WU(XRegister rd, XRegister rs1, int32_t offset);

  // 2RI12-Type
  // Store instructions : opcode from 00 1010 0100 
  //                                ~ 00 1010 0111
  void St_B(XRegister rd, XRegister rs1, int32_t offset);
  void St_H(XRegister rd, XRegister rs1, int32_t offset);
  void St_W(XRegister rd, XRegister rs1, int32_t offset);
  void St_D(XRegister rd, XRegister rs1, int32_t offset);

  // 2RI12-Type
  // IMM ALU instructions : opcode from 00 0000 1000 
  //                                  ~ 00 0000 1111
  void Slti(XRegister rd, XRegister rs1, int32_t imm12);
  void Sltui(XRegister rd, XRegister rs1, int32_t imm12);
  void Addi_W(XRegister rd, XRegister rs1, int32_t imm12);
  void Addi_D(XRegister rd, XRegister rs1, int32_t imm12);
  void Lu52i_d(XRegister rd, XRegister rs1, int32_t imm12);
  void Andi(XRegister rd, XRegister rs1, uint32_t imm12);
  void Ori(XRegister rd, XRegister rs1, uint32_t imm12);
  void Xori(XRegister rd, XRegister rs1, uint32_t imm12);

  // 3R-Type
  // low-level ALU instructions : opcode from 0 0000 0000 0010 0000 
  //                                        ~ 0 0000 0000 0011 0011
  void Add_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Add_d(XRegister rd, XRegister rs1, XRegister rs2);
  void Sub_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Sub_d(XRegister rd, XRegister rs1, XRegister rs2);
  void Slt(XRegister rd, XRegister rs1, XRegister rs2);
  void Sltu(XRegister rd, XRegister rs1, XRegister rs2);
  void Maskeqz(XRegister rd, XRegister rs1, XRegister rs2);
  void Masknez(XRegister rd, XRegister rs1, XRegister rs2);
  void Nor(XRegister rd, XRegister rs1, XRegister rs2);
  void And(XRegister rd, XRegister rs1, XRegister rs2);
  void Or(XRegister rd, XRegister rs1, XRegister rs2);
  void Xor(XRegister rd, XRegister rs1, XRegister rs2);
  void Orn(XRegister rd, XRegister rs1, XRegister rs2);
  void Andn(XRegister rd, XRegister rs1, XRegister rs2);
  void Sll_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Srl_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Sra_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Sll_d(XRegister rd, XRegister rs1, XRegister rs2);
  void Srl_d(XRegister rd, XRegister rs1, XRegister rs2);
  void Sra_d(XRegister rd, XRegister rs1, XRegister rs2);

  // Environment call and breakpoint , opcode from 0 0000 0000 0101 0100 
  //                                             ~ 0 0000 0000 0101 0110
  // void Break();
  // void Dbcl();
  // void Syscall();

  // 3R-Type
  // mid-level ALU instructions : opcode from 0 0000 0000 0011 1000 
  //                                        ~ 0 0000 0000 0100 0111
  void Mul_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulh_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulh_wu(XRegister rd, XRegister rs1, XRegister rs2);
  void Mul_d(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulh_d(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulh_du(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulw_d_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Mulw_d_wu(XRegister rd, XRegister rs1, XRegister rs2);
  void Div_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Mod_w(XRegister rd, XRegister rs1, XRegister rs2);
  void Div_wu(XRegister rd, XRegister rs1, XRegister rs2);
  void Mod_wu(XRegister rd, XRegister rs1, XRegister rs2);
  void Div_d(XRegister rd, XRegister rs1, XRegister rs2);
  void Mod_d(XRegister rd, XRegister rs1, XRegister rs2);
  void Div_du(XRegister rd, XRegister rs1, XRegister rs2);
  void Mod_du(XRegister rd, XRegister rs1, XRegister rs2);

  // transfer instruction, opcode from 01 0000
  //                                 ~ 01 1011
  void Beqz(XRegister rs, int32_t offset21);
  void Bnez(XRegister rs, int32_t offset21);
  // float branch
  // void Bceqz(XRegister rs, int32_t offset);
  // void Bcnez(XRegister rs, int32_t offset);
  void Jirl(XRegister rd, XRegister rs1, int32_t offset16);
  void B(int32_t offset26);
  void Bl(int32_t offset26);
  void Beq(XRegister rd, XRegister rs1, int32_t offset16);
  void Bne(XRegister rd, XRegister rs1, int32_t offset16);
  void Blt(XRegister rd, XRegister rs1, int32_t offset16);
  void Bge(XRegister rd, XRegister rs1, int32_t offset16);
  void Bltu(XRegister rd, XRegister rs1, int32_t offset16);
  void Bgeu(XRegister rd, XRegister rs1, int32_t offset16);

  // Branch pseudo instructions
  void Bgt(XRegister );
  // Jump pseudo instructions
  void Jr(XRegister rs);






  // 2RI12-Type
  //  FP load/store instructions, opcode from 00 1010 1100 
  //                                        ~ 00 1010 1111
  void Fld_s(FRegister rd, XRegister rs1, int32_t offset);
  void Fst_s(FRegister rd, XRegister rs1, int32_t offset);
  void Fld_d(FRegister rd, XRegister rs1, int32_t offset);
  void Fst_d(FRegister rd, XRegister rs1, int32_t offset);

  // 4R-Type
  // FP FM(ultiply)A instructions : opcode from 0000 1000 0001 
  //                                          ~ 0000 1000 1110
  void FMAdd_S(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FMAdd_D(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FMSub_S(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FMSub_D(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FNMAdd_S(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FNMAdd_D(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FNMSub_S(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);
  void FNMSub_D(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3, FPRoundingMode frm);

  // FP FMA instruction helpers passing the default rounding mode.
  void FMAdd_S(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FMAdd_S(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FMAdd_D(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FMAdd_D(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FMSub_S(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FMSub_S(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FMSub_D(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FMSub_D(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FNMAdd_S(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FNMAdd_S(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FNMAdd_D(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
     FNMAdd_D(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FNMSub_S(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
     FNMSub_S(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }
  void FNMSub_D(FRegister rd, FRegister rs1, FRegister rs2, FRegister rs3) {
    FNMSub_D(rd, rs1, rs2, rs3, FPRoundingMode::kDefault);
  }

  // FCMP instructions
  // FCMP_cond_S
  // FCMP_cond_D

  // 3R-Type
  // Simple FP instructions : opcode from 0 0000 0010 0000 0001 
  //                                    ~ 0 0000 0010 0001 1110
  // void FAdd_S(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  // void FAdd_D(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  // void FSub_S(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  // void FSub_D(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  // void FMul_S(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  // void FMul_D(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  // void FDiv_S(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  // void FDiv_D(FRegister rd, FRegister rs1, FRegister rs2, FPRoundingMode frm);
  // void FMax_S(FRegister rd, FRegister rs1, FRegister rs2);
  // void FMax_D(FRegister rd, FRegister rs1, FRegister rs2);
  // void FMin_S(FRegister rd, FRegister rs1, FRegister rs2);
  // void FMin_D(FRegister rd, FRegister rs1, FRegister rs2);
  // void FMaxA_S(FRegister rd, FRegister rs1, FRegister rs2);
  // void FMaxA_D(FRegister rd, FRegister rs1, FRegister rs2);
  // void FMinA_S(FRegister rd, FRegister rs1, FRegister rs2);
  // void FMinA_D(FRegister rd, FRegister rs1, FRegister rs2);

  // // Simple FP instruction helpers passing the default rounding mode.
  // void FAdd_S(FRegister rd, FRegister rs1, FRegister rs2) {
  //   FAdd_S(rd, rs1, rs2, FPRoundingMode::kDefault);
  // }
  // void FAdd_D(FRegister rd, FRegister rs1, FRegister rs2) {
  //   FAdd_D(rd, rs1, rs2, FPRoundingMode::kDefault);
  // }
  // void FSub_S(FRegister rd, FRegister rs1, FRegister rs2) {
  //   FSub_S(rd, rs1, rs2, FPRoundingMode::kDefault);
  // }
  // void FSub_D(FRegister rd, FRegister rs1, FRegister rs2) {
  //   FSub_D(rd, rs1, rs2, FPRoundingMode::kDefault);
  // }
  // void FMul_S(FRegister rd, FRegister rs1, FRegister rs2) {
  //   FMul_S(rd, rs1, rs2, FPRoundingMode::kDefault);
  // }
  // void FMul_D(FRegister rd, FRegister rs1, FRegister rs2) {
  //   FMul_D(rd, rs1, rs2, FPRoundingMode::kDefault);
  // }
  // void FDiv_S(FRegister rd, FRegister rs1, FRegister rs2) {
  //   FDiv_S(rd, rs1, rs2, FPRoundingMode::kDefault);
  // }
  // void FDiv_D(FRegister rd, FRegister rs1, FRegister rs2) {
  //   FDiv_D(rd, rs1, rs2, FPRoundingMode::kDefault);
  // }

  // // FP conversion instructions, opcode from 00 0000 0100 0110 0100 0110
  // //                                       ~ 00 0000 0100 0111 1001 0010
  // void FCVT_S_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FCVT_D_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRM_W_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRM_W_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRM_L_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRM_L_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRP_W_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRP_W_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRZ_L_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRZ_L_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRNE_W_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRNE_W_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRNE_L_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINTRNE_L_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINT_W_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINT_W_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINT_L_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FTINT_L_D(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FFINT_S_W(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FFINT_S_L(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FFINT_D_W(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FFINT_D_L(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FRINT_S(FRegister rd, FRegister rs1, FPRoundingMode frm);
  // void FRINT_D(FRegister rd, FRegister rs1, FPRoundingMode frm);

  // // FP conversion instruction helpers passing the default rounding mode.
  // void FCVT_S_D(FRegister rd, FRegister rs1) {  FCVT_S_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FCVT_D_S(FRegister rd, FRegister rs1) { FCVT_D_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRM_W_S(FRegister rd, FRegister rs1) { FTINTRM_W_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRM_W_D(FRegister rd, FRegister rs1) { FTINTRM_W_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRM_L_S(FRegister rd, FRegister rs1) { FTINTRM_L_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRM_L_D(FRegister rd, FRegister rs1) { FTINTRM_L_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRP_W_S(FRegister rd, FRegister rs1) { FTINTRP_W_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRP_W_D(FRegister rd, FRegister rs1) { FTINTRP_W_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRZ_L_S(FRegister rd, FRegister rs1) { FTINTRZ_L_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRZ_L_D(FRegister rd, FRegister rs1) { FTINTRZ_L_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRNE_W_S(FRegister rd, FRegister rs1) { FTINTRNE_W_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRNE_W_D(FRegister rd, FRegister rs1) { FTINTRNE_W_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRNE_L_S(FRegister rd, FRegister rs1) { FTINTRNE_L_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINTRNE_L_D(FRegister rd, FRegister rs1) { FTINTRNE_L_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINT_W_S(FRegister rd, FRegister rs1) { FTINT_W_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINT_W_D(FRegister rd, FRegister rs1) { FTINT_W_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINT_L_S(FRegister rd, FRegister rs1) { FTINT_L_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FTINT_L_D(FRegister rd, FRegister rs1) { FTINT_L_D(rd, rs1, FPRoundingMode::kDefault); }
  // void FFINT_S_W(FRegister rd, FRegister rs1) { FFINT_S_W(rd, rs1, FPRoundingMode::kDefault); }
  // void FFINT_S_L(FRegister rd, FRegister rs1) { FFINT_S_L(rd, rs1, FPRoundingMode::kDefault); }
  // void FFINT_D_W(FRegister rd, FRegister rs1) { FFINT_D_W(rd, rs1, FPRoundingMode::kDefault); }
  // void FFINT_D_L(FRegister rd, FRegister rs1) { FFINT_D_L(rd, rs1, FPRoundingMode::kDefault); }
  // void FRINT_S(FRegister rd, FRegister rs1) {  FRINT_S(rd, rs1, FPRoundingMode::kDefault); }
  // void FRINT_D(FRegister rd, FRegister rs1) {  FRINT_D(rd, rs1, FPRoundingMode::kDefault); }

  // // FR register-internal move instructions, opcode from 00 0000 0100 0101 0010 0101
  // //                                                   ~ 00 0000 0100 0101 0010 0110
  // void FMOV_S(FRegister rd, FRegister rs1);
  // void FMOV_D(FRegister rd, FRegister rs1);

  // // FR-GR register-between move instructions, opcode from 00 0000 0100 0101 0010 1001
  // //                                                     ~ 00 0000 0100 0101 0010 1111
  // void MOVGR2FR_W(FRegister rd, XRegister rs1);
  // void MOVGR2FR_D(FRegister rd, XRegister rs1);
  // void MOVGR2FRH_W(FRegister rd, XRegister rs1);
  // void MOVFR2GR_S(XRegister rd, FRegister rs1);
  // void MOVFR2GR_D(XRegister rd, FRegister rs1);
  // void MOVFRH2GR_S(XRegister rd, FRegister rs1);

  ////////////////////////////// LOONGARCH64 MACRO Instructions START ///////////////////////////////
  // These pseudo instructions are from "la-asm-manual".
  // :TODO

  void Bind(Label* label ATTRIBUTE_UNUSED) override {
    UNIMPLEMENTED(FATAL) << "TODO: Support branches.";
  }
  void Jump(Label* label ATTRIBUTE_UNUSED) override {
    UNIMPLEMENTED(FATAL) << "Do not use Jump for LOONGARCH64";
  }

 public:
  // Emit data (e.g. encoded instruction or immediate) to the instruction stream.
  void Emit(uint32_t value);

  // Emit slow paths queued during assembly and promote short branches to long if needed.
  void FinalizeCode() override;

  // Emit branches and finalize all instructions.
  void FinalizeInstructions(const MemoryRegion& region) override;

 private:

  // Emit helpers.

  // 2R-Type instruction:
  //
  //   31                                           10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . . . . . . . .| . . . .| . . . . .]
  //   [                  opcode                      | rj/rs1 |   rd     ]
  //   --------------------------------------------------------------------
  template <typename Reg2, typename Reg1>
  void Emit2R(uint32_t opcode, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<22>(opcode));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 10 | static_cast<uint32_t>(rj) << 5 |
                        static_cast<uint32_t>(rj);
    Emit(encoding);
  }

  // 3R-Type instruction:
  //
  //   31                                     15 14    10  9     5 4        0
  //   ----------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . . . . .| . . . .| . . . .| . . . . ]
  //   [                  opcode                | rk/rs2 | rj/rs1 |   rd    ]
  //   ----------------------------------------------------------------------
  template <typename Reg3, typename Reg2, typename Reg1>
  void Emit3R(uint32_t opcode, Reg3 rk, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<16>(opcode));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rk)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 15 | static_cast<uint32_t>(rk) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 4R-Type instruction:
  //
  //   31                        20 19      15 14    10  9     5 4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . | . . . . .| . . . .| . . . .| . . . . ]
  //   [       opcode 31:20        |  ra/rs3  | rk/rs2 | rj/rs1 |   rd    ]
  //   --------------------------------------------------------------------
  template <typename Reg4, typename Reg3, typename Reg2, typename Reg1>
  void Emit4R(uint32_t opcode, Reg4 ra, Reg3 rk, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<12>(opcode));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rk)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(ra)));
    uint32_t encoding = opcode << 20 | static_cast<uint32_t>(ra) << 15 | static_cast<uint32_t>(rk) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 2RI8-Type instruction:
  //
  //   31                              18 17        10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .| . . . .| . . . . .]
  //   [            opcode 31:18         |    I8      | rj/rs1 |   rd     ]
  //   --------------------------------------------------------------------
  template <typename Reg2, typename Reg1>
  void Emit2RI8(uint32_t opcode, int32_t imm8, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<14>(opcode));
    DCHECK(IsInt<8>(imm8)) << imm8; // Operators overloading when trigger assertion
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 18 | static_cast<uint32_t>(imm8 & 0xff) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 2RI12-Type instruction:
  //
  //   31                              22 21        10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .| . . . .| . . . . .]
  //   [            opcode 31:22         |    I12     | rj/rs1 |   rd     ]
  //   --------------------------------------------------------------------
  template <typename Reg2, typename Reg1>
  void Emit2RI12(uint32_t opcode, int32_t imm12, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<10>(opcode));
    DCHECK(IsInt<12>(imm12)) << imm12; // Operators overloading when trigger assertion
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 22 | static_cast<uint32_t>(imm12 & 0xfff) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 2RI12-Type instruction:
  //
  //   31                              22 21        10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .| . . . .| . . . . .]
  //   [            opcode 31:22         |    I12     | rj/rs1 |   rd     ]
  //   --------------------------------------------------------------------
  template <typename Reg2, typename Reg1>
  void Emit2RI12_U(uint32_t opcode, uint32_t imm12, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<10>(opcode));
    DCHECK(IsUint<12>(imm12)) << imm12;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 22 | (imm12 & 0xfff) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 2RI14-Type instruction:
  //
  //   31                              24 23        10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .| . . . .| . . . . .]
  //   [            opcode 31:24         |    I14     | rj/rs1 |   rd     ]
  //   --------------------------------------------------------------------
  template <typename Reg2, typename Reg1>
  void Emit2RI14(uint32_t opcode, int32_t imm14, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<8>(opcode));
    DCHECK(IsInt<14>(imm14)) << imm14; // Operators overloading when trigger assertion
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 24 | static_cast<uint32_t>(imm14 & 0x3fff) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 2RI16-Type instruction:
  //
  //   31                              26 25        10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .| . . . .| . . . . .]
  //   [            opcode 31:26         |    I16     | rj/rs1 |   rd     ]
  //   --------------------------------------------------------------------
  template <typename Reg2, typename Reg1>
  void Emit2RI16(uint32_t opcode, int32_t imm16, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<6>(opcode));
    DCHECK(IsInt<16>(imm16)) << imm16; // Operators overloading when trigger assertion
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 26 | ((imm16 ) & 0xffff) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 2RI16-Type instruction:
  //
  //   31                              26 25        10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .| . . . .| . . . . .]
  //   [            opcode 31:26         |    I16     | rj/rs1 |   rd     ]
  //   --------------------------------------------------------------------
  template <typename Reg2, typename Reg1>
  void Emit2RI16_B(uint32_t opcode, int32_t imm16, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<6>(opcode));
    DCHECK(IsInt<16>(imm16)) << imm16; // Operators overloading when trigger assertion
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 26 | ((imm16 >> 2 ) & 0xffff) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 1RI21-Type instruction:
  //
  //   31                              26 25        10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .| . . . .| . . . . .]
  //   [            opcode 31:26         | I21[15:0]  |   rd   |I21[20:16]]
  //   --------------------------------------------------------------------
  template <typename Reg1>
  void Emit1RI21(uint32_t opcode, int32_t imm21, Reg1 rd) {
    DCHECK(IsUint<6>(opcode));
    DCHECK(IsInt<21>(imm21)) << imm21;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    // imm will be aligned in assembly, so here use >> 2 to get larger range
    uint32_t encoding = opcode << 26 | ((imm21 >> 2) & 0xFFFF) << 10 |
                        static_cast<uint32_t>(rd) << 5 | (((imm21 >> 2) & 0x1F0000) >> 16);
    Emit(encoding);
  }

  // I26-Type instruction:
  //
  //   31                              26 25        10  9     5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .| . . . .| . . . . .]
  //   [            opcode 31:26         | I26[15:0]  |     I26[25:16]    ]
  //   --------------------------------------------------------------------
  void EmitI26(uint32_t opcode, int32_t imm26) {
    DCHECK(IsUint<6>(opcode));
    DCHECK(IsInt<26>(imm26)) << imm26;
    uint32_t encoding = opcode << 26 | ((imm26 >> 2) & 0xFFFF) << 10 |
                        (((imm26 >> 2) & 0x3FF0000) >> 16);
    Emit(encoding);
  }

  static constexpr uint32_t kXlen = 64;

  DISALLOW_COPY_AND_ASSIGN(Loongarch64Assembler);
};

}  // namespace loongarch64
}  // namespace art

#endif  // ART_COMPILER_UTILS_LOONGARCH64_ASSEMBLER_LOONGARCH64_H_
