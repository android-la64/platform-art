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

#ifndef ART_COMPILER_UTILS_LOONGARCH64_ASSEMBLER_LOONGARCH64_H_
#define ART_COMPILER_UTILS_LOONGARCH64_ASSEMBLER_LOONGARCH64_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "arch/loongarch64/instruction_set_features_loongarch64.h"
#include "arch/loongarch64/registers_loongarch64.h"
#include "base/arena_containers.h"
#include "base/globals.h"
#include "base/macros.h"
#include "base/pointer_size.h"
#include "managed_register_loongarch64.h"
#include "utils/assembler.h"
#include "utils/label.h"

namespace art {
namespace loongarch64 {

class ScratchRegisterScope;

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
static constexpr size_t kLoongarch64FloatRegSizeInBytes = 8;

class Loongarch64Label : public Label {
  public:
  Loongarch64Label() : prev_branch_id_(kNoPrevBranchId) {}

  Loongarch64Label(Loongarch64Label&& src)
      : Label(std::move(src)), prev_branch_id_(src.prev_branch_id_) {}

 private:
  static constexpr uint32_t kNoPrevBranchId = std::numeric_limits<uint32_t>::max();

  uint32_t prev_branch_id_;  // To get distance from preceding branch, if any.

  friend class Loongarch64Assembler;
  DISALLOW_COPY_AND_ASSIGN(Loongarch64Label);
};

// Assembler literal is a value embedded in code, retrieved using a PC-relative load.
class Literal {
 public:
  static constexpr size_t kMaxSize = 8;

  Literal(uint32_t size, const uint8_t* data) : label_(), size_(size) {
    DCHECK_LE(size, Literal::kMaxSize);
    memcpy(data_, data, size);
  }

  template <typename T>
  T GetValue() const {
    DCHECK_EQ(size_, sizeof(T));
    T value;
    memcpy(&value, data_, sizeof(T));
    return value;
  }

  uint32_t GetSize() const { return size_; }

  const uint8_t* GetData() const { return data_; }

  Loongarch64Label* GetLabel() { return &label_; }

  const Loongarch64Label* GetLabel() const { return &label_; }

 private:
  Loongarch64Label label_;
  const uint32_t size_;
  uint8_t data_[kMaxSize];

  DISALLOW_COPY_AND_ASSIGN(Literal);
};

// Jump table: table of labels emitted after the code and before the literals. Similar to literals.
class JumpTable {
 public:
  explicit JumpTable(ArenaVector<Loongarch64Label*>&& labels) : label_(), labels_(std::move(labels)) {}

  size_t GetSize() const { return labels_.size() * sizeof(int32_t); }

  const ArenaVector<Loongarch64Label*>& GetData() const { return labels_; }

  Loongarch64Label* GetLabel() { return &label_; }

  const Loongarch64Label* GetLabel() const { return &label_; }

 private:
  Loongarch64Label label_;
  ArenaVector<Loongarch64Label*> labels_;

  DISALLOW_COPY_AND_ASSIGN(JumpTable);
};


class Loongarch64Assembler final : public Assembler {
 public:
  explicit Loongarch64Assembler(ArenaAllocator* allocator,
                            const Loongarch64InstructionSetFeatures* instruction_set_features = nullptr)
      : Assembler(allocator),
        branches_(allocator->Adapter(kArenaAllocAssembler)),
        finalized_(false),
        overwriting_(false),
        overwrite_location_(0),
        literals_(allocator->Adapter(kArenaAllocAssembler)),
        long_literals_(allocator->Adapter(kArenaAllocAssembler)),
        jump_tables_(allocator->Adapter(kArenaAllocAssembler)),
        last_position_adjustment_(0),
        last_old_position_(0),
        last_branch_id_(0),
        available_scratch_core_registers_((1u << TMP) | (1u << TMP2)),
        available_scratch_fp_registers_(1u << FTMP)   {
    UNUSED(instruction_set_features);
    cfi().DelayEmittingAdvancePCs();
  }

  virtual ~Loongarch64Assembler() {
     for (auto& branch : branches_) {
      CHECK(branch.IsResolved());
    }
  }

  size_t CodeSize() const override { return Assembler::CodeSize(); }
  DebugFrameOpCodeWriterForAssembler& cfi() { return Assembler::cfi(); }

  ////////////////////////////// LOONGARCH64 MACRO Instructions END ///////////////////////////////
  // 2RI8-Type
  // Load signed instructions : opcode from 00 0000 0001 0000
  //                                      ~ 00 0000 0001 0011
  void Slli_w(XRegister rd, XRegister rj, int ui5);
  void Slli_d(XRegister rd, XRegister rj, int ui6);
  void Srli_w(XRegister rd, XRegister rj, int ui5);
  void Srli_d(XRegister rd, XRegister rj, int ui6);
  void Srai_w(XRegister rd, XRegister rj, int ui5);
  void Srai_d(XRegister rd, XRegister rj, int ui6);
  void Rotri_w(XRegister rd, XRegister rj, int ui5);
  void Rotri_d(XRegister rd, XRegister rj, int ui6);

  // 2RI14-Type
  // LL/SC and LD/STPTR instructions : opcode from 0010 0000
  //                                             ~ 0010 0111
  void LL_w(XRegister rd, XRegister rj, int32_t si14);
  void SC_w(XRegister rd, XRegister rj, int32_t si14);
  void LL_d(XRegister rd, XRegister rj, int32_t si14);
  void SC_d(XRegister rd, XRegister rj, int32_t si14);
  void Ldptr_w(XRegister rd, XRegister rj, int32_t si14);
  void Stptr_w(XRegister rd, XRegister rj, int32_t si14);
  void Ldptr_d(XRegister rd, XRegister rj, int32_t si14);
  void Stptr_d(XRegister rd, XRegister rj, int32_t si14);

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

  // F L/S signed instructions : opcode from 00 1010 1100
  //                                       ~ 00 1010 1111
  void FLd_s(FRegister fd, XRegister rs1, int32_t si12);
  void FSt_s(FRegister fd, XRegister rs1, int32_t si12);
  void FLd_d(FRegister fd, XRegister rs1, int32_t si12);
  void FSt_d(FRegister fd, XRegister rs1, int32_t si12);

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
  void Lu52i_D(XRegister rd, XRegister rs1, int32_t imm12);
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

  // PC-relative instructions : opcode from 0 0010 10
  //                                      ~ 0 0011 11
  void Lu12i_W(XRegister rd, uint32_t imm20);
  void Lu32i_D(XRegister rd, uint32_t imm20);
  void Pcaddi(XRegister rd, uint32_t imm20);
  void Pcalau12i(XRegister rd, uint32_t imm20);
  void Pcaddu12i(XRegister rd, uint32_t imm20);
  void Pcaddu18i(XRegister rd, uint32_t imm20);


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

  // Load/store macros for arbitrary 32-bit offsets.
  void Load_B(XRegister rd, XRegister rs1, int32_t offset);
  void Load_H(XRegister rd, XRegister rs1, int32_t offset);
  void Load_W(XRegister rd, XRegister rs1, int32_t offset);
  void Load_D(XRegister rd, XRegister rs1, int32_t offset);
  void Load_BU(XRegister rd, XRegister rs1, int32_t offset);
  void Load_HU(XRegister rd, XRegister rs1, int32_t offset);
  void Load_WU(XRegister rd, XRegister rs1, int32_t offset);
  void Store_B(XRegister rs2, XRegister rs1, int32_t offset);
  void Store_H(XRegister rs2, XRegister rs1, int32_t offset);
  void Store_W(XRegister rs2, XRegister rs1, int32_t offset);
  void Store_D(XRegister rs2, XRegister rs1, int32_t offset);
  void FLoad_S(FRegister fd, XRegister rs1, int32_t offset);
  void FLoad_D(FRegister fd, XRegister rs1, int32_t offset);
  void FStore_S(FRegister fd, XRegister rs1, int32_t offset);
  void FStore_D(FRegister fd, XRegister rs1, int32_t offset);

  // Macros for loading constants.
  void LoadConst32(XRegister rd, int32_t value);
  void LoadConst64(XRegister rd, int64_t value);

  // Macros for adding constants.
  void AddConst32(XRegister rd, XRegister rs1, int32_t value);
  void AddConst64(XRegister rd, XRegister rs1, int64_t value);

  // transfer instruction, opcode from 01 0000
  //                                 ~ 01 1011
  void Beqz(XRegister rs, int32_t offset21);
  void Bnez(XRegister rs, int32_t offset21);
  // float branch
  // void Bceqz(XRegister rs, int32_t offset);
  // void Bcnez(XRegister rs, int32_t offset);
  void B(int32_t offset26);
  void Bl(int32_t offset26);
  void Beq(XRegister rd, XRegister rs1, int32_t offset16);
  void Bne(XRegister rd, XRegister rs1, int32_t offset16);
  void Blt(XRegister rd, XRegister rs1, int32_t offset16);
  void Bge(XRegister rd, XRegister rs1, int32_t offset16);
  void Bltu(XRegister rd, XRegister rs1, int32_t offset16);
  void Bgeu(XRegister rd, XRegister rs1, int32_t offset16);
  void Jirl(XRegister rd, XRegister rs1, int32_t offset16);
  void Ret();

  // Branch pseudo instructions
  void Bgt(XRegister );
  // pseudo instructions
  void Move(XRegister rd, XRegister rj);
  // Jump pseudo instructions
  void Jr(XRegister rs);
  // pseudo instructions
  void Nop();
  void Li(XRegister rd, int64_t imm);
  void Max(XRegister rd, XRegister rs1, XRegister rs2);
  void Min(XRegister rd, XRegister rs1, XRegister rs2);


  // Jumps and branches to a label.
  void Bgez(XRegister rs, Loongarch64Label* label, bool is_bare = false);
  void Bltz(XRegister rs, Loongarch64Label* label, bool is_bare = false);
  void Beqz(XRegister rs, Loongarch64Label* label, bool is_bare = false);
  void Bnez(XRegister rs, Loongarch64Label* label, bool is_bare = false);
  void Jirl(XRegister rd, XRegister rs1, Loongarch64Label* label, bool is_bare = false);
  void B(Loongarch64Label* label, bool is_bare = false);
  void Bl(Loongarch64Label* label, bool is_bare = false);
  void Beq(XRegister rd, XRegister rs1, Loongarch64Label* label, bool is_bare = false);
  void Bne(XRegister rd, XRegister rs1, Loongarch64Label* label, bool is_bare = false);
  void Blt(XRegister rd, XRegister rs1, Loongarch64Label* label, bool is_bare = false);
  void Bge(XRegister rd, XRegister rs1, Loongarch64Label* label, bool is_bare = false);
  void Bltu(XRegister rd, XRegister rs1, Loongarch64Label* label, bool is_bare = false);
  void Bgeu(XRegister rd, XRegister rs1, Loongarch64Label* label, bool is_bare = false);

  // Literal load.
  void Load_W(XRegister rd, Literal* literal);
  void Load_WU(XRegister rd, Literal* literal);
  void Load_D(XRegister rd, Literal* literal);

  // Barrier instructions
  void Dbar(uint32_t);

  // Environment call and breakpoint , opcode from 0 0000 0000 0101 0100
  //                                             ~ 0 0000 0000 0101 0110
  void brk(uint32_t imm15);
  // void Dbcl();
  // void Syscall();

  // low-level ALU instructions : opcode 000 0000 0001 0110
  void Alsl_d(XRegister rd, XRegister rj, XRegister rk, uint32_t sa2);
  // 3R-Type
  // low-level ALU instructions : opcode from 000 0000 0000 0010
  //                                        ~ 000 0000 0000 0100
  void Alsl_w(XRegister rd, XRegister rj, XRegister rk, uint32_t sa2);
  void Alsl_wu(XRegister rd, XRegister rj, XRegister rk, uint32_t sa2);
  void Bytepick_w(XRegister rd, XRegister rj, XRegister rk, uint32_t sa2);
  // 3R-Type
  // low-level ALU instructions : opcode from 00 0000 0000 0011
  void Bytepick_d(XRegister rd, XRegister rj, XRegister rk, uint32_t sa3);







  ////////////////////////////// LOONGARCH64 MACRO Instructions END ///////////////////////////////

  void Bind(Label* label) override { Bind(down_cast<Loongarch64Label*>(label)); }

  void Jump(Label* label ATTRIBUTE_UNUSED) override {
    UNIMPLEMENTED(FATAL) << "Do not use Jump for LOONGARCH64";
  }

  void Bind(Loongarch64Label* label);

  // Load label address using PC-relative loads.
  void LoadLabelAddress(XRegister rd, Loongarch64Label* label);

  // Create a new literal with a given value.
  // NOTE:Use `Identity<>` to force the template parameter to be explicitly specified.
  template <typename T>
  Literal* NewLiteral(typename Identity<T>::type value) {
    static_assert(std::is_integral<T>::value, "T must be an integral type.");
    return NewLiteral(sizeof(value), reinterpret_cast<const uint8_t*>(&value));
  }

  // Create a new literal with the given data.
  Literal* NewLiteral(size_t size, const uint8_t* data);

  // Create a jump table for the given labels that will be emitted when finalizing.
  // When the table is emitted, offsets will be relative to the location of the table.
  // The table location is determined by the location of its label (the label precedes
  // the table data) and should be loaded using LoadLabelAddress().
  JumpTable* CreateJumpTable(ArenaVector<Loongarch64Label*>&& labels);

 public:
  // Emit data (e.g. encoded instruction or immediate) to the instruction stream.
  void Emit(uint32_t value);

  // Emit slow paths queued during assembly, promote short branches to long if needed,
  // and emit branches.
  void FinalizeCode() override;

  // Returns the current location of a label.
  //
  // This function must be used instead of `Loongarch64Label::GetPosition()`
  // which returns assembler's internal data instead of an actual location.
  //
  // The location can change during branch fixup in `FinalizeCode()`. Before that,
  // the location is not final and therefore not very useful to external users,
  // so they should preferably retrieve the location only after `FinalizeCode()`.
  uint32_t GetLabelLocation(const Loongarch64Label* label) const;

  // Get the final position of a label after local fixup based on the old position
  // recorded before FinalizeCode().
  uint32_t GetAdjustedPosition(uint32_t old_position);

 private:
  enum BranchCondition : uint8_t {
    kCondEQ,
    kCondNE,
    kCondLT,
    kCondGE,
    kCondLTU,
    kCondGEU,
    kCondEQZ,
    kCondNEZ,
    kUncond,
  };

  // Note that PC-relative literal loads are handled as pseudo branches because they need
  // to be emitted after branch relocation to use correct offsets.
  class Branch {
   public:
    enum Type : uint8_t {
      // Short branches (can be promoted to longer).
      kCondBranch,
      kUncondBranch,
      kCall,
      // Short branches (can't be promoted to longer).
      // TODO(loongarch64): Do we need these (untested) bare branches, or can we remove them?
      kBareCondBranch,
      kBareUncondBranch,
      kBareCall,

      // Medium branch (can be promoted to long).
      kCondBranch23,

      // Long branches.
      kLongCondBranch,
      kLongUncondBranch,
      kLongCall,

      // Label.
      kLabel,

      // Literals.
      kLiteral,
      kLiteralUnsigned,
      kLiteralLong,
    };

    // Bit sizes of offsets defined as enums to minimize chance of typos.
    enum OffsetBits {
      kOffset18 = 18, // offs16 + 2 shift = 18 = +/- 128 * KB
      kOffset23 = 23, // +/- 4 * MB offs26 + 2 shift = 28 = +/- 128 * MB
      kOffset32 = 32, // ZQZ-TODO
    };

    static constexpr uint32_t kUnresolved = 0xffffffff;  // Unresolved target_
    static constexpr uint32_t kMaxBranchLength = 12;  // In bytes.

    struct BranchInfo {
      // Branch length in bytes.
      uint32_t length;
      // The offset in bytes of the PC used in the (only) PC-relative instruction from
      // the start of the branch sequence. RISC-V always uses the address of the PC-relative
      // instruction as the PC, so this is essentially the offset of that instruction.
      uint32_t pc_offset;
      // How large (in bits) a PC-relative offset can be for a given type of branch.
      OffsetBits offset_size;
    };
    static const BranchInfo branch_info_[/* Type */];

    // Unconditional branch or call.
    Branch(uint32_t location, uint32_t target, XRegister rd, bool is_bare);
    // Conditional branch.
    Branch(uint32_t location,
           uint32_t target,
           BranchCondition condition,
           XRegister lhs_reg,
           XRegister rhs_reg,
           bool is_bare,
	   bool fcc_reg_flag_ = false);
    // Label address (in literal area) or literal.
    Branch(uint32_t location, uint32_t target, XRegister rd, Type label_or_literal_type);

    Branch(uint32_t location, uint32_t target, FRegister rd, Type literal_type);

    // Some conditional branches with lhs = rhs are effectively NOPs, while some
    // others are effectively unconditional.
    static bool IsNop(BranchCondition condition, XRegister lhs, XRegister rhs);
    static bool IsUncond(BranchCondition condition, XRegister lhs, XRegister rhs);
    static bool IsCompressed(Type type);

    static BranchCondition OppositeCondition(BranchCondition cond);

    Type GetType() const;
    Type GetOldType() const;
    BranchCondition GetCondition() const;
    XRegister GetLeftRegister() const;
    XRegister GetRightRegister() const;
    XRegister GetNonZeroRegister() const;
    FRegister GetFRegister() const;
    uint32_t GetTarget() const;
    uint32_t GetLocation() const;
    uint32_t GetOldLocation() const;
    uint32_t GetLength() const;
    uint32_t GetOldLength() const;
    uint32_t GetEndLocation() const;
    uint32_t GetOldEndLocation() const;
    bool IsBare() const;
    bool IsResolved() const;

    uint32_t NextBranchId() const;

    // Checks if condition meets compression requirements
    bool IsCompressableCondition() const;

    // Returns the bit size of the signed offset that the branch instruction can handle.
    OffsetBits GetOffsetSize() const;

    // Calculates the distance between two byte locations in the assembler buffer and
    // returns the number of bits needed to represent the distance as a signed integer.
    static OffsetBits GetOffsetSizeNeeded(uint32_t location, uint32_t target);

    // Resolve a branch when the target is known.
    void Resolve(uint32_t target);

    // Relocate a branch by a given delta if needed due to expansion of this or another
    // branch at a given location by this delta (just changes location_ and target_).
    void Relocate(uint32_t expand_location, uint32_t delta);

    // If necessary, updates the type by promoting a short branch to a longer branch
    // based on the branch location and target. Returns the amount (in bytes) by
    // which the branch size has increased.
    uint32_t PromoteIfNeeded();

    // Returns the offset into assembler buffer that shall be used as the base PC for
    // offset calculation. RISC-V always uses the address of the PC-relative instruction
    // as the PC, so this is essentially the location of that instruction.
    uint32_t GetOffsetLocation() const;

    // Calculates and returns the offset ready for encoding in the branch instruction(s).
    int32_t GetOffset() const;

    // Link with the next branch
    void LinkToList(uint32_t next_branch_id);

   private:
    // Completes branch construction by determining and recording its type.
    void InitializeType(Type initial_type);
    // Helper for the above.
    void InitShortOrLong(OffsetBits ofs_size, Type short_type, Type long_type, Type longest_type);
    void InitShortOrLong(OffsetBits ofs_size,
                         Type compressed_type,
                         Type short_type,
                         Type long_type,
                         Type longest_type);

    uint32_t old_location_;  // Offset into assembler buffer in bytes.
    uint32_t location_;      // Offset into assembler buffer in bytes.
    uint32_t target_;        // Offset into assembler buffer in bytes.

    bool fcc_reg_flag_;  // Mark this branch is bceqz/bcnez.
    XRegister lhs_reg_;          // Left-hand side register in conditional branches or
                                 // destination register in calls or literals.
    XRegister rhs_reg_;          // Right-hand side register in conditional branches.
    FRegister freg_;             // Destination register in FP literals.
    BranchCondition condition_;  // Condition for conditional branches.

    Type type_;      // Current type of the branch.
    Type old_type_;  // Initial type of the branch.

    // Id of the next branch bound to the same label in singly-linked zero-terminated list
    // NOTE: encoded the same way as a position in a linked Label (id + sizeof(void*))
    // Label itself is used to hold the 'head' of this list
    uint32_t next_branch_id_;
  };

  // Branch and literal fixup.

  void EmitBcond(BranchCondition cond, XRegister rs, XRegister rt, int32_t offset);
  void EmitBranch(Branch* branch);
  void EmitBranches();
  void EmitJumpTables();
  void EmitLiterals();

  void FinalizeLabeledBranch(Loongarch64Label* label);
  void Bcond(Loongarch64Label* label,
             bool is_bare,
             BranchCondition condition,
             XRegister lhs,
             XRegister rhs);
  void Buncond(Loongarch64Label* label, XRegister rd, bool is_bare);
  void LoadLiteral(Literal* literal, XRegister rd, Branch::Type literal_type);

  Branch* GetBranch(uint32_t branch_id);
  const Branch* GetBranch(uint32_t branch_id) const;

  void ReserveJumpTableSpace();
  void PromoteBranches();
  void PatchCFI();


  // Adjust base register and offset if needed for load/store with a large offset.
  void AdjustBaseAndOffset(XRegister& base, int32_t& offset);

  // Convert 12-bit x to a sign-extended 12-bit integer
  static int simm12(int x) {
    DCHECK(x == (x & 0xFFF)) << x << "must be 12-bit only";
    return (x << 20) >> 20;
  }

  // Convert 20-bit x to a sign-extended 20-bit integer
  static int simm20(int32_t x) {
    DCHECK(x == (x & 0xFFFFF)) << x << "must be 20-bit only";
    return (x << 12) >> 12;
  }

  // get a word with the n.th or the right-most or left-most n bits set
  // (note: #define used only so that they can be used in enum constant definitions)
  #define nth_bit(n)        (((n) >= (1 << 6)) ? 0ULL : (1ULL << (n)))
  #define right_n_bits(n)   (nth_bit(n) - 1)
  inline int64_t mask_bits      (int64_t  x, int64_t m) { return x & m; }
  // returns the bitfield of x starting at start_bit_no with length field_length (no sign-extension!)
  inline int64_t bitfield(int64_t x, int start_bit_no, int field_length) {
    return mask_bits(x >> start_bit_no, right_n_bits(field_length));
  }

  // Implementation helper for `Li()`, `LoadConst32()` and `LoadConst64()`.
  void LoadImmediate(XRegister rd, int64_t imm);

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

  // 3RI2-Type instruction:
  //
  //   31                                   16 15 14    10  9     5 4        0
  //   ----------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . . . | .| . . . .| . . . .| . . . . ]
  //   [                  opcode            |sa2| rk/rs2 | rj/rs1 |   rd    ]
  //   ----------------------------------------------------------------------
  template <typename Reg3, typename Reg2, typename Reg1>
  void Emit3RI2(uint32_t opcode, uint32_t sa2, Reg3 rk, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<15>(opcode));
    DCHECK(IsUint<2>(sa2)) << sa2;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rk)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 17 | (sa2 & 0x3) << 15 | static_cast<uint32_t>(rk) << 10 |
                        static_cast<uint32_t>(rj) << 5 | static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // 3RI3-Type instruction:
  //
  //   31                                  17  15 14    10  9     5 4        0
  //   ----------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . . | . .| . . . .| . . . .| . . . . ]
  //   [                  opcode           | sa3| rk/rs2 | rj/rs1 |   rd    ]
  //   ----------------------------------------------------------------------
  template <typename Reg3, typename Reg2, typename Reg1>
  void Emit3RI3(uint32_t opcode, uint32_t sa3, Reg3 rk, Reg2 rj, Reg1 rd) {
    DCHECK(IsUint<14>(opcode));
    DCHECK(IsUint<3>(sa3)) << sa3;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rk)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 18 | (sa3 & 0x7) << 15 | static_cast<uint32_t>(rk) << 10 |
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
  void Emit2RI12(uint32_t opcode, int32_t imm12, Reg2 rj, Reg1 rd, bool check_imm12 = true) {
    DCHECK(IsUint<10>(opcode));
    if (check_imm12) DCHECK(IsInt<12>(imm12)) << imm12; // Operators overloading when trigger assertion
    imm12 = imm12 & 0xfff;
    DCHECK(IsUint<5>(static_cast<uint32_t>(rj)));
    DCHECK(IsUint<5>(static_cast<uint32_t>(rd)));
    uint32_t encoding = opcode << 22 | static_cast<uint32_t>(imm12) << 10 |
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
    DCHECK(IsInt<16>(imm16 >> 2)) << (imm16 >> 2); // Operators overloading when trigger assertion
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
    DCHECK(IsInt<21>(imm21 >> 2)) << (imm21 >> 2);
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

  // I26-Type instruction:
  //
  //   31                              25 24                 5  4        0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .  . . . .| . . . . .]
  //   [            opcode 31:25         |       I20[19:0]     |    rd    ]
  //   --------------------------------------------------------------------
  template <typename Reg1>
  void EmitPC_rel(uint32_t opcode, uint32_t imm20, Reg1 rd) {
    DCHECK(IsUint<7>(opcode));
    DCHECK(IsUint<20>(imm20 & 0xFFFFF)) << imm20;
    uint32_t encoding = opcode << 25 | (imm20 & 0xFFFFF) << 5 |
                        static_cast<uint32_t>(rd);
    Emit(encoding);
  }

  // I15
  //
  //   31                              15 14                              0
  //   --------------------------------------------------------------------
  //   [ . . . . . . . . . . . . . . . . | . . . . . .  . . . .  . . . . .]
  //   [            opcode 31:15         |          I15[14:0]             ]
  //   --------------------------------------------------------------------
  void EmitI15(uint32_t opcode, int imm15) {
    DCHECK(IsUint<15>(imm15)) << imm15;
    uint32_t encoding = opcode << 15 | (imm15 & 0x7FFF);
    Emit(encoding);
  }

  ArenaVector<Branch> branches_;

  // For checking that we finalize the code only once.
  bool finalized_;

  // Whether appending instructions at the end of the buffer or overwriting the existing ones.
  bool overwriting_;
  // The current overwrite location.
  uint32_t overwrite_location_;

  // Use `std::deque<>` for literal labels to allow insertions at the end
  // without invalidating pointers and references to existing elements.
  ArenaDeque<Literal> literals_;
  ArenaDeque<Literal> long_literals_;  // 64-bit literals separated for alignment reasons.

  // Jump table list.
  ArenaDeque<JumpTable> jump_tables_;

  // Data for `GetAdjustedPosition()`, see the description there.
  uint32_t last_position_adjustment_;
  uint32_t last_old_position_;
  uint32_t last_branch_id_;

  uint32_t available_scratch_core_registers_;
  uint32_t available_scratch_fp_registers_;

  static constexpr uint32_t kXlen = 64;

  friend class ScratchRegisterScope;

  DISALLOW_COPY_AND_ASSIGN(Loongarch64Assembler);
};

class ScratchRegisterScope {
 public:
  explicit ScratchRegisterScope(Loongarch64Assembler* assembler)
      : assembler_(assembler),
        old_available_scratch_core_registers_(assembler->available_scratch_core_registers_),
        old_available_scratch_fp_registers_(assembler->available_scratch_fp_registers_) {}

  ~ScratchRegisterScope() {
    assembler_->available_scratch_core_registers_ = old_available_scratch_core_registers_;
    assembler_->available_scratch_fp_registers_ = old_available_scratch_fp_registers_;
  }

  // Alocate a scratch `XRegister`. There must be an available register to allocate.
  XRegister AllocateXRegister() {
    CHECK_NE(assembler_->available_scratch_core_registers_, 0u);
    // Allocate the highest available scratch register (prefer TMP(T6) over TMP2(T5)).
    uint32_t reg_num = (BitSizeOf(assembler_->available_scratch_core_registers_) - 1u) -
                       CLZ(assembler_->available_scratch_core_registers_);
    assembler_->available_scratch_core_registers_ &= ~(1u << reg_num);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfXRegisters));
    return enum_cast<XRegister>(reg_num);
  }

  // Free a previously unavailable core register for use as a scratch register.
  // This can be an arbitrary register, not necessarly the usual `TMP` or `TMP2`.
  void FreeXRegister(XRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfXRegisters));
    CHECK_EQ((1u << reg_num) & assembler_->available_scratch_core_registers_, 0u);
    assembler_->available_scratch_core_registers_ |= 1u << reg_num;
  }

  // The number of available scratch core registers.
  size_t AvailableXRegisters() {
    return POPCOUNT(assembler_->available_scratch_core_registers_);
  }

  // Make sure a core register is available for use as a scratch register.
  void IncludeXRegister(XRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfXRegisters));
    assembler_->available_scratch_core_registers_ |= 1u << reg_num;
  }

  // Make sure a core register is not available for use as a scratch register.
  void ExcludeXRegister(XRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfXRegisters));
    assembler_->available_scratch_core_registers_ &= ~(1u << reg_num);
  }

  // Alocate a scratch `FRegister`. There must be an available register to allocate.
  FRegister AllocateFRegister() {
    CHECK_NE(assembler_->available_scratch_fp_registers_, 0u);
    // Allocate the highest available scratch register (same as for core registers).
    uint32_t reg_num = (BitSizeOf(assembler_->available_scratch_fp_registers_) - 1u) -
                       CLZ(assembler_->available_scratch_fp_registers_);
    assembler_->available_scratch_fp_registers_ &= ~(1u << reg_num);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfFRegisters));
    return enum_cast<FRegister>(reg_num);
  }

  // Free a previously unavailable FP register for use as a scratch register.
  // This can be an arbitrary register, not necessarly the usual `FTMP`.
  void FreeFRegister(FRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfFRegisters));
    CHECK_EQ((1u << reg_num) & assembler_->available_scratch_fp_registers_, 0u);
    assembler_->available_scratch_fp_registers_ |= 1u << reg_num;
  }

  // The number of available scratch FP registers.
  size_t AvailableFRegisters() {
    return POPCOUNT(assembler_->available_scratch_fp_registers_);
  }

  // Make sure an FP register is available for use as a scratch register.
  void IncludeFRegister(FRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfFRegisters));
    assembler_->available_scratch_fp_registers_ |= 1u << reg_num;
  }

  // Make sure an FP register is not available for use as a scratch register.
  void ExcludeFRegister(FRegister reg) {
    uint32_t reg_num = enum_cast<uint32_t>(reg);
    DCHECK_LT(reg_num, enum_cast<uint32_t>(kNumberOfFRegisters));
    assembler_->available_scratch_fp_registers_ &= ~(1u << reg_num);
  }

 private:
  Loongarch64Assembler* const assembler_;
  const uint32_t old_available_scratch_core_registers_;
  const uint32_t old_available_scratch_fp_registers_;

  DISALLOW_COPY_AND_ASSIGN(ScratchRegisterScope);
};

}  // namespace loongarch64
}  // namespace art

#endif  // ART_COMPILER_UTILS_LOONGARCH64_ASSEMBLER_LOONGARCH64_H_
