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

#include <gtest/gtest.h>

namespace art {

TEST(Loongarch64InstructionSetFeaturesTest, Loongarch64FeaturesFromDefaultVariant) {
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> loongarch64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kLoongarch64, "generic", &error_msg));
  ASSERT_TRUE(loongarch64_features.get() != nullptr) << error_msg;

  EXPECT_EQ(loongarch64_features->GetInstructionSet(), InstructionSet::kLoongarch64);

  EXPECT_TRUE(loongarch64_features->Equals(loongarch64_features.get()));
}

}  // namespace art
