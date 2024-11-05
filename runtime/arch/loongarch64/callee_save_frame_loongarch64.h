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

#ifndef ART_RUNTIME_ARCH_LOONGARCH64_CALLEE_SAVE_FRAME_LOONGARCH64_H_
#define ART_RUNTIME_ARCH_LOONGARCH64_CALLEE_SAVE_FRAME_LOONGARCH64_H_

#include "arch/instruction_set.h"
#include "base/bit_utils.h"
#include "base/callee_save_type.h"
#include "base/macros.h"
#include "base/pointer_size.h"
#include "quick/quick_method_frame_info.h"
#include "registers_loongarch64.h"
#include "runtime_globals.h"

namespace art {
namespace loongarch64 {

static constexpr uint32_t kLoongarch64CalleeSaveAlwaysSpills =
    (1 << art::loongarch64::RA);  // Return address
// Callee-saved registers except for SP and S1 (SP is callee-saved according to LOONGARCH spec, but
// it cannot contain object reference, and S1(TR) is excluded as the ART thread register).
static constexpr uint32_t kLoongarch64CalleeSaveRefSpills =
    (1 << art::loongarch64::S0) | (1 << art::loongarch64::S2) | (1 << art::loongarch64::S3) |
    (1 << art::loongarch64::S4) | (1 << art::loongarch64::S5) | (1 << art::loongarch64::S6) |
    (1 << art::loongarch64::S7) | (1 << art::loongarch64::S8) | (1 << art::loongarch64::FP);
// Stack pointer SP is excluded (although it is callee-saved by calling convention) because it is
// restored by the code logic and not from a stack frame.
static constexpr uint32_t kLoongarch64CalleeSaveAllSpills = 0;
// Argument registers except A0 (which contains method pointer).
static constexpr uint32_t kLoongarch64CalleeSaveArgSpills =
    (1 << art::loongarch64::A1) | (1 << art::loongarch64::A2) | (1 << art::loongarch64::A3) |
    (1 << art::loongarch64::A4) | (1 << art::loongarch64::A5) | (1 << art::loongarch64::A6) |
    (1 << art::loongarch64::A7);
// All registers except SP, immutable Zero, unallocatable thread pointer TP.
static constexpr uint32_t kLoongarch64CalleeSaveEverythingSpills =
    (1 << art::loongarch64::TR) | (1 << art::loongarch64::T0) | (1 << art::loongarch64::T1) |
    (1 << art::loongarch64::T2) | (1 << art::loongarch64::T3) | (1 << art::loongarch64::T4) |
    (1 << art::loongarch64::T5) | (1 << art::loongarch64::T6) | (1 << art::loongarch64::T7) |
    (1 << art::loongarch64::T8) | (1 << art::loongarch64::A0) | (1 << art::loongarch64::A1) |
    (1 << art::loongarch64::A2) | (1 << art::loongarch64::A3) | (1 << art::loongarch64::A4) |
    (1 << art::loongarch64::A5) | (1 << art::loongarch64::A6) | (1 << art::loongarch64::A7);

// No references in floating-point registers.
static constexpr uint32_t kLoongarch64CalleeSaveFpSpills = 0;
// Floating-point argument registers FA0 - FA7.
static constexpr uint32_t kLoongarch64CalleeSaveFpArgSpills =
    (1 << art::loongarch64::FA0) | (1 << art::loongarch64::FA1) | (1 << art::loongarch64::FA2) |
    (1 << art::loongarch64::FA3) | (1 << art::loongarch64::FA4) | (1 << art::loongarch64::FA5) |
    (1 << art::loongarch64::FA6) | (1 << art::loongarch64::FA7);
// Floating-point callee-saved registers FS0 - FS7.
static constexpr uint32_t kLoongarch64CalleeSaveFpAllSpills =
    (1 << art::loongarch64::FS0) | (1 << art::loongarch64::FS1) | (1 << art::loongarch64::FS2) |
    (1 << art::loongarch64::FS3) | (1 << art::loongarch64::FS4) | (1 << art::loongarch64::FS5) |
    (1 << art::loongarch64::FS6) | (1 << art::loongarch64::FS7);
// All floating-point registers.
static constexpr uint32_t kLoongarch64CalleeSaveFpEverythingSpills =
    (1 << art::loongarch64::FA0) | (1 << art::loongarch64::FA1) | (1 << art::loongarch64::FA2) |
    (1 << art::loongarch64::FA3) | (1 << art::loongarch64::FA4) | (1 << art::loongarch64::FA5) |
    (1 << art::loongarch64::FA6) | (1 << art::loongarch64::FA7) | (1 << art::loongarch64::FT0) |
    (1 << art::loongarch64::FT1) | (1 << art::loongarch64::FT2) | (1 << art::loongarch64::FT3) |
    (1 << art::loongarch64::FT4) | (1 << art::loongarch64::FT5) | (1 << art::loongarch64::FT6) |
    (1 << art::loongarch64::FT7) | (1 << art::loongarch64::FT8) | (1 << art::loongarch64::FT9) |
    (1 << art::loongarch64::FT10) | (1 << art::loongarch64::FT11) | (1 << art::loongarch64::FT12) |
    (1 << art::loongarch64::FT13) | (1 << art::loongarch64::FT14) | (1 << art::loongarch64::FT15) |
    (1 << art::loongarch64::FS0) | (1 << art::loongarch64::FS1) | (1 << art::loongarch64::FS2) |
    (1 << art::loongarch64::FS3) | (1 << art::loongarch64::FS4) | (1 << art::loongarch64::FS5) |
    (1 << art::loongarch64::FS6) | (1 << art::loongarch64::FS7);

class Loongarch64CalleeSaveFrame {
 public:
  static constexpr uint32_t GetCoreSpills(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return kLoongarch64CalleeSaveAlwaysSpills | kLoongarch64CalleeSaveRefSpills |
           (type == CalleeSaveType::kSaveRefsAndArgs ? kLoongarch64CalleeSaveArgSpills : 0) |
           (type == CalleeSaveType::kSaveAllCalleeSaves ? kLoongarch64CalleeSaveAllSpills : 0) |
           (type == CalleeSaveType::kSaveEverything ? kLoongarch64CalleeSaveEverythingSpills : 0);
  }

  static constexpr uint32_t GetFpSpills(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return kLoongarch64CalleeSaveFpSpills |
           (type == CalleeSaveType::kSaveRefsAndArgs ? kLoongarch64CalleeSaveFpArgSpills : 0) |
           (type == CalleeSaveType::kSaveAllCalleeSaves ? kLoongarch64CalleeSaveFpAllSpills : 0) |
           (type == CalleeSaveType::kSaveEverything ? kLoongarch64CalleeSaveFpEverythingSpills : 0);
  }

  static constexpr uint32_t GetFrameSize(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return RoundUp((POPCOUNT(GetCoreSpills(type)) /* gprs */ +
                    POPCOUNT(GetFpSpills(type)) /* fprs */ + 1 /* Method* */) *
                       static_cast<size_t>(kLoongarch64PointerSize),
                   kStackAlignment);
  }

  static constexpr QuickMethodFrameInfo GetMethodFrameInfo(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return QuickMethodFrameInfo(GetFrameSize(type), GetCoreSpills(type), GetFpSpills(type));
  }

  static constexpr size_t GetFpr1Offset(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return GetFrameSize(type) - (POPCOUNT(GetCoreSpills(type)) + POPCOUNT(GetFpSpills(type))) *
                                    static_cast<size_t>(kLoongarch64PointerSize);
  }

  static constexpr size_t GetGpr1Offset(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return GetFrameSize(type) -
           POPCOUNT(GetCoreSpills(type)) * static_cast<size_t>(kLoongarch64PointerSize);
  }

  static constexpr size_t GetReturnPcOffset(CalleeSaveType type) {
    type = GetCanonicalCalleeSaveType(type);
    return GetFrameSize(type) - static_cast<size_t>(kLoongarch64PointerSize);
  }
};

// Assembly entrypoints rely on these constants.
static_assert(Loongarch64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveRefsAndArgs) == 208);
static_assert(Loongarch64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveAllCalleeSaves) == 160);
static_assert(Loongarch64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveEverything) == 496);

}  // namespace loongarch64
}  // namespace art

#endif  // ART_RUNTIME_ARCH_LOONGARCH64_CALLEE_SAVE_FRAME_LOONGARCH64_H_
