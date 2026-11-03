#pragma once
#include <math.h>

#define SMALL_NUMBER	(1.e-8f)

struct FVector2D
{
	float X;
	float Y;

	FVector2D()
	{
		X = 0;
		Y = 0;
	}

	FVector2D(float x, float y)
	{
		X = x;
		Y = y;
	}

	bool IsEmpty()
	{
		return (X == 0 && Y == 0);
	}

	FVector2D Expand(const FVector2D& Size)
	{
		const FVector2D HalfSize = Size * 0.5f;
		return { X + HalfSize.X, Y + HalfSize.Y };
	}

	float Distance(FVector2D& v) const
	{
		return pow(v.X - X, 2) + pow(v.Y - Y, 2);
	}

	float GetHeight() const
	{
		return sqrt(X * X + Y * Y);
	}

	float InvSqrt(float Value)
	{
		return 1.0f / sqrt(Value);
	}

	float SizeSquared(const FVector2D& InVec) const
	{
		return InVec.X * InVec.X + InVec.Y * InVec.Y;
	}

	FVector2D GetSafeNormal(const FVector2D& InVec, float Tolerance = SMALL_NUMBER)
	{
		float SquareSum = SizeSquared(InVec);
		if (SquareSum < Tolerance * Tolerance)
			return {};
		else
		{

			float Scale = InvSqrt(SquareSum);
			return InVec * Scale;
		}
	}

	FVector2D operator +(const FVector2D& other) const
	{
		return FVector2D(X + other.X, Y + other.Y);
	}

	FVector2D operator +(float scalar) const
	{
		return FVector2D(X + scalar, Y + scalar);
	}

	FVector2D operator -(float scalar) const
	{
		return FVector2D(X - scalar, Y - scalar);
	}

	FVector2D operator -(const FVector2D& other) const
	{
		return FVector2D(X - other.X, Y - other.Y);
	}

	FVector2D operator *(float scalar) const
	{
		return FVector2D(X * scalar, Y * scalar);
	}

	FVector2D operator *(const FVector2D& other) const
	{
		return FVector2D(X * other.X, Y * other.Y);
	}

	FVector2D operator /(float scalar) const
	{
		return FVector2D(X / scalar, Y / scalar);
	}

	FVector2D operator /(const FVector2D& other) const
	{
		return FVector2D(X / other.X, Y / other.Y);
	}

	FVector2D& operator =(const FVector2D& other)
	{
		X = other.X;
		Y = other.Y;
		return *this;
	}

	FVector2D& operator +=(float scalar)
	{
		X += scalar;
		Y += scalar;
		return *this;
	}

	FVector2D& operator +=(const FVector2D& other)
	{
		X += other.X;
		Y += other.Y;
		return *this;
	}

	FVector2D& operator -=(float scalar)
	{
		X -= scalar;
		Y -= scalar;
		return *this;
	}

	FVector2D& operator -=(const FVector2D& other)
	{
		X -= other.X;
		Y -= other.Y;
		return *this;
	}

	FVector2D& operator *=(const FVector2D& other)
	{
		X *= other.X;
		Y *= other.Y;
		return *this;
	}

	FVector2D& operator *=(float scalar)
	{
		X *= scalar;
		Y *= scalar;
		return *this;
	}
};