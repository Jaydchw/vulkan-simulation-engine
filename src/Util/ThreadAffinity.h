#pragma once
// Thread-to-core affinity helpers for Win32.
//
// Assignment brief mapping (1-indexed core names):
//   Visualisation  → Core 1     (logical processor 0, bit 0) → mask 0x1
//   Networking     → Cores 2-3  (logical processors 1-2)     → mask 0x6
//   Simulation     → Core 4+    (logical processors 3+)      → mask ~0x7
//
// NOTE: "Core 1" in the brief maps to Windows logical processor index 0.
// SetThreadAffinityMask uses a bitmask where bit N = logical processor N.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <iostream>
#include <string>
#include <thread>

namespace ThreadAffinity {

  // ── Affinity masks per the assignment brief ──────────────────────────────
  constexpr DWORD_PTR VISUALISATION_MASK = 0x1ULL;       // Core 1  (bit 0)
  constexpr DWORD_PTR NETWORKING_MASK    = 0x6ULL;       // Cores 2-3 (bits 1-2)
  constexpr DWORD_PTR SIMULATION_MASK    = ~0x7ULL;      // Core 4+  (bits 3+)

  // ── Internal helper ───────────────────────────────────────────────────────
  inline bool applyMask(HANDLE threadHandle, DWORD_PTR mask, const std::string& label) {
    // Clamp mask to processors actually available to this process
    DWORD_PTR processAffinity = 0, systemAffinity = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity);
    DWORD_PTR effective = mask & processAffinity;
    if (effective == 0) {
      // Fall back to the full process affinity if the requested cores are unavailable
      effective = processAffinity;
      std::cerr << "[ThreadAffinity] WARNING: requested mask 0x" << std::hex << mask
                << " has no overlap with process affinity 0x" << processAffinity
                << " for '" << label << "' – using full process affinity.\n" << std::dec;
    }
    DWORD_PTR prev = SetThreadAffinityMask(threadHandle, effective);
    if (prev == 0) {
      std::cerr << "[ThreadAffinity] SetThreadAffinityMask failed (label='" << label
                << "', error=" << GetLastError() << ")\n";
      return false;
    }
    std::cout << "[ThreadAffinity] '" << label << "' pinned to mask 0x"
              << std::hex << effective << std::dec << "\n";
    return true;
  }

  // ── Public API ────────────────────────────────────────────────────────────

  /// Pin the calling thread to the given affinity mask.
  inline bool setCurrentThread(DWORD_PTR mask, const std::string& label = "unnamed") {
    return applyMask(GetCurrentThread(), mask, label);
  }

  /// Pin a std::thread to the given affinity mask.
  /// Must be called after the thread has been constructed (native_handle is valid).
  inline bool setThread(std::thread& t, DWORD_PTR mask, const std::string& label = "unnamed") {
    return applyMask(t.native_handle(), mask, label);
  }

  /// Log which logical processors are available to this process.
  inline void logAvailableCores() {
    DWORD_PTR processAffinity = 0, systemAffinity = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity);
    std::cout << "[ThreadAffinity] Process affinity mask: 0x"
              << std::hex << processAffinity << std::dec << "  (system: 0x"
              << std::hex << systemAffinity << std::dec << ")\n";
  }

} // namespace ThreadAffinity
