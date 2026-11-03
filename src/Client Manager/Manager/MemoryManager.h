#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
//  MemoryManager - module pattern scanning for the paidini base.
//  PatternScan parses an IDA-style signature ("4C 89 ? ...") and scans the
//  main game module, returning up to maxResults matches. Callers must treat
//  an empty result as "not found" — never index [0] blindly.
// ============================================================================
class MemoryManager
{
public:
    // Scan the main module for sig, collecting at most maxResults hits.
    std::vector<void*> PatternScan(const char* sig, int maxResults = 1)
    {
        std::vector<void*> out;
        if (!sig || maxResults <= 0) return out;

        // ---- Parse "4C 89 44 24 ? ..." into bytes + mask --------------------
        uint8_t bytes[512]{};
        bool    mask[512]{};
        int     len = 0;
        for (const char* p = sig; *p && len < (int)_countof(bytes);)
        {
            while (*p == ' ') p++;
            if (!*p) break;
            if (*p == '?')
            {
                mask[len] = false;
                bytes[len] = 0;
                len++;
                while (*p == '?') p++;
            }
            else
            {
                unsigned v = 0;
                int n = 0;
                if (sscanf_s(p, "%2x%n", &v, &n) != 1) break;
                mask[len]  = true;
                bytes[len] = (uint8_t)v;
                len++;
                p += n;
            }
        }
        if (!len) return out;

        // ---- Walk the main module's committed pages --------------------------
        HMODULE mod = GetModuleHandleW(nullptr);
        if (!mod) return out;

        auto* dos = (IMAGE_DOS_HEADER*)mod;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
        auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)mod + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return out;

        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; s++, sec++)
        {
            // Executable sections only — the target is code.
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;

            uint8_t* base = (uint8_t*)mod + sec->VirtualAddress;
            const DWORD size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
            if (size < (DWORD)len) continue;

            for (DWORD i = 0; i + (DWORD)len <= size; i++)
            {
                bool hit = true;
                for (int j = 0; j < len; j++)
                {
                    if (mask[j] && base[i + j] != bytes[j]) { hit = false; break; }
                }
                if (hit)
                {
                    out.push_back(base + i);
                    if ((int)out.size() >= maxResults) return out;
                    i += (DWORD)len - 1; // non-overlapping
                }
            }
        }
        return out;
    }
};
