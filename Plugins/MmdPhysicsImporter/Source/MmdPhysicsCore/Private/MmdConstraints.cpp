// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/Constraints.cs

#include "MmdConstraints.h"

namespace MmdPhysics
{
	float Joint::MaxCorrectionVel = 10.0f;
	bool Joint::AngularMixedAxes = false;
	int32 Joint::LinearLeverMode = 0;
	int64 Joint::WarmAngRows = 0;
	int64 Joint::WarmAngToggles = 0;
	float Joint::WarmStartFactor = 0.85f;

	TSharedPtr<Joint> Joint::FromPmx(
		EJointType InType, RigidBody* a, RigidBody* b,
		const RigidTransform& WorldFrame,
		const Vec3& LinLo, const Vec3& LinHi, const Vec3& AngLo, const Vec3& AngHi,
		const Vec3& SpringLin, const Vec3& SpringAng)
	{
		TSharedPtr<Joint> j = MakeShared<Joint>();
		j->Type = InType;
		j->BodyA = a;
		j->BodyB = b;
		j->LinearLowerLimit = LinLo;
		j->LinearUpperLimit = LinHi;
		j->AngularLowerLimit = AngLo;
		j->AngularUpperLimit = AngHi;
		j->SpringLinear = SpringLin;
		j->SpringAngular = SpringAng;

		// フレームを各剛体ローカルへ落とし込む。
		j->FrameInA = a != nullptr ? a->WorldTransform.InverseTimes(WorldFrame) : WorldFrame;
		j->FrameInB = b != nullptr ? b->WorldTransform.InverseTimes(WorldFrame) : WorldFrame;

		// Joint 種ごとの制限の解釈調整 (仕様の対応表に準拠)。
		switch (InType)
		{
		case EJointType::Point2Point:
			// 各制限/バネ無効。並進のみ固定。
			j->AngularLowerLimit = Vec3(1.0f); // lo>hi = 回転フリー
			j->AngularUpperLimit = Vec3(-1.0f);
			j->LinearLowerLimit = j->LinearUpperLimit = Vec3::Zero;
			j->SpringLinear = j->SpringAngular = Vec3::Zero;
			break;

		case EJointType::Hinge:
			// X 軸のみ回転可。並進固定。
			j->LinearLowerLimit = j->LinearUpperLimit = Vec3::Zero;
			j->AngularLowerLimit = Vec3(AngLo.x, 0, 0);
			j->AngularUpperLimit = Vec3(AngHi.x, 0, 0);
			break;

		case EJointType::Slider:
			// X 軸のみ並進/回転可。
			j->LinearLowerLimit = Vec3(LinLo.x, 0, 0);
			j->LinearUpperLimit = Vec3(LinHi.x, 0, 0);
			j->AngularLowerLimit = Vec3(AngLo.x, 0, 0);
			j->AngularUpperLimit = Vec3(AngHi.x, 0, 0);
			break;

		case EJointType::ConeTwist:
			// 並進固定。回転は円錐 (Y,Z) + 捻り (X)。
			j->LinearLowerLimit = j->LinearUpperLimit = Vec3::Zero;
			break;

		case EJointType::Generic6Dof:
			// バネ無効。
			j->SpringLinear = j->SpringAngular = Vec3::Zero;
			break;

		case EJointType::Spring6Dof:
		default:
			break;
		}
		return j;
	}

	void Joint::Prepare(float dt, bool bSplitBias, bool bWarmStart, bool bWarmStartAng)
	{
		_splitBias = bSplitBias;
		_warmStart = bWarmStart;
		_warmStartAng = bWarmStartAng;
		_warmLinSeen[0] = _warmLinSeen[1] = _warmLinSeen[2] = false;
		_warmAngSeen[0] = _warmAngSeen[1] = _warmAngSeen[2] = false;
		_rows.Reset();
		if (BodyA == nullptr || BodyB == nullptr) return;

		_worldA = BodyA->WorldTransform * FrameInA;
		_worldB = BodyB->WorldTransform * FrameInB;
		_anchorA = _worldA.Origin;
		_anchorB = _worldB.Origin;

		const Vec3 rA = _anchorA - BodyA->CenterOfMass();
		const Vec3 rB = _anchorB - BodyB->CenterOfMass();

		const Matrix3x3 basisA = Matrix3x3::FromQuat(_worldA.Rotation);
		_axesA[0] = basisA.Column(0);
		_axesA[1] = basisA.Column(1);
		_axesA[2] = basisA.Column(2);

		const float invDt = dt > 0 ? 1.0f / dt : 0.0f;
		const Vec3 linDelta = _anchorB - _anchorA;

		// 回転相対 (角度行と、LinearLeverMode=2 の rotAllowed 判定で使用)。
		const Quat qRel = _worldA.Rotation.Conjugated() * _worldB.Rotation;
		const Vec3 euler = ToEulerXYZ(qRel.Normalized());

		// LinearLeverMode=2 用の前計算 (Bullet calculateTransforms / setLinearLimits 相当)。
		bool hasStatic = false; float factA = 0.5f, factB = 0.5f;
		bool angActive[3] = { false, false, false };
		if (LinearLeverMode == 2)
		{
			const float miA = BodyA->InverseMass, miB = BodyB->InverseMass;
			hasStatic = miA < 1e-9f || miB < 1e-9f;
			const float miS = miA + miB;
			factA = miS > 0.0f ? miB / miS : 0.5f; factB = 1.0f - factA;
			for (int32 i = 0; i < 3; i++)
			{
				const float alo = AngularLowerLimit[i], ahi = AngularUpperLimit[i];
				// Bullet testLimitValue 相当: free=0 / 範囲外 or ロック(誤差あり)=active
				angActive[i] = !IsFree(alo, ahi) && (IsLocked(alo, ahi) ? euler[i] != alo : (euler[i] < alo || euler[i] > ahi));
			}
		}

		// --- 並進 3 軸 ---
		for (int32 i = 0; i < 3; i++)
		{
			const Vec3 axis = _axesA[i];
			const float lo = LinearLowerLimit[i], hi = LinearUpperLimit[i];
			if (IsFree(lo, hi)) continue;

			const float cur = linDelta.Dot(axis);
			float err, lower, upper;
			if (IsLocked(lo, hi)) { err = lo - cur; lower = -1e18f; upper = 1e18f; }
			else if (cur < lo) { err = lo - cur; lower = 0.0f; upper = 1e18f; }
			else if (cur > hi) { err = hi - cur; lower = -1e18f; upper = 0.0f; }
			else continue; // 制限内 → バネのみ (後段)

			// レバーアームをモード別に決定 (LinearLeverMode コメント参照)。
			Vec3 armA = rA, armB = rB;
			if (LinearLeverMode == 1)
			{
				armA = _anchorB - BodyA->CenterOfMass();   // 両剛体とも B側アンカー基準
				armB = rB;
			}
			else if (LinearLeverMode == 2)
			{
				// Bullet get_limit_motor_info2 (m_useOffsetForConstraintFrame) の翻訳分岐そのまま。
				const float pA = rA.Dot(axis), pB = rB.Dot(axis);
				const Vec3 orthoA = rA - axis * pA;
				const Vec3 orthoB = rB - axis * pB;
				const float desiredOffs = cur + err;               // = 目標バウンド (locked なら lo)
				const Vec3 totalDist = axis * (pA + desiredOffs - pB);
				armA = orthoA + totalDist * factA;
				armB = orthoB - totalDist * factB;
				const bool rotAllowed = !(angActive[(i + 1) % 3] && angActive[(i + 2) % 3]);
				if (hasStatic && !rotAllowed) { armA *= factA; armB *= factB; }
			}

			AddLinearRow(axis, armA, armB, Clamp(err * Beta * invDt), lower, upper, i, IsLocked(lo, hi));
		}

		// --- 回転 3 軸 ---
		// Bullet混合軸 (AngularMixedAxes=true): calculateAngleInfo をそのまま再現。
		Vec3 mix0, mix1, mix2;
		if (AngularMixedAxes)
		{
			const Matrix3x3 basisB = Matrix3x3::FromQuat(_worldB.Rotation);
			const Vec3 axis0 = basisB.Column(0);   // B col0
			const Vec3 axis2 = _axesA[2];          // A col2
			const Vec3 m1 = Vec3::Cross(axis2, axis0);
			const Vec3 m0 = Vec3::Cross(m1, axis2);
			const Vec3 m2 = Vec3::Cross(axis0, m1);
			mix0 = m0.Normalized(); mix1 = m1.Normalized(); mix2 = m2.Normalized();
		}
		for (int32 i = 0; i < 3; i++)
		{
			const Vec3 axis = AngularMixedAxes ? (i == 0 ? mix0 : i == 1 ? mix1 : mix2) : _axesA[i];
			const float lo = AngularLowerLimit[i], hi = AngularUpperLimit[i];
			if (IsFree(lo, hi)) continue;

			const float cur = euler[i];
			float err, lower, upper;
			int32 sideCode;
			if (IsLocked(lo, hi)) { err = lo - cur; lower = -1e18f; upper = 1e18f; sideCode = 0; }
			else if (cur < lo) { err = lo - cur; lower = 0.0f; upper = 1e18f; sideCode = 1; }
			else if (cur > hi) { err = hi - cur; lower = -1e18f; upper = 0.0f; sideCode = 2; }
			else continue;

			AddAngularRow(axis, Clamp(err * Beta * invDt), lower, upper, i, sideCode);
		}

		// warm-start: 今サブステップで warm 対象にならなかった DOF の蓄積は破棄する
		// (常時アクティブでない DOF の古い力積を誤って引き継がないため)。
		if (_warmStart) for (int32 i = 0; i < 3; i++) if (!_warmLinSeen[i]) _warmLin[i] = 0.0f;
		if (_warmStartAng) for (int32 i = 0; i < 3; i++) if (!_warmAngSeen[i]) { _warmAng[i] = 0.0f; _warmAngPrevSide[i] = -1; }

		// 前サブステップの蓄積インパルスを反復前に一度適用する (Bullet の warm-start と同型)。
		if (_warmStart || _warmStartAng)
		{
			for (int32 r = 0; r < _rows.Num(); r++)
			{
				const ConstraintRow& row = _rows[r];
				if (!row.WarmStartable) continue;
				if (row.Angular)
				{
					const Vec3 L = row.Axis * row.Accumulated;
					BodyA->ApplyTorqueImpulse(-L);
					BodyB->ApplyTorqueImpulse(L);
				}
				else
				{
					const Vec3 P = row.Axis * row.Accumulated;
					BodyA->ApplyImpulse(-P, row.RelA);
					BodyB->ApplyImpulse(P, row.RelB);
				}
			}
		}
	}

	float Joint::Clamp(float v)
	{
		return FMath::Max(-MaxCorrectionVel, FMath::Min(MaxCorrectionVel, v));
	}

	void Joint::AddLinearRow(const Vec3& Axis, const Vec3& rA, const Vec3& rB, float TargetVel, float lo, float hi, int32 Dof, bool bLocked)
	{
		const Vec3 rAxn = Vec3::Cross(rA, Axis);
		const Vec3 rBxn = Vec3::Cross(rB, Axis);
		const float k = BodyA->InverseMass + BodyB->InverseMass
			+ rAxn.Dot(BodyA->InverseInertiaWorld * rAxn)
			+ rBxn.Dot(BodyB->InverseInertiaWorld * rBxn);
		// (a-1) 直線ロック行のみ warm-start: 前サブステップの蓄積で初期化する。
		const bool warm = _warmStart && bLocked;
		float acc = 0.0f;
		if (warm) { _warmLinSeen[Dof] = true; acc = FMath::Max(lo, FMath::Min(hi, _warmLin[Dof] * WarmStartFactor)); }

		ConstraintRow row;
		row.Axis = Axis; row.Angular = false; row.RelA = rA; row.RelB = rB;
		row.LowerImpulse = lo; row.UpperImpulse = hi;
		row.TargetVel = _splitBias ? 0.0f : TargetVel;
		row.PositionBias = _splitBias ? TargetVel : 0.0f;
		row.EffMass = k > 0 ? 1.0f / k : 0.0f;
		row.Dof = Dof; row.WarmStartable = warm; row.Accumulated = acc;
		_rows.Add(row);
	}

	void Joint::AddAngularRow(const Vec3& Axis, float TargetVel, float lo, float hi, int32 Dof, int32 SideCode)
	{
		const float k = Axis.Dot(BodyA->InverseInertiaWorld * Axis)
			+ Axis.Dot(BodyB->InverseInertiaWorld * Axis);
		// (a-2) 角度 warm-start: 前サブステップと同じ側(sideCode)なら蓄積を引き継ぎ、
		// 側が変わった/不在だった場合は破棄(0スタート)。トグル頻度を診断カウント。
		bool warm = false;
		float acc = 0.0f;
		if (_warmStartAng)
		{
			warm = true;
			_warmAngSeen[Dof] = true;
			WarmAngRows++;
			const bool sameSide = _warmAngPrevSide[Dof] == SideCode;
			if (!sameSide) WarmAngToggles++;
			acc = sameSide ? FMath::Max(lo, FMath::Min(hi, _warmAng[Dof] * WarmStartFactor)) : 0.0f;
			_warmAngPrevSide[Dof] = SideCode;
		}

		ConstraintRow row;
		row.Axis = Axis; row.Angular = true;
		row.LowerImpulse = lo; row.UpperImpulse = hi;
		row.TargetVel = _splitBias ? 0.0f : TargetVel;
		row.PositionBias = _splitBias ? TargetVel : 0.0f;
		row.EffMass = k > 0 ? 1.0f / k : 0.0f;
		row.Dof = Dof; row.WarmStartable = warm; row.Accumulated = acc;
		_rows.Add(row);
	}

	// --- バネ (明示力積, サブステップ毎に 1 回) ---
	//
	// ★安定化クランプ (2026-08-10)。
	//   陽的(前進オイラー)ばね impulse = -k*err*dt は k*dt²/m が 1 を超えると必ず発散する。
	//   1ステップの速度変化が k*err*dt/m、それが次ステップの誤差を |err| より大きくするため、
	//   誤差が毎ステップ k*dt²/m 倍に増幅されるからである。
	//   実測: ぬこ式レーシングミク2023 は spring=100000 に対し錘の質量 0.01 で
	//   k*dt²/m = 2778 → 毎ステップ約30倍に発散し、25ステップで float が溢れて NaN になった
	//   (ツインテール先端の錘が起点。重力ゼロでも発散するので外力ではなくばねが原因)。
	//   PMXモデルの 6/13 が同じ発散域にあり、これまで表面化しなかったのは忠実度検証に使っていた
	//   IA のジョイントがばね定数ゼロだったため（＝1モデルでの検証は安定性を保証しない）。
	//
	//   対策: 力積を「1ステップで誤差をちょうど打ち消す量」= |err| * mEff / dt で頭打ちにする。
	//   これは k*dt²/m = 1（デッドビート）に相当する安定限界で、行き過ぎが原理的に起きない。
	//   mEff はソルバ本体 (AddLinearRow / AddAngularRow) と同一の式で求めるため、
	//   クランプが効かない範囲 (k*dt²/m < 1) では従来と1ビットも変わらない。
	//   実測A/B(300step 全剛体姿勢ハッシュ): IA系3モデルはビット完全一致、Racing_Miku2023 は NaN→完走。
	void Joint::ApplySprings(float dt)
	{
		if (BodyA == nullptr || BodyB == nullptr) return;
		const bool hasLin = SpringLinear.LengthSquared() > 0;
		const bool hasAng = SpringAngular.LengthSquared() > 0;
		if (!hasLin && !hasAng) return;
		const float invDtSpring = dt > 0 ? 1.0f / dt : 0.0f;

		const Vec3 rA = _anchorA - BodyA->CenterOfMass();
		const Vec3 rB = _anchorB - BodyB->CenterOfMass();
		const Vec3 linDelta = _anchorB - _anchorA;

		if (hasLin)
		{
			for (int32 i = 0; i < 3; i++)
			{
				const float k = SpringLinear[i];
				if (k <= 0) continue;
				const Vec3 axis = _axesA[i];
				const float eq = ClampToLimit(0.0f, LinearLowerLimit[i], LinearUpperLimit[i]);
				const float err = linDelta.Dot(axis) - eq;
				// Bullet の 6DOF バネには速度比例の粘性項が無いので付けない (force = -delta*k のみ)。
				float impulse = (-k * err) * dt;
				// 安定化: AddLinearRow と同一の実効質量でデッドビート量に頭打ち。
				const Vec3 rAxn = Vec3::Cross(rA, axis);
				const Vec3 rBxn = Vec3::Cross(rB, axis);
				const float invMEff = BodyA->InverseMass + BodyB->InverseMass
					+ rAxn.Dot(BodyA->InverseInertiaWorld * rAxn)
					+ rBxn.Dot(BodyB->InverseInertiaWorld * rBxn);
				impulse = ClampSpringImpulse(impulse, err, invMEff, invDtSpring);
				const Vec3 P = axis * impulse;
				BodyA->ApplyImpulse(-P, rA);
				BodyB->ApplyImpulse(P, rB);
			}
		}

		if (hasAng)
		{
			const Quat qRel = _worldA.Rotation.Conjugated() * _worldB.Rotation;
			const Vec3 euler = ToEulerXYZ(qRel.Normalized());
			for (int32 i = 0; i < 3; i++)
			{
				const float k = SpringAngular[i];
				if (k <= 0) continue;
				const Vec3 axis = _axesA[i];
				const float eq = ClampToLimit(0.0f, AngularLowerLimit[i], AngularUpperLimit[i]);
				const float err = euler[i] - eq;
				// Bullet の 6DOF バネには速度比例の粘性項が無いので付けない (force = -delta*k のみ)。
				float impulse = (-k * err) * dt;
				// 安定化: AddAngularRow と同一の実効慣性でデッドビート量に頭打ち。
				const float invMEff = axis.Dot(BodyA->InverseInertiaWorld * axis)
					+ axis.Dot(BodyB->InverseInertiaWorld * axis);
				impulse = ClampSpringImpulse(impulse, err, invMEff, invDtSpring);
				const Vec3 L = axis * impulse;
				BodyA->ApplyTorqueImpulse(-L);
				BodyB->ApplyTorqueImpulse(L);
			}
		}
	}

	/**
	 * 陽的ばねの力積を安定限界（デッドビート＝1ステップで誤差をちょうど打ち消す量）で
	 * 頭打ちにする。invMEff はソルバ行と同じ「1/実効質量」。これを超える力積は必ず行き過ぎを生み、
	 * 誤差が毎ステップ増幅されて発散する。k*dt²/m < 1 の健全なモデルでは一度も発動しない。
	 */
	float Joint::ClampSpringImpulse(float Impulse, float Err, float InvMEff, float InvDt)
	{
		if (InvMEff <= 0.0f || InvDt <= 0.0f) return Impulse; // 両方静的 等
		const float maxImp = FMath::Abs(Err) / InvMEff * InvDt;   // |err| * mEff / dt
		if (Impulse > maxImp) return maxImp;
		if (Impulse < -maxImp) return -maxImp;
		return Impulse;
	}

	float Joint::ClampToLimit(float v, float lo, float hi)
	{
		if (lo > hi) return v;      // free
		return FMath::Max(lo, FMath::Min(hi, v));
	}

	void Joint::SolveVelocity()
	{
		// ★C# は List<struct> の値コピー + 書き戻しだが、C++ の参照アクセスと意味は同一。
		for (int32 r = 0; r < _rows.Num(); r++)
		{
			ConstraintRow& row = _rows[r];
			// 線形行は行のレバーアーム(RelA/RelB)で速度を測る (J整合)。mode0 では
			// RelA/RelB = anchor−COM なので従来の VelocityAtPoint(anchor) とビット同一。
			const float relVel = row.Angular
				? (BodyB->AngularVelocity - BodyA->AngularVelocity).Dot(row.Axis)
				: ((BodyB->LinearVelocity + Vec3::Cross(BodyB->AngularVelocity, row.RelB))
					- (BodyA->LinearVelocity + Vec3::Cross(BodyA->AngularVelocity, row.RelA))).Dot(row.Axis);

			float dImpulse = (row.TargetVel - relVel) * row.EffMass;
			const float old = row.Accumulated;
			row.Accumulated = FMath::Max(row.LowerImpulse, FMath::Min(row.UpperImpulse, old + dImpulse));
			dImpulse = row.Accumulated - old;

			if (row.Angular)
			{
				const Vec3 L = row.Axis * dImpulse;
				BodyA->ApplyTorqueImpulse(-L);
				BodyB->ApplyTorqueImpulse(L);
			}
			else
			{
				const Vec3 P = row.Axis * dImpulse;
				BodyA->ApplyImpulse(-P, row.RelA);
				BodyB->ApplyImpulse(P, row.RelB);
			}
			// warm-start: 行の最終累積を次サブステップへ引き継ぐ (角度→_warmAng / 直線→_warmLin)。
			if (row.WarmStartable) { if (row.Angular) _warmAng[row.Dof] = row.Accumulated; else _warmLin[row.Dof] = row.Accumulated; }
		}
	}

	// 実速度は SolveVelocity が target=0 (バイアス抜き) で解き、位置誤差 err の補正はここで
	// 擬似速度へ積む。擬似速度は位置積分にのみ反映され実速度には残らないため、Baumgarte が
	// 実速度へエネルギーを注入せず、以後の warm-start を安定化できる (Bullet の split-impulse と同型)。
	void Joint::SolveSplitPosition()
	{
		for (int32 r = 0; r < _rows.Num(); r++)
		{
			ConstraintRow& row = _rows[r];
			const float relVel = row.Angular
				? (BodyB->PseudoAngularVelocity - BodyA->PseudoAngularVelocity).Dot(row.Axis)
				: ((BodyB->PseudoLinearVelocity + Vec3::Cross(BodyB->PseudoAngularVelocity, row.RelB))
					- (BodyA->PseudoLinearVelocity + Vec3::Cross(BodyA->PseudoAngularVelocity, row.RelA))).Dot(row.Axis);

			float dImpulse = (row.PositionBias - relVel) * row.EffMass;
			const float old = row.PseudoAccumulated;
			row.PseudoAccumulated = FMath::Max(row.LowerImpulse, FMath::Min(row.UpperImpulse, old + dImpulse));
			dImpulse = row.PseudoAccumulated - old;

			if (row.Angular)
			{
				const Vec3 L = row.Axis * dImpulse;
				BodyA->ApplyPushTorqueImpulse(-L);
				BodyB->ApplyPushTorqueImpulse(L);
			}
			else
			{
				const Vec3 P = row.Axis * dImpulse;
				BodyA->ApplyPushImpulse(-P, row.RelA);
				BodyB->ApplyPushImpulse(P, row.RelB);
			}
		}
	}

	Vec3 Joint::ToEulerXYZ(const Quat& q)
	{
		const Matrix3x3 m = Matrix3x3::FromQuat(q);
		// 行列から XYZ オイラー角を復元。
		const float m02 = m.Row0.z;
		float y, x, z;
		if (m02 < 1.0f - 1e-6f)
		{
			if (m02 > -1.0f + 1e-6f)
			{
				y = MAsin(FMath::Clamp(m02, -1.0f, 1.0f));
				x = MAtan2(-m.Row1.z, m.Row2.z);
				z = MAtan2(-m.Row0.y, m.Row0.x);
			}
			else
			{
				y = -static_cast<float>(MPi / 2);
				x = -MAtan2(m.Row1.x, m.Row1.y);
				z = 0.0f;
			}
		}
		else
		{
			y = static_cast<float>(MPi / 2);
			x = MAtan2(m.Row1.x, m.Row1.y);
			z = 0.0f;
		}
		return Vec3(x, y, z);
	}
}
