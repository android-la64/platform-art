/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "calling_convention.h"

#include <android-base/logging.h>

#include "arch/instruction_set.h"
#include "indirect_reference_table.h"

#ifdef ART_ENABLE_CODEGEN_arm
#include "jni/quick/arm/calling_convention_arm.h"
#endif

#ifdef ART_ENABLE_CODEGEN_arm64
#include "jni/quick/arm64/calling_convention_arm64.h"
#endif

#ifdef ART_ENABLE_CODEGEN_x86
#include "jni/quick/x86/calling_convention_x86.h"
#endif

#ifdef ART_ENABLE_CODEGEN_x86_64
#include "jni/quick/x86_64/calling_convention_x86_64.h"
#endif

#ifdef ART_ENABLE_CODEGEN_loongarch64_unimp  // XC-TODO unimplmented 
#include "jni/quick/loongarch64/calling_convention_loongarch64.h"
#endif

namespace art {

// Managed runtime calling convention

std::unique_ptr<ManagedRuntimeCallingConvention> ManagedRuntimeCallingConvention::Create(
    ArenaAllocator* allocator,
    bool is_static,
    bool is_synchronized,
    const char* shorty,
    InstructionSet instruction_set) {
  switch (instruction_set) {
#ifdef ART_ENABLE_CODEGEN_arm
    case InstructionSet::kArm:
    case InstructionSet::kThumb2:
      return std::unique_ptr<ManagedRuntimeCallingConvention>(
          new (allocator) arm::ArmManagedRuntimeCallingConvention(
              is_static, is_synchronized, shorty));
#endif
#ifdef ART_ENABLE_CODEGEN_arm64
    case InstructionSet::kArm64:
      return std::unique_ptr<ManagedRuntimeCallingConvention>(
          new (allocator) arm64::Arm64ManagedRuntimeCallingConvention(
              is_static, is_synchronized, shorty));
#endif
#ifdef ART_ENABLE_CODEGEN_x86
    case InstructionSet::kX86:
      return std::unique_ptr<ManagedRuntimeCallingConvention>(
          new (allocator) x86::X86ManagedRuntimeCallingConvention(
              is_static, is_synchronized, shorty));
#endif
#ifdef ART_ENABLE_CODEGEN_x86_64
    case InstructionSet::kX86_64:
      return std::unique_ptr<ManagedRuntimeCallingConvention>(
          new (allocator) x86_64::X86_64ManagedRuntimeCallingConvention(
              is_static, is_synchronized, shorty));
#endif
#ifdef ART_ENABLE_CODEGEN_loongarch64_unimp   // XC-TODO unimplmented 
    case InstructionSet::kLoongarch64:
      return std::unique_ptr<ManagedRuntimeCallingConvention>(
          new (allocator) loongarch64::Loongarch64ManagedRuntimeCallingConvention(
              is_static, is_synchronized, shorty));
#endif
    default:
      UNUSED(allocator);
      UNUSED(is_static);
      UNUSED(is_synchronized);
      UNUSED(shorty);
      LOG(FATAL) << "Unknown InstructionSet: " << instruction_set;
      UNREACHABLE();
  }
}

bool ManagedRuntimeCallingConvention::HasNext() {
  return itr_args_ < NumArgs();
}

void ManagedRuntimeCallingConvention::Next() {
  CHECK(HasNext());
  if (IsCurrentArgExplicit() &&  // don't query parameter type of implicit args
      IsParamALongOrDouble(itr_args_)) {
    itr_longs_and_doubles_++;
    itr_slots_++;
  }
  if (IsParamAFloatOrDouble(itr_args_)) {
    itr_float_and_doubles_++;
  }
  if (IsCurrentParamAReference()) {
    itr_refs_++;
  }
  itr_args_++;
  itr_slots_++;
}

bool ManagedRuntimeCallingConvention::IsCurrentArgExplicit() {
  // Static methods have no implicit arguments, others implicitly pass this
  return IsStatic() || (itr_args_ != 0);
}

bool ManagedRuntimeCallingConvention::IsCurrentArgPossiblyNull() {
  return IsCurrentArgExplicit();  // any user parameter may be null
}

size_t ManagedRuntimeCallingConvention::CurrentParamSize() {
  return ParamSize(itr_args_);
}

bool ManagedRuntimeCallingConvention::IsCurrentParamAReference() {
  return IsParamAReference(itr_args_);
}

bool ManagedRuntimeCallingConvention::IsCurrentParamAFloatOrDouble() {
  return IsParamAFloatOrDouble(itr_args_);
}

bool ManagedRuntimeCallingConvention::IsCurrentParamADouble() {
  return IsParamADouble(itr_args_);
}

bool ManagedRuntimeCallingConvention::IsCurrentParamALong() {
  return IsParamALong(itr_args_);
}

// JNI calling convention

std::unique_ptr<JniCallingConvention> JniCallingConvention::Create(ArenaAllocator* allocator,
                                                                   bool is_static,
                                                                   bool is_synchronized,
                                                                   bool is_critical_native,
                                                                   const char* shorty,
                                                                   InstructionSet instruction_set) {
  switch (instruction_set) {
#ifdef ART_ENABLE_CODEGEN_arm
    case InstructionSet::kArm:
    case InstructionSet::kThumb2:
      return std::unique_ptr<JniCallingConvention>(
          new (allocator) arm::ArmJniCallingConvention(
              is_static, is_synchronized, is_critical_native, shorty));
#endif
#ifdef ART_ENABLE_CODEGEN_arm64
    case InstructionSet::kArm64:
      return std::unique_ptr<JniCallingConvention>(
          new (allocator) arm64::Arm64JniCallingConvention(
              is_static, is_synchronized, is_critical_native, shorty));
#endif
#ifdef ART_ENABLE_CODEGEN_x86
    case InstructionSet::kX86:
      return std::unique_ptr<JniCallingConvention>(
          new (allocator) x86::X86JniCallingConvention(
              is_static, is_synchronized, is_critical_native, shorty));
#endif
#ifdef ART_ENABLE_CODEGEN_x86_64
    case InstructionSet::kX86_64:
      return std::unique_ptr<JniCallingConvention>(
          new (allocator) x86_64::X86_64JniCallingConvention(
              is_static, is_synchronized, is_critical_native, shorty));
#endif
#ifdef ART_ENABLE_CODEGEN_loongarch64_unimp   // XC-TODO unimplmented
    case InstructionSet::kLoongarch64:
      return std::unique_ptr<JniCallingConvention>(
          new (allocator) loongarch64::Loongarch64JniCallingConvention(
              is_static, is_synchronized, is_critical_native, shorty));
#endif
    default:
      UNUSED(allocator);
      UNUSED(is_static);
      UNUSED(is_synchronized);
      UNUSED(is_critical_native);
      UNUSED(shorty);
      LOG(FATAL) << "Unknown InstructionSet: " << instruction_set;
      UNREACHABLE();
  }
}

size_t JniCallingConvention::ReferenceCount() const {
  return NumReferenceArgs() + (IsStatic() ? 1 : 0);
}

FrameOffset JniCallingConvention::ReturnValueSaveLocation() const {
  // The saved return value goes at a properly aligned slot after the method pointer.
  DCHECK(SpillsReturnValue());
  size_t return_value_offset = static_cast<size_t>(frame_pointer_size_);
  const size_t return_value_size = SizeOfReturnValue();
  DCHECK(return_value_size == 4u || return_value_size == 8u) << return_value_size;
  DCHECK_ALIGNED(return_value_offset, 4u);
  if (return_value_size == 8u) {
    return_value_offset = RoundUp(return_value_offset, 8u);
  }
  return FrameOffset(displacement_.SizeValue() + return_value_offset);
}

bool JniCallingConvention::HasNext() {
  if (IsCurrentArgExtraForJni()) {
    return true;
  } else {
    unsigned int arg_pos = GetIteratorPositionWithinShorty();
    return arg_pos < NumArgs();
  }
}

void JniCallingConvention::Next() {
  CHECK(HasNext());
  if (IsCurrentParamALong() || IsCurrentParamADouble()) {
    itr_longs_and_doubles_++;
    itr_slots_++;
  }
  if (IsCurrentParamAFloatOrDouble()) {
    itr_float_and_doubles_++;
  }
  if (IsCurrentParamAReference()) {
    itr_refs_++;
  }
  // This default/fallthrough case also covers the extra JNIEnv* argument,
  // as well as any other single-slot primitives.
  itr_args_++;
  itr_slots_++;
}

bool JniCallingConvention::IsCurrentParamAReference() {
  bool return_value;
  if (SwitchExtraJniArguments(itr_args_,
                              false,  // JNIEnv*
                              true,   // jobject or jclass
                              /* out parameters */
                              &return_value)) {
    return return_value;
  } else {
    int arg_pos = GetIteratorPositionWithinShorty();
    return IsParamAReference(arg_pos);
  }
}


bool JniCallingConvention::IsCurrentParamJniEnv() {
  if (UNLIKELY(!HasJniEnv())) {
    return false;
  }
  return (itr_args_ == kJniEnv);
}

bool JniCallingConvention::IsCurrentParamAFloatOrDouble() {
  bool return_value;
  if (SwitchExtraJniArguments(itr_args_,
                              false,  // jnienv*
                              false,  // jobject or jclass
                              /* out parameters */
                              &return_value)) {
    return return_value;
  } else {
    int arg_pos = GetIteratorPositionWithinShorty();
    return IsParamAFloatOrDouble(arg_pos);
  }
}

bool JniCallingConvention::IsCurrentParamADouble() {
  bool return_value;
  if (SwitchExtraJniArguments(itr_args_,
                              false,  // jnienv*
                              false,  // jobject or jclass
                              /* out parameters */
                              &return_value)) {
    return return_value;
  } else {
    int arg_pos = GetIteratorPositionWithinShorty();
    return IsParamADouble(arg_pos);
  }
}

bool JniCallingConvention::IsCurrentParamALong() {
  bool return_value;
  if (SwitchExtraJniArguments(itr_args_,
                              false,  // jnienv*
                              false,  // jobject or jclass
                              /* out parameters */
                              &return_value)) {
    return return_value;
  } else {
    int arg_pos = GetIteratorPositionWithinShorty();
    return IsParamALong(arg_pos);
  }
}

size_t JniCallingConvention::CurrentParamSize() const {
  if (IsCurrentArgExtraForJni()) {
    return static_cast<size_t>(frame_pointer_size_);  // JNIEnv or jobject/jclass
  } else {
    int arg_pos = GetIteratorPositionWithinShorty();
    return ParamSize(arg_pos);
  }
}

size_t JniCallingConvention::NumberOfExtraArgumentsForJni() const {
  if (LIKELY(HasExtraArgumentsForJni())) {
    // The first argument is the JNIEnv*.
    // Static methods have an extra argument which is the jclass.
    return IsStatic() ? 2 : 1;
  } else {
    // Critical natives exclude the JNIEnv and the jclass/this parameters.
    return 0;
  }
}

bool JniCallingConvention::HasSelfClass() const {
  if (!IsStatic()) {
    // Virtual functions: There is never an implicit jclass parameter.
    return false;
  } else {
    // Static functions: There is an implicit jclass parameter unless it's @CriticalNative.
    return HasExtraArgumentsForJni();
  }
}

unsigned int JniCallingConvention::GetIteratorPositionWithinShorty() const {
  // We need to subtract out the extra JNI arguments if we want to use this iterator position
  // with the inherited CallingConvention member functions, which rely on scanning the shorty.
  // Note that our shorty does *not* include the JNIEnv, jclass/jobject parameters.
  DCHECK_GE(itr_args_, NumberOfExtraArgumentsForJni());
  return itr_args_ - NumberOfExtraArgumentsForJni();
}

bool JniCallingConvention::IsCurrentArgExtraForJni() const {
  if (UNLIKELY(!HasExtraArgumentsForJni())) {
    return false;  // If there are no extra args, we can never be an extra.
  }
  // Only parameters kJniEnv and kObjectOrClass are considered extra.
  return itr_args_ <= kObjectOrClass;
}

bool JniCallingConvention::SwitchExtraJniArguments(size_t switch_value,
                                                   bool case_jni_env,
                                                   bool case_object_or_class,
                                                   /* out parameters */
                                                   bool* return_value) const {
  DCHECK(return_value != nullptr);
  if (UNLIKELY(!HasExtraArgumentsForJni())) {
    return false;
  }

  switch (switch_value) {
    case kJniEnv:
      *return_value = case_jni_env;
      return true;
    case kObjectOrClass:
      *return_value = case_object_or_class;
      return true;
    default:
      return false;
  }
}


}  // namespace art
