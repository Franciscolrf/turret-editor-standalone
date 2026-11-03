#pragma once
#include <cstdint>

enum EObjectFlags
{
	RF_Public = 0x1,
	RF_Standalone = 0x2,
	RF_Native = 0x4,
	RF_Transactional = 0x8,
	RF_ClassDefaultObject = 0x10,
	RF_ArchetypeObject = 0x20,
	RF_Transient = 0x40,
	RF_RootSet = 0x80,
	RF_Unreachable = 0x100,
	RF_TagGarbageTemp = 0x200,
	RF_NeedLoad = 0x400,
	RF_AsyncLoading = 0x800,
	RF_NeedPostLoad = 0x1000,
	RF_NeedPostLoadSubobjects = 0x2000,
	RF_PendingKill = 0x4000,
	RF_BeginDestroyed = 0x8000,
	RF_FinishDestroyed = 0x10000,
	RF_BeingRegenerated = 0x20000,
	RF_DefaultSubObject = 0x40000,
	RF_WasLoaded = 0x80000,
	RF_TextExportTransient = 0x100000,
	RF_LoadCompleted = 0x200000,
	RF_WhiteListed = 0x400000,
	RF_AsyncLoadingRef = 0x800000,
	RF_MarkedByCooker = 0x1000000,
	RF_ForceTagExp = 0x2000000,
	RF_OlderObject = 0x4000000,
	RF_AllFlags = 0x7FFFFFF,
	RF_NoFlags = 0x0,
	RF_Load = 0x14003F,
	RF_PropagateToSubObjects = 0x69,
};


enum class EBlendMode : uint8_t
{
	BLEND_Opaque = 0,
	BLEND_Masked = 1,
	BLEND_Translucent = 2,
	BLEND_Additive = 3,
	BLEND_Modulate = 4,
	BLEND_MAX = 5
};

