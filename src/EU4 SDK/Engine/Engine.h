#pragma once
#include <Basic/Enums.h>
#include <Basic/TArray.h>
#include <Basic/FVector2D.h>
#include <Basic/FLinearColor.h>
#include <Basic/FRotator-FVector.h>
#include <Macros.h>
#include <vector>
#include <string>
#include <locale>
#include <unordered_map>
#include <unordered_set>
#include <Windows.h>

struct FItemNetID
{
	uint32_t ItemID1;
	uint32_t ItemID2;
};

class FString : public TArray<wchar_t>
{
public:
	FString() { }

	FString(const wchar_t* other)
	{
		_max = _count = *other ? static_cast<int32_t>(wcslen(other)) + 1 : 0;
		if (_count)
			_data = const_cast<wchar_t*>(other);
	}

	const wchar_t* wc_str() const
	{
		return _data;
	}

	const char* c_str() const
	{
		return (const char*)_data;
	}

	bool IsValid() const
	{
		return _data != nullptr;
	}

	bool IsEmpty() const
	{
		return _count <= 0;
	}

	std::string ToString() const
	{
		if (!_data)
			return "error";

		try
		{
			size_t length = std::wcslen(Data());
			std::string str(length, '\0');
			std::use_facet<std::ctype<wchar_t>>(std::locale()).narrow(Data(), Data() + length, '?', &str[0]);
			return str;
		}
		catch (const std::exception&)
		{
			return "error";
		}
	}

	std::wstring ToStringW() const
	{
		std::wstring str(_data, _count);
		return str;
	}
};

class FUObjectItem
{
public:
	class UObject*	Object;
	int32_t			SerialNumber;
	int32_t			Flags;











	enum class ObjectFlags : int32_t
	{
		None = 0,
		Native = 1 << 25,
		Async = 1 << 26,
		AsyncLoading = 1 << 27,
		Unreachable = 1 << 28,
		PendingKill = 1 << 29,
		RootSet = 1 << 30,
		NoStrongReference = 1 << 31
	};

	bool IsUnreachable() const
	{
		return !!(Flags & static_cast<std::underlying_type_t<ObjectFlags>>(ObjectFlags::Unreachable));
	}

	bool IsPendingKill() const
	{
		return !!(Flags & static_cast<std::underlying_type_t<ObjectFlags>>(ObjectFlags::PendingKill));
	}











	bool IsValid() const
	{
		return this->IsUnreachable() == false && this->IsPendingKill() == false;


	}
};

class FChunkedFixedUObjectArray
{
public:
	static const constexpr int32_t		NumElementsPerChunk = 64 * 1024;
	FUObjectItem**						Objects;
	FUObjectItem*						PreAllocatedObjects;
	int32_t								MaxElements;
	int32_t								NumElements;
	int32_t								MaxChunks;
	int32_t								NumChunks;

	int32_t Count() const
	{
		return NumElements;
	}

	int32_t Max() const
	{
		return MaxElements;
	}

	bool IsValidIndex(int32_t Index) const
	{
		return Index < Count() && Index >= 0;
	}

	FUObjectItem* GetObjectPtr(int32_t Index) const
	{
		const int32_t ChunkIndex = Index / NumElementsPerChunk;
		const int32_t WithinChunkIndex = Index % NumElementsPerChunk;
		if (ChunkIndex > NumChunks || Index > MaxElements)
			return nullptr;

		FUObjectItem* Chunk = Objects[ChunkIndex];
		if (!Chunk) return nullptr;
		return Chunk + WithinChunkIndex;
	}

	UObject* GetByIndex(int32_t index) const
	{
		FUObjectItem* ItemPtr = GetObjectPtr(index);
		if (!ItemPtr) return nullptr;
		return (*ItemPtr).Object;
	}

	FUObjectItem* GetItemByIndex(int32_t index) const
	{
		FUObjectItem* ItemPtr = GetObjectPtr(index);
		if (!ItemPtr) return nullptr;
		return ItemPtr;
	}

	UObject* operator[](int32_t i)
	{
		return GetByIndex(i);
	}

	const UObject* operator[](int32_t i) const
	{
		return GetByIndex(i);
	}
};

class FNameEntry
{
private:
	int32_t		Index;
	PAD(0x4);
	FNameEntry* HashNext;
	union
	{
		char AnsiName[1024];
		wchar_t WideName[1024];
	};

public:
	const int32_t GetIndex() const
	{
		return Index >> 0x1;
	}

	bool IsWide() const
	{
		return Index & 0x1;
	}

	std::string GetAnsiName() const
	{
	return std::string(AnsiName);
	}

	std::wstring GetWideName() const
	{
		return std::wstring(WideName);
	}

	std::string GetName() const
	{
		return GetAnsiName();
	}
};

template<typename ElementType, int32_t ElementsPerChunk>
class TStaticIndirectArrayThreadSafeRead
{
public:
	ElementType**		Chunks[128];
	uint32_t			NumElements;
	uint32_t			NumChunks;

	ElementType* GetItemPtr(int32_t index) const
	{
		int32_t ChunkIndex = index / ElementsPerChunk;
		int32_t WithinChunkIndex = index % ElementsPerChunk;
		ElementType* Chunk = (*Chunks)[ChunkIndex];
		int offset = WithinChunkIndex * 0x8;
		uintptr_t ptrAddress = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(Chunk) + offset);
		ElementType* ItemPtr = reinterpret_cast<ElementType*>(ptrAddress);
		return ItemPtr;
	}

	int32_t Count()
	{
		return 238300;
	}

	bool IsValidIndex(int32_t index) const
	{
		return index < Count() && index >= 0;
	}

	ElementType* operator[](int32_t index) const
	{
		try
		{
			ElementType* ItemPtr = GetItemPtr(index);
			return ItemPtr ? ItemPtr : nullptr;
		}
		catch (...)
		{
			return nullptr;
		}
	}
};

class TNameEntryArray : public TStaticIndirectArrayThreadSafeRead<FNameEntry, 16384> {};











































class FName
{
public:
	static inline TNameEntryArray* GNames = nullptr;
	int32_t ComparisonIndex;
	uint32_t Number;

	FName()
	{
		ComparisonIndex = 0;
		Number = 0;
	}

	FName(int32_t i)
	{
		ComparisonIndex = i;
		Number = 0;
	}

	FName(const char* nameToFind)
	{
		Number = 0;
		for (int32_t i = 0; i < GetGlobalNames().Count(); ++i)
		{
			FNameEntry* Name = GetGlobalNames()[i];
			if (!Name)
				continue;

			if (Name->GetAnsiName() == nameToFind)
			{
				ComparisonIndex = i;
				return;
			}
		}
	}

	FName(const wchar_t* nameToFind)
	{
		Number = 0;
		for (int32_t i = 0; i < GetGlobalNames().Count(); ++i)
		{
			FNameEntry* Name = GetGlobalNames()[i];
			if (!Name)
				continue;

			if (Name->GetWideName() == nameToFind)
			{
				ComparisonIndex = i;
				return;
			}
		}
	}

	static TNameEntryArray& GetGlobalNames()
	{
		return *GNames;
	}

	std::string GetNameA() const
	{
		return GetGlobalNames()[ComparisonIndex]->GetAnsiName();
	}

	std::wstring GetNameW() const
	{
		return GetGlobalNames()[ComparisonIndex]->GetWideName();
	}

	std::string GetName() const
	{
		return GetNameA();
	}
};

class AActor;
class UClass;
class UFunction;
class UNetDriver;
class UWorld;
class UObject
{
public:
	static inline FChunkedFixedUObjectArray* GObjects = nullptr;
	void* VfTable;
	int32_t			Flags;
	int				InternalIndex;
	class UClass* Class;
	FName			Name;
	class UObject* Outer;


	static inline void (*oProcessRemoteFunction)(UNetDriver* Driver, AActor* Actor, UFunction* Function, void* Parameters, void* OutParms, void* Stack, UObject* SubObject) = nullptr;


	static inline void (*oProcessEvent)(void* Object, UFunction* Function, void* Parms) = nullptr;


	static inline void (*pProcessEvent)(void* Object, class UFunction* Function, void* Parms) = nullptr;

	static FChunkedFixedUObjectArray& GetGlobalObjects()
	{
		return *GObjects;
	}

	bool IsAddressInRange(void* Address) const;

	bool IsValid(bool bRecursive = false);

	std::string GetName() const
	{
		std::string name(Name.GetName());
		if (Name.Number > 0)
			name += '_' + std::to_string(Name.Number);
		auto pos = name.rfind('/');
		if (pos == std::string::npos)
			return name;
		return name.substr(pos + 1);
	}

	std::string GetFullName() const
	{
		std::string name;
		if (Class != nullptr)
		{
			std::string temp;
			for (auto p = Outer; p; p = p->Outer)
			{
				temp = p->GetName() + "." + temp;
			}
			UObject* oClass = (UObject*)Class;
			name = oClass->GetName();
			name += " ";
			name += temp;
			name += GetName();
		}
		return name;
	}


	template<typename T>
	static T* FindObject(const std::string& name)
	{
		for (int32_t i = 0; i < UObject::GetGlobalObjects().Count(); ++i)
		{
			auto object = UObject::GetGlobalObjects().GetByIndex(i);

			if (!object)
				continue;

			if (object->GetFullName() == name)
				return reinterpret_cast<T*>(object);
		}
		return nullptr;
	}

	template<typename T>
	static T* FindObject()
	{
		auto v = T::StaticClass();
		for (int32_t i = 0; i < UObject::GetGlobalObjects().Count(); ++i)
		{
			auto object = UObject::GetGlobalObjects().GetByIndex(i);

			if (!object)
				continue;

			if (object->IsA(v))
				return reinterpret_cast<T*>(object);
		}
		return nullptr;
	}

	template<typename T>
	static std::vector<T*> FindObjects(const std::string& name)
	{
		std::vector<T*> ret;
		for (int32_t i = 0; i < UObject::GetGlobalObjects().Count(); ++i)
		{
			auto object = UObject::GetGlobalObjects().GetByIndex(i);

			if (!object)
				continue;

			if (object->GetFullName() == name)
				ret.push_back(reinterpret_cast<T*>(object));
		}
		return ret;
	}

	template<typename T>
	static std::vector<T*> FindObjects(const std::vector<std::string>& Names)
	{
		std::unordered_map<std::string, T*> ObjectMap;
		std::unordered_set<std::string> NamesSet(Names.begin(), Names.end());
		size_t foundCount = 0;

		for (int i = 0; i < GetGlobalObjects().Count(); ++i)
		{
			auto Object = GetGlobalObjects().GetByIndex(i);
			if (Object != nullptr)
			{
				std::string FullName = Object->GetFullName();
				if (NamesSet.find(FullName) != NamesSet.end())
				{
					ObjectMap[FullName] = reinterpret_cast<T*>(Object);
					foundCount++;
				}
			}
		}

		std::vector<T*> Objects;
		for (const std::string& Name : Names)
		{
			auto it = ObjectMap.find(Name);
			if (it != ObjectMap.end())
			{
				if (it->second != nullptr)
					Objects.push_back(it->second);
			}
		}

		return Objects;
	}

	template<typename T>
	static std::vector<T*> FindObjects(UClass* Class)
	{
		std::vector<T*> ret;
		for (int i = 0; i < UObject::GetGlobalObjects().Count(); ++i)
		{
			auto object = UObject::GetGlobalObjects().GetByIndex(i);

			if (!object)
				continue;

			if (object->IsA(Class))
				ret.push_back(reinterpret_cast<T*>(object));
		}
		return ret;
	}

	template<typename T>
	static std::vector<T*> FindObjects()
	{
		std::vector<T*> ret;
		auto v = T::StaticClass();
		for (int i = 0; i < UObject::GetGlobalObjects().Count(); ++i)
		{
			auto object = UObject::GetGlobalObjects().GetByIndex(i);

			if (!object)
				continue;

			if (object->IsA(v))
				ret.push_back(reinterpret_cast<T*>(object));
		}
		return ret;
	}

	static UClass* FindClass(const std::string& name)
	{
		return FindObject<UClass>(name);
	}

	static UFunction* FindFunction(const std::string& name)
	{
		return FindObject<UFunction>(name);
	}

	static bool IsA(UObject* obj, class UClass* cmp);

	bool IsA(UClass* cmp) const;

	void ProcessEvent(class UFunction* function, void* parms);

	static void ProcessEvent(void* object, class UFunction* function, void* parms);
};

class UField : public UObject
{
public:
	class UField* Next;
};

class UStruct : public UField
{
public:
	class UStruct*			SuperField;
	class UField*			Children;
	int						PropertySize;
	PAD(0x4);
	TArray<unsigned char>	Script;
	int						MinAlignment;
	PAD(0x4);
	class UProperty*		PropertyLink;
	class UProperty*		RefLink;
	class UProperty*		DestructorLink;
	class UProperty*		PostConstructLink;
	TArray<class UObject*>	ScriptAndPropertyObjectReferences;
};

class UFunction : public UStruct
{
public:
	uint32_t FunctionFlags;
	uint16_t RepOffset;
	char NumParms;
	PAD(0x1);
	uint16_t ParmsSize;
	uint16_t ReturnValueOffset;
	uint16_t RPCId;
	uint16_t RPCResponseId;
	UProperty* FirstPropertyToInit;
	void* Func;
};

class UProperty : public UField
{
public:
	PAD(0x4);
	int32_t ElementSize;
	PAD(0xC);
	FName RepNotifyFunc;
	int32_t Offset;
};

class UClass : public UStruct
{
public:
	PAD(0x10);
	uint32_t ClassFlags;
	PAD(0x4);
	uint64_t ClassCastFlags;
	PAD(0x48);
	UObject* ClassDefaultObject;

	enum EClassCastFlags : uint64_t
	{
		None = 0x0000000000000000,
		Field = 0x0000000000000001,
		Int8Property = 0x0000000000000002,
		Enum = 0x0000000000000004,
		Struct = 0x0000000000000008,
		ScriptStruct = 0x0000000000000010,
		Class = 0x0000000000000020,
		ByteProperty = 0x0000000000000040,
		IntProperty = 0x0000000000000080,
		FloatProperty = 0x0000000000000100,
		UInt64Property = 0x0000000000000200,
		ClassProperty = 0x0000000000000400,
		UInt32Property = 0x0000000000000800,
		InterfaceProperty = 0x0000000000001000,
		NameProperty = 0x0000000000002000,
		StrProperty = 0x0000000000004000,
		Property = 0x0000000000008000,
		ObjectProperty = 0x0000000000010000,
		BoolProperty = 0x0000000000020000,
		UInt16Property = 0x0000000000040000,
		Function = 0x0000000000080000,
		StructProperty = 0x0000000000100000,
		ArrayProperty = 0x0000000000200000,
		Int64Property = 0x0000000000400000,
		DelegateProperty = 0x0000000000800000,
		NumericProperty = 0x0000000001000000,
		MulticastDelegateProperty = 0x0000000002000000,
		ObjectPropertyBase = 0x0000000004000000,
		WeakObjectProperty = 0x0000000008000000,
		LazyObjectProperty = 0x0000000010000000,
		SoftObjectProperty = 0x0000000020000000,
		TextProperty = 0x0000000040000000,
		Int16Property = 0x0000000080000000,
		DoubleProperty = 0x0000000100000000,
		SoftClassProperty = 0x0000000200000000,
		Package = 0x0000000400000000,
		Level = 0x0000000800000000,
		Actor = 0x0000001000000000,
		PlayerController = 0x0000002000000000,
		Pawn = 0x0000004000000000,
		SceneComponent = 0x0000008000000000,
		PrimitiveComponent = 0x0000010000000000,
		SkinnedMeshComponent = 0x0000020000000000,
		SkeletalMeshComponent = 0x0000040000000000,
		Blueprint = 0x0000080000000000,
		DelegateFunction = 0x0000100000000000,
		StaticMeshComponent = 0x0000200000000000,
		MapProperty = 0x0000400000000000,
		SetProperty = 0x0000800000000000,
		EnumProperty = 0x0001000000000000,
		SparseDelegateFunction = 0x0002000000000000,
		MulticastInlineDelegateProperty = 0x0004000000000000,
		MulticastSparseDelegateProperty = 0x0008000000000000,
		FieldPathProperty = 0x0010000000000000,
		LargeWorldCoordinatesRealProperty = 0x0080000000000000,
		OptionalProperty = 0x0100000000000000,
		VValueProperty = 0x0200000000000000,
		VerseVMClass = 0x0400000000000000,
		VRestValueProperty = 0x0800000000000000,
	};

	bool IsType(EClassCastFlags Flags) const
	{
		return (Flags != EClassCastFlags::None ? (ClassCastFlags & Flags) : true);
	}

	bool IsA(EClassCastFlags Flags) const
	{
		return (Flags != EClassCastFlags::None ? IsType(Flags) : true);
	}

	bool HasType(UClass* TypeClass)
	{
		if (TypeClass == nullptr)
			return false;

		for (UStruct* S = this; S; S = S->SuperField)
		{
			if (S == TypeClass)
				return true;
		}

		return true;
	}

	char GetPrefix(UClass* ActorClass, UClass* InterfaceClass)
	{
		if (IsA(EClassCastFlags::Class))
		{
			if (this->HasType(ActorClass))
			{
				return 'A';
			}
			else if (this->HasType(InterfaceClass))
			{
				return 'I';
			}

			return 'U';
		}
		return 'F';
	}

	template <typename Type = class UObject>
	DWORD GetPropertyOffsetEx(const char* PropertyName)
	{
		for (UProperty* Child = (UProperty*)this->Children; Child; Child = (UProperty*)Child->Next)
		{
			Type* oChild = (Type*)Child;
			if (oChild->GetName() == PropertyName)
			{
				const DWORD Offset = (DWORD)Child->Offset;
				return Offset;
			}
		}
		return -1;
	}

	DWORD GetPropertyOffset(const char* PropertyName)
	{
		return GetPropertyOffsetEx<>(PropertyName);
	}

	template <typename Type = class UObject>
	UProperty* GetPropertyEx(const char* PropertyName)
	{
		for (UProperty* Child = (UProperty*)this->Children; Child; Child = (UProperty*)Child->Next)
		{
			Type* oChild = (Type*)Child;
			if (oChild->GetName() == PropertyName)
				return Child;
		}
		return nullptr;
	}

	UProperty* GetProperty(const char* PropertyName)
	{
		return GetPropertyEx<>(PropertyName);
	}

	template <typename Type = class UObject>
	UClass* GetPropertyClassEx(const char* PropertyName)
	{
		UProperty* Property = GetProperty(PropertyName);
		Type* oProperty = (Type*)Property;
		if (oProperty)
			return oProperty->Class;

		return nullptr;
	}

	UClass* GetPropertyClass(const char* PropertyName)
	{
		return GetPropertyClassEx<>(PropertyName);
	}
};

class UObjectProperty
{
public:
	PAD(0x70);
	UClass* PropertyClass;

	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.ObjectProperty");
		return ptr;
	}
};

class UIntProperty
{
public:
	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.IntProperty");
		return ptr;
	}
};

class UUInt32Property
{
public:
	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.UInt32Property");
		return ptr;
	}
};

class UStructProperty
{
public:
	PAD(0x70);
	UStruct* Struct;
	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.StructProperty");
		return ptr;
	}
};

class UClassProperty
{
public:
	PAD(0x78);
	UClass* MetaClass;

	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.ClassProperty");
		return ptr;
	}
};

class UFloatProperty
{
public:
	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.FloatProperty");
		return ptr;
	}
};

class UByteProperty
{
public:
	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.ByteProperty");
		return ptr;
	}
};

class UNameProperty
{
public:
	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.NameProperty");
		return ptr;
	}
};

class UStrProperty
{
public:
	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.StrProperty");
		return ptr;
	}
};

class UBoolProperty
{
public:
	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class CoreUObject.BoolProperty");
		return ptr;
	}
};


class FWeakObjectPtr
{
public:
	int32_t                                                    ObjectIndex;
	int32_t                                                    ObjectSerialNumber;

	bool SerialNumbersMatch(FUObjectItem* objectItem) const
	{
		return objectItem->SerialNumber == ObjectSerialNumber;
	}

	bool IsValid() const
	{
		if (!this || ObjectSerialNumber == 0 || ObjectIndex < 0)
			return false;

		FUObjectItem* ObjectItem = UObject::GetGlobalObjects().GetItemByIndex(ObjectIndex);
		if (!ObjectItem)
			return false;

		return true;
	}

	UObject* Get() const
	{
		if (!UObject::GObjects->IsValidIndex(ObjectIndex))
			return nullptr;

		FUObjectItem* ObjectItem = UObject::GetGlobalObjects().GetItemByIndex(ObjectIndex);
		if (!ObjectItem)
			return nullptr;

		return ObjectItem->Object;
	}
};

template<class T, class TWeakObjectPtrBase = FWeakObjectPtr>
class TWeakObjectPtr : public TWeakObjectPtrBase
{
public:
	T* Get() const
	{
		return (T*)TWeakObjectPtrBase::Get();
	}

	T& operator*() const
	{
		return *Get();
	}

	T* operator->() const
	{
		return Get();
	}
};

struct FKey
{
	class FName		KeyName;
	unsigned char	UnknownData_F0L8[0x10];
};

struct FMinimalViewInfo
{
	FVector Location;
	FRotator Rotation;
	FRotator AimRotation;
	PAD(0x4);
	float FOV;
};

struct FCameraCacheEntry
{
	PAD(0x8);
	FMinimalViewInfo POV;
};

struct APlayerCameraManager
{
	PAD(0x498);
	float DefaultFOV;
	float LockedFOV;
	PAD(0x48);
	FCameraCacheEntry CameraCache;
	FCameraCacheEntry LastFrameCameraCache;

	void SetCustomDepthStencilEnabled(bool Enabled)
	{
		bool* bTargetingForceTraceFloatingHUD = reinterpret_cast<bool*>(reinterpret_cast<uint64_t>(this) + 0x1850);
		if (!bTargetingForceTraceFloatingHUD)
			return;

		if (Enabled)
			*bTargetingForceTraceFloatingHUD = true;
		else
			*bTargetingForceTraceFloatingHUD = false;
	}

	FRotator GetCameraRotation()
	{
		static auto fn = UObject::FindObject<UFunction>("Function Engine.PlayerCameraManager.GetCameraRotation");
		if (!fn) return CameraCache.POV.Rotation;
		struct { FRotator ReturnValue; } Params;
		UObject::ProcessEvent(this, fn, &Params);
		return Params.ReturnValue;
	}
	FVector GetCameraLocation()
	{
		static auto fn = UObject::FindObject<UFunction>("Function Engine.PlayerCameraManager.GetCameraLocation");
		if (!fn) return CameraCache.POV.Location;
		struct { FVector ReturnValue; } Params;
		UObject::ProcessEvent(this, fn, &Params);
		return Params.ReturnValue;
	}
};

struct FBPNetExecParams
{
	int IntParam1;
	int IntParam2;
	int IntParam3;
	float FloatParam1;
	float FloatParam2;
	float FloatParam3;
	UObject* ObjParam1;
	UObject* ObjParam2;
	UObject* ObjParam3;
	FString StringParam1;
};

struct FGuid
{
	unsigned int A;
	unsigned int B;
	unsigned int C;
	unsigned int D;
};

struct FAdminPlayerDataInfo
{
	FString PlayerName;
	FString PlayerSteamName;
	FString SteamID;
	__int64 LinkedPlayerID;
	bool IsHost;
	PAD(0x7);
};

struct FPrimalPlayerCharacterConfigStruct
{
	bool                                                       bIsFemale : 1;
	unsigned char                                              UnknownData_SDGK[0x3];
	struct FLinearColor                                        BodyColors[0x4];
	struct FLinearColor                                        OverrideHeadHairColor;
	struct FLinearColor                                        OverrideFacialHairColor;
	unsigned char                                              FacialHairIndex;
	unsigned char                                              HeadHairIndex;
	unsigned char                                              UnknownData_1KV3[0x2];
	class FString                                              PlayerCharacterName;
	float                                                      RawBoneModifiers[0x16];
	int32_t                                                    PlayerSpawnRegionIndex;
	float                                                      PercentOfFullHeadHairGrowth;
	float                                                      PercentOfFullFacialHairGrowth;
	unsigned char                                              UnknownData_2REP[0x4];
};













struct FPrimalPlayerDataStruct
{
public:
	uint64_t                                                   PlayerDataID;
	PAD(0x10);
	class FString                                              SavedNetworkAddress;
	class FString                                              PlayerName;
	uint32_t                                                   LocalPlayerIndex;
	bool                                                       bFirstSpawned : 1;
	bool                                                       bUseSpectator : 1;
	unsigned char                                              UnknownData_PX1B[0x3];
	struct FPrimalPlayerCharacterConfigStruct                  MyPlayerCharacterConfig;
	int32_t                                                    LastPinCodeUsed;
	unsigned char                                              UnknownData_QXVR[0x4];
	PAD(0x2C8);
	int32_t                                                    NumPersonalDinos;
	int32_t                                                    TribeId;
	TArray<int32_t>                                            AppIDSet;
	int32_t                                                    PlayerDataVersion;
	unsigned char                                              UnknownData_1TRD[0x4];
	double                                                     NextAllowedRespawnTime;
	double                                                     LastTimeDiedToEnemyTeam;
	double                                                     LoginTime;
	double                                                     LastLoginTime;
	float                                                      AllowedRespawnInterval;
	float                                                      NumOfDeaths;
	int32_t                                                    SpawnDayNumber;
	float                                                      SpawnDayTime;
	double                                                     LastInventoryRetrievalUTCTime;
	PAD(0x10);
};

class APlayerState
{
public:
	PAD(0x538);
	void* MyPlayerData;
	FPrimalPlayerDataStruct MyPlayerDataStruct;
};

struct FItemNetInfo
{
	PAD(0x1B0);
};

class ULocalPlayer;
class APlayerController
{
public:
	PAD(0x498);
	class APlayerState* PlayerState;
	PAD(0x8);
	FRotator					ControlRotation;
	PAD(0x14);
	ULocalPlayer* Player;
	PAD(0x18);
	class APawn* AcknowledgedPawn;
	PAD(0x18);
	class APlayerCameraManager* PlayerCameraManager;
	PAD(0xC);
	struct FRotator				TargetViewRotation;
	PAD(0x12C);
	bool						bShowMouseCursor : 1;
	bool						bForceShowMouseCursor : 1;
	bool						bEnableClickEvents : 1;
	bool						bEnableTouchEvents : 1;
	bool						bEnableMouseOverEvents : 1;
	bool						bEnableTouchOverEvents : 1;
	bool						bForceFeedbackEnabled : 1;
	PAD(0x3DF);
	TWeakObjectPtr<AActor>		LastControlledPlayerCharacter;
	PAD(0x4);
	class UClass* StructurePlacerClass;
	PAD(0x5F0);
	long double LastClientModifiedARKInventoryTime;

	void ServerTransferAllFromRemoteInventory(void* InventoryComp, const FString& CurrentCustomFolderFilter, const FString& CurrentNameFilter, const FString& CurrentDestinationFolder, bool bNoFolderView)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function ShooterGame.ShooterPlayerController.ServerTransferAllFromRemoteInventory");

		struct
		{
			void* InventoryComp;
			FString CurrentCustomFolderFilter;
			FString CurrentNameFilter;
			FString CurrentDestinationFolder;
			bool bNoFolderView;
		} params;
		params.InventoryComp = InventoryComp;
		params.CurrentCustomFolderFilter = CurrentCustomFolderFilter;
		params.CurrentNameFilter = CurrentNameFilter;
		params.CurrentDestinationFolder = CurrentDestinationFolder;
		params.bNoFolderView = bNoFolderView;
		UObject::ProcessEvent(this, fn, &params);
	}


	static inline FString* (*pGetUniqueNetIdAsString)(APlayerController* Controller, FString* Result) = nullptr;

	FString GetGamertagAsString()
	{
		if (!this->PlayerState)
			return {};

		return this->PlayerState->MyPlayerDataStruct.PlayerName;
	}

	FString GetUniqueNetIDAsString()
	{
		if (!pGetUniqueNetIdAsString)
			return {};

		FString Result = {};
		pGetUniqueNetIdAsString(this, &Result);
		return Result;
	}

	AActor* GetPlayerCharacter()
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function ShooterGame.ShooterPlayerController.GetPlayerCharacter");

		struct AShooterPlayerController_GetPlayerCharacter_Params
		{
			AActor* ReturnValue;
		} params;
		UObject::ProcessEvent(this, fn, &params);
		return params.ReturnValue;
	}

	void ServerActorViewRemoteInventory(void* InventoryComp)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function ShooterGame.ShooterPlayerController.ServerActorViewRemoteInventory");
		struct
		{
			void* InventoryComp;
		} params;
		params.InventoryComp = InventoryComp;
		UObject::ProcessEvent(this, fn, &params);
	}

	void ServerRequestActorItems(void* forInventory, bool bInventoryItems, bool bIsFirstSpawn)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function ShooterGame.ShooterPlayerController.ServerRequestActorItems");

		struct AShooterPlayerController_ServerRequestActorItems_Params
		{
			void* forInventory;
			bool bInventoryItems;
			bool bIsFirstSpawn;
		} params;
		params.forInventory = forInventory;
		params.bInventoryItems = bInventoryItems;
		params.bIsFirstSpawn = bIsFirstSpawn;
		UObject::ProcessEvent(this, fn, &params);
	}

	void ServerLoadArkInventoryItems(TArray<struct FItemNetInfo> ArkInventoryItems, bool bFinalBatch)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function ShooterGame.ShooterPlayerController.ServerLoadArkInventoryItems");

		struct AShooterPlayerController_ServerLoadArkInventoryItems_Params
		{
			TArray<struct FItemNetInfo>                                ArkInventoryItems;
			bool                                                       bFinalBatch;
		} params;
		params.ArkInventoryItems = ArkInventoryItems;
		params.bFinalBatch = bFinalBatch;
		UObject::ProcessEvent(this, fn, &params);
	}

	void ServerRefreshSteamInventory()
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function ShooterGame.ShooterPlayerController.ServerRefreshSteamInventory");

		struct {} params;
		UObject::ProcessEvent(this, fn, &params);
	}
};

struct FBoxSphereBounds
{
	FVector Origin;
	FVector BoxExtent;
	float SphereRadius;

	__forceinline FVector GetCenter() const { return Origin; }

	__forceinline FVector GetExtent() const { return BoxExtent; }

	__forceinline FVector GetTopCenter() { return Origin + FVector(0.0f, 0.0f, BoxExtent.Z); }

	__forceinline FVector GetBottomCenter() { return Origin - FVector(0.0f, 0.0f, BoxExtent.Z); }
};

struct USceneComponent
{
	PAD(0x118);
	FBoxSphereBounds Bounds;
	PAD(0x24);
	FVector  RelativeLocation;
};

class AActor : public UObject
{
public:
	PAD(0x208);
	int					TargetingTeam;
	PAD(0x34);
	USceneComponent*	RootComponent;
	PAD(0x12C0);
	FString				PlayerName;
	PAD(0xB8);
	FString				PlatformProfileName;

	bool IsPlayer() const
	{
		static auto Class = UObject::FindClass("Class ShooterGame.ShooterCharacter");
		return IsA(Class);
	}

	bool IsDino() const
	{
		static auto Class = UObject::FindClass("Class ShooterGame.PrimalDinoCharacter");
		return IsA(Class);
	}

	bool IsCorruptDino() const
	{
		static UFunction* fn = nullptr;
		if (!fn) fn = UObject::FindObject<UFunction>("Function Engine.Actor.IsCorruptDino");
		if (!fn) return false;
		struct { bool ReturnValue; } p{ false };
		UObject::ProcessEvent(const_cast<AActor*>(this), fn, &p);
		return p.ReturnValue;
	}

	bool IsDead() const
	{
		static UFunction* fn = nullptr;
		if (!fn) fn = UObject::FindObject<UFunction>("Function Engine.Actor.IsDead");
		if (!fn) return false;
		struct { bool ReturnValue; } p{ false };
		UObject::ProcessEvent(const_cast<AActor*>(this), fn, &p);
		return p.ReturnValue;
	}

	bool IsStructure() const
	{
		static auto Class = UObject::FindClass("Class ShooterGame.PrimalStructure");
		return IsA(Class);
	}

	bool IsPlayerMale() const
	{
		BYTE* bIsFemale = reinterpret_cast<BYTE*>(reinterpret_cast<uint64_t>(this) + 0x1260);
		if (bIsFemale && *bIsFemale & BitEight)
			return false;

		return true;
	}

	bool IsAlive()
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function ShooterGame.PrimalCharacter.IsAlive");

		if (!this || !fn)
			return false;

		struct { bool ReturnValue; } Params;
		UObject::ProcessEvent(this, fn, &Params);
		return Params.ReturnValue;
	}

	bool IsConscious() const
	{
		BYTE* bIsSleeping = reinterpret_cast<BYTE*>(reinterpret_cast<uint64_t>(this) + 0x8B4);
		if (bIsSleeping && !(*bIsSleeping & BitOne))
			return true;

		return false;
	}

	bool IsTamed() const
	{
		return !(this->TargetingTeam < 50000);
	}

	APlayerController* GetController()
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.Pawn.GetController");

		struct { APlayerController* ReturnValue; } params;
		UObject::ProcessEvent(this, fn, &params);
		return params.ReturnValue;
	}

	template <typename Type>
	Type* GetCharacterWeapon()
	{
		return *reinterpret_cast<Type**>(reinterpret_cast<uint64_t>(this) + 0x1788);
	}
};


class UGameInstance : UObject
{
public:

	PAD(0x10);
	TArray<class ULocalPlayer*> LocalPlayers;

	void AsyncLoadAssetFromPath(const class FString& AssetPath)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function ShooterGame.ShooterGameInstance.AsyncLoadAssetFromPath");

		struct { class FString AssetPath; } Params;
		Params.AssetPath = AssetPath;
		UObject::ProcessEvent(this, fn, &Params);
	}
};

class ULevelBase : public UObject
{
public:
	PAD(0x60);
	TArray<AActor*> Actors;
};

class ULevel : public ULevelBase { };

class AGameState
{
public:
	PAD(0x598);
	double NetworkTime;
	PAD(0x368);
	class FString ClusterId;
};

class AWorldSettings
{
public:
	PAD(0x570);
	TArray<AActor*> ActorLists[33];
};

class UWorld : public UObject
{
public:
	static inline UWorld**	GWorld = nullptr;
	PAD(0xD0);
	class ULevel*			PersistentLevel;
	PAD(0x28);
	class AGameState*		GameState;
	PAD(0x138);
	TArray<class ULevel*>	Levels;
	PAD(0x18);
	class UGameInstance*	OwningGameInstance;

	static UWorld* Get()
	{
		if (!UWorld::GWorld)
			return nullptr;

		return *UWorld::GWorld;
	}

	static bool IsMainMenu()
	{
		UWorld* World = Get();
		if (!World)
			return false;

		return UWorld::Get()->Name.ComparisonIndex == 558;
	}
};

class ULocalPlayer
{
public:
	PAD(0x30);
	class APlayerController* PlayerController;
	int32_t						CurrentNetSpeed;
	int32_t						ConfiguredInternetSpeed;
	int32_t						ConfiguredLanSpeed;
	PAD(0x24);
	class UGameViewportClient* ViewportClient;

	static ULocalPlayer* Get()
	{
		try {
			class UWorld* World = UWorld::Get();
			if (!World || !World->OwningGameInstance)
				return nullptr;

			return World->OwningGameInstance->LocalPlayers[0];
		}
		catch (...) {
			return nullptr;
		}
		return nullptr;
	}

	static AActor* GetActor()
	{
		try {
			ULocalPlayer* LP = ULocalPlayer::Get();
			if (!LP)
				return nullptr;

			APlayerController* LPC = LP->PlayerController;
			if (!LPC)
				return nullptr;

			return LPC->GetPlayerCharacter();
		}
		catch (...) {
			return nullptr;
		}
		return nullptr;
	}
};
struct FEngineShowFlags
{
	unsigned __int32 Bloom : 1;
	unsigned __int32 Tonemapper : 1;
	unsigned __int32 AntiAliasing : 1;
	unsigned __int32 TemporalAA : 1;
	unsigned __int32 AmbientCubemap : 1;
	unsigned __int32 EyeAdaptation : 1;
	unsigned __int32 VisualizeHDR : 1;
	unsigned __int32 LensFlares : 1;
	unsigned __int32 GlobalIllumination : 1;
	unsigned __int32 Vignette : 1;
	unsigned __int32 Grain : 1;
	unsigned __int32 AmbientOcclusion : 1;
	unsigned __int32 Decals : 1;
	unsigned __int32 CameraImperfections : 1;
	unsigned __int32 OnScreenDebug : 1;
	unsigned __int32 OverrideDiffuseAndSpecular : 1;
	unsigned __int32 ReflectionOverride : 1;
	unsigned __int32 VisualizeBuffer : 1;
	unsigned __int32 ColorGrading : 1;
	unsigned __int32 VectorFields : 1;
	unsigned __int32 DepthOfField : 1;
	unsigned __int32 ShadowsFromEditorHiddenActors : 1;
	unsigned __int32 GBufferHints : 1;
	unsigned __int32 MotionBlur : 1;
	unsigned __int32 CompositeEditorPrimitives : 1;
	unsigned __int32 TestImage : 1;
	unsigned __int32 VisualizeDOF : 1;
	unsigned __int32 VisualizeAdaptiveDOF : 1;
	unsigned __int32 VertexColors : 1;
	unsigned __int32 Refraction : 1;
	unsigned __int32 CameraInterpolation : 1;
	unsigned __int32 SceneColorFringe : 1;
	unsigned __int32 SeparateTranslucency : 1;
	unsigned __int32 ScreenPercentage : 1;
	unsigned __int32 VisualizeMotionBlur : 1;
	unsigned __int32 ReflectionEnvironment : 1;
	unsigned __int32 VisualizeOutOfBoundsPixels : 1;
	unsigned __int32 Diffuse : 1;
	unsigned __int32 Specular : 1;
	unsigned __int32 SelectionOutline : 1;
	unsigned __int32 ScreenSpaceReflections : 1;
	unsigned __int32 IndirectLightingCache : 1;
	unsigned __int32 DebugAI : 1;
	unsigned __int32 VisLog : 1;
	unsigned __int32 Navigation : 1;
	unsigned __int32 GameplayDebug : 1;
	unsigned __int32 TexturedLightProfiles : 1;
	unsigned __int32 LightFunctions : 1;
	unsigned __int32 Tessellation : 1;
	unsigned __int32 InstancedStaticMeshes : 1;
	unsigned __int32 DynamicShadows : 1;
	unsigned __int32 Particles : 1;
	unsigned __int32 SkeletalMeshes : 1;
	unsigned __int32 BuilderBrush : 1;
	unsigned __int32 Translucency : 1;
	unsigned __int32 BillboardSprites : 1;
	unsigned __int32 LOD : 1;
	unsigned __int32 LightComplexity : 1;
	unsigned __int32 ShaderComplexity : 1;
	unsigned __int32 StationaryLightOverlap : 1;
	unsigned __int32 LightMapDensity : 1;
	unsigned __int32 StreamingBounds : 1;
	unsigned __int32 Constraints : 1;
	unsigned __int32 CameraFrustums : 1;
	unsigned __int32 AudioRadius : 1;
	unsigned __int32 BSPSplit : 1;
	unsigned __int32 Brushes : 1;
	unsigned __int32 Lighting : 1;
	unsigned __int32 DeferredLighting : 1;
	unsigned __int32 Editor : 1;
	unsigned __int32 BSPTriangles : 1;
	unsigned __int32 LargeVertices : 1;
	unsigned __int32 Grid : 1;
	unsigned __int32 Snap : 1;
	unsigned __int32 MeshEdges : 1;
	unsigned __int32 Cover : 1;
	unsigned __int32 Splines : 1;
	unsigned __int32 Selection : 1;
	unsigned __int32 ModeWidgets : 1;
	unsigned __int32 Bounds : 1;
	unsigned __int32 HitProxies : 1;
	unsigned __int32 PropertyColoration : 1;
	unsigned __int32 LightInfluences : 1;
	unsigned __int32 Pivot : 1;
	unsigned __int32 ShadowFrustums : 1;
	unsigned __int32 Wireframe : 1;
	unsigned __int32 Materials : 1;
	unsigned __int32 StaticMeshes : 1;
	unsigned __int32 Landscape : 1;
	unsigned __int32 LightRadius : 1;
	unsigned __int32 Fog : 1;
	unsigned __int32 Volumes : 1;
	unsigned __int32 Game : 1;
	unsigned __int32 LevelColoration : 1;
	unsigned __int32 BSP : 1;
	unsigned __int32 Collision : 1;
	unsigned __int32 CollisionVisibility : 1;
	unsigned __int32 CollisionPawn : 1;
	unsigned __int32 LightShafts : 1;
	unsigned __int32 PostProcessMaterial : 1;
	unsigned __int32 Atmosphere : 1;
	unsigned __int32 CameraAspectRatioBars : 1;
	unsigned __int32 CameraSafeFrames : 1;
	unsigned __int32 TextRender : 1;
	unsigned __int32 Rendering : 1;
	unsigned __int32 HighResScreenshotMask : 1;
	unsigned __int32 HMDDistortion : 1;
	unsigned __int32 StereoRendering : 1;
	unsigned __int32 DistanceCulledPrimitives : 1;
	unsigned __int32 VisualizeLightCulling : 1;
	unsigned __int32 PrecomputedVisibility : 1;
	unsigned __int32 SkyLighting : 1;
	unsigned __int32 VisualizeLPV : 1;
	unsigned __int32 PreviewShadowsIndicator : 1;
	unsigned __int32 PrecomputedVisibilityCells : 1;
	unsigned __int32 VolumeLightingSamples : 1;
	unsigned __int32 LpvLightingOnly : 1;
	unsigned __int32 Paper2DSprites : 1;
	unsigned __int32 VisualizeDistanceFieldAO : 1;
	unsigned __int32 VisualizeDistanceFieldGI : 1;
	unsigned __int32 VisualizeMeshDistanceFields : 1;
	unsigned __int32 VisualizeGlobalDistanceField : 1;
	unsigned __int32 DistanceFieldAO : 1;
	unsigned __int32 DistanceFieldGI : 1;
	unsigned __int32 ForceGBuffer : 1;
	unsigned __int32 VisualizeSenses : 1;
	unsigned __int32 TrueSky : 1;
	unsigned __int32 GroundClutter : 1;
	unsigned __int32 AllowMaskedZEqual : 1;
};

struct UGameViewportClient
{
	PAD(0x38);
	TArray<UObject*> ViewPortWidgets;
	PAD(0x68);
	int32_t ViewModeIndex;
	FEngineShowFlags EngineShowFlags;
};

class UCanvas : public UObject
{
public:
	PAD(0x38);
	class UTexture2D* DefaultTexture;
	class UTexture2D* GradientTexture0;

	struct FVector2D K2_StrLen(class UFont* RenderFont, const class FString& RenderText)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindFunction("Function Engine.Canvas.K2_StrLen");

		struct { class UFont* RenderFont; class FString RenderText; struct FVector2D ReturnValue; } Params;
		Params.RenderFont = RenderFont;
		Params.RenderText = RenderText;
		UObject::ProcessEvent(fn, &Params);
		return Params.ReturnValue;
	}




	struct FVector K2_Project(const struct FVector& WorldLocation)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindFunction("Function Engine.Canvas.K2_Project");

		struct { struct FVector WorldLocation; struct FVector ReturnValue; } Params;
		Params.WorldLocation = WorldLocation;
		UObject::ProcessEvent(fn, &Params);
		return Params.ReturnValue;
	}

	void K2_DrawLine(const struct FVector2D& ScreenPositionA, const struct FVector2D& ScreenPositionB, float Thickness, const struct FLinearColor& RenderColor)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindFunction("Function Engine.Canvas.K2_DrawLine");

		struct { FVector2D ScreenPositionA; FVector2D ScreenPositionB; float Thickness; FLinearColor RenderColor; } Params;
		Params.ScreenPositionA = ScreenPositionA;
		Params.ScreenPositionB = ScreenPositionB;
		Params.Thickness = Thickness;
		Params.RenderColor = RenderColor;
		UObject::ProcessEvent(fn, &Params);
	}

	void K2_DrawTexture(class UTexture* RenderTexture, const struct FVector2D& ScreenPosition, const struct FVector2D& ScreenSize, const struct FVector2D& CoordinatePosition, const struct FVector2D& CoordinateSize, const struct FLinearColor& RenderColor, EBlendMode BlendMode, float Rotation, const struct FVector2D& PivotPoint)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindFunction("Function Engine.Canvas.K2_DrawTexture");

		struct { class UTexture* RenderTexture; struct FVector2D ScreenPosition; struct FVector2D ScreenSize; struct FVector2D CoordinatePosition; struct FVector2D CoordinateSize; struct FLinearColor RenderColor; EBlendMode BlendMode; unsigned char UnknownData_Q2GI[0x3]; float Rotation; struct FVector2D PivotPoint; } Params;
		Params.RenderTexture = RenderTexture;
		Params.ScreenPosition = ScreenPosition;
		Params.ScreenSize = ScreenSize;
		Params.CoordinatePosition = CoordinatePosition;
		Params.CoordinateSize = CoordinateSize;
		Params.RenderColor = RenderColor;
		Params.BlendMode = BlendMode;
		Params.Rotation = Rotation;
		Params.PivotPoint = PivotPoint;
		UObject::ProcessEvent(fn, &Params);
	}

	void K2_DrawMaterial(class UMaterialInterface* RenderMaterial, const struct FVector2D& ScreenPosition, const struct FVector2D& ScreenSize, const struct FVector2D& CoordinatePosition, const struct FVector2D& CoordinateSize, float Rotation, const struct FVector2D& PivotPoint)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindFunction("Function Engine.Canvas.K2_DrawMaterial");

		struct { class UMaterialInterface* RenderMaterial; FVector2D ScreenPosition; FVector2D ScreenSize; FVector2D CoordinatePosition; FVector2D CoordinateSize; float Rotation; FVector2D PivotPoint; } Params;
		Params.RenderMaterial = RenderMaterial;
		Params.ScreenPosition = ScreenPosition;
		Params.ScreenSize = ScreenSize;
		Params.CoordinatePosition = CoordinatePosition;
		Params.CoordinateSize = CoordinateSize;
		Params.Rotation = Rotation;
		Params.PivotPoint = PivotPoint;
		UObject::ProcessEvent(fn, &Params);
	}

	void K2_DrawText(class UFont* RenderFont, const FString& RenderText, const FVector2D& ScreenPosition, const FLinearColor& RenderColor, float Kerning, const FLinearColor& ShadowColor, const FVector2D& ShadowOffset, bool bCentreX, bool bCentreY, bool bOutlined, const FLinearColor& OutlineColor)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindFunction("Function Engine.Canvas.K2_DrawText");

		struct { class UFont* RenderFont; FString RenderText; FVector2D ScreenPosition; FLinearColor RenderColor; float Kerning; struct FLinearColor ShadowColor; FVector2D ShadowOffset; bool bCentreX; bool bCentreY; bool bOutlined; unsigned char UnknownData_WOTH[0x1]; FLinearColor OutlineColor; } Params;
		Params.RenderFont = RenderFont;
		Params.RenderText = RenderText;
		Params.ScreenPosition = ScreenPosition;
		Params.RenderColor = RenderColor;
		Params.Kerning = Kerning;
		Params.ShadowColor = ShadowColor;
		Params.ShadowOffset = ShadowOffset;
		Params.bCentreX = bCentreX;
		Params.bCentreY = bCentreY;
		Params.bOutlined = bOutlined;
		Params.OutlineColor = OutlineColor;
		UObject::ProcessEvent(fn, &Params);
	}

	void K2_DrawPolygon(class UTexture* RenderTexture, const struct FVector2D& ScreenPosition, const struct FVector2D& Radius, int32_t NumberOfSides, const struct FLinearColor& RenderColor)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindFunction("Function Engine.Canvas.K2_DrawPolygon");

		struct { class UTexture* RenderTexture; struct FVector2D ScreenPosition; struct FVector2D Radius; int32_t NumberOfSides; struct FLinearColor RenderColor; } Params;
		Params.RenderTexture = RenderTexture;
		Params.ScreenPosition = ScreenPosition;
		Params.Radius = Radius;
		Params.NumberOfSides = NumberOfSides;
		Params.RenderColor = RenderColor;
		UObject::ProcessEvent(fn, &Params);
	}
};

struct UUI_Inventory
{
	PAD(0x59C);
	TWeakObjectPtr<UObject> ViewingInventoryComp;
};

class AHUD
{
public:
	PAD(0x490);
	class APlayerController* PlayerOwner;
	PAD(0x40);
	class UCanvas* Canvas;
	class UCanvas* DebugCanvas;
	PAD(0x5D0);
	UUI_Inventory* CurrentOpenedInventory;

	void GetTextSize(const class FString& Text, float* OutWidth, float* OutHeight, class UFont* Font, float Scale)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.HUD.GetTextSize");

		struct { class FString Text; float OutWidth; float OutHeight; class UFont* Font; float Scale; } Params;
		Params.Text = Text;
		Params.Font = Font;
		Params.Scale = Scale;
		UObject::ProcessEvent(this, fn, &Params);

		if (OutWidth != nullptr)
			*OutWidth = Params.OutWidth;
		if (OutHeight != nullptr)
			*OutHeight = Params.OutHeight;
	}

	void DrawTextureSimple(class UTexture* Texture, float ScreenX, float ScreenY, float Scale, bool bScalePosition)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.HUD.DrawTextureSimple");

		struct { class UTexture* Texture; float ScreenX; float ScreenY; float Scale; bool bScalePosition; } Params;
		Params.Texture = Texture;
		Params.ScreenX = ScreenX;
		Params.ScreenY = ScreenY;
		Params.Scale = Scale;
		Params.bScalePosition = bScalePosition;
		UObject::ProcessEvent(this, fn, &Params);
	}

	void DrawTexture(class UTexture* Texture, float ScreenX, float ScreenY, float ScreenW, float ScreenH, float TextureU, float TextureV, float TextureUWidth, float TextureVHeight, const struct FLinearColor& TintColor, EBlendMode BlendMode, float Scale, bool bScalePosition, float Rotation, const struct FVector2D& RotPivot)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.HUD.DrawTexture");

		struct { class UTexture* Texture; float ScreenX; float ScreenY; float ScreenW; float ScreenH; float TextureU; float TextureV; float TextureUWidth; float TextureVHeight; struct FLinearColor TintColor; EBlendMode BlendMode; unsigned char UnknownData_OBGP[0x3]; float Scale; bool bScalePosition; unsigned char UnknownData_T9JH[0x3]; float Rotation; struct FVector2D RotPivot; } Params;
		Params.Texture = Texture;
		Params.ScreenX = ScreenX;
		Params.ScreenY = ScreenY;
		Params.ScreenW = ScreenW;
		Params.ScreenH = ScreenH;
		Params.TextureU = TextureU;
		Params.TextureV = TextureV;
		Params.TextureUWidth = TextureUWidth;
		Params.TextureVHeight = TextureVHeight;
		Params.TintColor = TintColor;
		Params.BlendMode = BlendMode;
		Params.Scale = Scale;
		Params.bScalePosition = bScalePosition;
		Params.Rotation = Rotation;
		Params.RotPivot = RotPivot;
		UObject::ProcessEvent(this, fn, &Params);
	}

	void HDrawText(const class FString& Text, const struct FLinearColor& TextColor, float ScreenX, float ScreenY, class UFont* Font, float Scale, bool bScalePosition)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.HUD.DrawText");

		struct { class FString Text; struct FLinearColor TextColor; float ScreenX; float ScreenY; class UFont* Font; float Scale; bool bScalePosition; } Params;
		Params.Text = Text;
		Params.TextColor = TextColor;
		Params.ScreenX = ScreenX;
		Params.ScreenY = ScreenY;
		Params.Font = Font;
		Params.Scale = Scale;
		Params.bScalePosition = bScalePosition;
		UObject::ProcessEvent(this, fn, &Params);
	}

	void DrawRect(const struct FLinearColor& RectColor, float ScreenX, float ScreenY, float ScreenW, float ScreenH)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.HUD.DrawRect");

		struct { struct FLinearColor RectColor; float ScreenX; float ScreenY; float ScreenW; float ScreenH; } Params;
		Params.RectColor = RectColor;
		Params.ScreenX = ScreenX;
		Params.ScreenY = ScreenY;
		Params.ScreenW = ScreenW;
		Params.ScreenH = ScreenH;
		UObject::ProcessEvent(this, fn, &Params);
	}

	void DrawMaterialSimple(class UMaterialInterface* Material, float ScreenX, float ScreenY, float ScreenW, float ScreenH, float Scale, bool bScalePosition)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.HUD.DrawMaterialSimple");

		struct { class UMaterialInterface* Material; float ScreenX; float ScreenY; float ScreenW; float ScreenH; float Scale; bool bScalePosition; } Params;
		Params.Material = Material;
		Params.ScreenX = ScreenX;
		Params.ScreenY = ScreenY;
		Params.ScreenW = ScreenW;
		Params.ScreenH = ScreenH;
		Params.Scale = Scale;
		Params.bScalePosition = bScalePosition;
		UObject::ProcessEvent(this, fn, &Params);
	}

	void DrawMaterial(class UMaterialInterface* Material, float ScreenX, float ScreenY, float ScreenW, float ScreenH, float MaterialU, float MaterialV, float MaterialUWidth, float MaterialVHeight, float Scale, bool bScalePosition, float Rotation, const struct FVector2D& RotPivot)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.HUD.DrawMaterial");

		struct { class UMaterialInterface* Material; float ScreenX; float ScreenY; float ScreenW; float ScreenH; float MaterialU; float MaterialV; float MaterialUWidth; float MaterialVHeight; float Scale; bool bScalePosition; float Rotation; struct FVector2D RotPivot; } Params;
		Params.Material = Material;
		Params.ScreenX = ScreenX;
		Params.ScreenY = ScreenY;
		Params.ScreenW = ScreenW;
		Params.ScreenH = ScreenH;
		Params.MaterialU = MaterialU;
		Params.MaterialV = MaterialV;
		Params.MaterialUWidth = MaterialUWidth;
		Params.MaterialVHeight = MaterialVHeight;
		Params.Scale = Scale;
		Params.bScalePosition = bScalePosition;
		Params.Rotation = Rotation;
		Params.RotPivot = RotPivot;
		UObject::ProcessEvent(this, fn, &Params);
	}

	void DrawLine(float StartScreenX, float StartScreenY, float EndScreenX, float EndScreenY, const struct FLinearColor& LineColor)
	{
		static UFunction* fn = nullptr;
		if (!fn)
			fn = UObject::FindObject<UFunction>("Function Engine.HUD.DrawLine");

		struct { float StartScreenX; float StartScreenY; float EndScreenX; float EndScreenY; struct FLinearColor LineColor; } Params;
		Params.StartScreenX = StartScreenX;
		Params.StartScreenY = StartScreenY;
		Params.EndScreenX = EndScreenX;
		Params.EndScreenY = EndScreenY;
		Params.LineColor = LineColor;
		UObject::ProcessEvent(this, fn, &Params);
	}
};

class UFont : public UObject
{
public:
	PAD(0x128);
	float ScalingFactor;

	static UClass* StaticClass()
	{
		static UClass* ptr = nullptr;
		if (!ptr)
			ptr = UObject::FindClass("Class Engine.KismetRenderingLibrary");
		return ptr;
	}
};

class UTexture : public UObject {};

class UTexture2D
{
public:
	PAD(0xCC);
	int32_t	SizeX;
	int32_t	SizeY;
	int32_t	OriginalSizeX;
	int32_t	OriginalSizeY;
	int32_t	ImportedSizeX;
	int32_t	ImportedSizeY;
};