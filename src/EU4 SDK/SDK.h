#pragma once
#include <Windows.h>
#include <Macros.h>


#include <Basic/FLinearColor.h>
#include <Basic/FRotator-FVector.h>
#include <Basic/FVector2D.h>
#include <Basic/FMatrix.h>



#include <Engine/Engine.h>



#include <Game/Game.h>


// Parses "48 8B ? ..." into bytes (-1 = wildcard). Hand-rolled instead of
// strtoul: CRT parsing misbehaved inside the manually mapped image.
static std::vector<int32_t> PatternByte(const std::string& pattern)
{
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<int32_t> bytes;
    for (size_t i = 0; i < pattern.size();)
    {
        const char c = pattern[i];
        if (c == ' ') { ++i; continue; }
        if (c == '?') { bytes.push_back(-1); while (i < pattern.size() && pattern[i] == '?') ++i; continue; }
        const int hi = nib(c), lo = i + 1 < pattern.size() ? nib(pattern[i + 1]) : -1;
        if (hi < 0 || lo < 0) return {};
        bytes.push_back(hi * 16 + lo);
        i += 2;
    }
    return bytes;
}

// Scans only the module's executable sections (every SDK signature is code).
static void* ScanPattern(void* moduleBaseAddress, const std::string& signature)
{
    if (!moduleBaseAddress) return nullptr;
    auto* mod = static_cast<uint8_t*>(moduleBaseAddress);
    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(mod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(mod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const std::vector<int32_t> pat = PatternByte(signature);
    if (pat.empty()) return nullptr;
    const size_t len = pat.size();

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec)
    {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* base = mod + sec->VirtualAddress;
        const size_t size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
        for (size_t i = 0; i + len <= size; ++i)
        {
            size_t j = 0;
            while (j < len && (pat[j] < 0 || base[i + j] == pat[j])) ++j;
            if (j == len) return const_cast<uint8_t*>(base + i);
        }
    }
    return nullptr;
}

static void* ScanPattern(const std::string& signature)
{
    return ScanPattern(GetModuleHandleW(nullptr), signature);
}










static uintptr_t ResolveRipGlobal(const char* Tag, const std::string& Signature)
{
    const uintptr_t Site = reinterpret_cast<uintptr_t>(ScanPattern(Signature));
    if (!Site)
    {
        LOG("InitSDK: %s signature NOT FOUND", Tag);
        return 0;
    }

    const uint32_t Displacement = *reinterpret_cast<uint32_t*>(Site + 3);
    const uintptr_t Resolved = Site + Displacement + 7;
    LOG("InitSDK: %s = %p (site %p)", Tag, (void*)Resolved, (void*)Site);
    return Resolved;
}

static void* ResolveFunction(const char* Tag, const std::string& Signature)
{
    void* Address = ScanPattern(Signature);
    if (!Address) LOG("InitSDK: %s signature NOT FOUND", Tag);
    else          LOG("InitSDK: %s = %p", Tag, Address);
    return Address;
}



static bool InitSDK()
{

    const uintptr_t Objects = ResolveRipGlobal("GObjects",
        "4C 8B 15 ? ? ? ? 33 F6 48 89 5C 24 ? 48 89 7C 24 ?");
    UObject::GObjects = reinterpret_cast<FChunkedFixedUObjectArray*>(Objects);









    const uintptr_t Names = ResolveRipGlobal("GNames",
        "48 8B 05 ? ? ? ? 48 85 C0 75 77 48 8B 0D ? ? ? ? 48 89 5C 24 ?");
    FName::GNames = reinterpret_cast<TNameEntryArray*>(Names);


    const uintptr_t World = ResolveRipGlobal("GWorld",
        "48 8B 3D ? ? ? ? 40 38 35 ? ? ? ? 48 0F 45 F8 EB 03 48 8B FE 48 85 FF");
    UWorld::GWorld = reinterpret_cast<decltype(UWorld::GWorld)>(World);


    UObject::pProcessEvent = reinterpret_cast<decltype(UObject::pProcessEvent)>(
        ResolveFunction("ProcessEvent",
            "40 55 53 56 57 41 54 41 56 41 57 48 81 EC ? ? ? ? 48 8D 6C 24 ? 48 C7 45 ? ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C5 48 89 85 ? ? ? ? 49 8B F0 48 8B FA 4C 8B F1 8B 41 08 C1 E8 0E A8 01 0F 85 ? ? ? ?"));


    APlayerController::pGetUniqueNetIdAsString = reinterpret_cast<decltype(APlayerController::pGetUniqueNetIdAsString)>(
        ResolveFunction("GetUniqueNetIdAsString",
            "48 8B C4 48 89 50 10 57 48 83 EC 60 48 C7 40 ? ? ? ? ? 48 89 58 18 48 89 70 20 48 8B FA 33 F6 89 70 B8 48 8B 99 ? ? ? ? 48 85 DB 74 30 48 8B 8B ? ? ? ? 48 85 C9 74 24 48 8B 01 FF 50 20 84 C0 74 1A"));



    if (!Objects || !Names)
    {
        const auto* mod = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        LOG("InitSDK: module base=%p parsed=%zu bytes, bytes@GWorldSite(rva 27B954)=%02X %02X %02X",
            (void*)mod, PatternByte("48 8B 3D ? ? ? ? 40 38 35").size(), mod[0x27B954], mod[0x27B955], mod[0x27B956]);
        LOG("InitSDK: FAILED - GObjects=%p GNames=%p", (void*)Objects, (void*)Names);
        return false;
    }

    return true;
}