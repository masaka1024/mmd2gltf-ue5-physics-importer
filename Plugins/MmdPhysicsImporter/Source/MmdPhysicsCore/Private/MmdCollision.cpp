// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/Collision.cs

#include "MmdCollision.h"

namespace MmdPhysics
{
	float GjkEpa::SpeculativeMargin = 0.02f;
	int64 GjkEpa::EpaIterCapHits = 0;
	int64 GjkEpa::EpaFaceCapHits = 0;

	// =====================================================================
	// PersistentManifold
	// =====================================================================

	void PersistentManifold::Refresh()
	{
		// 各接触点をローカル座標から現在姿勢でワールドへ再投影し、
		// 法線方向に離れた/横ずれした点を破棄する。生存点は位置を更新。
		for (int32 i = Points.Num() - 1; i >= 0; i--)
		{
			ContactPoint& cp = Points[i];
			const Vec3 worldA = BodyA->WorldTransform.TransformPoint(cp.LocalPointA);
			const Vec3 worldB = BodyB->WorldTransform.TransformPoint(cp.LocalPointB);
			const Vec3 diff = worldA - worldB;
			const float d = diff.Dot(cp.Normal);
			const Vec3 lateral = diff - cp.Normal * d;
			if (d > 0.04f || lateral.LengthSquared() > 0.04f * 0.04f)
			{
				Points.RemoveAt(i);
				continue;
			}
			// 生存: 再投影した位置と貫入量をソルバへ渡すため更新。
			cp.PositionWorldA = worldA;
			cp.PositionWorldB = worldB;
			cp.Distance = d;
		}
	}

	void PersistentManifold::AddPoint(ContactPoint cp)
	{
		// 近い既存点があればウォームスタート値を引き継いで置換。
		const float mergeDist2 = 0.02f * 0.02f;
		for (int32 i = 0; i < Points.Num(); i++)
		{
			if ((Points[i].PositionWorldA - cp.PositionWorldA).LengthSquared() < mergeDist2)
			{
				cp.NormalImpulse = Points[i].NormalImpulse;
				cp.TangentImpulse1 = Points[i].TangentImpulse1;
				cp.TangentImpulse2 = Points[i].TangentImpulse2;
				Points[i] = cp;
				return;
			}
		}
		if (Points.Num() < 4) Points.Add(cp);
		else Points[WorstPointIndex(cp)] = cp;
	}

	int32 PersistentManifold::WorstPointIndex(const ContactPoint& Candidate) const
	{
		// 最も浅い点を置換候補にする簡易版。
		int32 worst = 0; float maxDist = Points[0].Distance;
		for (int32 i = 1; i < Points.Num(); i++)
		{
			if (Points[i].Distance > maxDist) { maxDist = Points[i].Distance; worst = i; }
		}
		return worst;
	}

	// =====================================================================
	// GjkEpa
	// =====================================================================

	Vec3 GjkEpa::WorldSupport(RigidBody* Body, const Vec3& DirWorld, Vec3& OutWitness)
	{
		const Vec3 localDir = Body->WorldTransform.InverseTransformDirection(DirWorld);
		const Vec3 localP = Body->Shape->LocalSupportWithMargin(localDir);
		OutWitness = Body->WorldTransform.TransformPoint(localP);
		return OutWitness;
	}

	GjkEpa::SupportVert GjkEpa::Support(RigidBody* a, RigidBody* b, const Vec3& Dir)
	{
		Vec3 wa, wb;
		const Vec3 pa = WorldSupport(a, Dir, wa);
		const Vec3 pb = WorldSupport(b, -Dir, wb);
		SupportVert sv;
		sv.V = pa - pb; sv.A = wa; sv.B = wb;
		return sv;
	}

	void GjkEpa::Detect(RigidBody* a, RigidBody* b, TArray<ContactPoint>& OutPoints)
	{
		const EShapeType ta = a->Shape->Type();
		const EShapeType tb = b->Shape->Type();

		if (ta == EShapeType::Sphere && tb == EShapeType::Sphere)
			SphereSphere(a, b, OutPoints);
		else if (ta == EShapeType::Sphere && tb == EShapeType::Capsule)
			SphereCapsule(a, b, /*bSphereIsA=*/true, OutPoints);
		else if (ta == EShapeType::Capsule && tb == EShapeType::Sphere)
			SphereCapsule(b, a, /*bSphereIsA=*/false, OutPoints);
		else if (ta == EShapeType::Capsule && tb == EShapeType::Capsule)
			CapsuleCapsule(a, b, OutPoints);
		else if (ta == EShapeType::Sphere && tb == EShapeType::Box)
			SphereBox(a, b, /*bSphereIsA=*/true, OutPoints);
		else if (ta == EShapeType::Box && tb == EShapeType::Sphere)
			SphereBox(b, a, /*bSphereIsA=*/false, OutPoints);
		else if (ta == EShapeType::Capsule && tb == EShapeType::Box)
			CapsuleBox(a, b, /*bCapsuleIsA=*/true, OutPoints);
		else if (ta == EShapeType::Box && tb == EShapeType::Capsule)
			CapsuleBox(b, a, /*bCapsuleIsA=*/false, OutPoints);
		else
		{
			// 箱×箱 のみ GJK+EPA (タスクBの安全弁で保護)。
			// カプセル×箱は解析化済み: 薄い箱(スカート厚み0.085等)での EPA 縮退による
			// 接触取りこぼし=脚カプセル貫通を避けるため。
			ContactPoint cp;
			if (GjkEpaPenetration(a, b, cp))
				OutPoints.Add(cp);
		}
	}

	// 接触点を A→B 規約で OutPoints へ追加。LocalPoint も必ず埋める。
	void GjkEpa::Emit(RigidBody* A, RigidBody* B, TArray<ContactPoint>& OutPoints,
		const Vec3& NormalAtoB, float Sep, const Vec3& pA, const Vec3& pB)
	{
		ContactPoint cp;
		cp.Normal = NormalAtoB;
		cp.Distance = Sep;
		cp.PositionWorldA = pA;
		cp.PositionWorldB = pB;
		cp.LocalPointA = A->WorldTransform.InverseTransformPoint(pA);
		cp.LocalPointB = B->WorldTransform.InverseTransformPoint(pB);
		OutPoints.Add(cp);
	}

	// --- 球×球 ---
	void GjkEpa::SphereSphere(RigidBody* a, RigidBody* b, TArray<ContactPoint>& OutPoints)
	{
		const Vec3 cA = a->WorldTransform.Origin; const float rA = static_cast<SphereShape*>(a->Shape.Get())->Radius;
		const Vec3 cB = b->WorldTransform.Origin; const float rB = static_cast<SphereShape*>(b->Shape.Get())->Radius;
		const Vec3 dab = cB - cA; const float rsum = rA + rB;
		const float dist2 = dab.LengthSquared();
		const float rlim = rsum + SpeculativeMargin;
		if (dist2 >= rlim * rlim) return;

		const float dist = MSqrt(dist2);
		const Vec3 n = dist > ContactEps ? dab / dist : Vec3::YAxis; // A→B、中心一致は退避
		const float sep = dist - rsum;
		Emit(a, b, OutPoints, n, sep, cA + n * rA, cB - n * rB);
	}

	// --- 球×カプセル (sphere, capsule はどちらが A/B かを bSphereIsA で指定) ---
	void GjkEpa::SphereCapsule(RigidBody* Sphere, RigidBody* Capsule,
		bool bSphereIsA, TArray<ContactPoint>& OutPoints)
	{
		const Vec3 sc = Sphere->WorldTransform.Origin; const float sr = static_cast<SphereShape*>(Sphere->Shape.Get())->Radius;
		Vec3 q0, q1; float cr;
		CapsuleSegment(Capsule, q0, q1, cr);

		const Vec3 cc = ClosestPtPointSegment(sc, q0, q1);
		const Vec3 d = cc - sc; const float rsum = sr + cr;
		const float dist = d.Length();
		if (dist >= rsum + SpeculativeMargin) return;

		const Vec3 nSphereToCap = dist > ContactEps ? d / dist : Vec3::YAxis;
		const float sep = dist - rsum;
		const Vec3 pSphere = sc + nSphereToCap * sr;
		const Vec3 pCap = cc - nSphereToCap * cr;

		if (bSphereIsA)
			Emit(Sphere, Capsule, OutPoints, nSphereToCap, sep, pSphere, pCap);
		else
			Emit(Capsule, Sphere, OutPoints, -nSphereToCap, sep, pCap, pSphere);
	}

	// --- カプセル×カプセル (平行時は2点) ---
	void GjkEpa::CapsuleCapsule(RigidBody* a, RigidBody* b, TArray<ContactPoint>& OutPoints)
	{
		Vec3 a0, a1, b0, b1; float rA, rB;
		CapsuleSegment(a, a0, a1, rA);
		CapsuleSegment(b, b0, b1, rB);
		const Vec3 dA = a1 - a0; const Vec3 dB = b1 - b0;
		const float lenA2 = dA.LengthSquared(), lenB2 = dB.LengthSquared();
		const float rsum = rA + rB;

		// 平行判定 → 重なり区間の両端で2点接触。
		const Vec3 cross = Vec3::Cross(dA, dB);
		const bool parallel = lenA2 > ContactEps && lenB2 > ContactEps &&
			cross.LengthSquared() <= CapsuleParallelSinSq * lenA2 * lenB2;
		if (parallel)
		{
			const int32 before = OutPoints.Num();
			const float LA = MSqrt(lenA2);
			const Vec3 dAn = dA / LA;
			const float tb0 = (b0 - a0).Dot(dAn);
			const float tb1 = (b1 - a0).Dot(dAn);
			const float lo = FMath::Max(0.0f, FMath::Min(tb0, tb1));
			const float hi = FMath::Min(LA, FMath::Max(tb0, tb1));
			if (hi >= lo)
			{
				EmitParallelPoint(a, b, OutPoints, a0, dAn, lo, b0, b1, rA, rB, rsum);
				if (hi > lo + ContactEps)
					EmitParallelPoint(a, b, OutPoints, a0, dAn, hi, b0, b1, rA, rB, rsum);
				if (OutPoints.Num() > before) return; // 2点 (または1点) 生成できた
			}
			// 区間が無い/貫入なし → 単一最近点へフォールバック。
		}

		// 単一最近点。
		float sIgnored, tIgnored; Vec3 c1, c2;
		ClosestPtSegmentSegment(a0, a1, b0, b1, sIgnored, tIgnored, c1, c2);
		const Vec3 d = c2 - c1; const float dist = d.Length();
		if (dist >= rsum + SpeculativeMargin) return;
		const Vec3 n = dist > ContactEps ? d / dist : PerpVector(dA); // A→B、縮退は軸垂直
		const float sep = dist - rsum;
		Emit(a, b, OutPoints, n, sep, c1 + n * rA, c2 - n * rB);
	}

	// 平行カプセルの A軸パラメータ param における接触点を (貫入していれば) 追加。
	void GjkEpa::EmitParallelPoint(RigidBody* a, RigidBody* b, TArray<ContactPoint>& OutPoints,
		const Vec3& a0, const Vec3& dAn, float Param, const Vec3& b0, const Vec3& b1, float rA, float rB, float rsum)
	{
		const Vec3 cA = a0 + dAn * Param;
		const Vec3 cB = ClosestPtPointSegment(cA, b0, b1);
		const Vec3 d = cB - cA; const float dist = d.Length();
		if (dist >= rsum + SpeculativeMargin) return;
		const Vec3 n = dist > ContactEps ? d / dist : PerpVector(dAn);
		const float sep = dist - rsum;
		Emit(a, b, OutPoints, n, sep, cA + n * rA, cB - n * rB);
	}

	// 球中心(箱ローカル座標 local)・箱半サイズ he・実効半径 r から、
	// 「箱→球」ローカル法線・分離(sep<0 で貫入)・箱表面点(ローカル)を解く。
	// 非接触(SpeculativeMargin 超過)なら false。SphereBox / CapsuleBox の共通コア。
	bool GjkEpa::SolveSphereBoxLocal(const Vec3& Local, const Vec3& he, float r,
		Vec3& OutNLocalBoxToSphere, float& OutSep, Vec3& OutBoxSurfLocal)
	{
		const bool inside = FMath::Abs(Local.x) <= he.x &&
			FMath::Abs(Local.y) <= he.y &&
			FMath::Abs(Local.z) <= he.z;
		if (!inside)
		{
			const Vec3 q(
				FMath::Clamp(Local.x, -he.x, he.x),
				FMath::Clamp(Local.y, -he.y, he.y),
				FMath::Clamp(Local.z, -he.z, he.z));
			const Vec3 dl = Local - q; const float dist = dl.Length();
			if (dist >= r + SpeculativeMargin)
			{
				OutNLocalBoxToSphere = Vec3::YAxis; OutSep = 0.0f; OutBoxSurfLocal = q;
				return false;
			}
			OutNLocalBoxToSphere = dist > ContactEps ? dl / dist : Vec3::YAxis;
			OutBoxSurfLocal = q;
			OutSep = dist - r;
			return true;
		}
		// 中心が箱内部 → 最も近い面へ押し出す。
		float best = TNumericLimits<float>::Max(); int32 axis = 0; float sign = 1.0f;
		for (int32 i = 0; i < 3; i++)
		{
			const float toPos = he[i] - Local[i];
			const float toNeg = Local[i] + he[i];
			if (toPos < best) { best = toPos; axis = i; sign = +1.0f; }
			if (toNeg < best) { best = toNeg; axis = i; sign = -1.0f; }
		}
		Vec3 nl = Vec3::Zero; nl[axis] = sign;
		OutNLocalBoxToSphere = nl;
		OutBoxSurfLocal = Local; OutBoxSurfLocal[axis] = sign * he[axis];
		OutSep = -(best + r); // 貫入深さ = 面までの距離 + 半径
		return true;
	}

	// --- 球×箱 (sphere, box を bSphereIsA で指定) ---
	void GjkEpa::SphereBox(RigidBody* Sphere, RigidBody* Box,
		bool bSphereIsA, TArray<ContactPoint>& OutPoints)
	{
		const Vec3 sc = Sphere->WorldTransform.Origin; const float sr = static_cast<SphereShape*>(Sphere->Shape.Get())->Radius;
		const Vec3 he = static_cast<BoxShape*>(Box->Shape.Get())->HalfExtents;
		const RigidTransform bt = Box->WorldTransform;

		const Vec3 local = bt.InverseTransformPoint(sc);
		Vec3 nLocalBoxToSphere; float sep; Vec3 boxSurfLocal;
		if (!SolveSphereBoxLocal(local, he, sr, nLocalBoxToSphere, sep, boxSurfLocal))
			return;

		const Vec3 nWorldBoxToSphere = bt.TransformDirection(nLocalBoxToSphere).Normalized();
		const Vec3 boxSurfWorld = bt.TransformPoint(boxSurfLocal);
		const Vec3 sphereSurfWorld = sc - nWorldBoxToSphere * sr;

		if (bSphereIsA)
			Emit(Sphere, Box, OutPoints, -nWorldBoxToSphere, sep, sphereSurfWorld, boxSurfWorld);
		else
			Emit(Box, Sphere, OutPoints, nWorldBoxToSphere, sep, boxSurfWorld, sphereSurfWorld);
	}

	// --- カプセル×箱 (capsule, box を bCapsuleIsA で指定) ---
	// カプセル線分と箱(OBB)の最近点対を交互射影で近似し、その線分上の点を
	// 「球中心」とみなして球×箱コアで解く。薄い箱でも EPA の縮退を避け、
	// 解析的に安定した接触(法線・貫入量)を与える。
	void GjkEpa::CapsuleBox(RigidBody* Capsule, RigidBody* Box,
		bool bCapsuleIsA, TArray<ContactPoint>& OutPoints)
	{
		Vec3 p0, p1; float cr;
		CapsuleSegment(Capsule, p0, p1, cr);
		const Vec3 he = static_cast<BoxShape*>(Box->Shape.Get())->HalfExtents;
		const RigidTransform bt = Box->WorldTransform;

		// 箱ローカル空間へ (OBB → 原点中心 AABB[-he,he])。距離は剛体変換で不変。
		const Vec3 q0 = bt.InverseTransformPoint(p0);
		const Vec3 q1 = bt.InverseTransformPoint(p1);

		// 線分[q0,q1] と AABB の最近点対を交互射影で求める (凸集合間の最近点)。
		Vec3 boxPt(
			FMath::Clamp((q0.x + q1.x) * 0.5f, -he.x, he.x),
			FMath::Clamp((q0.y + q1.y) * 0.5f, -he.y, he.y),
			FMath::Clamp((q0.z + q1.z) * 0.5f, -he.z, he.z));
		Vec3 segPt = boxPt;
		for (int32 k = 0; k < 8; k++)
		{
			segPt = ClosestPtPointSegment(boxPt, q0, q1);
			const Vec3 np(
				FMath::Clamp(segPt.x, -he.x, he.x),
				FMath::Clamp(segPt.y, -he.y, he.y),
				FMath::Clamp(segPt.z, -he.z, he.z));
			if ((np - boxPt).LengthSquared() <= 1e-12f) { boxPt = np; break; }
			boxPt = np;
		}

		// 線分上の最近点(ローカル)を球中心として球×箱で解く。
		Vec3 nLocalBoxToCap; float sep; Vec3 boxSurfLocal;
		if (!SolveSphereBoxLocal(segPt, he, cr, nLocalBoxToCap, sep, boxSurfLocal))
			return;

		const Vec3 nWorldBoxToCap = bt.TransformDirection(nLocalBoxToCap).Normalized();
		const Vec3 boxSurfWorld = bt.TransformPoint(boxSurfLocal);
		const Vec3 capSurfWorld = bt.TransformPoint(segPt) - nWorldBoxToCap * cr;

		if (bCapsuleIsA)
			Emit(Capsule, Box, OutPoints, -nWorldBoxToCap, sep, capSurfWorld, boxSurfWorld);
		else
			Emit(Box, Capsule, OutPoints, nWorldBoxToCap, sep, boxSurfWorld, capSurfWorld);
	}

	// --- 幾何ヘルパー ---

	// カプセルの線分端点 (ワールド) と半径。マージン機構は使わず素の幾何値を使う。
	void GjkEpa::CapsuleSegment(RigidBody* Body, Vec3& p0, Vec3& p1, float& OutRadius)
	{
		const CapsuleShape* cap = static_cast<const CapsuleShape*>(Body->Shape.Get());
		OutRadius = cap->Radius;
		const float hh = cap->HalfHeight();
		p0 = Body->WorldTransform.TransformPoint(Vec3(0, hh, 0));
		p1 = Body->WorldTransform.TransformPoint(Vec3(0, -hh, 0));
	}

	Vec3 GjkEpa::ClosestPtPointSegment(const Vec3& p, const Vec3& a, const Vec3& b)
	{
		const Vec3 ab = b - a;
		const float denom = ab.LengthSquared();
		if (denom < ContactEps) return a; // 縮退線分
		float t = (p - a).Dot(ab) / denom;
		t = FMath::Clamp(t, 0.0f, 1.0f);
		return a + ab * t;
	}

	// Ericson "Real-Time Collision Detection" ClosestPtSegmentSegment 相当。
	void GjkEpa::ClosestPtSegmentSegment(
		const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2,
		float& s, float& t, Vec3& c1, Vec3& c2)
	{
		const Vec3 d1 = q1 - p1; const Vec3 d2 = q2 - p2; const Vec3 r = p1 - p2;
		const float a = d1.LengthSquared(), e = d2.LengthSquared(), f = d2.Dot(r);

		if (a <= ContactEps && e <= ContactEps)
		{
			s = t = 0.0f; c1 = p1; c2 = p2; return;
		}
		if (a <= ContactEps)
		{
			s = 0.0f; t = FMath::Clamp(f / e, 0.0f, 1.0f);
		}
		else
		{
			const float c = d1.Dot(r);
			if (e <= ContactEps)
			{
				t = 0.0f; s = FMath::Clamp(-c / a, 0.0f, 1.0f);
			}
			else
			{
				const float b = d1.Dot(d2); const float denom = a * e - b * b;
				s = denom != 0.0f ? FMath::Clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
				t = (b * s + f) / e;
				if (t < 0.0f) { t = 0.0f; s = FMath::Clamp(-c / a, 0.0f, 1.0f); }
				else if (t > 1.0f) { t = 1.0f; s = FMath::Clamp((b - c) / a, 0.0f, 1.0f); }
			}
		}
		c1 = p1 + d1 * s;
		c2 = p2 + d2 * t;
	}

	// 与えた軸に垂直な単位ベクトル (縮退法線のフォールバック)。
	Vec3 GjkEpa::PerpVector(const Vec3& Axis)
	{
		const Vec3 a = Axis.Normalized();
		const Vec3 reference = FMath::Abs(a.x) < 0.9f ? Vec3::XAxis : Vec3::YAxis;
		const Vec3 perp = Vec3::Cross(a, reference);
		const float len = perp.Length();
		return len > ContactEps ? perp / len : Vec3::YAxis;
	}

	/** A と B の貫入を GJK+EPA で解く (フォールバック用)。接触があれば true。 */
	bool GjkEpa::GjkEpaPenetration(RigidBody* a, RigidBody* b, ContactPoint& OutContact)
	{
		OutContact = ContactPoint();

		// --- GJK: 原点が Minkowski 差に含まれるか ---
		TArray<SupportVert> simplex;
		simplex.Reserve(4);
		Vec3 dir = a->WorldTransform.Origin - b->WorldTransform.Origin;
		if (dir.LengthSquared() < Epsilon) dir = Vec3::XAxis;

		simplex.Add(Support(a, b, dir));
		dir = -simplex[0].V;

		for (int32 iter = 0; iter < MaxIterations; iter++)
		{
			if (dir.LengthSquared() < Epsilon) break;
			const SupportVert p = Support(a, b, dir);
			if (p.V.Dot(dir) < 0)
				return false; // 原点を越えられない → 分離
			simplex.Add(p);
			if (DoSimplex(simplex, dir))
			{
				// 原点包含 → EPA で貫入解決。
				return Epa(a, b, simplex, OutContact);
			}
		}
		return false;
	}

	// GJK simplex 更新。原点を含んだら true。
	bool GjkEpa::DoSimplex(TArray<SupportVert>& s, Vec3& Dir)
	{
		if (s.Num() == 2) return Line(s, Dir);
		if (s.Num() == 3) return Triangle(s, Dir);
		return Tetrahedron(s, Dir);
	}

	bool GjkEpa::Line(TArray<SupportVert>& s, Vec3& Dir)
	{
		const Vec3 a = s[1].V; const Vec3 b = s[0].V;
		const Vec3 ab = b - a; const Vec3 ao = -a;
		if (ab.Dot(ao) > 0)
			Dir = Vec3::Cross(Vec3::Cross(ab, ao), ab);
		else { s.RemoveAt(0); Dir = ao; }
		return false;
	}

	bool GjkEpa::Triangle(TArray<SupportVert>& s, Vec3& Dir)
	{
		const Vec3 a = s[2].V; const Vec3 b = s[1].V; const Vec3 c = s[0].V;
		const Vec3 ab = b - a; const Vec3 ac = c - a; const Vec3 ao = -a;
		const Vec3 abc = Vec3::Cross(ab, ac);

		if (Vec3::Cross(abc, ac).Dot(ao) > 0)
		{
			if (ac.Dot(ao) > 0) { s.RemoveAt(1); Dir = Vec3::Cross(Vec3::Cross(ac, ao), ac); }
			else return StarLine(s, Dir);
		}
		else if (Vec3::Cross(ab, abc).Dot(ao) > 0)
		{
			return StarLine(s, Dir);
		}
		else
		{
			if (abc.Dot(ao) > 0) { Dir = abc; }
			else { Swap(s[0], s[1]); Dir = -abc; }
			return false;
		}
		return false;
	}

	bool GjkEpa::StarLine(TArray<SupportVert>& s, Vec3& Dir)
	{
		// 辺 AB を残す。
		s.RemoveAt(0);
		return Line(s, Dir);
	}

	bool GjkEpa::Tetrahedron(TArray<SupportVert>& s, Vec3& Dir)
	{
		const Vec3 a = s[3].V; const Vec3 b = s[2].V; const Vec3 c = s[1].V; const Vec3 d = s[0].V;
		const Vec3 ao = -a;
		const Vec3 abc = Vec3::Cross(b - a, c - a);
		const Vec3 acd = Vec3::Cross(c - a, d - a);
		const Vec3 adb = Vec3::Cross(d - a, b - a);

		if (abc.Dot(ao) > 0) { s.RemoveAt(0); Dir = abc; return Triangle(s, Dir); }
		if (acd.Dot(ao) > 0) { s.RemoveAt(2); Dir = acd; return Triangle(s, Dir); }
		if (adb.Dot(ao) > 0) { s.RemoveAt(1); Dir = adb; return Triangle(s, Dir); }
		return true; // 原点は四面体内部
	}

	// --- EPA: 貫入方向と深さ ---
	bool GjkEpa::Epa(RigidBody* a, RigidBody* b, TArray<SupportVert>& Simplex, ContactPoint& OutContact)
	{
		OutContact = ContactPoint();
		if (Simplex.Num() < 4) { if (!ExpandToTetra(a, b, Simplex)) return false; }

		TArray<SupportVert> verts(Simplex);
		// 初期四面体: 各面を対頂点から見て外向きになるよう巻き方向を正規化する。
		TArray<Face> faces;
		AddIfValid(faces, MakeFaceOriented(verts, 0, 1, 2, 3));
		AddIfValid(faces, MakeFaceOriented(verts, 0, 1, 3, 2));
		AddIfValid(faces, MakeFaceOriented(verts, 0, 2, 3, 1));
		AddIfValid(faces, MakeFaceOriented(verts, 1, 2, 3, 0));

		Face closest;
		bool converged = false, faceCap = false;
		TArray<TPair<int32, int32>> edges;

		for (int32 iter = 0; iter < MaxIterations; iter++)
		{
			if (!TryFindClosestFace(faces, closest)) break; // 有効面が無い → フォールバック

			const SupportVert p = Support(a, b, closest.Normal);
			const float d = p.V.Dot(closest.Normal);
			// 相対許容差での収束判定 (絶対値だと曲面で収束前に反復上限に達する)。
			if (d - closest.Dist < closest.Dist * 1e-3f + 1e-5f) { converged = true; break; }

			// p から見える面を削除し、輪郭 (horizon) を抽出。
			edges.Reset();
			for (int32 i = faces.Num() - 1; i >= 0; i--)
			{
				if (faces[i].Normal.Dot(p.V - verts[faces[i].A].V) > 0)
				{
					AddEdge(edges, faces[i].A, faces[i].B);
					AddEdge(edges, faces[i].B, faces[i].C);
					AddEdge(edges, faces[i].C, faces[i].A);
					faces.RemoveAt(i);
				}
			}
			const int32 newIndex = verts.Num();
			verts.Add(p);
			// 新規面は巻き方向を輪郭から継承 (反転しない)。縮退面は破棄。
			for (const TPair<int32, int32>& e : edges)
				AddIfValid(faces, MakeFace(verts, e.Key, e.Value, newIndex));

			if (faces.Num() == 0) break;
			if (faces.Num() > MaxFaces) { faceCap = true; break; }
		}

		if (faceCap) EpaFaceCapHits++;
		else if (!converged) EpaIterCapHits++;

		// 打ち切り後も最良の有効面で接触を返す。
		if (!TryFindClosestFace(faces, closest))
		{
			// 有効面ゼロの退化: NaN を返さず中心方向の安全な法線でフォールバック。
			const Vec3 dir = b->WorldTransform.Origin - a->WorldTransform.Origin;
			const Vec3 nf = dir.LengthSquared() > Epsilon ? dir.Normalized() : Vec3::YAxis;
			const Vec3 pa0 = a->WorldTransform.Origin;
			const Vec3 pb0 = b->WorldTransform.Origin;
			FillContact(a, b, OutContact, nf, -Epsilon, pa0, pb0);
			return true;
		}

		Vec3 baryA, baryB;
		BarycentricProject(verts, closest, baryA, baryB);
		FillContact(a, b, OutContact, closest.Normal, -closest.Dist, baryA, baryB);
		return true;
	}

	void GjkEpa::FillContact(RigidBody* a, RigidBody* b, ContactPoint& Contact,
		const Vec3& Normal, float Distance, const Vec3& BaryA, const Vec3& BaryB)
	{
		Contact.Normal = Normal;
		Contact.Distance = Distance; // 貫入は負
		Contact.PositionWorldA = BaryA;
		Contact.PositionWorldB = BaryB;
		// 剛体移動後に再投影できるようローカル座標も保持。
		Contact.LocalPointA = a->WorldTransform.InverseTransformPoint(BaryA);
		Contact.LocalPointB = b->WorldTransform.InverseTransformPoint(BaryB);
	}

	void GjkEpa::AddIfValid(TArray<Face>& Faces, const Face& f)
	{
		if (f.Valid) Faces.Add(f);
	}

	bool GjkEpa::ExpandToTetra(RigidBody* a, RigidBody* b, TArray<SupportVert>& s)
	{
		// 退化 simplex を四面体まで補う (稀ケースの保険)。
		const Vec3 dirs[] = { Vec3::XAxis, Vec3::YAxis, Vec3::ZAxis, -Vec3::XAxis, -Vec3::YAxis, -Vec3::ZAxis };
		for (const Vec3& d : dirs)
		{
			if (s.Num() >= 4) break;
			const SupportVert p = Support(a, b, d);
			bool dup = false;
			for (const SupportVert& e : s) { if ((e.V - p.V).LengthSquared() < 1e-8f) { dup = true; break; } }
			if (!dup) s.Add(p);
		}
		return s.Num() >= 4;
	}

	// 巻き方向から外向き法線を一意に決める (反転しない)。縮退面は Valid=false。
	GjkEpa::Face GjkEpa::MakeFace(const TArray<SupportVert>& v, int32 a, int32 b, int32 c)
	{
		Vec3 n = Vec3::Cross(v[b].V - v[a].V, v[c].V - v[a].V);
		const float len = n.Length();
		if (len <= Epsilon) { Face f; f.Valid = false; return f; } // 縮退は破棄
		n = n / len;
		Face f;
		f.A = a; f.B = b; f.C = c; f.Normal = n; f.Dist = n.Dot(v[a].V); f.Valid = true;
		return f;
	}

	// 初期四面体用: 対頂点 opp から見て外向きになるよう頂点順序を正規化する。
	GjkEpa::Face GjkEpa::MakeFaceOriented(const TArray<SupportVert>& v, int32 a, int32 b, int32 c, int32 Opp)
	{
		Vec3 n = Vec3::Cross(v[b].V - v[a].V, v[c].V - v[a].V);
		const float len = n.Length();
		if (len <= Epsilon) { Face f; f.Valid = false; return f; }
		n = n / len;
		// n が対頂点側を向いていたら内向き → 巻き方向を反転して外向きに揃える。
		if (n.Dot(v[Opp].V - v[a].V) > 0) { Swap(b, c); n = -n; }
		Face f;
		f.A = a; f.B = b; f.C = c; f.Normal = n; f.Dist = n.Dot(v[a].V); f.Valid = true;
		return f;
	}

	// 原点に最も近い有効面を返す。原点が外側になる不正面 (Dist<0) は無視。
	bool GjkEpa::TryFindClosestFace(const TArray<Face>& Faces, Face& OutBest)
	{
		OutBest = Face(); bool found = false; float bd = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Faces.Num(); i++)
		{
			const Face& f = Faces[i];
			if (!f.Valid || f.Dist < -1e-6f) continue;
			if (f.Dist < bd) { bd = f.Dist; OutBest = f; found = true; }
		}
		return found;
	}

	void GjkEpa::AddEdge(TArray<TPair<int32, int32>>& Edges, int32 a, int32 b)
	{
		// 反対向きの辺があれば相殺 (穴の輪郭抽出)。
		for (int32 i = 0; i < Edges.Num(); i++)
		{
			if (Edges[i].Key == b && Edges[i].Value == a) { Edges.RemoveAt(i); return; }
		}
		Edges.Add(TPair<int32, int32>(a, b));
	}

	void GjkEpa::BarycentricProject(const TArray<SupportVert>& v, const Face& f, Vec3& OnA, Vec3& OnB)
	{
		// 原点を面へ射影し、重心座標で A/B 上の witness を補間。
		const SupportVert& pa = v[f.A]; const SupportVert& pb = v[f.B]; const SupportVert& pc = v[f.C];
		const Vec3 proj = f.Normal * f.Dist;
		float u, vv, w;
		Barycentric(proj, pa.V, pb.V, pc.V, u, vv, w);
		OnA = pa.A * u + pb.A * vv + pc.A * w;
		OnB = pa.B * u + pb.B * vv + pc.B * w;
	}

	void GjkEpa::Barycentric(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c,
		float& u, float& v, float& w)
	{
		const Vec3 v0 = b - a; const Vec3 v1 = c - a; const Vec3 v2 = p - a;
		const float d00 = v0.Dot(v0); const float d01 = v0.Dot(v1); const float d11 = v1.Dot(v1);
		const float d20 = v2.Dot(v0); const float d21 = v2.Dot(v1);
		const float denom = d00 * d11 - d01 * d01;
		if (FMath::Abs(denom) < Epsilon) { u = 1; v = 0; w = 0; return; }
		v = (d11 * d20 - d01 * d21) / denom;
		w = (d00 * d21 - d01 * d20) / denom;
		u = 1.0f - v - w;
	}
}
