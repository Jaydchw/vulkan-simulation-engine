#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <thread>

#include "Debug.h"

namespace ThreadAffinity {

  constexpr DWORD_PTR VISUALISATION_MASK = 0x1ULL;
  constexpr DWORD_PTR NETWORKING_MASK    = 0x6ULL;
  constexpr DWORD_PTR SIMULATION_MASK    = ~0x7ULL;

  inline bool applyMask(HANDLE threadHandle, DWORD_PTR mask, const std::string& label) {
    DWORD_PTR processAffinity = 0, systemAffinity = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity);
    DWORD_PTR effective = mask & processAffinity;
    if (effective == 0) {
      effective = processAffinity;
      Debug::log(Debug::Category::THREADING,
                 "Affinity fallback label=", label,
                 " requested=0x", std::hex, mask,
                 " process=0x", processAffinity,
                 std::dec);
    }
    DWORD_PTR prev = SetThreadAffinityMask(threadHandle, effective);
    if (prev == 0) {
      Debug::log(Debug::Category::THREADING,
                 "SetThreadAffinityMask failed label=", label,
                 " error=", GetLastError());
      return false;
    }
    Debug::log(Debug::Category::THREADING,
               "Thread pinned label=", label,
               " mask=0x", std::hex, effective,
               std::dec);
    return true;
  }
  inline bool setCurrentThread(DWORD_PTR mask, const std::string& label = "unnamed") {
    return applyMask(GetCurrentThread(), mask, label);
  }
  inline bool setThread(std::thread& t, DWORD_PTR mask, const std::string& label = "unnamed") {
    return applyMask(t.native_handle(), mask, label);
  }
  inline void logAvailableCores() {
    DWORD_PTR processAffinity = 0, systemAffinity = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity);
    Debug::log(Debug::Category::THREADING,
               "Process affinity=0x", std::hex, processAffinity,
               " system=0x", systemAffinity,
               std::dec);
  }

} // namespace ThreadAffinity
