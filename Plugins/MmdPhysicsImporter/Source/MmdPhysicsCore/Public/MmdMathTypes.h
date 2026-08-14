// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – Core Math
// Bullet 2.75 の実装に合わせた高精度 3D 数学型
//
// 移植元: mmd2gltf-unity-physics-importer
//         Assets/MMD_Scripts/MmdPhysics/Core/MathTypes.cs (namespace BulletPhysics)
//
// ★UE の FVector / FQuat / FMatrix には置き換えないこと。
//   FVector は double 成分であり、演算順序も異なるため C# 版と数値が一致しなくなる。
//   UE 型との変換は MmdPhysicsRuntime の FMmdUeSpace ただ 1 箇所に集約する。
//   （移植元が UnityEngine.Vector3 との explicit operator をここに持っていたのに対し、
//     UE 版ではその責務をこのモジュールから外した。Core を UE 非依存に保つため。）
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

namespace MmdPhysics
{
	// ---------------------------------------------------------------------
	// C# の Math.Sqrt / Math.Sin などは引数を double に昇格して計算し、
	// 呼び出し側で (float) にキャストしている。float 版の sqrtf/sinf を使うと
	// 最終ビットが食い違い、長い反復の末に軌道がずれる。中間 double を再現する。
	// ---------------------------------------------------------------------
	FORCEINLINE float MSqrt(float V) { return static_cast<float>(FMath::Sqrt(static_cast<double>(V))); }
	FORCEINLINE float MSin(float V) { return static_cast<float>(FMath::Sin(static_cast<double>(V))); }
	FORCEINLINE float MCos(float V) { return static_cast<float>(FMath::Cos(static_cast<double>(V))); }
	FORCEINLINE float MAcos(float V) { return static_cast<float>(FMath::Acos(static_cast<double>(V))); }
	FORCEINLINE float MAsin(float V) { return static_cast<float>(FMath::Asin(static_cast<double>(V))); }
	FORCEINLINE float MAtan2(float Y, float X) { return static_cast<float>(FMath::Atan2(static_cast<double>(Y), static_cast<double>(X))); }

	/** C# の Math.PI は double。(float)(Math.PI / 2) を再現するために double で保持する。 */
	constexpr double MPi = 3.14159265358979323846;

	struct Matrix4x4;

	/**
	 * Bullet と同じ順序 (x,y,z) の 3D ベクトル。
	 * PmxEditor の float3 / Vector3 と同等。
	 */
	struct Vec3
	{
		float x, y, z;

		Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
		explicit Vec3(float v) : x(v), y(v), z(v) {}
		Vec3(float InX, float InY, float InZ) : x(InX), y(InY), z(InZ) {}

		float& operator[](int32 Index)
		{
			switch (Index)
			{
			case 0: return x;
			case 1: return y;
			case 2: return z;
			default: checkf(false, TEXT("Vec3 index out of range: %d"), Index); return x;
			}
		}

		float operator[](int32 Index) const
		{
			switch (Index)
			{
			case 0: return x;
			case 1: return y;
			case 2: return z;
			default: checkf(false, TEXT("Vec3 index out of range: %d"), Index); return x;
			}
		}

		// ★静的定数は個別に API マクロを付ける。struct 自体は非エクスポート
		//   (全メンバが inline なので実体が要るのはこれらだけ) だが、
		//   静的データメンバは明示しないと他モジュールからリンクできない。
		static MMDPHYSICSCORE_API const Vec3 Zero;
		static MMDPHYSICSCORE_API const Vec3 One;
		static MMDPHYSICSCORE_API const Vec3 XAxis;
		static MMDPHYSICSCORE_API const Vec3 YAxis;
		static MMDPHYSICSCORE_API const Vec3 ZAxis;

		float LengthSquared() const { return x * x + y * y + z * z; }
		float Length() const { return MSqrt(LengthSquared()); }

		Vec3 Normalized() const
		{
			const float len = Length();
			return len > 0 ? *this / len : Zero;
		}

		// Operators
		friend Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
		friend Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
		friend Vec3 operator-(const Vec3& a) { return Vec3(-a.x, -a.y, -a.z); }
		friend Vec3 operator*(const Vec3& a, float s) { return Vec3(a.x * s, a.y * s, a.z * s); }
		friend Vec3 operator*(float s, const Vec3& a) { return a * s; }
		// ★C# は逆数を1度だけ求めて乗算する。成分ごとの除算に書き換えると数値が変わる。
		friend Vec3 operator/(const Vec3& a, float s) { const float i = 1.0f / s; return a * i; }

		Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
		Vec3& operator-=(const Vec3& b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
		Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

		friend bool operator==(const Vec3& a, const Vec3& b) { return a.Equals(b); }
		friend bool operator!=(const Vec3& a, const Vec3& b) { return !a.Equals(b); }

		// Bullet-style functions
		static Vec3 Cross(const Vec3& a, const Vec3& b)
		{
			return Vec3(
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x
			);
		}

		float Dot(const Vec3& b) const { return x * b.x + y * b.y + z * b.z; }

		float Angle(const Vec3& b) const
		{
			const float d = Dot(b) / (Length() * b.Length());
			return MAcos(FMath::Clamp(d, -1.0f, 1.0f));
		}

		bool Equals(const Vec3& Other) const
		{
			return FMath::Abs(x - Other.x) < 1e-6f &&
				FMath::Abs(y - Other.y) < 1e-6f &&
				FMath::Abs(z - Other.z) < 1e-6f;
		}

		FString ToString() const { return FString::Printf(TEXT("(%.4f, %.4f, %.4f)"), x, y, z); }
	};

	FORCEINLINE uint32 GetTypeHash(const Vec3& V)
	{
		return HashCombine(HashCombine(::GetTypeHash(V.x), ::GetTypeHash(V.y)), ::GetTypeHash(V.z));
	}

	/**
	 * Bullet と同じ Quaternion (x, y, z, w) – w は実部。
	 * MMD のボーン回転クォータニオンと互換。
	 */
	struct Quat
	{
		float x, y, z, w;

		// ★既定コンストラクタは全 0 (w=0)。単位が必要な箇所は必ず Quat::Identity を使うこと。
		//   移植元 C# の new Quat() が全 0 になるのと同じ挙動に揃えている。
		Quat() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
		Quat(float InX, float InY, float InZ, float InW) : x(InX), y(InY), z(InZ), w(InW) {}

		static MMDPHYSICSCORE_API const Quat Identity;

		// Convert from axis-angle (Bullet の回転表現と同等)
		static Quat FromAxisAngle(const Vec3& Axis, float AngleRad)
		{
			const float half = AngleRad * 0.5f;
			const float sin = MSin(half);
			const Vec3 n = Axis.Normalized();
			return Quat(n.x * sin, n.y * sin, n.z * sin, MCos(half));
		}

		/**
		 * MMD/PMX のオイラー角(ラジアン)→クォータニオン。適用順は Z→X→Y、
		 * すなわち R = Ry * Rx * Rz (YXZ順)。PMX の剛体/Joint の回転はこの順序で定義されている
		 * (Bullet の btQuaternion(yaw, pitch, roll) と同型。saba 等の実装も同じ)。
		 *
		 * ★2026-08-10 修正: 従来は FromEuler(=ZYX順) を使っており、複合回転の剛体で
		 *   向きがズレていた。「髪のカプセルが髪の流れに沿わない」の原因。
		 *   全13モデルで実測: カプセルのY軸とボーン→子ボーン方向のズレ角(中央値)は
		 *   YXZ 0.0°/ZYX 1.6° (IA)、YXZ 0.0°/ZYX 5.8° (ぬこ式レーシングミク2023)。
		 *   15°以内に収まる割合も YXZ 91.8% / ZYX 80.0% (同モデル) と YXZ が優る。
		 *   撤去した旧PhysXインポーターが Quaternion.Euler(=Unityの YXZ) を使っていたのとも一致する。
		 */
		static Quat FromEulerYxz(float rx, float ry, float rz)
		{
			const float cx = MCos(rx * 0.5f); const float sx = MSin(rx * 0.5f);
			const float cy = MCos(ry * 0.5f); const float sy = MSin(ry * 0.5f);
			const float cz = MCos(rz * 0.5f); const float sz = MSin(rz * 0.5f);

			// q = qy * qx * qz を展開したもの。
			return Quat(
				cy * sx * cz + sy * cx * sz, // x
				sy * cx * cz - cy * sx * sz, // y
				cy * cx * sz - sy * sx * cz, // z
				cy * cx * cz + sy * sx * sz  // w
			);
		}

		// Convert from Euler angles (radians) – ZYX order (Roll-Pitch-Yaw)
		// ※PMX の回転はこの順序では**ない**。PMX データには FromEulerYxz を使うこと。
		static Quat FromEuler(float rx, float ry, float rz)
		{
			const float cx = MCos(rx * 0.5f); const float sx = MSin(rx * 0.5f);
			const float cy = MCos(ry * 0.5f); const float sy = MSin(ry * 0.5f);
			const float cz = MCos(rz * 0.5f); const float sz = MSin(rz * 0.5f);

			return Quat(
				sx * cy * cz - cx * sy * sz, // x
				cx * sy * cz + sx * cy * sz, // y
				cx * cy * sz - sx * sy * cz, // z
				cx * cy * cz + sx * sy * sz  // w
			);
		}

		friend Quat operator*(const Quat& a, const Quat& b)
		{
			return Quat(
				a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
				a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
				a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
			);
		}

		friend Vec3 operator*(const Quat& q, const Vec3& v)
		{
			// q * v * q^(-1) via conjugate for unit quaternion
			const float qx = q.x, qy = q.y, qz = q.z, qw = q.w;
			const float ix = v.x, iy = v.y, iz = v.z;
			const float t = 2.0f / (qx * qx + qy * qy + qz * qz + qw * qw);

			const float ox = (1.0f - t * (qy * qy + qz * qz)) * ix
				+ t * (qx * qy - qw * qz) * iy
				+ t * (qx * qz + qw * qy) * iz;
			const float oy = t * (qx * qy + qw * qz) * ix
				+ (1.0f - t * (qx * qx + qz * qz)) * iy
				+ t * (qy * qz - qw * qx) * iz;
			const float oz = t * (qx * qz - qw * qy) * ix
				+ t * (qy * qz + qw * qx) * iy
				+ (1.0f - t * (qx * qx + qy * qy)) * iz;

			return Vec3(ox, oy, oz);
		}

		static Quat FromMatrix(const Matrix4x4& m);

		Vec3 Rotate(const Vec3& v) const { return *this * v; }

		Quat Normalized() const
		{
			const float len = MSqrt(x * x + y * y + z * z + w * w);
			return len > 0 ? *this / len : Identity;
		}

		friend Quat operator/(const Quat& q, float s)
		{
			const float i = 1.0f / s;
			return Quat(q.x * i, q.y * i, q.z * i, q.w * i);
		}

		friend Quat operator*(const Quat& q, float s)
		{
			return Quat(q.x * s, q.y * s, q.z * s, q.w * s);
		}

		static Quat Slerp(const Quat& a, const Quat& b, float t)
		{
			float cos = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			bool wrap = false;
			if (cos < 0) { cos = -cos; wrap = true; }

			float s0, s1;
			if ((1.0f - cos) > 0.001f)
			{
				const float omega = MAcos(cos);
				const float sin = MSin(omega);
				s0 = MSin((1.0f - t) * omega) / sin;
				s1 = MSin(t * omega) / sin;
			}
			else
			{
				s0 = 1.0f - t;
				s1 = t;
			}

			if (wrap) { s1 = -s1; }
			return Quat(
				s0 * a.x + s1 * b.x,
				s0 * a.y + s1 * b.y,
				s0 * a.z + s1 * b.z,
				s0 * a.w + s1 * b.w);
		}

		Quat Conjugated() const { return Quat(-x, -y, -z, w); }

		FString ToString() const { return FString::Printf(TEXT("q(%.4f, %.4f, %.4f, %.4f)"), x, y, z, w); }

		bool Equals(const Quat& Other) const
		{
			return FMath::Abs(x - Other.x) < 1e-6f &&
				FMath::Abs(y - Other.y) < 1e-6f &&
				FMath::Abs(z - Other.z) < 1e-6f &&
				FMath::Abs(w - Other.w) < 1e-6f;
		}

		friend bool operator==(const Quat& a, const Quat& b) { return a.Equals(b); }
		friend bool operator!=(const Quat& a, const Quat& b) { return !a.Equals(b); }
	};

	/**
	 * 4x4 同次変換行列 (Row-major)。
	 * MMD ボーンのローカル行列 = Scale * Rotation * Translation
	 */
	struct Matrix4x4
	{
		float m00, m01, m02, m03;
		float m10, m11, m12, m13;
		float m20, m21, m22, m23;
		float m30, m31, m32, m33;

		// ★既定コンストラクタは全 0。単位行列は Identity を使うこと（移植元 C# と同じ）。
		Matrix4x4()
			: m00(0), m01(0), m02(0), m03(0)
			, m10(0), m11(0), m12(0), m13(0)
			, m20(0), m21(0), m22(0), m23(0)
			, m30(0), m31(0), m32(0), m33(0)
		{}

		static Matrix4x4 Identity()
		{
			Matrix4x4 m;
			m.m00 = 1; m.m11 = 1; m.m22 = 1; m.m33 = 1;
			return m;
		}

		static Matrix4x4 Translation(const Vec3& t)
		{
			Matrix4x4 m = Identity();
			m.m03 = t.x; m.m13 = t.y; m.m23 = t.z;
			return m;
		}

		static Matrix4x4 Rotation(const Quat& q)
		{
			const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

			Matrix4x4 m;
			m.m00 = 1.0f - 2.0f * (yy + zz); m.m01 = 2.0f * (xy + wz);        m.m02 = 2.0f * (xz - wy);        m.m03 = 0;
			m.m10 = 2.0f * (xy - wz);        m.m11 = 1.0f - 2.0f * (xx + zz); m.m12 = 2.0f * (yz + wx);        m.m13 = 0;
			m.m20 = 2.0f * (xz + wy);        m.m21 = 2.0f * (yz - wx);        m.m22 = 1.0f - 2.0f * (xx + yy); m.m23 = 0;
			m.m30 = 0;                       m.m31 = 0;                       m.m32 = 0;                       m.m33 = 1;
			return m;
		}

		static Matrix4x4 Scale(const Vec3& s)
		{
			Matrix4x4 m;
			m.m00 = s.x; m.m11 = s.y; m.m22 = s.z; m.m33 = 1;
			return m;
		}

		static Matrix4x4 Multiply(const Matrix4x4& a, const Matrix4x4& b);

		Vec3 TransformPoint(const Vec3& p) const
		{
			const float w = m03 * p.x + m13 * p.y + m23 * p.z + m33;
			return Vec3(
				(m00 * p.x + m10 * p.y + m20 * p.z + m30) / w,
				(m01 * p.x + m11 * p.y + m21 * p.z + m31) / w,
				(m02 * p.x + m12 * p.y + m22 * p.z + m32) / w
			);
		}

		Vec3 TransformVector(const Vec3& v) const
		{
			return Vec3(
				m00 * v.x + m10 * v.y + m20 * v.z,
				m01 * v.x + m11 * v.y + m21 * v.z,
				m02 * v.x + m12 * v.y + m22 * v.z
			);
		}

		bool Equals(const Matrix4x4& Other) const;

		FString ToString() const;
	};

	/**
	 * 無限平面 (地面) collision のための法線-距離構造体。
	 */
	struct Plane
	{
		Vec3 Normal;
		float Distance; // Plane: n·x + d = 0 の d

		Plane() : Distance(0.0f) {}
		Plane(const Vec3& InNormal, float InDistance)
			: Normal(InNormal.Normalized())
			, Distance(InDistance)
		{}

		// Signed distance from point to plane
		float SignedDistance(const Vec3& p) const { return Normal.Dot(p) + Distance; }
	};

	/**
	 * Axis-Aligned Bounding Box.
	 * BroadPhase 用。
	 */
	struct Aabb
	{
		Vec3 Min, Max;

		Aabb() {}
		Aabb(const Vec3& InMin, const Vec3& InMax) : Min(InMin), Max(InMax) {}

		bool Contains(const Vec3& p) const
		{
			return p.x >= Min.x && p.x <= Max.x &&
				p.y >= Min.y && p.y <= Max.y &&
				p.z >= Min.z && p.z <= Max.z;
		}

		bool Intersects(const Aabb& Other) const
		{
			return Min.x <= Other.Max.x && Max.x >= Other.Min.x &&
				Min.y <= Other.Max.y && Max.y >= Other.Min.y &&
				Min.z <= Other.Max.z && Max.z >= Other.Min.z;
		}

		Vec3 Center() const { return (Min + Max) * 0.5f; }
		Vec3 Extents() const { return (Max - Min) * 0.5f; }

		void Expand(const Vec3& p)
		{
			Min.x = FMath::Min(Min.x, p.x); Min.y = FMath::Min(Min.y, p.y); Min.z = FMath::Min(Min.z, p.z);
			Max.x = FMath::Max(Max.x, p.x); Max.y = FMath::Max(Max.y, p.y); Max.z = FMath::Max(Max.z, p.z);
		}

		void Expand(float Radius)
		{
			Min.x -= Radius; Min.y -= Radius; Min.z -= Radius;
			Max.x += Radius; Max.y += Radius; Max.z += Radius;
		}
	};
}
