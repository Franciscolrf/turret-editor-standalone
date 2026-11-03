#pragma once

// ============================================================================
//  CheatSettings.h - shared toggle state for the paidini base.
//  Right now this is just the INI preset toggles (graphics-quality switches
//  drawn by Modules/INI Presets/IniPresets.h and applied to the local
//  player's viewport client). Add new setting groups here as the base grows.
// ============================================================================

#include <string>

namespace Settings
{
    class Ini
    {
    public:
        static inline bool Normal = false;
        static inline bool Hard = false;
        static inline bool Potato = false;
        static inline bool FullBright = false;
        static inline bool NoXZ = false;
        static inline bool NoRockTrees = false;
        static inline bool RemoveParticles = false;
        static inline bool NoFog = false;
        static inline bool NoLandscape = false;
        static inline bool NoWater = false;

        static inline bool  StalkerVision = false;
        static inline float StalkerDist   = 100.f; // metres (ESPDepth = x100)

        static inline bool  CustomFOV = false;
        static inline float FOVValue  = 90.f;

        static inline bool  DrawArmor           = false;
        static inline bool  DrawArmorDurability = false;
        static inline bool  DrawArmorCount      = false;
        static inline bool  DrawArmorPercent    = false;
        static inline float ArmorHudScale       = 2.00f;
        static inline float ArmorHudYOffset     = 645.f;
        static inline float ArmorTextScale      = 1.f;
        static inline float ArmorTextOffsetX    = 0.f; // px at 1440p, relative to centred under the icon
        static inline float ArmorTextOffsetY    = 0.f;
    };

    // Game Filter (vibrance etc.), in menu units: percent, gamma as a factor.
    class Filter
    {
    public:
        struct V
        {
            float vibrance    = 0.f;   // -100..100
            float saturation  = 100.f; //  0..200
            float contrast    = 100.f; // 50..150
            float brightness  = 100.f; // 50..150
            float gamma       = 1.f;   // 0.5..2
            float temperature = 0.f;   // -100..100
            float sharpen     = 0.f;   //  0..100
        };
        static inline bool Enabled = false;
        static inline V    Values{};
    };

    // Info HUD - top-left text overlay (ArkHoles HudDisplay layout). Lines draw
    // in a fixed order; each one has its own toggle.
    class Hud
    {
    public:
        static inline bool  InfoHud          = true;  // master switch
        static inline float InfoHudScale     = 1.f;
        static inline bool  Rainbow          = false;
        static inline float RainbowSpeed     = 1.f;

        static inline bool  Watermark        = true;
        static inline bool  ShowFPS          = false;
        static inline bool  Ping             = false;
        static inline bool  ServerFPS        = false;
        static inline bool  ConnectedPlayers = false;
        static inline bool  TurretsInRange   = false; // enemy turrets
        static inline bool  FriendlyTurrets  = false; // also show your tribe's count
        static inline bool  InventorySlots   = false;
        static inline bool  ServerName       = false;

        // Custom crosshair (Modules/HUD/Crosshair.h) - drawn as ARK's own.
        static inline bool  Crosshair        = false;
        static inline int   CrossStyle       = 0;      // 0 Classic, 1 Cross, 2 Dot, 3 X
        static inline float CrossSize        = 1.f;    // x ARK's size
        static inline float CrossGap         = 2.f;    // ARK spread units
        static inline float CrossOpacity     = 100.f;  // %
        static inline float CrossThickness   = 2.f;    // px at 1080p (Cross style)
        static inline float CrossColor[3]    = { 1.f, 1.f, 1.f };

        // Tek Rifle indicator (Modules/HUD/TekIndicator.h)
        static inline bool  TekIndicator     = false;
        static inline bool  TekAnyWeapon     = false;  // show with any weapon, not only the Tek Rifle
        static inline float TekScale         = 1.f;
        static inline float TekRange         = 50.f;   // m - green inside, red past it

        // Awesome Spyglass (Modules/HUD/AwesomeSpyglass.h)
        static inline bool  Spyglass         = false;
        static inline float SpyglassScale    = 1.f;
        static inline float SpyglassX        = 0.f;    // px at 1440p, from the default (centred, near the top)
        static inline float SpyglassY        = 0.f;
        static inline int   SpyglassLockKey  = 0;
        static inline bool  SpyglassArkBg    = true;   // ARK's own panel art behind the panel      // ImGuiKey; press to lock / unlock the spyglass target
        static inline float TekOffsetX       = 0.f;    // px at 1440p, from under the crosshair
        static inline float TekOffsetY       = 0.f;
    };

    // Dino tab - breeding automation (Modules/Dino/Breeding.h).
    class Dino
    {
    public:
        static inline bool  AutoSaddle        = false;
        static inline float AutoSaddleReach   = 30.f;   // m
        static inline bool  AutoImprint       = false;
        static inline bool  AutoNameDino      = false;
        static inline char  AutoNameText[64]  = {};    // fixed buffer: written on the render thread, read on the game thread
        static inline float AutoNameRange     = 10.f;   // m
        static inline bool  AutoNameOverwrite = false;  // rename already-named dinos too
        static inline bool  AutoClaim         = false;
        static inline bool  AutoNeuter        = false;
        static inline float AutoNeuterRange   = 10.f;   // m
        static inline bool  AutoLevel         = false;
        static inline int   AutoLevelPoints[8] = {};    // Health, Stamina, Oxygen, Food, Weight, Melee, Speed, Crafting
        static inline bool  AutoClone         = false;
        static inline float AutoCloneRange    = 100.f;  // m
        static inline bool  AutoFillCloning   = false;
        static inline bool  AutoPickupEggs    = false;
        static inline bool  TransferEggsIncubator = false;
        static inline bool  HatchIncubatorEggs    = false;
    };

    class Misc
    {
    public:
        static inline bool QuickPickup    = false;
        static inline int  QuickPickupKey = 0; // ImGuiKey, 0 = none

        static inline bool  BetterInput       = false; // fast Drop / Transfer / Use in inventories
        static inline bool  ChatBypass        = false; // UWP profanity filter off
        static inline bool  TakeAll           = false; // take everything from an inventory you open
        static inline int   TakeAllKey        = 0;     // ImGuiKey; 0 = automatic on opening

        static inline bool  TurretFiller       = false; // top up your heavy / Tek turrets in reach
        static inline float TurretFillHeavy    = 200.f; // advanced rifle bullets per heavy turret
        static inline float TurretFillTek      = 50.f;  // element shards per Tek turret
        static inline bool  ShowTurretFill     = false; // green / red markers on your turrets
        static inline float ShowTurretFillDist = 100.f; // m

        static inline bool  StructureSnapping = false;
        static inline float SnapSpacing       = 0.f; // metres between snapped structures (arrow keys)

        static inline bool  AutoForge         = false; // automated smelting line (Misc tab)
        static inline bool  AutoForgeGas      = false; // split gasoline over industrial forges
        static inline bool  AutoForgeMetal    = false; // split metal + scrap over every forge
        static inline bool  AutoForgePull     = false; // pull finished ingots out of forges
        static inline bool  AutoForgePullDedi = false; // pulled ingots straight into dedis
        static inline bool  AutoForgeTurnOn   = false; // light off forges via the use wheel
    };

    // Turret Editor (Modules/Misc/TurretEditor.h) - bulk turret settings in a
    // radius measured in foundations (1 foundation = 400 cm). The settings
    // themselves (range / targeting) mirror ARK's turret wheel values.
    class Turret
    {
    public:
        static inline bool  Visible          = false; // F7 toggles while the menu is open
        static inline bool  Learn            = false; // opt in while manually changing a turret to learn actions
        static inline float RangeFoundations = 20.f;  // scan / apply radius around you
        static inline bool  FilterAuto       = true;
        static inline bool  FilterHeavy      = true;
        static inline bool  FilterTek        = true;
        static inline bool  FilterPlant      = false;

        static inline int   RangeSetting     = -1;    // -1 = leave alone, 0 Low, 1 Medium, 2 High
        static inline int   AISetting        = -1;    // -1 = leave alone, 0..5
        static inline volatile bool ApplyRequested = false;
        static inline volatile bool DumpRequested  = false;
        static inline volatile bool ForgetRequested = false;
        static inline volatile bool ResetRequested  = false;
    };

    // Read by the helper headers AutoForge compiles against
    // (Modules/Exploits/AutoDupe.h, TransmitterInventory.h). Nothing turns
    // these on - there is no menu for them.
    class Exploits
    {
    public:
        static inline bool Dupe       = false; // manual dupe: null ClusterId while on
        static inline bool AutoDupeV2 = false; // auto dupe loop off the selected item
        static inline float AutoDupeSpeed = 100.f; // auto dupe speed, % (100 = current pace)
        static inline bool ClickDupe  = false; // click dupe: one cycle per fresh selection
        static inline bool TransmitterInventory = false; // TransBag: ark transfers target a real tribute inventory
        static inline bool UploadScreenBlock = false; // no upload screen: block the ARK status popup widget
    };
}
