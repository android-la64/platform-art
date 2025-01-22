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

#include "registers_loongarch64.h"

#include <ostream>

namespace art {
namespace loongarch64 {

static const char* kXRegisterNames[] = {
  "zero", "ra", "tp", "sp", "a0", "a1", "a2", "a3",
  "a4", "a5", "a6", "a7", "t0", "t1", "t2", "t3",
  "t4", "t5", "t6", "t7", "t8", "t9", "fp", "s0",
  "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"
};

static const char* kFRegisterNames[] = {
  "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7",
  "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
  "ft8", "ft9", "ft10", "ft11", "ft12", "ft13", "ft14", "ft15",
  "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
};

std::ostream& operator<<(std::ostream& os, const XRegister& rhs) {
  if (rhs >= Zero && rhs < kNumberOfXRegisters) {
    os << kXRegisterNames[rhs];
  } else {
    os << "XRegister[" << static_cast<int>(rhs) << "]";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const FRegister& rhs) {
  if (rhs >= FT0 && rhs < kNumberOfFRegisters) {
    os << kFRegisterNames[rhs];
  } else {
    os << "FRegister[" << static_cast<int>(rhs) << "]";
  }
  return os;
}

}  // namespace loongarch64
}  // namespace art
