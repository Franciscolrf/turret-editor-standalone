#pragma once

// ============================================================================
//  Global.h - shared state + helpers for the paidini base.
//  SDK + logger + settings + memory-safety helpers used by the client.
// ============================================================================

#include <Windows.h>
#include <cstdlib>
#include <cstdint>
#include <string>

#include "EU4 SDK/SDK.h"
#include "Logger/Logger.h"
#include "Client Manager/CheatSettings.h"
#include "Client Manager/Manager/MemoryManager.h"

// -----------------------------------------------------------------
// Memory safety - VirtualQuery-based readability check. Every hook that
// dereferences a pointer scraped out of the game should gate on this
// (or MemBadPtr) instead of trusting the pointer blindly.
// -----------------------------------------------------------------
static bool MemReadable(const void* p, size_t n)
{
    if (!p || reinterpret_cast<uintptr_t>(p) < 0x10000 || n == 0)
        return false;

    const uint8_t* cur = static_cast<const uint8_t*>(p);
    const uint8_t* end = cur + n;
    if (end < cur) return false;

    while (cur < end)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(cur, &mbi, sizeof(mbi)))      return false;
        if (mbi.State != MEM_COMMIT)                    return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

        const uint8_t* regionEnd = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= cur) return false;
        cur = regionEnd;
    }
    return true;
}

static bool MemBadPtr(const void* p, size_t n) { return !MemReadable(p, n); }

// -----------------------------------------------------------------
// Session-wide flags the client reads.
// -----------------------------------------------------------------
static inline bool   UnloadDll = false;

static inline MemoryManager* Memory = nullptr;
