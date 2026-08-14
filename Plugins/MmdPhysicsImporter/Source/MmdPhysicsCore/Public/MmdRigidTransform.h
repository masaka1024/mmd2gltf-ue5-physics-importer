// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – RigidTransform
// Bullet の btTransform (basis + origin) と等価な剛体変換。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/Transform.cs (namespace BulletPhysics)
// ===========================================================================

#pragma once

#include "MmdMathTypes.h"

namespace MmdPhysics
{
	/**
	 * 剛体の姿勢を表す変換 (回転 + 平行移動)。Bullet の btTransform に対応。
	 * スケールを含まないため逆変換を安価に計算できる。
	 */
	struct RigidTransform
	{
		Quat Rotation;   // basis
		Vec3 Origin;     // translation

		RigidTransform() {}
		RigidTransform(const Quat& InRotation, const Vec3& InOrigin)
			: Rotation(InRotation)
			, Origin(InOrigin)
		{}

		static MMDPHYSICSCORE_API const RigidTransform Identity;

		/** ローカル点をワールド点へ変換 (basis * p + origin)。 */
		Vec3 TransformPoint(const Vec3& p) const { return Rotation * p + Origin; }

		/** 方向ベクトルを回転のみ適用して変換 (平行移動なし)。 */
		Vec3 TransformDirection(const Vec3& v) const { return Rotation * v; }

		/** ワールド点をローカル点へ変換 (逆変換)。 */
		Vec3 InverseTransformPoint(const Vec3& p) const { return Rotation.Conjugated() * (p - Origin); }

		/** ワールド方向をローカル方向へ変換。 */
		Vec3 InverseTransformDirection(const Vec3& v) const { return Rotation.Conjugated() * v; }

		/** 逆変換を返す。 */
		RigidTransform Inverse() const
		{
			const Quat invRot = Rotation.Conjugated();
			return RigidTransform(invRot, invRot * (-Origin));
		}

		/** this * rhs を合成 (rhs を先に、this を後に適用)。 */
		friend RigidTransform operator*(const RigidTransform& a, const RigidTransform& b)
		{
			return RigidTransform(
				(a.Rotation * b.Rotation).Normalized(),
				a.Rotation * b.Origin + a.Origin);
		}

		/** this から other への相対変換 (this^-1 * other)。 */
		RigidTransform InverseTimes(const RigidTransform& Other) const
		{
			const Vec3 v = Other.Origin - Origin;
			const Quat invRot = Rotation.Conjugated();
			return RigidTransform(invRot * Other.Rotation, invRot * v);
		}

		Matrix4x4 ToMatrix() const
		{
			Matrix4x4 m = Matrix4x4::Rotation(Rotation);
			m.m03 = Origin.x; m.m13 = Origin.y; m.m23 = Origin.z;
			return m;
		}

		/**
		 * PMX の (位置, オイラー角ラジアン) から剛体変換を作る。
		 * 回転は MMD/PMX の YXZ 順で解釈する (Quat::FromEulerYxz のコメント参照)。
		 */
		static RigidTransform FromEuler(const Vec3& PosRad, const Vec3& EulerRad)
		{
			return RigidTransform(Quat::FromEulerYxz(EulerRad.x, EulerRad.y, EulerRad.z), PosRad);
		}

		bool Equals(const RigidTransform& Other) const
		{
			return Rotation.Equals(Other.Rotation) && Origin.Equals(Other.Origin);
		}

		FString ToString() const { return FString::Printf(TEXT("T[%s, %s]"), *Origin.ToString(), *Rotation.ToString()); }
	};

	/**
	 * 3x3 行列。慣性テンソル / 姿勢 basis 計算用。Row-major。
	 */
	struct Matrix3x3
	{
		Vec3 Row0, Row1, Row2;

		Matrix3x3() {}
		Matrix3x3(const Vec3& r0, const Vec3& r1, const Vec3& r2) : Row0(r0), Row1(r1), Row2(r2) {}

		static MMDPHYSICSCORE_API const Matrix3x3 Identity;
		static MMDPHYSICSCORE_API const Matrix3x3 Zero;

		static Matrix3x3 Diagonal(const Vec3& d)
		{
			return Matrix3x3(Vec3(d.x, 0, 0), Vec3(0, d.y, 0), Vec3(0, 0, d.z));
		}

		static Matrix3x3 FromQuat(const Quat& q)
		{
			const Matrix4x4 m = Matrix4x4::Rotation(q);
			// Matrix4x4 は転置格納 (Rᵀ) のため、ここで転置して行=Rの行 に揃える。
			return Matrix3x3(
				Vec3(m.m00, m.m10, m.m20),
				Vec3(m.m01, m.m11, m.m21),
				Vec3(m.m02, m.m12, m.m22));
		}

		Vec3 Column(int32 i) const { return Vec3(Row0[i], Row1[i], Row2[i]); }

		friend Vec3 operator*(const Matrix3x3& m, const Vec3& v)
		{
			return Vec3(m.Row0.Dot(v), m.Row1.Dot(v), m.Row2.Dot(v));
		}

		friend Matrix3x3 operator*(const Matrix3x3& a, const Matrix3x3& b)
		{
			const Vec3 c0 = b.Column(0); const Vec3 c1 = b.Column(1); const Vec3 c2 = b.Column(2);
			return Matrix3x3(
				Vec3(a.Row0.Dot(c0), a.Row0.Dot(c1), a.Row0.Dot(c2)),
				Vec3(a.Row1.Dot(c0), a.Row1.Dot(c1), a.Row1.Dot(c2)),
				Vec3(a.Row2.Dot(c0), a.Row2.Dot(c1), a.Row2.Dot(c2)));
		}

		Matrix3x3 Transposed() const { return Matrix3x3(Column(0), Column(1), Column(2)); }

		/** this * diag(scale) * this^T — basis に対角テンソルを回転適用。 */
		Matrix3x3 Scaled(const Vec3& Scale) const
		{
			// 行列 basis の各列を scale 倍して basis^T を乗算する形。
			// 慣性テンソルのワールド変換: R * I_local * R^T に使用。
			return *this * Matrix3x3::Diagonal(Scale) * Transposed();
		}
	};
}
