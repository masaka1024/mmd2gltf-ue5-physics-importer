// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – RigidBody
// Bullet の btRigidBody に対応。PMX 剛体パラメータを保持。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/RigidBody.cs (namespace BulletPhysics)
// ===========================================================================

#pragma once

#include "MmdCollisionShape.h"
#include "MmdRigidTransform.h"

namespace MmdPhysics
{
	/**
	 * PMX 剛体の物理演算タイプ。
	 * 0:ボーン追従(static/kinematic) 1:物理演算(dynamic) 2:物理演算+Bone位置合わせ
	 */
	enum class EPhysicsMode : uint8
	{
		BoneFollow = 0,       // Kinematic: ボーンから位置を与える
		Dynamic = 1,          // 物理演算で動く
		DynamicBoneMerge = 2, // 物理演算後に位置をボーンへ合わせる
	};

	/**
	 * 剛体。btRigidBody 相当。運動状態 (姿勢/速度) と物性を保持する。
	 *
	 * ★移植メモ: C# は class（参照型）なので、実体は TSharedPtr で保持し、
	 *   PhysicsWorld / Joint / BoneLink からは生ポインタで参照する。
	 *   所有権は PhysicsWorld::Bodies が持つ。
	 */
	class MMDPHYSICSCORE_API RigidBody
	{
	public:
		// --- 識別/PMX メタ ---
		FString Name;
		int32 Index = -1;
		int32 BoneIndex = -1;

		// グループ / 衝突マスク (PMX)。マスクは bit=1 で「そのグループと衝突する」。
		uint8 Group = 0;
		uint16 CollisionMask = 0;

		// --- 形状/物性 ---
		TSharedPtr<CollisionShape> Shape;
		EPhysicsMode Mode = EPhysicsMode::Dynamic;

		float Mass = 0.0f;            // 質量 (static/kinematic は 0 扱い)
		float LinearDamping = 0.0f;   // 移動減衰
		float AngularDamping = 0.0f;  // 回転減衰
		float Restitution = 0.0f;     // 反発力
		float Friction = 0.0f;        // 摩擦力

		// --- 運動状態 ---
		RigidTransform WorldTransform = RigidTransform::Identity;
		Vec3 LinearVelocity;
		Vec3 AngularVelocity;

		// Split Impulse 用の擬似速度 (btRigidBody の m_pushVelocity / m_turnVelocity 相当)。
		// 貫入回復専用で実速度とは独立。毎サブステップ 0 に初期化され、位置積分に足すが速度としては残さない。
		Vec3 PseudoLinearVelocity;
		Vec3 PseudoAngularVelocity;

		// ボーン追従(kinematic) の目標姿勢 (UE 側から毎フレーム設定)。
		RigidTransform KinematicTarget = RigidTransform::Identity;

		// サブステップ毎に補間された当該サブステップの目標姿勢 (PhysicsWorld が設定)。
		RigidTransform KinematicStepTarget = RigidTransform::Identity;

		// 剛体重心オフセット (PMX 剛体はボーン基準に配置される。ここでは原点=重心とする)。
		Vec3 LocalInertiaDiag;    // ローカル慣性テンソル対角

		// 逆質量・逆慣性 (ワールド)。
		float InverseMass = 0.0f;
		Matrix3x3 InverseInertiaWorld = Matrix3x3::Zero;

	private:
		Vec3 _inverseInertiaLocal;

	public:
		// 累積力 (積分時に適用)。
		Vec3 TotalForce;
		Vec3 TotalTorque;

		// インパルスモーフ用の保持値 (ローカル/グローバル別)。
		Vec3 ImpulseLinearLocal, ImpulseAngularLocal;
		Vec3 ImpulseLinearGlobal, ImpulseAngularGlobal;
		bool ImpulseResetFlag = false;

		// Sleep 管理。
		bool IsActive = true;
		float SleepTimer = 0.0f;

		bool IsStaticOrKinematic() const
		{
			return Mode == EPhysicsMode::BoneFollow || Mass <= 0.0f;
		}

		bool IsKinematic() const { return Mode == EPhysicsMode::BoneFollow; }

		explicit RigidBody(const TSharedPtr<CollisionShape>& InShape)
			: Shape(InShape)
		{
			SetMassProps(0.0f);
		}

		/** 質量から逆質量・ローカル逆慣性を設定する。 */
		void SetMassProps(float InMass)
		{
			Mass = InMass;
			if (InMass > 0.0f && !IsKinematic())
			{
				InverseMass = 1.0f / InMass;
				LocalInertiaDiag = Shape->CalculateLocalInertia(InMass);
				_inverseInertiaLocal = Vec3(
					LocalInertiaDiag.x > 0 ? 1.0f / LocalInertiaDiag.x : 0.0f,
					LocalInertiaDiag.y > 0 ? 1.0f / LocalInertiaDiag.y : 0.0f,
					LocalInertiaDiag.z > 0 ? 1.0f / LocalInertiaDiag.z : 0.0f);
			}
			else
			{
				InverseMass = 0.0f;
				LocalInertiaDiag = Vec3::Zero;
				_inverseInertiaLocal = Vec3::Zero;
			}
			UpdateInertiaWorld();
		}

		/** ワールド逆慣性テンソルを姿勢から更新する。 */
		void UpdateInertiaWorld()
		{
			if (InverseMass == 0.0f)
			{
				InverseInertiaWorld = Matrix3x3::Zero;
				return;
			}
			const Matrix3x3 basis = Matrix3x3::FromQuat(WorldTransform.Rotation);
			// R * diag(invI) * R^T
			InverseInertiaWorld = basis.Scaled(_inverseInertiaLocal);
		}

		// --- ヘルパー ---

		Vec3 CenterOfMass() const { return WorldTransform.Origin; }

		/** 剛体上の点 (ワールド) の速度。v + w × r。 */
		Vec3 VelocityAtPoint(const Vec3& WorldPoint) const
		{
			const Vec3 r = WorldPoint - CenterOfMass();
			return LinearVelocity + Vec3::Cross(AngularVelocity, r);
		}

		void ApplyForce(const Vec3& Force) { TotalForce += Force; }

		void ApplyTorque(const Vec3& Torque) { TotalTorque += Torque; }

		void ApplyCentralImpulse(const Vec3& Impulse)
		{
			if (InverseMass == 0.0f) return;
			LinearVelocity += Impulse * InverseMass;
		}

		void ApplyTorqueImpulse(const Vec3& Torque)
		{
			if (InverseMass == 0.0f) return;
			AngularVelocity += InverseInertiaWorld * Torque;
		}

		/** 点 rel (重心相対) に加える力積。並進+回転の両方に反映。 */
		void ApplyImpulse(const Vec3& Impulse, const Vec3& Rel)
		{
			if (InverseMass == 0.0f) return;
			LinearVelocity += Impulse * InverseMass;
			AngularVelocity += InverseInertiaWorld * Vec3::Cross(Rel, Impulse);
		}

		/** Split Impulse: 擬似速度へ加える貫入回復力積 (実速度には反映しない)。 */
		void ApplyPushImpulse(const Vec3& Impulse, const Vec3& Rel)
		{
			if (InverseMass == 0.0f) return;
			PseudoLinearVelocity += Impulse * InverseMass;
			PseudoAngularVelocity += InverseInertiaWorld * Vec3::Cross(Rel, Impulse);
		}

		/** Split Impulse: 擬似角速度へ加える純トルク力積 (実速度には反映しない)。ジョイントの角度位置補正用。 */
		void ApplyPushTorqueImpulse(const Vec3& Torque)
		{
			if (InverseMass == 0.0f) return;
			PseudoAngularVelocity += InverseInertiaWorld * Torque;
		}

		/** 剛体上の点 (ワールド) の擬似速度。Split Impulse の反復で使用。 */
		Vec3 PseudoVelocityAtPoint(const Vec3& WorldPoint) const
		{
			const Vec3 r = WorldPoint - CenterOfMass();
			return PseudoLinearVelocity + Vec3::Cross(PseudoAngularVelocity, r);
		}

		void ClearForces()
		{
			TotalForce = Vec3::Zero;
			TotalTorque = Vec3::Zero;
		}

		/** ワールド AABB を計算する。 */
		Aabb ComputeAabb() const
		{
			const Vec3 c = WorldTransform.Origin;
			const float r = Shape->BoundingRadius() + Shape->Margin;
			return Aabb(c - Vec3(r), c + Vec3(r));
		}
	};
}
