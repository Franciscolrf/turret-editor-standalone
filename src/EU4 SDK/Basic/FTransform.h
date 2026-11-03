#pragma once
#include "FQuat.h"
#include "FRotator-FVector.h"

struct FTransform
{
	FQuat Rotation;
	FVector Translation;
	FVector Scale3D;
};