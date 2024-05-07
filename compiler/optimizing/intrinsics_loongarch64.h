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

#ifndef ART_COMPILER_OPTIMIZING_INTRINSICS_LOONGARCH64_H_
#define ART_COMPILER_OPTIMIZING_INTRINSICS_LOONGARCH64_H_

#include "base/macros.h"
#include "intrinsics.h"
#include "intrinsics_list.h"

namespace art {

class ArenaAllocator;
class HInvokeStaticOrDirect;
class HInvokeVirtual;

namespace loongarch64 {

class CodeGeneratorLOONGARCH64;
class Loongarch64Assembler;

class IntrinsicLocationsBuilderLOONGARCH64 final : public IntrinsicVisitor {
 public:
  explicit IntrinsicLocationsBuilderLOONGARCH64(ArenaAllocator* allocator,
                                            CodeGeneratorLOONGARCH64* codegen)
      : allocator_(allocator), codegen_(codegen) {}

  // Define visitor methods.

  // TODO(loongarch64): Implement in `intrinsics_loongarch64.cc`.
#define OPTIMIZING_INTRINSICS(                                             \
    Name, IsStatic, NeedsEnvironmentOrCache, SideEffects, Exceptions, ...) \
  void Visit##Name(HInvoke* invoke) override {                             \
    UNUSED(invoke);                                                        \
    LOG(FATAL) << "Unimplemented";                                         \
  }
  INTRINSICS_LIST(OPTIMIZING_INTRINSICS)
#undef OPTIMIZING_INTRINSICS

  // Check whether an invoke is an intrinsic, and if so, create a location summary. Returns whether
  // a corresponding LocationSummary with the intrinsified_ flag set was generated and attached to
  // the invoke.
  bool TryDispatch(HInvoke* invoke) {
    // TODO(loongarch64): Implement in `intrinsics_loongarch64.cc`.
    UNUSED(invoke);
    // Avoid compiling failed with "not used"
    UNUSED(codegen_);
    UNUSED(allocator_);
    return false;
  }

 private:
  ArenaAllocator* const allocator_;
  CodeGeneratorLOONGARCH64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(IntrinsicLocationsBuilderLOONGARCH64);
};

class IntrinsicCodeGeneratorLOONGARCH64 final : public IntrinsicVisitor {
 public:
  explicit IntrinsicCodeGeneratorLOONGARCH64(CodeGeneratorLOONGARCH64* codegen) : codegen_(codegen) {}

  // Define visitor methods.

  // TODO(loongarch64): Implement in `intrinsics_loongarch64.cc`.
#define OPTIMIZING_INTRINSICS(                                             \
    Name, IsStatic, NeedsEnvironmentOrCache, SideEffects, Exceptions, ...) \
  void Visit##Name(HInvoke* invoke) override {                             \
    UNUSED(invoke);                                                        \
    LOG(FATAL) << "Unimplemented";                                         \
  }
  INTRINSICS_LIST(OPTIMIZING_INTRINSICS)
#undef OPTIMIZING_INTRINSICS

 private:
  Loongarch64Assembler* GetAssembler();

  ArenaAllocator* GetAllocator();

  CodeGeneratorLOONGARCH64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(IntrinsicCodeGeneratorLOONGARCH64);
};

}  // namespace loongarch64
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_INTRINSICS_LOONGARCH64_H_

