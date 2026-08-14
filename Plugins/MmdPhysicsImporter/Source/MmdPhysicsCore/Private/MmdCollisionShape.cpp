// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/CollisionShape.cs

#include "MmdCollisionShape.h"

namespace MmdPhysics
{
	// A/B 検証用に可変のまま残す。0 にすると 2026-08-13 以前の値を再現できる。
	float CapsuleShape::InertiaMargin = 0.04f;
}
