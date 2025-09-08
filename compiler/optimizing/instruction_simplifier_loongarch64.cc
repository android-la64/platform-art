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

#include "instruction_simplifier_loongarch64.h"

#include "instruction_simplifier.h"

namespace art HIDDEN {

namespace loongarch64 {

class InstructionSimplifierLoongarch64Visitor final : public HGraphVisitor {
 public:
  InstructionSimplifierLoongarch64Visitor(HGraph* graph, OptimizingCompilerStats* stats)
      : HGraphVisitor(graph), stats_(stats) {}

 private:
  void RecordSimplification() {
    MaybeRecordStat(stats_, MethodCompilationStat::kInstructionSimplificationsArch);
  }

  void VisitBasicBlock(HBasicBlock* block) override {
    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (instruction->IsInBlock()) {
        instruction->Accept(this);
      }
    }
  }

  void VisitAnd(HAnd* inst) override {
    if (TryMergeNegatedInput(inst)) {
      RecordSimplification();
    }
  }

  void VisitOr(HOr* inst) override {
    if (TryMergeNegatedInput(inst)) {
      RecordSimplification();
    }
  }

  void VisitSub(HSub* inst) override {
    if (TryMergeWithAnd(inst)) {
      RecordSimplification();
    }
  }

  void VisitXor(HXor* inst) override {
    if (TryMergeNegatedInput(inst)) {
      RecordSimplification();
    }
  }

  OptimizingCompilerStats* stats_ = nullptr;
};

bool InstructionSimplifierLoongarch64::Run() {
  auto visitor = InstructionSimplifierLoongarch64Visitor(graph_, stats_);
  visitor.VisitReversePostOrder();
  return true;
}

}  // namespace loongarch64
}  // namespace art
