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

#include "instruction_set_features_loongarch64.h"

#include <fstream>
#include <sstream>

#include "android-base/stringprintf.h"
#include "android-base/strings.h"
#include "base/logging.h"

namespace art HIDDEN {

using android::base::StringPrintf;


// LA-TODO: add cpu features.
constexpr uint32_t BasicFeatures() {
  return 0;
}

Loongarch64FeaturesUniquePtr Loongarch64InstructionSetFeatures::FromVariant(
    const std::string& variant, [[maybe_unused]] std::string* error_msg) {
  if (variant != "generic") {
    LOG(WARNING) << "Unexpected CPU variant for Loongarch64 using defaults: " << variant;
  }
  return Loongarch64FeaturesUniquePtr(new Loongarch64InstructionSetFeatures(BasicFeatures()));
}

Loongarch64FeaturesUniquePtr Loongarch64InstructionSetFeatures::FromBitmap(uint32_t bitmap) {
  return Loongarch64FeaturesUniquePtr(new Loongarch64InstructionSetFeatures(bitmap));
}

Loongarch64FeaturesUniquePtr Loongarch64InstructionSetFeatures::FromCppDefines() {
  return Loongarch64FeaturesUniquePtr(new Loongarch64InstructionSetFeatures(BasicFeatures()));
}

Loongarch64FeaturesUniquePtr Loongarch64InstructionSetFeatures::FromCpuInfo() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Loongarch64FeaturesUniquePtr Loongarch64InstructionSetFeatures::FromHwcap() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Loongarch64FeaturesUniquePtr Loongarch64InstructionSetFeatures::FromAssembly() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

Loongarch64FeaturesUniquePtr Loongarch64InstructionSetFeatures::FromCpuFeatures() {
  UNIMPLEMENTED(WARNING);
  return FromCppDefines();
}

bool Loongarch64InstructionSetFeatures::Equals(const InstructionSetFeatures* other) const {
  if (InstructionSet::kRiscv64 != other->GetInstructionSet()) {
    return false;
  }
  return bits_ == other->AsLoongarch64InstructionSetFeatures()->bits_;
}

uint32_t Loongarch64InstructionSetFeatures::AsBitmap() const { return bits_; }

std::string Loongarch64InstructionSetFeatures::GetFeatureString() const {
  std::string result = "";
  
  return result;
}

std::unique_ptr<const InstructionSetFeatures>
Loongarch64InstructionSetFeatures::AddFeaturesFromSplitString(const std::vector<std::string>& features ATTRIBUTE_UNUSED,
                                                          std::string* error_msg ATTRIBUTE_UNUSED) const {
  UNIMPLEMENTED(WARNING);
  return std::unique_ptr<const InstructionSetFeatures>(new Loongarch64InstructionSetFeatures(bits_));
}

}  // namespace art
