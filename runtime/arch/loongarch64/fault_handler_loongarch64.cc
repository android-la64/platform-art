/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "fault_handler.h"

#include <asm/ucontext.h>

#include "arch/instruction_set.h"
#include "arch/loongarch64/callee_save_frame_loongarch64.h"
#include "art_method.h"
#include "base/callee_save_type.h"
#include "base/hex_dump.h"
#include "base/logging.h"  // For VLOG.
#include "base/macros.h"
#include "registers_loongarch64.h"
#include "runtime_globals.h"
#include "thread-current-inl.h"

extern "C" void art_quick_throw_stack_overflow();
extern "C" void art_quick_throw_null_pointer_exception_from_signal();

//
// Loongarch64 specific fault handler functions.
//

#ifndef LARCH_REG_T6
#define LARCH_REG_T6    18
#endif

namespace art {

void FaultManager::GetMethodAndReturnPcAndSp(siginfo_t* siginfo, void* context,
                                             ArtMethod** out_method,
                                             uintptr_t* out_return_pc,
                                             uintptr_t* out_sp,
                                             bool* out_is_stack_overflow) {
  struct ucontext* uc = reinterpret_cast<struct ucontext*>(context);
  struct sigcontext *sc = reinterpret_cast<struct sigcontext*>(&uc->uc_mcontext);
  *out_sp = static_cast<uintptr_t>(sc->sc_regs[LARCH_REG_SP]);
  VLOG(signals) << "sp: " << *out_sp;
  if (*out_sp == 0) {
    return;
  }

  // In the case of a stack overflow, the stack is not valid and we can't
  // get the method from the top of the stack.  However it's in r0.
  uintptr_t* fault_addr = reinterpret_cast<uintptr_t*>(siginfo->si_addr);  // BVA addr
  uintptr_t* overflow_addr = reinterpret_cast<uintptr_t*>(
      reinterpret_cast<uint8_t*>(*out_sp) - GetStackOverflowReservedBytes(InstructionSet::kLoongarch64));
  if (overflow_addr == fault_addr) {
    *out_method = reinterpret_cast<ArtMethod*>(sc->sc_regs[LARCH_REG_A0]);
    *out_is_stack_overflow = true;
  } else {
    // The method is at the top of the stack.
    *out_method = *reinterpret_cast<ArtMethod**>(*out_sp);
    *out_is_stack_overflow = false;
  }

  // Work out the return PC.  This will be the address of the instruction
  // following the faulting ldr/str instruction.

  VLOG(signals) << "pc: " << std::hex
      << static_cast<void*>(reinterpret_cast<uint8_t*>(sc->sc_pc));

  *out_return_pc = sc->sc_pc + 4;
}

bool NullPointerHandler::Action(int sig ATTRIBUTE_UNUSED, siginfo_t* info, void* context) {
  // Same as arm64 implementation here.
  if (!IsValidImplicitCheck(info)) {
    return false;
  }
  // The code that looks for the catch location needs to know the value of the
  // PC at the point of call.  For Null checks we insert a GC map that is immediately after
  // the load/store instruction that might cause the fault.

  struct ucontext *uc = reinterpret_cast<struct ucontext*>(context);
  struct sigcontext *sc = reinterpret_cast<struct sigcontext*>(&uc->uc_mcontext);

  // Decrement $sp by the frame size of the kSaveEverything method and store
  // the fault address in the padding right after the ArtMethod*.
  assert(loongarch64::Loongarch64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveEverything) == 496);
  sc->sc_regs[LARCH_REG_SP] -= loongarch64::Loongarch64CalleeSaveFrame::GetFrameSize(CalleeSaveType::kSaveEverything);
  uintptr_t* padding = reinterpret_cast<uintptr_t*>(sc->sc_regs[LARCH_REG_SP]) + /* ArtMethod* */ 1;
  *padding = reinterpret_cast<uintptr_t>(info->si_addr);

  sc->sc_regs[LARCH_REG_RA] = sc->sc_pc + 4;      // RA needs to point to gc map location
  sc->sc_pc = reinterpret_cast<uintptr_t>(art_quick_throw_null_pointer_exception_from_signal);
  VLOG(signals) << "Generating null pointer exception";
  return true;
}

bool SuspensionHandler::Action(int sig ATTRIBUTE_UNUSED, siginfo_t* info ATTRIBUTE_UNUSED,
                               void* context ATTRIBUTE_UNUSED) {
  return false;
}

// Stack overflow fault handler.
//
// This checks that the fault address is equal to the current stack pointer
// minus the overflow region size (16K typically). The instruction that
// generates this signal is:
//
// lw zero, -16384(sp)
//
// It will fault if sp is inside the protected region on the stack.
//
// If we determine this is a stack overflow we need to move the stack pointer
// to the overflow region below the protected region.

bool StackOverflowHandler::Action(int sig ATTRIBUTE_UNUSED, siginfo_t* info, void* context) {
  struct ucontext* uc = reinterpret_cast<struct ucontext*>(context);
  struct sigcontext *sc = reinterpret_cast<struct sigcontext*>(&uc->uc_mcontext);
  VLOG(signals) << "stack overflow handler with sp at " << std::hex << &uc;
  VLOG(signals) << "sigcontext: " << std::hex << sc;

  uintptr_t sp = sc->sc_regs[LARCH_REG_SP];
  VLOG(signals) << "sp: " << std::hex << sp;

  uintptr_t fault_addr = reinterpret_cast<uintptr_t>(info->si_addr);  // BVA addr
  VLOG(signals) << "fault_addr: " << std::hex << fault_addr;
  VLOG(signals) << "checking for stack overflow, sp: " << std::hex << sp <<
    ", fault_addr: " << fault_addr;

  uintptr_t overflow_addr = sp - GetStackOverflowReservedBytes(InstructionSet::kLoongarch64);

  // Check that the fault address is the value expected for a stack overflow.
  if (fault_addr != overflow_addr) {
    VLOG(signals) << "Not a stack overflow";
    return false;
  }

  VLOG(signals) << "Stack overflow found";

  // Now arrange for the signal handler to return to art_quick_throw_stack_overflow_from.
  // The value of RA must be the same as it was when we entered the code that
  // caused this fault.  This will be inserted into a callee save frame by
  // the function to which this handler returns (art_quick_throw_stack_overflow).
  sc->sc_pc = reinterpret_cast<uintptr_t>(art_quick_throw_stack_overflow);
  sc->sc_regs[LARCH_REG_T6] = sc->sc_pc;          // make sure T6 points to the function

  // The kernel will now return to the address in sc->arm_pc.
  return true;
}
}       // namespace art
