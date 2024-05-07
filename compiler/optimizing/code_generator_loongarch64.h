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

#ifndef ART_COMPILER_OPTIMIZING_CODE_GENERATOR_LOONGARCH64_H_
#define ART_COMPILER_OPTIMIZING_CODE_GENERATOR_LOONGARCH64_H_

#include "android-base/logging.h"
#include "arch/loongarch64/registers_loongarch64.h"
#include "base/macros.h"
#include "code_generator.h"
#include "driver/compiler_options.h"
#include "optimizing/locations.h"
#include "parallel_move_resolver.h"
#include "optimizing/nodes.h"
#include "utils/loongarch64/assembler_loongarch64.h"

namespace art {

namespace loongarch64 {

// InvokeDexCallingConvention registers
static constexpr XRegister kParameterCoreRegisters[] = {A1, A2, A3, A4, A5, A6, A7};
static constexpr size_t kParameterCoreRegistersLength = arraysize(kParameterCoreRegisters);

static constexpr FRegister kParameterFpuRegisters[] = {FA0, FA1, FA2, FA3, FA4, FA5, FA6, FA7};
static constexpr size_t kParameterFpuRegistersLength = arraysize(kParameterFpuRegisters);

// InvokeRuntimeCallingConvention registers
static constexpr XRegister kRuntimeParameterCoreRegisters[] = {A0, A1, A2, A3, A4, A5, A6, A7};
static constexpr size_t kRuntimeParameterCoreRegistersLength =
    arraysize(kRuntimeParameterCoreRegisters);

static constexpr FRegister kRuntimeParameterFpuRegisters[] = {
    FA0, FA1, FA2, FA3, FA4, FA5, FA6, FA7
};
static constexpr size_t kRuntimeParameterFpuRegistersLength =
    arraysize(kRuntimeParameterFpuRegisters);

#define UNIMPLEMENTED_INTRINSIC_LIST_LOONGARCH64(V) \
  V(SystemArrayCopyByte)                            \
  V(SystemArrayCopyChar)                            \
  V(SystemArrayCopyInt)                             \
  V(FP16Ceil)                                       \
  V(FP16Compare)                                    \
  V(FP16Floor)                                      \
  V(FP16Rint)                                       \
  V(FP16ToFloat)                                    \
  V(FP16ToHalf)                                     \
  V(FP16Greater)                                    \
  V(FP16GreaterEquals)                              \
  V(FP16Less)                                       \
  V(FP16LessEquals)                                 \
  V(FP16Min)                                        \
  V(FP16Max)                                        \
  V(StringStringIndexOf)                            \
  V(StringStringIndexOfAfter)                       \
  V(StringBufferAppend)                             \
  V(StringBufferLength)                             \
  V(StringBufferToString)                           \
  V(StringBuilderAppendObject)                      \
  V(StringBuilderAppendString)                      \
  V(StringBuilderAppendCharSequence)                \
  V(StringBuilderAppendCharArray)                   \
  V(StringBuilderAppendBoolean)                     \
  V(StringBuilderAppendChar)                        \
  V(StringBuilderAppendInt)                         \
  V(StringBuilderAppendLong)                        \
  V(StringBuilderAppendFloat)                       \
  V(StringBuilderAppendDouble)                      \
  V(StringBuilderLength)                            \
  V(StringBuilderToString)                          \
  V(CRC32Update)                                    \
  V(CRC32UpdateBytes)                               \
  V(CRC32UpdateByteBuffer)                          \
  V(MethodHandleInvokeExact)                        \
  V(MethodHandleInvoke)

// Method register on invoke.
static const XRegister kArtMethodRegister = A0;

class CodeGeneratorLOONGARCH64;

class InvokeRuntimeCallingConvention : public CallingConvention<XRegister, FRegister> {
 public:
  InvokeRuntimeCallingConvention()
      : CallingConvention(kRuntimeParameterCoreRegisters,
                          kRuntimeParameterCoreRegistersLength,
                          kRuntimeParameterFpuRegisters,
                          kRuntimeParameterFpuRegistersLength,
                          kLoongarch64PointerSize) {}

  Location GetReturnLocation(DataType::Type return_type);

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeRuntimeCallingConvention);
};

class InvokeDexCallingConvention : public CallingConvention<XRegister, FRegister> {
 public:
  InvokeDexCallingConvention()
      : CallingConvention(kParameterCoreRegisters,
                          kParameterCoreRegistersLength,
                          kParameterFpuRegisters,
                          kParameterFpuRegistersLength,
                          kLoongarch64PointerSize) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConvention);
};

class InvokeDexCallingConventionVisitorLOONGARCH64 : public InvokeDexCallingConventionVisitor {
 public:
  InvokeDexCallingConventionVisitorLOONGARCH64() {}
  virtual ~InvokeDexCallingConventionVisitorLOONGARCH64() {}

  Location GetNextLocation(DataType::Type type) override;
  Location GetReturnLocation(DataType::Type type) const override;
  Location GetMethodLocation() const override;

 private:
  InvokeDexCallingConvention calling_convention;

  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConventionVisitorLOONGARCH64);
};

class CriticalNativeCallingConventionVisitorLoongarch64 : public InvokeDexCallingConventionVisitor {
 public:
  explicit CriticalNativeCallingConventionVisitorLoongarch64(bool for_register_allocation)
      : for_register_allocation_(for_register_allocation) {}

  virtual ~CriticalNativeCallingConventionVisitorLoongarch64() {}

  Location GetNextLocation(DataType::Type type) override;
  Location GetReturnLocation(DataType::Type type) const override;
  Location GetMethodLocation() const override;

  size_t GetStackOffset() const { return stack_offset_; }

 private:
  // Register allocator does not support adjusting frame size, so we cannot provide final locations
  // of stack arguments for register allocation. We ask the register allocator for any location and
  // move these arguments to the right place after adjusting the SP when generating the call.
  const bool for_register_allocation_;
  size_t gpr_index_ = 0u;
  size_t fpr_index_ = 0u;
  size_t stack_offset_ = 0u;

  DISALLOW_COPY_AND_ASSIGN(CriticalNativeCallingConventionVisitorLoongarch64);
};

class SlowPathCodeLOONGARCH64 : public SlowPathCode {
 public:
  explicit SlowPathCodeLOONGARCH64(HInstruction* instruction)
      : SlowPathCode(instruction), entry_label_(), exit_label_() {}

  Loongarch64Label* GetEntryLabel() { return &entry_label_; }
  Loongarch64Label* GetExitLabel() { return &exit_label_; }

 private:
  Loongarch64Label entry_label_;
  Loongarch64Label exit_label_;

  DISALLOW_COPY_AND_ASSIGN(SlowPathCodeLOONGARCH64);
};

class ParallelMoveResolverLOONGARCH64 : public ParallelMoveResolverWithSwap {
 public:
  ParallelMoveResolverLOONGARCH64(ArenaAllocator* allocator, CodeGeneratorLOONGARCH64* codegen)
      : ParallelMoveResolverWithSwap(allocator), codegen_(codegen) {}

  void EmitMove(size_t index) override;
  void EmitSwap(size_t index) override;
  void SpillScratch(int reg) override;
  void RestoreScratch(int reg) override;

  void Exchange(int index1, int index2, bool double_slot);

  Loongarch64Assembler* GetAssembler() const;

 private:
  CodeGeneratorLOONGARCH64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(ParallelMoveResolverLOONGARCH64);
};

class LocationsBuilderLOONGARCH64 : public HGraphVisitor {
 public:
  LocationsBuilderLOONGARCH64(HGraph* graph, CodeGeneratorLOONGARCH64* codegen)
      : HGraphVisitor(graph), codegen_(codegen) {}

#define DECLARE_VISIT_INSTRUCTION(name, super) void Visit##name(H##name* instr) override;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_LOONGARCH64(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) override {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName() << " (id "
               << instruction->GetId() << ")";
  }

 protected:
  void HandleInvoke(HInvoke* invoke);
  void HandleBinaryOp(HBinaryOperation* operation);
  void HandleCondition(HCondition* instruction);
  void HandleShift(HBinaryOperation* operation);
  void HandleFieldSet(HInstruction* instruction, const FieldInfo& field_info);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);
  Location RegisterOrZeroConstant(HInstruction* instruction);
  Location FpuRegisterOrConstantForStore(HInstruction* instruction);

  InvokeDexCallingConventionVisitorLOONGARCH64 parameter_visitor_;

  CodeGeneratorLOONGARCH64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(LocationsBuilderLOONGARCH64);
};

class InstructionCodeGeneratorLOONGARCH64 : public InstructionCodeGenerator {
 public:
  InstructionCodeGeneratorLOONGARCH64(HGraph* graph, CodeGeneratorLOONGARCH64* codegen);

#define DECLARE_VISIT_INSTRUCTION(name, super) void Visit##name(H##name* instr) override;

  FOR_EACH_CONCRETE_INSTRUCTION_COMMON(DECLARE_VISIT_INSTRUCTION)
  FOR_EACH_CONCRETE_INSTRUCTION_LOONGARCH64(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

  void VisitInstruction(HInstruction* instruction) override {
    LOG(FATAL) << "Unreachable instruction " << instruction->DebugName() << " (id "
               << instruction->GetId() << ")";
  }

  Loongarch64Assembler* GetAssembler() const { return assembler_; }

  void GenerateMemoryBarrier(MemBarrierKind kind);

 protected:
  void GenerateClassInitializationCheck(SlowPathCodeLOONGARCH64* slow_path, XRegister class_reg);
  void GenerateBitstringTypeCheckCompare(HTypeCheckInstruction* check, XRegister temp);
  void GenerateSuspendCheck(HSuspendCheck* check, HBasicBlock* successor);
  void HandleBinaryOp(HBinaryOperation* operation);
  void HandleCondition(HCondition* instruction);
  void HandleShift(HBinaryOperation* operation);
  void HandleFieldSet(HInstruction* instruction,
                      const FieldInfo& field_info,
                      bool value_can_be_null);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);

  void GenerateMinMaxInt(LocationSummary* locations, bool is_min);
  void GenerateMinMaxFP(LocationSummary* locations, bool is_min, DataType::Type type);
  void GenerateMinMax(HBinaryOperation* minmax, bool is_min);

  // Generate a heap reference load using one register `out`:
  //
  //   out <- *(out + offset)
  //
  // while honoring heap poisoning and/or read barriers (if any).
  //
  // Location `maybe_temp` is used when generating a read barrier and
  // shall be a register in that case; it may be an invalid location
  // otherwise.
  void GenerateReferenceLoadOneRegister(HInstruction* instruction,
                                        Location out,
                                        uint32_t offset,
                                        Location maybe_temp,
                                        ReadBarrierOption read_barrier_option);
  // Generate a heap reference load using two different registers
  // `out` and `obj`:
  //
  //   out <- *(obj + offset)
  //
  // while honoring heap poisoning and/or read barriers (if any).
  //
  // Location `maybe_temp` is used when generating a Baker's (fast
  // path) read barrier and shall be a register in that case; it may
  // be an invalid location otherwise.
  void GenerateReferenceLoadTwoRegisters(HInstruction* instruction,
                                         Location out,
                                         Location obj,
                                         uint32_t offset,
                                         Location maybe_temp,
                                         ReadBarrierOption read_barrier_option);

  // Generate a GC root reference load:
  //
  //   root <- *(obj + offset)
  //
  // while honoring read barriers (if any).
  void GenerateGcRootFieldLoad(HInstruction* instruction,
                               Location root,
                               XRegister obj,
                               uint32_t offset,
                               ReadBarrierOption read_barrier_option,
                               Loongarch64Label* label_low = nullptr);

  void GenerateTestAndBranch(HInstruction* instruction,
                             size_t condition_input_index,
                             Loongarch64Label* true_target,
                             Loongarch64Label* false_target);
  void DivRemOneOrMinusOne(HBinaryOperation* instruction);
  void DivRemByPowerOfTwo(HBinaryOperation* instruction);
  void GenerateDivRemWithAnyConstant(HBinaryOperation* instruction);
  void GenerateDivRemIntegral(HBinaryOperation* instruction);
  void GenerateIntLongCondition(IfCondition cond, LocationSummary* locations);
  void GenerateIntLongCompareAndBranch(IfCondition cond,
                                       LocationSummary* locations,
                                       Loongarch64Label* label);
  void GenerateFpCondition(IfCondition cond,
                         bool gt_bias,
                         DataType::Type type,
                         LocationSummary* locations,
                         Loongarch64Label* label = nullptr);
  void HandleGoto(HInstruction* got, HBasicBlock* successor);
  void GenPackedSwitchWithCompares(XRegister value_reg,
                                   int32_t lower_bound,
                                   uint32_t num_entries,
                                   HBasicBlock* switch_block,
                                   HBasicBlock* default_block);
  void GenTableBasedPackedSwitch(XRegister value_reg,
                                 int32_t lower_bound,
                                 uint32_t num_entries,
                                 HBasicBlock* switch_block,
                                 HBasicBlock* default_block);
  int32_t VecAddress(LocationSummary* locations,
                     size_t size,
                     /*out*/ XRegister* adjusted_base);
  void GenConditionalMove(HSelect* select);

  Loongarch64Assembler* const assembler_;
  CodeGeneratorLOONGARCH64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(InstructionCodeGeneratorLOONGARCH64);
};

class CodeGeneratorLOONGARCH64 : public CodeGenerator {
 public:
  CodeGeneratorLOONGARCH64(HGraph* graph,
                       const CompilerOptions& compiler_options,
                       OptimizingCompilerStats* stats = nullptr);
  virtual ~CodeGeneratorLOONGARCH64() {}

  void GenerateFrameEntry() override;
  void GenerateFrameExit() override;

  void Bind(HBasicBlock* block) override;

  size_t GetWordSize() const override { return kLoongarch64WordSize; }

  bool SupportsPredicatedSIMD() const override {
    // TODO(loongarch64): Check the vector extension.
    return false;
  }

  size_t GetSlowPathFPWidth() const override {
    LOG(FATAL) << "CodeGeneratorLOONGARCH64::GetSlowPathFPWidth is unimplemented";
    UNREACHABLE();
  }

  size_t GetCalleePreservedFPWidth() const override {
    LOG(FATAL) << "CodeGeneratorLOONGARCH64::GetCalleePreservedFPWidth is unimplemented";
    UNREACHABLE();
  };

  size_t GetSIMDRegisterWidth() const override {
    LOG(FATAL) << "Vector is not unimplemented";
    UNREACHABLE();
  };

  uintptr_t GetAddressOf(HBasicBlock* block) override {
    return assembler_.GetLabelLocation(GetLabelOf(block));
  };

  Loongarch64Label* GetLabelOf(HBasicBlock* block) const {
    return CommonGetLabelOf<Loongarch64Label>(block_labels_, block);
  }

  void Initialize() override { block_labels_ = CommonInitializeLabels<Loongarch64Label>(); }

  void MoveConstant(Location destination, int32_t value) override;
  void MoveLocation(Location destination, Location source, DataType::Type dst_type) override;
  void AddLocationAsTemp(Location location, LocationSummary* locations) override;

  HGraphVisitor* GetInstructionVisitor() override { return &instruction_visitor_; }

  Loongarch64Assembler* GetAssembler() override { return &assembler_; }
  const Loongarch64Assembler& GetAssembler() const override { return assembler_; }

  HGraphVisitor* GetLocationBuilder() override { return &location_builder_; }

  void MaybeGenerateInlineCacheCheck(HInstruction* instruction, XRegister klass);

  void SetupBlockedRegisters() const override;

  size_t SaveCoreRegister(size_t stack_index, uint32_t reg_id) override;
  size_t RestoreCoreRegister(size_t stack_index, uint32_t reg_id) override;
  size_t SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id) override;
  size_t RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id) override;

  void DumpCoreRegister(std::ostream& stream, int reg) const override;
  void DumpFloatingPointRegister(std::ostream& stream, int reg) const override;

  InstructionSet GetInstructionSet() const override { return InstructionSet::kLoongarch64; }

  uint32_t GetPreferredSlotsAlignment() const override {
    return static_cast<uint32_t>(kLoongarch64PointerSize);
  }

  void Finalize() override;

  // Generate code to invoke a runtime entry point.
  void InvokeRuntime(QuickEntrypointEnum entrypoint,
                     HInstruction* instruction,
                     uint32_t dex_pc,
                     SlowPathCode* slow_path = nullptr) override;

  // Generate code to invoke a runtime entry point, but do not record
  // PC-related information in a stack map.
  void InvokeRuntimeWithoutRecordingPcInfo(int32_t entry_point_offset,
                                           HInstruction* instruction,
                                           SlowPathCode* slow_path);

  ParallelMoveResolver* GetMoveResolver() override { return &move_resolver_; }

  bool NeedsTwoRegisters([[maybe_unused]] DataType::Type type) const override { return false; }

  void IncreaseFrame(size_t adjustment) override;
  void DecreaseFrame(size_t adjustment) override;

  void GenerateNop() override;

  void GenerateImplicitNullCheck(HNullCheck* instruction) override;
  void GenerateExplicitNullCheck(HNullCheck* instruction) override;

  // Check if the desired_string_load_kind is supported. If it is, return it,
  // otherwise return a fall-back kind that should be used instead.
  HLoadString::LoadKind GetSupportedLoadStringKind(
      HLoadString::LoadKind desired_string_load_kind) override;

  // Check if the desired_class_load_kind is supported. If it is, return it,
  // otherwise return a fall-back kind that should be used instead.
  HLoadClass::LoadKind GetSupportedLoadClassKind(
      HLoadClass::LoadKind desired_class_load_kind) override;

  // Check if the desired_dispatch_info is supported. If it is, return it,
  // otherwise return a fall-back info that should be used instead.
  HInvokeStaticOrDirect::DispatchInfo GetSupportedInvokeStaticOrDirectDispatch(
      const HInvokeStaticOrDirect::DispatchInfo& desired_dispatch_info, ArtMethod* method) override;

  // The PcRelativePatchInfo is used for PC-relative addressing of methods/strings/types,
  // whether through .data.bimg.rel.ro, .bss, or directly in the boot image.
  //
  // The 20-bit and 12-bit parts of the 32-bit PC-relative offset are patched separately,
  // necessitating two patches/infos. There can be more than two patches/infos if the
  // instruction supplying the high part is shared with e.g. a slow path, while the low
  // part is supplied by separate instructions, e.g.:
  //     pcaddu12i $r1, high  // patch
  //     ld.wu $r2, $r1, low  // patch
  //     beqz  $r2, slow_path
  //   back:
  //     ...
  //   slow_path:
  //     ...
  //     st.w    $r2, $r1, low    // patch
  //     b     back
  struct PcRelativePatchInfo : PatchInfo<Loongarch64Label> {
    PcRelativePatchInfo(const DexFile* dex_file,
                        uint32_t off_or_idx,
                        const PcRelativePatchInfo* info_high)
        : PatchInfo<Loongarch64Label>(dex_file, off_or_idx), patch_info_high(info_high) {}

    // Pointer to the info for the high part patch or nullptr if this is the high part patch info.
    const PcRelativePatchInfo* patch_info_high;

   private:
    PcRelativePatchInfo(PcRelativePatchInfo&& other) = delete;
    DISALLOW_COPY_AND_ASSIGN(PcRelativePatchInfo);
  };

  PcRelativePatchInfo* NewBootImageIntrinsicPatch(uint32_t intrinsic_data,
                                                  const PcRelativePatchInfo* info_high = nullptr);
  PcRelativePatchInfo* NewBootImageRelRoPatch(uint32_t boot_image_offset,
                                              const PcRelativePatchInfo* info_high = nullptr);
  PcRelativePatchInfo* NewBootImageMethodPatch(MethodReference target_method,
                                               const PcRelativePatchInfo* info_high = nullptr);
  PcRelativePatchInfo* NewMethodBssEntryPatch(MethodReference target_method,
                                              const PcRelativePatchInfo* info_high = nullptr);
  PcRelativePatchInfo* NewBootImageJniEntrypointPatch(
      MethodReference target_method, const PcRelativePatchInfo* info_high = nullptr);

  PcRelativePatchInfo* NewBootImageTypePatch(const DexFile& dex_file,
                                             dex::TypeIndex type_index,
                                             const PcRelativePatchInfo* info_high = nullptr);
  PcRelativePatchInfo* NewTypeBssEntryPatch(HLoadClass* load_class,
                                            const PcRelativePatchInfo* info_high = nullptr);
  PcRelativePatchInfo* NewBootImageStringPatch(const DexFile& dex_file,
                                               dex::StringIndex string_index,
                                               const PcRelativePatchInfo* info_high = nullptr);
  PcRelativePatchInfo* NewStringBssEntryPatch(const DexFile& dex_file,
                                              dex::StringIndex string_index,
                                              const PcRelativePatchInfo* info_high = nullptr);

  void EmitPcRelativePcaddu12iPlaceholder(PcRelativePatchInfo* info_high, XRegister out);
  void EmitPcRelativeAddi_dPlaceholder(PcRelativePatchInfo* info_low, XRegister rd, XRegister rs1);
  void EmitPcRelativeLd_wuPlaceholder(PcRelativePatchInfo* info_low, XRegister rd, XRegister rs1);
  void EmitPcRelativeLd_dPlaceholder(PcRelativePatchInfo* info_low, XRegister rd, XRegister rs1);

  Literal* DeduplicateBootImageAddressLiteral(uint64_t address);

  void LoadMethod(MethodLoadKind load_kind, Location temp, HInvoke* invoke);
  void GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke,
                                  Location temp,
                                  SlowPathCode* slow_path = nullptr) override;
  void GenerateVirtualCall(HInvokeVirtual* invoke,
                           Location temp,
                           SlowPathCode* slow_path = nullptr) override;
  void MoveFromReturnRegister(Location trg, DataType::Type type) override;

  void GenerateMemoryBarrier(MemBarrierKind kind);

  void MaybeIncrementHotness(bool is_frame_entry);

  bool CanUseImplicitSuspendCheck() const;

  //
  // Heap poisoning.
  //

  // Poison a heap reference contained in `reg`.
  void PoisonHeapReference(XRegister reg);

  // Unpoison a heap reference contained in `reg`.
  void UnpoisonHeapReference(XRegister reg);

  // Poison a heap reference contained in `reg` if heap poisoning is enabled.
  void MaybePoisonHeapReference(XRegister reg);

  // Unpoison a heap reference contained in `reg` if heap poisoning is enabled.
  void MaybeUnpoisonHeapReference(XRegister reg);

  void SwapLocations(Location loc1, Location loc2, DataType::Type type);

private:
  using Uint32ToLiteralMap = ArenaSafeMap<uint32_t, Literal*>;
  using Uint64ToLiteralMap = ArenaSafeMap<uint64_t, Literal*>;
  using StringToLiteralMap =
      ArenaSafeMap<StringReference, Literal*, StringReferenceValueComparator>;
  using TypeToLiteralMap = ArenaSafeMap<TypeReference, Literal*, TypeReferenceValueComparator>;

  Literal* DeduplicateUint32Literal(uint32_t value);
  Literal* DeduplicateUint64Literal(uint64_t value);

  PcRelativePatchInfo* NewPcRelativePatch(const DexFile* dex_file,
                                          uint32_t offset_or_index,
                                          const PcRelativePatchInfo* info_high,
                                          ArenaDeque<PcRelativePatchInfo>* patches);
  Loongarch64Assembler assembler_;
  LocationsBuilderLOONGARCH64 location_builder_;
  InstructionCodeGeneratorLOONGARCH64 instruction_visitor_;
  Loongarch64Label frame_entry_label_;

  // Labels for each block that will be compiled.
  Loongarch64Label* block_labels_;  // Indexed by block id.

  ParallelMoveResolverLOONGARCH64 move_resolver_;

  // Deduplication map for 32-bit literals, used for non-patchable boot image addresses.
  Uint32ToLiteralMap uint32_literals_;
  // Deduplication map for 64-bit literals, used for non-patchable method address or method code
  // address.
  Uint64ToLiteralMap uint64_literals_;

  // PC-relative method patch info for kBootImageLinkTimePcRelative.
  ArenaDeque<PcRelativePatchInfo> boot_image_method_patches_;
  // PC-relative method patch info for kBssEntry.
  ArenaDeque<PcRelativePatchInfo> method_bss_entry_patches_;
  // PC-relative type patch info for kBootImageLinkTimePcRelative.
  ArenaDeque<PcRelativePatchInfo> boot_image_type_patches_;
  // PC-relative type patch info for kBssEntry.
  ArenaDeque<PcRelativePatchInfo> type_bss_entry_patches_;
  // PC-relative public type patch info for kBssEntryPublic.
  ArenaDeque<PcRelativePatchInfo> public_type_bss_entry_patches_;
  // PC-relative package type patch info for kBssEntryPackage.
  ArenaDeque<PcRelativePatchInfo> package_type_bss_entry_patches_;
  // PC-relative String patch info for kBootImageLinkTimePcRelative.
  ArenaDeque<PcRelativePatchInfo> boot_image_string_patches_;
  // PC-relative String patch info for kBssEntry.
  ArenaDeque<PcRelativePatchInfo> string_bss_entry_patches_;
  // PC-relative method patch info for kBootImageLinkTimePcRelative+kCallCriticalNative.
  ArenaDeque<PcRelativePatchInfo> boot_image_jni_entrypoint_patches_;
  // PC-relative patch info for IntrinsicObjects for the boot image,
  // and for method/type/string patches for kBootImageRelRo otherwise.
  ArenaDeque<PcRelativePatchInfo> boot_image_other_patches_;
};

}  // namespace loongarch64
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_GENERATOR_LOONGARCH64_H_
