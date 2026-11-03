#pragma once
#include <Global.h>
#include <string>
#include <initializer_list>

// ============================================================================
//  EngineRefs - every engine UFunction / UFont this DLL calls, resolved ONCE
//  on a background thread in a single GObjects pass.
//
//  Never use the SDK's UObject::FindObject/FindFunction/FindClass at runtime:
//  each call builds the full name of all ~1.2M objects (seconds of work), and
//  a miss is not cached, so it rescans on every call. Called from the render
//  or game thread that is a multi-second freeze. This pass only builds full
//  names for Function/Font objects (~150ms total, off-thread).
// ============================================================================

namespace EngineRefs
{
    struct Refs
    {
        UFunction* hudDrawTexture        = nullptr;
        UFunction* hudDrawMaterialSimple = nullptr;
        UFunction* hudDrawRect           = nullptr;
        UFunction* hudDrawText           = nullptr;
        UFunction* hudGetTextSize        = nullptr;
        UFunction* canvasDrawText        = nullptr;
        UFunction* itemGetHudIcon        = nullptr;
        UFunction* itemBPGetItemIcon     = nullptr;
        UFunction* actorDisableInput     = nullptr;
        UFunction* actorEnableInput      = nullptr;
        UFunction* pcGetPlayerCharacter  = nullptr;
        UFunction* pcServerMultiUse      = nullptr; // Quick Pickup
        UFunction* hudIsChatBoxVisible   = nullptr; // Quick Pickup
        UFunction* structBPIsAllowedToBuild   = nullptr; // Structure Snap
        UFunction* structBPIsAllowedToBuildEx = nullptr; // Structure Snap
        UFunction* pcServerRequestPlaceStructure = nullptr; // Structure Snap
        UFunction* hudDrawCrosshairOnScreen = nullptr; // Crosshair (ShooterHUD.DrawCrosshairOnScreen)
        UFunction* hudDrawLine           = nullptr;    // Crosshair X style (HUD.DrawLine)
        UFunction* charGetAimedActor     = nullptr;    // Tek indicator (PrimalCharacter.GetAimedActor)
        UFunction* charGetWeapon         = nullptr;    // Tek indicator (ShooterCharacter.GetWeapon)
        UFunction* compSetRelLocRot      = nullptr; // Structure Snap (SceneComponent.SetRelativeLocationAndRotation)
        // Breeding (Dino tab)
        UFunction* pcTransferToRemoteInv = nullptr; // ServerTransferToRemoteInventory
        UFunction* pcNotifyEditText      = nullptr; // ServerNotifyEditText (rename)
        UFunction* pcRequestLevelUp      = nullptr; // ServerRequestLevelUp
        UFunction* invAddItemToSlot      = nullptr; // PrimalInventoryComponent.ServerAddItemToSlot
        UFunction* pcNetExecCommand      = nullptr; // PlayerController.ServerProcessNetExecCommand
        UFunction* pcRequestActorItems   = nullptr; // ServerRequestActorItems
        UFunction* pcEquipToRemoteInv    = nullptr; // ServerEquipToRemoteInventory
        UFunction* pcViewRemoteInv       = nullptr; // ServerActorViewRemoteInventory
        UFunction* pcTransferAllFromRemote = nullptr; // ServerTransferAllFromRemoteInventory (Take All)
        UFunction* pcClientNotifyEditText = nullptr; // ClientNotifyEditText (name box after a claim)
        UFunction* statusCanLevelUp      = nullptr; // PrimalCharacterStatusComponent.CanLevelUp
        UFunction* charIsAlive           = nullptr; // PrimalCharacter.IsAlive
        UFunction* charIsConscious       = nullptr; // PrimalCharacter.BPIsConscious (Spyglass)
        UFunction* invGetMaxItems        = nullptr; // Info HUD
        UFunction* invAllowViewing       = nullptr; // Turret Filler (PrimalInventoryComponent.RemoteInventoryAllowViewing)
        UFunction* hudProject            = nullptr; // Turret Filler markers (HUD.Project)
        UFunction* uiEntrySecondaryClick = nullptr; // crash guard (DataListEntryButton.ClickedSecondaryButton)
        // ARK's own UI panel art (Spyglass "ARK Background"), loaded with the UI
        UObject*   uiPanelBgAnim   = nullptr; // Material /Game/PrimalEarth/UI/PanelBackgroundAnimation
        UObject*   uiBgHighOpacity = nullptr; // MaterialInstanceConstant UI_Background_HighOpacity_MIC
        UObject*   uiTooltipBg     = nullptr; // Texture2D ToolTipBg
        UObject*   uiBorderBg      = nullptr; // Texture2D BorderBg
        UObject*   uiBorderNormal  = nullptr; // Texture2D BaseBorderNormal
        UObject*   uiBorderGlow    = nullptr; // Texture2D PanelBorderHighlight
        UObject*   uiItemSlot      = nullptr; // Texture2D ItemNormalBox (inventory slot)
        UObject*   uiTabSeparator  = nullptr; // Texture2D InventoryTabSeparator
        UFunction* hudDrawMaterial      = nullptr; // HUD.DrawMaterial (with UVs)
        UFunction* victoryGetUIScale    = nullptr; // VictoryCore.GetUIScale
        UFunction* wllGetViewportScale  = nullptr; // WidgetLayoutLibrary.GetViewportScale
        int32_t    nameMaxOpacity       = -1;      // FName "MaxOpacity" (panel material scalar)
        UClass*    clsTurret             = nullptr; // Info HUD (covers heavies + plant X)
        UClass*    clsDataListPanel      = nullptr; // Better Input (inventory search boxes)
        UFont*     fontSansation = nullptr; // SansationBold18 - ARK's structure / prompt HUD font
        UFont*     font36 = nullptr;
        UFont*     font24 = nullptr;
        UFont*     font18 = nullptr;

        // Stalker Vision
        UFunction* kismetCreateMID       = nullptr;
        UObject*   kismetMaterialCDO     = nullptr;
        UClass*    clsPostProcessVolume  = nullptr;
        UObject*   matESPOutline         = nullptr;
        UFunction* midSetScalar          = nullptr;
        UFunction* midSetVector          = nullptr;
        int32_t    nameESPFillIntensity  = -1;
        int32_t    nameESPDepth          = -1;
        int32_t    nameBackgroundColor   = -1;

        // Structure Snap: arrow-key camera axes and keys (FName indices)
        int32_t    nameAxisLookUp = -1, nameAxisTurn = -1;
        int32_t    nameKeyLeft = -1, nameKeyRight = -1, nameKeyUp = -1, nameKeyDown = -1;
        int32_t    nameClone = -1;   // Breeding: the cloning chamber's net exec command
    };
    inline Refs          g_refs;
    inline volatile LONG g_state = 0; // 0 = not started, 1 = resolving, 2 = ready

    inline bool Ready() { return g_state == 2; }
    inline void LogUiArt();

    inline bool StalkerReady()
    {
        const Refs& r = g_refs;
        return r.kismetCreateMID && r.kismetMaterialCDO && r.clsPostProcessVolume && r.matESPOutline &&
               r.midSetScalar && r.midSetVector && r.nameESPFillIntensity > 0 && r.nameESPDepth > 0 &&
               r.nameBackgroundColor > 0;
    }

    inline void ResolveIndex(int32_t i)
    {
        UObject* o = UObject::GetGlobalObjects().GetByIndex(i);
        if (!o || !o->Class) return;
        const std::string cls = o->Class->GetName();
        if (cls != "Function" && cls != "Font" && cls != "Class" &&
            cls != "Material" && cls != "KismetMaterialLibrary" &&
            cls != "MaterialInstanceConstant" && cls != "Texture2D") return;
        // ARK's UI panel art: matched by class + short asset name (the asset
        // names are unique; full names with package paths did not match).
        if (cls == "Texture2D" || cls == "MaterialInstanceConstant" || cls == "Material")
        {
            const std::string n = o->GetName();
            struct { const char* cls; const char* name; UObject** slot; } const art[] = {
                { "Material",                 "PanelBackgroundAnimation",      &g_refs.uiPanelBgAnim },
                { "MaterialInstanceConstant", "UI_Background_HighOpacity_MIC", &g_refs.uiBgHighOpacity },
                { "Texture2D",                "ToolTipBg",                     &g_refs.uiTooltipBg },
                { "Texture2D",                "BorderBg",                      &g_refs.uiBorderBg },
                { "Texture2D",                "BaseBorderNormal",              &g_refs.uiBorderNormal },
                { "Texture2D",                "PanelBorderHighlight",          &g_refs.uiBorderGlow },
                { "Texture2D",                "ItemNormalBox",                 &g_refs.uiItemSlot },
                { "Texture2D",                "InventoryTabSeparator",         &g_refs.uiTabSeparator },
            };
            for (const auto& e : art)
                if (cls == e.cls && n == e.name) { *e.slot = o; return; }
            if (cls != "Material") return;   // other textures / MICs: not ours
        }

        const std::string full = o->GetFullName();
        struct { const char* name; void** slot; } const table[] = {
            { "Function Engine.HUD.DrawTexture",        (void**)&g_refs.hudDrawTexture },
            { "Function Engine.HUD.DrawMaterialSimple", (void**)&g_refs.hudDrawMaterialSimple },
            { "Function Engine.HUD.DrawRect",           (void**)&g_refs.hudDrawRect },
            { "Function Engine.HUD.DrawText",           (void**)&g_refs.hudDrawText },
            { "Function Engine.HUD.GetTextSize",        (void**)&g_refs.hudGetTextSize },
            { "Function Engine.Canvas.K2_DrawText",     (void**)&g_refs.canvasDrawText },
            { "Function ShooterGame.PrimalItem.GetHUDIconMaterial", (void**)&g_refs.itemGetHudIcon },
            { "Function ShooterGame.PrimalItem.BPGetItemIcon",      (void**)&g_refs.itemBPGetItemIcon },
            { "Function Engine.Actor.DisableInput",     (void**)&g_refs.actorDisableInput },
            { "Function Engine.Actor.EnableInput",      (void**)&g_refs.actorEnableInput },
            { "Function ShooterGame.ShooterPlayerController.GetPlayerCharacter", (void**)&g_refs.pcGetPlayerCharacter },
            { "Function ShooterGame.ShooterPlayerController.ServerMultiUse",     (void**)&g_refs.pcServerMultiUse },
            { "Function ShooterGame.ShooterHUD.IsChatBoxVisible",                (void**)&g_refs.hudIsChatBoxVisible },
            { "Function ShooterGame.PrimalInventoryComponent.GetMaxInventoryItems", (void**)&g_refs.invGetMaxItems },
            { "Function ShooterGame.ShooterPlayerController.ServerTransferToRemoteInventory", (void**)&g_refs.pcTransferToRemoteInv },
            { "Function ShooterGame.ShooterPlayerController.ServerNotifyEditText",  (void**)&g_refs.pcNotifyEditText },
            { "Function ShooterGame.ShooterPlayerController.ServerRequestLevelUp",  (void**)&g_refs.pcRequestLevelUp },
            { "Function ShooterGame.PrimalInventoryComponent.ServerAddItemToSlot",  (void**)&g_refs.invAddItemToSlot },
            { "Function Engine.PlayerController.ServerProcessNetExecCommand",       (void**)&g_refs.pcNetExecCommand },
            { "Function ShooterGame.ShooterPlayerController.ServerRequestActorItems", (void**)&g_refs.pcRequestActorItems },
            { "Function ShooterGame.ShooterPlayerController.ServerEquipToRemoteInventory", (void**)&g_refs.pcEquipToRemoteInv },
            { "Function ShooterGame.ShooterPlayerController.ServerActorViewRemoteInventory", (void**)&g_refs.pcViewRemoteInv },
            { "Function ShooterGame.ShooterPlayerController.ServerTransferAllFromRemoteInventory", (void**)&g_refs.pcTransferAllFromRemote },
            { "Function ShooterGame.ShooterPlayerController.ClientNotifyEditText",  (void**)&g_refs.pcClientNotifyEditText },
            { "Function ShooterGame.PrimalCharacterStatusComponent.CanLevelUp",     (void**)&g_refs.statusCanLevelUp },
            { "Function ShooterGame.PrimalCharacter.IsAlive",                       (void**)&g_refs.charIsAlive },
            { "Function ShooterGame.PrimalCharacter.BPIsConscious",                 (void**)&g_refs.charIsConscious },
            { "Function ShooterGame.PrimalStructure.BPIsAllowedToBuild",            (void**)&g_refs.structBPIsAllowedToBuild },
            { "Function ShooterGame.PrimalStructure.BPIsAllowedToBuildEx",          (void**)&g_refs.structBPIsAllowedToBuildEx },
            { "Function ShooterGame.ShooterPlayerController.ServerRequestPlaceStructure", (void**)&g_refs.pcServerRequestPlaceStructure },
            { "Function Engine.SceneComponent.SetRelativeLocationAndRotation",      (void**)&g_refs.compSetRelLocRot },
            { "Function ShooterGame.ShooterHUD.DrawCrosshairOnScreen",              (void**)&g_refs.hudDrawCrosshairOnScreen },
            { "Function Engine.HUD.DrawLine",                                       (void**)&g_refs.hudDrawLine },
            { "Function Engine.HUD.Project",                                        (void**)&g_refs.hudProject },
            { "Function Engine.HUD.DrawMaterial",                                   (void**)&g_refs.hudDrawMaterial },
            { "Function ShooterGame.VictoryCore.GetUIScale",                        (void**)&g_refs.victoryGetUIScale },
            { "Function UMG.WidgetLayoutLibrary.GetViewportScale",                  (void**)&g_refs.wllGetViewportScale },
            { "Function ShooterGame.DataListEntryButton.ClickedSecondaryButton",     (void**)&g_refs.uiEntrySecondaryClick },
            { "Function ShooterGame.PrimalInventoryComponent.RemoteInventoryAllowViewing", (void**)&g_refs.invAllowViewing },
            { "Function ShooterGame.PrimalCharacter.GetAimedActor",                 (void**)&g_refs.charGetAimedActor },
            { "Function ShooterGame.ShooterCharacter.GetWeapon",                    (void**)&g_refs.charGetWeapon },
            { "Class ShooterGame.PrimalStructureTurret",                           (void**)&g_refs.clsTurret },
            { "Class ShooterGame.DataListPanel",                                   (void**)&g_refs.clsDataListPanel },
            { "Font SansationBold18.SansationBold18",   (void**)&g_refs.fontSansation },
            { "Font Roboto36.Roboto36",               (void**)&g_refs.font36 },
            { "Font Roboto24.Roboto24",                 (void**)&g_refs.font24 },
            { "Font Roboto18.Roboto18",                 (void**)&g_refs.font18 },
            { "Function Engine.KismetMaterialLibrary.CreateDynamicMaterialInstance", (void**)&g_refs.kismetCreateMID },
            { "KismetMaterialLibrary Engine.Default__KismetMaterialLibrary",         (void**)&g_refs.kismetMaterialCDO },
            { "Class Engine.PostProcessVolume",                                      (void**)&g_refs.clsPostProcessVolume },
            { "Material ESPMaterialOutline.ESPMaterialOutline",                      (void**)&g_refs.matESPOutline },
            { "Function Engine.MaterialInstanceDynamic.SetScalarParameterValue",     (void**)&g_refs.midSetScalar },
            { "Function Engine.MaterialInstanceDynamic.SetVectorParameterValue",     (void**)&g_refs.midSetVector },
        };
        for (const auto& e : table)
            if (full == e.name) { *e.slot = o; return; }
    }

    inline void ResolveAll()
    {
        const int32_t n = UObject::GetGlobalObjects().Count();
        for (int32_t i = 0; i < n; i++)
        {
            __try { ResolveIndex(i); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // FName comparison indices for material parameter names, read straight
    // from GNames (global -> chunk table -> 16384 entry pointers per chunk;
    // entry: int32 Index (bit0 = wide), pad, HashNext, then the ANSI name).
    inline void ResolveNameChunk(uintptr_t chunk, int32_t chunkIdx)
    {
        struct { const char* name; int32_t* slot; } const want[] = {
            { "ESPFillIntensity", &g_refs.nameESPFillIntensity },
            { "ESPDepth",         &g_refs.nameESPDepth },
            { "BackgroundColor",  &g_refs.nameBackgroundColor },
            { "LookUp",           &g_refs.nameAxisLookUp },
            { "Turn",             &g_refs.nameAxisTurn },
            { "Left",             &g_refs.nameKeyLeft },
            { "Right",            &g_refs.nameKeyRight },
            { "Up",               &g_refs.nameKeyUp },
            { "Down",             &g_refs.nameKeyDown },
            { "Clone",            &g_refs.nameClone },
            { "MaxOpacity",       &g_refs.nameMaxOpacity },
        };
        for (int32_t j = 0; j < 16384; j++)
        {
            const uintptr_t e = reinterpret_cast<const uintptr_t*>(chunk)[j];
            if (e < 0x10000) continue;
            if (*reinterpret_cast<const int32_t*>(e) & 1) continue; // wide name
            const char* s = reinterpret_cast<const char*>(e + 0x10);
            for (const auto& w : want)
                if (*w.slot <= 0 && strcmp(s, w.name) == 0)
                    *w.slot = chunkIdx * 16384 + j;
        }
    }

    inline void ResolveNames()
    {
        if (!FName::GNames) return;
        const uintptr_t table = *reinterpret_cast<const uintptr_t*>(FName::GNames);
        if (table < 0x10000) return;
        for (int32_t k = 0; k < 128; k++)
        {
            const uintptr_t chunk = reinterpret_cast<const uintptr_t*>(table)[k];
            if (chunk < 0x10000) break;
            __try { ResolveNameChunk(chunk, k); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    inline void LogStalker(const char* pass)
    {
        const Refs& r = g_refs;
        static int s_logged = 0;
        if (StalkerReady() ? s_logged == 99 : s_logged >= 4) return;
        s_logged = StalkerReady() ? 99 : s_logged + 1;
        Logger::Log("[EngineRefs] stalker (%s): createMID=%p kismetCDO=%p ppvClass=%p espMaterial=%p setScalar=%p setVector=%p names=%d/%d/%d",
            pass, r.kismetCreateMID, r.kismetMaterialCDO, r.clsPostProcessVolume, r.matESPOutline,
            r.midSetScalar, r.midSetVector, r.nameESPFillIntensity, r.nameESPDepth, r.nameBackgroundColor);
    }

    inline DWORD WINAPI ResolveThread(LPVOID)
    {
        const DWORD t0 = GetTickCount();
        ResolveAll();
        ResolveNames();
        const Refs& r = g_refs;
        Logger::Log("[EngineRefs] resolved in %lums: DrawTexture=%p DrawMaterialSimple=%p DrawRect=%p DrawText=%p GetTextSize=%p K2_DrawText=%p GetHUDIcon=%p BPGetItemIcon=%p DisableInput=%p EnableInput=%p GetPlayerCharacter=%p font36=%p font24=%p font18=%p",
            GetTickCount() - t0, r.hudDrawTexture, r.hudDrawMaterialSimple, r.hudDrawRect, r.hudDrawText,
            r.hudGetTextSize, r.canvasDrawText, r.itemGetHudIcon, r.itemBPGetItemIcon, r.actorDisableInput,
            r.actorEnableInput, r.pcGetPlayerCharacter, r.font36, r.font24, r.font18);
        Logger::Log("[EngineRefs] snap: BPIsAllowedToBuild=%p Ex=%p ServerRequestPlaceStructure=%p SetRelLocRot=%p DrawCrosshairOnScreen=%p GetAimedActor=%p GetWeapon=%p turretCls=%p maxItems=%p sansation=%p",
            r.structBPIsAllowedToBuild, r.structBPIsAllowedToBuildEx, r.pcServerRequestPlaceStructure,
            r.compSetRelLocRot, r.hudDrawCrosshairOnScreen, r.charGetAimedActor, r.charGetWeapon, r.clsTurret, r.invGetMaxItems, r.fontSansation);
        Logger::Log("[EngineRefs] breeding: MultiUse=%p Transfer=%p EditText=%p LevelUp=%p AddToSlot=%p NetExec=%p RequestItems=%p EquipRemote=%p ViewRemote=%p ClientEditText=%p CanLevelUp=%p IsAlive=%p cloneName=%d TransferAll=%p",
            r.pcServerMultiUse, r.pcTransferToRemoteInv, r.pcNotifyEditText, r.pcRequestLevelUp, r.invAddItemToSlot,
            r.pcNetExecCommand, r.pcRequestActorItems, r.pcEquipToRemoteInv, r.pcViewRemoteInv, r.pcClientNotifyEditText,
            r.statusCanLevelUp, r.charIsAlive, r.nameClone, r.pcTransferAllFromRemote);
        LogStalker("initial");
        LogUiArt();
        InterlockedExchange(&g_state, 2);
        return 0;
    }

    // ---- late retry (Stalker Vision) -----------------------------------
    // The ESP material and its parameter names only exist once the asset is
    // loaded, which may be after inject. Retried on its own thread so the
    // main refs (armor HUD, input block) never drop out of Ready().
    inline volatile LONG g_retryBusy = 0;
    inline ULONGLONG     g_retryNext = 0;

    inline DWORD WINAPI RetryThread(LPVOID)
    {
        ResolveAll();
        ResolveNames();
        LogStalker("retry");
        LogUiArt();
        g_retryNext = GetTickCount64() + 5000;
        InterlockedExchange(&g_retryBusy, 0);
        return 0;
    }

    inline void LogUiArt()
    {
        static bool s_done = false;
        const Refs& r = g_refs;
        const bool all = r.uiPanelBgAnim && r.uiBorderGlow;
        if (s_done) return;
        if (all) s_done = true;
        Logger::Log("[EngineRefs] ARK UI art: panelAnim=%p borderGlow=%p drawMaterial=%p uiScale=%p viewportScale=%p maxOpacity=%d (bgHighOpacity=%p tooltipBg=%p)",
                    r.uiPanelBgAnim, r.uiBorderGlow, r.hudDrawMaterial, r.victoryGetUIScale, r.wllGetViewportScale, r.nameMaxOpacity,
                    r.uiBgHighOpacity, r.uiTooltipBg);
    }

    // The UI art loads with ARK's UI, maybe after inject: retried (same thread
    // as Stalker Vision) while the spyglass wants it.
    inline void RequestUiArt()
    {
        const Refs& r = g_refs;
        if (!Ready() || (r.uiPanelBgAnim && r.uiBorderGlow) || GetTickCount64() < g_retryNext) return;
        if (InterlockedCompareExchange(&g_retryBusy, 1, 0) != 0) return;
        HANDLE h = CreateThread(nullptr, 0, RetryThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
        else InterlockedExchange(&g_retryBusy, 0);
    }

    // Call while the feature wants its refs; cheap when already resolved.
    inline void RequestStalkerRefs()
    {
        if (!Ready() || StalkerReady() || GetTickCount64() < g_retryNext) return;
        if (InterlockedCompareExchange(&g_retryBusy, 1, 0) != 0) return;
        HANDLE h = CreateThread(nullptr, 0, RetryThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
        else InterlockedExchange(&g_retryBusy, 0);
    }

    // Starts the background pass once (call after InitSDK succeeds).
    inline void Start()
    {
        if (InterlockedCompareExchange(&g_state, 1, 0) == 0)
        {
            HANDLE h = CreateThread(nullptr, 0, ResolveThread, nullptr, 0, nullptr);
            if (h) CloseHandle(h);
            else InterlockedExchange(&g_state, 0);
        }
    }
}
