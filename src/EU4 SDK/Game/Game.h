#include <Macros.h>
#include <Engine/Engine.h>

struct PrimalUI
{
    void ClickedButton(void* clickedWidget)
    {
        static UFunction* fn = nullptr;
        if (!fn)
            fn = UObject::FindObject<UFunction>("Function ShooterGame.PrimalUI.ClickedButton");

        struct { void* clickedWidget; } params;
        params.clickedWidget = clickedWidget;
        UObject::ProcessEvent(this, fn, &params);
    }
};

struct UWidget
{
    PAD(0x38);
    bool bIsEnabled;
};

struct UUI_MainMenu : PrimalUI
{
    PAD(0x640);
    UWidget* HostButton;
};

struct UUI_ListSessions : PrimalUI
{
    PAD(0x718);
    bool ShouldGoToMainMenuOnJoinFailure;
    bool bShouldRetry;
    bool bFindSessions;
    bool bShowBuildColumn;
    bool bLeftShoulderPressed;
    bool bRightShoulderPressed;
    bool bSortAsc;
    PAD(0xF9);
    UWidget* JoinLastPlayedButton;
    UWidget* RefreshButton;
    PAD(0x2BC);
    int LastPlayedSessionIndex;
};

struct UStartScreenUI_C : PrimalUI
{
    PAD(0x590);
    UWidget* StartButton;
};

struct UUI_Spawn : PrimalUI
{
    PAD(0x6B8);
    UWidget* RespawnButton;
};

struct UUI_HostSession : PrimalUI
{
    PAD(0x6E0);
    UWidget* PasswordAcceptButton;
    UWidget* PasswordCancelButton;
    UWidget* HostButton;
    UWidget* HostDedicatedButton;
    PAD(0xB3F);
    bool bHostDediConfirmationAccepted;
};

struct __declspec(align(8)) FHitResult
{
    unsigned char bBlockingHit : 1;
    unsigned char bStartPenetrating : 1;
    unsigned char bVolatileCollision : 1;
    unsigned char UnknownData_SEIQ : 1;
    float Time;
    FVector Location;
    FVector Normal;
    FVector ImpactPoint;
    FVector ImpactNormal;
    FVector TraceStart;
    FVector TraceEnd;
    float PenetrationDepth;
    int Item;
    TWeakObjectPtr<AActor> PhysMaterial;
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UObject> Component;
    uintptr_t BodyInstance;
    FName BoneName;
    int FaceIndex;
};

struct FWeaponData
{
public:
    bool                                                       bInfiniteAmmo;
    bool                                                       bNeverReload;
    bool                                                       bUsesAmmo;
    bool                                                       bDontTakeAmmoOnReload;
    bool                                                       bFullReloadFromSingleItem;
    bool                                                       bShowAmmoInClipAsPercent;
    bool                                                       bDontReloadEmptyClipOnEquip;
    unsigned char                                              UnknownData_54NP[0x1];
    int32_t                                                    AmmoPerClip;
    int32_t                                                    AmmoPerShot;
    bool                                                       bSilentOverflowReload;
    unsigned char                                              UnknownData_4ZD7[0x3];
    float                                                      TimeBetweenShots;
    float                                                      NoAnimReloadDuration;
    bool                                                       bForceReloadDuration;
    bool                                                       bLoopedReloadAnim;
    unsigned char                                              UnknownData_EI1W[0x2];
    float                                                      ReloadDurationPerAmmoCount;
    float                                                      ItemDurabilityToConsumePerShot;
    float                                                      HarvestResourceMultiplier;
};

class AShooterCharacter
{
public:
    PAD(0x1260);
    bool bIsTargeting : 1;
    bool bIsCrafting : 1;
    bool bIsRiding : 1;
    bool bIsIndoors : 1;
    bool bIsClimbing : 1;
    bool bIsViewingInventory : 1;
    bool bUseAlternateFallBlendspace : 1;
    bool bIsFemale : 1;
    PAD(0x397);
    FString PlatformProfileName;
};

class AShooterWeapon : public UObject
{
public:
    PAD(0x608);
    struct UPrimalItem* AssociatedPrimalItem;
    PAD(0x1D0);
    struct FWeaponData WeaponConfig;
    PAD(0x84);
    AActor* MyPawn;
    PAD(0x208);
    int32_t CurrentAmmo;
    int32_t CurrentAmmoInClip;
};

struct UPrimalInventoryComponent
{
    PAD(0x118);
    TArray<struct UPrimalItem*>                                 InventoryItems;
    TArray<struct UPrimalItem*>                                 EquippedItems;
    TArray<struct UPrimalItem*>                                 ItemSlots;
    TArray<struct UPrimalItem*>                                 ArkTributeItems;
    TArray<struct UPrimalItem*>                                 AllDyeColorItems;
    TArray<struct FItemCraftQueueEntry>                        ItemCraftQueueEntries;
    int32_t                                                    OverrideInventoryDefaultTab;
    int32_t													   InventoryTypeID;

    bool RemoveItem(const FItemNetID& ItemId, bool bDoDrop, bool bSecondryAction, bool bForceRemoval, bool showHUDMessage)
    {
        static UFunction* fn = nullptr;
        if (!fn)
            fn = UObject::FindObject<UFunction>("Function ShooterGame.PrimalInventoryComponent.RemoveItem");

        struct
        {
            struct FItemNetID                                          ItemId;
            bool                                                       bDoDrop;
            bool                                                       bSecondryAction;
            bool                                                       bForceRemoval;
            bool                                                       showHUDMessage;
            bool                                                       ReturnValue;
        } params;
        params.ItemId = ItemId;
        params.bDoDrop = bDoDrop;
        params.bSecondryAction = bSecondryAction;
        params.bForceRemoval = bForceRemoval;
        params.showHUDMessage = showHUDMessage;
        UObject::ProcessEvent(this, fn, &params);
        return params.ReturnValue;
    }

    AActor* GetOwner()
    {
        static UFunction* fn = nullptr;
        if (!fn)
            fn = UObject::FindObject<UFunction>("Function Engine.ActorComponent.GetOwner");

        struct
        {
            class AActor* ReturnValue;
        } params;
        UObject::ProcessEvent(this, fn, &params);
        return params.ReturnValue;
    }

    static UClass* StaticClass()
    {
        static UClass* ptr = nullptr;
        if (!ptr)
            ptr = UObject::FindClass("Class ShooterGame.PrimalInventoryComponent");
        return ptr;
    }
};

struct ServerSetViewingInventory_Params
{
    bool bIsViewing;
};

struct ServerNotifyShot_Params
{
    TArray<FHitResult> Impacts;
    TArray<FVector> ShootDirs;
};

struct ServerAddItemToArkInventory_Params
{
public:
    class UPrimalInventoryComponent* InventoryComp;
    struct FItemNetID ItemId;
    int32_t RequestedQuantity;
};

struct ServerAddItemFromArkInventory_Params
{
    class UPrimalInventoryComponent* InventoryComp;
    struct FItemNetID ItemId;
    int32_t RequestedQuantity;
};
