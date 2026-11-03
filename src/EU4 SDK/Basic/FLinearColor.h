#pragma once

struct FLinearColor
{
	float R;
	float G;
	float B;
	float A;

	FLinearColor()
	{
		R = 0;
		G = 0;
		B = 0;
		A = 0;
	}

	FLinearColor(float r, float g, float b, float a)
	{
		R = r;
		G = g;
		B = b;
		A = a;
	}
};