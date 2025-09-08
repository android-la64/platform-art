/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef ART_COMPILER_OPTIMIZING_INSTRUCTION_SIMPLIFIER_LOONGARCH64_H_
#define ART_COMPILER_OPTIMIZING_INSTRUCTION_SIMPLIFIER_LOONGARCH64_H_

#include "base/macros.h"
#include "nodes.h"
#include "optimization.h"

namespace art HIDDEN {

namespace loongarch64 {

class InstructionSimplifierLoongarch64 : public HOptimization {
 public:
  InstructionSimplifierLoongarch64(HGraph* graph, OptimizingCompilerStats* stats)
      : HOptimization(graph, kInstructionSimplifierLoongarch64PassName, stats) {}

  static constexpr const char* kInstructionSimplifierLoongarch64PassName =
      "instruction_simplifier_loongarch64";

  bool Run() override;
};

}  // namespace loongarch64
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_INSTRUCTION_SIMPLIFIER_LOONGARCH64_H_
