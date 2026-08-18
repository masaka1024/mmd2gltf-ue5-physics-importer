// Copyright (c) 2026 masaka1024. MIT License.
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/PhysicsWorld.cs

#include "MmdPhysicsWorld.h"
#include "HAL/PlatformTime.h"

namespace MmdPhysics
{
	bool PhysicsWorld::ProfileEnabled = false;
	double PhysicsWorld::ProfBroad = 0;
	double PhysicsWorld::ProfBuild = 0;
	double PhysicsWorld::ProfPrepare = 0;
	double PhysicsWorld::ProfSpring = 0;
	double PhysicsWorld::ProfWarm = 0;
	double PhysicsWorld::ProfSolveContact = 0;
	double PhysicsWorld::ProfSolveJoint = 0;
	double PhysicsWorld::ProfIntegrate = 0;
	double PhysicsWorld::ProfStore = 0;
	int64 PhysicsWorld::ProfSubSteps = 0;
	int64 PhysicsWorld::ProfContacts = 0;
	int64 PhysicsWorld::ProfManifolds = 0;

	void PhysicsWorld::ProfReset()
	{
		ProfBroad = ProfBuild = ProfPrepare = ProfSpring = ProfWarm = ProfSolveContact = ProfSolveJoint = ProfIntegrate = ProfStore = 0;
		ProfSubSteps = ProfContacts = ProfManifolds = 0;
	}

	// 移植元の Stopwatch 相当。ProfileEnabled=false の間は一切呼ばれない。
	namespace
	{
		double GProfMark = 0.0;
		FORCEINLINE void ProfRestart() { GProfMark = FPlatformTime::Seconds(); }
		FORCEINLINE double ProfTick()
		{
			const double Now = FPlatformTime::Seconds();
			const double Ms = (Now - GProfMark) * 1000.0;
			GProfMark = Now;
			return Ms;
		}
	}

	void PhysicsWorld::AddBody(const TSharedPtr<RigidBody>& b)
	{
		b->Index = Bodies.Num();
		// static/kinematic の目標姿勢を現在姿勢で初期化 (未設定でのテレポート防止)。
		// ボーン追従はこの後 AnimNode 側が毎フレーム上書きする。
		if (b->IsStaticOrKinematic())
			b->KinematicTarget = b->WorldTransform;
		Bodies.Add(b);
		_pairCount = -1; // 候補ペアを作り直す
	}

	void PhysicsWorld::ClearContacts()
	{
		_manifolds.Reset();
		_contacts.Reset();
		_accumulator = 0.0f;
	}

	// --- 公開ステップ (可変 dt を固定ステップに分割) ---
	void PhysicsWorld::StepSimulation(float DeltaTime)
	{
		// Joint::MaxCorrectionVel は A/B 用の static なので、ワールド側の設定をここで写す。
		// 0 のときは触らない (= コア既定のまま = 従来とビット不変)。
		if (JointMaxCorrectionVel > 0.0f) Joint::MaxCorrectionVel = JointMaxCorrectionVel;

		_accumulator += DeltaTime;

		// ★積み残しは捨てる (上限 MaxStepsPerCall * FixedTimeStep)。
		//   クランプしないと、シェーダーコンパイルやアセットロードで数秒止まった分が
		//   借金として残り、以後しばらく毎フレーム上限いっぱい = 実時間の 8 倍速で回る。
		//   髪とスカートが暴れて体に潜り込み、貫入平衡に落ちて戻らなくなる
		//   (起動時の再整合は最初の数フレームで終わっているので復帰もしない)。
		//   落ちたフレームの時間を取り戻す価値は無い。捨てて実時間へ復帰させる。
		const float MaxBacklog = MaxStepsPerCall * FixedTimeStep;
		if (_accumulator > MaxBacklog)
		{
			DiscardedTime += _accumulator - MaxBacklog;
			_accumulator = MaxBacklog;
		}

		// このフレームで走らせる内部ステップ数を先に確定する。
		// キネマティック(ボーン)目標の補間は「フレーム全体の総サブステップ数」を分母に等分する。
		// こうしないと、InternalStep を複数回呼ぶ場合に各内部ステップが個別に目標へ到達してしまい、
		// 最初の内部ステップで目標へジャンプ→残り停止、という誤補間になる (30fps入力を細分できない)。
		int32 stepsToRun = 0;
		{ float rem = _accumulator; while (rem >= FixedTimeStep && stepsToRun < MaxStepsPerCall) { rem -= FixedTimeStep; stepsToRun++; } }
		LastStepsRun = stepsToRun; // 診断用 (タイミングログ)
		if (stepsToRun == 0) return;

		// フレーム開始時のキネマティック姿勢を保存。KinematicTarget はこのフレーム終端の姿勢。
		if (_frameKinStart.Num() < Bodies.Num())
			_frameKinStart.SetNum(Bodies.Num());
		for (int32 i = 0; i < Bodies.Num(); i++)
			if (Body(i)->IsKinematic()) _frameKinStart[i] = Body(i)->WorldTransform;

		const int32 totalSub = stepsToRun * SubSteps; // フレーム内の総サブステップ数 (補間の分母)
		for (int32 k = 0; k < stepsToRun; k++)
		{
			InternalStep(FixedTimeStep, k * SubSteps, totalSub);
			_accumulator -= FixedTimeStep;
		}
		// 端数 (_accumulator の残り) は次回呼び出しへ持ち越す (標準的な固定刻みアキュムレータ)。
		// 入力は 30fps フレーム境界で更新されるため、KinematicTarget はこのフレーム終端で到達済みとして扱う。
	}

	// --- 固定 1 ステップ --- gBase: このフレーム内での先行サブステップ数, TotalSub: フレーム総サブステップ数
	void PhysicsWorld::InternalStep(float dt, int32 gBase, int32 TotalSub)
	{
		const float sub = dt / SubSteps;
		for (int32 s = 0; s < SubSteps; s++)
			SubStep(sub, static_cast<float>(gBase + s + 1) / TotalSub);
	}

	// Frac: フレーム開始→終端目標の 等分補間割合 (1/TotalSub .. 1)。フレーム全体で連続する。
	void PhysicsWorld::SubStep(float dt, float Frac)
	{
		// ジョイントの求解反復数 (速度側と位置補正側で共通)。接触は SolverIterations のまま。
		const int32 JointIters = JointVelocityIterations > 0 ? JointVelocityIterations : SolverIterations;
		const int32 TotalIters = FMath::Max(SolverIterations, JointIters);

		for (int32 i = 0; i < Bodies.Num(); i++)
		{
			RigidBody* b = Body(i);
			if (!b->IsKinematic()) continue;
			b->KinematicStepTarget = InterpTransform(_frameKinStart[i], b->KinematicTarget, Frac);
		}

		if (ProfileEnabled) { ProfRestart(); }
		IntegrateVelocities(dt);
		if (ProfileEnabled) ProfIntegrate += ProfTick();
		BroadphaseNarrowphase();
		if (ProfileEnabled) { ProfBroad += ProfTick(); ProfManifolds += _manifolds.Num(); }
		BuildContactConstraints(dt);
		if (ProfileEnabled) { ProfBuild += ProfTick(); ProfContacts += _contacts.Num(); ProfSubSteps++; }

		for (const TSharedPtr<Joint>& j : Joints) j->Prepare(dt, UseJointSplitImpulse, UseJointWarmStart, UseJointWarmStartAngular);
		if (ProfileEnabled) ProfPrepare += ProfTick();
		for (const TSharedPtr<Joint>& j : Joints) j->ApplySprings(dt);
		if (ProfileEnabled) ProfSpring += ProfTick();

		WarmStart();
		if (ProfileEnabled) ProfWarm += ProfTick();

		// ★ジョイントは接触より多く回せる (JointVelocityIterations)。
		//   Gauss-Seidel は 1 反復でジョイント 1 本ぶんしか情報を伝えないので、
		//   節数が反復数を超える鎖ではアンカーの拘束が末端まで届かず、
		//   届かなかった相対速度が位置誤差として単調に溜まる (詳しくはヘッダの注記)。
		//   接触の反復数を増やすと待機区間の揺れが悪化するため、増やすのはジョイントだけにする。
		//   JointIters <= SolverIterations のときは従来と完全に同じ順序・同じ回数になる。
		for (int32 it = 0; it < TotalIters; it++)
		{
			const bool bDoContacts = it < SolverIterations;
			const bool bDoJoints = it < JointIters;
			if (SolveJointsFirst)
			{
				// Bullet 2.75 同順: ジョイント → 接触 (接触が後勝ち)。
				if (bDoJoints) for (const TSharedPtr<Joint>& j : Joints) j->SolveVelocity();
				if (ProfileEnabled) ProfSolveJoint += ProfTick();
				if (bDoContacts) SolveContacts();
				if (ProfileEnabled) ProfSolveContact += ProfTick();
			}
			else
			{
				// 従来順: 接触 → ジョイント (ジョイントが後勝ち)。
				if (bDoContacts) SolveContacts();
				if (ProfileEnabled) ProfSolveContact += ProfTick();
				if (bDoJoints) for (const TSharedPtr<Joint>& j : Joints) j->SolveVelocity();
				if (ProfileEnabled) ProfSolveJoint += ProfTick();
			}
		}

		// Split Impulse: 実速度の求解後、貫入回復(接触)/位置補正(ジョイント)を擬似速度側で
		// 別反復して解く。擬似速度を 0 から解き、位置積分にのみ反映する (実速度には残さない)。
		if (UseSplitImpulse || UseJointSplitImpulse)
		{
			for (int32 i = 0; i < Bodies.Num(); i++)
			{
				Body(i)->PseudoLinearVelocity = Vec3::Zero;
				Body(i)->PseudoAngularVelocity = Vec3::Zero;
			}
			// ★位置補正側の反復は SolverIterations のまま据え置くこと。
			//   ジョイントだけ JointIters 回に増やすと**悪化する** (実測: 120 秒が
			//   収まっていたのに しっぽ４ 376 倍へ)。位置補正を強めると行き過ぎるのは
			//   MaxCorrectionVel を上げたときと同じ傾向で、鎖の破綻は
			//   「補正が足りない」より「補正が暴れる」側にある。
			for (int32 it = 0; it < SolverIterations; it++)
			{
				if (UseSplitImpulse) SolveSplitImpulse();
				if (UseJointSplitImpulse) for (const TSharedPtr<Joint>& j : Joints) j->SolveSplitPosition();
			}
		}

		StoreImpulses();
		if (ProfileEnabled) ProfStore += ProfTick();
		IntegratePositions(dt);
		if (EnableSleeping) UpdateSleeping(dt);
	}

	void PhysicsWorld::UpdateSleeping(float dt)
	{
		const int32 n = Bodies.Num();
		if (_uf.Num() < n)
		{
			_uf.SetNum(n); _wantsSleep.SetNum(n); _islandBlocked.SetNum(n); _islandWants.SetNum(n);
		}

		const float lt2 = LinearSleepThreshold * LinearSleepThreshold;
		const float at2 = AngularSleepThreshold * AngularSleepThreshold;

		// 1) 各動的剛体のタイマー更新。しきい値を超えていたら即リセット+起床。
		for (int32 i = 0; i < n; i++)
		{
			RigidBody* b = Body(i);
			_uf[i] = i; _islandBlocked[i] = false; _islandWants[i] = true;
			_wantsSleep[i] = false;
			if (b->IsStaticOrKinematic()) continue;
			if (b->LinearVelocity.LengthSquared() < lt2 && b->AngularVelocity.LengthSquared() < at2)
				b->SleepTimer += dt;
			else { b->SleepTimer = 0.0f; b->IsActive = true; }
			_wantsSleep[i] = b->SleepTimer > DeactivationTime;
		}

		// 2) アイランド構築。動的どうしのみ連結する。
		for (const TSharedPtr<Joint>& j : Joints)
		{
			if (j->BodyA == nullptr || j->BodyB == nullptr) continue;
			if (j->BodyA->IsStaticOrKinematic() || j->BodyB->IsStaticOrKinematic()) continue;
			UfUnion(j->BodyA->Index, j->BodyB->Index);
		}
		for (int32 c = 0; c < _contacts.Num(); c++)
		{
			RigidBody* a = _contacts[c].A; RigidBody* b2 = _contacts[c].B;
			if (a == nullptr || b2 == nullptr) continue;
			if (a->IsStaticOrKinematic() || b2->IsStaticOrKinematic()) continue;
			UfUnion(a->Index, b2->Index);
		}

		// 3) 「動いている kinematic に触れているか」でアイランドを起床固定する。
		const float kme2 = KinematicMotionEpsilon * KinematicMotionEpsilon;
		auto KinMoving = [kme2](const RigidBody* k)
		{
			return k->LinearVelocity.LengthSquared() > kme2 ||
				k->AngularVelocity.LengthSquared() > kme2;
		};

		for (const TSharedPtr<Joint>& j : Joints)
		{
			if (j->BodyA == nullptr || j->BodyB == nullptr) continue;
			if (j->BodyA->IsStaticOrKinematic() && !j->BodyB->IsStaticOrKinematic())
			{
				if (KinMoving(j->BodyA)) _islandBlocked[UfFind(j->BodyB->Index)] = true;
			}
			else if (j->BodyB->IsStaticOrKinematic() && !j->BodyA->IsStaticOrKinematic())
			{
				if (KinMoving(j->BodyB)) _islandBlocked[UfFind(j->BodyA->Index)] = true;
			}
		}
		for (int32 c = 0; c < _contacts.Num(); c++)
		{
			RigidBody* a = _contacts[c].A; RigidBody* b2 = _contacts[c].B;
			if (a == nullptr || b2 == nullptr) continue;
			if (a->IsStaticOrKinematic() && !b2->IsStaticOrKinematic())
			{
				if (KinMoving(a)) _islandBlocked[UfFind(b2->Index)] = true;
			}
			else if (b2->IsStaticOrKinematic() && !a->IsStaticOrKinematic())
			{
				if (KinMoving(b2)) _islandBlocked[UfFind(a->Index)] = true;
			}
		}

		// 4) アイランド全員が眠りたがっているかを集約。
		for (int32 i = 0; i < n; i++)
		{
			if (Body(i)->IsStaticOrKinematic()) continue;
			if (!_wantsSleep[i]) _islandWants[UfFind(i)] = false;
		}

		// 5) 適用。眠るアイランドは速度を落として非活性化、それ以外は起こす。
		int32 sleeping = 0;
		for (int32 i = 0; i < n; i++)
		{
			RigidBody* b = Body(i);
			if (b->IsStaticOrKinematic()) continue;
			const int32 r = UfFind(i);
			const bool sleep = _islandWants[r] && !_islandBlocked[r];
			if (sleep)
			{
				b->IsActive = false;
				b->LinearVelocity = Vec3::Zero;
				b->AngularVelocity = Vec3::Zero;
				sleeping++;
			}
			else if (!b->IsActive)
			{
				b->IsActive = true;
				b->SleepTimer = 0.0f;   // 起きたらタイマーをやり直す
			}
		}
		SleepingBodyCount = sleeping;
	}

	// --- 速度積分: 重力/力/減衰 ---
	void PhysicsWorld::IntegrateVelocities(float dt)
	{
		for (const TSharedPtr<RigidBody>& bp : Bodies)
		{
			RigidBody* b = bp.Get();
			if (b->IsStaticOrKinematic())
			{
				// Kinematic: 目標姿勢へ移動する速度を算出 (接触応答に使用)。
				if (b->IsKinematic())
				{
					// このサブステップの補間目標への差分から速度を算出。
					const RigidTransform cur = b->WorldTransform;
					const RigidTransform tgt = b->KinematicStepTarget;
					b->LinearVelocity = (tgt.Origin - cur.Origin) / dt;
					const Quat dq = tgt.Rotation * cur.Rotation.Conjugated();
					b->AngularVelocity = QuatToAngularVelocity(dq, dt);
				}
				continue;
			}

			// 眠っている剛体は積分しない。重力を入れると毎ステップしきい値を超えて即起床し、
			// スリープが永久に成立しなくなる (Bullet も非活性化した剛体は積分対象外)。
			if (EnableSleeping && !b->IsActive) { b->ClearForces(); continue; }

			b->LinearVelocity += (Gravity + b->TotalForce * b->InverseMass) * dt;
			b->AngularVelocity += (b->InverseInertiaWorld * b->TotalTorque) * dt;

			// Bullet 2.75 btRigidBody::applyDamping と同じ秒単位の減衰。
			b->LinearVelocity *= DampingFactor(b->LinearDamping, dt);
			b->AngularVelocity *= DampingFactor(b->AngularDamping, dt);

			b->ClearForces();
		}
	}

	// Bullet 2.75 の秒単位減衰係数。(1 - d)^dt。
	// d=1.0 は 0 除算・完全停止を避けるためクランプする。
	float PhysicsWorld::DampingFactor(float Damping, float dt)
	{
		const float d = FMath::Clamp(Damping, 0.0f, 0.999f);
		// C# の Math.Pow は double。float 版 powf に落とすと最終ビットが食い違う。
		return static_cast<float>(FMath::Pow(static_cast<double>(1.0f - d), static_cast<double>(dt)));
	}

	// --- 位置積分 ---
	void PhysicsWorld::IntegratePositions(float dt)
	{
		for (const TSharedPtr<RigidBody>& bp : Bodies)
		{
			RigidBody* b = bp.Get();
			if (b->IsStaticOrKinematic())
			{
				if (b->IsKinematic())
				{
					b->WorldTransform = b->KinematicStepTarget;
					b->UpdateInertiaWorld();
				}
				continue;
			}

			if (EnableSleeping && !b->IsActive) continue;   // 眠っている剛体は動かさない

			RigidTransform t = b->WorldTransform;

			// Split Impulse 有効時は「実速度 + 擬似速度」で位置を進める (擬似は速度として残さない)。
			Vec3 vlin = b->LinearVelocity;
			Vec3 vang = b->AngularVelocity;
			if (UseSplitImpulse || UseJointSplitImpulse)
			{
				vlin += b->PseudoLinearVelocity;
				vang += b->PseudoAngularVelocity;
			}
			t.Origin += vlin * dt;

			// クォータニオン積分: q += 0.5 * w * q * dt。
			const Vec3 w = vang;
			const Quat spin = Quat(w.x, w.y, w.z, 0.0f) * t.Rotation;
			t.Rotation = Quat(
				t.Rotation.x + spin.x * 0.5f * dt,
				t.Rotation.y + spin.y * 0.5f * dt,
				t.Rotation.z + spin.z * 0.5f * dt,
				t.Rotation.w + spin.w * 0.5f * dt).Normalized();

			b->WorldTransform = t;
			b->UpdateInertiaWorld();
		}
	}

	Vec3 PhysicsWorld::QuatToAngularVelocity(Quat dq, float dt)
	{
		dq = dq.Normalized();
		float angle = 2.0f * MAcos(FMath::Clamp(dq.w, -1.0f, 1.0f));
		if (angle < 1e-6f) return Vec3::Zero;
		if (angle > MPi) angle -= 2.0f * static_cast<float>(MPi);
		const Vec3 axis(dq.x, dq.y, dq.z);
		const float len = axis.Length();
		if (len < 1e-9f) return Vec3::Zero;
		return axis / len * (angle / dt);
	}

	// 開始→目標を Frac で補間 (位置は線形、回転は slerp)。
	RigidTransform PhysicsWorld::InterpTransform(const RigidTransform& From, const RigidTransform& To, float Frac)
	{
		return RigidTransform(
			Quat::Slerp(From.Rotation, To.Rotation, Frac),
			From.Origin + (To.Origin - From.Origin) * Frac);
	}

	int32 PhysicsWorld::DebugCollisionPairCount()
	{
		if (_pairCount < 0 || _pairBuiltForCount != Bodies.Num()) BuildCollisionPairs();
		return _pairCount;
	}

	void PhysicsWorld::BuildCollisionPairs()
	{
		const int32 n = Bodies.Num();
		const int32 cap = n * (n - 1) / 2;
		if (_pairA.Num() < cap) { _pairA.SetNumUninitialized(cap); _pairB.SetNumUninitialized(cap); }
		int32 c = 0;
		for (int32 i = 0; i < n; i++)
		{
			for (int32 k = i + 1; k < n; k++)
			{
				const RigidBody* a = Body(i); const RigidBody* b = Body(k);
				if (a->IsStaticOrKinematic() && b->IsStaticOrKinematic()) continue;
				if (!ShouldCollide(a, b)) continue;
				_pairA[c] = i; _pairB[c] = k; c++;
			}
		}
		_pairCount = c; _pairBuiltForCount = n;
	}

	void PhysicsWorld::BroadphaseNarrowphase()
	{
		const int32 n = Bodies.Num();
		if (_pairCount < 0 || _pairBuiltForCount != n) BuildCollisionPairs();
		if (_aabbScratch.Num() < n) _aabbScratch.SetNum(n);
		TArray<Aabb>& aabbs = _aabbScratch;
		// 検出帯を既定より広げているときだけ AABB も同じだけ膨らませる。
		// 広げないと「形状は帯の中なのに AABB 段階で捨てられる」ため帯の拡大が効かない。
		// 既定 (SpeculativeMargin=0.02) では extra=0 なので従来と完全に同一 = ビット不変。
		const float extra = GjkEpa::SpeculativeMargin - GjkEpa::SpeculativeMarginDefault;
		if (extra > 0.0f) for (int32 i = 0; i < n; i++) { aabbs[i] = Body(i)->ComputeAabb(); aabbs[i].Expand(extra); }
		else for (int32 i = 0; i < n; i++) aabbs[i] = Body(i)->ComputeAabb();

		TSet<int64>& seen = _seenScratch; seen.Reset();
		for (int32 p = 0; p < _pairCount; p++)
		{
			const int32 i = _pairA[p], k = _pairB[p];
			if (!aabbs[i].Intersects(aabbs[k])) continue;
			RigidBody* a = Body(i); RigidBody* b = Body(k);

			const int64 key = PairKey(a->Index, b->Index);
			seen.Add(key);
			TSharedPtr<PersistentManifold>* found = _manifolds.Find(key);
			PersistentManifold* m;
			if (found == nullptr)
			{
				TSharedPtr<PersistentManifold> nm = MakeShared<PersistentManifold>(a, b);
				m = nm.Get();
				_manifolds.Add(key, nm);
			}
			else
			{
				m = found->Get();
			}
			m->Refresh();
			_detectBuffer.Reset();
			GjkEpa::Detect(a, b, _detectBuffer);
			for (int32 di = 0; di < _detectBuffer.Num(); di++)
				m->AddPoint(_detectBuffer[di]);
		}
		// 消えたペアを掃除。
		if (_manifolds.Num() > seen.Num())
		{
			TArray<int64>& dead = _deadScratch; dead.Reset();
			for (const TPair<int64, TSharedPtr<PersistentManifold>>& kv : _manifolds)
			{
				if (!seen.Contains(kv.Key)) dead.Add(kv.Key);
			}
			for (int32 d = 0; d < dead.Num(); d++) _manifolds.Remove(dead[d]);
		}
	}

	int64 PhysicsWorld::PairKey(int32 a, int32 b)
	{
		if (a > b) { Swap(a, b); }
		return (static_cast<int64>(a) << 32) | static_cast<int64>(static_cast<uint32>(b));
	}

	// --- 接触制約構築 ---
	void PhysicsWorld::BuildContactConstraints(float dt)
	{
		_contacts.Reset();

		// TMap の列挙順は挿入/削除履歴に依存し非決定的なので、剛体indexの組(PairKey)の
		// 昇順に並べ替えてから制約を構築する。これにより「無関係な剛体の増減」で接触の解く順序が
		// 変わって結果が揺れる (Gauss-Seidel の順序依存) 現象を排除する。式・パラメータは不変、順序のみ。
		_sortedManifolds.Reset();
		for (const TPair<int64, TSharedPtr<PersistentManifold>>& kv : _manifolds) _sortedManifolds.Add(kv.Value.Get());
		_sortedManifolds.Sort([](const PersistentManifold& X, const PersistentManifold& Y)
		{
			return PairKey(X.BodyA->Index, X.BodyB->Index) < PairKey(Y.BodyA->Index, Y.BodyB->Index);
		});

		for (PersistentManifold* m : _sortedManifolds)
		{
			for (int32 p = 0; p < m->Points.Num(); p++)
			{
				const ContactPoint& cp = m->Points[p];
				RigidBody* a = m->BodyA; RigidBody* b = m->BodyB;

				const Vec3 rA = cp.PositionWorldA - a->CenterOfMass();
				const Vec3 rB = cp.PositionWorldB - b->CenterOfMass();
				// EPA 法線は A→B 方向。ソルバ規約 (relVel=vB-vA, -P:A/+P:B) と一致。
				const Vec3 n = cp.Normal;

				// 接線基底。
				Vec3 t1, t2;
				BuildTangentBasis(n, t1, t2);

				ContactConstraint cc;
				cc.A = a; cc.B = b; cc.RelA = rA; cc.RelB = rB;
				cc.Normal = n; cc.Tangent1 = t1; cc.Tangent2 = t2;
				cc.Friction = MSqrt(FMath::Max(0.0f, a->Friction) * FMath::Max(0.0f, b->Friction));
				cc.NormalMass = EffectiveMass(a, b, rA, rB, n);
				cc.TangentMass1 = EffectiveMass(a, b, rA, rB, t1);
				cc.TangentMass2 = EffectiveMass(a, b, rA, rB, t2);
				cc.NormalImpulse = cp.NormalImpulse;
				cc.TangentImpulse1 = cp.TangentImpulse1;
				cc.TangentImpulse2 = cp.TangentImpulse2;
				cc.Manifold = m; cc.PointRef = p;

				// 法線の目標接近速度 (NormalBias) を決める。
				const float relN = (b->VelocityAtPoint(cp.PositionWorldB) - a->VelocityAtPoint(cp.PositionWorldA)).Dot(n);
				const float rest = MSqrt(FMath::Max(0.0f, a->Restitution) * FMath::Max(0.0f, b->Restitution));
				const float restBias = (-relN > RestitutionThreshold) ? rest * -relN : 0.0f;

				if (cp.Distance <= 0.0f)
				{
					// 貫入: Baumgarte 位置補正 + 反発。
					const float pen = -cp.Distance - PenetrationSlop;
					const float biasVel = pen > 0 ? BaumgarteFactor * pen / dt : 0.0f;
					// Split Impulse: Bullet 2.75 convertContact と同様、貫入が閾値より深い接触のみ
					// 位置補正を擬似速度側(PushBias)へ回し、実速度側は反発のみにする。
					// 浅い貫入 (閾値より浅い) は従来どおり速度側の Baumgarte に載せる。
					if (UseSplitImpulse && cp.Distance <= SplitImpulsePenetrationThreshold)
					{
						cc.NormalBias = restBias;   // 実速度側: 反発のみ (貫入回復は載せない)
						cc.PushBias = biasVel;      // 擬似速度側: 貫入回復
					}
					else
					{
						cc.NormalBias = FMath::Max(biasVel, restBias); // 従来どおり
						cc.PushBias = 0.0f;
					}
				}
				else
				{
					// 非貫入 (投機的接触): このステップで表面へちょうど到達する接近
					// (-Distance/dt) までは許し、それを超える接近だけを止める。押し戻さない。
					// 反発は押し戻す向きなので、より接近を許す (小さい) 方を採る。
					const float speculative = -cp.Distance / dt;
					cc.NormalBias = FMath::Min(speculative, restBias);
				}
				_contacts.Add(cc);
			}
		}
		DebugContactCount = _contacts.Num();
	}

	float PhysicsWorld::EffectiveMass(const RigidBody* a, const RigidBody* b, const Vec3& rA, const Vec3& rB, const Vec3& Dir)
	{
		const Vec3 rAxn = Vec3::Cross(rA, Dir);
		const Vec3 rBxn = Vec3::Cross(rB, Dir);
		const float k = a->InverseMass + b->InverseMass
			+ rAxn.Dot(a->InverseInertiaWorld * rAxn)
			+ rBxn.Dot(b->InverseInertiaWorld * rBxn);
		return k > 0 ? 1.0f / k : 0.0f;
	}

	void PhysicsWorld::BuildTangentBasis(const Vec3& n, Vec3& t1, Vec3& t2)
	{
		if (FMath::Abs(n.x) >= 0.577f)
			t1 = Vec3(n.y, -n.x, 0.0f).Normalized();
		else
			t1 = Vec3(0.0f, n.z, -n.y).Normalized();
		t2 = Vec3::Cross(n, t1);
	}

	// 蓄積インパルスを manifold へ書き戻し、次フレームのウォームスタートに使う。
	void PhysicsWorld::StoreImpulses()
	{
		for (int32 i = 0; i < _contacts.Num(); i++)
		{
			const ContactConstraint& c = _contacts[i];
			if (c.Manifold == nullptr || c.PointRef >= c.Manifold->Points.Num()) continue;
			ContactPoint& cp = c.Manifold->Points[c.PointRef];
			cp.NormalImpulse = c.NormalImpulse;
			cp.TangentImpulse1 = c.TangentImpulse1;
			cp.TangentImpulse2 = c.TangentImpulse2;
			if (DebugContacts != nullptr)
			{
				FMmdDebugContact Rec;
				Rec.A = c.A->Name; Rec.B = c.B->Name; Rec.Dist = cp.Distance; Rec.Ni = c.NormalImpulse;
				DebugContacts->Add(Rec);
			}
		}
	}

	void PhysicsWorld::WarmStart()
	{
		const float wf = ContactWarmStartFactor;
		for (int32 i = 0; i < _contacts.Num(); i++)
		{
			ContactConstraint& c = _contacts[i];
			// Bullet同様、蓄積インパルスに係数を掛けてから適用+アキュムレータ初期値にする(0.85時)。
			if (wf != 1.0f) { c.NormalImpulse *= wf; c.TangentImpulse1 *= wf; c.TangentImpulse2 *= wf; }
			const Vec3 P = c.Normal * c.NormalImpulse
				+ c.Tangent1 * c.TangentImpulse1
				+ c.Tangent2 * c.TangentImpulse2;
			c.A->ApplyImpulse(-P, c.RelA);
			c.B->ApplyImpulse(P, c.RelB);
		}
	}

	// --- 接触速度求解 ---
	void PhysicsWorld::SolveContacts()
	{
		for (int32 i = 0; i < _contacts.Num(); i++)
		{
			ContactConstraint& c = _contacts[i];
			RigidBody* a = c.A; RigidBody* b = c.B;

			if (!ContactNormalBeforeFriction)
			{
				// 従来: 摩擦(前反復の法線で上限) → 法線。
				SolveFriction(c, a, b, c.Tangent1, c.TangentMass1, c.TangentImpulse1);
				SolveFriction(c, a, b, c.Tangent2, c.TangentMass2, c.TangentImpulse2);
				SolveNormal(c, a, b);
			}
			else
			{
				// Bullet同順: 法線 → 摩擦(同反復の法線で上限)。
				SolveNormal(c, a, b);
				SolveFriction(c, a, b, c.Tangent1, c.TangentMass1, c.TangentImpulse1);
				SolveFriction(c, a, b, c.Tangent2, c.TangentMass2, c.TangentImpulse2);
			}
		}
	}

	// --- Split Impulse 求解 (擬似速度で貫入回復) ---
	// SolveContacts の法線部と同一の符号規約。ただし擬似速度に対して解き、目標は PushBias。
	// 蓄積擬似インパルス(PushImpulse)は 0 以上にクランプ。ウォームスタートしない。
	void PhysicsWorld::SolveSplitImpulse()
	{
		for (int32 i = 0; i < _contacts.Num(); i++)
		{
			ContactConstraint& c = _contacts[i];
			if (c.PushBias <= 0.0f) continue; // 深い貫入の接触のみ
			RigidBody* a = c.A; RigidBody* b = c.B;
			const Vec3 pA = a->CenterOfMass() + c.RelA;
			const Vec3 pB = b->CenterOfMass() + c.RelB;
			const float relN = (b->PseudoVelocityAtPoint(pB) - a->PseudoVelocityAtPoint(pA)).Dot(c.Normal);
			float dP = (c.PushBias - relN) * c.NormalMass;
			const float old = c.PushImpulse;
			c.PushImpulse = FMath::Max(0.0f, old + dP);
			dP = c.PushImpulse - old;
			const Vec3 P = c.Normal * dP;
			a->ApplyPushImpulse(-P, c.RelA);
			b->ApplyPushImpulse(P, c.RelB);
		}
	}

	void PhysicsWorld::SolveNormal(ContactConstraint& c, RigidBody* a, RigidBody* b)
	{
		const Vec3 pA = a->CenterOfMass() + c.RelA;
		const Vec3 pB = b->CenterOfMass() + c.RelB;
		const float relN = (b->VelocityAtPoint(pB) - a->VelocityAtPoint(pA)).Dot(c.Normal);
		float dPn = (c.NormalBias - relN) * c.NormalMass;
		const float oldN = c.NormalImpulse;
		c.NormalImpulse = FMath::Max(0.0f, oldN + dPn);
		dPn = c.NormalImpulse - oldN;
		const Vec3 Pn = c.Normal * dPn;
		a->ApplyImpulse(-Pn, c.RelA);
		b->ApplyImpulse(Pn, c.RelB);
	}

	void PhysicsWorld::SolveFriction(ContactConstraint& c, RigidBody* a, RigidBody* b,
		const Vec3& Tangent, float Mass, float& Accum)
	{
		const Vec3 pA = a->CenterOfMass() + c.RelA;
		const Vec3 pB = b->CenterOfMass() + c.RelB;
		const float relT = (b->VelocityAtPoint(pB) - a->VelocityAtPoint(pA)).Dot(Tangent);
		float dPt = -relT * Mass;
		const float maxF = c.Friction * c.NormalImpulse;
		const float old = Accum;
		Accum = FMath::Max(-maxF, FMath::Min(maxF, old + dPt));
		dPt = Accum - old;
		const Vec3 Pt = Tangent * dPt;
		a->ApplyImpulse(-Pt, c.RelA);
		b->ApplyImpulse(Pt, c.RelB);
	}
}
