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

#include "entrypoints/quick/quick_default_init_entrypoints.h"
#include "entrypoints/quick/quick_entrypoints.h"

namespace art {


// Cast entrypoints.
extern "C" size_t artInstanceOfFromCode(mirror::Object* obj, mirror::Class* ref_class);

// art_quick_read_barrier_mark_regX uses an non-standard calling convention: it
// expects its input in register X and returns its result in that same register,
// and saves and restores all other registers.
extern "C" mirror::Object* art_quick_read_barrier_mark_reg10(mirror::Object*);  // a0/r4

void UpdateReadBarrierEntrypoints(QuickEntryPoints* qpoints, bool is_active) {
  // TODO(loongarch64): add read barrier entrypoints
  qpoints->SetReadBarrierMarkReg10(is_active ? art_quick_read_barrier_mark_reg10 : nullptr);
 }

void InitEntryPoints(JniEntryPoints* jpoints,
                     QuickEntryPoints* qpoints,
                     bool monitor_jni_entry_exit) {
  DefaultInitEntryPoints(jpoints, qpoints, monitor_jni_entry_exit);

  // Cast
  qpoints->SetInstanceofNonTrivial(artInstanceOfFromCode);
  qpoints->SetCheckInstanceOf(art_quick_check_instance_of);

  // Math
  // TODO(loongarch64): null entrypoints not needed for loongarch64 - using generated code.
  qpoints->SetCmpgDouble(nullptr);
  qpoints->SetCmpgFloat(nullptr);
  qpoints->SetCmplDouble(nullptr);
  qpoints->SetCmplFloat(nullptr);
  qpoints->SetFmod(fmod);
  qpoints->SetL2d(nullptr);
  qpoints->SetFmodf(fmodf);
  qpoints->SetL2f(nullptr);
  qpoints->SetD2iz(nullptr);
  qpoints->SetF2iz(nullptr);
  qpoints->SetIdivmod(nullptr);
  qpoints->SetD2l(nullptr);
  qpoints->SetF2l(nullptr);
  qpoints->SetLdiv(nullptr);
  qpoints->SetLmod(nullptr);
  qpoints->SetLmul(nullptr);
  qpoints->SetShlLong(nullptr);
  qpoints->SetShrLong(nullptr);
  qpoints->SetUshrLong(nullptr);
  // TODO(loongarch64): add other entrypoints
}

}  // namespace art
