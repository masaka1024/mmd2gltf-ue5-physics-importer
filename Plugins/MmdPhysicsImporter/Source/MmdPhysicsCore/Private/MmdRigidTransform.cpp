// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/Transform.cs

#include "MmdRigidTransform.h"

namespace MmdPhysics
{
	// ★他の翻訳単位の静的定数 (Vec3::Zero など) を参照して初期化しないこと。
	//   静的初期化順序は TU 間で未定義であり、参照すると全ゼロの値を掴む可能性がある。
	//   ここはすべてリテラルから直接構築する。
	const RigidTransform RigidTransform::Identity(Quat(0, 0, 0, 1), Vec3(0, 0, 0));

	const Matrix3x3 Matrix3x3::Identity(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1));
	const Matrix3x3 Matrix3x3::Zero(Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(0, 0, 0));
}
