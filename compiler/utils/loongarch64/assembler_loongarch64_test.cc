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

#include <inttypes.h>

#include <map>

#include "arch/loongarch64/registers_loongarch64.h"
#include "base/bit_utils.h"
#include "utils/assembler_test.h"

#define __ GetAssembler()->

namespace art {
namespace loongarch64 {

struct LOONGARCH64CpuRegisterCompare {
  bool operator()(const XRegister& a, const XRegister& b) const { return a < b; }
};

class AssemblerLOONGARCH64Test : public AssemblerTest<Loongarch64Assembler,
                                                      Loongarch64Label,
                                                      XRegister,
                                                      FRegister,
                                                      int32_t> {
 public:
  using Base = AssemblerTest<Loongarch64Assembler,
                             Loongarch64Label,
                             XRegister,
                             FRegister,
                             uint32_t>;

  AssemblerLOONGARCH64Test()
      : instruction_set_features_(Loongarch64InstructionSetFeatures::FromVariant("default", nullptr)) {}

 protected:
  Loongarch64Assembler* CreateAssembler(ArenaAllocator* allocator) override {
    return new (allocator) Loongarch64Assembler(allocator, instruction_set_features_.get());
  }

  InstructionSet GetIsa() override { return InstructionSet::kLoongarch64; }

  void SetUpHelpers() override {
    if (registers_.size() == 0) {
     registers_.push_back(new XRegister(Zero));
     registers_.push_back(new XRegister(RA));
     registers_.push_back(new XRegister(TP));
     registers_.push_back(new XRegister(SP));
     registers_.push_back(new XRegister(A0));
     registers_.push_back(new XRegister(A1));
     registers_.push_back(new XRegister(A2));
     registers_.push_back(new XRegister(A3));
     registers_.push_back(new XRegister(A4));
     registers_.push_back(new XRegister(A5));
     registers_.push_back(new XRegister(A6));
     registers_.push_back(new XRegister(A7));
     registers_.push_back(new XRegister(T0));
     registers_.push_back(new XRegister(T1));
     registers_.push_back(new XRegister(T2));
     registers_.push_back(new XRegister(T3));
     registers_.push_back(new XRegister(T4));
     registers_.push_back(new XRegister(T5));
     registers_.push_back(new XRegister(T6));
     registers_.push_back(new XRegister(T7));
     registers_.push_back(new XRegister(T8));
     registers_.push_back(new XRegister(R21));
     registers_.push_back(new XRegister(FP));
     registers_.push_back(new XRegister(S0));
     registers_.push_back(new XRegister(S1));
     registers_.push_back(new XRegister(S2));
     registers_.push_back(new XRegister(S3));
     registers_.push_back(new XRegister(S4));
     registers_.push_back(new XRegister(S5));
     registers_.push_back(new XRegister(S6));
     registers_.push_back(new XRegister(S7));
     registers_.push_back(new XRegister(S8));

      //secondary_register_names_.emplace(Zero, "$r0");
      //secondary_register_names_.emplace(RA, "$r1");
      //secondary_register_names_.emplace(TP, "$r2");
      //secondary_register_names_.emplace(SP, "$r3");
      //secondary_register_names_.emplace(A0, "$r4");
      //secondary_register_names_.emplace(A1, "$r5");
      //secondary_register_names_.emplace(A2, "$r6");
      //secondary_register_names_.emplace(A3, "$r7");
      //secondary_register_names_.emplace(A4, "$r8");
      //secondary_register_names_.emplace(A5, "$r9");
      //secondary_register_names_.emplace(A6, "$r10");
      //secondary_register_names_.emplace(A7, "$r11");
      //secondary_register_names_.emplace(T0, "$r12");
      //secondary_register_names_.emplace(T1, "$r13");
      //secondary_register_names_.emplace(T2, "$r14");
      //secondary_register_names_.emplace(T3, "$r15");
      //secondary_register_names_.emplace(T4, "$r16");
      //secondary_register_names_.emplace(T5, "$r17");
      //secondary_register_names_.emplace(T6, "$r18");
      //secondary_register_names_.emplace(T7, "$r19");
      //secondary_register_names_.emplace(T8, "$r20");
      //secondary_register_names_.emplace(T9, "$r21");
      //secondary_register_names_.emplace(S9, "$r22");
      //secondary_register_names_.emplace(S0, "$r23");
      //secondary_register_names_.emplace(S1, "$r24");
      //secondary_register_names_.emplace(S2, "$r25");
      //secondary_register_names_.emplace(S3, "$r26");
      //secondary_register_names_.emplace(S4, "$r27");
      //secondary_register_names_.emplace(S5, "$r28");
      //secondary_register_names_.emplace(S6, "$r29");
      //secondary_register_names_.emplace(S7, "$r30");
      //secondary_register_names_.emplace(S8, "$r31");

      fp_registers_.push_back(new FRegister(FA0));
      fp_registers_.push_back(new FRegister(FA1));
      fp_registers_.push_back(new FRegister(FA2));
      fp_registers_.push_back(new FRegister(FA3));
      fp_registers_.push_back(new FRegister(FA4));
      fp_registers_.push_back(new FRegister(FA5));
      fp_registers_.push_back(new FRegister(FA6));
      fp_registers_.push_back(new FRegister(FA7));
      fp_registers_.push_back(new FRegister(FT0));
      fp_registers_.push_back(new FRegister(FT1));
      fp_registers_.push_back(new FRegister(FT2));
      fp_registers_.push_back(new FRegister(FT3));
      fp_registers_.push_back(new FRegister(FT4));
      fp_registers_.push_back(new FRegister(FT5));
      fp_registers_.push_back(new FRegister(FT6));
      fp_registers_.push_back(new FRegister(FT7));
      fp_registers_.push_back(new FRegister(FT8));
      fp_registers_.push_back(new FRegister(FT9));
      fp_registers_.push_back(new FRegister(FT10));
      fp_registers_.push_back(new FRegister(FT11));
      fp_registers_.push_back(new FRegister(FT12));
      fp_registers_.push_back(new FRegister(FT13));
      fp_registers_.push_back(new FRegister(FT14));
      fp_registers_.push_back(new FRegister(FT15));
      fp_registers_.push_back(new FRegister(FS0));
      fp_registers_.push_back(new FRegister(FS1));
      fp_registers_.push_back(new FRegister(FS2));
      fp_registers_.push_back(new FRegister(FS3));
      fp_registers_.push_back(new FRegister(FS4));
      fp_registers_.push_back(new FRegister(FS5));
      fp_registers_.push_back(new FRegister(FS6));
      fp_registers_.push_back(new FRegister(FS7));
   }
  }

  void TearDown() override {
    AssemblerTest::TearDown();
    STLDeleteElements(&registers_);
    STLDeleteElements(&fp_registers_);
  }

  std::vector<Loongarch64Label> GetAddresses() override {
    UNIMPLEMENTED(FATAL) << "Feature not implemented yet";
    UNREACHABLE();
  }

  ArrayRef<const XRegister> GetRegisters() override {
    static constexpr XRegister kXRegisters[] = {
        Zero,
        RA,
        TP,
        SP,
        A0,
        A1,
        A2,
        A3,
        A4,
        A5,
        A6,
        A7,
        T0,
        T1,
        T2,
        T3,
        T4,
        T5,
        T6,
        T7,
        T8,
        R21,
        FP,
        S0,
        S1,
        S2,
        S3,
        S4,
        S5,
        S6,
        S7,
        S8,
    };
    return ArrayRef<const XRegister>(kXRegisters);
}

ArrayRef<const FRegister> GetFPRegisters() override {
    static constexpr FRegister kFRegisters[] = {
        FA0,
        FA1,
        FA2,
        FA3,
        FA4,
        FA5,
        FA6,
        FA7,
        FT0,
        FT1,
        FT2,
        FT3,
        FT4,
        FT5,
        FT6,
        FT7,
        FT8,
        FT9,
        FT10,
        FT11,
        FT12,
        FT13,
        FT14,
        FT15,
        FS0,
        FS1,
        FS2,
        FS3,
        FS4,
        FS5,
        FS6,
        FS7,
    };
    return ArrayRef<const FRegister>(kFRegisters);
}

  std::string GetSecondaryRegisterName(const XRegister& reg) override {
    CHECK(secondary_register_names_.find(reg) != secondary_register_names_.end());
    return secondary_register_names_[reg];
  }

  int32_t CreateImmediate(int64_t imm_value) override { return dchecked_integral_cast<int32_t>(imm_value); }

  template <typename Emit>
  std::string RepeatInsn(size_t count, const std::string& insn, Emit&& emit) {
    std::string result;
    for (; count != 0u; --count) {
      result += insn;
      emit();
    }
    return result;
  }

  std::string EmitNops(size_t size) {
    DCHECK_ALIGNED(size, sizeof(uint32_t));
    const size_t num_nops = size / sizeof(uint32_t);
    return RepeatInsn(num_nops, "nop\n", [&]() { __ Nop(); });
  }

  template <typename EmitLoadConst>
  void TestLoadConst64(const std::string& test_name,
                       bool can_use_tmp,
                       EmitLoadConst&& emit_load_const) {
    std::string expected;
    // Test standard immediates. Unlike other instructions, `Li()` accepts an `int64_t` but
    // this is unsupported by `CreateImmediate()`, so we cannot use `RepeatRIb()` for these.
    // Note: This `CreateImmediateValuesBits()` call does not produce any values where
    // `LoadConst64()` would emit different code from `Li()`.
    for (int64_t value : CreateImmediateValuesBits(64, /*as_uint=*/ false)) {
      emit_load_const(A0, value);
      expected += "li.d $a0, " + std::to_string(value) + "\n";
    }
    // Test various registers with a few small values.
    // (Even Zero is an accepted register even if that does not really load the requested value.)
    for (XRegister reg : GetRegisters()) {
      if (can_use_tmp && reg == TMP) {
        continue;  // Not a valid target register.
      }
      std::string rd = GetRegisterName(reg);
      emit_load_const(reg, -1);
      expected += "li.d " + rd + ", -1\n";
      emit_load_const(reg, 0);
      expected += "li.d " + rd + ", 0\n";
      emit_load_const(reg, 1);
      expected += "li.d " + rd + ", 1\n";
    }
    // TODO
    //// Test some significant values. Some may just repeat the tests above but other values
    //// show some complex patterns, even exposing a value where clang (and therefore also this
    //// assembler) does not generate the shortest sequence.
    //// For the following values, `LoadConst64()` emits the same code as `Li()`.
    //int64_t test_values1[] = {
    //    // Small values, either ADDI, ADDI+SLLI, LUI, or LUI+ADDIW.
    //    // The ADDI+LUI is presumably used to allow shorter code for RV64C.
    //    -4097, -4096, -4095, -2176, -2049, -2048, -2047, -1025, -1024, -1023, -2, -1,
    //    0, 1, 2, 1023, 1024, 1025, 2047, 2048, 2049, 2176, 4095, 4096, 4097,
    //    // Just below std::numeric_limits<int32_t>::min()
    //    INT64_C(-0x80000001),  // LUI+ADDI
    //    INT64_C(-0x80000800),  // LUI+ADDI
    //    INT64_C(-0x80000801),  // LUI+ADDIW+SLLI+ADDI; LUI+ADDI+ADDI would be shorter.
    //    INT64_C(-0x80000800123),  // LUI+ADDIW+SLLI+ADDI
    //    INT64_C(0x0123450000000123),  // LUI+SLLI+ADDI
    //    INT64_C(-0x7654300000000123),  // LUI+SLLI+ADDI
    //    INT64_C(0x0fffffffffff0000),  // LUI+SRLI
    //    INT64_C(0x0ffffffffffff000),  // LUI+SRLI
    //    INT64_C(0x0ffffffffffff010),  // LUI+ADDIW+SRLI
    //    INT64_C(0x0fffffffffffff10),  // ADDI+SLLI+ADDI; LUI+ADDIW+SRLI would be same length.
    //    INT64_C(0x0fffffffffffff80),  // ADDI+SRLI
    //    INT64_C(0x0ffffffff7ffff80),  // LUI+ADDI+SRLI
    //    INT64_C(0x0123450000001235),  // LUI+SLLI+ADDI+SLLI+ADDI
    //    INT64_C(0x0123450000001234),  // LUI+SLLI+ADDI+SLLI
    //    INT64_C(0x0000000fff808010),  // LUI+SLLI+SRLI
    //    INT64_C(0x00000000fff80801),  // LUI+SLLI+SRLI
    //    INT64_C(0x00000000ffffffff),  // ADDI+SRLI
    //    INT64_C(0x00000001ffffffff),  // ADDI+SRLI
    //    INT64_C(0x00000003ffffffff),  // ADDI+SRLI
    //    INT64_C(0x00000000ffc00801),  // LUI+ADDIW+SLLI+ADDI
    //    INT64_C(0x00000001fffff7fe),  // ADDI+SLLI+SRLI
    //};
    //for (int64_t value : test_values1) {
    //  emit_load_const(A0, value);
    //  expected += "li.d $a0, " + std::to_string(value) + "\n";
    //}
    //// For the following values, `LoadConst64()` emits different code than `Li()`.
    //std::pair<int64_t, const char*> test_values2[] = {
    //    // Li:        LUI+ADDIW+SLLI+ADDI+SLLI+ADDI+SLLI+ADDI
    //    // LoadConst: LUI+ADDIW+LUI+ADDIW+SLLI+ADD (using TMP)
    //    { INT64_C(0x1234567812345678),
    //      "li.d {reg1}, 0x12345678 / 8\n"  // Trailing zero bits in high word are handled by SLLI.
    //      "li.d {reg2}, 0x12345678\n"
    //      "slli.d {reg1}, {reg1}, 32 + 3\n"
    //      "add.d {reg1}, {reg1}, {reg2}\n" },
    //    { INT64_C(0x1234567887654321),
    //      "li.d {reg1}, 0x12345678 + 1\n"  // One higher to compensate for negative TMP.
    //      "li.d {reg2}, 0x87654321 - 0x100000000\n"
    //      "slli.d {reg1}, {reg1}, 32\n"
    //      "add.d {reg1}, {reg1}, {reg2}\n" },
    //    { INT64_C(-0x1234567887654321),
    //      "li.d {reg1}, -0x12345678 - 1\n"  // High 32 bits of the constant.
    //      "li.d {reg2}, 0x100000000 - 0x87654321\n"  // Low 32 bits of the constant.
    //      "slli.d {reg1}, {reg1}, 32\n"
    //      "add.d {reg1}, {reg1}, {reg2}\n" },

    //    // Li:        LUI+SLLI+ADDI+SLLI+ADDI+SLLI
    //    // LoadConst: LUI+LUI+SLLI+ADD (using TMP)
    //    { INT64_C(0x1234500012345000),
    //      "lu12i.w {reg1}, 0x12345\n"
    //      "lu12i.w {reg2}, 0x12345\n"
    //      "slli.d {reg1}, {reg1}, 44 - 12\n"
    //      "add.d {reg1}, {reg1}, {reg2}\n" },
    //    { INT64_C(0x0123450012345000),
    //      "lu12i.w {reg1}, 0x12345\n"
    //      "lu12i.w {reg2}, 0x12345\n"
    //      "slli.d {reg1}, {reg1}, 40 - 12\n"
    //      "add.d {reg1}, {reg1}, {reg2}\n" },

    //    // Li:        LUI+ADDIW+SLLI+ADDI+SLLI+ADDI
    //    // LoadConst: LUI+LUI+ADDIW+SLLI+ADD (using TMP)
    //    { INT64_C(0x0001234512345678),
    //      "lu12i.w {reg1}, 0x12345\n"
    //      "li.d {reg2}, 0x12345678\n"
    //      "slli.d {reg1}, {reg1}, 32 - 12\n"
    //      "add.d {reg1}, {reg1}, {reg2}\n" },
    //    { INT64_C(0x0012345012345678),
    //      "lu12i.w {reg1}, 0x12345\n"
    //      "li.d {reg2}, 0x12345678\n"
    //      "slli.d {reg1}, {reg1}, 36 - 12\n"
    //      "add.d {reg1}, {reg1}, {reg2}\n" },
    //};
    //for (auto [value, fmt] : test_values2) {
    //  emit_load_const(A0, value);
    //  if (can_use_tmp) {
    //    std::string base = fmt;
    //    ReplaceReg(REG1_TOKEN, GetRegisterName(A0), &base);
    //    ReplaceReg(REG2_TOKEN, GetRegisterName(TMP), &base);
    //    expected += base;
    //  } else {
    //    expected += "li.d $a0, " + std::to_string(value) + "\n";
    //  }
    //}

    DriverStr(expected, test_name);
  }

  auto GetPrintBcond() {
    return [](const std::string& cond,
              [[maybe_unused]] const std::string& opposite_cond,
              const std::string& args,
              const std::string& target) {
      return "b" + cond + args + ", " + target + "\n";
    };
  }

  auto GetPrintBcondOppositeAndB(const std::string& skip_label) {
    return [=]([[maybe_unused]] const std::string& cond,
               const std::string& opposite_cond,
               const std::string& args,
               const std::string& target) {
      return "b" + opposite_cond + args + ", " + skip_label + "f\n" +
             "b " + target + "\n" +
             skip_label + ":\n";
    };
  }

  auto GetPrintBcondOppositeAndTail(const std::string& skip_label, const std::string& base_label) {
    return [=]([[maybe_unused]] const std::string& cond,
               const std::string& opposite_cond,
               const std::string& args,
               const std::string& target) {
      return "b" +opposite_cond + args + ", " + skip_label + "f\n" +
             base_label + ":\n" +
             "pcaddu18i $r21, %call36(" + target +")\n" +
             "jirl $r0, $r21, 0\n" +
             skip_label + ":\n";
    };
  }

  // Helper function for basic tests that all branch conditions map to the correct opcodes,
  // whether with branch expansion (a conditional branch with opposite condition over an
  // unconditional branch) or without.
  template <typename PrintBcond>
  std::string EmitBcondForAllConditions(Loongarch64Label* label,
                                        const std::string& target,
                                        PrintBcond&& print_bcond) {
    XRegister rs = A0;
    __ Beqz(rs, label);
    __ Bnez(rs, label);
    XRegister rt = A1;
    __ Beq(rs, rt, label);
    __ Bne(rs, rt, label);
    __ Bge(rs, rt, label);
    __ Blt(rs, rt, label);
    __ Bgeu(rs, rt, label);
    __ Bltu(rs, rt, label);

    return
        print_bcond("eq", "ne", "z $a0", target) +
        print_bcond("ne", "eq", "z $a0", target) +
        print_bcond("eq", "ne", " $a0, $a1", target) +
        print_bcond("ne", "eq", " $a0, $a1", target) +
        print_bcond("ge", "lt", " $a0, $a1", target) +
        print_bcond("lt", "ge", " $a0, $a1", target) +
        print_bcond("geu", "ltu", " $a0, $a1", target) +
        print_bcond("ltu", "geu", " $a0, $a1", target);
  }

  // Test Bcond for forward branches with all conditions.
  // The gap must be such that either all branches expand, or none does.
  template <typename PrintBcond>
  void TestBcondForward(const std::string& test_name,
                        size_t gap_size,
                        const std::string& target_label,
                        PrintBcond&& print_bcond) {
    std::string expected;
    Loongarch64Label label;
    expected += EmitBcondForAllConditions(&label, target_label + "f", print_bcond);
    expected += EmitNops(gap_size);
    __ Bind(&label);
    expected += target_label + ":\n";
    DriverStr(expected, test_name);
  }

  // Test Bcond for backward branches with all conditions.
  // The gap must be such that either all branches expand, or none does.
  template <typename PrintBcond>
  void TestBcondBackward(const std::string& test_name,
                         size_t gap_size,
                         const std::string& target_label,
                         PrintBcond&& print_bcond) {
    std::string expected;
    Loongarch64Label label;
    __ Bind(&label);
    expected += target_label + ":\n";
    expected += EmitNops(gap_size);
    expected += EmitBcondForAllConditions(&label, target_label + "b", print_bcond);
    DriverStr(expected, test_name);
  }

  size_t MaxOffset18BackwardDistance() {
    return 128 * KB;
  }

  size_t MaxOffset18ForwardDistance() {
    return 128 * KB - 4;
  }

  size_t MaxOffset23BackwardDistance() {
    return 4 * MB;
  }

  size_t MaxOffset23ForwardDistance() {
    return 4 * MB - 4;
  }

  template <typename PrintBcond>
  void TestBeqA0A1Forward(const std::string& test_name,
                          size_t nops_size,
                          const std::string& target_label,
                          PrintBcond&& print_bcond) {
    std::string expected;
    Loongarch64Label label;
    __ Beq(A0, A1, &label);
    expected += print_bcond("eq", "ne", " $a0, $a1", target_label + "f");
    expected += EmitNops(nops_size);
    __ Bind(&label);
    expected += target_label + ":\n";
    DriverStr(expected, test_name);
  }

  template <typename PrintBcond>
  void TestBeqA0A1Backward(const std::string& test_name,
                           size_t nops_size,
                           const std::string& target_label,
                           PrintBcond&& print_bcond) {
    std::string expected;
    Loongarch64Label label;
    __ Bind(&label);
    expected += target_label + ":\n";
    expected += EmitNops(nops_size);
    __ Beq(A0, A1, &label);
    expected += print_bcond("eq", "ne", " $a0, $a1", target_label + "b");
    DriverStr(expected, test_name);
  }

  // Test a branch setup where expanding one branch causes expanding another branch
  // which causes expanding another branch, etc. The argument `cascade` determines
  // whether we push the first branch to expand, or not.
  template <typename PrintBcond>
  void TestBeqA0A1MaybeCascade(const std::string& test_name,
                               bool cascade,
                               PrintBcond&& print_bcond) {
    const size_t kNumBeqs = MaxOffset18ForwardDistance() / sizeof(uint32_t) / 2u;
    auto label_name = [](size_t i) { return  ".L" + std::to_string(i); };

    std::string expected;
    std::vector<Loongarch64Label> labels(kNumBeqs);
    for (size_t i = 0; i != kNumBeqs; ++i) {
      __ Beq(A0, A1, &labels[i]);
      expected += print_bcond("eq", "ne", " $a0, $a1", label_name(i));
    }
    if (cascade) {
      expected += EmitNops(sizeof(uint32_t));
    }
    for (size_t i = 0; i != kNumBeqs; ++i) {
      expected += EmitNops(2 * sizeof(uint32_t));
      __ Bind(&labels[i]);
      expected += label_name(i) + ":\n";
    }
    DriverStr(expected, test_name);
  }

  auto GetPrintBlRd() {
    return [=](const std::string& target) {
      return "bl " + target + "\n";
    };
  }

  template <typename PrintJrRd>
  void TestBlRdForward(const std::string& test_name,
                        size_t gap_size,
                        const std::string& label_name,
                        PrintJrRd&& print_jrrd) {
    std::string expected;
    Loongarch64Label label;
    __ Bl(&label);
    expected += print_jrrd(label_name + "f");
    expected += EmitNops(gap_size);
    __ Bind(&label);
    expected += label_name + ":\n";
    DriverStr(expected, test_name);
  }

  template <typename PrintJrRd>
  void TestBlRdBackward(const std::string& test_name,
                         size_t gap_size,
                         const std::string& label_name,
                         PrintJrRd&& print_jrrd) {
    std::string expected;
    Loongarch64Label label;
    __ Bind(&label);
    expected += label_name + ":\n";
    expected += EmitNops(gap_size);
    __ Bl(&label);
    expected += print_jrrd(label_name + "b");
    DriverStr(expected, test_name);
  }

  auto GetEmitB() {
    return [=](Loongarch64Label* label) { __ B(label); };
  }

  auto GetEmitBl() {
    return [=](Loongarch64Label* label) { __ Bl(label); };
  }

  auto GetPrintB() {
    return [=](const std::string& target) {
      return "b " + target + "\n";
    };
  }

  auto GetPrintBl() {
    return [=](const std::string& target) {
      return "bl " + target + "\n";
    };
  }

  template <typename EmitBuncond, typename PrintBuncond>
  void TestBuncondForward(const std::string& test_name,
                          size_t gap_size,
                          const std::string& label_name,
                          EmitBuncond&& emit_buncond,
                          PrintBuncond&& print_buncond) {
    std::string expected;
    Loongarch64Label label;
    emit_buncond(&label);
    expected += print_buncond(label_name + "f");
    expected += EmitNops(gap_size);
    __ Bind(&label);
    expected += label_name + ":\n";
    DriverStr(expected, test_name);
  }

  template <typename EmitBuncond, typename PrintBuncond>
  void TestBuncondBackward(const std::string& test_name,
                           size_t gap_size,
                           const std::string& label_name,
                           EmitBuncond&& emit_buncond,
                           PrintBuncond&& print_buncond) {
    std::string expected;
    Loongarch64Label label;
    __ Bind(&label);
    expected += label_name + ":\n";
    expected += EmitNops(gap_size);
    emit_buncond(&label);
    expected += print_buncond(label_name + "b");
    DriverStr(expected, test_name);
  }

  template <typename EmitOp>
  void TestAddConst(const std::string& test_name,
                    size_t bits,
                    const std::string& suffix,
                    EmitOp&& emit_op) {
    int64_t kImm12s[] = {
        0, 1, 2, 0xff, 0x100, 0x1ff, 0x200, 0x3ff, 0x400, 0x7ff,
        -1, -2, -0x100, -0x101, -0x200, -0x201, -0x400, -0x401, -0x800,
    };
    int64_t kSimplePositiveValues[] = {
        0x800, 0x801, 0xbff, 0xc00, 0xff0, 0xff7, 0xff8, 0xffb, 0xffc, 0xffd, 0xffe,
    };
    int64_t kSimpleNegativeValues[] = {
        -0x801, -0x802, -0xbff, -0xc00, -0xff0, -0xff8, -0xffc, -0xffe, -0xfff, -0x1000,
    };
    std::vector<int64_t> large_values = CreateImmediateValuesBits(bits, /*as_uint=*/ false);
    auto kept_end = std::remove_if(large_values.begin(),
                                   large_values.end(),
                                   [](int64_t value) { return IsInt<13>(value); });
    large_values.erase(kept_end, large_values.end());
    large_values.push_back(0xfff);

    std::string tmp_name = GetRegisterName(TMP);

    std::string expected;
    for (XRegister rd : GetRegisters()) {
      std::string rd_name = GetRegisterName(rd);
      std::string addi_rd = "addi." + suffix + " " + rd_name + ", ";
      std::string add_rd = "add." + suffix + " " + rd_name + ", ";
      for (XRegister rs1 : GetRegisters()) {
        // TMP can be the destination register but not the source register.
        if (rs1 == TMP) {
          continue;
        }
        std::string rs1_name = GetRegisterName(rs1);

        for (int64_t imm : kImm12s) {
          emit_op(rd, rs1, imm);
          expected += addi_rd + rs1_name + ", " + std::to_string(imm) + "\n";
        }

        auto emit_simple_ops = [&](ArrayRef<const int64_t> imms, int64_t adjustment) {
          for (int64_t imm : imms) {
            emit_op(rd, rs1, imm);
            expected += addi_rd + rs1_name + ", " + std::to_string(adjustment) + "\n" +
                        addi_rd + rd_name + ", " + std::to_string(imm - adjustment) + "\n";
          }
        };
        emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveValues), 0x7ff);
        emit_simple_ops(ArrayRef<const int64_t>(kSimpleNegativeValues), -0x800);

        for (int64_t imm : large_values) {
          emit_op(rd, rs1, imm);
          expected += "li.d " + tmp_name + ", " + std::to_string(imm) + "\n" +
                      add_rd + rs1_name + ", " + tmp_name + "\n";
        }
      }
    }
    DriverStr(expected, test_name);
  }

  template <typename EmitOp>
  std::string RepeatLoadStoreArbitraryOffset(const std::string& head, EmitOp&& emit_op) {
    int64_t kImm12s[] = {
        0, 1, 2, 0xff, 0x100, 0x1ff, 0x200, 0x3ff, 0x400, 0x7ff,
        -1, -2, -0x100, -0x101, -0x200, -0x201, -0x400, -0x401, -0x800,
    };
    int64_t kSimplePositiveOffsetsAlign8[] = {
        0x800, 0x801, 0xbff, 0xc00, 0xff0, 0xff4, 0xff6, 0xff7
    };
    int64_t kSimplePositiveOffsetsAlign4[] = {
        0xff8, 0xff9, 0xffa, 0xffb
    };
    int64_t kSimplePositiveOffsetsAlign2[] = {
        0xffc, 0xffd
    };
    int64_t kSimplePositiveOffsetsNoAlign[] = {
        0xffe
    };
    int64_t kSimpleNegativeOffsets[] = {
        -0x801, -0x802, -0xbff, -0xc00, -0xff0, -0xff8, -0xffc, -0xffe, -0xfff, -0x1000,
    };
    //int64_t kSplitOffsets[] = {
    //    0xfff, 0x1000, 0x1001, 0x17ff, 0x1800, 0x1fff, 0x2000, 0x2001, 0x27ff, 0x2800,
    //    0x7fffe7ff, 0x7fffe800, 0x7fffefff, 0x7ffff000, 0x7ffff001, 0x7ffff7ff,
    //    -0x1001, -0x1002, -0x17ff, -0x1800, -0x1801, -0x2000, -0x2001, -0x2800, -0x2801,
    //    -0x7ffff000, -0x7ffff001, -0x7ffff800, -0x7ffff801, -0x7fffffff, -0x80000000,
    //};
    //int64_t kSpecialOffsets[] = {
    //    0x7ffff800, 0x7ffff801, 0x7ffffffe, 0x7fffffff
    //};

    std::string tmp_name = GetRegisterName(TMP);
    std::string expected;
    for (XRegister rs1 : GetRegisters()) {
      if (rs1 == TMP) {
        continue;  // TMP cannot be the address base register.
      }
      std::string rs1_name = GetRegisterName(rs1);

      for (int64_t imm : kImm12s) {
        emit_op(rs1, imm);
        expected += head + ", " + rs1_name + ", " + std::to_string(imm) + "\n";
      }

      auto emit_simple_ops = [&](ArrayRef<const int64_t> imms, int64_t adjustment) {
        for (int64_t imm : imms) {
          emit_op(rs1, imm);
          expected +=
              "addi.d " + tmp_name + ", " + rs1_name + ", " + std::to_string(adjustment) + "\n" +
              head + ", " + tmp_name + ", " + std::to_string(imm - adjustment) + "\n";
        }
      };
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign8), 0x7f8);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign4), 0x7fc);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsAlign2), 0x7fe);
      emit_simple_ops(ArrayRef<const int64_t>(kSimplePositiveOffsetsNoAlign), 0x7ff);
      emit_simple_ops(ArrayRef<const int64_t>(kSimpleNegativeOffsets), -0x800);

      //for (int64_t imm : kSplitOffsets) {
      //  emit_op(rs1, imm);
      //  uint32_t imm20 = ((imm >> 12) + ((imm >> 11) & 1)) & 0xfffff;
      //  int32_t small_offset = (imm & 0xfff) - ((imm & 0x800) << 1);
      //  expected += "lu12i.w " + tmp_name + ", " + std::to_string(imm20) + "\n"
      //              "add.d " + tmp_name + ", " + tmp_name + ", " + rs1_name + "\n" +
      //              head + ", " + tmp_name + ", " + std::to_string(small_offset) +  "\n";
      //}

      //for (int64_t imm : kSpecialOffsets) {
      //  emit_op(rs1, imm);
      //  expected +=
      //      "lu12i.w " + tmp_name + ", %abs_hi20(0x80000)\n"
      //      "addi.w " + tmp_name + ", " + tmp_name + ", " + std::to_string(imm - 0x80000000) + "\n" +
      //      "add.d " + tmp_name + ", " + tmp_name + ", " + rs1_name + "\n" +
      //      head + ", " + tmp_name + ", 0" + "\n";
      //}
    }
    return expected;
  }

  void TestLoadStoreArbitraryOffset(const std::string& test_name,
                                    const std::string& insn,
                                    void (Loongarch64Assembler::*fn)(XRegister, XRegister, int32_t),
                                    bool is_store) {
    std::string expected;
    for (XRegister rd : GetRegisters()) {
      // TMP can be the target register for loads but not for stores where loading the
      // adjusted address to TMP would clobber the value we want to store.
      if (is_store && rd == TMP) {
        continue;
      }
      if(is_store) {};
      expected += RepeatLoadStoreArbitraryOffset(
          insn + " " + GetRegisterName(rd),
          [&](XRegister rs1, int64_t offset) { (GetAssembler()->*fn)(rd, rs1, offset); });
    }
    DriverStr(expected, test_name);
  }

  void TestLoadLiteral(const std::string& test_name, bool with_padding_for_long) {
    std::string expected;
    Literal* narrow_literal = __ NewLiteral<uint32_t>(0x12345678);
    Literal* wide_literal = __ NewLiteral<uint64_t>(0x1234567887654321);
    auto print_load = [&](const std::string& load, XRegister rd, const std::string& label) {
      std::string rd_name = GetRegisterName(rd);
      expected += "1:\n"
                  "pcalau12i " + rd_name + ", %pc_hi20(" + label + "f)\n" +
                  load + " " + rd_name + ", " + rd_name + ", %pc_lo12(" + label + "f)\n";
    };
    for (XRegister reg : GetRegisters()) {
      if (reg != Zero) {
        __ Load_W(reg, narrow_literal);
        print_load("ld.w", reg, "2");
        __ Load_WU(reg, narrow_literal);
        print_load("ld.wu", reg, "2");
        __ Load_D(reg, wide_literal);
        print_load("ld.d", reg, "3");
      }
    }
    // All literal loads above emit 8 bytes of code. The narrow literal shall emit 4 bytes of code.
    // If we do not add another instruction, we shall end up with padding before the long literal.
    expected += EmitNops(with_padding_for_long ? 0u : sizeof(uint32_t));
    expected += "2:\n"
                ".4byte 0x12345678\n" +
                std::string(with_padding_for_long ? ".4byte 0\n" : "") +
                "3:\n"
                ".8byte 0x1234567887654321\n";
    DriverStr(expected, test_name);
  }








 private:
  std::vector<XRegister*> registers_;
  std::map<XRegister, std::string, LOONGARCH64CpuRegisterCompare> secondary_register_names_;
  
  std::vector<FRegister*> fp_registers_;
  std::unique_ptr<const Loongarch64InstructionSetFeatures> instruction_set_features_;
};

TEST_F(AssemblerLOONGARCH64Test, Toolchain) { EXPECT_TRUE(CheckTools()); }


///////////////////////////// LOONGARCH64 Transfer Instructions ///////////////////////////////
TEST_F(AssemblerLOONGARCH64Test, Beqz) {
  DriverStr(RepeatRIbS(&Loongarch64Assembler::Beqz, -19, 2,"beqz {reg}, {imm}"), "Beqz");// 19 for address aligned
}

TEST_F(AssemblerLOONGARCH64Test, Bnez) {
  DriverStr(RepeatRIbS(&Loongarch64Assembler::Bnez, -19, 2,"bnez {reg}, {imm}"), "Bnez");
}

TEST_F(AssemblerLOONGARCH64Test, Jirl) {
  DriverStr(RepeatRRIbS(&Loongarch64Assembler::Jirl, -14, 2, "jirl {reg1}, {reg2}, {imm}"), "Jirl");
}

TEST_F(AssemblerLOONGARCH64Test, B) {
  DriverStr(RepeatIb(&Loongarch64Assembler::B, -5, 2, "b {imm}"), "B");
}

TEST_F(AssemblerLOONGARCH64Test, Bl) {
  DriverStr(RepeatIb(&Loongarch64Assembler::Bl, -5, 2, "bl {imm}"), "Bl");
}

TEST_F(AssemblerLOONGARCH64Test, Beq) {
  DriverStr(RepeatRRIbS(&Loongarch64Assembler::Beq, -14, 2, "beq {reg1}, {reg2}, {imm}"), "Beq");
}

TEST_F(AssemblerLOONGARCH64Test, Bne) {
  DriverStr(RepeatRRIbS(&Loongarch64Assembler::Bne, -14, 2, "bne {reg1}, {reg2}, {imm}"), "Bne");
}

TEST_F(AssemblerLOONGARCH64Test, Blt) {
  DriverStr(RepeatRRIbS(&Loongarch64Assembler::Blt, -14, 2, "blt {reg1}, {reg2}, {imm}"), "Blt");
}

TEST_F(AssemblerLOONGARCH64Test, Bge) {
  DriverStr(RepeatRRIbS(&Loongarch64Assembler::Bge, -14, 2, "bge {reg1}, {reg2}, {imm}"), "Bge");
}

TEST_F(AssemblerLOONGARCH64Test, Bltu) {
  DriverStr(RepeatRRIbS(&Loongarch64Assembler::Bltu, -14, 2, "bltu {reg1}, {reg2}, {imm}"), "Bltu");
}

TEST_F(AssemblerLOONGARCH64Test, Bgeu) {
  DriverStr(RepeatRRIbS(&Loongarch64Assembler::Bgeu, -14, 2, "bgeu {reg1}, {reg2}, {imm}"), "Bgeu");
}




/////////////////////////////// LOONGARCH64 "2R1I-Type" Instructions ///////////////////////////////


TEST_F(AssemblerLOONGARCH64Test, Ld_B) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Ld_B, -12, "ld.b {reg1}, {reg2}, {imm}"), "Ld_B");// imm_bits means the bits of imm here, - means it is signed.
}

TEST_F(AssemblerLOONGARCH64Test, Ld_H) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Ld_H, -12, "ld.h {reg1}, {reg2}, {imm}"), "Ld_H");
}

TEST_F(AssemblerLOONGARCH64Test, Ld_W) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Ld_W, -12, "ld.w {reg1}, {reg2}, {imm}"), "Ld_W");
}

TEST_F(AssemblerLOONGARCH64Test, Ld_D) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Ld_D, -12, "ld.d {reg1}, {reg2}, {imm}"), "Ld_D");
}

TEST_F(AssemblerLOONGARCH64Test, Ld_BU) { // Why here imm_bits could be negative?
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Ld_BU, -12, "ld.bu {reg1}, {reg2}, {imm}"), "Ld_BU");
}

TEST_F(AssemblerLOONGARCH64Test, Ld_HU) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Ld_HU, -12, "ld.hu {reg1}, {reg2}, {imm}"), "Ld_HU");
}

TEST_F(AssemblerLOONGARCH64Test, Ld_WU) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Ld_WU, -12, "ld.wu {reg1}, {reg2}, {imm}"), "Ld_WU");
}

TEST_F(AssemblerLOONGARCH64Test, St_B) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::St_B, -12, "st.b {reg1}, {reg2}, {imm}"), "St_B");
}

TEST_F(AssemblerLOONGARCH64Test, St_H) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::St_H, -12, "st.h {reg1}, {reg2}, {imm}"), "St_H");
}

TEST_F(AssemblerLOONGARCH64Test, St_W) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::St_W, -12, "st.w {reg1}, {reg2}, {imm}"), "St_W");
}

TEST_F(AssemblerLOONGARCH64Test, St_D) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::St_D, -12, "st.d {reg1}, {reg2}, {imm}"), "St_D");
}

TEST_F(AssemblerLOONGARCH64Test, Slti) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Slti, -12, "slti {reg1}, {reg2}, {imm}"), "Slti");
}

TEST_F(AssemblerLOONGARCH64Test, Sltui) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Sltui, -12, "sltui {reg1}, {reg2}, {imm}"), "Sltui");
}

TEST_F(AssemblerLOONGARCH64Test, Addi_W) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Addi_W, -12, "addi.w {reg1}, {reg2}, {imm}"), "Addi_W");
}

TEST_F(AssemblerLOONGARCH64Test, Addi_D) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Addi_D, -12, "addi.d {reg1}, {reg2}, {imm}"), "Addi_D");
}

TEST_F(AssemblerLOONGARCH64Test, Lu52i_d) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Lu52i_D, -12, "lu52i.d {reg1}, {reg2}, {imm}"), "Lu52i_d");
}

TEST_F(AssemblerLOONGARCH64Test, Andi) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Andi, 12, "andi {reg1}, {reg2}, {imm}"), "Andi");
}

TEST_F(AssemblerLOONGARCH64Test, Ori) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Ori, 12, "ori {reg1}, {reg2}, {imm}"), "Ori");
}

TEST_F(AssemblerLOONGARCH64Test, Xori) {
  DriverStr(RepeatRRIb(&Loongarch64Assembler::Xori, 12, "xori {reg1}, {reg2}, {imm}"), "Xori");
}

/////////////////////////////// LOONGARCH64 "3R-Type" Instructions ///////////////////////////////


TEST_F(AssemblerLOONGARCH64Test, Add_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Add_w, "add.w {reg1}, {reg2}, {reg3}"), "Add_w");
}

TEST_F(AssemblerLOONGARCH64Test, Add_d) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Add_d, "add.d {reg1}, {reg2}, {reg3}"), "Add_d");
}

TEST_F(AssemblerLOONGARCH64Test, Sub_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Sub_w, "sub.w {reg1}, {reg2}, {reg3}"), "Sub_w");
}

TEST_F(AssemblerLOONGARCH64Test, Slt) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Slt, "slt {reg1}, {reg2}, {reg3}"), "Slt");
}

TEST_F(AssemblerLOONGARCH64Test, Sltu) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Sltu, "sltu {reg1}, {reg2}, {reg3}"), "Sltu");
}

TEST_F(AssemblerLOONGARCH64Test, Maskeqz) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Maskeqz, "maskeqz {reg1}, {reg2}, {reg3}"), "Maskeqz");
}

TEST_F(AssemblerLOONGARCH64Test, Masknez) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Masknez, "masknez {reg1}, {reg2}, {reg3}"), "Masknez");
}

TEST_F(AssemblerLOONGARCH64Test, Nor) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Nor, "nor {reg1}, {reg2}, {reg3}"), "Nor");
}

TEST_F(AssemblerLOONGARCH64Test, And) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::And, "and {reg1}, {reg2}, {reg3}"), "And");
}

TEST_F(AssemblerLOONGARCH64Test, Or) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Or, "or {reg1}, {reg2}, {reg3}"), "Or");
}

TEST_F(AssemblerLOONGARCH64Test, Xor) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Xor, "xor {reg1}, {reg2}, {reg3}"), "Xor");
}

TEST_F(AssemblerLOONGARCH64Test, Orn) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Orn, "orn {reg1}, {reg2}, {reg3}"), "Orn");
}

TEST_F(AssemblerLOONGARCH64Test, Andn) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Andn, "andn {reg1}, {reg2}, {reg3}"), "Andn");
}

TEST_F(AssemblerLOONGARCH64Test, Sll_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Sll_w, "sll.w {reg1}, {reg2}, {reg3}"), "Sll.w");
}

TEST_F(AssemblerLOONGARCH64Test, Srl_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Srl_w, "srl.w {reg1}, {reg2}, {reg3}"), "Srl_w");
}

TEST_F(AssemblerLOONGARCH64Test, Sra_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Sra_w, "sra.w {reg1}, {reg2}, {reg3}"), "Sra_w");
}

TEST_F(AssemblerLOONGARCH64Test, Sll_d) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Sll_d, "sll.d {reg1}, {reg2}, {reg3}"), "Sll_d");
}

TEST_F(AssemblerLOONGARCH64Test, Srl_d) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Srl_d, "srl.d {reg1}, {reg2}, {reg3}"), "Srl_d");
}

TEST_F(AssemblerLOONGARCH64Test, Sra_d) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Sra_d, "sra.d {reg1}, {reg2}, {reg3}"), "Sra_d");
}
// mid-level ALU instructions
TEST_F(AssemblerLOONGARCH64Test, Mul_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mul_w, "mul.w {reg1}, {reg2}, {reg3}"), "Mul_w");
}

TEST_F(AssemblerLOONGARCH64Test, Mulh_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mulh_w, "mulh.w {reg1}, {reg2}, {reg3}"), "Mulh_w");
}

TEST_F(AssemblerLOONGARCH64Test, Mulh_wu) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mulh_wu, "mulh.wu {reg1}, {reg2}, {reg3}"), "Mulh_wu");
}

TEST_F(AssemblerLOONGARCH64Test, Mul_d) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mul_d, "mul.d {reg1}, {reg2}, {reg3}"), "Mul_d");
}

TEST_F(AssemblerLOONGARCH64Test, Mulh_d) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mulh_d, "mulh.d {reg1}, {reg2}, {reg3}"), "Mulh_d");
}

TEST_F(AssemblerLOONGARCH64Test, Mulh_du) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mulh_du, "mulh.du {reg1}, {reg2}, {reg3}"), "Mulh_du");
}

TEST_F(AssemblerLOONGARCH64Test, Mulw_d_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mulw_d_w, "mulw.d.w {reg1}, {reg2}, {reg3}"), "Mulw_d_w");
}

TEST_F(AssemblerLOONGARCH64Test, Mulw_d_wu) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mulw_d_wu, "mulw.d.wu {reg1}, {reg2}, {reg3}"), "Mulw_d_wu");
}

TEST_F(AssemblerLOONGARCH64Test, Div_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Div_w, "div.w {reg1}, {reg2}, {reg3}"), "Div_w");
}

TEST_F(AssemblerLOONGARCH64Test, Mod_w) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mod_w, "mod.w {reg1}, {reg2}, {reg3}"), "Mod_w");
}

TEST_F(AssemblerLOONGARCH64Test, Div_wu) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Div_wu, "div.wu {reg1}, {reg2}, {reg3}"), "Div_wu");
}

TEST_F(AssemblerLOONGARCH64Test, Mod_wu) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mod_wu, "mod.wu {reg1}, {reg2}, {reg3}"), "Mod_wu");
}

TEST_F(AssemblerLOONGARCH64Test, Div_d) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Div_d, "div.d {reg1}, {reg2}, {reg3}"), "Div_d");
}

TEST_F(AssemblerLOONGARCH64Test, Mod_d) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mod_d, "mod.d {reg1}, {reg2}, {reg3}"), "Mod_d");
}

TEST_F(AssemblerLOONGARCH64Test, Div_du) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Div_du, "div.du {reg1}, {reg2}, {reg3}"), "Div_du");
}

TEST_F(AssemblerLOONGARCH64Test, Mod_du) {
  DriverStr(RepeatRRR(&Loongarch64Assembler::Mod_du, "mod.du {reg1}, {reg2}, {reg3}"), "Mod_du");
}


TEST_F(AssemblerLOONGARCH64Test, LoadConst32) {
  // `LoadConst32()` emits the same code sequences as `Li()` for 32-bit values.
  DriverStr(RepeatRIb(&Loongarch64Assembler::LoadConst32, -32, "li.w {reg}, {imm}"), "LoadConst32");
}

TEST_F(AssemblerLOONGARCH64Test, LoadConst64) {
  TestLoadConst64("LoadConst64",
                  /*can_use_tmp=*/ false,
                  [&](XRegister rd, int64_t value) { __ LoadConst64(rd, value); });
}

TEST_F(AssemblerLOONGARCH64Test, AddConst32) {
  auto emit_op = [&](XRegister rd, XRegister rs1, int64_t value) {
    __ AddConst32(rd, rs1, dchecked_integral_cast<int32_t>(value));
  };
  TestAddConst("AddConst32", 32, /*suffix=*/ "w", emit_op);
}

TEST_F(AssemblerLOONGARCH64Test, AddConst64) {
  auto emit_op = [&](XRegister rd, XRegister rs1, int64_t value) {
    __ AddConst64(rd, rs1, value);
  };
  TestAddConst("AddConst64", 64, /*suffix=*/ "d", emit_op);
}

TEST_F(AssemblerLOONGARCH64Test, Load_B) {
  TestLoadStoreArbitraryOffset("Load_B", "ld.b", &Loongarch64Assembler::Load_B, /*is_store=*/ false);
}

TEST_F(AssemblerLOONGARCH64Test, Load_H) {
  TestLoadStoreArbitraryOffset("Load_H", "ld.h", &Loongarch64Assembler::Load_H, /*is_store=*/ false);
}

TEST_F(AssemblerLOONGARCH64Test, Load_W) {
  TestLoadStoreArbitraryOffset("Load_W", "ld.w", &Loongarch64Assembler::Load_W, /*is_store=*/ false);
}

TEST_F(AssemblerLOONGARCH64Test, Load_D) {
  TestLoadStoreArbitraryOffset("Load_D", "ld.d", &Loongarch64Assembler::Load_D, /*is_store=*/ false);
}

TEST_F(AssemblerLOONGARCH64Test, Load_BU) {
  TestLoadStoreArbitraryOffset("Load_BU", "ld.bu", &Loongarch64Assembler::Load_BU, /*is_store=*/ false);
}

TEST_F(AssemblerLOONGARCH64Test, Load_HU) {
  TestLoadStoreArbitraryOffset("Load_HU", "ld.hu", &Loongarch64Assembler::Load_HU, /*is_store=*/ false);
}

TEST_F(AssemblerLOONGARCH64Test, Load_WU) {
  TestLoadStoreArbitraryOffset("Load_WU", "ld.wu", &Loongarch64Assembler::Load_WU, /*is_store=*/ false);
}

TEST_F(AssemblerLOONGARCH64Test, Store_B) {
  TestLoadStoreArbitraryOffset("Store_B", "st.b", &Loongarch64Assembler::Store_B, /*is_store=*/ true);
}

TEST_F(AssemblerLOONGARCH64Test, Store_H) {
  TestLoadStoreArbitraryOffset("Store_H", "st.h", &Loongarch64Assembler::Store_H, /*is_store=*/ true);
}

TEST_F(AssemblerLOONGARCH64Test, Store_W) {
  TestLoadStoreArbitraryOffset("Store_W", "st.w", &Loongarch64Assembler::Store_W, /*is_store=*/ true);
}

TEST_F(AssemblerLOONGARCH64Test, Store_D) {
  TestLoadStoreArbitraryOffset("Store_D", "st.d", &Loongarch64Assembler::Store_D, /*is_store=*/ true);
}
// Branch test
// +- [0 ~ 128k) use beq/.../b to jump
// +- [128K ~ 4M) use b/bl to jump
// +- [4M ~ 128M) use pcaddu18i+jirl to jump
// > 128M use combined instructions to jump
TEST_F(AssemblerLOONGARCH64Test, BcondForward127KiB) {
  TestBcondForward("BcondForward127KiB", 127 * KB, "1", GetPrintBcond());
}

TEST_F(AssemblerLOONGARCH64Test, BcondBackward127KiB) {
  TestBcondBackward("BcondBackward127KiB", 127 * KB, "1", GetPrintBcond());
}

TEST_F(AssemblerLOONGARCH64Test, BcondForward129KB) {
  TestBcondForward("BcondForward3MiB", 129 * KB, "1", GetPrintBcondOppositeAndB("2"));
}

TEST_F(AssemblerLOONGARCH64Test, BcondBackward129KB) {
  TestBcondBackward("BcondBackward3MiB", 129 * KB, "1", GetPrintBcondOppositeAndB("2"));
}

TEST_F(AssemblerLOONGARCH64Test, BeqA0A1MaxOffset18Forward) {
  TestBeqA0A1Forward("BeqA0A1MaxOffset18Forward",
                     MaxOffset18ForwardDistance() - /*BEQ*/ 4u,
                     "1",
                     GetPrintBcond());
}

TEST_F(AssemblerLOONGARCH64Test, BeqA0A1MaxOffset18Backward) {
  TestBeqA0A1Backward("BeqA0A1MaxOffset18Forward",
                      MaxOffset18BackwardDistance(),
                      "1",
                      GetPrintBcond());
}

TEST_F(AssemblerLOONGARCH64Test, BeqA0A1OverMaxOffset18Forward) {
  TestBeqA0A1Forward("BeqA0A1OverMaxOffset18Forward",
                     MaxOffset18ForwardDistance() - /*BEQ*/ 4u + /*Exceed max*/ 4u,
                     "1",
                     GetPrintBcondOppositeAndB("2"));
}

TEST_F(AssemblerLOONGARCH64Test, BeqA0A1OverMaxOffset18Backward) {
  TestBeqA0A1Backward("BeqA0A1OverMaxOffset18Forward",
                      MaxOffset18BackwardDistance() + /*Exceed max*/ 4u,
                      "1",
                      GetPrintBcondOppositeAndB("2"));
}

TEST_F(AssemblerLOONGARCH64Test, BeqA0A1MaxOffset23Forward) {
  TestBeqA0A1Forward("BeqA0A1MaxOffset23Forward",
                     MaxOffset23ForwardDistance() - /*Beqz*/ 4u,
                     "1",
                     GetPrintBcondOppositeAndB("2"));
}

TEST_F(AssemblerLOONGARCH64Test, BeqA0A1MaxOffset23Backward) {
  TestBeqA0A1Backward("BeqA0A1MaxOffset23Backward",
                      MaxOffset23BackwardDistance() - /*BNE*/ 4u,
                      "1",
                      GetPrintBcondOppositeAndB("2"));
}

TEST_F(AssemblerLOONGARCH64Test, BeqA0A1AlmostCascade) {
  TestBeqA0A1MaybeCascade("BeqA0A1AlmostCascade", /*cascade=*/ false, GetPrintBcond());
}

TEST_F(AssemblerLOONGARCH64Test, BeqA0A1Cascade) {
  TestBeqA0A1MaybeCascade(
      "BeqA0A1AlmostCascade", /*cascade=*/ true, GetPrintBcondOppositeAndB("1"));
}

TEST_F(AssemblerLOONGARCH64Test, BcondElimination) {
  Loongarch64Label label;
  __ Bind(&label);
  __ Nop();
  for (XRegister reg : GetRegisters()) {
    __ Bne(reg, reg, &label);
    __ Blt(reg, reg, &label);
    __ Bltu(reg, reg, &label);
  }
  DriverStr("nop\n", "BcondElimination");
}

TEST_F(AssemblerLOONGARCH64Test, BcondUnconditional) {
  Loongarch64Label label;
  __ Bind(&label);
  __ Nop();
  for (XRegister reg : GetRegisters()) {
    __ Beq(reg, reg, &label);
    __ Bge(reg, reg, &label);
    __ Bgeu(reg, reg, &label);
  }
  std::string expected =
      "1:\n"
      "nop\n" +
      RepeatInsn(3u * GetRegisters().size(), "b 1b\n", []() {});
  DriverStr(expected, "BcondUnconditional");
}

TEST_F(AssemblerLOONGARCH64Test, BlRdForward2MiB) {
  TestBlRdForward("BlRdForward2MiB", 2 * MB, "1", GetPrintBlRd());
}

TEST_F(AssemblerLOONGARCH64Test, JalRdBackward2MiB) {
  TestBlRdBackward("BlRdBackward2MiB", 2 * MB, "1", GetPrintBlRd());
}

TEST_F(AssemblerLOONGARCH64Test, BMaxOffset23Forward) {
  TestBuncondForward("BMaxOffset23Forward",
                     MaxOffset23ForwardDistance() - /*B*/ 4u,
                     "1",
                     GetEmitB(),
                     GetPrintB());
}

TEST_F(AssemblerLOONGARCH64Test, BMaxOffset23Backward) {
  TestBuncondBackward("BMaxOffset23Backward",
                      MaxOffset23BackwardDistance(),
                      "1",
                      GetEmitB(),
                      GetPrintB());
}

/* TODO : address of %pc_lo12 based on current pc_abs */
//TEST_F(AssemblerLOONGARCH64Test, LoadLabelAddress) {
//  std::string expected;
//  constexpr size_t kNumLoadsForward = 4 * KB ;
//  constexpr size_t kNumLoadsBackward = 4 * KB;
//  Loongarch64Label label;
//  auto emit_batch = [&](size_t num_loads, const std::string& target_label) {
//    for (size_t i = 0; i != num_loads; ++i) {
//      // Cycle through non-Zero registers.
//      XRegister rd = enum_cast<XRegister>((i % (kNumberOfXRegisters - 1)) + 1);
//      std::string rd_name = GetRegisterName(rd);
//      __ LoadLabelAddress(rd, &label);
//      expected += "1:\n"
//                  "pcalau12i " + rd_name + ", %pc_hi20(" + target_label + ")\n"
//                  "addi.d " + rd_name + ", " + rd_name + ", %pc_lo12(" + target_label + ")\n";
//    }
//  };
//  emit_batch(kNumLoadsForward, "2f");
//  __ Bind(&label);
//  expected += "2:\n";
//  emit_batch(kNumLoadsBackward, "2b");
//  DriverStr(expected, "LoadLabelAddress");
//}
//
//TEST_F(AssemblerLOONGARCH64Test, LoadLiteralWithPaddingForLong) {
//  TestLoadLiteral("LoadLiteralWithPaddingForLong", /*with_padding_for_long=*/ true);
//}
//
//TEST_F(AssemblerLOONGARCH64Test, LoadLiteralWithoutPaddingForLong) {
//  TestLoadLiteral("LoadLiteralWithoutPaddingForLong", /*with_padding_for_long=*/ false);
//}
//TEST_F(AssemblerLOONGARCH64Test, JumpTable) {
//  std::string expected;
//  expected += EmitNops(sizeof(uint32_t));
//  Loongarch64Label targets[4];
//  uint32_t target_locations[4];
//  JumpTable* jump_table = __ CreateJumpTable(ArenaVector<Loongarch64Label*>(
//      {&targets[0], &targets[1], &targets[2], &targets[3]}, __ GetAllocator()->Adapter()));
//  for (size_t i : {0, 1, 2, 3}) {
//    target_locations[i] = __ CodeSize();
//    __ Bind(&targets[i]);
//    expected += std::to_string(i) + ":\n";
//    expected += EmitNops(sizeof(uint32_t));
//  }
//  __ LoadLabelAddress(A0, jump_table->GetLabel());
//  expected += "4:\n"
//              "pcalau12i $a0, %pc_hi20(5f)\n"
//              "addi.d $a0, $a0, %pc_lo12(5f)\n";
//  expected += EmitNops(sizeof(uint32_t));
//  uint32_t label5_location = __ CodeSize();
//  auto target_offset = [&](size_t i) {
//    // Even with `-mno-relax`, clang assembler does not fully resolve `.4byte 0b - 5b`
//    // and emits a relocation, so we need to calculate target offsets ourselves.
//    return std::to_string(static_cast<int64_t>(target_locations[i] - label5_location));
//  };
//  expected += "5:\n"
//              ".4byte " + target_offset(0) + "\n"
//              ".4byte " + target_offset(1) + "\n"
//              ".4byte " + target_offset(2) + "\n"
//              ".4byte " + target_offset(3) + "\n";
//  DriverStr(expected, "JumpTable");
//}


// TODO:tail jump:need assembler support
//TEST_F(AssemblerLOONGARCH64Test, BeqA0A1OverMaxOffset23Forward) {
//  TestBeqA0A1Forward("BeqA0A1OverMaxOffset23Forward",
//                     MaxOffset23ForwardDistance() - /* B */ 4u + /*Exceed max*/ 4u,
//                     "1",
//                     GetPrintBcondOppositeAndTail("2", "3"));
//}
//
//TEST_F(AssemblerLOONGARCH64Test, BeqA0A1OverMaxOffset23Backward) {
//  TestBeqA0A1Backward("BeqA0A1OverMaxOffset23Backward",
//                      MaxOffset23BackwardDistance() - /*BNE*/ 4u + /*Exceed max*/ 4u,
//                      "1",
//                      GetPrintBcondOppositeAndTail("2", "3"));
//}
//
//TEST_F(AssemblerLOONGARCH64Test, BcondForward5MiB) {
//  TestBcondForward("BcondForward3MiB", 5 * MB, "1", GetPrintBcondOppositeAndTail("2", "3"));
//}
//
//TEST_F(AssemblerLOONGARCH64Test, BcondBackward5MiB) {
//  TestBcondBackward("BcondBackward3MiB", 5 * MB, "1", GetPrintBcondOppositeAndTail("2", "3"));
//}




/////////////////////////////////////////////////


#undef __

}  // namespace loongarch64
}  // namespace art
