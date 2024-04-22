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
#include "arch/loongarch64/registers_loongarch64.h"
#include "base/macros.h"
#include "intrinsics_list.h"
#include "optimizing/nodes.h"
#include "utils/loongarch64/assembler_loongarch64.h"

namespace art {
namespace loongarch64 {

static constexpr XRegister kCoreCalleeSaves[] = {
    // S1(TR) is excluded as the ART thread register.
    S0, S2, S3, S4, S5, S6, S7, S8, RA
};

static constexpr FRegister kFpuCalleeSaves[] = {
    FS0, FS1, FS2, FS3, FS4, FS5, FS6, FS7
};

#define __                   down_cast<CodeGeneratorLOONGARCH64*>(codegen_)->GetAssembler()->  // NOLINT
#define QUICK_ENTRY_POINT(x) QUICK_ENTRYPOINT_OFFSET(kLoongarch64PointerSize, x).Int32Value()

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
  UNUSED(instruction);
  UNUSED(successor);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  UNUSED(condition_input_index);
  UNUSED(true_target);
  UNUSED(false_target);
  LOG(FATAL) << "Unimplemented";
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

void InstructionCodeGeneratorLOONGARCH64::GenerateIntLongCompare(IfCondition cond,
                                                             bool is_64bit,
                                                             LocationSummary* locations) {
  UNUSED(cond);
  UNUSED(is_64bit);
  UNUSED(locations);
  LOG(FATAL) << "Unimplemented";
}

bool InstructionCodeGeneratorLOONGARCH64::MaterializeIntLongCompare(IfCondition cond,
                                                                bool is_64bit,
                                                                LocationSummary* locations,
                                                                XRegister dest) {
  UNUSED(cond);
  UNUSED(is_64bit);
  UNUSED(locations);
  UNUSED(dest);
  LOG(FATAL) << "UniMplemented";
  UNREACHABLE();
}

void InstructionCodeGeneratorLOONGARCH64::GenerateIntLongCompareAndBranch(IfCondition cond,
                                                                      bool is_64bit,
                                                                      LocationSummary* locations,
                                                                      Loongarch64Label* label) {
  UNUSED(cond);
  UNUSED(is_64bit);
  UNUSED(locations);
  UNUSED(label);
  LOG(FATAL) << "UniMplemented";
}

void InstructionCodeGeneratorLOONGARCH64::GenerateFpCompare(IfCondition cond,
                                                        bool gt_bias,
                                                        DataType::Type type,
                                                        LocationSummary* locations) {
  UNUSED(cond);
  UNUSED(gt_bias);
  UNUSED(type);
  UNUSED(locations);
  LOG(FATAL) << "Unimplemented";
}

bool InstructionCodeGeneratorLOONGARCH64::MaterializeFpCompare(IfCondition cond,
                                                           bool gt_bias,
                                                           DataType::Type type,
                                                           LocationSummary* locations,
                                                           XRegister dest) {
  UNUSED(cond);
  UNUSED(gt_bias);
  UNUSED(type);
  UNUSED(locations);
  UNUSED(dest);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void InstructionCodeGeneratorLOONGARCH64::GenerateFpCompareAndBranch(IfCondition cond,
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
  UNUSED(instruction);
  UNUSED(successor);
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::HandleBinaryOp(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::HandleCondition(HCondition* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::HandleCondition(HCondition* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::HandleShift(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::HandleShift(HBinaryOperation* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitAbove(HAbove* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitAboveOrEqual(HAboveOrEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitAboveOrEqual(HAboveOrEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitAbs(HAbs* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitAbs(HAbs* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitArrayLength(HArrayLength* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitBelow(HBelow* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitBelowOrEqual(HBelowOrEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitBelowOrEqual(HBelowOrEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitBooleanNot(HBooleanNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitBooleanNot(HBooleanNot* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitBoundsCheck(HBoundsCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitBoundsCheck(HBoundsCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitBoundType(HBoundType* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitBoundType(HBoundType* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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

void InstructionCodeGeneratorLOONGARCH64::VisitClassTableGet(HClassTableGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitClearException(HClearException* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitClearException(HClearException* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitConstructorFence(HConstructorFence* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitCurrentMethod(HCurrentMethod* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitCurrentMethod(HCurrentMethod* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitShouldDeoptimizeFlag(HShouldDeoptimizeFlag* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitShouldDeoptimizeFlag(
    HShouldDeoptimizeFlag* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitDoubleConstant(HDoubleConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitEqual(HEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitEqual(HEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitGoto(HGoto* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitGreaterThan(HGreaterThan* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitGreaterThan(HGreaterThan* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitGreaterThanOrEqual(HGreaterThanOrEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitGreaterThanOrEqual(HGreaterThanOrEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitIf(HIf* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitIf(HIf* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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

void LocationsBuilderLOONGARCH64::VisitPredicatedInstanceFieldGet(
    HPredicatedInstanceFieldGet* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitPredicatedInstanceFieldGet(
    HPredicatedInstanceFieldGet* instruction) {
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitIntConstant(HIntConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitLessThan(HLessThan* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitLessThanOrEqual(HLessThanOrEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitLessThanOrEqual(HLessThanOrEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitLongConstant(HLongConstant* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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

void LocationsBuilderLOONGARCH64::VisitNop(HNop* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNop(HNop* instruction) {
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitNotEqual(HNotEqual* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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

void LocationsBuilderLOONGARCH64::VisitParallelMove(HParallelMove* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitParallelMove(HParallelMove* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitParameterValue(HParameterValue* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitParameterValue(HParameterValue* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitPhi(HPhi* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitPhi(HPhi* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitReturn(HReturn* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void LocationsBuilderLOONGARCH64::VisitReturnVoid(HReturnVoid* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitReturnVoid(HReturnVoid* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitSuspendCheck(HSuspendCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
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

void LocationsBuilderLOONGARCH64::VisitVecPredCondition(HVecPredCondition* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

void InstructionCodeGeneratorLOONGARCH64::VisitVecPredCondition(HVecPredCondition* instruction) {
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

}  // namespace detail

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
                    stats),
      assembler_(graph->GetAllocator(),
                 compiler_options.GetInstructionSetFeatures()->AsLoongarch64InstructionSetFeatures()),
      location_builder_(graph, this) {
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorLOONGARCH64::GenerateFrameEntry() { LOG(FATAL) << "Unimplemented"; }
void CodeGeneratorLOONGARCH64::GenerateFrameExit() { LOG(FATAL) << "Unimplemented"; }

void CodeGeneratorLOONGARCH64::Bind(HBasicBlock* block) {
  UNUSED(block);
  LOG(FATAL) << "Unimplemented";
}

size_t CodeGeneratorLOONGARCH64::GetSIMDRegisterWidth() const {
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void CodeGeneratorLOONGARCH64::MoveConstant(Location destination, int32_t value) {
  UNUSED(destination);
  UNUSED(value);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}
void CodeGeneratorLOONGARCH64::MoveLocation(Location dst, Location src, DataType::Type dst_type) {
  UNUSED(dst);
  UNUSED(src);
  UNUSED(dst_type);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
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
  UNUSED(stack_index);
  UNUSED(reg_id);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

size_t CodeGeneratorLOONGARCH64::RestoreCoreRegister(size_t stack_index, uint32_t reg_id) {
  UNUSED(stack_index);
  UNUSED(reg_id);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
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

void CodeGeneratorLOONGARCH64::Finalize(CodeAllocator* allocator) {
  UNUSED(allocator);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

// Generate code to invoke a runtime entry point.
void CodeGeneratorLOONGARCH64::InvokeRuntime(QuickEntrypointEnum entrypoint,
                                         HInstruction* instruction,
                                         uint32_t dex_pc,
                                         SlowPathCode* slow_path) {
  UNUSED(entrypoint);
  UNUSED(instruction);
  UNUSED(dex_pc);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

// Generate code to invoke a runtime entry point, but do not record
// PC-related information in a stack map.
void CodeGeneratorLOONGARCH64::InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                                               HInstruction* instruction,
                                                               SlowPathCode* slow_path) {
  UNUSED(entry_point_offset);
  UNUSED(instruction);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorLOONGARCH64::IncreaseFrame(size_t adjustment) {
  UNUSED(adjustment);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorLOONGARCH64::DecreaseFrame(size_t adjustment) {
  UNUSED(adjustment);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorLOONGARCH64::GenerateNop() { LOG(FATAL) << "Unimplemented"; }

void CodeGeneratorLOONGARCH64::GenerateImplicitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}
void CodeGeneratorLOONGARCH64::GenerateExplicitNullCheck(HNullCheck* instruction) {
  UNUSED(instruction);
  LOG(FATAL) << "Unimplemented";
}

HLoadString::LoadKind CodeGeneratorLOONGARCH64::GetSupportedLoadStringKind(
    HLoadString::LoadKind desired_string_load_kind) {
  UNUSED(desired_string_load_kind);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

HLoadClass::LoadKind CodeGeneratorLOONGARCH64::GetSupportedLoadClassKind(
    HLoadClass::LoadKind desired_class_load_kind) {
  UNUSED(desired_class_load_kind);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

HInvokeStaticOrDirect::DispatchInfo CodeGeneratorLOONGARCH64::GetSupportedInvokeStaticOrDirectDispatch(
    const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info, ArtMethod* method) {
  UNUSED(desired_dispatch_info);
  UNUSED(method);
  LOG(FATAL) << "Unimplemented";
  UNREACHABLE();
}

void CodeGeneratorLOONGARCH64::LoadMethod(MethodLoadKind load_kind, Location temp, HInvoke* invoke) {
  UNUSED(load_kind);
  UNUSED(temp);
  UNUSED(invoke);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorLOONGARCH64::GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke,
                                                      Location temp,
                                                      SlowPathCode* slow_path) {
  UNUSED(temp);
  UNUSED(invoke);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorLOONGARCH64::GenerateVirtualCall(HInvokeVirtual* invoke,
                                               Location temp,
                                               SlowPathCode* slow_path) {
  UNUSED(temp);
  UNUSED(invoke);
  UNUSED(slow_path);
  LOG(FATAL) << "Unimplemented";
}

void CodeGeneratorLOONGARCH64::MoveFromReturnRegister(Location trg, DataType::Type type) {
  UNUSED(trg);
  UNUSED(type);
  LOG(FATAL) << "Unimplemented";
}

}  // namespace riscv64
}  // namespace art
