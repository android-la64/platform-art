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

#include "disassembler_loongarch64.h"

#include "android-base/logging.h"
#include "android-base/stringprintf.h"

#include <sstream>

#include "base/bit_utils.h"
#include "base/casts.h"

using android::base::StringPrintf;

namespace art {
namespace loongarch64 {

#define LAInstructionSize 4

class DisassemblerLoongarch64::Printer {
 public:
  Printer(DisassemblerLoongarch64* disassembler, std::ostream& os)
      : disassembler_(disassembler), os_(os), pc_(0) {}

  void Dump32(const uint8_t* insn);

 private:
  // This enumeration should mirror the declarations in runtime/arch/loongarch64/registers_loongarch64.h.
  // We do not include that file to avoid a dependency on libart.
  enum {
    Zero = 0,
    RA = 1,
    FP  = 22,
    TR  = 24,
  };

  class ScopedNewLinePrinter {
    std::ostream& os_;

   public:
    explicit ScopedNewLinePrinter(std::ostream& os) : os_(os) {}
    ~ScopedNewLinePrinter() { os_ << '\n'; }
  };

  static const char* XRegName(uint32_t regno);
  static const char* FRegName(uint32_t regno);
  static const char* VRegName(uint32_t regno);
  static const char* FCCRegName(uint32_t regno);
  static const char* CondName(uint32_t regno);
  static const char* RoundingModeName(uint32_t rm);

  // Regular instruction immediate utils

  static int32_t Decode32Imm12(uint32_t insn32) {
    uint32_t sign = (insn32 >> 31);
    uint32_t imm12 = (insn32 >> 20);
    return static_cast<int32_t>(imm12) - static_cast<int32_t>(sign << 12);  // Sign-extend.
  }

  static uint32_t Decode32UImm7(uint32_t insn32) { return (insn32 >> 25) & 0x7Fu; }

  static uint32_t Decode32UImm12(uint32_t insn32) { return (insn32 >> 20) & 0xFFFu; }

  static int32_t Decode32StoreOffset(uint32_t insn32) {
    uint32_t bit11 = insn32 >> 31;
    uint32_t bits5_11 = insn32 >> 25;
    uint32_t bits0_4 = (insn32 >> 7) & 0x1fu;
    uint32_t imm = (bits5_11 << 5) + bits0_4;
    return static_cast<int32_t>(imm) - static_cast<int32_t>(bit11 << 12);  // Sign-extend.
  }

  // Compressed instruction immediate utils

  // Extracts the offset from a compressed instruction
  // where `offset[5:3]` is in bits `[12:10]` and `offset[2|6]` is in bits `[6:5]`
  static uint32_t Decode16CMOffsetW(uint32_t insn16) {
    DCHECK(IsUint<16>(insn16));
    return BitFieldExtract(insn16, 5, 1) << 6 | BitFieldExtract(insn16, 10, 3) << 3 |
           BitFieldExtract(insn16, 6, 1) << 2;
  }

  // Extracts the offset from a compressed instruction
  // where `offset[5:3]` is in bits `[12:10]` and `offset[7:6]` is in bits `[6:5]`
  static uint32_t Decode16CMOffsetD(uint32_t insn16) {
    DCHECK(IsUint<16>(insn16));
    return BitFieldExtract(insn16, 5, 2) << 6 | BitFieldExtract(insn16, 10, 3) << 3;
  }

  // Re-orders raw immediatate into real value
  // where `imm[5:3]` is in bits `[5:3]` and `imm[8:6]` is in bits `[2:0]`
  static uint32_t Uimm6ToOffsetD16(uint32_t uimm6) {
    DCHECK(IsUint<6>(uimm6));
    return (BitFieldExtract(uimm6, 3, 3) << 3) | (BitFieldExtract(uimm6, 0, 3) << 6);
  }

  // Re-orders raw immediatate to form real value
  // where `imm[5:2]` is in bits `[5:2]` and `imm[7:6]` is in bits `[1:0]`
  static uint32_t Uimm6ToOffsetW16(uint32_t uimm6) {
    DCHECK(IsUint<6>(uimm6));
    return (BitFieldExtract(uimm6, 2, 4) << 2) | (BitFieldExtract(uimm6, 0, 2) << 6);
  }

  // Re-orders raw immediatate to form real value
  // where `imm[1]` is in bit `[0]` and `imm[0]` is in bit `[1]`
  static uint32_t Uimm2ToOffset10(uint32_t uimm2) {
    DCHECK(IsUint<2>(uimm2));
    return (uimm2 >> 1) | (uimm2 & 0x1u) << 1;
  }

  // Re-orders raw immediatate to form real value
  // where `imm[1]` is in bit `[0]` and `imm[0]` is `0`
  static uint32_t Uimm2ToOffset1(uint32_t uimm2) {
    DCHECK(IsUint<2>(uimm2));
    return (uimm2 & 0x1u) << 1;
  }

  template <size_t kWidth>
  static constexpr int32_t SignExtendBits(uint32_t bits) {
    static_assert(kWidth < BitSizeOf<uint32_t>());
    const uint32_t sign_bit = (bits >> kWidth) & 1u;
    return static_cast<int32_t>(bits) - static_cast<int32_t>(sign_bit << kWidth);
  }

  // Extracts the immediate from a compressed instruction
  // where `imm[5]` is in bit `[12]` and `imm[4:0]` is in bits `[6:2]`
  // and performs sign-extension if required
  template <typename T>
  static T Decode16Imm6(uint32_t insn16) {
    DCHECK(IsUint<16>(insn16));
    static_assert(std::is_integral_v<T>, "T must be integral");
    const T bits =
        BitFieldInsert(BitFieldExtract(insn16, 2, 5), BitFieldExtract(insn16, 12, 1), 5, 1);
    const T checked_bits = dchecked_integral_cast<T>(bits);
    if (std::is_unsigned_v<T>) {
      return checked_bits;
    }
    return SignExtendBits<6>(checked_bits);
  }

  // Regular instruction register utils

  static uint32_t GetRd(uint32_t insn32) { return insn32 & 0x1fu; }
  static uint32_t GetRj(uint32_t insn32) { return (insn32 >> 5) & 0x1fu; }
  static uint32_t GetRk(uint32_t insn32) { return (insn32 >> 10) & 0x1fu; }
  static uint32_t GetRa(uint32_t insn32) { return (insn32 >> 15) & 0x1fu; }

  void Print2RType(uint32_t insn32);
  void PrintAsrt(uint32_t insn32);
  void PrintAlsl(uint32_t insn32);
  void PrintBytepick(uint32_t insn32);
  void PrintBstrW(uint32_t insn32);
  void Print3RType(uint32_t insn32);
  void Print3RLOADSTOREType(uint32_t insn32);
  void Print2RUI5UI6Type(uint32_t insn32);
  void Print2RI12Type(uint32_t insn32);
  void Print2RSI14Type(uint32_t insn32);
  void PrintBstrD(uint32_t insn32);
  void PrintADDU16ID(uint32_t insn32);
  void PrintBRANCHType(uint32_t insn32);
  void Print1RSI20Type(uint32_t insn32);
  void PrintF3R2RType(uint32_t insn32);
  void PrintF4RType(uint32_t insn32);
  void PrintOtherType(uint32_t insn32);

  DisassemblerLoongarch64* const disassembler_;
  std::ostream& os_;
  size_t pc_;
};

const char* DisassemblerLoongarch64::Printer::XRegName(uint32_t regno) {
  static const char* const kXRegisterNames[] = {
      "zero",
      "ra",
      "tp",
      "sp",
      "a0", // v0
      "a1", // v1
      "a2",
      "a3",
      "a4",
      "a5",
      "a6",
      "a7",
      "t0",
      "t1",
      "t2",
      "t3",
      "t4",
      "t5",
      "t6",
      "t7",
      "t8",
      "x",
      "fp",
      "s0",
      "s1",
      "s2",
      "s3",
      "s4",
      "s5",
      "s6",
      "s7",
      "s8",
  };
  static_assert(std::size(kXRegisterNames) == 32);
  DCHECK_LT(regno, 32u);
  return kXRegisterNames[regno];
}

const char* DisassemblerLoongarch64::Printer::FRegName(uint32_t regno) {
  static const char* const kFRegisterNames[] = {
      "fa0",
      "fa1",
      "fa2",
      "fa3",
      "fa4",
      "fa5",
      "fa6",
      "fa7",
      "ft0",
      "ft1",
      "ft2",
      "ft3",
      "ft4",
      "ft5",
      "ft6",
      "ft7",
      "ft8",
      "ft9",
      "ft10",
      "ft11",
      "ft12",
      "ft13",
      "ft14",
      "ft15",
      "fs0",
      "fs1",
      "fs2",
      "fs3",
      "fs4",
      "fs5",
      "fs6",
      "fs7",
  };
  static_assert(std::size(kFRegisterNames) == 32);
  DCHECK_LT(regno, 32u);
  return kFRegisterNames[regno];
}

const char* DisassemblerLoongarch64::Printer::FCCRegName(uint32_t regno) {
  static const char* const kFCCRegisterNames[] = {
      "fcc0",
      "fcc1",
      "fcc2",
      "fcc3",
      "fcc4",
      "fcc5",
      "fcc6",
      "fcc7",
  };
  static_assert(std::size(kFCCRegisterNames) == 8);
  DCHECK_LT(regno, 8u);
  return kFCCRegisterNames[regno];
}

const char* DisassemblerLoongarch64::Printer::VRegName(uint32_t regno) {
  static const char* const kVRegisterNames[] = {
      "V0",
      "V1",
      "V2",
      "V3",
      "V4",
      "V5",
      "V6",
      "V7",
      "V8",
      "V9",
      "V10",
      "V11",
      "V12",
      "V13",
      "V14",
      "V15",
      "V16",
      "V17",
      "V18",
      "V19",
      "V20",
      "V21",
      "V22",
      "V23",
      "V24",
      "V25",
      "V26",
      "V27",
      "V28",
      "V29",
      "V30",
      "V31",
  };
  static_assert(std::size(kVRegisterNames) == 32);
  DCHECK_LT(regno, 32u);
  return kVRegisterNames[regno];
}

const char* DisassemblerLoongarch64::Printer::CondName(uint32_t regno) {
  static const char* const kCondNames[] = {
    "fcmp.caf", // 0x0
    "fcmp.saf", // 0x1
    "fcmp.clt", // 0x2
    "fcmp.slt", // 0x3
    "fcmp.ceq", // 0x4
    "fcmp.seq", // 0x5
    "fcmp.cle", // 0x6
    "fcmp.sle", // 0x7
    "fcmp.cun", // 0x8
    "fcmp.sun", // 0x9
    "fcmp.cult",// 0xA
    "fcmp.sult",// 0xB
    "fcmp.cueq",// 0xC
    "fcmp.sueq",// 0xD
    "fcmp.cule",// 0xE
    "fcmp.sule",// 0xF
    "fcmp.cne", // 0x10
    "fcmp.sne", // 0x11
    "unknow",   // 0x12
    "unknow",   // 0x13
    "fcmp.cor", // 0x14
    "fcmp.sor", // 0x15
    "unknow",   // 0x16
    "unknow",   // 0x17
    "fcmp.cune",// 0x18
    "fcmp.sune",// 0x19
  };
  static_assert(std::size(kCondNames) == 26);
  DCHECK_LT(regno, 26u);
  return kCondNames[regno];
}

// FMADD.S - FSEL
void DisassemblerLoongarch64::Printer::PrintF4RType(uint32_t insn32) {
  uint32_t fd = GetRd(insn32);
  uint32_t fj = GetRj(insn32);
  uint32_t fk = GetRk(insn32);
  uint32_t fa = GetRa(insn32);
  uint32_t op = insn32 >> 20;

  if (op >= 0x81u && op <= 0x8eu) {
    static const char* const kOpcodes[] = { "NULL", "fmadd.s",  "fmadd.d",  "NULL", "NULL", "fmsub.s",  "fmsub.d",  "NULL",
                                            "NULL", "fnmadd.s", "fnmadd.d", "NULL", "NULL", "fnmsub.s", "fnmsub.d", "NULL",};
    uint32_t index = op & 0xfu;

    os_ << kOpcodes[index] << " " << FRegName(fd) << ", " << FRegName(fj) << ", " << FRegName(fk) << ", " << FRegName(fa);
  } else if (op == 0xc1u || op == 0xc2u) {
    uint32_t cond = fa;
    uint32_t cd = fd;
    if (op == 0xc1u) os_ << CondName(cond) << ".s" << " " << FCCRegName(cd) << ", " << FRegName(fj) << ", " << FRegName(fk);
    else             os_ << CondName(cond) << ".d" << " " << FCCRegName(cd) << ", " << FRegName(fj) << ", " << FRegName(fk);
  } else if (op == 0xd0u) {
    uint32_t ca = fa;
    os_ << "fsel" << " " << FRegName(fd) << ", " << FRegName(fj) << ", " << FRegName(fk) << ", " << FCCRegName(ca);
  } else {
    os_ << "<unknown32>" << std::hex << insn32;
  }
}
void DisassemblerLoongarch64::Printer::PrintOtherType(uint32_t insn32) {
  // TODO MOVGR2FCSR - FRINT.D

  // PrintCsr(insn32);             // CSRRD - CSRXCHG
  // PrintCacop(insn32);           // CACOP
  // PrintLDDIRPTE(insn32);           // LDDIR - LDPTE
  // PrintIOCSR(insn32);           // IOCSRRD.B - IOCSRWR.D
  // PrintTLBX(insn32);           // TLBCLR - ERTN
  // PrintIDLE(insn32);           // IDLE
  // PrintINVTLB(insn32);           // INVTLB
   os_ << "<unknown32>" << std::hex << insn32;
}

// FADD.S - FRINT.D
void DisassemblerLoongarch64::Printer::PrintF3R2RType(uint32_t insn32) {
  uint32_t fd = GetRd(insn32);
  uint32_t fj = GetRj(insn32);
  uint32_t op = insn32 >> 15;

  if (op >= 0x201 && op <= 0x226u) { //3R
    uint32_t fk = GetRk(insn32);
    static const char* const kOpcodes[] = { "NULL", "fadd.s", "fadd.d", "NULL", "NULL", "fsub.s", "fsub.d", "NULL",
                                            "NULL", "fmul.s", "fmul.d", "NULL", "NULL", "fdiv.s", "fdiv.d", "NULL",
                                            "NULL", "fmax.s", "fmax.d", "NULL", "NULL", "fmin.s", "fmin.d", "NULL",
                                            "NULL", "fmaxa.s", "fmaxa.d", "NULL", "NULL", "fmina.s", "fmina.d", "NULL",
                                            "NULL", "fscaleb.s", "fscaleb.d", "NULL", "NULL", "fcopysign.s", "fcopysign.d", "NULL",};
    uint32_t index = op & 0x3fu;

    os_ << kOpcodes[index] << " " << FRegName(fd) << ", " << FRegName(fj) << ", " << FRegName(fk);

  } else { //2R
    uint32_t op_2r = insn32 >> 10;

    if (op_2r >= 0x4501u && op_2r <= 0x4526u) {
       uint32_t index = (insn32 >> 10) & 0x3fu;
       static const char* const kOpcodes[] = { "NULL", "fabs.s", "fabs.d", "NULL", "NULL", "fneg.s", "fneg.d", "NULL",
                                               "NULL", "flogb.s", "flogb.d", "NULL", "NULL", "fclass.s", "fclass.d", "NULL",
                                               "NULL", "fsqrt.s", "fsqrt.d", "NULL", "NULL", "frecip.s", "frecip.d", "NULL",
                                               "NULL", "frsqrt.s", "frsqrt.d", "NULL", "NULL", "frecipe.s", "frecipe.d", "NULL",
                                               "NULL", "frsqrte.s", "frsqrte.d", "NULL", "NULL", "fmov.s", "fmov.d", "NULL",};

       os_ << kOpcodes[index] << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r >= 0x4529u && op_2r <= 0x452fu) {
       uint32_t index = op_2r & 0x7u;
       static const char* const kOpcodes[] = { "NULL", "movgr2fr.w", "movgr2fr.d", "movgr2frh.w", "movfr2gr.s", "movfr2gr.d", "movfrh2gr.s" };

       if (index <=3) {
         os_ << kOpcodes[index] << " " << FRegName(fd) << ", " << XRegName(fj);
       } else {
         os_ << kOpcodes[index] << " " << XRegName(fd) << ", " << FRegName(fj);
       }
    } else if (op_2r == 0x4530u) {
         os_ << "movgr2fcsr" << " " << (insn32 & 0x1fu) << ", " << XRegName(fj);
    } else if (op_2r == 0x4532u) {
         os_ << "movfcsr2gr" << " " << XRegName(fd) << ", " << (insn32 & 0x1fu);
    } else if (op_2r >= 0x4534u && op_2r <= 0x4537u) {
      static const char* const kOpcodes[] = { "movfr2cf", "movcf2fr", "movgr2cf", "movcf2gr",};
      uint32_t index = op_2r & 0x3u;

      if (index == 0)      os_ << kOpcodes[index] << " " << FCCRegName(fd) << ", " << FRegName(fj);
      else if (index == 1) os_ << kOpcodes[index] << " " << FRegName(fd) << ", " << FCCRegName(fj);
      else if (index == 2) os_ << kOpcodes[index] << " " << FCCRegName(fd) << ", " << XRegName(fj);
      else if (index == 3) os_ << kOpcodes[index] << " " << XRegName(fd) << ", " << FCCRegName(fj);
    } else if (op_2r == 0x4646u) {
      os_ << "fcvt.s.d"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4649u) {
      os_ << "fcvt.d.s"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4681u) {
      os_ << "ftintrm.w.s" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4682u) {
      os_ << "ftintrm.w.d" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4689u) {
      os_ << "ftintrm.l.s" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x468au) {
      os_ << "ftintrm.l.d" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4691u) {
      os_ << "ftintrp.w.s" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4692u) {
      os_ << "ftintrp.w.d" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4699u) {
      os_ << "ftintrp.l.s" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x469au) {
      os_ << "ftintrp.l.d" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46a1u) {
      os_ << "ftintrz.w.s" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46a2u) {
      os_ << "ftintrz.w.d" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46a9u) {
      os_ << "ftintrz.l.s" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46aau) {
      os_ << "ftintrz.l.d" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46b1u) {
      os_ << "ftintrne.w.s" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46b2u) {
      os_ << "ftintrne.w.d" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46b9u) {
      os_ << "ftintrne.l.s" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46bau) {
      os_ << "ftintrne.l.d" << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46c1u) {
      os_ << "ftint.w.s"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46c2u) {
      os_ << "ftint.w.d"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46c9u) {
      os_ << "ftint.l.s"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x46cau) {
      os_ << "ftint.l.d"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4744u) {
      os_ << "ffint.s.w"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4746u) {
      os_ << "ffint.s.l"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4748u) {
      os_ << "ffint.d.w"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x474au) {
      os_ << "ffint.d.l"    << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4791u) {
      os_ << "frint.s"      << " " << FRegName(fd) << ", " << FRegName(fj);
    } else if (op_2r == 0x4792u) {
      os_ << "frint.d"      << " " << FRegName(fd) << ", " << FRegName(fj);
    } else {
      os_ << "<unknown32>" << std::hex << insn32;
    }
  }
}

void DisassemblerLoongarch64::Printer::PrintADDU16ID(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t si16 = (insn32 >> 10) & 0xffffu;

  std::string dot = ", ";
  if (si16>>15 == 1) {
    si16 = ~(si16-1) & 0xffffu;
    dot = ", -";
  }

  
  os_ << "addu16i.d" << " " << XRegName(rd) << ", " << XRegName(rj) << dot << si16;
}

void DisassemblerLoongarch64::Printer::Print2RSI14Type(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t si14 = (insn32 >> 10) & 0x3fffu;
  uint32_t op = insn32 >> 24;
  uint32_t index = op & 0xfu;
  static const char* const kOpcodes[] = { "ll.w", "sc.w", "ll.d", "sc.d", "ldptr.w", "stptr.w", "ldptr.d", "stptr.d" };
  std::string dot = ", ";
  if (si14>>13 == 1) {
    si14 = ~(si14-1) & 0x3fffu;
    dot = ", -";
  }
  os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << dot << si14;
}

void DisassemblerLoongarch64::Printer::Print3RLOADSTOREType(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t rk = GetRk(insn32);
  uint32_t op = insn32 >> 15;

  if (op >= 0x7000 && op <= 0x7078) {
    uint32_t index = (insn32 >> 18) & 0xfu;
    static const char* const kOpcodes[] = { "ldx.b",  "ldx.h",  "ldx.w",  "ldx.d",  "stx.b",  "stx.h",  "stx.w",  "stx.d",
                                            "ldx.bu", "ldx.hu", "ldx.wu", "preldx", "fldx.s", "fldx.d", "fstx.s", "fstx.d" };

    if (index <= 11) {
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
    } else {
      os_ << kOpcodes[index] << " " << FRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
    }
  } else if (op == 0x70ae) {
    os_ << "sc.q" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);

  } else if (op == 0x70af) {
    uint32_t index = (insn32 >> 10) & 0xfu;
    static const char* const kOpcodes[] = { "llacq.w", "screl.w", "llacq.d", "screl.d"};

    os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj);

  } else if (op >= 0x70b0 && op <= 0x70bf) {
    uint32_t index = (insn32 >> 15) & 0xfu;
    static const char* const kOpcodes[] = { "amcas.b",  "amcas.h",  "amcas.w",  "amcas.d",  "amcas_db.b",  "amcas_db.h",  "amcas_db.w",  "amcas_db.d",
                                            "amswap.b", "amswap.h", "amadd.b", "amadd.h", "amswap_db.b", "amswap_db.h", "amadd_db.b", "amadd_db.h" };

    os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
  } else if (op >= 0x70c0 && op <= 0x70cf) {
    uint32_t index = (insn32 >> 15) & 0xfu;
    static const char* const kOpcodes[] = { "amswap.w",  "amswap.d",  "amadd.w",  "amadd.d",  "amand.w",  "amand.d",  "amor.w",  "amor.d",
                                            "amxor.w",   "amxor.d",   "ammax.w",   "ammax.d", "ammin.w",  "ammin.d",  "ammax.wu", "ammax.du"};

    os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
  } else if (op >= 0x70d0 && op <= 0x70df) {
    uint32_t index = (insn32 >> 15) & 0xfu;
    static const char* const kOpcodes[] = { "ammin.wu",  "ammin.du",  "amswap_db.w",  "amswap_db.d", "amadd_db.w",  "amadd_db.d", "amand_db.w",  "amand_db.d",
                                            "amor_db.w",  "amor_db.d", "amxor_db.w",   "amxor_db.d", "ammax_db.w",   "ammax_db.d", "ammin_db.w",  "ammin_db.d"};

    os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
  } else if (op >= 0x70e0 && op <= 0x70e3) {
    uint32_t index = (insn32 >> 15) & 0xfu;
    static const char* const kOpcodes[] = { "ammax_db.wu",   "ammax_db.du", "ammin_db.wu",  "ammin_db.du" };

    os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
  } else if (op == 0x70e4) {
    uint32_t hint = insn32 & 0x3fffu;
    os_ << "dbar" << " " << StringPrintf("0x%x", hint);
  } else if (op == 0x70e5) {
    uint32_t hint = insn32 & 0x3fffu;
    os_ << "ibar" << " " << StringPrintf("0x%x", hint);
  } else if (op >= 0x70e8 && op <= 0x70ef) {
    uint32_t index = (insn32 >> 15) & 0xfu;
    static const char* const kOpcodes[] = { "fldgt.s", "fldgt.d", "fldle.s", "fldle.d", "fstgt.s", "fstgt.d", "fstle.s", "fstgt.d" };

    os_ << kOpcodes[index] << " " << FRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
  } else if (op >= 0x70f0 && op <= 0x70ff ) {
    uint32_t index = (insn32 >> 15) & 0xfu;
    static const char* const kOpcodes[] = { "ldgt.b", "ldgt.h", "ldgt.w", "ldgt.d", "ldle.b", "ldle.h", "ldle.w", "ldle.d",
                                            "stgt.b", "stgt.h", "stgt.w", "stgt.d", "stle.b", "stle.h", "stle.w", "stle.d" };

    os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
  }
}

void DisassemblerLoongarch64::Printer::PrintBRANCHType(uint32_t insn32) {
  uint32_t op = insn32 >> 26;
  uint32_t index = op & 0xfu;
  uint32_t offs_16 = (insn32 >> 10) & 0xffffu;

  static const char* const kOpcodes[] = { "beqz", "bnez", "null", "jirl", "b", "bl", "beq", "bne",
                                          "blt", "bge", "bltu", "bgeu"};
  switch (index) {
  case 0u:
  case 1u: {
    uint32_t offs_h = GetRd(insn32);
    uint32_t rj = GetRj(insn32);
    uint32_t offs21 = ((offs_h << 16) | offs_16) << 2;
    size_t addr = pc_ + offs21;

    std::string dot = ", ";
    if (offs_h>>4 == 1) {
      offs21 = ~(offs21-1) & 0x1fffffu;
      dot = ", -";
      addr = pc_ - offs21;
    }
    os_ << kOpcodes[index] << " " << XRegName(rj) << dot << offs21 << StringPrintf(" (addr 0x%zx)", addr);
    break;
   }
  case 2u: {
    uint32_t offs_h = GetRd(insn32);
    uint32_t cj = (insn32 >> 5) & 0x3u;
    uint32_t offs = ((offs_h << 16) | offs_16) << 2;

    if (((insn32 >> 8) & 0x1u) == 0) {
      os_ << "bceqz" << " " << FCCRegName(cj) << ", " << offs;
    } else {
      os_ << "bcnez" << " " << FCCRegName(cj) << ", " << offs;
    }
    break;
   }
  case 4u:
  case 5u: { // B BL
    uint32_t offs_h = insn32 & 0x3ffu;
    uint32_t offs26 = ((offs_h << 16) | offs_16) << 2;
    size_t addr = pc_ + offs26;

    std::string dot = " ";
    if (offs_h>>9 == 1) {
      offs26 = ~(offs26-1) & 0x3ffffffu;
      addr = pc_ - offs26;
      dot = " -";
    }
    os_ << kOpcodes[index] << dot << offs26 << StringPrintf(" (addr 0x%zx)", addr);
    break;
   }
  default: {
    uint32_t rd = GetRd(insn32);
    uint32_t rj = GetRj(insn32);

    std::string dot = ", ";
    offs_16 = offs_16 << 2;
    size_t addr = pc_ + offs_16;

    if (offs_16>>15 == 1) {
      offs_16 = ~(offs_16-1) & 0xffffu;
      addr = pc_ + offs_16;
      dot = ", -";
    }
    if(index != 3u) { // BEQ BNE BLT BGE BLTU GGEU
      os_ << kOpcodes[index] << " " << XRegName(rj) << ", " << XRegName(rd) << dot << offs_16 << StringPrintf(" (addr 0x%zx)", addr);
    } else { // JIRL
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << dot << offs_16;
    }
    break;
   }
  }
}

void DisassemblerLoongarch64::Printer::Print1RSI20Type(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t si20 = (insn32 >> 5) & 0xfffffu;
  uint32_t op = insn32 >> 25;
  uint32_t index = op & 0x7u;

  static const char* const kOpcodes[] = { "NULL", "NULL", "lu12i.w", "lu32i.d", "pcaddi", "pcalau12i", "pcaddu12i", "pcaddu18i"};

  std::string dot = ", ";
  if (si20>>19 == 1) {
    si20 = ~(si20-1) & 0xfffffu;
    dot = ", -";
  }
  os_ << kOpcodes[index] << " " << XRegName(rd) << dot << si20 << StringPrintf(" (0x%x)", si20);
}

void DisassemblerLoongarch64::Printer::Print2RI12Type(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t op = insn32 >> 22;
  uint32_t si12 = (insn32 >> 10) & 0xfffu;
  uint32_t ui12 = si12;

  std::string dot = ", ";
  if (si12>>11 == 1) {
    si12 = ~(si12-1) & 0xfffu;
    dot = ", -";
  }

  if (op >= 0x8u && op <= 0xfu) {
    uint32_t index = op & 0x7u;
    static const char* const kOpcodes[] = { "slti", "sltui", "addi.w", "addi.d", "lu52i.d", "andi", "ori", "xori"};

    if (index <= 4) { // si12
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << dot << si12 << StringPrintf(" (0x%x)", si12);
    } else { // ui12
      if (index == 6 && rj == 0) { // ori
        os_ << "move" << " " << XRegName(rd) << StringPrintf(", 0x%x", ui12);
      } else {
        os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << StringPrintf(", 0x%x", ui12);
      }
    }
  } else {
    uint32_t index = op & 0x1fu;
    static const char* const kOpcodes[] = { "ld.b",  "ld.h",  "ld.w",  "ld.d",  "st.b",  "st.h",  "st.w",  "st.d",
                                            "ld.bu", "ld.hu", "ld.wu", "preld", "fld.s", "fst.s", "fld.d", "fst.d" };

    if (index <= 11) {
      if (rj == TR) {
        std::ostringstream tmp_stream;
        disassembler_->GetDisassemblerOptions()->thread_offset_name_function_(tmp_stream, static_cast<uint32_t>(si12));
        os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << "tr" << dot << si12 << " ; " << tmp_stream.str().c_str();
      } else {
        os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << dot << si12;
      }
    } else {
      os_ << kOpcodes[index] << " " << FRegName(rd) << ", " << XRegName(rj) << dot << si12;
    }
  }
}

void DisassemblerLoongarch64::Printer::Print2RType(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t op = insn32 >> 10;

  switch (op >> 2) {
    case 1u: {
      static const char* const kOpcodes[] = { "clo.w", "clz.w", "cto.w", "ctz.w" };
      uint32_t index = op & 0x3u;
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj);
      break;
     }
    case 2u: {
      static const char* const kOpcodes[] = { "clo.d", "clz.d", "cto.d", "ctz.d" };
      uint32_t index = op & 0x3u;
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj);
      break;
     }
    case 3u: {
      static const char* const kOpcodes[] = { "revb.2h", "revb.4h", "revb.2w", "revb.d" };
      uint32_t index = op & 0x3u;
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj);
      break;
     }
    case 4u: {
      static const char* const kOpcodes[] = { "revh.2w", "revh.d", "bitrev.4b", "bitrev.8b" };
      uint32_t index = op & 0x3u;
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj);
      break;
     }
    case 5u: {
      static const char* const kOpcodes[] = { "bitrev.w", "bitrev.d", "ext.w.h", "ext.w.b" };
      uint32_t index = op & 0x3u;
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj);
      break;
     }
    case 6u: {
      static const char* const kOpcodes[] = { "rdtimel.w", "rdtimeh.w", "rdtime.d", "cpucfg" };
      uint32_t index = op & 0x3u;
      os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj);
      break;
     }
    default:
      os_ << "<unknown32>";
      break;
  }
}

void DisassemblerLoongarch64::Printer::PrintAsrt(uint32_t insn32) {
  uint32_t rj = GetRj(insn32);
  uint32_t rk = GetRk(insn32);
  uint32_t op = insn32 >> 15;

  if (op == 0x2u) {
    os_ << "asrtle.d" << " " << XRegName(rj) << ", " << XRegName(rk);
  } else if (op == 0x3u) {
    os_ << "asrtgt.d" << " " << XRegName(rj) << ", " << XRegName(rk);
  } else {
    os_ << "<unknown32>";
  }
}

void DisassemblerLoongarch64::Printer::PrintAlsl(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t rk = GetRk(insn32);
  uint32_t op = insn32 >> 17;
  uint32_t sa2 = (insn32 >> 15) & 0x3u;

  if (op == 0x0u) {
    os_ << "alsl.w" << " " << XRegName(rd) << XRegName(rj) << ", " << XRegName(rk) << ", " << sa2;
  } else if (op == 0x1u) {
    os_ << "alsl.wu" << " " << XRegName(rd) << XRegName(rj) << ", " << XRegName(rk) << ", " << sa2;
  } else {
    os_ << "<unknown32>";
  }
}

void DisassemblerLoongarch64::Printer::PrintBytepick(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t rk = GetRk(insn32);
  uint32_t op = insn32 >> 18;
  uint32_t sa2 = (insn32 >> 15) & 0x3u;
  uint32_t sa3 = (insn32 >> 15) & 0x7u;

  if (op == 0x0u) {
    os_ << "bytepick.w" << " " << XRegName(rd) << XRegName(rj) << ", " << XRegName(rk) << ", " << sa2;
  } else if (op == 0x1u) {
    os_ << "bytepick.d" << " " << XRegName(rd) << XRegName(rj) << ", " << XRegName(rk) << ", " << sa3;
  } else {
    os_ << "<unknown32>";
  }
}

void DisassemblerLoongarch64::Printer::Print3RType(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t rk = GetRk(insn32);
  uint32_t op = insn32 >> 15;

  switch (op >> 5) {
    case 1u:
    {
      static const char* const kOpcodes[] = { "add.w", "add.d", "sub.w", "sub.d", "slt", "sltu", "maskeqz", "masknez",
                                              "nop",   "and",   "or",    "xor",   "orn", "andn", "sll.w",   "srl.w",
                                              "sra.w", "sll.d", "srl.d", "sra.d", "NULL", "NULL", "rotr.w", "rotr.d",
                                              "mul.w", "mulh.w", "mulh.wu", "mul.d", "mulh.d", "mulh.du", "mulw.d.w", "mulw.d.wu" };
      uint32_t index = op & 0x1fu;
      if (index == 10 && rk == 0) { // or
        os_ << "move" << " " << XRegName(rd) << ", " << XRegName(rj);
      } else {
        os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
      }
      return;
    }
    case 2u:
    {
      if ((op >> 2) == 0x16u) {
        uint32_t sa2 = op & 0x3u;
        os_ << "alsl.d" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk) << ", " << sa2;
      } else if ((op >> 2) == 0x15u) {
        uint32_t code = insn32 & 0x7fffu;
        uint32_t index = op & 0x3u;
        static const char* const kOpcodes[] = { "break", "dbcl", "syscall"};
        os_ << kOpcodes[index] << " " << code;
      } else {
        uint32_t index = op & 0x1fu;
        static const char* const kOpcodes[] = { "div.w", "mod.w", "div.wu", "mod.wu", "div.d" , "mod.d", "div.du", "mod.du",
                                                "crc.w.b.w", "crc.w.h.w", "crc.w.w.w", "crc.w.d.w", "crcc.w.b.w", "crcc.w.h.w", "crcc.w.w.w", "crcc.w.d.w" };
        os_ << kOpcodes[index] << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << XRegName(rk);
      }
      return;
    }
    default:
    os_ << "<unknown32>";
    break;
  }
}

void DisassemblerLoongarch64::Printer::PrintBstrW(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t lsbw = (insn32 >> 10) & 0x1fu;
  uint32_t msbw = (insn32 >> 16) & 0x1fu;

  if ((insn32 >> 15 & 0x1u) == 0x0) {
    os_ << "bstrins.w" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << msbw << ", " << lsbw;
  } else if ((insn32 >> 15 & 0x1u) == 0x1u) {
    os_ << "bstrpick.w" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << msbw << ", " << lsbw;
  } else {
    os_ << "<unknown32>";
  }
}

void DisassemblerLoongarch64::Printer::PrintBstrD(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);
  uint32_t lsbd = (insn32 >> 10) & 0x3fu;
  uint32_t msbd = (insn32 >> 16) & 0x3fu;

  if ((insn32 >> 22) == 0x2) {
    os_ << "bstrins.d" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << msbd << ", " << lsbd;
  } else if ((insn32 >> 22) == 0x3) {
    os_ << "bstrpick.d" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << msbd << ", " << lsbd;
  } else {
    os_ << "<unknown32>";
  }
}

void DisassemblerLoongarch64::Printer::Print2RUI5UI6Type(uint32_t insn32) {
  uint32_t rd = GetRd(insn32);
  uint32_t rj = GetRj(insn32);

  uint32_t ui5 = (insn32 >> 10) & 0x1fu;
  uint32_t ui6 = (insn32 >> 10) & 0x3fu;
  uint32_t op_ui5 = insn32 >> 15;
  uint32_t op_ui6 = insn32 >> 16;

  if ((insn32 >> 21) == 0x11) {
    PrintBstrW(insn32);
  } else {
    if (op_ui5 == 0x81u) {
      os_ << "slli.w" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << ui5;
    } else if (op_ui6 == 0x41u) {
      os_ << "slli.d" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << ui6;
    } else if (op_ui5 == 0x89u) {
      os_ << "srli.w" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << ui5;
    } else if (op_ui6 == 0x45u) {
      os_ << "srli.d" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << ui6;
    } else if (op_ui5 == 0x91u) {
      os_ << "srai.w" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << ui5;
    } else if (op_ui6 == 0x49u) {
      os_ << "srai.d" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << ui6;
    } else if (op_ui5 == 0x99u) {
      os_ << "rotri.w" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << ui5;
    } else if (op_ui6 == 0x4du) {
      os_ << "rotri.d" << " " << XRegName(rd) << ", " << XRegName(rj) << ", " << ui6;
    } else {
      os_ << "<unknown32>";
    }
  }
}

void DisassemblerLoongarch64::Printer::Dump32(const uint8_t* insn) {
  uint32_t insn32 = static_cast<uint32_t>(insn[0]) +
                    (static_cast<uint32_t>(insn[1]) << 8) +
                    (static_cast<uint32_t>(insn[2]) << 16) +
                    (static_cast<uint32_t>(insn[3]) << 24);
  pc_ = insn - disassembler_->GetDisassemblerOptions()->base_address_;

  // CHECK_EQ(insn32 & 3u, 3u);
  os_ << disassembler_->FormatInstructionPointer(insn) << StringPrintf(": %08x\t", insn32);
  if ((insn32 >> 15)  == 0x0) {          // CLO.W-CPUCFG
    Print2RType(insn32);
  } else if (insn32 >> 16 == 0x1) {      // ASRTLE.D ASRTGT.D
    PrintAsrt(insn32);
  } else if (insn32 >> 18 == 0x1) {      // ALSL.W ALSL.WU
    PrintAlsl(insn32);
  } else if (insn32 >> 19 == 0x1) {      // BYTEPICK.W BYTEPICK.D
    PrintBytepick(insn32);
  } else if (insn32 >> 20 == 0x1) {      // ADD.W - MULW.D.WU
    Print3RType(insn32);
  } else if (insn32 >> 21 == 0x1) {      // DIV.W - ALSL.D
    Print3RType(insn32);
  } else if (insn32 >> 22 == 0x1) {      // SLLI.W - BSTRPICK.W
    Print2RUI5UI6Type(insn32);
  } else if (insn32 >> 23 == 0x1) {      // BSTRINS.D BSTRPICK.D
    PrintBstrD(insn32);
  } else if (insn32 >> 24 == 0x1) {      // FADD.S - FRINT.D
    PrintF3R2RType(insn32);
  } else if (insn32 >> 25 == 0x1) {      // SLTI - XORI
    Print2RI12Type(insn32);
  } else if (insn32 >> 26 == 0x1) {      // CSRRD - INVTLB
    PrintOtherType(insn32) ;
  } else if (insn32 >> 27 == 0x1) { // FMADD.S - FSEL
    PrintF4RType(insn32);
  } else if (insn32 >> 28 == 0x1) { // ADDU16I.D - PCADDU18I
    if (insn32 >> 26 == 0x4)
      PrintADDU16ID(insn32);
    else
      Print1RSI20Type(insn32);

  } else if (insn32 >> 29 == 0x1) { // LL.W - STLE.D
    if (insn32 >> 27 == 0x4u) {
      Print2RSI14Type(insn32);
    } else if (insn32 >> 27 == 0x5u) {
      Print2RI12Type(insn32);
    } else /*if (insn32 >> 27 == 0x7u)*/ {
      Print3RLOADSTOREType(insn32);
    }

  } else if (insn32 >> 30 == 0x1) { // BEQZ - BGEU
    PrintBRANCHType(insn32);
  } else {
    os_ << "<unknown32>";
  }
  os_ << "\n";
}

size_t DisassemblerLoongarch64::Dump(std::ostream& os, const uint8_t* begin) {
  if (begin < GetDisassemblerOptions()->base_address_ ||
      begin >= GetDisassemblerOptions()->end_address_) {
     LOG(WARNING) << "Dump outside the range";
    return 0u;  // Outside the range.
  }

  Printer printer(this, os);
  if (!IsAligned<2u>(begin) || GetDisassemblerOptions()->end_address_ - begin == 1) {
    LOG(FATAL) << "Unsupport 1 byte";
    return 1u;
  }

  printer.Dump32(begin);
  return LAInstructionSize;
}

void DisassemblerLoongarch64::Dump(std::ostream& os, const uint8_t* begin, const uint8_t* end) {
  Printer printer(this, os);
  DCHECK_EQ((end-begin)%LAInstructionSize, 0);
  const uint8_t* cur = begin;
  if (cur < end && !IsAligned<2u>(cur)) {
    // Unaligned, dump as a `.byte` to get to an aligned address.
    //printer.DumpByte(cur);
    LOG(FATAL) << "Unsupport 1 byte";
  }
  if (cur >= end) {
    return;
  }
  while (end - cur >= LAInstructionSize) {
      printer.Dump32(cur);
      cur += LAInstructionSize;
  }
}

}  // namespace loongarch64
}  // namespace art
