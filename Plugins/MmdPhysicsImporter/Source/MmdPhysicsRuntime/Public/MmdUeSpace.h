// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// MMD (PMX ネイティブ) 空間 と UE コンポーネント空間の変換。
//
// ★プラグイン内で座標変換を行ってよいのはここだけ。
//   移植元 Unity 版が「駆動式を ApplyKinematicTargets の 1 箇所に集約」して
//   誤配置事故の再発を止めたのと同じ理由で、変換も 1 箇所に閉じ込める。
//
// --- 変換の根拠 (docs/coordinate_transform.md に詳述) ---
//
// mmd2gltf は PMX(左手 Y-up) → glTF(右手 Y-up) で Z を反転する:
//     gltf = (px, py, -pz) * unitScale
//   (移植元 GlbPhysicsReader が (lt.x/s, lt.y/s, -lt.z/s) で戻していることから確定)
//
// UE 標準の Interchange glTF インポータは glTF → UE で Y と Z を入れ替える。
// 実値は UE 5.5 エンジンソース
//   Engine/Plugins/Interchange/Runtime/Source/Parsers/GLTFCore/Private/GLTF/ConversionUtilities.h
//     ConvertVec3(V) -> { V.X, V.Z, V.Y }
//     ConvertQuat(Q) -> { -Q.X, -Q.Z, -Q.Y, Q.W }
// さらに glTF はメートル、UE はセンチメートルなので ×100。
//
// 合成すると:
//     位置  UE = (px, -pz, py) * UnitScale * 100        // UnitScale=0.08 なら ×8
//     回転  UE = (qx, -qz, qy, qw)
//
// この行列は行列式 +1 の純粋な回転 (X軸まわり +90°) であり、鏡映ではない。
// PMX(左手) → glTF(右手) → UE(左手) と手系反転が 2 回で偶数回だからである。
// 帰結として、Unity の旧 PhysX 版が必要としていた「角度制限の鏡像処理」は不要になる。
// カイラリティが保たれるので、ジョイントの回転制限をそのまま渡してよい。
//
// ★副作用: MMD の正面 (0,0,-1) は UE で +Y を向く (UE の慣例はキャラ正面 = +X)。
//   物理の正しさには影響しないが、見た目を UE 慣例に合わせたい場合は
//   アクター/ルート側で Yaw -90° を掛ける。
// ===========================================================================

#pragma once

#include "CoreMinimal.h"
#include "MmdRigidTransform.h"

/** MMD(PMX ネイティブ) ↔ UE コンポーネント空間。 */
struct MMDPHYSICSRUNTIME_API FMmdUeSpace
{
	/** glTF(メートル) → UE(センチメートル)。 */
	static constexpr float CmPerMeter = 100.0f;

	/** PMX 1 単位あたりの UE センチメートル。UnitScale=0.08 なら 8。 */
	static FORCEINLINE float LengthScale(float UnitScale) { return UnitScale * CmPerMeter; }

	static FORCEINLINE FVector ToUe(const MmdPhysics::Vec3& V, float UnitScale)
	{
		const float S = LengthScale(UnitScale);
		return FVector(V.x * S, -V.z * S, V.y * S);
	}

	static FORCEINLINE MmdPhysics::Vec3 ToMmd(const FVector& V, float UnitScale)
	{
		const float S = LengthScale(UnitScale);
		// ToUe の逆: (X,Y,Z) = (x,-z,y) より x=X, y=Z, z=-Y。
		return MmdPhysics::Vec3(
			static_cast<float>(V.X) / S,
			static_cast<float>(V.Z) / S,
			-static_cast<float>(V.Y) / S);
	}

	static FORCEINLINE FQuat ToUe(const MmdPhysics::Quat& Q)
	{
		return FQuat(Q.x, -Q.z, Q.y, Q.w);
	}

	static FORCEINLINE MmdPhysics::Quat ToMmd(const FQuat& Q)
	{
		// 位置と同じ (x, -z, y) パターン。純回転なので符号補正は不要。
		return MmdPhysics::Quat(
			static_cast<float>(Q.X),
			static_cast<float>(Q.Z),
			-static_cast<float>(Q.Y),
			static_cast<float>(Q.W));
	}

	static FORCEINLINE MmdPhysics::RigidTransform ToMmd(const FTransform& T, float UnitScale)
	{
		return MmdPhysics::RigidTransform(ToMmd(T.GetRotation()), ToMmd(T.GetLocation(), UnitScale));
	}

	/** 既存のトランスフォームのスケールを保ったまま、位置と回転だけ差し替える。 */
	static FORCEINLINE FTransform ToUe(const MmdPhysics::RigidTransform& T, float UnitScale, const FVector& KeepScale)
	{
		return FTransform(ToUe(T.Rotation), ToUe(T.Origin, UnitScale), KeepScale);
	}
};
