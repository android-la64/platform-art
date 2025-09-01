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

#include "intrinsics_loongarch64.h"
#include "runtime.h"
#include "code_generator_loongarch64.h"

namespace art {
namespace loongarch64 {

bool IntrinsicLocationsBuilderLOONGARCH64::TryDispatch(HInvoke* invoke) {
  Dispatch(invoke);
  LocationSummary* res = invoke->GetLocations();
  if (res == nullptr) {
    return false;
  }
  return res->Intrinsified();
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

static void CreateIntToFPLocations(ArenaAllocator* allocator, HInvoke* invoke) {
  LocationSummary* locations =
      new (allocator) LocationSummary(invoke, LocationSummary::kNoCall, kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresFpuRegister());
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

template <typename EmitOp>
void EmitMemoryPoke(HInvoke* invoke, EmitOp&& emit_op) {
  LocationSummary* locations = invoke->GetLocations();
  emit_op(locations->InAt(1).AsRegister<XRegister>(), locations->InAt(0).AsRegister<XRegister>());
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

#define MARK_UNIMPLEMENTED(Name) UNIMPLEMENTED_INTRINSIC(LOONGARCH64, Name)
UNIMPLEMENTED_INTRINSIC_LIST_LOONGARCH64(MARK_UNIMPLEMENTED);
#undef MARK_UNIMPLEMENTED

UNREACHABLE_INTRINSICS(LOONGARCH64)

}  // namespace loongarch64
}  // namespace art
