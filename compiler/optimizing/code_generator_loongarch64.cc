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

#include "code_generator_loongarch64.h"

#include "android-base/logging.h"
#include "android-base/macros.h"
#include "thread.h"
#include "arch/loongarch64/registers_loongarch64.h"
#include "base/macros.h"
#include "code_generator_utils.h"
#include "dwarf/register.h"
#include "heap_poisoning.h"
#include "intrinsics_list.h"
#include "jit/profiling_info.h"
#include "mirror/class-inl.h"
#include "optimizing/nodes.h"
#include "stack_map_stream.h"
#include "utils/label.h"
#include "utils/loongarch64/assembler_loongarch64.h"
#include "utils/stack_checks.h"
#include "runtime.h"

namespace art {
namespace loongarch64 {

static constexpr XRegister kCoreCalleeSaves[] = {
    // S1(TR) is excluded as the ART thread register.
    S0, S2, S3, S4, S5, S6, S7, S8, RA
};

static constexpr FRegister kFpuCalleeSaves[] = {
    FS0, FS1, FS2, FS3, FS4, FS5, FS6, FS7
};

#define QUICK_ENTRY_POINT(x) QUICK_ENTRYPOINT_OFFSET(kLoongarch64PointerSize, x).Int32Value()

Location RegisterOrZeroBitPatternLocation(HInstruction* instruction) {
  return IsZeroBitPattern(instruction)
      ? Location::ConstantLocation(instruction->AsConstant())
      : Location::RequiresRegister();
}

XRegister InputXRegisterOrZero(Location location) {
  if (location.IsConstant()) {
    DCHECK(location.GetConstant()->IsZeroBitPattern());
    return Zero;
  } else {
    return location.AsRegister<XRegister>();
  }
}

Location Loongarch64ReturnLocation(DataType::Type return_type) {
  switch (return_type) {
    case DataType::Type::kBool:
    case DataType::Type::kUint8:
    case DataType::Type::kInt8:
    case DataType::Type::kUint16:
    case DataType::Type::kInt16:
    case DataType::Type::kUint32:
    case DataType::Type::kInt32:
    case DataType::Type::kReference:
    case DataType::Type::kUint64:
    case DataType::Type::kInt64:
      return Location::RegisterLocation(A0);

    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      return Location::FpuRegisterLocation(FA0);

    case DataType::Type::kVoid:
      return Location::NoLocation();
  }
  UNREACHABLE();
}

Location InvokeDexCallingConventionVisitorLOONGARCH64::GetReturnLocation(DataType::Type type) const {
  return Loongarch64ReturnLocation(type);
}

Location InvokeDexCallingConventionVisitorLOONGARCH64::GetMethodLocation() const {
  return Location::RegisterLocation(kArtMethodRegister);

}

Location InvokeDexCallingConventionVisitorLOONGARCH64::GetNextLocation(DataType::Type type) {
  Location next_location;
  if (type == DataType::Type::kVoid) {
    LOG(FATAL) << "Unexpected parameter type " << type;
  }

  // Note: Unlike the LOONGARCH C/C++ calling convention, managed ABI does not use
  // GPRs to pass FP args when we run out of FPRs.
  if (DataType::IsFloatingPointType(type) &&
      float_index_ < calling_convention.GetNumberOfFpuRegisters()) {
    next_location =
        Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(float_index_++));
  } else if (!DataType::IsFloatingPointType(type) &&
             (gp_index_ < calling_convention.GetNumberOfRegisters())) {
    next_location = Location::RegisterLocation(calling_convention.GetRegisterAt(gp_index_++));
  } else {
    size_t stack_offset = calling_convention.GetStackOffsetOf(stack_index_);
    next_location = DataType::Is64BitType(type) ? Location::DoubleStackSlot(stack_offset) :
                                                  Location::StackSlot(stack_offset);
  }

  // Space on the stack is reserved for all arguments.
  stack_index_ += DataType::Is64BitType(type) ? 2 : 1;

  return next_location;
}

#define __ down_cast<CodeGeneratorLOONGARCH64*>(codegen)->GetAssembler()->  // NOLINT

void LocationsBuilderLOONGARCH64::HandleInvoke(HInvoke* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

Location LocationsBuilderLOONGARCH64::RegisterOrZeroConstant(HInstruction* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

Location LocationsBuilderLOONGARCH64::FpuRegisterOrConstantForStore(HInstruction* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

class CompileOptimizedSlowPathLOONGARCH64 : public SlowPathCodeLOONGARCH64 {
 public:
  CompileOptimizedSlowPathLOONGARCH64() : SlowPathCodeLOONGARCH64(/*instruction=*/ nullptr) {}

  void EmitNativeCode(CodeGenerator* codegen) override {
    uint32_t entrypoint_offset =
        GetThreadOffset<kLoongarch64PointerSize>(kQuickCompileOptimized).Int32Value();
    __ Bind(GetEntryLabel());
    __ Load_D(RA, TR, entrypoint_offset);
    // Note: we don't record the call here (and therefore don't generate a stack
    // map), as the entrypoint should never be suspended.
    __ Move(TMP, RA);
    __ Jirl(RA, TMP, 0);
    __ B(GetExitLabel());
  }

  const char* GetDescription() const override { return "CompileOptimizedSlowPath"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(CompileOptimizedSlowPathLOONGARCH64);
};

class SuspendCheckSlowPathLOONGARCH64 : public SlowPathCodeLOONGARCH64 {
 public:
  SuspendCheckSlowPathLOONGARCH64(HSuspendCheck* instruction, HBasicBlock* successor)
      : SlowPathCodeLOONGARCH64(instruction), successor_(successor) {}

  void EmitNativeCode(CodeGenerator* codegen) override {
    LocationSummary* locations = instruction_->GetLocations();
    CodeGeneratorLOONGARCH64* loongarch64_codegen = down_cast<CodeGeneratorLOONGARCH64*>(codegen);
    __ Bind(GetEntryLabel());
    SaveLiveRegisters(codegen, locations);  // Only saves live vector registers for SIMD.
    loongarch64_codegen->InvokeRuntime(kQuickTestSuspend, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickTestSuspend, void, void>();
    RestoreLiveRegisters(codegen, locations);  // Only restores live vector registers for SIMD.
    if (successor_ == nullptr) {
      __ B(GetReturnLabel());
    } else {
      __ B(loongarch64_codegen->GetLabelOf(successor_));
    }
  }

  Loongarch64Label* GetReturnLabel() {
    DCHECK(successor_ == nullptr);
    return &return_label_;
  }

  const char* GetDescription() const override { return "SuspendCheckSlowPathLOONGARCH64"; }

  HBasicBlock* GetSuccessor() const { return successor_; }

 private:
  // If not null, the block to branch to after the suspend check.
  HBasicBlock* const successor_;

  // If `successor_` is null, the label to branch to after the suspend check.
  Loongarch64Label return_label_;

  DISALLOW_COPY_AND_ASSIGN(SuspendCheckSlowPathLOONGARCH64);
};

class NullCheckSlowPathLOONGARCH64 : public SlowPathCodeLOONGARCH64 {
 public:
  explicit NullCheckSlowPathLOONGARCH64(HNullCheck* instr) : SlowPathCodeLOONGARCH64(instr) {}

  void EmitNativeCode(CodeGenerator* codegen) override {
    CodeGeneratorLOONGARCH64* loongarch64_codegen = down_cast<CodeGeneratorLOONGARCH64*>(codegen);
    __ Bind(GetEntryLabel());
    if (instruction_->CanThrowIntoCatchBlock()) {
      // Live registers will be restored in the catch block if caught.
      SaveLiveRegisters(codegen, instruction_->GetLocations());
    }
    loongarch64_codegen->InvokeRuntime(
        kQuickThrowNullPointer, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickThrowNullPointer, void, void>();
  }

  bool IsFatal() const override { return true; }

  const char* GetDescription() const override { return "NullCheckSlowPathRISCV64"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(NullCheckSlowPathLOONGARCH64);
};

class BoundsCheckSlowPathLOONGARCH64 : public SlowPathCodeLOONGARCH64 {
 public:
  explicit BoundsCheckSlowPathLOONGARCH64(HBoundsCheck* instruction)
      : SlowPathCodeLOONGARCH64(instruction) {}

  void EmitNativeCode(CodeGenerator* codegen) override {
    LocationSummary* locations = instruction_->GetLocations();
    CodeGeneratorLOONGARCH64* loongarch64_codegen = down_cast<CodeGeneratorLOONGARCH64*>(codegen);
    __ Bind(GetEntryLabel());
    if (instruction_->CanThrowIntoCatchBlock()) {
      // Live registers will be restored in the catch block if caught.
      SaveLiveRegisters(codegen, instruction_->GetLocations());
    }
    // We're moving two locations to locations that could overlap, so we need a parallel
    // move resolver.
    InvokeRuntimeCallingConvention calling_convention;
    codegen->EmitParallelMoves(locations->InAt(0),
                               Location::RegisterLocation(calling_convention.GetRegisterAt(0)),
                               DataType::Type::kInt32,
                               locations->InAt(1),
                               Location::RegisterLocation(calling_convention.GetRegisterAt(1)),
                               DataType::Type::kInt32);
    QuickEntrypointEnum entrypoint = instruction_->AsBoundsCheck()->IsStringCharAt() ?
                                         kQuickThrowStringBounds :
                                         kQuickThrowArrayBounds;
    loongarch64_codegen->InvokeRuntime(entrypoint, instruction_, instruction_->GetDexPc(), this);
    CheckEntrypointTypes<kQuickThrowStringBounds, void, int32_t, int32_t>();
    CheckEntrypointTypes<kQuickThrowArrayBounds, void, int32_t, int32_t>();
  }

  bool IsFatal() const override { return true; }

  const char* GetDescription() const override { return "BoundsCheckSlowPathLOONGARCH64"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(BoundsCheckSlowPathLOONGARCH64);
};

#undef __
#define __ down_cast<Loongarch64Assembler*>(GetAssembler())->  // NOLINT

Loongarch64Assembler* ParallelMoveResolverLOONGARCH64::GetAssembler() const {
  return codegen_->GetAssembler();
}

void ParallelMoveResolverLOONGARCH64::EmitMove(size_t index) {
  MoveOperands* move = moves_[index];
  codegen_->MoveLocation(move->GetDestination(), move->GetSource(), move->GetType());
}

void ParallelMoveResolverLOONGARCH64::EmitSwap(size_t index) {
  MoveOperands* move = moves_[index];
  codegen_->SwapLocations(move->GetDestination(), move->GetSource(), move->GetType());
}

void ParallelMoveResolverLOONGARCH64::SpillScratch([[maybe_unused]] int reg) {
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void ParallelMoveResolverLOONGARCH64::RestoreScratch([[maybe_unused]] int reg) {
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void ParallelMoveResolverLOONGARCH64::Exchange(int index1, int index2, bool double_slot) {
  // We have 2 scratch X registers and 1 scratch F register that we can use. We prefer
  // to use X registers for the swap but if both offsets are too big, we need to reserve
  // one of the X registers for address adjustment and use an F register.
  bool use_fp_tmp2 = false;
  if (!IsInt<12>(index2)) {
    if (!IsInt<12>(index1)) {
      use_fp_tmp2 = true;
    } else {
      std::swap(index1, index2);
    }
  }
  // DCHECK_IMPLIES(!IsInt<12>(index2), use_fp_tmp2);

  Location loc1(double_slot ? Location::DoubleStackSlot(index1) : Location::StackSlot(index1));
  Location loc2(double_slot ? Location::DoubleStackSlot(index2) : Location::StackSlot(index2));
  loongarch64::ScratchRegisterScope srs(GetAssembler());
  Location tmp = Location::RegisterLocation(srs.AllocateXRegister());
  DataType::Type tmp_type = double_slot ? DataType::Type::kInt64 : DataType::Type::kInt32;
  Location tmp2 = use_fp_tmp2
      ? Location::FpuRegisterLocation(srs.AllocateFRegister())
      : Location::RegisterLocation(srs.AllocateXRegister());
  DataType::Type tmp2_type = use_fp_tmp2
      ? (double_slot ? DataType::Type::kFloat64 : DataType::Type::kFloat32)
      : tmp_type;

  codegen_->MoveLocation(tmp, loc1, tmp_type);
  codegen_->MoveLocation(tmp2, loc2, tmp2_type);
  if (use_fp_tmp2) {
    codegen_->MoveLocation(loc2, tmp, tmp_type);
  } else {
    // We cannot use `Stored()` or `Storew()` via `MoveLocation()` because we have
    // no more scratch registers available. Use `Sd()` or `Sw()` explicitly.
    DCHECK(IsInt<12>(index2));
    if (double_slot) {
      __ St_D(tmp.AsRegister<XRegister>(), SP, index2);
    } else {
      __ St_W(tmp.AsRegister<XRegister>(), SP, index2);
    }
    srs.FreeXRegister(tmp.AsRegister<XRegister>());  // Free a temporary for `MoveLocation()`.
  }
  codegen_->MoveLocation(loc1, tmp2, tmp2_type);
}

InstructionCodeGeneratorLOONGARCH64::InstructionCodeGeneratorLOONGARCH64(HGraph* graph,
                                                                 CodeGeneratorLOONGARCH64* codegen)
    : InstructionCodeGenerator(graph, codegen),
      assembler_(codegen->GetAssembler()),
      codegen_(codegen) {}

void InstructionCodeGeneratorLOONGARCH64::GenerateClassInitializationCheck(
    SlowPathCodeLOONGARCH64* slow_path, XRegister class_reg) {
  UNUSED(slow_path);
  UNUSED(class_reg);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateBitstringTypeCheckCompare(
    HTypeCheckInstruction* instruction, XRegister temp) {
  UNUSED(instruction);
  UNUSED(temp);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateSuspendCheck(HSuspendCheck* instruction,
                                                           HBasicBlock* successor) {
  if (codegen_->CanUseImplicitSuspendCheck()) {
    LOG(FATAL) << "Unimplemented ImplicitSuspendCheck";
    return;
  }

  SuspendCheckSlowPathLOONGARCH64* slow_path =
      down_cast<SuspendCheckSlowPathLOONGARCH64*>(instruction->GetSlowPath());

  if (slow_path == nullptr) {
    slow_path =
        new (codegen_->GetScopedAllocator()) SuspendCheckSlowPathLOONGARCH64(instruction, successor);
    instruction->SetSlowPath(slow_path);
    codegen_->AddSlowPath(slow_path);
    if (successor != nullptr) {
      DCHECK(successor->IsLoopHeader());
    }
  } else {
    DCHECK_EQ(slow_path->GetSuccessor(), successor);
  }

  ScratchRegisterScope srs(GetAssembler());
  XRegister tmp = srs.AllocateXRegister();
  __ Load_H(tmp, TR, Thread::ThreadFlagsOffset<kLoongarch64PointerSize>().SizeValue());
  // Shift out other bits. Use an instruction that can be 16-bit with the "C" Standard Extension.
  //__ Slli_d(tmp, tmp, CLZ(static_cast<uint64_t>(Thread::SuspendOrCheckpointRequestFlags())));
  if (successor == nullptr) {
    __ Bnez(tmp, slow_path->GetEntryLabel());
    __ Bind(slow_path->GetReturnLabel());
  } else {
    __ Beqz(tmp, codegen_->GetLabelOf(successor));
    __ B(slow_path->GetEntryLabel());
    // slow_path will return to GetLabelOf(successor).
  }
}

void InstructionCodeGeneratorLOONGARCH64::GenerateMinMaxInt(LocationSummary* locations, bool is_min) {
  UNUSED(locations);
  UNUSED(is_min);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateMinMaxFP(LocationSummary* locations,
                                                       bool is_min,
                                                       DataType::Type type) {
  UNUSED(locations);
  UNUSED(is_min);
  UNUSED(type);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateMinMax(HBinaryOperation* instruction, bool is_min) {
  UNUSED(instruction);
  UNUSED(is_min);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateReferenceLoadOneRegister(
    HInstruction* instruction,
    Location out,
    uint32_t offset,
    Location maybe_temp,
    ReadBarrierOption read_barrier_option) {
  UNUSED(instruction);
  UNUSED(out);
  UNUSED(offset);
  UNUSED(maybe_temp);
  UNUSED(read_barrier_option);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateReferenceLoadTwoRegisters(
    HInstruction* instruction,
    Location out,
    Location obj,
    uint32_t offset,
    Location maybe_temp,
    ReadBarrierOption read_barrier_option) {
  UNUSED(instruction);
  UNUSED(out);
  UNUSED(offset);
  UNUSED(maybe_temp);
  UNUSED(read_barrier_option);
  UNUSED(obj);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateGcRootFieldLoad(HInstruction* instruction,
                                                              Location root,
                                                              XRegister obj,
                                                              uint32_t offset,
                                                              ReadBarrierOption read_barrier_option,
                                                              Loongarch64Label* label_low) {
  UNUSED(instruction);
  UNUSED(root);
  UNUSED(obj);
  UNUSED(offset);
  UNUSED(read_barrier_option);
  UNUSED(label_low);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateTestAndBranch(HInstruction* instruction,
                                                            size_t condition_input_index,
                                                            Loongarch64Label* true_target,
                                                            Loongarch64Label* false_target) {
  HInstruction* cond = instruction->InputAt(condition_input_index);

  if (true_target == nullptr && false_target == nullptr) {
    // Nothing to do. The code always falls through.
    return;
  } else if (cond->IsIntConstant()) {
    // Constant condition, statically compared against "true" (integer value 1).
    if (cond->AsIntConstant()->IsTrue()) {
      if (true_target != nullptr) {
        __ B(true_target);
      }
    } else {
      DCHECK(cond->AsIntConstant()->IsFalse()) << cond->AsIntConstant()->GetValue();
      if (false_target != nullptr) {
        __ B(false_target);
      }
    }
    return;
  }

  // The following code generates these patterns:
  //  (1) true_target == nullptr && false_target != nullptr
  //        - opposite condition true => branch to false_target
  //  (2) true_target != nullptr && false_target == nullptr
  //        - condition true => branch to true_target
  //  (3) true_target != nullptr && false_target != nullptr
  //        - condition true => branch to true_target
  //        - branch to false_target
  if (IsBooleanValueOrMaterializedCondition(cond)) {
    // The condition instruction has been materialized, compare the output to 0.
    Location cond_val = instruction->GetLocations()->InAt(condition_input_index);
    DCHECK(cond_val.IsRegister());
    if (true_target == nullptr) {
      __ Beqz(cond_val.AsRegister<XRegister>(), false_target);
    } else {
      __ Bnez(cond_val.AsRegister<XRegister>(), true_target);
    }
  } else {
    // The condition instruction has not been materialized, use its inputs as
    // the comparison and its condition as the branch condition.
    HCondition* condition = cond->AsCondition();
    DataType::Type type = condition->InputAt(0)->GetType();
    LocationSummary* locations = condition->GetLocations();
    IfCondition if_cond = condition->GetCondition();
    Loongarch64Label* branch_target = true_target;

    if (true_target == nullptr) {
      if_cond = condition->GetOppositeCondition();
      branch_target = false_target;
    }

    switch (type) {
      case DataType::Type::kFloat32:
      case DataType::Type::kFloat64:
        GenerateFpCondition(if_cond, condition->IsGtBias(), type, locations, branch_target);
        break;
      default:
        // Integral types and reference equality.
        GenerateIntLongCompareAndBranch(if_cond, locations, branch_target);
        break;
    }
  }

  // If neither branch falls through (case 3), the conditional branch to `true_target`
  // was already emitted (case 2) and we need to emit a jump to `false_target`.
  if (true_target != nullptr && false_target != nullptr) {
    __ B(false_target);
  }
}

void InstructionCodeGeneratorLOONGARCH64::DivRemOneOrMinusOne(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::DivRemByPowerOfTwo(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateDivRemWithAnyConstant(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateDivRemIntegral(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateIntLongCondition(IfCondition cond,
                                                             LocationSummary* locations) {
  XRegister rd = locations->Out().AsRegister<XRegister>();
  XRegister rs1 = locations->InAt(0).AsRegister<XRegister>();
  Location rs2_location = locations->InAt(1);
  bool use_imm = rs2_location.IsConstant();
  int64_t imm = use_imm ? CodeGenerator::GetInt64ValueOf(rs2_location.GetConstant()) : 0;
  XRegister rs2 = use_imm ? kNoXRegister : rs2_location.AsRegister<XRegister>();
  switch (cond) {
    case kCondEQ:
    case kCondNE:
      if (!use_imm) {
        __ Sub_d(rd, rs1, rs2);  // SUB is OK here even for 32-bit comparison.
      } else if (imm != 0) {
        DCHECK(IsInt<12>(-imm));
        __ Addi_D(rd, rs1, -imm);  // ADDI is OK here even for 32-bit comparison.
      }  // else test `rs1` directly without subtraction for `use_imm && imm == 0`.
      if (cond == kCondEQ) {
        __ Sltui(rd, (use_imm && imm == 0) ? rs1 : rd, 1); // seqz
      } else {
        __ Sltu(rd, Zero, (use_imm && imm == 0) ? rs1 : rd); // snez
      }
      break;

    case kCondLT:
    case kCondGE:
      if (use_imm) {
        DCHECK(IsInt<12>(imm));
        __ Slti(rd, rs1, imm);
      } else {
        __ Slt(rd, rs1, rs2);
      }
      if (cond == kCondGE) {
        // Calculate `rs1 >= rhs` as `!(rs1 < rhs)` since there's only the SLT but no SGE.
        __ Xori(rd, rd, 1);
      }
      break;

    case kCondLE:
    case kCondGT:
      if (use_imm) {
        // Calculate `rs1 <= imm` as `rs1 < imm + 1`.
        DCHECK(IsInt<12>(imm + 1));  // The value that overflows would fail this check.
        __ Slti(rd, rs1, imm + 1);
      } else {
        __ Slt(rd, rs2, rs1);
      }
      if ((cond == kCondGT) == use_imm) {
        // Calculate `rs1 > imm` as `!(rs1 < imm + 1)` and calculate
        // `rs1 <= rs2` as `!(rs2 < rs1)` since there's only the SLT but no SGE.
        __ Xori(rd, rd, 1);
      }
      break;

    case kCondB:
    case kCondAE:
      if (use_imm) {
        // Sltiu sign-extends its 12-bit immediate operand before the comparison
        // and thus lets us compare directly with unsigned values in the ranges
        // [0, 0x7ff] and [0x[ffffffff]fffff800, 0x[ffffffff]ffffffff].
        DCHECK(IsInt<12>(imm));
        __ Sltui(rd, rs1, imm);
      } else {
        __ Sltu(rd, rs1, rs2);
      }
      if (cond == kCondAE) {
        // Calculate `rs1 AE rhs` as `!(rs1 B rhs)` since there's only the SLTU but no SGEU.
        __ Xori(rd, rd, 1);
      }
      break;

    case kCondBE:
    case kCondA:
      if (use_imm) {
        // Calculate `rs1 BE imm` as `rs1 B imm + 1`.
        // Sltiu sign-extends its 12-bit immediate operand before the comparison
        // and thus lets us compare directly with unsigned values in the ranges
        // [0, 0x7ff] and [0x[ffffffff]fffff800, 0x[ffffffff]ffffffff].
        DCHECK(IsInt<12>(imm + 1));  // The value that overflows would fail this check.
        __ Sltui(rd, rs1, imm + 1);
      } else {
        __ Sltu(rd, rs2, rs1);
      }
      if ((cond == kCondA) == use_imm) {
        // Calculate `rs1 A imm` as `!(rs1 B imm + 1)` and calculate
        // `rs1 BE rs2` as `!(rs2 B rs1)` since there's only the SLTU but no SGEU.
        __ Xori(rd, rd, 1);
      }
      break;
  }
}

void InstructionCodeGeneratorLOONGARCH64::GenerateIntLongCompareAndBranch(IfCondition cond,
                                                                      LocationSummary* locations,
                                                                      Loongarch64Label* label) {
  XRegister left = locations->InAt(0).AsRegister<XRegister>();
  Location right_location = locations->InAt(1);
  if (right_location.IsConstant()) {
    DCHECK_EQ(CodeGenerator::GetInt64ValueOf(right_location.GetConstant()), 0);
    switch (cond) {
      case kCondEQ:
      case kCondBE:  // <= 0 if zero
        __ Beqz(left, label);
        break;
      case kCondNE:
      case kCondA:  // > 0 if non-zero
        __ Bnez(left, label);
        break;
      case kCondLT:
        __ Blt(left, Zero, label);
        break;
      case kCondGE:
        __ Bge(left, Zero, label);
        break;
      case kCondLE:
        __ Bge(Zero, left, label);
        break;
      case kCondGT:
        __ Blt(Zero, left, label);
        break;
      case kCondB:  // always false
        break;
      case kCondAE:  // always true
        __ B(label);
        break;
    }
  } else {
    XRegister right_reg = right_location.AsRegister<XRegister>();
    switch (cond) {
      case kCondEQ:
        __ Beq(left, right_reg, label);
        break;
      case kCondNE:
        __ Bne(left, right_reg, label);
        break;
      case kCondLT:
        __ Blt(left, right_reg, label);
        break;
      case kCondGE:
        __ Bge(left, right_reg, label);
        break;
      case kCondLE:
        __ Bge(right_reg, left, label);
        break;
      case kCondGT:
        __ Blt(right_reg, left, label);
        break;
      case kCondB:
        __ Bltu(left, right_reg, label);
        break;
      case kCondAE:
        __ Bgeu(left, right_reg, label);
        break;
      case kCondBE:
        __ Bgeu(right_reg, left, label);
        break;
      case kCondA:
        __ Bltu(right_reg, left, label);
        break;
    }
  }
}

void InstructionCodeGeneratorLOONGARCH64::GenerateFpCondition(IfCondition cond,
                                                        bool gt_bias,
                                                        DataType::Type type,
                                                        LocationSummary* locations,
                                                        Loongarch64Label* label) {
  UNUSED(cond);
  UNUSED(gt_bias);
  UNUSED(type);
  UNUSED(locations);
  UNUSED(label);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::HandleGoto(HInstruction* instruction,
                                                 HBasicBlock* successor) {
  if (successor->IsExitBlock()) {
    DCHECK(instruction->GetPrevious()->AlwaysThrows());
    return;  // no code needed
  }

  HBasicBlock* block = instruction->GetBlock();
  HInstruction* previous = instruction->GetPrevious();
  HLoopInformation* info = block->GetLoopInformation();

  if (info != nullptr && info->IsBackEdge(*block) && info->HasSuspendCheck()) {
    codegen_->MaybeIncrementHotness(/*is_frame_entry=*/ false);
    GenerateSuspendCheck(info->GetSuspendCheck(), successor);
    return;  // `GenerateSuspendCheck()` emitted the jump.
  }
  if (block->IsEntryBlock() && previous != nullptr && previous->IsSuspendCheck()) {
    GenerateSuspendCheck(previous->AsSuspendCheck(), nullptr);
  }
  if (!codegen_->GoesToNextBlock(block, successor)) {
    __ B(codegen_->GetLabelOf(successor));
  }
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenPackedSwitchWithCompares(XRegister reg,
                                                                  int32_t lower_bound,
                                                                  uint32_t num_entries,
                                                                  HBasicBlock* switch_block,
                                                                  HBasicBlock* default_block) {
  UNUSED(reg);
  UNUSED(lower_bound);
  UNUSED(num_entries);
  UNUSED(switch_block);
  UNUSED(default_block);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenTableBasedPackedSwitch(XRegister reg,
                                                                int32_t lower_bound,
                                                                uint32_t num_entries,
                                                                HBasicBlock* switch_block,
                                                                HBasicBlock* default_block) {
  UNUSED(reg);
  UNUSED(lower_bound);
  UNUSED(num_entries);
  UNUSED(switch_block);
  UNUSED(default_block);
  LOG(FATAL) << "Unimplemented";
}

int32_t InstructionCodeGeneratorLOONGARCH64::VecAddress(LocationSummary* locations,
                                                    size_t size,
                                                    /* out */ XRegister* adjusted_base) {
  UNUSED(locations);
  UNUSED(size);
  UNUSED(adjusted_base);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void InstructionCodeGeneratorLOONGARCH64::GenConditionalMove(HSelect* select) {
  UNUSED(select);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::HandleBinaryOp(HBinaryOperation* instruction) {
  DCHECK_EQ(instruction->InputCount(), 2u);
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DataType::Type type = instruction->GetResultType();
  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      locations->SetInAt(0, Location::RequiresRegister());
      HInstruction* right = instruction->InputAt(1);
      bool can_use_imm = false;
      if (right->IsConstant()) {
        int64_t imm = CodeGenerator::GetInt64ValueOf(right->AsConstant());
        can_use_imm = IsInt<12>(instruction->IsSub() ? -imm : imm);
      }
      if (can_use_imm) {
        locations->SetInAt(1, Location::ConstantLocation(right->AsConstant()));
      } else {
        locations->SetInAt(1, Location::RequiresRegister());
      }
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }

    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;

    default:
      LOG(FATAL) << "Unexpected " << instruction->DebugName() << " type " << type;
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorLOONGARCH64::HandleBinaryOp(HBinaryOperation* instruction) {
  DataType::Type type = instruction->GetType();
  LocationSummary* locations = instruction->GetLocations();

  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      XRegister rd = locations->Out().AsRegister<XRegister>();
      XRegister rs1 = locations->InAt(0).AsRegister<XRegister>();
      Location rs2_location = locations->InAt(1);

      bool use_imm = rs2_location.IsConstant();
      XRegister rs2 = use_imm ? kNoXRegister : rs2_location.AsRegister<XRegister>();
      int64_t imm = use_imm ? CodeGenerator::GetInt64ValueOf(rs2_location.GetConstant()) : 0;

      if (instruction->IsAnd()) {
        if (use_imm) {
          __ Andi(rd, rs1, imm);
        } else {
          __ And(rd, rs1, rs2);
        }
      } else if (instruction->IsOr()) {
        if (use_imm) {
          __ Ori(rd, rs1, imm);
        } else {
          __ Or(rd, rs1, rs2);
        }
      } else if (instruction->IsXor()) {
        if (use_imm) {
          __ Xori(rd, rs1, imm);
        } else {
          __ Xor(rd, rs1, rs2);
        }
      } else {
        DCHECK(instruction->IsAdd() || instruction->IsSub());
        if (type == DataType::Type::kInt32) {
          if (use_imm) {
            __ Addi_W(rd, rs1, instruction->IsSub() ? -imm : imm);
          } else if (instruction->IsAdd()) {
            __ Add_w(rd, rs1, rs2);
          } else {
            DCHECK(instruction->IsSub());
            __ Sub_w(rd, rs1, rs2);
          }
        } else {
          if (use_imm) {
            __ Addi_D(rd, rs1, instruction->IsSub() ? -imm : imm);
          } else if (instruction->IsAdd()) {
            __ Add_d(rd, rs1, rs2);
          } else {
            DCHECK(instruction->IsSub());
            __ Sub_d(rd, rs1, rs2);
          }
        }
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected binary operation type " << type;
      UNREACHABLE();
  }
}

void LocationsBuilderLOONGARCH64::HandleCondition(HCondition* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  switch (instruction->InputAt(0)->GetType()) {
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetInAt(1, Location::RequiresFpuRegister());
      break;

    default: {
      locations->SetInAt(0, Location::RequiresRegister());
      HInstruction* rhs = instruction->InputAt(1);
      bool use_imm = false;
      if (rhs->IsConstant()) {
        int64_t imm = CodeGenerator::GetInt64ValueOf(rhs->AsConstant());
        if (instruction->IsEmittedAtUseSite()) {
          // For `HIf`, materialize all non-zero constants with an `HParallelMove`.
          // Note: For certain constants and conditions, the code could be improved.
          // For example, 2048 takes two instructions to materialize but the negative
          // -2048 could be embedded in ADDI for EQ/NE comparison.
          use_imm = (imm == 0);
        } else {
          // Constants that cannot be embedded in an instruction's 12-bit immediate shall be
          // materialized with an `HParallelMove`. This simplifies the code and avoids cases
          // with arithmetic overflow. Adjust the `imm` if needed for a particular instruction.
          switch (instruction->GetCondition()) {
            case kCondEQ:
            case kCondNE:
              imm = -imm; // ADDI with negative immediate (there is no SUBI).
              break;
            case kCondLE:
            case kCondGT:
            case kCondBE:
            case kCondA:
              imm += 1; // SLTI/SLTIU with adjusted immediate (there is no SLEI/SLEIU).
              break;
            default:
              break;
          }
          // Constants that cannot be embedded in an instruction's 12-bit immediate shall be
          // materialized. This simplifies the code and avoids cases with arithmetic overflow.
          use_imm = IsInt<12>(imm);
        }
      }
      if (use_imm) {
        locations->SetInAt(1, Location::ConstantLocation(rhs->AsConstant()));
      } else {
        locations->SetInAt(1, Location::RequiresRegister());
      }
      break;
    }
  }
  if (!instruction->IsEmittedAtUseSite()) {
    locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
  }
}

void InstructionCodeGeneratorLOONGARCH64::HandleCondition(HCondition* instruction) {
  if (instruction->IsEmittedAtUseSite()) {
    return;
  }

  DataType::Type type = instruction->InputAt(0)->GetType();
  LocationSummary* locations = instruction->GetLocations();
  switch (type) {
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      GenerateFpCondition(instruction->GetCondition(), instruction->IsGtBias(), type, locations);
      return;
    default:
      // Integral types and reference equality.
      GenerateIntLongCondition(instruction->GetCondition(), locations);
      return;
  }
}

void LocationsBuilderLOONGARCH64::HandleShift(HBinaryOperation* instruction) {
  DCHECK(instruction->IsShl() ||
         instruction->IsShr() ||
         instruction->IsUShr() ||
         instruction->IsRor());

  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DataType::Type type = instruction->GetResultType();
  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetInAt(1, Location::RegisterOrConstant(instruction->InputAt(1)));
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    }
    default:
      LOG(FATAL) << "Unexpected shift type " << type;
      UNREACHABLE();
  }
}

void InstructionCodeGeneratorLOONGARCH64::HandleShift(HBinaryOperation* instruction) {
  DCHECK(instruction->IsShl() ||
         instruction->IsShr() ||
         instruction->IsUShr() ||
         instruction->IsRor());
  LocationSummary* locations = instruction->GetLocations();
  DataType::Type type = instruction->GetType();

  switch (type) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64: {
      XRegister rd = locations->Out().AsRegister<XRegister>();
      XRegister rs1 = locations->InAt(0).AsRegister<XRegister>();
      Location rs2_location = locations->InAt(1);

      if (rs2_location.IsConstant()) {
        int64_t imm = CodeGenerator::GetInt64ValueOf(rs2_location.GetConstant());
        uint32_t shamt =
            imm & (type == DataType::Type::kInt32 ? kMaxIntShiftDistance : kMaxLongShiftDistance);

        if (shamt == 0) {
          if (rd != rs1) {
            __ Move(rd, rs1);
          }
        } else if (type == DataType::Type::kInt32) {
          if (instruction->IsShl()) {
            __ Slli_w(rd, rs1, shamt);
          } else if (instruction->IsShr()) {
            __ Srai_w(rd, rs1, shamt);
          } else if (instruction->IsUShr()) {
            __ Srli_w(rd, rs1, shamt);
          } else {
            ScratchRegisterScope srs(GetAssembler());
            XRegister tmp = srs.AllocateXRegister();
            __ Srli_w(tmp, rs1, shamt);
            __ Slli_w(rd, rs1, 32 - shamt);
            __ Or(rd, rd, tmp);
          }
        } else {
          if (instruction->IsShl()) {
            __ Slli_d(rd, rs1, shamt);
          } else if (instruction->IsShr()) {
            __ Srai_d(rd, rs1, shamt);
          } else if (instruction->IsUShr()) {
            __ Srli_d(rd, rs1, shamt);
          } else {
            ScratchRegisterScope srs(GetAssembler());
            XRegister tmp = srs.AllocateXRegister();
            __ Srli_d(tmp, rs1, shamt);
            __ Slli_d(rd, rs1, 64 - shamt);
            __ Or(rd, rd, tmp);
          }
        }
      } else {
        XRegister rs2 = rs2_location.AsRegister<XRegister>();
        if (type == DataType::Type::kInt32) {
          if (instruction->IsShl()) {
            __ Sll_w(rd, rs1, rs2);
          } else if (instruction->IsShr()) {
            __ Sra_w(rd, rs1, rs2);
          } else if (instruction->IsUShr()) {
            __ Srl_w(rd, rs1, rs2);
          } else {
            ScratchRegisterScope srs(GetAssembler());
            XRegister tmp = srs.AllocateXRegister();
            XRegister tmp2 = srs.AllocateXRegister();
            __ Srl_w(tmp, rs1, rs2);
            __ Sub_w(tmp2, Zero, rs2);  // tmp2 = -rs; we can use this instead of `32 - rs`
            __ Sll_w(rd, rs1, tmp2);   // because only low 5 bits are used for SLL_W.
            __ Or(rd, rd, tmp);
          }
        } else {
          if (instruction->IsShl()) {
            __ Sll_d(rd, rs1, rs2);
          } else if (instruction->IsShr()) {
            __ Sra_d(rd, rs1, rs2);
          } else if (instruction->IsUShr()) {
            __ Srl_d(rd, rs1, rs2);
          } else {
            ScratchRegisterScope srs(GetAssembler());
            XRegister tmp = srs.AllocateXRegister();
            XRegister tmp2 = srs.AllocateXRegister();
            __ Srl_d(tmp, rs1, rs2);
            __ Sub_d(tmp2, Zero, rs2);  // tmp2 = -rs; we can use this instead of `64 - rs`
            __ Sll_d(rd, rs1, tmp2);    // because only low 6 bits are used for SLL.
            __ Or(rd, rd, tmp);
          }
        }
      }
      break;
    }
    default:
      LOG(FATAL) << "Unexpected shift operation type " << type;
  }
}

void LocationsBuilderLOONGARCH64::HandleFieldSet(HInstruction* instruction,
                                             const FieldInfo& field_info) {
  UNUSED(instruction);
  UNUSED(field_info);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::HandleFieldSet(HInstruction* instruction,
                                                     const FieldInfo& field_info,
                                                     bool value_can_be_null) {
  UNUSED(instruction);
  UNUSED(field_info);
  UNUSED(value_can_be_null);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::HandleFieldGet(HInstruction* instruction,
                                             const FieldInfo& field_info) {
  UNUSED(instruction);
  UNUSED(field_info);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::HandleFieldGet(HInstruction* instruction,
                                                     const FieldInfo& field_info) {
  UNUSED(instruction);
  UNUSED(field_info);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitAbove(HAbove* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitAbove(HAbove* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitAboveOrEqual(HAboveOrEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitAboveOrEqual(HAboveOrEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitAbs(HAbs* abs) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(abs);
  switch (abs->GetResultType()) {
    case DataType::Type::kInt32:
    case DataType::Type::kInt64:
      locations->SetInAt(0, Location::RequiresRegister());
      locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
      break;
    case DataType::Type::kFloat32:
    case DataType::Type::kFloat64:
      locations->SetInAt(0, Location::RequiresFpuRegister());
      locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
      break;
    default:
      LOG(FATAL) << "Unexpected abs type " << abs->GetResultType();
  }
}

void InstructionCodeGeneratorLOONGARCH64::VisitAbs(HAbs* abs) {
  LocationSummary* locations = abs->GetLocations();
  switch (abs->GetResultType()) {
    case DataType::Type::kInt32: {
      XRegister in = locations->InAt(0).AsRegister<XRegister>();
      XRegister out = locations->Out().AsRegister<XRegister>();
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      __ Srai_w(tmp, in, 31);
      __ Xor(out, in, tmp);
      __ Sub_w(out, out, tmp);
      break;
    }
    case DataType::Type::kInt64: {
      XRegister in = locations->InAt(0).AsRegister<XRegister>();
      XRegister out = locations->Out().AsRegister<XRegister>();
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      __ Srai_d(tmp, in, 63);
      __ Xor(out, in, tmp);
      __ Sub_d(out, out, tmp);
      break;
    }
    case DataType::Type::kFloat32: {
      LOG(FATAL) << "Unexpected abs type kFloat32.";
      break;
    }
    case DataType::Type::kFloat64: {
      LOG(FATAL) << "Unexpected abs type kFloat64.";
      break;
    }
    default:
      LOG(FATAL) << "Unexpected abs type " << abs->GetResultType();
  }
}

void LocationsBuilderLOONGARCH64::VisitAdd(HAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitAdd(HAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitAnd(HAnd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitAnd(HAnd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitArrayGet(HArrayGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitArrayGet(HArrayGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitArrayLength(HArrayLength* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorLOONGARCH64::VisitArrayLength(HArrayLength* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  uint32_t offset = CodeGenerator::GetArrayLengthOffset(instruction);
  XRegister obj = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  __ Load_WU(out, obj, offset);  // Unsigned for string length; does not matter for other arrays.
  codegen_->MaybeRecordImplicitNullCheck(instruction);
  // Mask out compression flag from String's array length.
  if (mirror::kUseStringCompression && instruction->IsStringLength()) {
    __ Srli_d(out, out, 1u);
  }
}

void LocationsBuilderLOONGARCH64::VisitArraySet(HArraySet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitArraySet(HArraySet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitBelow(HBelow* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitBelow(HBelow* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitBelowOrEqual(HBelowOrEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitBelowOrEqual(HBelowOrEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitBooleanNot(HBooleanNot* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void InstructionCodeGeneratorLOONGARCH64::VisitBooleanNot(HBooleanNot* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  __ Xori(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>(), 1);
}

void LocationsBuilderLOONGARCH64::VisitBoundsCheck(HBoundsCheck* instruction) {
  RegisterSet caller_saves = RegisterSet::Empty();
  InvokeRuntimeCallingConvention calling_convention;
  caller_saves.Add(Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  caller_saves.Add(Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  LocationSummary* locations = codegen_->CreateThrowingSlowPathLocations(instruction, caller_saves);

  HInstruction* index = instruction->InputAt(0);
  HInstruction* length = instruction->InputAt(1);

  bool const_index = false;
  bool const_length = false;

  if (length->IsConstant()) {
    if (index->IsConstant()) {
      const_index = true;
      const_length = true;
    } else {
      int32_t length_value = length->AsIntConstant()->GetValue();
      if (length_value == 0 || length_value == 1) {
        const_length = true;
      }
    }
  } else if (index->IsConstant()) {
    int32_t index_value = index->AsIntConstant()->GetValue();
    if (index_value <= 0) {
      const_index = true;
    }
  }

  locations->SetInAt(
      0,
      const_index ? Location::ConstantLocation(index->AsConstant()) : Location::RequiresRegister());
  locations->SetInAt(1,
                     const_length ? Location::ConstantLocation(length->AsConstant()) :
                                    Location::RequiresRegister());
}

void InstructionCodeGeneratorLOONGARCH64::VisitBoundsCheck(HBoundsCheck* instruction) {
  LocationSummary* locations = instruction->GetLocations();
  Location index_loc = locations->InAt(0);
  Location length_loc = locations->InAt(1);

  if (length_loc.IsConstant()) {
    int32_t length = length_loc.GetConstant()->AsIntConstant()->GetValue();
    if (index_loc.IsConstant()) {
      int32_t index = index_loc.GetConstant()->AsIntConstant()->GetValue();
      if (index < 0 || index >= length) {
        BoundsCheckSlowPathLOONGARCH64* slow_path =
            new (codegen_->GetScopedAllocator()) BoundsCheckSlowPathLOONGARCH64(instruction);
        codegen_->AddSlowPath(slow_path);
        __ B(slow_path->GetEntryLabel());
      } else {
        // Nothing to be done.
      }
      return;
    }

    BoundsCheckSlowPathLOONGARCH64* slow_path =
        new (codegen_->GetScopedAllocator()) BoundsCheckSlowPathLOONGARCH64(instruction);
    codegen_->AddSlowPath(slow_path);
    XRegister index = index_loc.AsRegister<XRegister>();
    if (length == 0) {
      __ B(slow_path->GetEntryLabel());
    } else {
      DCHECK_EQ(length, 1);
      __ Bnez(index, slow_path->GetEntryLabel());
    }
  } else {
    XRegister length = length_loc.AsRegister<XRegister>();
    BoundsCheckSlowPathLOONGARCH64* slow_path =
        new (codegen_->GetScopedAllocator()) BoundsCheckSlowPathLOONGARCH64(instruction);
    codegen_->AddSlowPath(slow_path);
    if (index_loc.IsConstant()) {
      int32_t index = index_loc.GetConstant()->AsIntConstant()->GetValue();
      if (index < 0) {
        __ B(slow_path->GetEntryLabel());
      } else {
        DCHECK_EQ(index, 0);
        __ Bge(Zero, length, slow_path->GetEntryLabel());
      }
    } else {
      XRegister index = index_loc.AsRegister<XRegister>();
      __ Bgeu(index, length, slow_path->GetEntryLabel());
    }
  }
}

void LocationsBuilderLOONGARCH64::VisitBoundType([[maybe_unused]] HBoundType* instruction) {
  // Nothing to do, this should be removed during prepare for register allocator.
  LOG(FATAL) << "Unreachable";
}

void InstructionCodeGeneratorLOONGARCH64::VisitBoundType([[maybe_unused]] HBoundType* instruction) {
  // Nothing to do, this should be removed during prepare for register allocator.
  LOG(FATAL) << "Unreachable";
}

void LocationsBuilderLOONGARCH64::VisitCheckCast(HCheckCast* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitCheckCast(HCheckCast* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitClassTableGet(HClassTableGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

static int32_t GetExceptionTlsOffset() {
  return Thread::ExceptionOffset<kLoongarch64PointerSize>().Int32Value();
}

void InstructionCodeGeneratorLOONGARCH64::VisitClassTableGet(HClassTableGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitClearException(HClearException* instruction) {
  new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
}

void InstructionCodeGeneratorLOONGARCH64::VisitClearException(
    [[maybe_unused]] HClearException* instruction) {
  __ Store_D(Zero, TR, GetExceptionTlsOffset());
}

void LocationsBuilderLOONGARCH64::VisitClinitCheck(HClinitCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitClinitCheck(HClinitCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitCompare(HCompare* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitCompare(HCompare* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitConstructorFence(HConstructorFence* instruction) {
  instruction->SetLocations(nullptr);
}

void InstructionCodeGeneratorLOONGARCH64::VisitConstructorFence(
    [[maybe_unused]] HConstructorFence* instruction) {
  codegen_->GenerateMemoryBarrier(MemBarrierKind::kStoreStore);
}

void LocationsBuilderLOONGARCH64::VisitCurrentMethod(HCurrentMethod* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(Location::RegisterLocation(kArtMethodRegister));
}

void InstructionCodeGeneratorLOONGARCH64::VisitCurrentMethod(
    [[maybe_unused]] HCurrentMethod* instruction) {
  // Nothing to do, the method is already at its location.
}

void LocationsBuilderLOONGARCH64::VisitShouldDeoptimizeFlag(HShouldDeoptimizeFlag* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(Location::RequiresRegister());
}

void InstructionCodeGeneratorLOONGARCH64::VisitShouldDeoptimizeFlag(
    HShouldDeoptimizeFlag* instruction) {
  __ Load_W(instruction->GetLocations()->Out().AsRegister<XRegister>(),
           SP,
           codegen_->GetStackOffsetOfShouldDeoptimizeFlag());
}

void LocationsBuilderLOONGARCH64::VisitDeoptimize(HDeoptimize* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitDeoptimize(HDeoptimize* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitDiv(HDiv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitDiv(HDiv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitDivZeroCheck(HDivZeroCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitDivZeroCheck(HDivZeroCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitDoubleConstant(HDoubleConstant* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(instruction));
}

void InstructionCodeGeneratorLOONGARCH64::VisitDoubleConstant([[maybe_unused]] HDoubleConstant* instruction) {}

void LocationsBuilderLOONGARCH64::VisitEqual(HEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitEqual(HEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitExit(HExit* instruction) {
  instruction->SetLocations(nullptr);
}

void InstructionCodeGeneratorLOONGARCH64::VisitExit([[maybe_unused]] HExit* instruction) {}

void LocationsBuilderLOONGARCH64::VisitFloatConstant(HFloatConstant* instruction) {
  LocationSummary* locations =
      new (GetGraph()->GetAllocator()) LocationSummary(instruction, LocationSummary::kNoCall);
  locations->SetOut(Location::ConstantLocation(instruction));
}

void InstructionCodeGeneratorLOONGARCH64::VisitFloatConstant([[maybe_unused]] HFloatConstant* instruction) {
  // Will be generated at use site.
}

void LocationsBuilderLOONGARCH64::VisitGoto(HGoto* instruction) {
  instruction->SetLocations(nullptr);
}

void InstructionCodeGeneratorLOONGARCH64::VisitGoto(HGoto* instruction) {
  HandleGoto(instruction, instruction->GetSuccessor());
}

void LocationsBuilderLOONGARCH64::VisitGreaterThan(HGreaterThan* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitGreaterThan(HGreaterThan* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitGreaterThanOrEqual(HGreaterThanOrEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitGreaterThanOrEqual(HGreaterThanOrEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitIf(HIf* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  if (IsBooleanValueOrMaterializedCondition(instruction->InputAt(0))) {
    locations->SetInAt(0, Location::RequiresRegister());
  }
}

void InstructionCodeGeneratorLOONGARCH64::VisitIf(HIf* instruction) {
  HBasicBlock* true_successor = instruction->IfTrueSuccessor();
  HBasicBlock* false_successor = instruction->IfFalseSuccessor();
  Loongarch64Label* true_target = codegen_->GoesToNextBlock(instruction->GetBlock(), true_successor)
      ? nullptr
      : codegen_->GetLabelOf(true_successor);
  Loongarch64Label* false_target = codegen_->GoesToNextBlock(instruction->GetBlock(), false_successor)
      ? nullptr
      : codegen_->GetLabelOf(false_successor);
  GenerateTestAndBranch(instruction, /* condition_input_index= */ 0, true_target, false_target);
}

void LocationsBuilderLOONGARCH64::VisitInstanceFieldGet(HInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInstanceFieldGet(HInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitInstanceFieldSet(HInstanceFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInstanceFieldSet(HInstanceFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitInstanceOf(HInstanceOf* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInstanceOf(HInstanceOf* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitIntConstant(HIntConstant* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetOut(Location::ConstantLocation(instruction));
}

void InstructionCodeGeneratorLOONGARCH64::VisitIntConstant([[maybe_unused]] HIntConstant* instruction) {
  // Will be generated at use site.
}

void LocationsBuilderLOONGARCH64::VisitIntermediateAddress(HIntermediateAddress* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitIntermediateAddress(HIntermediateAddress* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitInvokeUnresolved(HInvokeUnresolved* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInvokeUnresolved(HInvokeUnresolved* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitInvokeInterface(HInvokeInterface* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInvokeInterface(HInvokeInterface* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitInvokeStaticOrDirect(HInvokeStaticOrDirect* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInvokeStaticOrDirect(
    HInvokeStaticOrDirect* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitInvokeVirtual(HInvokeVirtual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInvokeVirtual(HInvokeVirtual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitInvokePolymorphic(HInvokePolymorphic* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInvokePolymorphic(HInvokePolymorphic* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitInvokeCustom(HInvokeCustom* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitInvokeCustom(HInvokeCustom* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitLessThan(HLessThan* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitLessThan(HLessThan* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitLessThanOrEqual(HLessThanOrEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitLessThanOrEqual(HLessThanOrEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitLoadClass(HLoadClass* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitLoadClass(HLoadClass* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitLoadException(HLoadException* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitLoadException(HLoadException* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitLoadMethodHandle(HLoadMethodHandle* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitLoadMethodHandle(HLoadMethodHandle* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitLoadMethodType(HLoadMethodType* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitLoadMethodType(HLoadMethodType* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitLoadString(HLoadString* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitLoadString(HLoadString* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitLongConstant(HLongConstant* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  locations->SetOut(Location::ConstantLocation(instruction));
}

void InstructionCodeGeneratorLOONGARCH64::VisitLongConstant(
    [[maybe_unused]] HLongConstant* instruction) {
  // Will be generated at use site.
}

void LocationsBuilderLOONGARCH64::VisitMax(HMax* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitMax(HMax* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitMemoryBarrier(HMemoryBarrier* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitMemoryBarrier(HMemoryBarrier* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitMethodEntryHook(HMethodEntryHook* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitMethodEntryHook(HMethodEntryHook* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitMethodExitHook(HMethodExitHook* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitMethodExitHook(HMethodExitHook* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitMin(HMin* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitMin(HMin* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitMonitorOperation(HMonitorOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitMonitorOperation(HMonitorOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitMul(HMul* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitMul(HMul* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitNeg(HNeg* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNeg(HNeg* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitNop(HNop* info) {
  UNUSED(info);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNop(HNop*) {
  // The environment recording already happened in CodeGenerator::Compile.
}

void LocationsBuilderLOONGARCH64::VisitNewArray(HNewArray* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNewArray(HNewArray* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitNewInstance(HNewInstance* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNewInstance(HNewInstance* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitNot(HNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNot(HNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitNotEqual(HNotEqual* instruction) {
  HandleCondition(instruction);
}

void InstructionCodeGeneratorLOONGARCH64::VisitNotEqual(HNotEqual* instruction) {
  HandleCondition(instruction);
}

void LocationsBuilderLOONGARCH64::VisitNullConstant(HNullConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNullConstant(HNullConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitOr(HOr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitOr(HOr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitPackedSwitch(HPackedSwitch* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitPackedSwitch(HPackedSwitch* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitParallelMove([[maybe_unused]] HParallelMove* instruction) {
  LOG(FATAL) << "Unreachable";
}

void InstructionCodeGeneratorLOONGARCH64::VisitParallelMove(HParallelMove* instruction) {
  if (instruction->GetNext()->IsSuspendCheck() &&
      instruction->GetBlock()->GetLoopInformation() != nullptr) {
    HSuspendCheck* suspend_check = instruction->GetNext()->AsSuspendCheck();
    // The back edge will generate the suspend check.
    codegen_->ClearSpillSlotsFromLoopPhisInStackMap(suspend_check, instruction);
  }

  codegen_->GetMoveResolver()->EmitNativeCode(instruction);
}

void LocationsBuilderLOONGARCH64::VisitParameterValue(HParameterValue* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  Location location = parameter_visitor_.GetNextLocation(instruction->GetType());
  if (location.IsStackSlot()) {
    location = Location::StackSlot(location.GetStackIndex() + codegen_->GetFrameSize());
  } else if (location.IsDoubleStackSlot()) {
    location = Location::DoubleStackSlot(location.GetStackIndex() + codegen_->GetFrameSize());
  }
  locations->SetOut(location);
}

void InstructionCodeGeneratorLOONGARCH64::VisitParameterValue(
    [[maybe_unused]] HParameterValue* instruction) {
  // Nothing to do, the parameter is already at its location.
}

void LocationsBuilderLOONGARCH64::VisitPhi(HPhi* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  for (size_t i = 0, e = locations->GetInputCount(); i < e; ++i) {
    locations->SetInAt(i, Location::Any());
  }
  locations->SetOut(Location::Any());
}

void InstructionCodeGeneratorLOONGARCH64::VisitPhi([[maybe_unused]] HPhi* instruction) {
  LOG(FATAL) << "Unreachable";
}

void LocationsBuilderLOONGARCH64::VisitRem(HRem* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitRem(HRem* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitReturn(HReturn* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator()) LocationSummary(instruction);
  DataType::Type return_type = instruction->InputAt(0)->GetType();
  DCHECK_NE(return_type, DataType::Type::kVoid);
  locations->SetInAt(0, Loongarch64ReturnLocation(return_type));
}

void InstructionCodeGeneratorLOONGARCH64::VisitReturn([[maybe_unused]] HReturn* instruction) {
  //if (GetGraph()->IsCompilingOsr()) {
  //  // To simplify callers of an OSR method, we put a floating point return value
  //  // in both floating point and core return registers.
  //  switch (instruction->InputAt(0)->GetType()) {
  //    default:
  //      break;
  //  }
  //}
  codegen_->GenerateFrameExit();
}

void LocationsBuilderLOONGARCH64::VisitReturnVoid(HReturnVoid* instruction) {
  instruction->SetLocations(nullptr);
}

void InstructionCodeGeneratorLOONGARCH64::VisitReturnVoid([[maybe_unused]] HReturnVoid* instruction) {
  codegen_->GenerateFrameExit();
}

void LocationsBuilderLOONGARCH64::VisitRor(HRor* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitRor(HRor* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitShl(HShl* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitShl(HShl* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitShr(HShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitShr(HShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitStaticFieldSet(HStaticFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitStaticFieldSet(HStaticFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitStringBuilderAppend(HStringBuilderAppend* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitStringBuilderAppend(HStringBuilderAppend* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitUnresolvedInstanceFieldGet(
    HUnresolvedInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitUnresolvedInstanceFieldGet(
    HUnresolvedInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitUnresolvedInstanceFieldSet(
    HUnresolvedInstanceFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitUnresolvedInstanceFieldSet(
    HUnresolvedInstanceFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitUnresolvedStaticFieldGet(
    HUnresolvedStaticFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitUnresolvedStaticFieldGet(
    HUnresolvedStaticFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitUnresolvedStaticFieldSet(
    HUnresolvedStaticFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitUnresolvedStaticFieldSet(
    HUnresolvedStaticFieldSet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitSelect(HSelect* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitSelect(HSelect* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitSub(HSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitSub(HSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitSuspendCheck(HSuspendCheck* instruction) {
  LocationSummary* locations = new (GetGraph()->GetAllocator())
      LocationSummary(instruction, LocationSummary::kCallOnSlowPath);
  // In suspend check slow path, usually there are no caller-save registers at all.
  // If SIMD instructions are present, however, we force spilling all live SIMD
  // registers in full width (since the runtime only saves/restores lower part).
  locations->SetCustomSlowPathCallerSaves(GetGraph()->HasSIMD() ? RegisterSet::AllFpu() :
                                                                  RegisterSet::Empty());
}

void InstructionCodeGeneratorLOONGARCH64::VisitSuspendCheck(HSuspendCheck* instruction) {
  HBasicBlock* block = instruction->GetBlock();
  if (block->GetLoopInformation() != nullptr) {
    DCHECK(block->GetLoopInformation()->GetSuspendCheck() == instruction);
    // The back edge will generate the suspend check.
    return;
  }
  if (block->IsEntryBlock() && instruction->GetNext()->IsGoto()) {
    // The goto will generate the suspend check.
    return;
  }
  GenerateSuspendCheck(instruction, nullptr);
}

void LocationsBuilderLOONGARCH64::VisitThrow(HThrow* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitThrow(HThrow* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitTryBoundary(HTryBoundary* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitTryBoundary(HTryBoundary* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitTypeConversion(HTypeConversion* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitTypeConversion(HTypeConversion* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitUShr(HUShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitUShr(HUShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitXor(HXor* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitXor(HXor* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitBitwiseNegatedRight(HBitwiseNegatedRight* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitBitwiseNegatedRight(HBitwiseNegatedRight* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecReplicateScalar(HVecReplicateScalar* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecExtractScalar(HVecExtractScalar* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecReduce(HVecReduce* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecReduce(HVecReduce* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecCnv(HVecCnv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecCnv(HVecCnv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecNeg(HVecNeg* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecNeg(HVecNeg* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecAbs(HVecAbs* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecAbs(HVecAbs* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecNot(HVecNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecNot(HVecNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecAdd(HVecAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecAdd(HVecAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecHalvingAdd(HVecHalvingAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecSub(HVecSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecSub(HVecSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecMul(HVecMul* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecMul(HVecMul* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecDiv(HVecDiv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecDiv(HVecDiv* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecMin(HVecMin* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecMin(HVecMin* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecMax(HVecMax* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecMax(HVecMax* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecAnd(HVecAnd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecAnd(HVecAnd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecAndNot(HVecAndNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecAndNot(HVecAndNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecOr(HVecOr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecOr(HVecOr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecXor(HVecXor* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecXor(HVecXor* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecSaturationAdd(HVecSaturationAdd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecSaturationSub(HVecSaturationSub* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecShl(HVecShl* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecShl(HVecShl* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecShr(HVecShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecShr(HVecShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecUShr(HVecUShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecUShr(HVecUShr* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecSetScalars(HVecSetScalars* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecSetScalars(HVecSetScalars* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecMultiplyAccumulate(HVecMultiplyAccumulate* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecMultiplyAccumulate(
    HVecMultiplyAccumulate* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecSADAccumulate(HVecSADAccumulate* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecDotProd(HVecDotProd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecDotProd(HVecDotProd* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecLoad(HVecLoad* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecLoad(HVecLoad* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecStore(HVecStore* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecStore(HVecStore* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecPredSetAll(HVecPredSetAll* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecPredWhile(HVecPredWhile* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecPredWhile(HVecPredWhile* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecPredToBoolean(HVecPredToBoolean* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecPredToBoolean(HVecPredToBoolean* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecCondition(HVecCondition* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecCondition(HVecCondition* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitVecPredNot(HVecPredNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecPredNot(HVecPredNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

#undef __

namespace detail {

// Mark which intrinsics we don't have handcrafted code for.
template <Intrinsics T>
struct IsUnimplemented {
  bool is_unimplemented = false;
};

#define TRUE_OVERRIDE(Name, ...)                \
  template <>                                   \
  struct IsUnimplemented<Intrinsics::k##Name> { \
    bool is_unimplemented = true;               \
  };
UNIMPLEMENTED_INTRINSIC_LIST_LOONGARCH64(TRUE_OVERRIDE)
#undef TRUE_OVERRIDE

static constexpr bool kIsIntrinsicUnimplemented[] = {
    false,  // kNone
#define IS_UNIMPLEMENTED(Intrinsic, ...) \
    IsUnimplemented<Intrinsics::k##Intrinsic>().is_unimplemented,
    ART_INTRINSICS_LIST(IS_UNIMPLEMENTED)
#undef IS_UNIMPLEMENTED
};

}  // namespace detail

#define __ down_cast<Loongarch64Assembler*>(GetAssembler())->  // NOLINT

CodeGeneratorLOONGARCH64::CodeGeneratorLOONGARCH64(HGraph* graph,
                                           const CompilerOptions& compiler_options,
                                           OptimizingCompilerStats* stats)
    : CodeGenerator(graph,
                    kNumberOfXRegisters,
                    kNumberOfFRegisters,
                    /*number_of_register_pairs=*/ 0u,
                    ComputeRegisterMask(reinterpret_cast<const int*>(kCoreCalleeSaves), arraysize(kCoreCalleeSaves)),
                    ComputeRegisterMask(reinterpret_cast<const int*>(kFpuCalleeSaves), arraysize(kFpuCalleeSaves)),
                    compiler_options,
                    stats,
                    ArrayRef<const bool>
                    (detail::kIsIntrinsicUnimplemented)),
      assembler_(graph->GetAllocator(),
                 compiler_options.GetInstructionSetFeatures()->AsLoongarch64InstructionSetFeatures()),
      location_builder_(graph, this),
      instruction_visitor_(graph, this),
      block_labels_(nullptr),
      move_resolver_(graph->GetAllocator(), this),
      uint32_literals_(std::less<uint32_t>(),
                       graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      uint64_literals_(std::less<uint64_t>(),
                       graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      boot_image_method_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      method_bss_entry_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      boot_image_type_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      type_bss_entry_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      public_type_bss_entry_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      package_type_bss_entry_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      boot_image_string_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      string_bss_entry_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      boot_image_jni_entrypoint_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)),
      boot_image_other_patches_(graph->GetAllocator()->Adapter(kArenaAllocCodeGenerator)) {
  // Always mark the RA register to be saved.
  AddAllocatedRegister(Location::RegisterLocation(RA));
}

void CodeGeneratorLOONGARCH64::MaybeIncrementHotness(bool is_frame_entry) {
  if (GetCompilerOptions().CountHotnessInCompiledCode()) {
    ScratchRegisterScope srs(GetAssembler());
    XRegister method = is_frame_entry ? kArtMethodRegister : srs.AllocateXRegister();
    if (!is_frame_entry) {
      __ Load_D(method, SP, 0);
    }
    XRegister counter = srs.AllocateXRegister();
    __ Load_HU(counter, method, ArtMethod::HotnessCountOffset().Int32Value());
    Loongarch64Label done;
    __ Beqz(counter, &done);  // Can clobber `TMP` if taken.
    __ Addi_D(counter, counter, -1);
    // We may not have another scratch register available for `Storeh`()`,
    // so we must use the `Sh()` function directly.
    static_assert(IsInt<12>(ArtMethod::HotnessCountOffset().Int32Value()));
    __ St_H(counter, method, ArtMethod::HotnessCountOffset().Int32Value());
    __ Bind(&done);
  }

  if (GetGraph()->IsCompilingBaseline() && !Runtime::Current()->IsAotCompiler()) {
    SlowPathCodeLOONGARCH64* slow_path = new (GetScopedAllocator()) CompileOptimizedSlowPathLOONGARCH64();
    AddSlowPath(slow_path);
    ScopedProfilingInfoUse spiu(Runtime::Current()->GetJit(), GetGraph()->GetArtMethod(), Thread::Current());
    ProfilingInfo* info = spiu.GetProfilingInfo();
    DCHECK(info != nullptr);
    DCHECK(!HasEmptyFrame());
    uint64_t address = reinterpret_cast64<uint64_t>(info);
    Loongarch64Label done;
    ScratchRegisterScope srs(GetAssembler());
    XRegister tmp = srs.AllocateXRegister();
    __ LoadConst64(tmp, address);
    XRegister counter = srs.AllocateXRegister();
    __ Load_HU(counter, tmp, ProfilingInfo::BaselineHotnessCountOffset().Int32Value());
    __ Beqz(counter, slow_path->GetEntryLabel());  // Can clobber `TMP` if taken.
    __ Addi_D(counter, counter, -1);
    // We do not have another scratch register available for `Storeh`()`,
    // so we must use the `Sh()` function directly.
    static_assert(IsInt<12>(ProfilingInfo::BaselineHotnessCountOffset().Int32Value()));
    __ St_H(counter, tmp, ProfilingInfo::BaselineHotnessCountOffset().Int32Value());
    __ Bind(slow_path->GetExitLabel());
  }
}

bool CodeGeneratorLOONGARCH64::CanUseImplicitSuspendCheck() const {
  // TODO(loongarch64): Implement implicit suspend checks to reduce code size.
  return false;
}

void CodeGeneratorLOONGARCH64::GenerateMemoryBarrier(MemBarrierKind kind) {
  switch (kind) {
    case MemBarrierKind::kAnyAny:
    case MemBarrierKind::kAnyStore:
    case MemBarrierKind::kLoadAny:
    case MemBarrierKind::kStoreStore: {
      // TODO(loongarch64): Use more specific fences.
      __ Dbar(0x700);
      break;
    }

    default:
      LOG(FATAL) << "Unexpected memory barrier " << kind;
      UNREACHABLE();
  }
}

void CodeGeneratorLOONGARCH64::GenerateFrameEntry() {
   __ Bind(&frame_entry_label_);

  bool do_overflow_check =
      FrameNeedsStackCheck(GetFrameSize(), InstructionSet::kLoongarch64) || !IsLeafMethod();

  if (do_overflow_check) {
    __ Load_W(
        Zero, SP, -static_cast<int32_t>(GetStackOverflowReservedBytes(InstructionSet::kLoongarch64)));
    RecordPcInfo(nullptr, 0);
  }

  if (!HasEmptyFrame()) {
    // Make sure the frame size isn't unreasonably large.
    if (GetFrameSize() > GetStackOverflowReservedBytes(InstructionSet::kLoongarch64)) {
      LOG(FATAL) << "Stack frame larger than "
                 << GetStackOverflowReservedBytes(InstructionSet::kLoongarch64) << " bytes";
    }

    // Spill callee-saved registers.

    uint32_t frame_size = GetFrameSize();

    IncreaseFrame(frame_size);

    uint32_t offset = frame_size;
    for (size_t i = arraysize(kCoreCalleeSaves); i != 0; ) {
      --i;
      XRegister reg = kCoreCalleeSaves[i];
      if (allocated_registers_.ContainsCoreRegister(reg)) {
        offset -= kLoongarch64DoublewordSize;
        __ Store_D(reg, SP, offset);
        __ cfi().RelOffset(dwarf::Reg::Loongarch64Core(reg), offset);
      }
    }
    // TODO: support FP

    // Save the current method if we need it. Note that we do not
    // do this in HCurrentMethod, as the instruction might have been removed
    // in the SSA graph.
    if (RequiresCurrentMethod()) {
      __ Store_D(kArtMethodRegister, SP, 0);
    }

    if (GetGraph()->HasShouldDeoptimizeFlag()) {
      // Initialize should_deoptimize flag to 0.
      __ Store_W(Zero, SP, GetStackOffsetOfShouldDeoptimizeFlag());
    }
  }
  MaybeIncrementHotness(/*is_frame_entry=*/ true);
}

void CodeGeneratorLOONGARCH64::GenerateFrameExit() {
  __ cfi().RememberState();

  if (!HasEmptyFrame()) {
    // Restore callee-saved registers.

    // For better instruction scheduling restore RA before other registers.
    uint32_t offset = GetFrameSize();
    for (size_t i = arraysize(kCoreCalleeSaves); i != 0; ) {
      --i;
      XRegister reg = kCoreCalleeSaves[i];
      if (allocated_registers_.ContainsCoreRegister(reg)) {
        offset -= kLoongarch64DoublewordSize;
        __ Load_D(reg, SP, offset);
        __ cfi().Restore(dwarf::Reg::Loongarch64Core(reg));
      }
    }
    // TODO: support FP
    DecreaseFrame(GetFrameSize());
  }

  __ Jr(RA);

  __ cfi().RestoreState();
  __ cfi().DefCFAOffset(GetFrameSize());
}

void CodeGeneratorLOONGARCH64::Bind(HBasicBlock* block) {  __ Bind(GetLabelOf(block)); }

void CodeGeneratorLOONGARCH64::MoveConstant(Location destination, int32_t value) {
  DCHECK(destination.IsRegister());
  __ LoadConst32(destination.AsRegister<XRegister>(), value);
}

void CodeGeneratorLOONGARCH64::MoveLocation(Location destination, Location source, DataType::Type dst_type) {
  if (source.Equals(destination)) {
    return;
  }

  // A valid move type can always be inferred from the destination and source locations.
  // When moving from and to a register, the `dst_type` can be used to generate 32-bit instead
  // of 64-bit moves but it's generally OK to use 64-bit moves for 32-bit values in registers.
  bool unspecified_type = (dst_type == DataType::Type::kVoid);
  // TODO(loongarch64): Is the destination type known in all cases?
  // TODO(loongarch64): Can unspecified `dst_type` move 32-bit GPR to FPR without NaN-boxing?
  CHECK(!unspecified_type);

  if (destination.IsRegister() || destination.IsFpuRegister()) {
    if (unspecified_type) {
      HConstant* src_cst = source.IsConstant() ? source.GetConstant() : nullptr;
      if (source.IsStackSlot() ||
          (src_cst != nullptr &&
           (src_cst->IsIntConstant() || src_cst->IsFloatConstant() || src_cst->IsNullConstant()))) {
        // For stack slots and 32-bit constants, a 32-bit type is appropriate.
        dst_type = destination.IsRegister() ? DataType::Type::kInt32 : DataType::Type::kFloat32;
      } else {
        // If the source is a double stack slot or a 64-bit constant, a 64-bit type
        // is appropriate. Else the source is a register, and since the type has not
        // been specified, we chose a 64-bit type to force a 64-bit move.
        dst_type = destination.IsRegister() ? DataType::Type::kInt64 : DataType::Type::kFloat64;
      }
    }
    DCHECK((destination.IsFpuRegister() && DataType::IsFloatingPointType(dst_type)) ||
           (destination.IsRegister() && !DataType::IsFloatingPointType(dst_type)));

    if (source.IsStackSlot() || source.IsDoubleStackSlot()) {
      // Move to GPR/FPR from stack
      // TODO: support FP
     } else {
        if (DataType::Is64BitType(dst_type)) {
          __ Load_D(destination.AsRegister<XRegister>(), SP, source.GetStackIndex());
        } else if (dst_type == DataType::Type::kReference) {
          __ Load_WU(destination.AsRegister<XRegister>(), SP, source.GetStackIndex());
        } else {
          __ Load_W(destination.AsRegister<XRegister>(), SP, source.GetStackIndex());
        }
      }
  } else if (source.IsConstant()) {
    // Move to GPR/FPR from constant
    // TODO(loongarch64): Consider using literals for difficult-to-materialize 64-bit constants.
    int64_t value = GetInt64ValueOf(source.GetConstant()->AsConstant());
    ScratchRegisterScope srs(GetAssembler());
    XRegister gpr = DataType::IsFloatingPointType(dst_type)
        ? srs.AllocateXRegister()
        : destination.AsRegister<XRegister>();
    if (DataType::IsFloatingPointType(dst_type) && value == 0) {
      gpr = Zero;  // Note: The scratch register allocated above shall not be used.
    } else {
      // Note: For `float` we load the sign-extended value here as it can sometimes yield
      // a shorter instruction sequence. The higher 32 bits shall be ignored during the
      // transfer to FP reg and the result shall be correctly NaN-boxed.
      __ LoadConst64(gpr, value);
    }
    // TODO: support FP
  } else if (source.IsRegister()) {
    if (destination.IsRegister()) {
      // Move to GPR from GPR
      __ Move(destination.AsRegister<XRegister>(), source.AsRegister<XRegister>());
    } else {
      // TODO: support FP
    }
  } else if (source.IsFpuRegister()) {
    if (destination.IsFpuRegister()) {
      if (GetGraph()->HasSIMD()) {
        LOG(FATAL) << "Vector extension is unsupported";
        UNREACHABLE();
      } else {
        // Move to FPR from FPR
        // TODO: support FP
      }
    } else {
      DCHECK(destination.IsRegister());
      // TODO: support FP
    }
  } else if (destination.IsSIMDStackSlot()) {
    LOG(FATAL) << "SIMD is unsupported";
    UNREACHABLE();
  } else {  // The destination is not a register. It must be a stack slot.
    DCHECK(destination.IsStackSlot() || destination.IsDoubleStackSlot());
    if (source.IsRegister() || source.IsFpuRegister()) {
      if (unspecified_type) {
        if (source.IsRegister()) {
          dst_type = destination.IsStackSlot() ? DataType::Type::kInt32 : DataType::Type::kInt64;
        } else {
          dst_type =
              destination.IsStackSlot() ? DataType::Type::kFloat32 : DataType::Type::kFloat64;
        }
      }
      DCHECK((destination.IsDoubleStackSlot() == DataType::Is64BitType(dst_type)) &&
             (source.IsFpuRegister() == DataType::IsFloatingPointType(dst_type)));
      // Move to stack from GPR/FPR
      if (DataType::Is64BitType(dst_type)) {
        if (source.IsRegister()) {
          __ Store_D(source.AsRegister<XRegister>(), SP, destination.GetStackIndex());
        }
      } else {
        if (source.IsRegister()) {
          __ Store_W(source.AsRegister<XRegister>(), SP, destination.GetStackIndex());
        }
      }
    } else if (source.IsConstant()) {
      // Move to stack from constant
      int64_t value = GetInt64ValueOf(source.GetConstant());
      ScratchRegisterScope srs(GetAssembler());
      XRegister gpr = (value != 0) ? srs.AllocateXRegister() : Zero;
      if (value != 0) {
        __ LoadConst64(gpr, value);
      }
      if (destination.IsStackSlot()) {
        __ Store_W(gpr, SP, destination.GetStackIndex());
      } else {
        DCHECK(destination.IsDoubleStackSlot());
        __ Store_D(gpr, SP, destination.GetStackIndex());
      }
    } else {
      DCHECK(source.IsStackSlot() || source.IsDoubleStackSlot());
      DCHECK_EQ(source.IsDoubleStackSlot(), destination.IsDoubleStackSlot());
      // Move to stack from stack
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      if (destination.IsStackSlot()) {
        __ Load_W(tmp, SP, source.GetStackIndex());
        __ Store_W(tmp, SP, destination.GetStackIndex());
      } else {
        __ Load_D(tmp, SP, source.GetStackIndex());
        __ Store_D(tmp, SP, destination.GetStackIndex());
      }
    }
  }
}

void CodeGeneratorLOONGARCH64::AddLocationAsTemp(Location location, LocationSummary* locations) {
  if (location.IsRegister()) {
    locations->AddTemp(location);
  } else {
    UNIMPLEMENTED(FATAL) << "AddLocationAsTemp not implemented for location " << location;
  }
}

void CodeGeneratorLOONGARCH64::SetupBlockedRegisters() const {
  // ZERO, SP, RA, TP and TR(S1) are reserved and can't be allocated.
  blocked_core_registers_[Zero] = true;
  blocked_core_registers_[SP] = true;
  blocked_core_registers_[RA] = true;
  blocked_core_registers_[TP] = true;
  blocked_core_registers_[TR] = true;  // ART Thread register.

  // TMP(R21), TMP2(T8) and FTMP(FT15) are used as temporary/scratch registers.
  blocked_core_registers_[TMP] = true;
  blocked_core_registers_[TMP2] = true;
  blocked_fpu_registers_[FTMP] = true;

  if (GetGraph()->IsDebuggable()) {
    // Stubs do not save callee-save floating point registers. If the graph
    // is debuggable, we need to deal with these registers differently. For
    // now, just block them.
    for (size_t i = 0; i < arraysize(kFpuCalleeSaves); ++i) {
      blocked_fpu_registers_[kFpuCalleeSaves[i]] = true;
    }
  }
}

size_t CodeGeneratorLOONGARCH64::SaveCoreRegister(size_t stack_index, uint32_t reg_id) {
  __ Store_D(XRegister(reg_id), SP, stack_index);
  return kLoongarch64DoublewordSize;
}

size_t CodeGeneratorLOONGARCH64::RestoreCoreRegister(size_t stack_index, uint32_t reg_id) {
  __ Load_D(XRegister(reg_id), SP, stack_index);
  return kLoongarch64DoublewordSize;
}

size_t CodeGeneratorLOONGARCH64::SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) {
  UNUSED(stack_index);
  UNUSED(reg_id);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

size_t CodeGeneratorLOONGARCH64::RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) {
  UNUSED(stack_index);
  UNUSED(reg_id);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void CodeGeneratorLOONGARCH64::DumpCoreRegister(std::ostream& stream, int reg) const {
  stream << XRegister(reg);
}

void CodeGeneratorLOONGARCH64::DumpFloatingPointRegister(std::ostream& stream, int reg) const {
  stream << FRegister(reg);
}

void CodeGeneratorLOONGARCH64::Finalize() {
  // Ensure that we fix up branches and literal loads and emit the literal pool.
  __ FinalizeCode();

  // Adjust native pc offsets in stack maps.
  StackMapStream* stack_map_stream = GetStackMapStream();
  for (size_t i = 0, num = stack_map_stream->GetNumberOfStackMaps(); i != num; ++i) {
    uint32_t old_position = stack_map_stream->GetStackMapNativePcOffset(i);
    uint32_t new_position = __ GetAdjustedPosition(old_position);
    DCHECK_GE(new_position, old_position);
    stack_map_stream->SetStackMapNativePcOffset(i, new_position);
  }

  // Adjust pc offsets for the disassembly information.
  if (disasm_info_ != nullptr) {
    GeneratedCodeInterval* frame_entry_interval = disasm_info_->GetFrameEntryInterval();
    frame_entry_interval->start = __ GetAdjustedPosition(frame_entry_interval->start);
    frame_entry_interval->end = __ GetAdjustedPosition(frame_entry_interval->end);
    for (auto& entry : *disasm_info_->GetInstructionIntervals()) {
      entry.second.start = __ GetAdjustedPosition(entry.second.start);
      entry.second.end = __ GetAdjustedPosition(entry.second.end);
    }
    for (auto& entry : *disasm_info_->GetSlowPathIntervals()) {
      entry.code_interval.start = __ GetAdjustedPosition(entry.code_interval.start);
      entry.code_interval.end = __ GetAdjustedPosition(entry.code_interval.end);
    }
  }

  CodeGenerator::Finalize();
}

// Generate code to invoke a runtime entry point.
void CodeGeneratorLOONGARCH64::InvokeRuntime(QuickEntrypointEnum entrypoint,
                                         HInstruction* instruction,
                                         uint32_t dex_pc,
                                         SlowPathCode* slow_path) {
  ValidateInvokeRuntime(entrypoint, instruction, slow_path);

  ThreadOffset64 entrypoint_offset = GetThreadOffset<kLoongarch64PointerSize>(entrypoint);

  // TODO(loongarch64): Reduce code size for AOT by using shared trampolines for slow path
  // runtime calls across the entire oat file.
  __ Load_D(TMP, TR, entrypoint_offset.Int32Value());
  __ Jirl(RA, TMP, 0);
  if (EntrypointRequiresStackMap(entrypoint)) {
    RecordPcInfo(instruction, dex_pc, slow_path);
  }
}

// Generate code to invoke a runtime entry point, but do not record
// PC-related information in a stack map.
void CodeGeneratorLOONGARCH64::InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                                               HInstruction* instruction,
                                                               SlowPathCode* slow_path) {
  ValidateInvokeRuntimeWithoutRecordingPcInfo(instruction, slow_path);
  __ Load_D(TMP, TR, entry_point_offset);
  __ Jirl(RA, TMP, 0);
}

void CodeGeneratorLOONGARCH64::IncreaseFrame(size_t adjustment) {
  int32_t adjustment32 = dchecked_integral_cast<int32_t>(adjustment);
  __ AddConst64(SP, SP, -adjustment32);
  GetAssembler()->cfi().AdjustCFAOffset(adjustment32);
}

void CodeGeneratorLOONGARCH64::DecreaseFrame(size_t adjustment) {
  int32_t adjustment32 = dchecked_integral_cast<int32_t>(adjustment);
  __ AddConst64(SP, SP, adjustment32);
  GetAssembler()->cfi().AdjustCFAOffset(-adjustment32);
}

void CodeGeneratorLOONGARCH64::GenerateNop() { __ Nop(); }

void CodeGeneratorLOONGARCH64::GenerateImplicitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}
void CodeGeneratorLOONGARCH64::GenerateExplicitNullCheck(HNullCheck* instruction) {
  SlowPathCodeLOONGARCH64* slow_path = new (GetScopedAllocator()) NullCheckSlowPathLOONGARCH64(instruction);
  AddSlowPath(slow_path);

  Location obj = instruction->GetLocations()->InAt(0);

  __ Beqz(obj.AsRegister<XRegister>(), slow_path->GetEntryLabel());
}

HLoadString::LoadKind CodeGeneratorLOONGARCH64::GetSupportedLoadStringKind(
    HLoadString::LoadKind desired_string_load_kind) {
  switch (desired_string_load_kind) {
    case HLoadString::LoadKind::kBootImageLinkTimePcRelative:
    case HLoadString::LoadKind::kBootImageRelRo:
    case HLoadString::LoadKind::kBssEntry:
      DCHECK(!Runtime::Current()->UseJitCompilation());
      break;
    case HLoadString::LoadKind::kJitBootImageAddress:
    case HLoadString::LoadKind::kJitTableAddress:
      DCHECK(Runtime::Current()->UseJitCompilation());
      break;
    case HLoadString::LoadKind::kRuntimeCall:
      break;
  }
  return desired_string_load_kind;
}

HLoadClass::LoadKind CodeGeneratorLOONGARCH64::GetSupportedLoadClassKind(
    HLoadClass::LoadKind desired_class_load_kind) {
  switch (desired_class_load_kind) {
    case HLoadClass::LoadKind::kInvalid:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
    case HLoadClass::LoadKind::kReferrersClass:
      break;
    case HLoadClass::LoadKind::kBootImageLinkTimePcRelative:
    case HLoadClass::LoadKind::kBootImageRelRo:
    case HLoadClass::LoadKind::kAppImageRelRo:
    case HLoadClass::LoadKind::kBssEntry:
    case HLoadClass::LoadKind::kBssEntryPublic:
    case HLoadClass::LoadKind::kBssEntryPackage:
      DCHECK(!Runtime::Current()->UseJitCompilation());
      break;
    case HLoadClass::LoadKind::kJitBootImageAddress:
    case HLoadClass::LoadKind::kJitTableAddress:
      DCHECK(Runtime::Current()->UseJitCompilation());
      break;
    case HLoadClass::LoadKind::kRuntimeCall:
      break;
  }
  return desired_class_load_kind;
}

HInvokeStaticOrDirect::DispatchInfo CodeGeneratorLOONGARCH64::GetSupportedInvokeStaticOrDirectDispatch(
    const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info, ArtMethod* method) {
  UNUSED(method);
  // On LOONGARCH64 we support all dispatch types.
  return desired_dispatch_info;
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewBootImageIntrinsicPatch(
    uint32_t intrinsic_data, const PcRelativePatchInfo* info_high) {
  return NewPcRelativePatch(
      /* dex_file= */ nullptr, intrinsic_data, info_high, &boot_image_other_patches_);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewBootImageRelRoPatch(
    uint32_t boot_image_offset, const PcRelativePatchInfo* info_high) {
  return NewPcRelativePatch(
      /* dex_file= */ nullptr, boot_image_offset, info_high, &boot_image_other_patches_);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewBootImageMethodPatch(
    MethodReference target_method, const PcRelativePatchInfo* info_high) {
  return NewPcRelativePatch(
      target_method.dex_file, target_method.index, info_high, &boot_image_method_patches_);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewMethodBssEntryPatch(
    MethodReference target_method, const PcRelativePatchInfo* info_high) {
  return NewPcRelativePatch(
      target_method.dex_file, target_method.index, info_high, &method_bss_entry_patches_);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewBootImageTypePatch(
    const DexFile& dex_file, dex::TypeIndex type_index, const PcRelativePatchInfo* info_high) {
  return NewPcRelativePatch(&dex_file, type_index.index_, info_high, &boot_image_type_patches_);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewBootImageJniEntrypointPatch(
    MethodReference target_method, const PcRelativePatchInfo* info_high) {
  return NewPcRelativePatch(
      target_method.dex_file, target_method.index, info_high, &boot_image_jni_entrypoint_patches_);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewTypeBssEntryPatch(
    HLoadClass* load_class,
    const PcRelativePatchInfo* info_high) {
  const DexFile& dex_file = load_class->GetDexFile();
  dex::TypeIndex type_index = load_class->GetTypeIndex();
  ArenaDeque<PcRelativePatchInfo>* patches = nullptr;
  switch (load_class->GetLoadKind()) {
    case HLoadClass::LoadKind::kBssEntry:
      patches = &type_bss_entry_patches_;
      break;
    case HLoadClass::LoadKind::kBssEntryPublic:
      patches = &public_type_bss_entry_patches_;
      break;
    case HLoadClass::LoadKind::kBssEntryPackage:
      patches = &package_type_bss_entry_patches_;
      break;
    default:
      LOG(FATAL) << "Unexpected load kind: " << load_class->GetLoadKind();
      UNREACHABLE();
  }
  return NewPcRelativePatch(&dex_file, type_index.index_, info_high, patches);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewBootImageStringPatch(
    const DexFile& dex_file, dex::StringIndex string_index, const PcRelativePatchInfo* info_high) {
  return NewPcRelativePatch(&dex_file, string_index.index_, info_high, &boot_image_string_patches_);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewStringBssEntryPatch(
    const DexFile& dex_file, dex::StringIndex string_index, const PcRelativePatchInfo* info_high) {
  return NewPcRelativePatch(&dex_file, string_index.index_, info_high, &string_bss_entry_patches_);
}

CodeGeneratorLOONGARCH64::PcRelativePatchInfo* CodeGeneratorLOONGARCH64::NewPcRelativePatch(
    const DexFile* dex_file,
    uint32_t offset_or_index,
    const PcRelativePatchInfo* info_high,
    ArenaDeque<PcRelativePatchInfo>* patches) {
  patches->emplace_back(dex_file, offset_or_index, info_high);
  return &patches->back();
}

Literal* CodeGeneratorLOONGARCH64::DeduplicateUint32Literal(uint32_t value) {
  return uint32_literals_.GetOrCreate(value,
                                      [this, value]() { return __ NewLiteral<uint32_t>(value); });
}

Literal* CodeGeneratorLOONGARCH64::DeduplicateUint64Literal(uint64_t value) {
  return uint64_literals_.GetOrCreate(value,
                                      [this, value]() { return __ NewLiteral<uint64_t>(value); });
}

Literal* CodeGeneratorLOONGARCH64::DeduplicateBootImageAddressLiteral(uint64_t address) {
  return DeduplicateUint32Literal(dchecked_integral_cast<uint32_t>(address));
}

void CodeGeneratorLOONGARCH64::EmitPcRelativePcaddu12iPlaceholder(PcRelativePatchInfo* info_high,
                                                          XRegister out) {
  DCHECK(info_high->patch_info_high == nullptr);
  __ Bind(&info_high->label);
  __ Pcaddu12i(out, /*imm20=*/ 0x12345);  // Placeholder `imm20` patched at link time.
}

void CodeGeneratorLOONGARCH64::EmitPcRelativeAddi_dPlaceholder(PcRelativePatchInfo* info_low,
                                                         XRegister rd,
                                                         XRegister rs1) {
  DCHECK(info_low->patch_info_high != nullptr);
  __ Bind(&info_low->label);
  __ Addi_D(rd, rs1, /*imm12=*/ 0x678);  // Placeholder `imm12` patched at link time.
}

void CodeGeneratorLOONGARCH64::EmitPcRelativeLd_wuPlaceholder(PcRelativePatchInfo* info_low,
                                                        XRegister rd,
                                                        XRegister rs1) {
  DCHECK(info_low->patch_info_high != nullptr);
  __ Bind(&info_low->label);
  __ Ld_WU(rd, rs1, /*offset=*/ 0x678);  // Placeholder `offset` patched at link time.
}

void CodeGeneratorLOONGARCH64::EmitPcRelativeLd_dPlaceholder(PcRelativePatchInfo* info_low,
                                                       XRegister rd,
                                                       XRegister rs1) {
  DCHECK(info_low->patch_info_high != nullptr);
  __ Bind(&info_low->label);
  __ Ld_D(rd, rs1, /*offset=*/ 0x678);  // Placeholder `offset` patched at link time.
}

void CodeGeneratorLOONGARCH64::LoadMethod(MethodLoadKind load_kind, Location temp, HInvoke* invoke) {
    switch (load_kind) {
    case MethodLoadKind::kBootImageLinkTimePcRelative: {
      DCHECK(GetCompilerOptions().IsBootImage() || GetCompilerOptions().IsBootImageExtension());
      CodeGeneratorLOONGARCH64::PcRelativePatchInfo* info_high =
          NewBootImageMethodPatch(invoke->GetResolvedMethodReference());
      EmitPcRelativePcaddu12iPlaceholder(info_high, temp.AsRegister<XRegister>());
      CodeGeneratorLOONGARCH64::PcRelativePatchInfo* info_low =
          NewBootImageMethodPatch(invoke->GetResolvedMethodReference(), info_high);
      EmitPcRelativeAddi_dPlaceholder(
          info_low, temp.AsRegister<XRegister>(), temp.AsRegister<XRegister>());
      break;
    }
    // Read-only data area in Boot Image
    case MethodLoadKind::kBootImageRelRo: {
      // get the method offset in Boot Image
      uint32_t boot_image_offset = GetBootImageOffset(invoke);
      PcRelativePatchInfo* info_high = NewBootImageRelRoPatch(boot_image_offset);
      EmitPcRelativePcaddu12iPlaceholder(info_high, temp.AsRegister<XRegister>());
      PcRelativePatchInfo* info_low = NewBootImageRelRoPatch(boot_image_offset, info_high);
      // Note: Boot image is in the low 4GiB and the entry is 32-bit, so emit a 32-bit load.
      EmitPcRelativeLd_wuPlaceholder(
          info_low, temp.AsRegister<XRegister>(), temp.AsRegister<XRegister>());
      break;
    }
    case MethodLoadKind::kBssEntry: {
      PcRelativePatchInfo* info_high = NewMethodBssEntryPatch(invoke->GetMethodReference());
      EmitPcRelativePcaddu12iPlaceholder(info_high, temp.AsRegister<XRegister>());
      PcRelativePatchInfo* info_low =
          NewMethodBssEntryPatch(invoke->GetMethodReference(), info_high);
      EmitPcRelativeLd_dPlaceholder(
          info_low, temp.AsRegister<XRegister>(), temp.AsRegister<XRegister>());
      break;
    }
    case MethodLoadKind::kJitDirectAddress: {
      __ LoadConst64(temp.AsRegister<XRegister>(),
                     reinterpret_cast<uint64_t>(invoke->GetResolvedMethod()));
      break;
    }
    case MethodLoadKind::kRuntimeCall: {
      // Test situation, don't do anything.
      break;
    }
    default: {
      LOG(FATAL) << "Load kind should have already been handled " << load_kind;
      UNREACHABLE();
    }
  }
}

void CodeGeneratorLOONGARCH64::GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke,
                                                      Location temp,
                                                      SlowPathCode* slow_path) {
  UNUSED(temp);
  UNUSED(invoke);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorLOONGARCH64::MaybeGenerateInlineCacheCheck(HInstruction* instruction,
                                                         XRegister klass) {
  // We know the destination of an intrinsic, so no need to record inline caches.
  if (!instruction->GetLocations()->Intrinsified() &&
      GetGraph()->IsCompilingBaseline() &&
      !Runtime::Current()->IsAotCompiler()) {
    DCHECK(!instruction->GetEnvironment()->IsFromInlinedInvoke());
    ScopedProfilingInfoUse spiu(
        Runtime::Current()->GetJit(), GetGraph()->GetArtMethod(), Thread::Current());
    ProfilingInfo* info = spiu.GetProfilingInfo();
    DCHECK(info != nullptr);
    InlineCache* cache = info->GetInlineCache(instruction->GetDexPc());
    uint64_t address = reinterpret_cast64<uint64_t>(cache);
    Loongarch64Label done;
    {
      ScratchRegisterScope srs(GetAssembler());
      XRegister tmp = srs.AllocateXRegister();
      __ LoadConst64(tmp, address);
      __ Load_D(tmp, tmp, InlineCache::ClassesOffset().Int32Value());
      // Fast path for a monomorphic cache.
      __ Beq(klass, tmp, &done);
    }
    InvokeRuntime(kQuickUpdateInlineCache, instruction, instruction->GetDexPc());
    __ Bind(&done);
  }
}

void CodeGeneratorLOONGARCH64::GenerateVirtualCall(HInvokeVirtual* invoke,
                                               Location temp_location,
                                               SlowPathCode* slow_path) {
  // Use the calling convention instead of the location of the receiver, as
  // intrinsics may have put the receiver in a different register. In the intrinsics
  // slow path, the arguments have been moved to the right place, so here we are
  // guaranteed that the receiver is the first register of the calling convention.
  InvokeDexCallingConvention calling_convention;
  XRegister receiver = calling_convention.GetRegisterAt(0);
  XRegister temp = temp_location.AsRegister<XRegister>();
  MemberOffset method_offset =
      mirror::Class::EmbeddedVTableEntryOffset(invoke->GetVTableIndex(), kLoongarch64PointerSize);
  MemberOffset class_offset = mirror::Object::ClassOffset();
  Offset entry_point = ArtMethod::EntryPointFromQuickCompiledCodeOffset(kLoongarch64PointerSize);

  // temp = object->GetClass();
  __ Load_WU(temp, receiver, class_offset.Int32Value());
  MaybeRecordImplicitNullCheck(invoke);
  // Instead of simply (possibly) unpoisoning `temp` here, we should
  // emit a read barrier for the previous class reference load.
  // However this is not required in practice, as this is an
  // intermediate/temporary reference and because the current
  // concurrent copying collector keeps the from-space memory
  // intact/accessible until the end of the marking phase (the
  // concurrent copying collector may not in the future).
  MaybeUnpoisonHeapReference(temp);

  // If we're compiling baseline, update the inline cache.
  MaybeGenerateInlineCacheCheck(invoke, temp);

  // temp = temp->GetMethodAt(method_offset);
  __ Load_D(temp, temp, method_offset.Int32Value());
  // RA = temp->GetEntryPoint();
  __ Load_D(TMP, temp, entry_point.Int32Value());
  // RA();
  __ Jirl(RA, TMP, 0);
  RecordPcInfo(invoke, invoke->GetDexPc(), slow_path);
}

void CodeGeneratorLOONGARCH64::MoveFromReturnRegister(Location trg, DataType::Type type) {
  if (!trg.IsValid()) {
    DCHECK_EQ(type, DataType::Type::kVoid);
    return;
  }

  DCHECK_NE(type, DataType::Type::kVoid);

  if (DataType::IsIntegralType(type) || type == DataType::Type::kReference) {
    XRegister trg_reg = trg.AsRegister<XRegister>();
    XRegister res_reg = Loongarch64ReturnLocation(type).AsRegister<XRegister>();
    if (trg_reg != res_reg) {
      __ Move(trg_reg, res_reg);
    }
  } 
}

void CodeGeneratorLOONGARCH64::PoisonHeapReference(XRegister reg) {
  __ Sub_d(reg, Zero, reg);  // Negate the ref.
}

void CodeGeneratorLOONGARCH64::UnpoisonHeapReference(XRegister reg) {
  __ Sub_d(reg, Zero, reg);  // Negate the ref.
}

inline void CodeGeneratorLOONGARCH64::MaybePoisonHeapReference(XRegister reg) {
  if (kPoisonHeapReferences) {
    PoisonHeapReference(reg);
  }
}

inline void CodeGeneratorLOONGARCH64::MaybeUnpoisonHeapReference(XRegister reg) {
  if (kPoisonHeapReferences) {
    UnpoisonHeapReference(reg);
  }
}

void CodeGeneratorLOONGARCH64::SwapLocations(Location loc1, Location loc2, DataType::Type type) {
  DCHECK(!loc1.IsConstant());
  DCHECK(!loc2.IsConstant());

  if (loc1.Equals(loc2)) {
    return;
  }

  bool is_slot1 = loc1.IsStackSlot() || loc1.IsDoubleStackSlot();
  bool is_slot2 = loc2.IsStackSlot() || loc2.IsDoubleStackSlot();
  bool is_simd1 = loc1.IsSIMDStackSlot();
  bool is_simd2 = loc2.IsSIMDStackSlot();
  bool is_fp_reg1 = loc1.IsFpuRegister();
  bool is_fp_reg2 = loc2.IsFpuRegister();

  if ((is_slot1 != is_slot2) ||
      (loc2.IsRegister() && loc1.IsRegister()) ||
      (is_fp_reg2 && is_fp_reg1)) {
    if ((is_fp_reg2 && is_fp_reg1) && GetGraph()->HasSIMD()) {
      LOG(FATAL) << "Unsupported";
      UNREACHABLE();
    }
    ScratchRegisterScope srs(GetAssembler());
    Location tmp = (is_fp_reg2 || is_fp_reg1)
        ? Location::FpuRegisterLocation(srs.AllocateFRegister())
        : Location::RegisterLocation(srs.AllocateXRegister());
    MoveLocation(tmp, loc1, type);
    MoveLocation(loc1, loc2, type);
    MoveLocation(loc2, tmp, type);
  } else if (is_slot1 && is_slot2) {
    move_resolver_.Exchange(loc1.GetStackIndex(), loc2.GetStackIndex(), loc1.IsDoubleStackSlot());
  } else if (is_simd1 && is_simd2) {
    // TODO(loongarch64): Add VECTOR/SIMD later.
    UNIMPLEMENTED(FATAL) << "Vector extension is unsupported";
  } else if ((is_fp_reg1 && is_simd2) || (is_fp_reg2 && is_simd1)) {
    // TODO(loongarch64): Add VECTOR/SIMD later.
    UNIMPLEMENTED(FATAL) << "Vector extension is unsupported";
  } else {
    LOG(FATAL) << "Unimplemented swap between locations " << loc1 << " and " << loc2;
  }
}

}  // namespace loongarch64
}  // namespace art
