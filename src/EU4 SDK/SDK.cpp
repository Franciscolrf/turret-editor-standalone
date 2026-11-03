#include <SDK.h>

bool UObject::IsAddressInRange(void* Address) const
{
	return !((reinterpret_cast<uintptr_t>(Address) % alignof(UObject)) != 0);
}

bool UObject::IsValid(bool bRecursive)
{
    // (Re-written from garbled decompiler output — the original truncated
    // `this`/`Class`/`ClassDefaultObject` pointers to unsigned char before
    // testing alignment, which is meaningless on a 64-bit pointer and was
    // flagged by the compiler. Same checks, done on the actual pointers.)
    try
    {
        if (reinterpret_cast<uintptr_t>(this) < 0x100)
            return false;
        if (!IsAddressInRange(this))
            return false;
        if (!VfTable)
            return false;
        if (bRecursive && !IsValid(false))
            return false;

        UClass* v5 = Class;
        if (!v5 || !IsAddressInRange(v5))
            return false;

        UObject* v6 = v5->ClassDefaultObject;
        if (!v6 || !IsAddressInRange(v6))
            return false;

        if (InternalIndex < 0 || InternalIndex >= UObject::GetGlobalObjects().NumElements)
            return false;

        const int32_t nameIndex = Name.ComparisonIndex;
        if (nameIndex < 0 || nameIndex >= FName::GetGlobalNames().Count())
            return false;

        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool UObject::IsA(UObject* obj, class UClass* cmp)
{
	for (auto super = obj->Class; super; super = static_cast<UClass*>(super->SuperField))
	{
		if (super == cmp)
			return true;
	}
	return false;
}

bool UObject::IsA(UClass* cmp) const
{
	for (auto super = Class; super; super = static_cast<UClass*>(super->SuperField))
	{
		if (super == cmp)
			return true;
	}
	return false;
}

void UObject::ProcessEvent(class UFunction* function, void* parms)
{
    pProcessEvent(static_cast<void*>(this), function, parms);
}

void UObject::ProcessEvent(void* object, class UFunction* function, void* parms)
{
    pProcessEvent(object, function, parms);
}