#pragma once
#include <Basic/FRotator-FVector.h>

struct FPlane : public FVector
{
    float W;
};

struct FMatrix
{
    struct FPlane XPlane;
    struct FPlane YPlane;
    struct FPlane ZPlane;
    struct FPlane WPlane;
};