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

#ifndef ART_RUNTIME_ARCH_LOONGARCH64_REGISTERS_LOONGARCH64_H_
#define ART_RUNTIME_ARCH_LOONGARCH64_REGISTERS_LOONGARCH64_H_

#include <iosfwd>

#include "base/macros.h"

namespace art {
namespace loongarch64 {

enum XRegister {
  Zero = 0,  // X0, hard-wired zero
  RA = 1,   // X1, return address
  TP = 2,   // X2, thread pointer (points to TLS area, not ART-internal thread)
  SP = 3,   // X3, stack pointer

  A0 = 4,   // X4, argument 0 / return value 0
  A1 = 5,   // X5, argument 1 / return value 1
  A2 = 6,   // X6, argument 2
  A3 = 7,   // X7, argument 3
  A4 = 8,   // X8, argument 4
  A5 = 9,   // X9, argument 5
  A6 = 10,  // X10, argument 6
  A7 = 11,  // X11, argument 7

  T0 = 12,  // X12, temporary 0
  T1 = 13,  // X13, temporary 1
  T2 = 14,  // X14, temporary 2
  T3 = 15,  // X15, temporary 3
  T4 = 16,  // X16, temporary 4
  T5 = 17,  // X17, temporary 5
  T6 = 18,  // X18, temporary 6
  T7 = 19,  // X19, temporary 7
  T8 = 20,  // X20, temporary 8
  T9 = 21,  // X21, Reserved

  S9 = 22,  // X22/FP, callee-saved 9, frame pointer

  S0 = 23,  // X23, callee-saved 0
  S1 = 24,  // X24, callee-saved 1
  S2 = 25,  // X25, callee-saved 2
  S3 = 26,  // X26, callee-saved 3
  S4 = 27,  // X27, callee-saved 4
  S5 = 28,  // X28, callee-saved 5
  S6 = 29,  // X29, callee-saved 6
  S7 = 30,  // X30, callee-saved 7
  S8 = 31,  // X31, callee-saved 8

  kNumberOfXRegisters = 32,
  kNoRegister = -1,  // Signals an illegal register.

  // Aliases.
  TR = S1,  // ART Thread Register - managed runtime
};

std::ostream& operator<<(std::ostream& os, const XRegister& rhs);

enum FRegister {
  FA0 = 0,  // F10, argument 0 / return value 0
  FA1 = 1,  // F11, argument 1 / return value 1
  FA2 = 2,  // F12, argument 2
  FA3 = 3,  // F13, argument 3
  FA4 = 4,  // F14, argument 4
  FA5 = 5,  // F15, argument 5
  FA6 = 6,  // F16, argument 6
  FA7 = 7,  // F17, argument 7

  FT0 = 8,    // F8, temporary 0
  FT1 = 9,    // F9, temporary 1
  FT2 = 10,   // F10, temporary 2
  FT3 = 11,   // F11, temporary 3
  FT4 = 12,   // F12, temporary 4
  FT5 = 13,   // F13, temporary 5
  FT6 = 14,   // F14, temporary 6
  FT7 = 15,   // F15, temporary 7
  FT8 = 16,   // F16, temporary 8
  FT9 = 17,   // F17, temporary 9
  FT10 = 18,  // F18, temporary 18
  FT11 = 19,  // F19, temporary 19
  FT12 = 20,  // F20, temporary 20
  FT13 = 21,  // F21, temporary 21
  FT14 = 22,  // F22, temporary 22
  FT15 = 23,  // F23, temporary 23

  FS0 = 24,  // F24, callee-saved 0
  FS1 = 25,  // F25, callee-saved 1
  FS2 = 26,  // F26, callee-saved 2
  FS3 = 27,  // F27, callee-saved 3
  FS4 = 28,  // F28, callee-saved 4
  FS5 = 29,  // F29, callee-saved 5
  FS6 = 30,  // F30, callee-saved 6
  FS7 = 31,  // F31, callee-saved 7

  kNumberOfFRegisters = 32,
};

std::ostream& operator<<(std::ostream& os, const FRegister& rhs);

}  // namespace loongarch64
}  // namespace art

#endif  // ART_RUNTIME_ARCH_LOONGARCH64_REGISTERS_LOONGARCH64_H_
