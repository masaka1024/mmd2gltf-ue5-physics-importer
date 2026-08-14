// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – Narrowphase (GJK + EPA)
// 凸形状同士の接触判定・貫入量・法線・接触点を算出する。
// Bullet の btGjkPairDetector + btGjkEpaPenetrationDepthSolver 相当。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/Collision.cs (namespace BulletPhysics)
// ===========================================================================

#pragma once

#include "MmdRigidBody.h"

namespace MmdPhysics
{
	/** 1 つの接触点。ワールド座標系。 */
	struct ContactPoint
	{
		Vec3 PositionWorldA;   // A 上の接触点
		Vec3 PositionWorldB;   // B 上の接触点
		Vec3 LocalPointA;      // A ローカルの接触点 (Refresh 再投影用)
		Vec3 LocalPointB;      // B ローカルの接触点 (Refresh 再投影用)
		Vec3 Normal;           // B から A へ向かう単位法線
		float Distance = 0.0f; // 負値 = 貫入量

		// ソルバ用の蓄積インパルス (ウォームスタート)。
		float NormalImpulse = 0.0f;
		float TangentImpulse1 = 0.0f;
		float TangentImpulse2 = 0.0f;
	};

	/** 剛体ペアの接触点集合 (最大 4 点)。フレーム間で保持する。 */
	class MMDPHYSICSCORE_API PersistentManifold
	{
	public:
		RigidBody* BodyA = nullptr;
		RigidBody* BodyB = nullptr;
		TArray<ContactPoint> Points;

		PersistentManifold(RigidBody* a, RigidBody* b) : BodyA(a), BodyB(b) { Points.Reserve(4); }

		void Refresh();
		void AddPoint(ContactPoint cp);

	private:
		int32 WorstPointIndex(const ContactPoint& Candidate) const;
	};

	/** GJK による距離判定と EPA による貫入解決。 */
	class MMDPHYSICSCORE_API GjkEpa
	{
	public:
		/**
		 * 投機的接触マージン。表面が触れる少し手前から接触を生成して連続化し、
		 * 貫入 on/off の振動 (Baumgarte のエネルギー注入) を防ぐ。Bullet の
		 * collision margin と同じ役割。形状の Radius マージンとは別物 (二重計上しない)。
		 * ★2026-08-13: A/B 用に const → static field 化。既定 0.02 は従来値なのでビット不変。
		 *   貫入調査で判明: この帯は「速度を見ない固定距離」で、駆動剛体は接触点で 1/60 あたり
		 *   法線方向へ中央 0.026 動く = 中央値ですら 1ステップで帯を越える。その結果、貫入の 58% は
		 *   「前フレームに接触点ゼロ」から始まり、生成時点で既に深い。
		 * ★★測定結果: 帯を広げても貫入は直らない (2026-08-13, モデルA 1200フレーム, SubSteps=2)。
		 *   0.02 → 0.15 で「帯を飛び越えた割合」は 58.3% → 7.8% と狙いどおり激減するのに、
		 *   貫入中央は 0.0786 → 0.0812 で**まったく動かない**。0.08 では深貫入>0.5 が 5→20件と悪化。
		 *   = 律速は「検出が遅い」ことではなく「1ステップで押し出しきれない」ことだった。
		 *   → 速度依存マージン (Bullet 2.8x / Box2D の speculative contact) は**不採用**。
		 *   効いたのは刻みを細かくする方 (SubSteps 2→4 で貫入中央 -45%)。dt が小さいほど
		 *   Baumgarte の補正速度 (factor*pen/dt) が大きくなり、押し出し回数も増えるため。
		 *   このつまみは負の結果の再現用に残す。既定 0.02 から動かさないこと。
		 *   ※広げるときは PhysicsWorld のブロードフェーズ AABB も同じだけ広げないと、
		 *     ペアが AABB 段階で捨てられて効かない (PhysicsWorld::BroadphaseNarrowphase 参照)。
		 */
		static float SpeculativeMargin;
		static constexpr float SpeculativeMarginDefault = 0.02f;

		// 安全弁の発動回数 (診断用。PhysicsWorld::DebugContactCount と同様の public フィールド)。
		static int64 EpaIterCapHits;   // 反復上限で打ち切った回数
		static int64 EpaFaceCapHits;   // 面数上限で打ち切った回数

		/**
		 * A と B の接触を判定し、接触点を OutPoints へ格納する (0..複数)。
		 * MMD の球/箱/カプセルは可能な限り解析解で解き、残りは GJK+EPA にフォールバックする。
		 * 法線は A→B、Distance は貫入で負 (既存規約)。
		 */
		static void Detect(RigidBody* a, RigidBody* b, TArray<ContactPoint>& OutPoints);

	private:
		static constexpr int32 MaxIterations = 32;
		static constexpr float Epsilon = 1e-7f;
		// 縮退ガード用の小さな閾値。
		static constexpr float ContactEps = 1e-9f;
		// カプセル軸がほぼ平行とみなす閾値 (sin^2θ)。
		// cross(dA,dB)^2 = |dA|^2|dB|^2 sin^2θ なので、正規化した外積長^2 と比較する。
		// 1e-3 は sinθ≈0.0316 (≈1.8°)。スカート等の面接触が数度以内で平行判定されるよう
		// やや緩めに設定 (2点接触にして転がりを防ぐのが目的)。
		static constexpr float CapsuleParallelSinSq = 1e-3f;
		// 面数の上限。到達時はその時点の最良面で打ち切る (無限ループ/例外にしない)。
		static constexpr int32 MaxFaces = 128;

		struct SupportVert
		{
			Vec3 V;   // Minkowski 差の点
			Vec3 A;   // A 上の support
			Vec3 B;   // B 上の support
		};

		struct Face
		{
			int32 A = 0, B = 0, C = 0;
			Vec3 Normal;
			float Dist = 0.0f;
			bool Valid = false;
		};

		static Vec3 WorldSupport(RigidBody* Body, const Vec3& DirWorld, Vec3& OutWitness);
		static SupportVert Support(RigidBody* a, RigidBody* b, const Vec3& Dir);

		static void Emit(RigidBody* A, RigidBody* B, TArray<ContactPoint>& OutPoints,
			const Vec3& NormalAtoB, float Sep, const Vec3& pA, const Vec3& pB);

		static void SphereSphere(RigidBody* a, RigidBody* b, TArray<ContactPoint>& OutPoints);
		static void SphereCapsule(RigidBody* Sphere, RigidBody* Capsule, bool bSphereIsA, TArray<ContactPoint>& OutPoints);
		static void CapsuleCapsule(RigidBody* a, RigidBody* b, TArray<ContactPoint>& OutPoints);
		static void EmitParallelPoint(RigidBody* a, RigidBody* b, TArray<ContactPoint>& OutPoints,
			const Vec3& a0, const Vec3& dAn, float Param, const Vec3& b0, const Vec3& b1, float rA, float rB, float rsum);
		static bool SolveSphereBoxLocal(const Vec3& Local, const Vec3& he, float r,
			Vec3& OutNLocalBoxToSphere, float& OutSep, Vec3& OutBoxSurfLocal);
		static void SphereBox(RigidBody* Sphere, RigidBody* Box, bool bSphereIsA, TArray<ContactPoint>& OutPoints);
		static void CapsuleBox(RigidBody* Capsule, RigidBody* Box, bool bCapsuleIsA, TArray<ContactPoint>& OutPoints);

		static void CapsuleSegment(RigidBody* Body, Vec3& p0, Vec3& p1, float& OutRadius);
		static Vec3 ClosestPtPointSegment(const Vec3& p, const Vec3& a, const Vec3& b);
		static void ClosestPtSegmentSegment(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2,
			float& s, float& t, Vec3& c1, Vec3& c2);
		static Vec3 PerpVector(const Vec3& Axis);

		static bool GjkEpaPenetration(RigidBody* a, RigidBody* b, ContactPoint& OutContact);
		static bool DoSimplex(TArray<SupportVert>& s, Vec3& Dir);
		static bool Line(TArray<SupportVert>& s, Vec3& Dir);
		static bool Triangle(TArray<SupportVert>& s, Vec3& Dir);
		static bool StarLine(TArray<SupportVert>& s, Vec3& Dir);
		static bool Tetrahedron(TArray<SupportVert>& s, Vec3& Dir);

		static bool Epa(RigidBody* a, RigidBody* b, TArray<SupportVert>& Simplex, ContactPoint& OutContact);
		static void FillContact(RigidBody* a, RigidBody* b, ContactPoint& Contact,
			const Vec3& Normal, float Distance, const Vec3& BaryA, const Vec3& BaryB);
		static void AddIfValid(TArray<Face>& Faces, const Face& f);
		static bool ExpandToTetra(RigidBody* a, RigidBody* b, TArray<SupportVert>& s);
		static Face MakeFace(const TArray<SupportVert>& v, int32 a, int32 b, int32 c);
		static Face MakeFaceOriented(const TArray<SupportVert>& v, int32 a, int32 b, int32 c, int32 Opp);
		static bool TryFindClosestFace(const TArray<Face>& Faces, Face& OutBest);
		static void AddEdge(TArray<TPair<int32, int32>>& Edges, int32 a, int32 b);
		static void BarycentricProject(const TArray<SupportVert>& v, const Face& f, Vec3& OnA, Vec3& OnB);
		static void Barycentric(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c,
			float& u, float& v, float& w);
	};
}
