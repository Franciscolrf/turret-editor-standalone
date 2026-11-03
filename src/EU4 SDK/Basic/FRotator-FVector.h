#pragma once
#include <cmath>

#define R_PI		3.1415926535897932f
#define R_HALF_PI	1.57079632679f
#define R_INV_PI	0.31830988618f

struct FVector
{
	float X;
	float Y;
	float Z;

	FVector()
	{
		X = 0;
		Y = 0;
		Z = 0;
	}

	FVector(float x, float y, float z)
	{
		X = x;
		Y = y;
		Z = z;
	}

	FVector operator +(float scalar) const
	{
		return FVector(X + scalar, Y + scalar, Z + scalar);
	}

	FVector operator +(const FVector& other) const
	{
		return FVector(X + other.X, Y + other.Y, Z + other.Z);
	}

	FVector operator -(float scalar) const
	{
		return FVector(X - scalar, Y - scalar, Z - scalar);
	}

	FVector operator -(const FVector& other) const
	{
		return FVector(X - other.X, Y - other.Y, Z - other.Z);
	}

	FVector operator *(float scalar) const
	{
		return FVector(X * scalar, Y * scalar, Z * scalar);
	}

	FVector operator *(const FVector& other) const
	{
		return FVector(X * other.X, Y * other.Y, Z * other.Z);
	}

	FVector operator /(float scalar) const
	{
		return FVector(X / scalar, Y / scalar, Z / scalar);
	}

	FVector operator /(const FVector& other) const
	{
		return FVector(X / other.X, Y / other.Y, Z / other.Z);
	}

	FVector& operator =(const FVector& other)
	{
		X = other.X;
		Y = other.Y;
		Z = other.Z;
		return *this;
	}

	FVector& operator +=(float scalar)
	{
		X += scalar;
		Y += scalar;
		Z += scalar;
		return *this;
	}

	FVector& operator +=(const FVector& other)
	{
		X += other.X;
		Y += other.Y;
		Z += other.Z;
		return *this;
	}

	FVector& operator -=(float scalar)
	{
		X -= scalar;
		Y -= scalar;
		Z -= scalar;
		return *this;
	}

	FVector& operator -=(const FVector& other)
	{
		X -= other.X;
		Y -= other.Y;
		Z -= other.Z;
		return *this;
	}

	FVector& operator *=(const FVector& other)
	{
		X *= other.X;
		Y *= other.Y;
		Z *= other.Z;
		return *this;
	}

	FVector& operator *=(float scalar)
	{
		X *= scalar;
		Y *= scalar;
		Z *= scalar;
		return *this;
	}

	bool IsValid()
	{
		return X == 0.0 && Y == 0.0;
	}

	bool IsEmpty() const
	{
		return X == 0 && Y == 0 && Z == 0;
	}

	void Normalize360(FVector& angle)
	{
		const float maxX1 = 80.0;

		while (angle.X > maxX1)
			angle.X -= 180.0;
		while (angle.X < -maxX1)
			angle.X += 180.0;
		while (angle.Y > 180.0)
			angle.Y -= 360.0;
		while (angle.Y < -180.0)
			angle.Y += 360.0;
	}

	float Dot(const FVector& b) const
	{
		return (X * b.X) + (Y * b.Y) + (Z * b.Z);
	}

	float MagnitudeSqr() const
	{
		return Dot(*this);
	}

	float Magnitude() const
	{
		return sqrt(MagnitudeSqr());
	}

	float Size() const
	{
		return sqrtf(X * X + Y * Y + Z * Z);
	}

	FVector Unit() const
	{
		const float fMagnitude = Magnitude();
		return FVector(X / fMagnitude, Y / fMagnitude, Z / fMagnitude);
	}

	float Distance(const FVector& v, bool bIgnoreY = false) const
	{
		float deltaX = v.X - X;
		float deltaZ = v.Z - Z;


		float deltaY = bIgnoreY ? 0.0f : v.Y - Y;

		return sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
	}

	float DistanceMeter(const FVector& v, bool bIgnoreY = false) const
	{
		return Distance(v, bIgnoreY) * 0.01f;
	}











	friend bool operator ==(const FVector& first, const FVector& second)
	{
		return first.X == second.X && first.Y == second.Y && first.Z == second.Z;
	}

	friend bool operator !=(const FVector& first, const FVector& second)
	{
		return !(first == second);
	}
};

struct FRotator
{
	float Pitch;
	float Yaw;
	float Roll;

	FRotator()
	{
		Pitch = 0.0;
		Yaw = 0.0;
		Roll = 0.0;
	}

	FRotator(float pitch, float yaw, float roll)
	{
		Pitch = pitch;
		Yaw = yaw;
		Roll = roll;
	}

	FRotator operator +(float scalar) const
	{
		return FRotator(Pitch + scalar, Yaw + scalar, Roll + scalar);
	}

	FRotator operator +(const FRotator& other) const
	{
		return FRotator(Pitch + other.Pitch, Yaw + other.Yaw, Roll + other.Roll);
	}

	FRotator operator -(float scalar) const
	{
		return FRotator(Pitch - scalar, Yaw - scalar, Roll - scalar);
	}

	FRotator operator -(const FRotator& other) const
	{
		return FRotator(Pitch - other.Pitch, Yaw - other.Yaw, Roll - other.Roll);
	}

	FRotator operator *(float scalar) const
	{
		return FRotator(Pitch * scalar, Yaw * scalar, Roll * scalar);
	}

	FRotator operator *(const FRotator& other) const
	{
		return FRotator(Pitch * other.Pitch, Yaw * other.Yaw, Roll * other.Roll);
	}

	FRotator operator /(float scalar) const
	{
		return FRotator(Pitch / scalar, Yaw / scalar, Roll / scalar);
	}

	FRotator operator /(const FRotator& other) const
	{
		return FRotator(Pitch / other.Pitch, Yaw / other.Yaw, Roll / other.Roll);
	}

	FRotator& operator =(const FRotator& other)
	{
		Pitch = other.Pitch; Yaw = other.Yaw; Roll = other.Roll; return *this;
	}

	FRotator& operator +=(float scalar)
	{
		Pitch += scalar;
		Yaw += scalar;
		Roll += scalar;
		return *this;
	}

	FRotator& operator +=(const FRotator& other)
	{
		Pitch += other.Pitch;
		Yaw += other.Yaw;
		Roll += other.Roll;
		return *this;
	}

	FRotator& operator -=(float scalar)
	{
		Pitch -= scalar;
		Yaw -= scalar;
		Roll -= scalar;
		return *this;
	}

	FRotator& operator -=(const FRotator& other)
	{
		Pitch -= other.Pitch;
		Yaw -= other.Yaw;
		Roll -= other.Roll;
		return *this;
	}

	FRotator& operator *=(const FRotator& other)
	{
		Pitch *= other.Pitch;
		Yaw *= other.Yaw;
		Roll *= other.Roll;
		return *this;
	}

	FRotator& operator *=(const float other)
	{
		Pitch *= other;
		Yaw *= other;
		Roll *= other;
		return *this;
	}

	FRotator& operator /=(const FRotator& other)
	{
		Pitch /= other.Pitch;
		Yaw /= other.Yaw;
		Roll /= other.Roll;
		return *this;
	}

	FRotator& operator /=(const float other)
	{
		Pitch /= other;
		Yaw /= other;
		Roll /= other;
		return *this;
	}

	float Size() const
	{
		return sqrt(Pitch * Pitch + Yaw * Yaw + Roll * Roll);
	}

	FRotator Clamp() const
	{
		FRotator r = { Pitch, Yaw, Roll };
		if (r.Yaw > 180.0)
			r.Yaw -= 360.0;
		else if (r.Yaw < -180.0)
			r.Yaw += 360.0;
		if (r.Pitch > 180.0)
			r.Pitch -= 360.0;
		else if (r.Pitch < -180.0)
			r.Pitch += 360.0;
		if (r.Pitch < -89.0)
			r.Pitch = -89.0;
		else if (r.Pitch > 89.0)
			r.Pitch = 89.0;
		r.Roll = 0.0;
		return r;
	}

	void SinCos(float* ScalarSin, float* ScalarCos, float Value)
	{
		float quotient = (R_INV_PI * 0.5f) * Value;
		if (Value >= 0.0f)
			quotient = (float)((int)(quotient + 0.5f));
		else
			quotient = (float)((int)(quotient - 0.5f));

		float y = Value - (2.0f * R_PI) * quotient;

		float sign;
		if (y > R_HALF_PI)
		{
			y = R_PI - y;
			sign = -1.0f;
		}
		else if (y < -R_HALF_PI)
		{
			y = -R_PI - y;
			sign = -1.0f;
		}
		else
			sign = +1.0f;

		float y2 = y * y;


		*ScalarSin = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;


		float p = ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f;
		*ScalarCos = sign * p;
	}

	struct FVector ToVector()
	{
		float CP, SP, CY, SY;
		SinCos(&SP, &CP, Pitch * (R_PI / 180.f));
		SinCos(&SY, &CY, Yaw * (R_PI / 180.f));
		struct FVector V(CP * CY, CP * SY, SP);

		return V;
	}
};