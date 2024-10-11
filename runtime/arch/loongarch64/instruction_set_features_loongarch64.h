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

#ifndef ART_RUNTIME_ARCH_LOONGARCH64_INSTRUCTION_SET_FEATURES_LOONGARCH64_H_
#define ART_RUNTIME_ARCH_LOONGARCH64_INSTRUCTION_SET_FEATURES_LOONGARCH64_H_

#include "arch/instruction_set_features.h"

namespace art {

class Loongarch64InstructionSetFeatures;
using Loongarch64FeaturesUniquePtr = std::unique_ptr<const Loongarch64InstructionSetFeatures>;

// Instruction set features relevant to the LOONGARCH64 architecture.
class Loongarch64InstructionSetFeatures final : public InstructionSetFeatures {
 public:
  // Bitmap positions for encoding features as a bitmap.
  enum {
    kExtGeneric = (1 << 0),     // G extension covers the basic set
  };

  static Loongarch64FeaturesUniquePtr FromVariant(const std::string& variant, std::string* error_msg);

  // Parse a bitmap and create an InstructionSetFeatures.
  static Loongarch64FeaturesUniquePtr FromBitmap(uint32_t bitmap);

  // Turn C pre-processor #defines into the equivalent instruction set features.
  static Loongarch64FeaturesUniquePtr FromCppDefines();

  // Process /proc/cpuinfo and use kRuntimeISA to produce InstructionSetFeatures.
  static Loongarch64FeaturesUniquePtr FromCpuInfo();

  // Process the auxiliary vector AT_HWCAP entry and use kRuntimeISA to produce
  // InstructionSetFeatures.
  static Loongarch64FeaturesUniquePtr FromHwcap();

  // Use assembly tests of the current runtime (ie kRuntimeISA) to determine the
  // InstructionSetFeatures. This works around kernel bugs in AT_HWCAP and /proc/cpuinfo.
  static Loongarch64FeaturesUniquePtr FromAssembly();

  // Use external cpu_features library.
  static Loongarch64FeaturesUniquePtr FromCpuFeatures();

  bool Equals(const InstructionSetFeatures* other) const override;

  InstructionSet GetInstructionSet() const override { return InstructionSet::kLoongarch64; }

  uint32_t AsBitmap() const override;

  std::string GetFeatureString() const override;

  virtual ~Loongarch64InstructionSetFeatures() {}

 protected:
  std::unique_ptr<const InstructionSetFeatures> AddFeaturesFromSplitString(
      const std::vector<std::string>& features, std::string* error_msg) const override;

 private:
  explicit Loongarch64InstructionSetFeatures(uint32_t bits) : InstructionSetFeatures(), bits_(bits) {}

  // Extension bitmap.
  const uint32_t bits_;

  DISALLOW_COPY_AND_ASSIGN(Loongarch64InstructionSetFeatures);
};

}  // namespace art

#endif  // ART_RUNTIME_ARCH_LOONGARCH64_INSTRUCTION_SET_FEATURES_LOONGARCH64_H_
