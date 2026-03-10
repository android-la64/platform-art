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

#include <atomic>

#include "intrinsics_loongarch64.h"
#include "code_generator_loongarch64.h"
#include "intrinsic_objects.h"
#include "intrinsics_utils.h"
#include "runtime.h"
#include "well_known_classes.h"

namespace art {
namespace loongarch64 {

using IntrinsicSlowPathLOONGARCH64 =
    IntrinsicSlowPath<InvokeDexCallingConventionVisitorLOONGARCH64,
                      SlowPathCodeLOONGARCH64,
                      Loongarch64Assembler>;

bool IntrinsicLocationsBuilderLOONGARCH64::TryDispatch(HInvoke* invoke) {
  UNUSED(invoke);
  return false;
}

Loongarch64Assembler* IntrinsicCodeGeneratorLOONGARCH64::GetAssembler() {
  return codegen_->GetAssembler();
}

#define __ GetAssembler()->

static void CreateIntToIntLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void CreateFPToIntLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresRegister());
}

static void CreateFPToFPLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
}

static void CreateFpFpFpToFpNoOverlapLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  DCHECK_EQ(invoke->GetNumberOfArguments(), 3U);
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(0)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(1)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(2)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->GetType()));

  LocationSummary* const locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);

  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetInAt(1, Location::RequiresFpuRegister());
  locations->SetInAt(2, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
}

static void CreateIntToFPLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresFpuRegister());
}

static void CreateFPToFPCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  DCHECK_EQ(invoke->GetNumberOfArguments(), 1U);
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(0)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->GetType()));

  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnMainOnly, kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(0)));
  locations->SetOut(calling_convention.GetReturnLocation(invoke->GetType()));
}

static void CreateFPFPToFPCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  DCHECK_EQ(invoke->GetNumberOfArguments(), 2U);
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(0)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->InputAt(1)->GetType()));
  DCHECK(DataType::IsFloatingPointType(invoke->GetType()));

  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnMainOnly, kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(0)));
  locations->SetInAt(1, Location::FpuRegisterLocation(calling_convention.GetFpuRegisterAt(1)));
  locations->SetOut(calling_convention.GetReturnLocation(invoke->GetType()));
}

static bool GetStringBuilderCountOffset(HInvoke* invoke, uint32_t* offset) {
  ScopedObjectAccess soa(Thread::Current());
  ArtMethod* resolved_method = invoke->GetResolvedMethod();
  if (resolved_method == nullptr) {
    return false;
  }
  ObjPtr<mirror::Class> declaring_class = resolved_method->GetDeclaringClass();
  if (declaring_class == nullptr) {
    return false;
  }
  ArtField* count_field = declaring_class->FindInstanceField("count", "I");
  if (count_field == nullptr) {
    return false;
  }
  *offset = count_field->GetOffset().Uint32Value();
  return true;
}

static void CreateStringBuilderLengthLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  uint32_t count_offset;
  if (!GetStringBuilderCountOffset(invoke, &count_offset)) {
    return;
  }
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  // Slow paths require overlap-friendly outputs; see IntrinsicSlowPath::EmitNativeCode().
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

static void CreateStringBuilderToStringLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  // These intrinsics rely on pNewStringFromStringBuffer/pNewStringFromStringBuilder quick
  // entrypoints. The loongarch64 runtime does not initialize them yet, so using the intrinsic
  // would jump through an invalid TLS slot. Fall back to the normal invoke path instead.
  UNUSED(allocator);
  UNUSED(invoke);
}

template <typename EmitOp>
void EmitMemoryPeek(HInvoke* invoke, EmitOp&& emit_op) {
  LocationSummary* locations = invoke->GetLocations();
  emit_op(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMemoryPeekByte(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMemoryPeekByte(HInvoke* invoke) {
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Ld_B(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMemoryPeekIntNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMemoryPeekIntNative(HInvoke* invoke) {
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Ld_W(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMemoryPeekLongNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMemoryPeekLongNative(HInvoke* invoke) {
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Ld_D(rd, rs1, 0); });
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMemoryPeekShortNative(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMemoryPeekShortNative(HInvoke* invoke) {
  EmitMemoryPeek(invoke, [&](XRegister rd, XRegister rs1) { __ Ld_H(rd, rs1, 0); });
}

static void CreateIntIntToVoidLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
}

static void CreateIntIntToIntLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister());
}

static void CreateIntIntToIntNoOverlapLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void CreateIntIntToIntSlowPathCallLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  // Force kOutputOverlap; see comments in IntrinsicSlowPath::EmitNativeCode.
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

template <typename EmitOp>
void EmitMemoryPoke(HInvoke* invoke, EmitOp&& emit_op) {
  LocationSummary* locations = invoke->GetLocations();
  emit_op(locations->InAt(1).AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

static void GenerateCodeForCalculationCRC32ValueOfBytes(Loongarch64Assembler* assembler,
                                                        XRegister crc,
                                                        XRegister ptr,
                                                        XRegister length,
                                                        XRegister out) {
  Loongarch64Label aligned2;
  Loongarch64Label aligned4;
  Loongarch64Label aligned8;
  Loongarch64Label loop;
  Loongarch64Label process_4bytes;
  Loongarch64Label process_2bytes;
  Loongarch64Label process_1byte;
  Loongarch64Label done;

  ScratchRegisterScope srs(assembler);
  XRegister data = srs.AllocateXRegister();
  XRegister tmp = srs.AllocateXRegister();
  XRegister len = length;

  assembler->Addi_W(out, crc, 0);
  assembler->Nor(out, out, Zero);
  assembler->Addi_W(len, len, 0);

  assembler->Andi(tmp, ptr, 1);
  assembler->Beqz(tmp, &aligned2);
  assembler->Addi_D(len, len, -1);
  assembler->Blt(len, Zero, &done);
  assembler->Ld_BU(data, ptr, 0);
  assembler->Addi_D(ptr, ptr, 1);
  assembler->Crc_w_b_w(out, data, out);

  assembler->Bind(&aligned2);
  assembler->Andi(tmp, ptr, 2);
  assembler->Beqz(tmp, &aligned4);
  assembler->Addi_D(len, len, -2);
  assembler->Blt(len, Zero, &process_1byte);
  assembler->Ld_HU(data, ptr, 0);
  assembler->Addi_D(ptr, ptr, 2);
  assembler->Crc_w_h_w(out, data, out);

  assembler->Bind(&aligned4);
  assembler->Andi(tmp, ptr, 4);
  assembler->Beqz(tmp, &aligned8);
  assembler->Addi_D(len, len, -4);
  assembler->Blt(len, Zero, &process_2bytes);
  assembler->Ld_WU(data, ptr, 0);
  assembler->Addi_D(ptr, ptr, 4);
  assembler->Crc_w_w_w(out, data, out);

  assembler->Bind(&aligned8);
  assembler->Addi_D(len, len, -8);
  assembler->Blt(len, Zero, &process_4bytes);

  assembler->Bind(&loop);
  assembler->Ld_D(data, ptr, 0);
  assembler->Addi_D(ptr, ptr, 8);
  assembler->Addi_D(len, len, -8);
  assembler->Crc_w_d_w(out, data, out);
  assembler->Bge(len, Zero, &loop);

  assembler->Bind(&process_4bytes);
  assembler->Andi(tmp, len, 4);
  assembler->Beqz(tmp, &process_2bytes);
  assembler->Ld_WU(data, ptr, 0);
  assembler->Addi_D(ptr, ptr, 4);
  assembler->Crc_w_w_w(out, data, out);

  assembler->Bind(&process_2bytes);
  assembler->Andi(tmp, len, 2);
  assembler->Beqz(tmp, &process_1byte);
  assembler->Ld_HU(data, ptr, 0);
  assembler->Addi_D(ptr, ptr, 2);
  assembler->Crc_w_h_w(out, data, out);

  assembler->Bind(&process_1byte);
  assembler->Andi(tmp, len, 1);
  assembler->Beqz(tmp, &done);
  assembler->Ld_BU(data, ptr, 0);
  assembler->Crc_w_b_w(out, data, out);

  assembler->Bind(&done);
  assembler->Nor(out, out, Zero);
  assembler->Addi_W(out, out, 0);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMemoryPokeByte(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMemoryPokeByte(HInvoke* invoke) {
  EmitMemoryPoke(invoke, [&](XRegister rs2, XRegister rs1) { __ St_B(rs2, rs1, 0); });
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMemoryPokeIntNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMemoryPokeIntNative(HInvoke* invoke) {
  EmitMemoryPoke(invoke, [&](XRegister rs2, XRegister rs1) { __ St_W(rs2, rs1, 0); });
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMemoryPokeLongNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMemoryPokeLongNative(HInvoke* invoke) {
  EmitMemoryPoke(invoke, [&](XRegister rs2, XRegister rs1) { __ St_D(rs2, rs1, 0); });
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMemoryPokeShortNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMemoryPokeShortNative(HInvoke* invoke) {
  EmitMemoryPoke(invoke, [&](XRegister rs2, XRegister rs1) { __ St_H(rs2, rs1, 0); });
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = GetAssembler();
  __ Movfr2gr_d(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsFpuRegister<FRegister>());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  CreateIntToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = GetAssembler();
  __ Movgr2fr_d(locations->Out().AsFpuRegister<FRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = GetAssembler();
  __ Movfr2gr_s(locations->Out().AsRegister<XRegister>(), locations->InAt(0).AsFpuRegister<FRegister>());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  CreateIntToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = GetAssembler();
  __ Movgr2fr_w(locations->Out().AsFpuRegister<FRegister>(), locations->InAt(0).AsRegister<XRegister>());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitDoubleIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitDoubleIsInfinite(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();

  assembler->Movfr2gr_d(out, in);
  assembler->Bstrpick_d(out, out, 62, 0);
  assembler->LoadConst64(tmp, INT64_C(0x7ff0000000000000));
  assembler->Sub_d(out, out, tmp);
  assembler->Sltui(out, out, 1);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFloatIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFloatIsInfinite(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();

  assembler->Movfr2gr_s(out, in);
  assembler->Bstrpick_d(out, out, 30, 0);
  assembler->LoadConst32(tmp, 0x7f800000);
  assembler->Sub_d(out, out, tmp);
  assembler->Sltui(out, out, 1);
}

#define VISIT_INTRINSIC(name, low, high, type, start_index)                              \
  void IntrinsicLocationsBuilderLOONGARCH64::Visit##name##ValueOf(HInvoke* invoke) {     \
    InvokeRuntimeCallingConvention calling_convention;                                   \
    IntrinsicVisitor::ComputeValueOfLocations(                                           \
        invoke,                                                                          \
        codegen_,                                                                        \
        low,                                                                             \
        (high) - (low) + 1,                                                              \
        calling_convention.GetReturnLocation(DataType::Type::kReference),                \
        Location::RegisterLocation(calling_convention.GetRegisterAt(0)));                \
  }                                                                                      \
  void IntrinsicCodeGeneratorLOONGARCH64::Visit##name##ValueOf(HInvoke* invoke) {        \
    IntrinsicVisitor::ValueOfInfo info =                                                 \
        IntrinsicVisitor::ComputeValueOfInfo(invoke,                                     \
                                             codegen_->GetCompilerOptions(),             \
                                             WellKnownClasses::java_lang_##name##_value, \
                                             low,                                        \
                                             (high) - (low) + 1,                         \
                                             start_index);                               \
    HandleValueOf(invoke, info, type);                                                   \
  }
BOXED_TYPES(VISIT_INTRINSIC)
#undef VISIT_INTRINSIC

void IntrinsicCodeGeneratorLOONGARCH64::HandleValueOf(
    HInvoke* invoke, const IntrinsicVisitor::ValueOfInfo& info, DataType::Type type) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister out = locations->Out().AsRegister<XRegister>();
  ScratchRegisterScope srs(assembler);
  XRegister temp = srs.AllocateXRegister();
  auto allocate_instance = [&]() {
    DCHECK_EQ(out, InvokeRuntimeCallingConvention().GetRegisterAt(0));
    codegen_->LoadIntrinsicDeclaringClass(out, invoke);
    codegen_->InvokeRuntime(kQuickAllocObjectInitialized, invoke, invoke->GetDexPc());
    CheckEntrypointTypes<kQuickAllocObjectWithChecks, void*, mirror::Class*>();
  };

  auto emit_store = [&](XRegister value_reg) {
    switch (type) {
      case DataType::Type::kInt8:
        assembler->Store_B(value_reg, out, info.value_offset);
        break;
      case DataType::Type::kInt16:
      case DataType::Type::kUint16:
        assembler->Store_H(value_reg, out, info.value_offset);
        break;
      case DataType::Type::kInt32:
        assembler->Store_W(value_reg, out, info.value_offset);
        break;
      default:
        LOG(FATAL) << "Unexpected valueOf type " << type;
        UNREACHABLE();
    }
  };

  if (invoke->InputAt(0)->IsIntConstant()) {
    int32_t value = invoke->InputAt(0)->AsIntConstant()->GetValue();
    if (static_cast<uint32_t>(value - info.low) < info.length) {
      DCHECK_NE(info.value_boot_image_reference, IntrinsicVisitor::ValueOfInfo::kInvalidReference);
      codegen_->LoadBootImageAddress(out, info.value_boot_image_reference);
    } else {
      DCHECK(locations->CanCall());
      allocate_instance();
      assembler->LoadConst32(temp, value);
      emit_store(temp);
      // Class pointer and `value` final field stores require a barrier before publication.
      codegen_->GenerateMemoryBarrier(MemBarrierKind::kStoreStore);
    }
  } else {
    DCHECK(locations->CanCall());
    XRegister in = locations->InAt(0).AsRegister<XRegister>();
    Loongarch64Label allocate, done;
    // Check bounds of our cache.
    assembler->LoadConst32(temp, info.low);
    assembler->Sub_d(out, in, temp);
    assembler->LoadConst32(temp, info.length);
    assembler->Bgeu(out, temp, &allocate);
    // If the value is within the bounds, load the object directly from the array.
    codegen_->LoadBootImageAddress(temp, info.array_data_boot_image_reference);
    assembler->Alsl_d(temp, out, temp, 1);  // out * 4 + temp
    assembler->Load_WU(out, temp, 0);
    if (kPoisonHeapReferences) {
      codegen_->UnpoisonHeapReference(out);
    }
    assembler->B(&done);
    assembler->Bind(&allocate);
    // Otherwise allocate and initialize a new object.
    allocate_instance();
    emit_store(in);
    // Class pointer and `value` final field stores require a barrier before publication.
    codegen_->GenerateMemoryBarrier(MemBarrierKind::kStoreStore);
    assembler->Bind(&done);
  }
}

static InstructionCodeGeneratorLOONGARCH64* GetInstructionVisitor(
    CodeGeneratorLOONGARCH64* codegen) {
  return down_cast<InstructionCodeGeneratorLOONGARCH64*>(codegen->GetInstructionVisitor());
}

static void CreateUnsafeGetLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());  // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void GenUnsafeGet(HInvoke* invoke,
                         CodeGeneratorLOONGARCH64* codegen,
                         std::memory_order order,
                         DataType::Type type) {
  DCHECK(type == DataType::Type::kInt8 ||
         type == DataType::Type::kInt32 ||
         type == DataType::Type::kInt64 ||
         type == DataType::Type::kReference);
  LocationSummary* locations = invoke->GetLocations();
  XRegister object = locations->InAt(1).AsRegister<XRegister>();
  XRegister offset = locations->InAt(2).AsRegister<XRegister>();
  Location out = locations->Out();
  Loongarch64Assembler* assembler = codegen->GetAssembler();
  InstructionCodeGeneratorLOONGARCH64* instruction_visitor = GetInstructionVisitor(codegen);

  bool seq_cst_barrier = (order == std::memory_order_seq_cst);
  bool acquire_barrier = seq_cst_barrier || (order == std::memory_order_acquire);
  DCHECK(acquire_barrier || order == std::memory_order_relaxed);

  if (seq_cst_barrier) {
    codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  }

  ScratchRegisterScope srs(assembler);
  XRegister address = srs.AllocateXRegister();
  assembler->Add_d(address, object, offset);
  instruction_visitor->Load(out, address, /*offset=*/0, type);

  if (acquire_barrier) {
    codegen->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);
  }
}

static void CreateUnsafePutLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());  // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
}

static void GenUnsafePut(HInvoke* invoke,
                         CodeGeneratorLOONGARCH64* codegen,
                         std::memory_order order,
                         DataType::Type type) {
  DCHECK(type == DataType::Type::kInt8 ||
         type == DataType::Type::kInt32 ||
         type == DataType::Type::kInt64 ||
         type == DataType::Type::kReference);
  LocationSummary* locations = invoke->GetLocations();
  XRegister object = locations->InAt(1).AsRegister<XRegister>();
  XRegister offset = locations->InAt(2).AsRegister<XRegister>();
  Location value = locations->InAt(3);
  Loongarch64Assembler* assembler = codegen->GetAssembler();
  InstructionCodeGeneratorLOONGARCH64* instruction_visitor = GetInstructionVisitor(codegen);

  if (type == DataType::Type::kReference) {
    bool value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MaybeMarkGCCard(object, value.AsRegister<XRegister>(), value_can_be_null);
  }

  ScratchRegisterScope srs(assembler);
  XRegister address = srs.AllocateXRegister();
  assembler->Add_d(address, object, offset);

  if (order == std::memory_order_seq_cst) {
    instruction_visitor->StoreSeqCst(value, address, /*offset=*/0, type);
  } else {
    if (order == std::memory_order_release) {
      codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyStore);
    } else {
      DCHECK(order == std::memory_order_relaxed);
    }
    instruction_visitor->Store(value, address, /*offset=*/0, type);
  }

}

static void CreateUnsafeCASLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());  // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void GenUnsafeCas(HInvoke* invoke, DataType::Type type, CodeGeneratorLOONGARCH64* codegen) {
  DCHECK(type == DataType::Type::kInt32 ||
         type == DataType::Type::kInt64 ||
         type == DataType::Type::kReference);
  Loongarch64Assembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister out = locations->Out().AsRegister<XRegister>();            // Boolean result.
  XRegister object = locations->InAt(1).AsRegister<XRegister>();       // Object pointer.
  XRegister offset = locations->InAt(2).AsRegister<XRegister>();       // Long offset.
  XRegister expected = locations->InAt(3).AsRegister<XRegister>();     // Expected.
  XRegister new_value = locations->InAt(4).AsRegister<XRegister>();    // New value.

  if (type == DataType::Type::kReference) {
    // Mark card for object assuming new value is stored.
    bool new_value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MaybeMarkGCCard(object, new_value, new_value_can_be_null);
  }

  ScratchRegisterScope srs(assembler);
  XRegister address = srs.AllocateXRegister();
  XRegister old_value = srs.AllocateXRegister();
  assembler->Add_d(address, object, offset);
  assembler->Move(old_value, expected);
  if (type == DataType::Type::kReference) {
    assembler->Amcas_db_w(old_value, new_value, address);
    assembler->Bstrpick_d(old_value, old_value, 31, 0);
  } else if (type == DataType::Type::kInt32) {
    assembler->Amcas_db_w(old_value, new_value, address);
  } else {
    assembler->Amcas_db_d(old_value, new_value, address);
  }
  assembler->Sub_d(out, old_value, expected);
  assembler->Sltui(out, out, 1);
}

enum class UnsafeGetAndUpdateOp {
  kAdd,
  kSet,
};

static void CreateUnsafeGetAndUpdateLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());  // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void GenUnsafeGetAndUpdate(HInvoke* invoke,
                                  DataType::Type type,
                                  UnsafeGetAndUpdateOp op,
                                  CodeGeneratorLOONGARCH64* codegen) {
  DCHECK(type == DataType::Type::kInt32 ||
         type == DataType::Type::kInt64 ||
         type == DataType::Type::kReference);
  Loongarch64Assembler* assembler = codegen->GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister out = locations->Out().AsRegister<XRegister>();        // Result.
  XRegister object = locations->InAt(1).AsRegister<XRegister>();   // Object pointer.
  XRegister offset = locations->InAt(2).AsRegister<XRegister>();   // Long offset.
  XRegister arg = locations->InAt(3).AsRegister<XRegister>();      // New value or addend.

  if (type == DataType::Type::kReference) {
    DCHECK(op == UnsafeGetAndUpdateOp::kSet);
    // Mark card for object as a new value shall be stored.
    bool new_value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MaybeMarkGCCard(object, arg, new_value_can_be_null);
  }

  ScratchRegisterScope srs(assembler);
  XRegister address = srs.AllocateXRegister();
  assembler->Add_d(address, object, offset);

  switch (op) {
    case UnsafeGetAndUpdateOp::kAdd:
      DCHECK_NE(type, DataType::Type::kReference);
      if (type == DataType::Type::kInt32) {
        assembler->Amadd_db_w(out, arg, address);
      } else {
        assembler->Amadd_db_d(out, arg, address);
      }
      break;
    case UnsafeGetAndUpdateOp::kSet:
      if (type == DataType::Type::kReference || type == DataType::Type::kInt32) {
        assembler->Amswap_db_w(out, arg, address);
        if (type == DataType::Type::kReference) {
          assembler->Bstrpick_d(out, out, 31, 0);
        }
      } else {
        assembler->Amswap_db_d(out, arg, address);
      }
      break;
  }
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGet(HInvoke* invoke) {
  VisitJdkUnsafeGet(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGet(HInvoke* invoke) {
  VisitJdkUnsafeGet(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetVolatile(HInvoke* invoke) {
  VisitJdkUnsafeGetVolatile(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetVolatile(HInvoke* invoke) {
  VisitJdkUnsafeGetVolatile(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetLong(HInvoke* invoke) {
  VisitJdkUnsafeGetLong(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetLong(HInvoke* invoke) {
  VisitJdkUnsafeGetLong(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetLongVolatile(HInvoke* invoke) {
  VisitJdkUnsafeGetLongVolatile(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetLongVolatile(HInvoke* invoke) {
  VisitJdkUnsafeGetLongVolatile(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetByte(HInvoke* invoke) {
  VisitJdkUnsafeGetByte(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetByte(HInvoke* invoke) {
  VisitJdkUnsafeGetByte(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGet(HInvoke* invoke) {
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGet(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_relaxed, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetAcquire(HInvoke* invoke) {
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetAcquire(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_acquire, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetVolatile(HInvoke* invoke) {
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_seq_cst, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetObject(HInvoke* invoke) {
  VisitJdkUnsafeGetReference(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetObject(HInvoke* invoke) {
  VisitJdkUnsafeGetReference(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetObjectVolatile(HInvoke* invoke) {
  VisitJdkUnsafeGetReferenceVolatile(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetObjectVolatile(HInvoke* invoke) {
  VisitJdkUnsafeGetReferenceVolatile(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetReference(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetReference(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_relaxed, DataType::Type::kReference);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetReferenceAcquire(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetReferenceAcquire(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_acquire, DataType::Type::kReference);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetReferenceVolatile(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetReferenceVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_seq_cst, DataType::Type::kReference);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetLong(HInvoke* invoke) {
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetLong(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_relaxed, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetLongAcquire(HInvoke* invoke) {
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetLongAcquire(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_acquire, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetLongVolatile(HInvoke* invoke) {
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetLongVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_seq_cst, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetByte(HInvoke* invoke) {
  CreateUnsafeGetLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetByte(HInvoke* invoke) {
  GenUnsafeGet(invoke, codegen_, std::memory_order_relaxed, DataType::Type::kInt8);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePut(HInvoke* invoke) {
  VisitJdkUnsafePut(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePut(HInvoke* invoke) {
  VisitJdkUnsafePut(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutOrdered(HInvoke* invoke) {
  VisitJdkUnsafePutOrdered(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutOrdered(HInvoke* invoke) {
  VisitJdkUnsafePutOrdered(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutVolatile(HInvoke* invoke) {
  VisitJdkUnsafePutVolatile(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutVolatile(HInvoke* invoke) {
  VisitJdkUnsafePutVolatile(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutObject(HInvoke* invoke) {
  VisitJdkUnsafePutReference(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutObject(HInvoke* invoke) {
  VisitJdkUnsafePutReference(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutObjectOrdered(HInvoke* invoke) {
  VisitJdkUnsafePutObjectOrdered(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutObjectOrdered(HInvoke* invoke) {
  VisitJdkUnsafePutObjectOrdered(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutObjectVolatile(HInvoke* invoke) {
  VisitJdkUnsafePutReferenceVolatile(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutObjectVolatile(HInvoke* invoke) {
  VisitJdkUnsafePutReferenceVolatile(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutReference(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutReference(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_relaxed, DataType::Type::kReference);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutObjectOrdered(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutObjectOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_release, DataType::Type::kReference);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutReferenceRelease(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutReferenceRelease(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_release, DataType::Type::kReference);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutReferenceVolatile(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutReferenceVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_seq_cst, DataType::Type::kReference);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutLong(HInvoke* invoke) {
  VisitJdkUnsafePutLong(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutLong(HInvoke* invoke) {
  VisitJdkUnsafePutLong(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutLongOrdered(HInvoke* invoke) {
  VisitJdkUnsafePutLongOrdered(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutLongOrdered(HInvoke* invoke) {
  VisitJdkUnsafePutLongOrdered(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutLongVolatile(HInvoke* invoke) {
  VisitJdkUnsafePutLongVolatile(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutLongVolatile(HInvoke* invoke) {
  VisitJdkUnsafePutLongVolatile(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafePutByte(HInvoke* invoke) {
  VisitJdkUnsafePutByte(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafePutByte(HInvoke* invoke) {
  VisitJdkUnsafePutByte(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePut(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePut(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_relaxed, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutOrdered(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_release, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutRelease(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutRelease(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_release, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutVolatile(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_seq_cst, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutLong(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutLong(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_relaxed, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutLongOrdered(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutLongOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_release, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutLongRelease(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutLongRelease(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_release, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutLongVolatile(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutLongVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_seq_cst, DataType::Type::kInt64);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafePutByte(HInvoke* invoke) {
  CreateUnsafePutLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafePutByte(HInvoke* invoke) {
  GenUnsafePut(invoke, codegen_, std::memory_order_relaxed, DataType::Type::kInt8);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeCASInt(HInvoke* invoke) {
  VisitJdkUnsafeCASInt(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeCASInt(HInvoke* invoke) {
  VisitJdkUnsafeCASInt(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeCASLong(HInvoke* invoke) {
  VisitJdkUnsafeCASLong(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeCASLong(HInvoke* invoke) {
  VisitJdkUnsafeCASLong(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeCASObject(HInvoke* invoke) {
  VisitJdkUnsafeCASObject(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeCASObject(HInvoke* invoke) {
  VisitJdkUnsafeCASObject(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeCASInt(HInvoke* invoke) {
  // `jdk.internal.misc.Unsafe.compareAndSwapInt` has compare-and-set semantics (see javadoc).
  VisitJdkUnsafeCompareAndSetInt(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeCASInt(HInvoke* invoke) {
  // `jdk.internal.misc.Unsafe.compareAndSwapInt` has compare-and-set semantics (see javadoc).
  VisitJdkUnsafeCompareAndSetInt(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeCASLong(HInvoke* invoke) {
  // `jdk.internal.misc.Unsafe.compareAndSwapLong` has compare-and-set semantics (see javadoc).
  VisitJdkUnsafeCompareAndSetLong(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeCASLong(HInvoke* invoke) {
  // `jdk.internal.misc.Unsafe.compareAndSwapLong` has compare-and-set semantics (see javadoc).
  VisitJdkUnsafeCompareAndSetLong(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeCASObject(HInvoke* invoke) {
  // `jdk.internal.misc.Unsafe.compareAndSwapObject` has compare-and-set semantics (see javadoc).
  VisitJdkUnsafeCompareAndSetReference(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeCASObject(HInvoke* invoke) {
  // `jdk.internal.misc.Unsafe.compareAndSwapObject` has compare-and-set semantics (see javadoc).
  VisitJdkUnsafeCompareAndSetReference(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeCompareAndSetInt(HInvoke* invoke) {
  CreateUnsafeCASLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeCompareAndSetInt(HInvoke* invoke) {
  GenUnsafeCas(invoke, DataType::Type::kInt32, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeCompareAndSetLong(HInvoke* invoke) {
  CreateUnsafeCASLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeCompareAndSetLong(HInvoke* invoke) {
  GenUnsafeCas(invoke, DataType::Type::kInt64, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeCompareAndSetReference(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafeCASLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeCompareAndSetReference(HInvoke* invoke) {
  GenUnsafeCas(invoke, DataType::Type::kReference, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetAndAddInt(HInvoke* invoke) {
  VisitJdkUnsafeGetAndAddInt(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetAndAddInt(HInvoke* invoke) {
  VisitJdkUnsafeGetAndAddInt(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetAndAddLong(HInvoke* invoke) {
  VisitJdkUnsafeGetAndAddLong(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetAndAddLong(HInvoke* invoke) {
  VisitJdkUnsafeGetAndAddLong(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetAndSetInt(HInvoke* invoke) {
  VisitJdkUnsafeGetAndSetInt(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetAndSetInt(HInvoke* invoke) {
  VisitJdkUnsafeGetAndSetInt(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetAndSetLong(HInvoke* invoke) {
  VisitJdkUnsafeGetAndSetLong(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetAndSetLong(HInvoke* invoke) {
  VisitJdkUnsafeGetAndSetLong(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitUnsafeGetAndSetObject(HInvoke* invoke) {
  VisitJdkUnsafeGetAndSetReference(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitUnsafeGetAndSetObject(HInvoke* invoke) {
  VisitJdkUnsafeGetAndSetReference(invoke);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetAndAddInt(HInvoke* invoke) {
  CreateUnsafeGetAndUpdateLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetAndAddInt(HInvoke* invoke) {
  GenUnsafeGetAndUpdate(invoke, DataType::Type::kInt32, UnsafeGetAndUpdateOp::kAdd, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetAndAddLong(HInvoke* invoke) {
  CreateUnsafeGetAndUpdateLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetAndAddLong(HInvoke* invoke) {
  GenUnsafeGetAndUpdate(invoke, DataType::Type::kInt64, UnsafeGetAndUpdateOp::kAdd, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetAndSetInt(HInvoke* invoke) {
  CreateUnsafeGetAndUpdateLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetAndSetInt(HInvoke* invoke) {
  GenUnsafeGetAndUpdate(invoke, DataType::Type::kInt32, UnsafeGetAndUpdateOp::kSet, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetAndSetLong(HInvoke* invoke) {
  CreateUnsafeGetAndUpdateLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetAndSetLong(HInvoke* invoke) {
  GenUnsafeGetAndUpdate(invoke, DataType::Type::kInt64, UnsafeGetAndUpdateOp::kSet, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitJdkUnsafeGetAndSetReference(HInvoke* invoke) {
  if (codegen_->EmitReadBarrier() || kPoisonHeapReferences) {
    return;
  }
  CreateUnsafeGetAndUpdateLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitJdkUnsafeGetAndSetReference(HInvoke* invoke) {
  GenUnsafeGetAndUpdate(invoke, DataType::Type::kReference, UnsafeGetAndUpdateOp::kSet, codegen_);
}

static bool IsKnownFieldVarHandle(HInvoke* invoke) {
  VarHandleOptimizations optimizations(invoke);
  if (optimizations.GetDoNotIntrinsify() || !optimizations.GetUseKnownImageVarHandle()) {
    return false;
  }
  // Handle static/instance fields only. Array coordinates (including byte-array views) are
  // intentionally left to the managed slow path in this batch.
  return GetExpectedVarHandleCoordinatesCount(invoke) <= 1u;
}

static bool IsVarHandleTypeSupportedOnLOONGARCH64(HInvoke* invoke,
                                                  CodeGeneratorLOONGARCH64* codegen,
                                                  DataType::Type type) {
  if (type != DataType::Type::kInt32 &&
      type != DataType::Type::kInt64 &&
      type != DataType::Type::kReference) {
    return false;
  }
  if (type == DataType::Type::kReference && (codegen->EmitReadBarrier() || kPoisonHeapReferences)) {
    return false;
  }
  return IsKnownFieldVarHandle(invoke);
}

static LocationSummary* CreateKnownFieldVarHandleLocations(ArenaAllocator* allocator,
                                                           HInvoke* invoke,
                                                           bool has_output) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  DCHECK_LE(expected_coordinates_count, 1u);

  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);

  // The `VarHandle` object itself is compile-time known for this path.
  locations->SetInAt(0, Location::NoLocation());
  if (expected_coordinates_count == 1u) {
    locations->SetInAt(1, Location::RequiresRegister());
  }
  for (size_t arg_index = 1u + expected_coordinates_count;
       arg_index != invoke->GetNumberOfArguments();
       ++arg_index) {
    locations->SetInAt(arg_index, Location::RequiresRegister());
  }
  if (has_output) {
    // Slow paths require overlap-friendly outputs; see IntrinsicSlowPath::EmitNativeCode().
    locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
  }

  // Offset temporary.
  locations->AddTemp(Location::RequiresRegister());
  if (expected_coordinates_count == 0u) {
    // Target object for static fields (declaring class).
    locations->AddTemp(Location::RequiresRegister());
  }
  return locations;
}

struct KnownFieldVarHandleTarget {
  XRegister object;
  XRegister offset;
  bool needs_object_null_check;
};

static KnownFieldVarHandleTarget GetKnownFieldVarHandleTarget(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  DCHECK_LE(expected_coordinates_count, 1u);

  KnownFieldVarHandleTarget target;
  target.offset = locations->GetTemp(0u).AsRegister<XRegister>();
  if (expected_coordinates_count == 0u) {
    target.object = locations->GetTemp(1u).AsRegister<XRegister>();
    target.needs_object_null_check = false;
  } else {
    target.object = locations->InAt(1u).AsRegister<XRegister>();
    target.needs_object_null_check = !VarHandleOptimizations(invoke).GetSkipObjectNullCheck();
  }
  return target;
}

static void GenerateKnownFieldVarHandleTarget(HInvoke* invoke,
                                              const KnownFieldVarHandleTarget& target,
                                              CodeGeneratorLOONGARCH64* codegen) {
  size_t expected_coordinates_count = GetExpectedVarHandleCoordinatesCount(invoke);
  DCHECK_LE(expected_coordinates_count, 1u);

  ScopedObjectAccess soa(Thread::Current());
  ArtField* target_field = GetBootImageVarHandleField(invoke);
  if (expected_coordinates_count == 0u) {
    ObjPtr<mirror::Class> declaring_class = target_field->GetDeclaringClass();
    if (Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(declaring_class)) {
      uint32_t boot_image_offset = CodeGenerator::GetBootImageOffset(declaring_class);
      codegen->LoadBootImageRelRoEntry(target.object, boot_image_offset);
    } else {
      codegen->LoadTypeForBootImageIntrinsic(
          target.object,
          TypeReference(&declaring_class->GetDexFile(), declaring_class->GetDexTypeIndex()));
    }
  }
  codegen->GetAssembler()->LoadConst32(target.offset, target_field->GetOffset().Uint32Value());
}

static IntrinsicSlowPathLOONGARCH64* MaybeCreateVarHandleNullCheckSlowPath(
    HInvoke* invoke,
    CodeGeneratorLOONGARCH64* codegen,
    Loongarch64Assembler* assembler,
    const KnownFieldVarHandleTarget& target) {
  if (!target.needs_object_null_check) {
    return nullptr;
  }
  auto* slow_path = new (codegen->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen->AddSlowPath(slow_path);
  assembler->Beqz(target.object, slow_path->GetEntryLabel());
  return slow_path;
}

static void CreateVarHandleGetLocations(HInvoke* invoke,
                                        ArenaAllocator* allocator,
                                        CodeGeneratorLOONGARCH64* codegen) {
  DataType::Type type = invoke->GetType();
  if (!IsVarHandleTypeSupportedOnLOONGARCH64(invoke, codegen, type)) {
    return;
  }
  CreateKnownFieldVarHandleLocations(allocator, invoke, /*has_output=*/ true);
}

static void GenerateVarHandleGet(HInvoke* invoke,
                                 CodeGeneratorLOONGARCH64* codegen,
                                 std::memory_order order) {
  DataType::Type type = invoke->GetType();
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = codegen->GetAssembler();

  KnownFieldVarHandleTarget target = GetKnownFieldVarHandleTarget(invoke);
  GenerateKnownFieldVarHandleTarget(invoke, target, codegen);
  IntrinsicSlowPathLOONGARCH64* slow_path =
      MaybeCreateVarHandleNullCheckSlowPath(invoke, codegen, assembler, target);

  bool seq_cst_barrier = (order == std::memory_order_seq_cst);
  bool acquire_barrier = seq_cst_barrier || (order == std::memory_order_acquire);
  DCHECK(acquire_barrier || order == std::memory_order_relaxed);

  if (seq_cst_barrier) {
    codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  }

  ScratchRegisterScope srs(assembler);
  XRegister address = srs.AllocateXRegister();
  assembler->Add_d(address, target.object, target.offset);
  GetInstructionVisitor(codegen)->Load(locations->Out(), address, /*offset=*/ 0, type);

  if (acquire_barrier) {
    codegen->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);
  }

  if (slow_path != nullptr) {
    assembler->Bind(slow_path->GetExitLabel());
  }
}

static void CreateVarHandleSetLocations(HInvoke* invoke,
                                        ArenaAllocator* allocator,
                                        CodeGeneratorLOONGARCH64* codegen) {
  uint32_t value_index = invoke->GetNumberOfArguments() - 1u;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, value_index);
  if (!IsVarHandleTypeSupportedOnLOONGARCH64(invoke, codegen, value_type)) {
    return;
  }
  CreateKnownFieldVarHandleLocations(allocator, invoke, /*has_output=*/ false);
}

static void GenerateVarHandleSet(HInvoke* invoke,
                                 CodeGeneratorLOONGARCH64* codegen,
                                 std::memory_order order) {
  uint32_t value_index = invoke->GetNumberOfArguments() - 1u;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, value_index);
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = codegen->GetAssembler();

  KnownFieldVarHandleTarget target = GetKnownFieldVarHandleTarget(invoke);
  GenerateKnownFieldVarHandleTarget(invoke, target, codegen);
  IntrinsicSlowPathLOONGARCH64* slow_path =
      MaybeCreateVarHandleNullCheckSlowPath(invoke, codegen, assembler, target);

  Location value = locations->InAt(value_index);
  if (value_type == DataType::Type::kReference) {
    bool value_can_be_null = true;
    codegen->MaybeMarkGCCard(target.object, value.AsRegister<XRegister>(), value_can_be_null);
  }
  ScratchRegisterScope srs(assembler);
  XRegister address = srs.AllocateXRegister();
  assembler->Add_d(address, target.object, target.offset);

  if (order == std::memory_order_seq_cst) {
    GetInstructionVisitor(codegen)->StoreSeqCst(value, address, /*offset=*/ 0, value_type);
  } else {
    if (order == std::memory_order_release) {
      codegen->GenerateMemoryBarrier(MemBarrierKind::kAnyStore);
    } else {
      DCHECK(order == std::memory_order_relaxed);
    }
    GetInstructionVisitor(codegen)->Store(value, address, /*offset=*/ 0, value_type);
  }

  if (slow_path != nullptr) {
    assembler->Bind(slow_path->GetExitLabel());
  }
}

static void CreateVarHandleCompareAndSetOrExchangeLocations(HInvoke* invoke,
                                                            ArenaAllocator* allocator,
                                                            CodeGeneratorLOONGARCH64* codegen) {
  uint32_t expected_index = invoke->GetNumberOfArguments() - 2u;
  uint32_t new_value_index = invoke->GetNumberOfArguments() - 1u;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, new_value_index);
  if (value_type != GetDataTypeFromShorty(invoke, expected_index) ||
      !IsVarHandleTypeSupportedOnLOONGARCH64(invoke, codegen, value_type)) {
    return;
  }
  CreateKnownFieldVarHandleLocations(allocator, invoke, /*has_output=*/ true);
}

static void GenerateVarHandleCompareAndSetOrExchange(HInvoke* invoke,
                                                     CodeGeneratorLOONGARCH64* codegen,
                                                     [[maybe_unused]] std::memory_order order,
                                                     bool return_success,
                                                     [[maybe_unused]] bool strong) {
  uint32_t expected_index = invoke->GetNumberOfArguments() - 2u;
  uint32_t new_value_index = invoke->GetNumberOfArguments() - 1u;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, new_value_index);
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = codegen->GetAssembler();

  KnownFieldVarHandleTarget target = GetKnownFieldVarHandleTarget(invoke);
  GenerateKnownFieldVarHandleTarget(invoke, target, codegen);
  IntrinsicSlowPathLOONGARCH64* slow_path =
      MaybeCreateVarHandleNullCheckSlowPath(invoke, codegen, assembler, target);

  XRegister expected = locations->InAt(expected_index).AsRegister<XRegister>();
  XRegister new_value = locations->InAt(new_value_index).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  if (value_type == DataType::Type::kReference) {
    bool value_can_be_null = true;
    codegen->MaybeMarkGCCard(target.object, new_value, value_can_be_null);
  }

  ScratchRegisterScope srs(assembler);
  XRegister address = srs.AllocateXRegister();
  XRegister old_value = srs.AllocateXRegister();
  assembler->Add_d(address, target.object, target.offset);
  assembler->Move(old_value, expected);
  if (value_type == DataType::Type::kReference) {
    assembler->Amcas_db_w(old_value, new_value, address);
    assembler->Bstrpick_d(old_value, old_value, 31, 0);
  } else if (value_type == DataType::Type::kInt32) {
    assembler->Amcas_db_w(old_value, new_value, address);
  } else {
    DCHECK_EQ(value_type, DataType::Type::kInt64);
    assembler->Amcas_db_d(old_value, new_value, address);
  }

  if (return_success) {
    assembler->Sub_d(out, old_value, expected);
    assembler->Sltui(out, out, 1);
  } else {
    assembler->Move(out, old_value);
  }

  if (slow_path != nullptr) {
    assembler->Bind(slow_path->GetExitLabel());
  }
}

enum class VarHandleGetAndUpdateOp {
  kSet,
  kAdd,
  kAnd,
  kOr,
  kXor,
};

static bool IsVarHandleGetAndUpdateTypeSupported(HInvoke* invoke,
                                                 CodeGeneratorLOONGARCH64* codegen,
                                                 VarHandleGetAndUpdateOp op,
                                                 DataType::Type type) {
  if (!IsKnownFieldVarHandle(invoke)) {
    return false;
  }
  if (type == DataType::Type::kReference) {
    if (op != VarHandleGetAndUpdateOp::kSet) {
      return false;
    }
    return !codegen->EmitReadBarrier() && !kPoisonHeapReferences;
  }
  return type == DataType::Type::kInt32 || type == DataType::Type::kInt64;
}

static void CreateVarHandleGetAndUpdateLocations(HInvoke* invoke,
                                                 ArenaAllocator* allocator,
                                                 CodeGeneratorLOONGARCH64* codegen,
                                                 VarHandleGetAndUpdateOp op) {
  uint32_t arg_index = invoke->GetNumberOfArguments() - 1u;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, arg_index);
  if (!IsVarHandleGetAndUpdateTypeSupported(invoke, codegen, op, value_type)) {
    return;
  }
  CreateKnownFieldVarHandleLocations(allocator, invoke, /*has_output=*/ true);
}

static void GenerateVarHandleGetAndUpdate(HInvoke* invoke,
                                          CodeGeneratorLOONGARCH64* codegen,
                                          VarHandleGetAndUpdateOp op,
                                          [[maybe_unused]] std::memory_order order) {
  uint32_t arg_index = invoke->GetNumberOfArguments() - 1u;
  DataType::Type value_type = GetDataTypeFromShorty(invoke, arg_index);
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = codegen->GetAssembler();

  KnownFieldVarHandleTarget target = GetKnownFieldVarHandleTarget(invoke);
  GenerateKnownFieldVarHandleTarget(invoke, target, codegen);
  IntrinsicSlowPathLOONGARCH64* slow_path =
      MaybeCreateVarHandleNullCheckSlowPath(invoke, codegen, assembler, target);

  XRegister arg = locations->InAt(arg_index).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  if (value_type == DataType::Type::kReference) {
    bool value_can_be_null = true;
    codegen->MaybeMarkGCCard(target.object, arg, value_can_be_null);
  }
  ScratchRegisterScope srs(assembler);
  XRegister address = srs.AllocateXRegister();
  assembler->Add_d(address, target.object, target.offset);

  if (value_type == DataType::Type::kReference) {
    DCHECK(op == VarHandleGetAndUpdateOp::kSet);
    assembler->Amswap_db_w(out, arg, address);
    assembler->Bstrpick_d(out, out, 31, 0);
  } else if (value_type == DataType::Type::kInt32) {
    switch (op) {
      case VarHandleGetAndUpdateOp::kSet:
        assembler->Amswap_db_w(out, arg, address);
        break;
      case VarHandleGetAndUpdateOp::kAdd:
        assembler->Amadd_db_w(out, arg, address);
        break;
      case VarHandleGetAndUpdateOp::kAnd:
        assembler->Amand_db_w(out, arg, address);
        break;
      case VarHandleGetAndUpdateOp::kOr:
        assembler->Amor_db_w(out, arg, address);
        break;
      case VarHandleGetAndUpdateOp::kXor:
        assembler->Amxor_db_w(out, arg, address);
        break;
    }
  } else {
    DCHECK_EQ(value_type, DataType::Type::kInt64);
    switch (op) {
      case VarHandleGetAndUpdateOp::kSet:
        assembler->Amswap_db_d(out, arg, address);
        break;
      case VarHandleGetAndUpdateOp::kAdd:
        assembler->Amadd_db_d(out, arg, address);
        break;
      case VarHandleGetAndUpdateOp::kAnd:
        assembler->Amand_db_d(out, arg, address);
        break;
      case VarHandleGetAndUpdateOp::kOr:
        assembler->Amor_db_d(out, arg, address);
        break;
      case VarHandleGetAndUpdateOp::kXor:
        assembler->Amxor_db_d(out, arg, address);
        break;
    }
  }

  if (slow_path != nullptr) {
    assembler->Bind(slow_path->GetExitLabel());
  }
}

#define VAR_HANDLE_GET_INTRINSIC(Name, order)                                        \
  void IntrinsicLocationsBuilderLOONGARCH64::Visit##Name(HInvoke* invoke) {          \
    CreateVarHandleGetLocations(invoke, allocator_, codegen_);                       \
  }                                                                                   \
  void IntrinsicCodeGeneratorLOONGARCH64::Visit##Name(HInvoke* invoke) {             \
    GenerateVarHandleGet(invoke, codegen_, order);                                   \
  }

VAR_HANDLE_GET_INTRINSIC(VarHandleGet, std::memory_order_relaxed)
VAR_HANDLE_GET_INTRINSIC(VarHandleGetOpaque, std::memory_order_relaxed)
VAR_HANDLE_GET_INTRINSIC(VarHandleGetAcquire, std::memory_order_acquire)
VAR_HANDLE_GET_INTRINSIC(VarHandleGetVolatile, std::memory_order_seq_cst)

#undef VAR_HANDLE_GET_INTRINSIC

#define VAR_HANDLE_SET_INTRINSIC(Name, order)                                        \
  void IntrinsicLocationsBuilderLOONGARCH64::Visit##Name(HInvoke* invoke) {          \
    CreateVarHandleSetLocations(invoke, allocator_, codegen_);                       \
  }                                                                                   \
  void IntrinsicCodeGeneratorLOONGARCH64::Visit##Name(HInvoke* invoke) {             \
    GenerateVarHandleSet(invoke, codegen_, order);                                   \
  }

VAR_HANDLE_SET_INTRINSIC(VarHandleSet, std::memory_order_relaxed)
VAR_HANDLE_SET_INTRINSIC(VarHandleSetOpaque, std::memory_order_relaxed)
VAR_HANDLE_SET_INTRINSIC(VarHandleSetRelease, std::memory_order_release)
VAR_HANDLE_SET_INTRINSIC(VarHandleSetVolatile, std::memory_order_seq_cst)

#undef VAR_HANDLE_SET_INTRINSIC

#define VAR_HANDLE_CAS_INTRINSIC(Name, order, return_success, strong)                 \
  void IntrinsicLocationsBuilderLOONGARCH64::Visit##Name(HInvoke* invoke) {           \
    CreateVarHandleCompareAndSetOrExchangeLocations(invoke, allocator_, codegen_);    \
  }                                                                                    \
  void IntrinsicCodeGeneratorLOONGARCH64::Visit##Name(HInvoke* invoke) {              \
    GenerateVarHandleCompareAndSetOrExchange(                                          \
        invoke, codegen_, order, return_success, strong);                              \
  }

VAR_HANDLE_CAS_INTRINSIC(
    VarHandleCompareAndExchange, std::memory_order_seq_cst, /*return_success=*/ false, /*strong=*/ true)
VAR_HANDLE_CAS_INTRINSIC(
    VarHandleCompareAndExchangeAcquire, std::memory_order_acquire, /*return_success=*/ false, /*strong=*/ true)
VAR_HANDLE_CAS_INTRINSIC(
    VarHandleCompareAndExchangeRelease, std::memory_order_release, /*return_success=*/ false, /*strong=*/ true)
VAR_HANDLE_CAS_INTRINSIC(
    VarHandleCompareAndSet, std::memory_order_seq_cst, /*return_success=*/ true, /*strong=*/ true)
VAR_HANDLE_CAS_INTRINSIC(
    VarHandleWeakCompareAndSet, std::memory_order_seq_cst, /*return_success=*/ true, /*strong=*/ false)
VAR_HANDLE_CAS_INTRINSIC(
    VarHandleWeakCompareAndSetAcquire, std::memory_order_acquire, /*return_success=*/ true, /*strong=*/ false)
VAR_HANDLE_CAS_INTRINSIC(
    VarHandleWeakCompareAndSetPlain, std::memory_order_relaxed, /*return_success=*/ true, /*strong=*/ false)
VAR_HANDLE_CAS_INTRINSIC(
    VarHandleWeakCompareAndSetRelease, std::memory_order_release, /*return_success=*/ true, /*strong=*/ false)

#undef VAR_HANDLE_CAS_INTRINSIC

#define VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(Name, order, op)                          \
  void IntrinsicLocationsBuilderLOONGARCH64::Visit##Name(HInvoke* invoke) {           \
    CreateVarHandleGetAndUpdateLocations(invoke, allocator_, codegen_, op);           \
  }                                                                                    \
  void IntrinsicCodeGeneratorLOONGARCH64::Visit##Name(HInvoke* invoke) {              \
    GenerateVarHandleGetAndUpdate(invoke, codegen_, op, order);                       \
  }

VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndSet, std::memory_order_seq_cst, VarHandleGetAndUpdateOp::kSet)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndSetAcquire, std::memory_order_acquire, VarHandleGetAndUpdateOp::kSet)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndSetRelease, std::memory_order_release, VarHandleGetAndUpdateOp::kSet)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndAdd, std::memory_order_seq_cst, VarHandleGetAndUpdateOp::kAdd)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndAddAcquire, std::memory_order_acquire, VarHandleGetAndUpdateOp::kAdd)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndAddRelease, std::memory_order_release, VarHandleGetAndUpdateOp::kAdd)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseAnd, std::memory_order_seq_cst, VarHandleGetAndUpdateOp::kAnd)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseAndAcquire, std::memory_order_acquire, VarHandleGetAndUpdateOp::kAnd)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseAndRelease, std::memory_order_release, VarHandleGetAndUpdateOp::kAnd)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseOr, std::memory_order_seq_cst, VarHandleGetAndUpdateOp::kOr)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseOrAcquire, std::memory_order_acquire, VarHandleGetAndUpdateOp::kOr)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseOrRelease, std::memory_order_release, VarHandleGetAndUpdateOp::kOr)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseXor, std::memory_order_seq_cst, VarHandleGetAndUpdateOp::kXor)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseXorAcquire, std::memory_order_acquire, VarHandleGetAndUpdateOp::kXor)
VAR_HANDLE_GET_AND_UPDATE_INTRINSIC(
    VarHandleGetAndBitwiseXorRelease, std::memory_order_release, VarHandleGetAndUpdateOp::kXor)

#undef VAR_HANDLE_GET_AND_UPDATE_INTRINSIC

void IntrinsicLocationsBuilderLOONGARCH64::VisitReferenceGetReferent(HInvoke* invoke) {
  IntrinsicVisitor::CreateReferenceGetReferentLocations(invoke, codegen_);

  if (codegen_->EmitBakerReadBarrier() && invoke->GetLocations() != nullptr) {
    invoke->GetLocations()->AddTemp(Location::RequiresRegister());
  }
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitReferenceGetReferent(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  Location obj = locations->InAt(0);
  Location out = locations->Out();

  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen_->AddSlowPath(slow_path);

  if (codegen_->EmitReadBarrier()) {
    // Check self->GetWeakRefAccessEnabled().
    ScratchRegisterScope srs(assembler);
    XRegister temp = srs.AllocateXRegister();
    assembler->Load_W(temp, TR, Thread::WeakRefAccessEnabledOffset<kLoongarch64PointerSize>().Int32Value());
    static_assert(enum_cast<int32_t>(WeakRefAccessState::kVisiblyEnabled) == 0);
    assembler->Bnez(temp, slow_path->GetEntryLabel());
  }

  {
    // Load the java.lang.ref.Reference class.
    ScratchRegisterScope srs(assembler);
    XRegister temp = srs.AllocateXRegister();
    codegen_->LoadIntrinsicDeclaringClass(temp, invoke);

    // Check static fields java.lang.ref.Reference.{disableIntrinsic,slowPathEnabled} together.
    MemberOffset disable_intrinsic_offset = IntrinsicVisitor::GetReferenceDisableIntrinsicOffset();
    DCHECK_ALIGNED(disable_intrinsic_offset.Uint32Value(), 2u);
    DCHECK_EQ(disable_intrinsic_offset.Uint32Value() + 1u,
              IntrinsicVisitor::GetReferenceSlowPathEnabledOffset().Uint32Value());
    assembler->Load_HU(temp, temp, disable_intrinsic_offset.Int32Value());
    assembler->Bnez(temp, slow_path->GetEntryLabel());
  }

  // Load the value from the field.
  uint32_t referent_offset = mirror::Reference::ReferentOffset().Uint32Value();
  if (codegen_->EmitBakerReadBarrier()) {
    codegen_->GenerateFieldLoadWithBakerReadBarrier(invoke,
                                                    out,
                                                    obj.AsRegister<XRegister>(),
                                                    referent_offset,
                                                    /*temp=*/locations->GetTemp(0),
                                                    /*needs_null_check=*/false);
  } else {
    auto* instruction_visitor =
        down_cast<InstructionCodeGeneratorLOONGARCH64*>(codegen_->GetInstructionVisitor());
    instruction_visitor->Load(
        out, obj.AsRegister<XRegister>(), referent_offset, DataType::Type::kReference);
    codegen_->MaybeGenerateReadBarrierSlow(invoke, out, out, obj, referent_offset);
  }
  // Emit memory barrier for load-acquire.
  codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);
  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitReferenceRefersTo(HInvoke* invoke) {
  IntrinsicVisitor::CreateReferenceRefersToLocations(invoke, codegen_);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitReferenceRefersTo(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister obj = locations->InAt(0).AsRegister<XRegister>();
  XRegister other = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  uint32_t referent_offset = mirror::Reference::ReferentOffset().Uint32Value();
  uint32_t monitor_offset = mirror::Object::MonitorOffset().Int32Value();

  auto* instruction_visitor =
      down_cast<InstructionCodeGeneratorLOONGARCH64*>(codegen_->GetInstructionVisitor());
  instruction_visitor->Load(
      Location::RegisterLocation(out), obj, referent_offset, DataType::Type::kReference);
  codegen_->MaybeRecordImplicitNullCheck(invoke);
  if (kPoisonHeapReferences) {
    codegen_->UnpoisonHeapReference(out);
  }

  // Emit memory barrier for load-acquire.
  codegen_->GenerateMemoryBarrier(MemBarrierKind::kLoadAny);

  if (codegen_->EmitReadBarrier()) {
    DCHECK(kUseBakerReadBarrier);

    Loongarch64Label calculate_result;

    // If equal to `other`, the loaded reference is final (it cannot be a from-space reference).
    assembler->Beq(out, other, &calculate_result);

    // If the GC is not marking, the loaded reference is final.
    ScratchRegisterScope srs(assembler);
    XRegister temp = srs.AllocateXRegister();
    assembler->Load_W(temp, TR, Thread::IsGcMarkingOffset<kLoongarch64PointerSize>().Int32Value());
    assembler->Beqz(temp, &calculate_result);

    // Check if the loaded reference is null.
    assembler->Beqz(out, &calculate_result);

    // For correct memory visibility, we need a barrier before loading the lock word to
    // synchronize with the publishing of `other` by the CC GC. However, as long as the
    // load-acquire above is implemented as a plain load followed by a barrier (rather
    // than an atomic load-acquire instruction which synchronizes only with other
    // instructions on the same memory location), that barrier is sufficient.

    // Load the lockword and check if it is a forwarding address.
    static_assert(LockWord::kStateShift == 30u);
    static_assert(LockWord::kStateForwardingAddress == 3u);
    // Load the lock word sign-extended. Comparing it to the sign-extended forwarding
    // address bits as unsigned is the same as comparing both zero-extended.
    assembler->Load_W(temp, out, monitor_offset);
    // Materialize sign-extended forwarding address bits.
    XRegister temp2 = srs.AllocateXRegister();
    assembler->LoadConst64(
        temp2, INT64_C(-1) & ~static_cast<int64_t>((1 << LockWord::kStateShift) - 1));
    // If we do not have a forwarding address, the loaded reference cannot be the same as `other`,
    // so we proceed to calculate the result with `out != other`.
    assembler->Bltu(temp, temp2, &calculate_result);

    // Extract the forwarding address for comparison with `other`.
    // Note that the high 32 bits shall not be used for the result calculation.
    assembler->Slli_w(out, temp, LockWord::kForwardingAddressShift);

    assembler->Bind(&calculate_result);
  }

  // Calculate the result `out == other`.
  assembler->Sub_d(out, out, other);
  assembler->Sltui(out, out, 1);
}

static void CreateSystemArrayCopyPrimitiveLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  // arraycopy(T[] src, int src_pos, T[] dst, int dst_pos, int length)
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
}

static void CheckSystemArrayCopyPrimitivePosition(Loongarch64Assembler* assembler,
                                                  XRegister array,
                                                  XRegister pos,
                                                  XRegister length,
                                                  SlowPathCodeLOONGARCH64* slow_path,
                                                  XRegister temp) {
  const int32_t array_length_offset = mirror::Array::LengthOffset().Int32Value();
  // Calculate array.length - pos.
  assembler->Load_W(temp, array, array_length_offset);
  assembler->Sub_w(temp, temp, pos);
  // Require (array.length - pos) >= length.
  assembler->Blt(temp, length, slow_path->GetEntryLabel());
}

static void GenPrimitiveArrayAddress(Loongarch64Assembler* assembler,
                                     XRegister out,
                                     XRegister array,
                                     XRegister pos,
                                     DataType::Type type,
                                     XRegister temp) {
  DCHECK(type == DataType::Type::kInt8 ||
         type == DataType::Type::kUint16 ||
         type == DataType::Type::kInt32);
  const int32_t data_offset = mirror::Array::DataOffset(DataType::Size(type)).Int32Value();
  switch (type) {
    case DataType::Type::kInt8:
      assembler->Add_d(out, array, pos);
      break;
    case DataType::Type::kUint16:
      assembler->Slli_d(temp, pos, 1);
      assembler->Add_d(out, array, temp);
      break;
    case DataType::Type::kInt32:
      assembler->Slli_d(temp, pos, 2);
      assembler->Add_d(out, array, temp);
      break;
    default:
      UNREACHABLE();
  }
  assembler->Addi_D(out, out, data_offset);
}

static void SystemArrayCopyPrimitive(HInvoke* invoke,
                                     Loongarch64Assembler* assembler,
                                     CodeGeneratorLOONGARCH64* codegen,
                                     DataType::Type type) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister src = locations->InAt(0).AsRegister<XRegister>();
  XRegister src_pos = locations->InAt(1).AsRegister<XRegister>();
  XRegister dst = locations->InAt(2).AsRegister<XRegister>();
  XRegister dst_pos = locations->InAt(3).AsRegister<XRegister>();
  XRegister length = locations->InAt(4).AsRegister<XRegister>();

  XRegister src_base = locations->GetTemp(0).AsRegister<XRegister>();
  XRegister dst_base = locations->GetTemp(1).AsRegister<XRegister>();
  XRegister count = locations->GetTemp(2).AsRegister<XRegister>();
  XRegister temp = locations->GetTemp(3).AsRegister<XRegister>();

  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen->AddSlowPath(slow_path);

  // Handle overlap/null/out-of-bounds in managed slow path.
  assembler->Beq(src, dst, slow_path->GetEntryLabel());
  assembler->Beqz(src, slow_path->GetEntryLabel());
  assembler->Beqz(dst, slow_path->GetEntryLabel());
  assembler->Bltz(length, slow_path->GetEntryLabel());
  assembler->Bltz(src_pos, slow_path->GetEntryLabel());
  assembler->Bltz(dst_pos, slow_path->GetEntryLabel());

  CheckSystemArrayCopyPrimitivePosition(assembler, src, src_pos, length, slow_path, temp);
  CheckSystemArrayCopyPrimitivePosition(assembler, dst, dst_pos, length, slow_path, temp);

  GenPrimitiveArrayAddress(assembler, src_base, src, src_pos, type, temp);
  GenPrimitiveArrayAddress(assembler, dst_base, dst, dst_pos, type, temp);

  assembler->Move(count, length);
  Loongarch64Label done;
  Loongarch64Label loop;
  assembler->Beqz(count, &done);

  const int32_t element_size = DataType::Size(type);
  assembler->Bind(&loop);
  switch (type) {
    case DataType::Type::kInt8:
      assembler->Load_BU(temp, src_base, 0);
      assembler->Store_B(temp, dst_base, 0);
      break;
    case DataType::Type::kUint16:
      assembler->Load_HU(temp, src_base, 0);
      assembler->Store_H(temp, dst_base, 0);
      break;
    case DataType::Type::kInt32:
      assembler->Load_WU(temp, src_base, 0);
      assembler->Store_W(temp, dst_base, 0);
      break;
    default:
      UNREACHABLE();
  }
  assembler->Addi_D(src_base, src_base, element_size);
  assembler->Addi_D(dst_base, dst_base, element_size);
  assembler->Addi_D(count, count, -1);
  assembler->Blt(Zero, count, &loop);

  assembler->Bind(&done);
  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitSystemArrayCopyByte(HInvoke* invoke) {
  CreateSystemArrayCopyPrimitiveLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitSystemArrayCopyByte(HInvoke* invoke) {
  SystemArrayCopyPrimitive(invoke, GetAssembler(), codegen_, DataType::Type::kInt8);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitSystemArrayCopyInt(HInvoke* invoke) {
  CreateSystemArrayCopyPrimitiveLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitSystemArrayCopyInt(HInvoke* invoke) {
  SystemArrayCopyPrimitive(invoke, GetAssembler(), codegen_, DataType::Type::kInt32);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitSystemArrayCopyChar(HInvoke* invoke) {
  CreateSystemArrayCopyPrimitiveLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitSystemArrayCopyChar(HInvoke* invoke) {
  SystemArrayCopyPrimitive(invoke, GetAssembler(), codegen_, DataType::Type::kUint16);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitSystemArrayCopy(HInvoke* invoke) {
  // Keep this intrinsic for supported configurations only.
  if (codegen_->EmitNonBakerReadBarrier() || kPoisonHeapReferences) {
    return;
  }

  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  // arraycopy(Object[] src, int src_pos, Object[] dst, int dst_pos, int length)
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());

  // class_src, class_dst, src_index, dst_index, remaining, value, temp_addr
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  if (codegen_->EmitBakerReadBarrier()) {
    locations->AddTemp(Location::RequiresRegister());
  }
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitSystemArrayCopy(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  auto* instruction_visitor =
      down_cast<InstructionCodeGeneratorLOONGARCH64*>(codegen_->GetInstructionVisitor());

  const int32_t class_offset = mirror::Object::ClassOffset().Int32Value();
  const int32_t component_offset = mirror::Class::ComponentTypeOffset().Int32Value();
  const int32_t primitive_offset = mirror::Class::PrimitiveTypeOffset().Int32Value();
  const int32_t ref_data_offset =
      mirror::Array::DataOffset(DataType::Size(DataType::Type::kReference)).Int32Value();

  XRegister src = locations->InAt(0).AsRegister<XRegister>();
  XRegister src_pos = locations->InAt(1).AsRegister<XRegister>();
  XRegister dst = locations->InAt(2).AsRegister<XRegister>();
  XRegister dst_pos = locations->InAt(3).AsRegister<XRegister>();
  XRegister length = locations->InAt(4).AsRegister<XRegister>();

  XRegister class_src = locations->GetTemp(0).AsRegister<XRegister>();
  XRegister class_dst = locations->GetTemp(1).AsRegister<XRegister>();
  XRegister src_index = locations->GetTemp(2).AsRegister<XRegister>();
  XRegister dst_index = locations->GetTemp(3).AsRegister<XRegister>();
  XRegister remaining = locations->GetTemp(4).AsRegister<XRegister>();
  XRegister value = locations->GetTemp(5).AsRegister<XRegister>();
  XRegister temp = locations->GetTemp(6).AsRegister<XRegister>();
  XRegister rb_temp = kNoXRegister;
  if (codegen_->EmitBakerReadBarrier()) {
    rb_temp = locations->GetTemp(7).AsRegister<XRegister>();
  }

  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen_->AddSlowPath(slow_path);

  // Conservative fast-path guards.
  assembler->Beq(src, dst, slow_path->GetEntryLabel());
  assembler->Beqz(src, slow_path->GetEntryLabel());
  assembler->Beqz(dst, slow_path->GetEntryLabel());
  assembler->Bltz(length, slow_path->GetEntryLabel());
  assembler->Bltz(src_pos, slow_path->GetEntryLabel());
  assembler->Bltz(dst_pos, slow_path->GetEntryLabel());

  CheckSystemArrayCopyPrimitivePosition(assembler, src, src_pos, length, slow_path, temp);
  CheckSystemArrayCopyPrimitivePosition(assembler, dst, dst_pos, length, slow_path, temp);

  // Fast path for same-class reference arrays only.
  assembler->Load_WU(class_src, src, class_offset);
  assembler->Load_WU(class_dst, dst, class_offset);
  assembler->Bne(class_src, class_dst, slow_path->GetEntryLabel());
  assembler->Load_WU(temp, class_src, component_offset);
  assembler->Beqz(temp, slow_path->GetEntryLabel());
  assembler->Load_HU(temp, temp, primitive_offset);
  assembler->Bnez(temp, slow_path->GetEntryLabel());

  assembler->Move(src_index, src_pos);
  assembler->Move(dst_index, dst_pos);
  assembler->Move(remaining, length);

  Loongarch64Label done;
  Loongarch64Label loop;
  assembler->Beqz(remaining, &done);

  Location value_loc = Location::RegisterLocation(value);
  if (codegen_->EmitBakerReadBarrier()) {
    Location src_index_loc = Location::RegisterLocation(src_index);
    Location rb_temp_loc = Location::RegisterLocation(rb_temp);
    assembler->Bind(&loop);
    codegen_->GenerateArrayLoadWithBakerReadBarrier(
        invoke, value_loc, src, ref_data_offset, src_index_loc, rb_temp_loc, /*needs_null_check=*/false);
    assembler->Slli_d(temp, dst_index, 2);
    assembler->Add_d(temp, temp, dst);
    instruction_visitor->Store(value_loc, temp, ref_data_offset, DataType::Type::kReference);
    assembler->Addi_D(src_index, src_index, 1);
    assembler->Addi_D(dst_index, dst_index, 1);
    assembler->Addi_D(remaining, remaining, -1);
    assembler->Blt(Zero, remaining, &loop);
  } else {
    assembler->Bind(&loop);
    assembler->Slli_d(temp, src_index, 2);
    assembler->Add_d(temp, temp, src);
    instruction_visitor->Load(value_loc, temp, ref_data_offset, DataType::Type::kReference);
    assembler->Slli_d(temp, dst_index, 2);
    assembler->Add_d(temp, temp, dst);
    instruction_visitor->Store(value_loc, temp, ref_data_offset, DataType::Type::kReference);
    assembler->Addi_D(src_index, src_index, 1);
    assembler->Addi_D(dst_index, dst_index, 1);
    assembler->Addi_D(remaining, remaining, -1);
    assembler->Blt(Zero, remaining, &loop);
  }

  codegen_->MarkGCCard(dst);
  assembler->Bind(&done);
  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringEquals(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kOutputOverlap);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringEquals(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  const int32_t count_offset = mirror::String::CountOffset().Int32Value();
  const int32_t value_offset = mirror::String::ValueOffset().Int32Value();
  const int32_t class_offset = mirror::Object::ClassOffset().Int32Value();

  XRegister str = locations->InAt(0).AsRegister<XRegister>();
  XRegister arg = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  ScratchRegisterScope srs(assembler);
  XRegister temp = srs.AllocateXRegister();
  XRegister temp1 = locations->GetTemp(0).AsRegister<XRegister>();
  XRegister temp2 = srs.AllocateXRegister();

  Loongarch64Label loop;
  Loongarch64Label end;
  Loongarch64Label return_true;
  Loongarch64Label return_false;

  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  StringEqualsOptimizations optimizations(invoke);
  if (!optimizations.GetArgumentNotNull()) {
    assembler->Beqz(arg, &return_false);
  }

  assembler->Beq(str, arg, &return_true);

  if (!optimizations.GetArgumentIsString()) {
    AssertNonMovableStringClass();
    assembler->Load_WU(temp, str, class_offset);
    assembler->Load_WU(temp1, arg, class_offset);
    assembler->Bne(temp, temp1, &return_false);
  }

  assembler->Load_WU(temp, str, count_offset);
  assembler->Load_WU(temp1, arg, count_offset);
  assembler->Bne(temp, temp1, &return_false);
  assembler->Beqz(temp, &return_true);

  if (mirror::kUseStringCompression) {
    assembler->Andi(temp1, temp, 1);
    assembler->Srli_w(temp, temp, 1);
    assembler->Sll_w(temp, temp, temp1);
  }

  assembler->LoadConst32(temp1, value_offset);

  assembler->Bind(&loop);
  assembler->Add_d(out, str, temp1);
  assembler->Load_D(out, out, 0);
  assembler->Add_d(temp2, arg, temp1);
  assembler->Load_D(temp2, temp2, 0);
  assembler->Addi_D(temp1, temp1, sizeof(uint64_t));
  assembler->Bne(out, temp2, &return_false);
  assembler->Addi_D(temp, temp, mirror::kUseStringCompression ? -8 : -4);
  assembler->Blt(Zero, temp, &loop);

  assembler->Bind(&return_true);
  assembler->LoadConst32(out, 1);
  assembler->B(&end);

  assembler->Bind(&return_false);
  assembler->Move(out, Zero);
  assembler->Bind(&end);
}

static void GenerateVisitStringIndexOf(HInvoke* invoke,
                                       Loongarch64Assembler* assembler,
                                       CodeGeneratorLOONGARCH64* codegen,
                                       bool start_at_zero) {
  LocationSummary* locations = invoke->GetLocations();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  // Check for code points > 0xFFFF. Either a slow-path check when we don't know statically,
  // or directly dispatch for a large constant, or omit slow-path for a small constant or a char.
  SlowPathCodeLOONGARCH64* slow_path = nullptr;
  HInstruction* code_point = invoke->InputAt(1);
  if (code_point->IsIntConstant()) {
    if (static_cast<uint32_t>(code_point->AsIntConstant()->GetValue()) > 0xFFFFU) {
      // Always needs the slow-path. We could directly dispatch to it, but this case should be
      // rare, so for simplicity just put the full slow-path down and branch unconditionally.
      slow_path = new (codegen->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
      codegen->AddSlowPath(slow_path);
      assembler->B(slow_path->GetEntryLabel());
      assembler->Bind(slow_path->GetExitLabel());
      return;
    }
  } else if (code_point->GetType() != DataType::Type::kUint16) {
    slow_path = new (codegen->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
    codegen->AddSlowPath(slow_path);
    ScratchRegisterScope srs(assembler);
    XRegister temp = srs.AllocateXRegister();
    assembler->Srli_w(temp, locations->InAt(1).AsRegister<XRegister>(), 16);
    assembler->Bnez(temp, slow_path->GetEntryLabel());
  }

  if (start_at_zero) {
    // Start-index = 0.
    XRegister temp = locations->GetTemp(0).AsRegister<XRegister>();
    assembler->LoadConst32(temp, 0);
  }

  codegen->InvokeRuntime(kQuickIndexOf, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickIndexOf, int32_t, void*, uint32_t, uint32_t>();

  if (slow_path != nullptr) {
    assembler->Bind(slow_path->GetExitLabel());
  }
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringIndexOf(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime calling convention. So it's
  // best to align the inputs accordingly.
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetOut(calling_convention.GetReturnLocation(DataType::Type::kInt32));

  // Need to send start_index=0.
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringIndexOf(HInvoke* invoke) {
  GenerateVisitStringIndexOf(invoke, GetAssembler(), codegen_, /* start_at_zero= */ true);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringIndexOfAfter(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime calling convention. So it's
  // best to align the inputs accordingly.
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->SetOut(calling_convention.GetReturnLocation(DataType::Type::kInt32));
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringIndexOfAfter(HInvoke* invoke) {
  GenerateVisitStringIndexOf(invoke, GetAssembler(), codegen_, /* start_at_zero= */ false);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringBuilderLength(HInvoke* invoke) {
  CreateStringBuilderLengthLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringBuilderLength(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister string_builder = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  uint32_t count_offset;
  bool found = GetStringBuilderCountOffset(invoke, &count_offset);
  DCHECK(found);
  if (!found) {
    return;
  }

  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen_->AddSlowPath(slow_path);
  assembler->Beqz(string_builder, slow_path->GetEntryLabel());
  assembler->Load_W(out, string_builder, count_offset);
  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringBufferToString(HInvoke* invoke) {
  CreateStringBuilderToStringLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringBufferToString(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister string_buffer = locations->InAt(0).AsRegister<XRegister>();

  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen_->AddSlowPath(slow_path);
  assembler->Beqz(string_buffer, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickNewStringFromStringBuffer, invoke, invoke->GetDexPc(), slow_path);
  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringBuilderToString(HInvoke* invoke) {
  CreateStringBuilderToStringLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringBuilderToString(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister string_builder = locations->InAt(0).AsRegister<XRegister>();

  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen_->AddSlowPath(slow_path);
  assembler->Beqz(string_builder, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickNewStringFromStringBuilder, invoke, invoke->GetDexPc(), slow_path);
  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringNewStringFromBytes(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->SetInAt(3, Location::RegisterLocation(calling_convention.GetRegisterAt(3)));
  locations->SetOut(calling_convention.GetReturnLocation(DataType::Type::kReference));
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringNewStringFromBytes(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister byte_array = locations->InAt(0).AsRegister<XRegister>();

  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen_->AddSlowPath(slow_path);
  assembler->Beqz(byte_array, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickAllocStringFromBytes, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickAllocStringFromBytes, void*, void*, int32_t, int32_t, int32_t>();
  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringNewStringFromChars(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kCallOnMainOnly, kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->SetOut(calling_convention.GetReturnLocation(DataType::Type::kReference));
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringNewStringFromChars(HInvoke* invoke) {
  // No need to emit code checking whether `locations->InAt(2)` is a null
  // pointer, as callers of the native method
  //
  //   java.lang.StringFactory.newStringFromChars(int offset, int charCount, char[] data)
  //
  // all include a null check on `data` before calling that method.
  codegen_->InvokeRuntime(kQuickAllocStringFromChars, invoke, invoke->GetDexPc());
  CheckEntrypointTypes<kQuickAllocStringFromChars, void*, int32_t, int32_t, void*>();
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringNewStringFromString(HInvoke* invoke) {
  LocationSummary* locations = new (allocator_) LocationSummary(
      invoke, LocationSummary::kCallOnMainAndSlowPath, kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetOut(calling_convention.GetReturnLocation(DataType::Type::kReference));
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringNewStringFromString(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();
  XRegister string_to_copy = locations->InAt(0).AsRegister<XRegister>();

  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen_->AddSlowPath(slow_path);
  assembler->Beqz(string_to_copy, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickAllocStringFromString, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickAllocStringFromString, void*, void*>();
  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathFmaDouble(HInvoke* invoke) {
  CreateFpFpFpToFpNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathFmaDouble(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  FRegister a = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister b = locations->InAt(1).AsFpuRegister<FRegister>();
  FRegister c = locations->InAt(2).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();

  __ FMadd_d(out, a, b, c);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathFmaFloat(HInvoke* invoke) {
  CreateFpFpFpToFpNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathFmaFloat(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  FRegister a = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister b = locations->InAt(1).AsFpuRegister<FRegister>();
  FRegister c = locations->InAt(2).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();

  __ FMadd_s(out, a, b, c);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathCos(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathCos(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickCos, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathSin(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathSin(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickSin, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathAcos(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathAcos(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickAcos, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathAsin(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathAsin(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickAsin, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathAtan(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathAtan(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickAtan, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathAtan2(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathAtan2(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickAtan2, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathPow(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathPow(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickPow, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathCbrt(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathCbrt(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickCbrt, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathCosh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathCosh(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickCosh, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathExp(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathExp(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickExp, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathExpm1(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathExpm1(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickExpm1, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathHypot(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathHypot(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickHypot, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathLog(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathLog(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickLog, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathLog10(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathLog10(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickLog10, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathNextAfter(HInvoke* invoke) {
  CreateFPFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathNextAfter(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickNextAfter, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathSinh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathSinh(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickSinh, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathTan(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathTan(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickTan, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathTanh(HInvoke* invoke) {
  CreateFPToFPCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathTanh(HInvoke* invoke) {
  codegen_->InvokeRuntime(kQuickTanh, invoke, invoke->GetDexPc());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathSqrt(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathSqrt(HInvoke* invoke) {
  DCHECK_EQ(invoke->InputAt(0)->GetType(), DataType::Type::kFloat64);
  DCHECK_EQ(invoke->GetType(), DataType::Type::kFloat64);

  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();
  __ FSqrt_d(out, in);
}

using FloatToIntOp = void (Loongarch64Assembler::*)(FRegister fd, FRegister fj);

static void GenDoubleRound(HInvoke* invoke, FloatToIntOp op, Loongarch64Assembler* assembler) {
  DCHECK_EQ(invoke->InputAt(0)->GetType(), DataType::Type::kFloat64);
  DCHECK_EQ(invoke->GetType(), DataType::Type::kFloat64);

  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();
  ScratchRegisterScope srs(assembler);
  XRegister bits = srs.AllocateXRegister();
  XRegister limit = srs.AllocateXRegister();
  FRegister ftmp = srs.AllocateFRegister();
  Loongarch64Label done;

  // For |x| >= 2^52 and NaN/Inf values, the argument is already integral for
  // ceil/floor/rint semantics, so keep it unchanged.
  assembler->Movfr2gr_d(bits, in);
  assembler->Movgr2fr_d(out, bits);
  assembler->Bstrpick_d(bits, bits, 62, 0);
  assembler->LoadConst64(limit, INT64_C(0x4330000000000000));
  assembler->Bgeu(bits, limit, &done);

  // Convert to integral value with explicit rounding mode, then convert back.
  (assembler->*op)(ftmp, in);
  assembler->FFint_d_l(out, ftmp);
  assembler->FCopysign_d(out, out, in);
  assembler->Bind(&done);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathCeil(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathCeil(HInvoke* invoke) {
  GenDoubleRound(invoke, &Loongarch64Assembler::FTintrp_l_d, GetAssembler());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathFloor(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathFloor(HInvoke* invoke) {
  GenDoubleRound(invoke, &Loongarch64Assembler::FTintrm_l_d, GetAssembler());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathRint(HInvoke* invoke) {
  CreateFPToFPLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathRint(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();
  GetAssembler()->FRint_d(out, in);
}

static void GenMathRound(HInvoke* invoke, DataType::Type type, Loongarch64Assembler* assembler) {
  DCHECK(type == DataType::Type::kFloat64 || type == DataType::Type::kFloat32);
  DCHECK_EQ(invoke->InputAt(0)->GetType(), type);

  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();
  XRegister tmp2 = srs.AllocateXRegister();
  FRegister ftmp0 = srs.AllocateFRegister();
  Loongarch64Label nan;
  Loongarch64Label saturate;
  Loongarch64Label saturate_max;
  Loongarch64Label saturate_min;
  Loongarch64Label done;

  if (type == DataType::Type::kFloat64) {
    assembler->Movfr2gr_d(tmp2, in);
    assembler->Bstrpick_d(tmp, tmp2, 62, 0);
    assembler->LoadConst64(out, INT64_C(0x7ff0000000000000));
    assembler->Bltu(out, tmp, &nan);
    assembler->LoadConst64(out, INT64_C(0x43e0000000000000));  // 2^63d
    assembler->Bgeu(tmp, out, &saturate);

    // Avoid the precision pitfall of computing floor(x + 0.5) with a rounded add.
    assembler->FTintrm_l_d(ftmp0, in);
    assembler->Movfr2gr_d(out, ftmp0);
    assembler->FFint_d_l(ftmp0, ftmp0);
    assembler->FSub_d(ftmp0, in, ftmp0);
    assembler->Movfr2gr_d(tmp, ftmp0);
    assembler->Bstrpick_d(tmp, tmp, 62, 0);
    assembler->LoadConst64(tmp2, INT64_C(0x3fe0000000000000));  // 0.5d
    assembler->Bltu(tmp, tmp2, &done);
    assembler->LoadConst64(tmp, 1);
    assembler->Add_d(out, out, tmp);
  } else {
    assembler->Movfr2gr_s(tmp2, in);
    assembler->Bstrpick_d(tmp, tmp2, 30, 0);
    assembler->LoadConst32(out, 0x7f800000);
    assembler->Bltu(out, tmp, &nan);
    assembler->LoadConst32(out, 0x4f000000);  // 2^31f
    assembler->Bgeu(tmp, out, &saturate);

    // Avoid the precision pitfall of computing floor(x + 0.5) with a rounded add.
    assembler->FTintrm_w_s(ftmp0, in);
    assembler->Movfr2gr_s(out, ftmp0);
    assembler->FFint_s_w(ftmp0, ftmp0);
    assembler->FSub_s(ftmp0, in, ftmp0);
    assembler->Movfr2gr_s(tmp, ftmp0);
    assembler->Bstrpick_d(tmp, tmp, 30, 0);
    assembler->LoadConst32(tmp2, 0x3f000000);  // 0.5f
    assembler->Bltu(tmp, tmp2, &done);
    assembler->LoadConst32(tmp, 1);
    assembler->Add_d(out, out, tmp);
  }

  assembler->B(&done);
  assembler->Bind(&nan);
  assembler->Move(out, Zero);
  assembler->B(&done);
  assembler->Bind(&saturate);
  assembler->Blt(tmp2, Zero, &saturate_min);
  assembler->B(&saturate_max);
  assembler->Bind(&saturate_max);
  if (type == DataType::Type::kFloat64) {
    assembler->LoadConst64(out, INT64_C(0x7fffffffffffffff));
  } else {
    assembler->LoadConst32(out, INT32_C(0x7fffffff));
  }
  assembler->B(&done);
  assembler->Bind(&saturate_min);
  if (type == DataType::Type::kFloat64) {
    assembler->LoadConst64(out, -INT64_C(0x7fffffffffffffff) - 1);
  } else {
    assembler->LoadConst32(out, -INT32_C(0x7fffffff) - 1);
  }
  assembler->Bind(&done);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathRoundDouble(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathRoundDouble(HInvoke* invoke) {
  DCHECK_EQ(invoke->GetType(), DataType::Type::kInt64);
  GenMathRound(invoke, DataType::Type::kFloat64, GetAssembler());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathRoundFloat(HInvoke* invoke) {
  CreateFPToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathRoundFloat(HInvoke* invoke) {
  DCHECK_EQ(invoke->GetType(), DataType::Type::kInt32);
  GenMathRound(invoke, DataType::Type::kFloat32, GetAssembler());
}

static void GenerateDivRemUnsigned(HInvoke* invoke,
                                   bool is_div,
                                   CodeGeneratorLOONGARCH64* codegen) {
  LocationSummary* locations = invoke->GetLocations();
  Loongarch64Assembler* assembler = codegen->GetAssembler();
  DataType::Type type = invoke->GetType();
  DCHECK(type == DataType::Type::kInt32 || type == DataType::Type::kInt64);

  XRegister dividend = locations->InAt(0).AsRegister<XRegister>();
  XRegister divisor = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  // Check if divisor is zero, bail to managed implementation to handle.
  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen->AddSlowPath(slow_path);
  assembler->Beqz(divisor, slow_path->GetEntryLabel());

  if (is_div) {
    if (type == DataType::Type::kInt32) {
      assembler->Div_wu(out, dividend, divisor);
    } else {
      assembler->Div_du(out, dividend, divisor);
    }
  } else {
    if (type == DataType::Type::kInt32) {
      assembler->Mod_wu(out, dividend, divisor);
    } else {
      assembler->Mod_du(out, dividend, divisor);
    }
  }

  assembler->Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerDivideUnsigned(HInvoke* invoke) {
  CreateIntIntToIntSlowPathCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerDivideUnsigned(HInvoke* invoke) {
  GenerateDivRemUnsigned(invoke, /*is_div=*/true, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongDivideUnsigned(HInvoke* invoke) {
  CreateIntIntToIntSlowPathCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongDivideUnsigned(HInvoke* invoke) {
  GenerateDivRemUnsigned(invoke, /*is_div=*/true, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerRemainderUnsigned(HInvoke* invoke) {
  CreateIntIntToIntSlowPathCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerRemainderUnsigned(HInvoke* invoke) {
  GenerateDivRemUnsigned(invoke, /*is_div=*/false, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongRemainderUnsigned(HInvoke* invoke) {
  CreateIntIntToIntSlowPathCallLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongRemainderUnsigned(HInvoke* invoke) {
  GenerateDivRemUnsigned(invoke, /*is_div=*/false, codegen_);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerReverseBytes(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();
  XRegister mask = srs.AllocateXRegister();

  assembler->LoadConst32(mask, 0x00ff00ff);
  assembler->Srli_d(tmp, in, 8);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, in, mask);
  assembler->Slli_d(out, out, 8);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 16);
  assembler->Slli_d(out, out, 16);
  assembler->Or(out, out, tmp);
  // Keep Java int32 return convention (upper bits are sign-extension).
  assembler->Add_w(out, out, Zero);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongReverseBytes(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();
  XRegister mask = srs.AllocateXRegister();

  assembler->LoadConst64(mask, INT64_C(0x00ff00ff00ff00ff));
  assembler->Srli_d(tmp, in, 8);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, in, mask);
  assembler->Slli_d(out, out, 8);
  assembler->Or(out, out, tmp);

  assembler->LoadConst64(mask, INT64_C(0x0000ffff0000ffff));
  assembler->Srli_d(tmp, out, 16);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 16);
  assembler->Or(out, out, tmp);

  assembler->Srli_d(tmp, out, 32);
  assembler->Slli_d(out, out, 32);
  assembler->Or(out, out, tmp);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitShortReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitShortReverseBytes(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();

  assembler->Bstrpick_d(out, in, 15, 0);
  assembler->Srli_d(tmp, out, 8);
  assembler->Slli_d(out, out, 8);
  assembler->Or(out, out, tmp);
  // Sign-extend low 16 bits to Java short return convention.
  assembler->Slli_w(out, out, 16);
  assembler->Srai_w(out, out, 16);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerReverse(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerReverse(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();
  XRegister mask = srs.AllocateXRegister();

  assembler->Bstrpick_d(out, in, 31, 0);

  assembler->LoadConst32(mask, 0x55555555);
  assembler->Srli_d(tmp, out, 1);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 1);
  assembler->Or(out, out, tmp);

  assembler->LoadConst32(mask, 0x33333333);
  assembler->Srli_d(tmp, out, 2);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 2);
  assembler->Or(out, out, tmp);

  assembler->LoadConst32(mask, 0x0f0f0f0f);
  assembler->Srli_d(tmp, out, 4);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 4);
  assembler->Or(out, out, tmp);

  assembler->LoadConst32(mask, 0x00ff00ff);
  assembler->Srli_d(tmp, out, 8);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 8);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 16);
  assembler->Slli_d(out, out, 16);
  assembler->Or(out, out, tmp);
  assembler->Add_w(out, out, Zero);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongReverse(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongReverse(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();
  XRegister mask = srs.AllocateXRegister();

  assembler->Move(out, in);

  assembler->LoadConst64(mask, INT64_C(0x5555555555555555));
  assembler->Srli_d(tmp, out, 1);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 1);
  assembler->Or(out, out, tmp);

  assembler->LoadConst64(mask, INT64_C(0x3333333333333333));
  assembler->Srli_d(tmp, out, 2);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 2);
  assembler->Or(out, out, tmp);

  assembler->LoadConst64(mask, INT64_C(0x0f0f0f0f0f0f0f0f));
  assembler->Srli_d(tmp, out, 4);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 4);
  assembler->Or(out, out, tmp);

  assembler->LoadConst64(mask, INT64_C(0x00ff00ff00ff00ff));
  assembler->Srli_d(tmp, out, 8);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 8);
  assembler->Or(out, out, tmp);

  assembler->LoadConst64(mask, INT64_C(0x0000ffff0000ffff));
  assembler->Srli_d(tmp, out, 16);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Slli_d(out, out, 16);
  assembler->Or(out, out, tmp);

  assembler->Srli_d(tmp, out, 32);
  assembler->Slli_d(out, out, 32);
  assembler->Or(out, out, tmp);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerBitCount(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerBitCount(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();
  XRegister mask = srs.AllocateXRegister();

  assembler->Bstrpick_d(out, in, 31, 0);

  assembler->LoadConst32(mask, 0x55555555);
  assembler->Srli_d(tmp, out, 1);
  assembler->And(tmp, tmp, mask);
  assembler->Sub_d(out, out, tmp);

  assembler->LoadConst32(mask, 0x33333333);
  assembler->Srli_d(tmp, out, 2);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Add_d(out, out, tmp);

  assembler->Srli_d(tmp, out, 4);
  assembler->Add_d(out, out, tmp);
  assembler->LoadConst32(mask, 0x0f0f0f0f);
  assembler->And(out, out, mask);

  assembler->Srli_d(tmp, out, 8);
  assembler->Add_d(out, out, tmp);
  assembler->Srli_d(tmp, out, 16);
  assembler->Add_d(out, out, tmp);
  assembler->Andi(out, out, 0x3f);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongBitCount(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongBitCount(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();
  XRegister mask = srs.AllocateXRegister();

  assembler->Move(out, in);

  assembler->LoadConst64(mask, INT64_C(0x5555555555555555));
  assembler->Srli_d(tmp, out, 1);
  assembler->And(tmp, tmp, mask);
  assembler->Sub_d(out, out, tmp);

  assembler->LoadConst64(mask, INT64_C(0x3333333333333333));
  assembler->Srli_d(tmp, out, 2);
  assembler->And(tmp, tmp, mask);
  assembler->And(out, out, mask);
  assembler->Add_d(out, out, tmp);

  assembler->Srli_d(tmp, out, 4);
  assembler->Add_d(out, out, tmp);
  assembler->LoadConst64(mask, INT64_C(0x0f0f0f0f0f0f0f0f));
  assembler->And(out, out, mask);

  assembler->Srli_d(tmp, out, 8);
  assembler->Add_d(out, out, tmp);
  assembler->Srli_d(tmp, out, 16);
  assembler->Add_d(out, out, tmp);
  assembler->Srli_d(tmp, out, 32);
  assembler->Add_d(out, out, tmp);
  assembler->Andi(out, out, 0x7f);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerHighestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerHighestOneBit(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();

  assembler->Bstrpick_d(out, in, 31, 0);
  assembler->Srli_d(tmp, out, 1);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 2);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 4);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 8);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 16);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 1);
  assembler->Sub_d(out, out, tmp);
  assembler->Add_w(out, out, Zero);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongHighestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongHighestOneBit(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();

  assembler->Move(out, in);
  assembler->Srli_d(tmp, out, 1);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 2);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 4);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 8);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 16);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 32);
  assembler->Or(out, out, tmp);
  assembler->Srli_d(tmp, out, 1);
  assembler->Sub_d(out, out, tmp);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerLowestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerLowestOneBit(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();

  assembler->Sub_w(tmp, Zero, in);
  assembler->And(out, in, tmp);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongLowestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongLowestOneBit(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();

  assembler->Sub_d(tmp, Zero, in);
  assembler->And(out, in, tmp);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister value = srs.AllocateXRegister();
  XRegister bit = srs.AllocateXRegister();
  Loongarch64Label loop, done, non_zero;

  assembler->Bstrpick_d(value, in, 31, 0);
  assembler->Bnez(value, &non_zero);
  assembler->LoadConst32(out, 32);
  assembler->B(&done);

  assembler->Bind(&non_zero);
  assembler->Move(out, Zero);
  assembler->Bind(&loop);
  assembler->Srli_d(bit, value, 31);
  assembler->Bnez(bit, &done);
  assembler->Addi_D(out, out, 1);
  assembler->Slli_d(value, value, 1);
  assembler->B(&loop);

  assembler->Bind(&done);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister value = srs.AllocateXRegister();
  XRegister bit = srs.AllocateXRegister();
  Loongarch64Label loop, done, non_zero;

  assembler->Move(value, in);
  assembler->Bnez(value, &non_zero);
  assembler->LoadConst32(out, 64);
  assembler->B(&done);

  assembler->Bind(&non_zero);
  assembler->Move(out, Zero);
  assembler->Bind(&loop);
  assembler->Srli_d(bit, value, 63);
  assembler->Bnez(bit, &done);
  assembler->Addi_D(out, out, 1);
  assembler->Slli_d(value, value, 1);
  assembler->B(&loop);

  assembler->Bind(&done);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister value = srs.AllocateXRegister();
  XRegister bit = srs.AllocateXRegister();
  Loongarch64Label loop, done, non_zero;

  assembler->Bstrpick_d(value, in, 31, 0);
  assembler->Bnez(value, &non_zero);
  assembler->LoadConst32(out, 32);
  assembler->B(&done);

  assembler->Bind(&non_zero);
  assembler->Move(out, Zero);
  assembler->Bind(&loop);
  assembler->Andi(bit, value, 1);
  assembler->Bnez(bit, &done);
  assembler->Addi_D(out, out, 1);
  assembler->Srli_d(value, value, 1);
  assembler->B(&loop);

  assembler->Bind(&done);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Assembler* assembler = GetAssembler();
  ScratchRegisterScope srs(assembler);
  XRegister value = srs.AllocateXRegister();
  XRegister bit = srs.AllocateXRegister();
  Loongarch64Label loop, done, non_zero;

  assembler->Move(value, in);
  assembler->Bnez(value, &non_zero);
  assembler->LoadConst32(out, 64);
  assembler->B(&done);

  assembler->Bind(&non_zero);
  assembler->Move(out, Zero);
  assembler->Bind(&loop);
  assembler->Andi(bit, value, 1);
  assembler->Bnez(bit, &done);
  assembler->Addi_D(out, out, 1);
  assembler->Srli_d(value, value, 1);
  assembler->B(&loop);

  assembler->Bind(&done);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitMathMultiplyHigh(HInvoke* invoke) {
  CreateIntIntToIntLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitMathMultiplyHigh(HInvoke* invoke) {
  DCHECK_EQ(invoke->InputAt(0)->GetType(), DataType::Type::kInt64);
  DCHECK_EQ(invoke->InputAt(1)->GetType(), DataType::Type::kInt64);
  DCHECK_EQ(invoke->GetType(), DataType::Type::kInt64);

  LocationSummary* locations = invoke->GetLocations();
  XRegister x = locations->InAt(0).AsRegister<XRegister>();
  XRegister y = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  __ Mulh_d(out, x, y);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitStringGetCharsNoCheck(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitStringGetCharsNoCheck(HInvoke* invoke) {
  Loongarch64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  constexpr size_t char_size = DataType::Size(DataType::Type::kUint16);
  static_assert(char_size == 2u);

  const uint32_t array_data_offset = mirror::Array::DataOffset(char_size).Uint32Value();
  const uint32_t string_value_offset = mirror::String::ValueOffset().Uint32Value();

  XRegister source_string_object = locations->InAt(0).AsRegister<XRegister>();
  XRegister source_begin_index = locations->InAt(1).AsRegister<XRegister>();
  XRegister source_end_index = locations->InAt(2).AsRegister<XRegister>();
  XRegister destination_array_object = locations->InAt(3).AsRegister<XRegister>();
  XRegister destination_begin_offset = locations->InAt(4).AsRegister<XRegister>();

  XRegister source_ptr = locations->GetTemp(0).AsRegister<XRegister>();
  XRegister destination_ptr = locations->GetTemp(1).AsRegister<XRegister>();
  XRegister number_of_chars = locations->GetTemp(2).AsRegister<XRegister>();

  ScratchRegisterScope srs(assembler);
  XRegister tmp = srs.AllocateXRegister();

  Loongarch64Label done;
  Loongarch64Label uncompressed_loop;
  Loongarch64Label compressed_string_preloop;
  Loongarch64Label compressed_string_loop;

  assembler->Sub_w(number_of_chars, source_end_index, source_begin_index);
  assembler->Beqz(number_of_chars, &done);

  assembler->Addi_D(destination_ptr, destination_array_object, array_data_offset);
  assembler->Slli_d(tmp, destination_begin_offset, 1);
  assembler->Add_d(destination_ptr, destination_ptr, tmp);

  assembler->Addi_D(source_ptr, source_string_object, string_value_offset);

  if (mirror::kUseStringCompression) {
    const uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
    assembler->Load_WU(tmp, source_string_object, count_offset);
    assembler->Andi(tmp, tmp, 0x1);
    assembler->Beqz(tmp, &compressed_string_preloop);
  }

  assembler->Slli_d(tmp, source_begin_index, 1);
  assembler->Add_d(source_ptr, source_ptr, tmp);

  assembler->Bind(&uncompressed_loop);
  assembler->Load_HU(tmp, source_ptr, 0);
  assembler->Store_H(tmp, destination_ptr, 0);
  assembler->Addi_D(source_ptr, source_ptr, char_size);
  assembler->Addi_D(destination_ptr, destination_ptr, char_size);
  assembler->Addi_D(number_of_chars, number_of_chars, -1);
  assembler->Blt(Zero, number_of_chars, &uncompressed_loop);

  if (mirror::kUseStringCompression) {
    assembler->B(&done);

    assembler->Bind(&compressed_string_preloop);
    assembler->Add_d(source_ptr, source_ptr, source_begin_index);

    assembler->Bind(&compressed_string_loop);
    assembler->Load_BU(tmp, source_ptr, 0);
    assembler->Store_H(tmp, destination_ptr, 0);
    assembler->Addi_D(source_ptr, source_ptr, 1);
    assembler->Addi_D(destination_ptr, destination_ptr, char_size);
    assembler->Addi_D(number_of_chars, number_of_chars, -1);
    assembler->Blt(Zero, number_of_chars, &compressed_string_loop);
  }

  assembler->Bind(&done);
}

static constexpr uint32_t kFP16SignMask = 0x8000u;
static constexpr uint32_t kFP16Infinity = 0x7c00u;
static constexpr uint32_t kFP16NaN = 0x7e00u;

static void GenerateFP16SignExtend(Loongarch64Assembler* assembler,
                                   XRegister src,
                                   XRegister dst) {
  assembler->Bstrpick_w(dst, src, 15, 0);
  assembler->Slli_w(dst, dst, 16);
  assembler->Srai_w(dst, dst, 16);
}

// Java semantics: NaN iff ((h & 0x7fff) > 0x7c00).
static void GenerateFP16IsNaN(Loongarch64Assembler* assembler,
                              XRegister src,
                              XRegister out,
                              XRegister tmp) {
  assembler->Bstrpick_w(out, src, 14, 0);
  assembler->LoadConst32(tmp, kFP16Infinity);
  assembler->Sltu(out, tmp, out);
}

// Total-order key used by libcore.util.FP16.{less,greater,...}:
//   key = sign ? (0x8000 - bits16) : bits16
static void GenerateFP16OrderedValue(Loongarch64Assembler* assembler,
                                     XRegister src,
                                     XRegister out,
                                     XRegister tmp,
                                     XRegister sign) {
  assembler->Bstrpick_w(out, src, 15, 0);
  assembler->Srli_w(sign, out, 15);
  assembler->LoadConst32(tmp, kFP16SignMask);
  assembler->Sub_w(tmp, tmp, out);
  assembler->Masknez(tmp, tmp, sign);
  assembler->Maskeqz(out, out, sign);
  assembler->Or(out, out, tmp);
}

enum class FP16Relation {
  kLess,
  kLessEquals,
  kGreater,
  kGreaterEquals,
};

static void GenerateFP16RelationalOp(HInvoke* invoke,
                                     Loongarch64Assembler* assembler,
                                     FP16Relation relation) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister x = locations->InAt(0).AsRegister<XRegister>();
  XRegister y = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  ScratchRegisterScope srs(assembler);
  XRegister ordered_x = srs.AllocateXRegister();
  XRegister ordered_y = srs.AllocateXRegister();
  XRegister tmp = srs.AllocateXRegister();
  XRegister nan_x = srs.AllocateXRegister();

  GenerateFP16OrderedValue(assembler, x, ordered_x, tmp, nan_x);
  GenerateFP16OrderedValue(assembler, y, ordered_y, tmp, nan_x);

  switch (relation) {
    case FP16Relation::kLess:
      assembler->Sltu(out, ordered_x, ordered_y);
      break;
    case FP16Relation::kLessEquals:
      assembler->Sltu(out, ordered_y, ordered_x);
      assembler->Sltui(out, out, 1);
      break;
    case FP16Relation::kGreater:
      assembler->Sltu(out, ordered_y, ordered_x);
      break;
    case FP16Relation::kGreaterEquals:
      assembler->Sltu(out, ordered_x, ordered_y);
      assembler->Sltui(out, out, 1);
      break;
  }

  // If either operand is NaN, result is false.
  GenerateFP16IsNaN(assembler, x, nan_x, tmp);
  GenerateFP16IsNaN(assembler, y, tmp, ordered_y);
  assembler->Or(nan_x, nan_x, tmp);
  assembler->Maskeqz(out, out, nan_x);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16Less(HInvoke* invoke) {
  // Fall back to the normal invoke path until the FP16 intrinsic set is stable.
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16Less(HInvoke* invoke) {
  GenerateFP16RelationalOp(invoke, GetAssembler(), FP16Relation::kLess);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16LessEquals(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16LessEquals(HInvoke* invoke) {
  GenerateFP16RelationalOp(invoke, GetAssembler(), FP16Relation::kLessEquals);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16Greater(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16Greater(HInvoke* invoke) {
  GenerateFP16RelationalOp(invoke, GetAssembler(), FP16Relation::kGreater);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16GreaterEquals(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16GreaterEquals(HInvoke* invoke) {
  GenerateFP16RelationalOp(invoke, GetAssembler(), FP16Relation::kGreaterEquals);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16Compare(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16Compare(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister x = locations->InAt(0).AsRegister<XRegister>();
  XRegister y = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  ScratchRegisterScope srs(GetAssembler());
  XRegister ordered_x = srs.AllocateXRegister();
  XRegister ordered_y = srs.AllocateXRegister();
  XRegister nan_x = srs.AllocateXRegister();
  XRegister nan_y = srs.AllocateXRegister();
  XRegister tmp = srs.AllocateXRegister();

  Loongarch64Label not_equal;
  Loongarch64Label return_minus_one;
  Loongarch64Label done;

  GenerateFP16OrderedValue(GetAssembler(), x, ordered_x, tmp, out);
  GenerateFP16OrderedValue(GetAssembler(), y, ordered_y, tmp, out);
  GenerateFP16IsNaN(GetAssembler(), x, nan_x, tmp);
  GenerateFP16IsNaN(GetAssembler(), y, nan_y, tmp);
  GetAssembler()->Or(tmp, nan_x, nan_y);

  // if (less(x, y)) return -1;
  GetAssembler()->Sltu(out, ordered_x, ordered_y);
  GetAssembler()->Maskeqz(out, out, tmp);
  GetAssembler()->Bnez(out, &return_minus_one);

  // if (greater(x, y)) return 1;
  GetAssembler()->Sltu(out, ordered_y, ordered_x);
  GetAssembler()->Maskeqz(out, out, tmp);
  GetAssembler()->Bnez(out, &not_equal);

  // Collapse NaNs to canonical short NaN and compare signed 16-bit values.
  GenerateFP16SignExtend(GetAssembler(), x, ordered_x);
  GenerateFP16SignExtend(GetAssembler(), y, ordered_y);
  GetAssembler()->LoadConst32(out, kFP16NaN);
  GetAssembler()->Masknez(out, out, nan_x);
  GetAssembler()->Maskeqz(ordered_x, ordered_x, nan_x);
  GetAssembler()->Or(ordered_x, ordered_x, out);
  GetAssembler()->LoadConst32(out, kFP16NaN);
  GetAssembler()->Masknez(out, out, nan_y);
  GetAssembler()->Maskeqz(ordered_y, ordered_y, nan_y);
  GetAssembler()->Or(ordered_y, ordered_y, out);

  GetAssembler()->Beq(ordered_x, ordered_y, &done);
  GetAssembler()->Slt(out, ordered_x, ordered_y);
  GetAssembler()->Bnez(out, &return_minus_one);

  GetAssembler()->Bind(&not_equal);
  GetAssembler()->LoadConst32(out, 1);
  GetAssembler()->B(&done);

  GetAssembler()->Bind(&return_minus_one);
  GetAssembler()->LoadConst32(out, -1);
  GetAssembler()->Bind(&done);
}

static void GenerateFP16MinMax(HInvoke* invoke, Loongarch64Assembler* assembler, bool is_min) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister x = locations->InAt(0).AsRegister<XRegister>();
  XRegister y = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  ScratchRegisterScope srs(assembler);
  XRegister x_bits = srs.AllocateXRegister();
  XRegister y_bits = srs.AllocateXRegister();
  XRegister ordered_x = srs.AllocateXRegister();
  XRegister ordered_y = srs.AllocateXRegister();
  XRegister tmp = srs.AllocateXRegister();
  XRegister nan = srs.AllocateXRegister();

  Loongarch64Label return_nan;
  Loongarch64Label non_zero_values;
  Loongarch64Label choose_x;
  Loongarch64Label choose_y;
  Loongarch64Label done;

  GenerateFP16SignExtend(assembler, x, x_bits);
  GenerateFP16SignExtend(assembler, y, y_bits);

  // If either input is NaN, return canonical NaN.
  GenerateFP16IsNaN(assembler, x, nan, tmp);
  GenerateFP16IsNaN(assembler, y, tmp, ordered_x);
  assembler->Or(nan, nan, tmp);
  assembler->Bnez(nan, &return_nan);

  // Special-case both signed zeros to preserve Java ordering semantics.
  assembler->Bstrpick_w(ordered_x, x_bits, 14, 0);
  assembler->Bstrpick_w(ordered_y, y_bits, 14, 0);
  assembler->Or(tmp, ordered_x, ordered_y);
  assembler->Bnez(tmp, &non_zero_values);

  if (is_min) {
    assembler->Bltz(x_bits, &choose_x);
    assembler->B(&choose_y);
  } else {
    assembler->Bltz(x_bits, &choose_y);
    assembler->B(&choose_x);
  }

  assembler->Bind(&non_zero_values);
  GenerateFP16OrderedValue(assembler, x, ordered_x, tmp, nan);
  GenerateFP16OrderedValue(assembler, y, ordered_y, tmp, nan);
  if (is_min) {
    assembler->Sltu(tmp, ordered_x, ordered_y);
  } else {
    assembler->Sltu(tmp, ordered_y, ordered_x);
  }
  assembler->Bnez(tmp, &choose_x);
  assembler->B(&choose_y);

  assembler->Bind(&choose_x);
  assembler->Move(out, x_bits);
  assembler->B(&done);

  assembler->Bind(&choose_y);
  assembler->Move(out, y_bits);
  assembler->B(&done);

  assembler->Bind(&return_nan);
  assembler->LoadConst32(out, kFP16NaN);
  assembler->Bind(&done);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16Min(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16Min(HInvoke* invoke) {
  GenerateFP16MinMax(invoke, GetAssembler(), /* is_min= */ true);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16Max(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16Max(HInvoke* invoke) {
  GenerateFP16MinMax(invoke, GetAssembler(), /* is_min= */ false);
}

enum class FP16RoundMode {
  kCeil,
  kFloor,
  kRint,
};

static void GenerateFP16Round(HInvoke* invoke,
                              Loongarch64Assembler* assembler,
                              FP16RoundMode mode) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  ScratchRegisterScope srs(assembler);
  XRegister bits = srs.AllocateXRegister();
  XRegister abs = srs.AllocateXRegister();
  XRegister result = srs.AllocateXRegister();
  XRegister exp = srs.AllocateXRegister();
  XRegister mask = srs.AllocateXRegister();
  XRegister tmp = srs.AllocateXRegister();
  XRegister tmp2 = srs.AllocateXRegister();
  XRegister adjust = srs.AllocateXRegister();

  Loongarch64Label small;
  Loongarch64Label mid;
  Loongarch64Label set_small_integer;
  Loongarch64Label finalize;
  Loongarch64Label canonicalize_nan;
  Loongarch64Label done;

  assembler->Bstrpick_w(bits, in, 15, 0);
  assembler->Bstrpick_w(abs, bits, 14, 0);
  assembler->Move(result, bits);

  assembler->LoadConst32(tmp, 0x3c00);
  assembler->Bltu(abs, tmp, &small);
  assembler->LoadConst32(tmp, 0x6400);
  assembler->Bltu(abs, tmp, &mid);
  assembler->B(&finalize);

  assembler->Bind(&small);
  assembler->LoadConst32(mask, kFP16SignMask);
  assembler->And(result, bits, mask);
  if (mode == FP16RoundMode::kRint) {
    assembler->LoadConst32(tmp, 0x3800);
    assembler->Bltu(tmp, abs, &set_small_integer);
    assembler->B(&finalize);
  } else if (mode == FP16RoundMode::kCeil) {
    assembler->Beqz(abs, &finalize);
    assembler->Srli_w(tmp, bits, 15);
    assembler->Bnez(tmp, &finalize);
    assembler->B(&set_small_integer);
  } else {
    DCHECK(mode == FP16RoundMode::kFloor);
    assembler->LoadConst32(tmp, kFP16SignMask);
    assembler->Bltu(tmp, bits, &set_small_integer);
    assembler->B(&finalize);
  }

  assembler->Bind(&set_small_integer);
  assembler->LoadConst32(tmp, 0x3c00);
  assembler->Or(result, result, tmp);
  assembler->B(&finalize);

  assembler->Bind(&mid);
  assembler->Srli_w(exp, abs, 10);
  assembler->LoadConst32(tmp, 25);
  assembler->Sub_w(exp, tmp, exp);
  assembler->LoadConst32(mask, 1);
  assembler->Sll_w(mask, mask, exp);
  assembler->Addi_D(mask, mask, -1);

  if (mode == FP16RoundMode::kRint) {
    assembler->Srl_w(tmp, abs, exp);
    assembler->Nor(tmp, tmp, Zero);
    assembler->Andi(tmp, tmp, 1);
    assembler->Addi_D(tmp2, exp, -1);
    assembler->LoadConst32(adjust, 1);
    assembler->Sll_w(adjust, adjust, tmp2);
    assembler->Sub_w(adjust, adjust, tmp);
    assembler->Add_w(result, result, adjust);
  } else if (mode == FP16RoundMode::kCeil) {
    assembler->Srli_w(tmp, bits, 15);
    assembler->Addi_D(tmp, tmp, -1);
    assembler->And(tmp2, mask, tmp);
    assembler->Add_w(result, result, tmp2);
  } else {
    DCHECK(mode == FP16RoundMode::kFloor);
    assembler->Srli_w(tmp, bits, 15);
    assembler->Sub_w(tmp, Zero, tmp);
    assembler->And(tmp2, mask, tmp);
    assembler->Add_w(result, result, tmp2);
  }

  assembler->Nor(tmp, mask, Zero);
  assembler->And(result, result, tmp);

  assembler->Bind(&finalize);
  assembler->Bstrpick_w(tmp, result, 14, 0);
  assembler->LoadConst32(tmp2, kFP16Infinity);
  assembler->Bltu(tmp2, tmp, &canonicalize_nan);
  assembler->B(&done);

  assembler->Bind(&canonicalize_nan);
  assembler->LoadConst32(tmp2, kFP16NaN);
  assembler->Or(result, result, tmp2);

  assembler->Bind(&done);
  assembler->Slli_w(out, result, 16);
  assembler->Srai_w(out, out, 16);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16Ceil(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16Ceil(HInvoke* invoke) {
  GenerateFP16Round(invoke, GetAssembler(), FP16RoundMode::kCeil);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16Floor(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16Floor(HInvoke* invoke) {
  GenerateFP16Round(invoke, GetAssembler(), FP16RoundMode::kFloor);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16Rint(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16Rint(HInvoke* invoke) {
  GenerateFP16Round(invoke, GetAssembler(), FP16RoundMode::kRint);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16ToFloat(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16ToFloat(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister in = locations->InAt(0).AsRegister<XRegister>();
  FRegister out = locations->Out().AsFpuRegister<FRegister>();

  ScratchRegisterScope srs(GetAssembler());
  XRegister bits = srs.AllocateXRegister();
  XRegister sign = srs.AllocateXRegister();
  XRegister exp = srs.AllocateXRegister();
  XRegister man = srs.AllocateXRegister();
  XRegister out_exp = srs.AllocateXRegister();
  XRegister out_man = srs.AllocateXRegister();
  XRegister tmp = srs.AllocateXRegister();

  Loongarch64Label exp_zero;
  Loongarch64Label exp_inf_nan;
  Loongarch64Label denorm_loop;
  Loongarch64Label denorm_done;
  Loongarch64Label build;

  GetAssembler()->Bstrpick_w(bits, in, 15, 0);
  GetAssembler()->Bstrpick_w(sign, bits, 15, 15);  // 0 or 1.
  GetAssembler()->Srli_w(exp, bits, 10);
  GetAssembler()->Andi(exp, exp, 0x1f);
  GetAssembler()->Andi(man, bits, 0x3ff);
  GetAssembler()->Move(out_exp, Zero);
  GetAssembler()->Move(out_man, Zero);

  GetAssembler()->Beqz(exp, &exp_zero);
  GetAssembler()->LoadConst32(tmp, 0x1f);
  GetAssembler()->Beq(exp, tmp, &exp_inf_nan);
  GetAssembler()->Slli_w(out_man, man, 13);
  GetAssembler()->Addi_D(out_exp, exp, 112);  // 127 - 15.
  GetAssembler()->B(&build);

  GetAssembler()->Bind(&exp_zero);
  GetAssembler()->Beqz(man, &build);  // Zero.
  GetAssembler()->LoadConst32(out_exp, 113);  // 127 - 14.
  GetAssembler()->Bind(&denorm_loop);
  GetAssembler()->Andi(tmp, man, 0x400);
  GetAssembler()->Bnez(tmp, &denorm_done);
  GetAssembler()->Slli_w(man, man, 1);
  GetAssembler()->Addi_D(out_exp, out_exp, -1);
  GetAssembler()->B(&denorm_loop);

  GetAssembler()->Bind(&denorm_done);
  GetAssembler()->Andi(man, man, 0x3ff);
  GetAssembler()->Slli_w(out_man, man, 13);
  GetAssembler()->B(&build);

  GetAssembler()->Bind(&exp_inf_nan);
  GetAssembler()->LoadConst32(out_exp, 0xff);
  GetAssembler()->Slli_w(out_man, man, 13);
  GetAssembler()->Beqz(out_man, &build);
  GetAssembler()->LoadConst32(tmp, 0x400000);  // Quiet NaN bit.
  GetAssembler()->Or(out_man, out_man, tmp);

  GetAssembler()->Bind(&build);
  GetAssembler()->Slli_w(sign, sign, 31);
  GetAssembler()->Slli_w(out_exp, out_exp, 23);
  GetAssembler()->Or(tmp, sign, out_exp);
  GetAssembler()->Or(tmp, tmp, out_man);
  GetAssembler()->Movgr2fr_w(out, tmp);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitFP16ToHalf(HInvoke* invoke) {
  UNUSED(invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitFP16ToHalf(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  FRegister in = locations->InAt(0).AsFpuRegister<FRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  ScratchRegisterScope srs(GetAssembler());
  XRegister bits = srs.AllocateXRegister();
  XRegister sign = srs.AllocateXRegister();
  XRegister exp = srs.AllocateXRegister();
  XRegister man = srs.AllocateXRegister();
  XRegister out_exp = srs.AllocateXRegister();
  XRegister out_man = srs.AllocateXRegister();
  XRegister tmp = srs.AllocateXRegister();
  XRegister tmp2 = srs.AllocateXRegister();
  XRegister tmp3 = srs.AllocateXRegister();
  XRegister tmp4 = srs.AllocateXRegister();

  Loongarch64Label exp_inf_nan;
  Loongarch64Label overflow;
  Loongarch64Label underflow;
  Loongarch64Label normal;
  Loongarch64Label round_up;
  Loongarch64Label calc_done;

  GetAssembler()->Movfr2gr_s(bits, in);
  GetAssembler()->Srli_w(sign, bits, 31);
  GetAssembler()->Srli_w(exp, bits, 23);
  GetAssembler()->Andi(exp, exp, 0xff);
  GetAssembler()->Bstrpick_w(man, bits, 22, 0);
  GetAssembler()->Move(out_exp, Zero);
  GetAssembler()->Move(out_man, Zero);

  GetAssembler()->LoadConst32(tmp, 0xff);
  GetAssembler()->Beq(exp, tmp, &exp_inf_nan);

  // e = e - FP32_EXPONENT_BIAS + EXPONENT_BIAS.
  GetAssembler()->Addi_D(exp, exp, -112);  // 127 - 15.
  GetAssembler()->LoadConst32(tmp, 0x1f);
  GetAssembler()->Bge(exp, tmp, &overflow);
  GetAssembler()->Blt(Zero, exp, &normal);
  GetAssembler()->B(&underflow);

  GetAssembler()->Bind(&normal);
  GetAssembler()->Move(out_exp, exp);
  GetAssembler()->Srli_w(out_man, man, 13);
  GetAssembler()->Andi(tmp, man, 0x1fff);
  GetAssembler()->Andi(tmp2, out_man, 0x1);
  GetAssembler()->Add_w(tmp, tmp, tmp2);
  GetAssembler()->LoadConst32(tmp2, 0x1000);
  GetAssembler()->Bltu(tmp2, tmp, &round_up);
  GetAssembler()->B(&calc_done);

  GetAssembler()->Bind(&underflow);
  GetAssembler()->LoadConst32(tmp, -10);
  GetAssembler()->Blt(exp, tmp, &calc_done);
  GetAssembler()->LoadConst32(tmp, 0x800000);
  GetAssembler()->Or(man, man, tmp);
  GetAssembler()->LoadConst32(tmp, 14);
  GetAssembler()->Sub_w(tmp, tmp, exp);  // shift = 14 - e.
  GetAssembler()->Srl_w(out_man, man, tmp);
  GetAssembler()->LoadConst32(tmp2, 1);
  GetAssembler()->Sll_w(tmp2, tmp2, tmp);
  GetAssembler()->Addi_D(tmp2, tmp2, -1);  // (1 << shift) - 1
  GetAssembler()->And(tmp3, man, tmp2);    // lowm
  GetAssembler()->Addi_D(tmp4, tmp, -1);
  GetAssembler()->LoadConst32(tmp2, 1);
  GetAssembler()->Sll_w(tmp2, tmp2, tmp4);  // hway
  GetAssembler()->Andi(tmp4, out_man, 0x1);
  GetAssembler()->Add_w(tmp3, tmp3, tmp4);
  GetAssembler()->Bltu(tmp2, tmp3, &round_up);
  GetAssembler()->B(&calc_done);

  GetAssembler()->Bind(&exp_inf_nan);
  GetAssembler()->LoadConst32(out_exp, 0x1f);
  GetAssembler()->Beqz(man, &calc_done);
  GetAssembler()->LoadConst32(out_man, 0x200);
  GetAssembler()->B(&calc_done);

  GetAssembler()->Bind(&overflow);
  GetAssembler()->LoadConst32(out_exp, 0x1f);
  GetAssembler()->B(&calc_done);

  GetAssembler()->Bind(&round_up);
  GetAssembler()->Addi_D(out_man, out_man, 1);
  GetAssembler()->B(&calc_done);

  GetAssembler()->Bind(&calc_done);
  GetAssembler()->Slli_w(sign, sign, 15);
  GetAssembler()->Slli_w(out_exp, out_exp, 10);
  GetAssembler()->Add_w(tmp, out_exp, out_man);
  GetAssembler()->Or(tmp, tmp, sign);
  GetAssembler()->Slli_w(out, tmp, 16);
  GetAssembler()->Srai_w(out, out, 16);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitThreadCurrentThread(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitThreadCurrentThread(HInvoke* invoke) {
  XRegister out = invoke->GetLocations()->Out().AsRegister<XRegister>();
  __ Load_WU(out, TR, Thread::PeerOffset<kLoongarch64PointerSize>().Int32Value());
  if (kPoisonHeapReferences) {
    codegen_->UnpoisonHeapReference(out);
  }
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitThreadInterrupted(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitThreadInterrupted(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister out = locations->Out().AsRegister<XRegister>();
  Loongarch64Label done;

  codegen_->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  __ Load_W(out, TR, Thread::InterruptedOffset<kLoongarch64PointerSize>().Int32Value());
  __ Beqz(out, &done);
  __ Store_W(Zero, TR, Thread::InterruptedOffset<kLoongarch64PointerSize>().Int32Value());
  codegen_->GenerateMemoryBarrier(MemBarrierKind::kAnyAny);
  __ Bind(&done);
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitReachabilityFence(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::Any());
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitReachabilityFence([[maybe_unused]] HInvoke* invoke) {}

void IntrinsicLocationsBuilderLOONGARCH64::VisitCRC32Update(HInvoke* invoke) {
  CreateIntIntToIntNoOverlapLocations(allocator_, invoke);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitCRC32Update(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister crc = locations->InAt(0).AsRegister<XRegister>();
  XRegister value = locations->InAt(1).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();

  ScratchRegisterScope srs(GetAssembler());
  XRegister data = srs.AllocateXRegister();
  __ Andi(data, value, 0xff);
  __ Addi_W(out, crc, 0);
  __ Nor(out, out, Zero);
  __ Crc_w_b_w(out, data, out);
  __ Nor(out, out, Zero);
  __ Addi_W(out, out, 0);
}

static constexpr int32_t kCRC32UpdateBytesThreshold = 64 * 1024;

void IntrinsicLocationsBuilderLOONGARCH64::VisitCRC32UpdateBytes(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kCallOnSlowPath, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RegisterOrConstant(invoke->InputAt(2)));
  locations->SetInAt(3, Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitCRC32UpdateBytes(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  SlowPathCodeLOONGARCH64* slow_path =
      new (codegen_->GetScopedAllocator()) IntrinsicSlowPathLOONGARCH64(invoke);
  codegen_->AddSlowPath(slow_path);

  XRegister ptr = locations->GetTemp(0).AsRegister<XRegister>();
  XRegister len = locations->GetTemp(1).AsRegister<XRegister>();
  XRegister threshold = len;
  XRegister length = locations->InAt(3).AsRegister<XRegister>();
  __ LoadConst32(threshold, kCRC32UpdateBytesThreshold);
  __ Blt(threshold, length, slow_path->GetEntryLabel());

  const int32_t array_data_offset =
      mirror::Array::DataOffset(Primitive::kPrimByte).Int32Value();
  XRegister array = locations->InAt(1).AsRegister<XRegister>();
  Location offset = locations->InAt(2);
  if (offset.IsConstant()) {
    int32_t offset_value = offset.GetConstant()->AsIntConstant()->GetValue();
    __ AddConst64(ptr, array, array_data_offset + offset_value);
  } else {
    __ AddConst64(ptr, array, array_data_offset);
    __ Add_d(ptr, ptr, offset.AsRegister<XRegister>());
  }

  XRegister crc = locations->InAt(0).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  __ Addi_W(len, length, 0);
  GenerateCodeForCalculationCRC32ValueOfBytes(GetAssembler(), crc, ptr, len, out);

  __ Bind(slow_path->GetExitLabel());
}

void IntrinsicLocationsBuilderLOONGARCH64::VisitCRC32UpdateByteBuffer(HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator_) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

void IntrinsicCodeGeneratorLOONGARCH64::VisitCRC32UpdateByteBuffer(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  XRegister ptr = locations->GetTemp(0).AsRegister<XRegister>();
  XRegister len = locations->GetTemp(1).AsRegister<XRegister>();
  XRegister addr = locations->InAt(1).AsRegister<XRegister>();
  XRegister offset = locations->InAt(2).AsRegister<XRegister>();
  __ Add_d(ptr, addr, offset);

  XRegister crc = locations->InAt(0).AsRegister<XRegister>();
  XRegister length = locations->InAt(3).AsRegister<XRegister>();
  XRegister out = locations->Out().AsRegister<XRegister>();
  __ Addi_W(len, length, 0);
  GenerateCodeForCalculationCRC32ValueOfBytes(GetAssembler(), crc, ptr, len, out);
}

#define MARK_UNIMPLEMENTED(Name) UNIMPLEMENTED_INTRINSIC(LOONGARCH64, Name)
UNIMPLEMENTED_INTRINSIC_LIST_LOONGARCH64(MARK_UNIMPLEMENTED);
#undef MARK_UNIMPLEMENTED

UNREACHABLE_INTRINSICS(LOONGARCH64)

}  // namespace loongarch64
}  // namespace art
