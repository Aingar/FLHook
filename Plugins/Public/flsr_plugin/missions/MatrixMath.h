#pragma once
#include <FLHook.h>

struct TransformMatrix
{
	float d[4][4];
};
TransformMatrix MultiplyMatrix(const TransformMatrix& mat1, const TransformMatrix& mat2);
TransformMatrix SetupTransform(const Vector& p, const Vector& r);