// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/MathTypes.cs
//
// ヘッダに置くと肥大化する定義（静的定数・行列積・FromMatrix）だけをこちらに分ける。
// 数式そのものは C# と一字一句同じ順序を保つこと。

#include "MmdMathTypes.h"

namespace MmdPhysics
{
	const Vec3 Vec3::Zero(0, 0, 0);
	const Vec3 Vec3::One(1, 1, 1);
	const Vec3 Vec3::XAxis(1, 0, 0);
	const Vec3 Vec3::YAxis(0, 1, 0);
	const Vec3 Vec3::ZAxis(0, 0, 1);

	const Quat Quat::Identity(0, 0, 0, 1);

	Quat Quat::FromMatrix(const Matrix4x4& m)
	{
		const float trace = m.m00 + m.m11 + m.m22;
		Quat q;

		if (trace > 0)
		{
			const float s = MSqrt(trace + 1.0f) * 2.0f;
			q = Quat(
				(m.m21 - m.m12) / s,
				(m.m02 - m.m20) / s,
				(m.m10 - m.m01) / s,
				0.25f * s);
		}
		else
		{
			if (m.m00 > m.m11 && m.m00 > m.m22)
			{
				const float s = MSqrt(1.0f + m.m00 - m.m11 - m.m22) * 2.0f;
				q = Quat(
					0.25f * s,
					(m.m01 + m.m10) / s,
					(m.m02 + m.m20) / s,
					(m.m21 - m.m12) / s);
			}
			else if (m.m11 > m.m22)
			{
				const float s = MSqrt(1.0f + m.m11 - m.m00 - m.m22) * 2.0f;
				q = Quat(
					(m.m01 + m.m10) / s,
					0.25f * s,
					(m.m12 + m.m21) / s,
					(m.m02 - m.m20) / s);
			}
			else
			{
				const float s = MSqrt(1.0f + m.m22 - m.m00 - m.m11) * 2.0f;
				q = Quat(
					(m.m02 + m.m20) / s,
					(m.m12 + m.m21) / s,
					0.25f * s,
					(m.m10 - m.m01) / s);
			}
		}
		return q;
	}

	Matrix4x4 Matrix4x4::Multiply(const Matrix4x4& a, const Matrix4x4& b)
	{
		Matrix4x4 r;
		r.m00 = a.m00 * b.m00 + a.m01 * b.m10 + a.m02 * b.m20 + a.m03 * b.m30;
		r.m01 = a.m00 * b.m01 + a.m01 * b.m11 + a.m02 * b.m21 + a.m03 * b.m31;
		r.m02 = a.m00 * b.m02 + a.m01 * b.m12 + a.m02 * b.m22 + a.m03 * b.m32;
		r.m03 = a.m00 * b.m03 + a.m01 * b.m13 + a.m02 * b.m23 + a.m03 * b.m33;

		r.m10 = a.m10 * b.m00 + a.m11 * b.m10 + a.m12 * b.m20 + a.m13 * b.m30;
		r.m11 = a.m10 * b.m01 + a.m11 * b.m11 + a.m12 * b.m21 + a.m13 * b.m31;
		r.m12 = a.m10 * b.m02 + a.m11 * b.m12 + a.m12 * b.m22 + a.m13 * b.m32;
		r.m13 = a.m10 * b.m03 + a.m11 * b.m13 + a.m12 * b.m23 + a.m13 * b.m33;

		r.m20 = a.m20 * b.m00 + a.m21 * b.m10 + a.m22 * b.m20 + a.m23 * b.m30;
		r.m21 = a.m20 * b.m01 + a.m21 * b.m11 + a.m22 * b.m21 + a.m23 * b.m31;
		r.m22 = a.m20 * b.m02 + a.m21 * b.m12 + a.m22 * b.m22 + a.m23 * b.m32;
		r.m23 = a.m20 * b.m03 + a.m21 * b.m13 + a.m22 * b.m23 + a.m23 * b.m33;

		r.m30 = a.m30 * b.m00 + a.m31 * b.m10 + a.m32 * b.m20 + a.m33 * b.m30;
		r.m31 = a.m30 * b.m01 + a.m31 * b.m11 + a.m32 * b.m21 + a.m33 * b.m31;
		r.m32 = a.m30 * b.m02 + a.m31 * b.m12 + a.m32 * b.m22 + a.m33 * b.m32;
		r.m33 = a.m30 * b.m03 + a.m31 * b.m13 + a.m32 * b.m23 + a.m33 * b.m33;
		return r;
	}

	bool Matrix4x4::Equals(const Matrix4x4& Other) const
	{
		const float e = 1e-6f;
		return FMath::Abs(m00 - Other.m00) < e && FMath::Abs(m01 - Other.m01) < e &&
			FMath::Abs(m02 - Other.m02) < e && FMath::Abs(m03 - Other.m03) < e &&
			FMath::Abs(m10 - Other.m10) < e && FMath::Abs(m11 - Other.m11) < e &&
			FMath::Abs(m12 - Other.m12) < e && FMath::Abs(m13 - Other.m13) < e &&
			FMath::Abs(m20 - Other.m20) < e && FMath::Abs(m21 - Other.m21) < e &&
			FMath::Abs(m22 - Other.m22) < e && FMath::Abs(m23 - Other.m23) < e &&
			FMath::Abs(m30 - Other.m30) < e && FMath::Abs(m31 - Other.m31) < e &&
			FMath::Abs(m32 - Other.m32) < e && FMath::Abs(m33 - Other.m33) < e;
	}

	FString Matrix4x4::ToString() const
	{
		return FString::Printf(TEXT("[%.2f,%.2f,%.2f,%.2f]\n[%.2f,%.2f,%.2f,%.2f]\n[%.2f,%.2f,%.2f,%.2f]\n[%.2f,%.2f,%.2f,%.2f]"),
			m00, m01, m02, m03,
			m10, m11, m12, m13,
			m20, m21, m22, m23,
			m30, m31, m32, m33);
	}
}
