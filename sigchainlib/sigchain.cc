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

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__BIONIC__)
#include <bionic/macros.h>
#endif

#include <algorithm>
#include <initializer_list>
#include <mutex>
#include <type_traits>
#include <utility>

#include "log.h"
#include "sigchain.h"

#if defined(__APPLE__)
#define _NSIG NSIG
#define sighandler_t sig_t

// Darwin has an #error when ucontext.h is included without _XOPEN_SOURCE defined.
#define _XOPEN_SOURCE
#endif

#define SA_UNSUPPORTED 0x00000400
#define SA_EXPOSE_TAGBITS 0x00000800

#include <ucontext.h>

// libsigchain provides an interception layer for signal handlers, to allow ART and others to give
// their signal handlers the first stab at handling signals before passing them on to user code.
//
// It implements wrapper functions for signal, sigaction, and sigprocmask, and a handler that
// forwards signals appropriately.
//
// In our handler, we start off with all signals blocked, fetch the original signal mask from the
// passed in ucontext, and then adjust our signal mask appropriately for the user handler.
//
// It's somewhat tricky for us to properly handle some flag cases:
//   SA_NOCLDSTOP and SA_NOCLDWAIT: shouldn't matter, we don't have special handlers for SIGCHLD.
//   SA_NODEFER: unimplemented, we can manually change the signal mask appropriately.
//  ~SA_ONSTACK: always silently enable this
//   SA_RESETHAND: unimplemented, but we can probably do this?
//  ~SA_RESTART: unimplemented, maybe we can reserve an RT signal, register an empty handler that
//               doesn't have SA_RESTART, and raise the signal to avoid restarting syscalls that are
//               expected to be interrupted?

#if defined(__BIONIC__) && !defined(__LP64__)
static int sigismember(const sigset64_t* sigset, int signum) {
  return sigismember64(sigset, signum);
}

static int sigemptyset(sigset64_t* sigset) {
  return sigemptyset64(sigset);
}

static int sigaddset(sigset64_t* sigset, int signum) {
  return sigaddset64(sigset, signum);
}

static int sigdelset(sigset64_t* sigset, int signum) {
  return sigdelset64(sigset, signum);
}
#endif

template<typename SigsetType>
static int sigorset(SigsetType* dest, SigsetType* left, SigsetType* right) {
  sigemptyset(dest);
  for (size_t i = 0; i < sizeof(SigsetType) * CHAR_BIT; ++i) {
    if (sigismember(left, i) == 1 || sigismember(right, i) == 1) {
      sigaddset(dest, i);
    }
  }
  return 0;
}

namespace art {

static decltype(&sigaction) linked_sigaction;
static decltype(&sigprocmask) linked_sigprocmask;

#if defined(__BIONIC__)
static decltype(&sigaction64) linked_sigaction64;
static decltype(&sigprocmask64) linked_sigprocmask64;
#endif

template <typename T>
static void lookup_libc_symbol(T* output, T wrapper, const char* name) {
#if defined(__BIONIC__)
  constexpr const char* libc_name = "libc.so";
#elif defined(__GLIBC__)
#if __GNU_LIBRARY__ != 6
#error unsupported glibc version
#endif
  constexpr const char* libc_name = "libc.so.6";
#else
#error unsupported libc: not bionic or glibc?
#endif

  static void* libc = []() {
    void* result = dlopen(libc_name, RTLD_LOCAL | RTLD_LAZY);
    if (!result) {
      fatal("failed to dlopen %s: %s", libc_name, dlerror());
    }
    return result;
  }();

  void* sym = dlsym(libc, name);  // NOLINT glibc triggers cert-dcl16-c with RTLD_NEXT.
  if (sym == nullptr) {
    sym = dlsym(RTLD_DEFAULT, name);
    if (sym == wrapper || sym == sigaction) {
      fatal("Unable to find next %s in signal chain", name);
    }
  }
  *output = reinterpret_cast<T>(sym);
}

__attribute__((constructor)) static void InitializeSignalChain() {
  static std::once_flag once;
  std::call_once(once, []() {
    lookup_libc_symbol(&linked_sigaction, sigaction, "sigaction");
    lookup_libc_symbol(&linked_sigprocmask, sigprocmask, "sigprocmask");

#if defined(__BIONIC__)
    lookup_libc_symbol(&linked_sigaction64, sigaction64, "sigaction64");
    lookup_libc_symbol(&linked_sigprocmask64, sigprocmask64, "sigprocmask64");
#endif
  });
}

static pthread_key_t GetHandlingSignalKey() {
  static pthread_key_t key;
  static std::once_flag once;
  std::call_once(once, []() {
    int rc = pthread_key_create(&key, nullptr);
    if (rc != 0) {
      fatal("failed to create sigchain pthread key: %s", strerror(rc));
    }
  });
  return key;
}

static bool GetHandlingSignal() {
  void* result = pthread_getspecific(GetHandlingSignalKey());
  return reinterpret_cast<uintptr_t>(result);
}

static void SetHandlingSignal(bool value) {
  pthread_setspecific(GetHandlingSignalKey(),
                      reinterpret_cast<void*>(static_cast<uintptr_t>(value)));
}

class ScopedHandlingSignal {
 public:
  ScopedHandlingSignal() : original_value_(GetHandlingSignal()) {
  }

  ~ScopedHandlingSignal() {
    SetHandlingSignal(original_value_);
  }

 private:
  bool original_value_;
};

class SignalChain {
 public:
  SignalChain() : claimed_(false) {
  }

  bool IsClaimed() {
    return claimed_;
  }

  void Claim(int signo) {
    if (!claimed_) {
      Register(signo);
      claimed_ = true;
    }
  }

  // Register the signal chain with the kernel if needed.
  void Register(int signo) {
#if defined(__BIONIC__)
    struct sigaction64 handler_action = {};
    sigfillset64(&handler_action.sa_mask);
#else
    struct sigaction handler_action = {};
    sigfillset(&handler_action.sa_mask);
#endif

    handler_action.sa_sigaction = SignalChain::Handler;
    handler_action.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONSTACK |
                              SA_UNSUPPORTED | SA_EXPOSE_TAGBITS;

#if defined(__BIONIC__)
    linked_sigaction64(signo, &handler_action, &action_);
    linked_sigaction64(signo, nullptr, &handler_action);
#else
    linked_sigaction(signo, &handler_action, &action_);
    linked_sigaction(signo, nullptr, &handler_action);
#endif

    // Newer kernels clear unknown flags from sigaction.sa_flags in order to
    // allow userspace to determine which flag bits are supported. We use this
    // behavior in turn to implement the same flag bit support detection
    // protocol regardless of kernel version. Due to the lack of a flag bit
    // support detection protocol in older kernels we assume support for a base
    // set of flags that have been supported since at least 2003 [1]. No flags
    // were introduced since then until the introduction of SA_EXPOSE_TAGBITS
    // handled below. glibc headers do not define SA_RESTORER so we define it
    // ourselves.
    //
    // TODO(pcc): The new kernel behavior has been implemented in a kernel
    // patch [2] that has not yet landed. Update the code if necessary once it
    // lands.
    //
    // [1] https://github.com/mpe/linux-fullhistory/commit/c0f806c86fc8b07ad426df023f1a4bb0e53c64f6
    // [2] https://lore.kernel.org/linux-arm-kernel/cover.1605235762.git.pcc@google.com/
#if !defined(__BIONIC__)
#define SA_RESTORER 0x04000000
#endif
    kernel_supported_flags_ = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_SIGINFO |
                              SA_ONSTACK | SA_RESTART | SA_NODEFER |
                              SA_RESETHAND;
    // XC-TODO: loongarch dont have SA_RESTORER defined

    // Determine whether the kernel supports SA_EXPOSE_TAGBITS. For newer
    // kernels we use the flag support detection protocol described above. In
    // order to allow userspace to distinguish old and new kernels,
    // SA_UNSUPPORTED has been reserved as an unsupported flag. If the kernel
    // did not clear it then we know that we have an old kernel that would not
    // support SA_EXPOSE_TAGBITS anyway.
    if (!(handler_action.sa_flags & SA_UNSUPPORTED) &&
        (handler_action.sa_flags & SA_EXPOSE_TAGBITS)) {
      kernel_supported_flags_ |= SA_EXPOSE_TAGBITS;
    }
  }

  template <typename SigactionType>
  SigactionType GetAction() {
    if constexpr (std::is_same_v<decltype(action_), SigactionType>) {
      return action_;
    } else {
      SigactionType result;
      result.sa_flags = action_.sa_flags;
      result.sa_handler = action_.sa_handler;
#if defined(SA_RESTORER)
      result.sa_restorer = action_.sa_restorer;
#endif
      memcpy(&result.sa_mask, &action_.sa_mask,
             std::min(sizeof(action_.sa_mask), sizeof(result.sa_mask)));
      return result;
    }
  }

  template <typename SigactionType>
  void SetAction(const SigactionType* new_action) {
    if constexpr (std::is_same_v<decltype(action_), SigactionType>) {
      action_ = *new_action;
    } else {
      action_.sa_flags = new_action->sa_flags;
      action_.sa_handler = new_action->sa_handler;
#if defined(SA_RESTORER)
      action_.sa_restorer = new_action->sa_restorer;
#endif
      sigemptyset(&action_.sa_mask);
      memcpy(&action_.sa_mask, &new_action->sa_mask,
             std::min(sizeof(action_.sa_mask), sizeof(new_action->sa_mask)));
    }
    action_.sa_flags &= kernel_supported_flags_;
  }

  void AddSpecialHandler(SigchainAction* sa) {
    for (SigchainAction& slot : special_handlers_) {
      if (slot.sc_sigaction == nullptr) {
        slot = *sa;
        return;
      }
    }

    fatal("too many special signal handlers");
  }

  void RemoveSpecialHandler(bool (*fn)(int, siginfo_t*, void*)) {
    // This isn't thread safe, but it's unlikely to be a real problem.
    size_t len = sizeof(special_handlers_)/sizeof(*special_handlers_);
    for (size_t i = 0; i < len; ++i) {
      if (special_handlers_[i].sc_sigaction == fn) {
        for (size_t j = i; j < len - 1; ++j) {
          special_handlers_[j] = special_handlers_[j + 1];
        }
        special_handlers_[len - 1].sc_sigaction = nullptr;
        return;
      }
    }

    fatal("failed to find special handler to remove");
  }


  static void Handler(int signo, siginfo_t* siginfo, void*);

 private:
  bool claimed_;
  int kernel_supported_flags_;
#if defined(__BIONIC__)
  struct sigaction64 action_;
#else
  struct sigaction action_;
#endif
  SigchainAction special_handlers_[2];
};

// _NSIG is 1 greater than the highest valued signal, but signals start from 1.
// Leave an empty element at index 0 for convenience.
static SignalChain chains[_NSIG + 1];

static bool is_signal_hook_debuggable = false;

void SignalChain::Handler(int signo, siginfo_t* siginfo, void* ucontext_raw) {
  // Try the special handlers first.
  // If one of them crashes, we'll reenter this handler and pass that crash onto the user handler.
  if (!GetHandlingSignal()) {
    for (const auto& handler : chains[signo].special_handlers_) {
      if (handler.sc_sigaction == nullptr) {
        break;
      }

      // The native bridge signal handler might not return.
      // Avoid setting the thread local flag in this case, since we'll never
      // get a chance to restore it.
      bool handler_noreturn = (handler.sc_flags & SIGCHAIN_ALLOW_NORETURN);
      sigset_t previous_mask;
      linked_sigprocmask(SIG_SETMASK, &handler.sc_mask, &previous_mask);

      ScopedHandlingSignal restorer;
      if (!handler_noreturn) {
        SetHandlingSignal(true);
      }

      if (handler.sc_sigaction(signo, siginfo, ucontext_raw)) {
        return;
      }

      linked_sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
    }
  }

  // Forward to the user's signal handler.
  int handler_flags = chains[signo].action_.sa_flags;
  ucontext_t* ucontext = static_cast<ucontext_t*>(ucontext_raw);
#if defined(__BIONIC__)
  sigset64_t mask;
  sigorset(&mask, &ucontext->uc_sigmask64, &chains[signo].action_.sa_mask);
#else
  sigset_t mask;
  sigorset(&mask, &ucontext->uc_sigmask, &chains[signo].action_.sa_mask);
#endif
  if (!(handler_flags & SA_NODEFER)) {
    sigaddset(&mask, signo);
  }

#if defined(__BIONIC__)
  linked_sigprocmask64(SIG_SETMASK, &mask, nullptr);
#else
  linked_sigprocmask(SIG_SETMASK, &mask, nullptr);
#endif

  if ((handler_flags & SA_SIGINFO)) {
    // If the chained handler is not expecting tag bits in the fault address,
    // mask them out now.
#if defined(__BIONIC__)
    if (!(handler_flags & SA_EXPOSE_TAGBITS) &&
        (signo == SIGILL || signo == SIGFPE || signo == SIGSEGV ||
         signo == SIGBUS || signo == SIGTRAP) &&
        siginfo->si_code > SI_USER && siginfo->si_code < SI_KERNEL &&
        !(signo == SIGTRAP && siginfo->si_code == TRAP_HWBKPT)) {
      siginfo->si_addr = untag_address(siginfo->si_addr);
    }
#endif
    chains[signo].action_.sa_sigaction(signo, siginfo, ucontext_raw);
  } else {
    auto handler = chains[signo].action_.sa_handler;
    if (handler == SIG_IGN) {
      return;
    } else if (handler == SIG_DFL) {
      fatal("exiting due to SIG_DFL handler for signal %d", signo);
    } else {
      handler(signo);
    }
  }
}

template <typename SigactionType>
static int __sigaction(int signal, const SigactionType* new_action,
                       SigactionType* old_action,
                       int (*linked)(int, const SigactionType*,
                                     SigactionType*)) {
  if (is_signal_hook_debuggable) {
    return 0;
  }

  // If this signal has been claimed as a signal chain, record the user's
  // action but don't pass it on to the kernel.
  // Note that we check that the signal number is in range here.  An out of range signal
  // number should behave exactly as the libc sigaction.
  if (signal <= 0 || signal >= _NSIG) {
    errno = EINVAL;
    return -1;
  }

  if (chains[signal].IsClaimed()) {
    SigactionType saved_action = chains[signal].GetAction<SigactionType>();
    if (new_action != nullptr) {
      chains[signal].SetAction(new_action);
    }
    if (old_action != nullptr) {
      *old_action = saved_action;
    }
    return 0;
  }

  // Will only get here if the signal chain has not been claimed.  We want
  // to pass the sigaction on to the kernel via the real sigaction in libc.
  return linked(signal, new_action, old_action);
}

extern "C" int sigaction(int signal, const struct sigaction* new_action,
                         struct sigaction* old_action) {
  InitializeSignalChain();
  return __sigaction(signal, new_action, old_action, linked_sigaction);
}

#if defined(__BIONIC__)
extern "C" int sigaction64(int signal, const struct sigaction64* new_action,
                           struct sigaction64* old_action) {
  InitializeSignalChain();
  return __sigaction(signal, new_action, old_action, linked_sigaction64);
}
#endif

extern "C" sighandler_t signal(int signo, sighandler_t handler) {
  InitializeSignalChain();

  if (signo <= 0 || signo >= _NSIG) {
    errno = EINVAL;
    return SIG_ERR;
  }

  struct sigaction sa = {};
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = handler;
  sa.sa_flags = SA_RESTART | SA_ONSTACK;
  sighandler_t oldhandler;

  // If this signal has been claimed as a signal chain, record the user's
  // action but don't pass it on to the kernel.
  if (chains[signo].IsClaimed()) {
    oldhandler = reinterpret_cast<sighandler_t>(
        chains[signo].GetAction<struct sigaction>().sa_handler);
    chains[signo].SetAction(&sa);
    return oldhandler;
  }

  // Will only get here if the signal chain has not been claimed.  We want
  // to pass the sigaction on to the kernel via the real sigaction in libc.
  if (linked_sigaction(signo, &sa, &sa) == -1) {
    return SIG_ERR;
  }

  return reinterpret_cast<sighandler_t>(sa.sa_handler);
}

#if !defined(__LP64__)
extern "C" sighandler_t bsd_signal(int signo, sighandler_t handler) {
  InitializeSignalChain();

  return signal(signo, handler);
}
#endif

template <typename SigsetType>
int __sigprocmask(int how, const SigsetType* new_set, SigsetType* old_set,
                  int (*linked)(int, const SigsetType*, SigsetType*)) {
  // When inside a signal handler, forward directly to the actual sigprocmask.
  if (GetHandlingSignal()) {
    return linked(how, new_set, old_set);
  }

  const SigsetType* new_set_ptr = new_set;
  SigsetType tmpset;
  if (new_set != nullptr) {
    tmpset = *new_set;

    if (how == SIG_BLOCK || how == SIG_SETMASK) {
      // Don't allow claimed signals in the mask.  If a signal chain has been claimed
      // we can't allow the user to block that signal.
      for (int i = 1; i < _NSIG; ++i) {
        if (chains[i].IsClaimed() && sigismember(&tmpset, i)) {
          sigdelset(&tmpset, i);
        }
      }
    }
    new_set_ptr = &tmpset;
  }

  return linked(how, new_set_ptr, old_set);
}

extern "C" int sigprocmask(int how, const sigset_t* new_set,
                           sigset_t* old_set) {
  InitializeSignalChain();
  return __sigprocmask(how, new_set, old_set, linked_sigprocmask);
}

#if defined(__BIONIC__)
extern "C" int sigprocmask64(int how, const sigset64_t* new_set,
                             sigset64_t* old_set) {
  InitializeSignalChain();
  return __sigprocmask(how, new_set, old_set, linked_sigprocmask64);
}
#endif

extern "C" void AddSpecialSignalHandlerFn(int signal, SigchainAction* sa) {
  InitializeSignalChain();

  if (signal <= 0 || signal >= _NSIG) {
    fatal("Invalid signal %d", signal);
  }

  // Set the managed_handler.
  chains[signal].AddSpecialHandler(sa);
  chains[signal].Claim(signal);
}

extern "C" void RemoveSpecialSignalHandlerFn(int signal, bool (*fn)(int, siginfo_t*, void*)) {
  InitializeSignalChain();

  if (signal <= 0 || signal >= _NSIG) {
    fatal("Invalid signal %d", signal);
  }

  chains[signal].RemoveSpecialHandler(fn);
}

extern "C" void EnsureFrontOfChain(int signal) {
  InitializeSignalChain();

  if (signal <= 0 || signal >= _NSIG) {
    fatal("Invalid signal %d", signal);
  }

  // Read the current action without looking at the chain, it should be the expected action.
#if defined(__BIONIC__)
  struct sigaction64 current_action;
  linked_sigaction64(signal, nullptr, &current_action);
#else
  struct sigaction current_action;
  linked_sigaction(signal, nullptr, &current_action);
#endif

  // If the sigactions don't match then we put the current action on the chain and make ourself as
  // the main action.
  if (current_action.sa_sigaction != SignalChain::Handler) {
    log("Warning: Unexpected sigaction action found %p\n", current_action.sa_sigaction);
    chains[signal].Register(signal);
  }
}

extern "C" void SkipAddSignalHandler(bool value) {
  is_signal_hook_debuggable = value;
}

}   // namespace art

