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

#include <sys/ucontext.h>

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

// LOONGARCH64 specific fault handler functions (or stubs if unimplemented yet).

namespace art {

uintptr_t FaultManager::GetFaultPc(siginfo_t*, void* context) {
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(context);
  mcontext_t* mc = reinterpret_cast<mcontext_t*>(&uc->uc_mcontext);
  if (mc->sc_regs[LARCH_REG_SP] == 0) {
    VLOG(signals) << "Missing SP";
    return 0u;
  }
  return mc->sc_pc;
}

uintptr_t FaultManager::GetFaultSp(void* context) {
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(context);
  mcontext_t* mc = reinterpret_cast<mcontext_t*>(&uc->uc_mcontext);
  return mc->sc_regs[LARCH_REG_SP];
}


bool NullPointerHandler::Action(int sig ATTRIBUTE_UNUSED, siginfo_t* info, void* context) {
  uintptr_t fault_address = reinterpret_cast<uintptr_t>(info->si_addr);
  if (!IsValidFaultAddress(fault_address)) {
    return false;
  }

  ucontext_t* uc = reinterpret_cast<ucontext_t*>(context);
  mcontext_t* mc = reinterpret_cast<mcontext_t*>(&uc->uc_mcontext);
  ArtMethod** sp = reinterpret_cast<ArtMethod**>(mc->sc_regs[LARCH_REG_SP]);
  if (!IsValidMethod(*sp)) {
    return false;
  }
  // For null checks in compiled code we insert a stack map that is immediately
  // after the load/store instruction that might cause the fault and we need to
  // pass the return PC to the handler. For null checks in Nterp, we similarly
  // need the return PC to recognize that this was a null check in Nterp, so
  // that the handler can get the needed data from the Nterp frame.

  // Need to work out the size of the instruction that caused the exception.
  uintptr_t old_pc = mc->sc_pc;
  uintptr_t instr_size = (reinterpret_cast<uint16_t*>(old_pc)[0] & 3u) == 3u ? 4u : 2u;
  uintptr_t return_pc = old_pc + instr_size;
  if (!IsValidReturnPc(sp, return_pc)) {
    return false;
  }

  // Decrement $sp by the frame size of the kSaveEverything method and store
  // the fault address in the padding right after the ArtMethod*.
  mc->sc_regs[LARCH_REG_SP] -= sizeof(uintptr_t);
  *reinterpret_cast<uintptr_t*>(mc->sc_regs[LARCH_REG_SP]) = return_pc;

  mc->sc_regs[LARCH_REG_RA] = fault_address;
  mc->sc_pc = reinterpret_cast<uintptr_t>(art_quick_throw_null_pointer_exception_from_signal);
  VLOG(signals) << "Generating null pointer exception";
  return true;
}

bool SuspensionHandler::Action(int sig ATTRIBUTE_UNUSED, siginfo_t* info ATTRIBUTE_UNUSED,
                               void* context ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "SuspensionHandler::Action is not implemented for LOONGARCH";
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

  // The kernel will now return to the address in sc->arm_pc.
  return true;
}
}       // namespace art
