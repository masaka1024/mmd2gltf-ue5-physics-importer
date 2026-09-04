// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – Collision Shapes
// PMX 剛体形状: 0:球 1:箱 2:カプセル
// Bullet の btSphereShape / btBoxShape / btCapsuleShape に対応。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/CollisionShape.cs (namespace BulletPhysics)
// ===========================================================================

#pragma once

#include "MmdMathTypes.h"

namespace MmdPhysics
{
	enum class EShapeType : uint8
	{
		Sphere = 0,   // PMX 形状 0
		Box = 1,      // PMX 形状 1
		Capsule = 2,  // PMX 形状 2
	};

	/**
	 * 凸形状の抽象基底。GJK/EPA 用の support 関数と慣性計算を提供。
	 */
	class MMDPHYSICSCORE_API CollisionShape
	{
	public:
		virtual ~CollisionShape() = default;

		virtual EShapeType Type() const = 0;

		/** 衝突マージン。Bullet 既定に合わせ既定値を持つ。 */
		float Margin = 0.04f;

		/**
		 * Bullet 2.75 `CONVEX_DISTANCE_MARGIN` (btCollisionMargin.h:21)。
		 * btConvexInternalShape の既定マージンで、箱もカプセルもこの値を使う。つまみにしない。
		 */
		static constexpr float BulletConvexDistanceMargin = 0.04f;

		/**
		 * 形状マージンを Bullet 2.75 と同じ取り方にする (既定 false = ビット不変)。
		 * **形状の構築時に読む**ので、PmxPhysicsBuilder::Build より前に立てること。
		 *
		 *   箱     : 当方 `min(半幅)*0.04` / Bullet 2.75 は **0.04 固定**。
		 *            2.75 に `setSafeMargin` は存在しない (後年の Bullet で追加されたもの)。
		 *            半幅 0.2 のスカート箱で 0.008 vs 0.04 = 5倍の差。
		 *            ※2.75 は半幅 < 0.04 の箱でコアが負になるが、退化形状を作らないため
		 *              ここでは半幅未満へクランプする。
		 *   カプセル: 当方 `Margin = 半径` (コア = 線分) / Bullet は 0.04 でコアを
		 *            線分 + (半径 - 0.04) にする。外形はどちらも 線分+半径 で一致し、
		 *            差が出るのは getMargin() を使う量 (GJK の探索距離・AABB・受理閾値) だけ。
		 */
		static bool BulletShapeMargin;

		/** ローカル座標系での support point (方向 dir で最も遠い点)。 */
		virtual Vec3 LocalSupport(const Vec3& Dir) const = 0;

		/** マージンを含む support point。 */
		Vec3 LocalSupportWithMargin(const Vec3& Dir) const
		{
			const Vec3 d = Dir;
			const float len2 = d.LengthSquared();
			if (len2 < 1e-12f) return LocalSupport(Vec3::XAxis);
			return LocalSupport(d) + d.Normalized() * Margin;
		}

		/** 質量 mass に対するローカル慣性テンソル (対角成分)。 */
		virtual Vec3 CalculateLocalInertia(float Mass) const = 0;

		/** ローカル AABB 半径 (原点中心の外接半径)。 */
		virtual float BoundingRadius() const = 0;
	};

	/** 球。PMX size.x を半径として使用。 */
	class MMDPHYSICSCORE_API SphereShape final : public CollisionShape
	{
	public:
		float Radius;

		explicit SphereShape(float InRadius) : Radius(InRadius) { Margin = InRadius; }

		virtual EShapeType Type() const override { return EShapeType::Sphere; }

		// 球の support は常に中心 (半径はマージン扱い)。
		virtual Vec3 LocalSupport(const Vec3& Dir) const override { return Vec3::Zero; }

		virtual Vec3 CalculateLocalInertia(float Mass) const override
		{
			const float i = 0.4f * Mass * Radius * Radius; // 2/5 m r^2
			return Vec3(i, i, i);
		}

		virtual float BoundingRadius() const override { return Radius; }
	};

	/** 箱。PMX size(x,y,z) は半径 (half-extents)。 */
	class MMDPHYSICSCORE_API BoxShape final : public CollisionShape
	{
	public:
		Vec3 HalfExtents;

		explicit BoxShape(const Vec3& InHalfExtents)
			: HalfExtents(InHalfExtents)
		{
			const float MinHalf = FMath::Min(FMath::Min(InHalfExtents.x, InHalfExtents.y), InHalfExtents.z);
			Margin = BulletShapeMargin
				? FMath::Min(BulletConvexDistanceMargin, MinHalf * 0.999f)   // 2.75 は 0.04 固定 (クランプは退化よけ)
				: MinHalf * 0.04f;
		}

		virtual EShapeType Type() const override { return EShapeType::Box; }

		virtual Vec3 LocalSupport(const Vec3& Dir) const override
		{
			// マージン分を差し引いたコア半径。
			const Vec3 h = HalfExtents - Vec3(Margin);
			return Vec3(
				Dir.x >= 0 ? h.x : -h.x,
				Dir.y >= 0 ? h.y : -h.y,
				Dir.z >= 0 ? h.z : -h.z);
		}

		virtual Vec3 CalculateLocalInertia(float Mass) const override
		{
			const float x = HalfExtents.x * 2.0f;
			const float y = HalfExtents.y * 2.0f;
			const float z = HalfExtents.z * 2.0f;
			const float c = Mass / 12.0f;
			return Vec3(
				c * (y * y + z * z),
				c * (x * x + z * z),
				c * (x * x + y * y));
		}

		virtual float BoundingRadius() const override { return HalfExtents.Length(); }
	};

	/**
	 * カプセル。PMX: size.x = 半径, size.y = 高さ(中心の直線長)。Y 軸方向。
	 * Bullet btCapsuleShape と同様、線分 + 半径。
	 */
	class MMDPHYSICSCORE_API CapsuleShape final : public CollisionShape
	{
	public:
		float Radius;
		float Height; // 円柱部分の長さ (両端の半球は含まない)

		CapsuleShape(float InRadius, float InHeight)
			: Radius(InRadius)
			, Height(InHeight)
		{
			// 当方はコア=線分なので Margin=半径。Bullet 2.75 はコアを線分+(半径-0.04) にして Margin=0.04。
			// どちらも外形は 線分+半径 で同じ。
			Margin = BulletShapeMargin ? FMath::Min(BulletConvexDistanceMargin, InRadius) : InRadius;
		}

		virtual EShapeType Type() const override { return EShapeType::Capsule; }

		/**
		 * 慣性計算で half-extent に足すマージン。Bullet 2.75 の CONVEX_DISTANCE_MARGIN。
		 * ★2026-08-13: 0 → 0.04 (Bullet 準拠)。長らくこの1項が抜けており、
		 * 「btCapsuleShape::calculateLocalInertia を忠実に再現」というコメントが実際には嘘だった。
		 * 実測(モデルA): スカートは完全に無影響(箱剛体のため)、髪は静区間の角度差 中央 20.96→19.84 /
		 * p90 81.30→73.07 (-10%) / 位置差 p90 1.601→1.425 と改善。深貫入は 0 件のまま。
		 * 符号バイアスのみ -1.53°→-2.12° と微増(どちらも「動かなさすぎ」側)。
		 * A/B 用に static のまま残す (env INERTIAMARGIN)。0 で従来値を再現できる。
		 */
		static float InertiaMargin;

		float HalfHeight() const { return Height * 0.5f; }

		virtual Vec3 LocalSupport(const Vec3& Dir) const override
		{
			// 線分の端点。半径のうち Margin ぶんは LocalSupportWithMargin が足すので、
			// ここでは (半径 - Margin) だけ膨らませる。
			// 既定は Margin == Radius なので膨らみゼロ = 従来と完全に同一 (ビット不変)。
			const Vec3 Seg = Dir.y >= 0 ? Vec3(0, HalfHeight(), 0) : Vec3(0, -HalfHeight(), 0);
			const float Core = Radius - Margin;
			if (Core <= 0.0f) return Seg;
			const float Len2 = Dir.LengthSquared();
			if (Len2 < 1e-12f) return Seg;
			return Seg + Dir * (Core / MSqrt(Len2));
		}

		virtual Vec3 CalculateLocalInertia(float Mass) const override
		{
			// Bullet btCapsuleShape::calculateLocalInertia を忠実に再現する。
			// Bullet は「両端の球を含む外接箱」の慣性で近似している
			// (原文コメント: "as an approximation, take the inertia of the box that
			//  bounds the spheres")。物理的に正しい円柱+半球の解析式ではないが、
			// MMD(Bullet 2.75/PMX)との挙動互換を優先し、あえてこの近似を使う。
			// ※ 将来「正しい式」に直さないこと。円柱式は横軸慣性が半分以下になり、
			//   カプセル剛体(髪など)がMMDより過敏に回ってしまう。
			// upAxis は Y (PMX のカプセルは Y 軸方向)。halfExtents = (r,r,r) の Y に halfHeight を加算。
			// ★2026-08-13: Bullet は各 half-extent に CONVEX_DISTANCE_MARGIN(0.04) を足してから
			//   箱慣性を計算する (btCapsuleShape.cpp:136-140)。当エンジンはこの1項が抜けていた。
			//   小さいカプセルほど効く: r=0.15/h=0.30 の前髪で慣性が 26〜38% 過小
			//   (I_x 1.35倍 / I_y 1.60倍)、r=0.60/h=1.50 の髪では 7〜13% にとどまる。
			//   慣性が小さい = 同じトルクでより速く回る = 駆動への過剰応答。
			//   ※箱は Bullet も margin 込みの half-extents を使うので当エンジンと一致済み。ここだけの差。
			//   既定 0 = 従来値 = ビット不変。0.04 で Bullet 準拠。
			const float r = Radius;
			const float m = InertiaMargin;
			const float hx = r + m;
			const float hy = r + HalfHeight() + m;
			const float hz = r + m;
			const float lx = 2.0f * hx;
			const float ly = 2.0f * hy;
			const float lz = 2.0f * hz;
			const float c = Mass / 12.0f; // Bullet の scaledmass = mass * 0.08333333
			return Vec3(
				c * (ly * ly + lz * lz),  // X
				c * (lx * lx + lz * lz),  // Y (up)
				c * (lx * lx + ly * ly)); // Z
		}

		virtual float BoundingRadius() const override { return HalfHeight() + Radius; }
	};
}
