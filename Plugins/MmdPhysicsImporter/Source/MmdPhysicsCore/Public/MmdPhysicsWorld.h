// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – PhysicsWorld
// btDiscreteDynamicsWorld 相当。重力・積分・衝突・Joint を統合する。
// Sequential-Impulse ソルバ + Baumgarte 位置補正。
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/PhysicsWorld.cs (namespace BulletPhysics)
// ===========================================================================

#pragma once

#include "MmdCollision.h"
#include "MmdConstraints.h"

namespace MmdPhysics
{
	/** 接触制約 (1 接触点 = 法線 + 2 摩擦)。 */
	struct ContactConstraint
	{
		RigidBody* A = nullptr;
		RigidBody* B = nullptr;
		Vec3 RelA, RelB;
		Vec3 Normal, Tangent1, Tangent2;
		float NormalMass = 0.0f, TangentMass1 = 0.0f, TangentMass2 = 0.0f;
		float NormalBias = 0.0f;       // 速度側の目標接近速度 (restitution + 浅貫入の Baumgarte)
		float PushBias = 0.0f;         // Split Impulse: 擬似速度側の貫入回復目標 (深貫入のみ非0)
		float Friction = 0.0f;
		float NormalImpulse = 0.0f, TangentImpulse1 = 0.0f, TangentImpulse2 = 0.0f;
		float PushImpulse = 0.0f;      // Split Impulse の蓄積擬似インパルス (ウォームスタートしない)
		PersistentManifold* Manifold = nullptr; int32 PointRef = 0; // ウォームスタート書き戻し用
	};

	/** 診断フック用の 1 接触レコード (移植元の値タプルに対応)。 */
	struct FMmdDebugContact
	{
		FString A;
		FString B;
		float Dist = 0.0f;
		float Ni = 0.0f;
	};

	/** 物理ワールド。剛体と Joint を保持しシミュレートする。 */
	class MMDPHYSICSCORE_API PhysicsWorld
	{
	public:
		Vec3 Gravity = Vec3(0.0f, -9.8f * 10.0f, 0.0f); // MMD スケール: 重力は約 98

		// ソルバ設定。
		// リファレンスは実効 1/60 (FixedTimeStep=1/30 は 30fps 入力に合わせ、SubSteps=2 で刻む)。
		// 刻み掃引で、実効刻みを細かくするほどMMDのスカート傾きに一致することを確認した
		// (12窓比 1/30:1.133 → 1/60:1.030 → 1/120:0.978)。外部実装も細刻み (Saba=1/120,
		// libmmd=1/60, MMDは物理最大60fps)。
		// ※細刻み化は SubSteps で行う (FixedTimeStep を下げる経路はキネマティック補間の分母が
		//   フレーム総サブステップ数で正しく効く。両経路とも補間は修正済みだが、入力は 1/30 境界)。
		int32 SolverIterations = 10;
		// ★2026-08-13: 2 → 4 (実効 1/60 → 1/120)。貫入対策。
		//   機構: 接触の検出帯は MmdCollision の SpeculativeMargin=0.02 という「速度を見ない固定距離」で、
		//   駆動剛体は接触点で 1/30 あたり中央 0.114 (法線成分 0.052) 動く = 帯の 5.7倍。
		//   接触が生成された時点で既に深く刺さっており、貫入オンセットの 58% は
		//   「前フレームに接触点ゼロ」から始まっていた。刻みを半分にすると 1ステップの移動量が減り、
		//   ソルバが押し出す機会も増えるので両方に効く。
		//   実測(5モデル): 貫入中央 -45〜-63%、悪化したモデルは無し。深貫入>0.5 は IA で 5件→0件、
		//   hairfid 7001フレームで 20件→0件。
		//   ★忠実度も同時に改善する: 12窓比 中央 1.0608 → 0.9867 (過去最良)、
		//   スカート p90 22.16 → 25.98 (MMD 25.66)。8 は行き過ぎ (傾き14.30/12窓比1.181)。
		//   ※過去に「SubSteps=4 は 12窓比 1.288 で不採用」と記録したが、あれはジョイント
		//   warm-start が ON の時の測定。warm-start はサブステップ間で蓄積を引き継ぐため
		//   細刻みほど悪化していた。撤去後は細刻みが素直に効く (両対策が噛み合っている)。
		//   コスト: 物理位相の計装合計 0.473 → 0.876 ms/step (約1.85倍, 30Hz予算の 4%→7%)。
		int32 SubSteps = 4;
		float FixedTimeStep = 1.0f / 30.0f;

		float PenetrationSlop = 0.005f;
		float BaumgarteFactor = 0.2f;
		float RestitutionThreshold = 1.0f;

		// --- Split Impulse (接触の貫入回復を実速度から切り離す) ---
		// 貫入が閾値より深い接触のみ擬似速度側で回復し、実速度にエネルギーを注入しない。
		// 反発(restitution)は貫入回復ではないため実速度側に残す。ジョイントの ERP(Beta) は不変。
		// 既定は false (Bullet 2.75 の m_splitImpulse=false に準拠)。true で新方式へ切替。
		// ※効果検証の結果、過大スイング仮説は不支持 (反復依存が弱まらず一部窓は悪化) だったため、
		//   旧ベースライン維持と 2.75 準拠のため既定 false を選択。実装は比較用に温存する。
		bool UseSplitImpulse = false;
		// ステップ2(b): ジョイントの Baumgarte 位置バイアスを split-impulse(擬似速度)へ分離する。
		// 接触用の UseSplitImpulse とは独立 (接触既定 false=Bullet2.75 準拠は不変)。既定 false=挙動不変。
		bool UseJointSplitImpulse = false;
		// ステップ2(a-1): ジョイントの直線ロック行のみ warm-start (蓄積インパルスをサブステップ間で引継ぎ)。
		// 2026-08-09 に factor 0.85 で既定ON化した (VMD統計がMMDへ接近・12窓比が帯内・単鎖トルク3×改善)。
		// ★2026-08-12 既定OFFへ戻した。Bullet 2.75 は非接触拘束を warm-start しない
		//   (btSequentialImpulseConstraintSolver.cpp:788 で毎ステップ m_appliedImpulse=0)。
		//   0.85 でもラチェットは残っており、前髪チェーンが自励振動する。臨界係数はモデル依存で、
		//   中間値(0.70/0.50)は「どれかのモデルのピークを踏む」。5モデル実測で 0(=撤去) が唯一安全:
		//   待機区間の騒がしい3本 J中央 モデルA -89% / モデルN -41% / モデルB -32% (全モデルで全体最良)。
		//   代償はスカート平時傾き -0.46°(11.33→10.87, MMD 11.39)と髪×体貫入の1.1〜3.7倍増
		//   (ただし貫入>0.5 のフレームは全モデル0件)。12窓比はむしろ改善 1.0972→1.0608。
		//   A/B で旧既定に戻すときは両フラグを true にする (Joint::WarmStartFactor が 0.85 のまま残してある)。
		bool UseJointWarmStart = false;
		// ステップ2(a-2): 角度行も warm-start (同一性キー=軸+側 lo/hi、同一性が変わればキャッシュ破棄)。
		// UseJointWarmStart と併用。既定OFF(上記と同じ理由)。
		bool UseJointWarmStartAngular = false;
		float SplitImpulsePenetrationThreshold = -0.02f; // Bullet 2.75 m_splitImpulsePenetrationThreshold

		// 求解順序: Bullet 2.75 solveSingleIteration は 1反復内で「ジョイント(NonContact)→接触→摩擦」の順に解き、
		// 接触がジョイントより後=接触が後勝ち。自前の従来は「接触→ジョイント」でジョイントが後勝ち(接触の押し出しが
		// 毎反復打ち消される=スカート貫入の主因)。ON で Bullet 同順(ジョイント→接触)に切替。
		// 既定 false=従来順(ビット不変)。ON=Bullet 準拠。
		bool SolveJointsFirst = false;

		// 診断用: 直近の StepSimulation で実行された内部ステップ数 (0=蓄積のみ)。挙動に影響しない。
		int32 LastStepsRun = 0;

		// --- 位相別プロファイル (既定OFF=計時呼び出しゼロ=挙動/性能ともビット不変) ---
		// ON にすると SubStep 内の各位相の累積時間(ms)と回数を積む。最適化の効果測定用。
		// ★MSVC は dllexport クラスの静的メンバをカンマ区切りでまとめて宣言できない (C2487)。
		//   移植元は1行にまとめていたが、ここは1宣言ずつに分ける。
		static bool ProfileEnabled;
		static double ProfBroad;
		static double ProfBuild;
		static double ProfPrepare;
		static double ProfSpring;
		static double ProfWarm;
		static double ProfSolveContact;
		static double ProfSolveJoint;
		static double ProfIntegrate;
		static double ProfStore;
		static int64 ProfSubSteps;
		static int64 ProfContacts;
		static int64 ProfManifolds;
		static void ProfReset();

		// 接触監査#5: Bullet は接触のwarm-startで蓄積インパルスに m_warmstartingFactor(0.85) を掛ける
		// (btContactSolverInfo.h:79)。当エンジンは従来 1.0 (減衰なし=残留を捨てない) だった。
		// ★2026-08-12 Bullet 準拠の 0.85 を既定化。モデルA 実測で髪の符号バイアス -2.74°→-0.52°
		//   (負=MMDより動かなさすぎ)、スカート平時傾き・深貫入はほぼ不変。
		//   ジョイント側 warm-start 撤去と併用したときの 12窓比は 1.0972→1.0608 で MMD へ接近する。
		float ContactWarmStartFactor = 0.85f;

		// 接触監査#1+2: Bullet は 1反復内で法線→摩擦の順(摩擦は同反復の法線インパルスで上限決定)。
		// 当エンジンは従来 摩擦→法線(摩擦は前反復の法線を使用)。ON で Bullet 同順(法線先)。既定 false=ビット不変。
		bool ContactNormalBeforeFriction = false;

		// ★所有権はここが持つ。Joint / BoneLink 側は生ポインタで参照する。
		TArray<TSharedPtr<RigidBody>> Bodies;
		TArray<TSharedPtr<Joint>> Joints;

		RigidBody* Body(int32 i) const { return Bodies[i].Get(); }

		void AddBody(const TSharedPtr<RigidBody>& b);
		void AddJoint(const TSharedPtr<Joint>& j) { Joints.Add(j); }

		/**
		 * 接触マニフォールド(蓄積インパルス含む)とアキュムレータをクリアする。
		 * 物理リセット時に前状態のウォームスタート値を持ち越さないために使う。
		 */
		void ClearContacts();

		// --- 公開ステップ (可変 dt を固定ステップに分割) ---
		void StepSimulation(float DeltaTime);

		// ═══════════════════════════════════════════
		//  スリープ (Bullet の deactivation 相当) — 2026-08-10 実装
		//
		//  症状: ほとんど静止しているのに揺れ物が細かく震え続ける。MMD(Bullet)は静止した
		//  剛体を非活性化して計算から外すので完全に止まる。当エンジンは RigidBody に IsActive /
		//  SleepTimer の宣言だけがあり、どこからも使われていなかった。
		//
		//  Bullet と同じく「アイランド単位」で判定する。連結した剛体群の全員が眠りたがって
		//  いるときだけ、まとめて眠らせる。1体だけ眠らせると鎖の途中が固まって不自然になる。
		//  アイランドは「動的剛体どうしを繋ぐ Joint と接触」で連結する (Bullet 同様、
		//  static/kinematic はアイランドを繋がない = 体を介して髪とスカートが一体化しない)。
		//
		//  ★起こす条件が最重要。ここを誤ると「髪が固まって二度と動かない」というジッタより
		//    はるかに重い不具合になる。動いている kinematic (ボーン追従) 剛体に Joint or 接触で
		//    触れているアイランドは、眠りたがっていても眠らせない。ダンス中は体のボーンが
		//    動き続けるので、髪もスカートも常に起きたままになる。
		// ═══════════════════════════════════════════
		//  ★既定 OFF (2026-08-10)。実装はしたが現状ほとんど発動しない: 当エンジンの静止時の
		//    残留運動が Bullet のしきい値を超えているため (IA で |w|平均 1.5 > しきい値 1.0)。
		//    101体中 2体しか眠らず、効果が無い一方で「起こし損ねると固まる」リスクだけが残る。
		//    残留運動そのものを下げる方が先。下げられたら既定ONを検討する。
		bool EnableSleeping = false;
		float LinearSleepThreshold = 0.8f;    // Bullet 既定
		float AngularSleepThreshold = 1.0f;   // Bullet 既定
		float DeactivationTime = 2.0f;        // Bullet 既定 (秒)
		/**
		 * kinematic 剛体を「動いている」とみなす速度のしきい値。
		 * 完全に 0 でないと止まらない、を避けるための微小値。
		 */
		float KinematicMotionEpsilon = 1e-4f;
		/** 診断: 現在眠っている動的剛体の数。 */
		int32 SleepingBodyCount = 0;

		/**
		 * Group/CollisionMask/Mode を実行時に変えたら呼ぶ (次ステップで候補ペアを作り直す)。
		 * 剛体の追加は AddBody が自動で無効化する。削除APIを足す場合も必ずここを呼ぶこと。
		 */
		void InvalidateCollisionPairs() { _pairCount = -1; }

		/** 診断/テスト用: 現在の候補ペア数 (未構築なら構築する)。挙動には影響しない。 */
		int32 DebugCollisionPairCount();

		/**
		 * PMX 衝突フィルタ。16bitフィールドは「衝突する相手グループ」のビットマスク
		 * (bit=1 で衝突する)。Bullet の (groupA & maskB) && (groupB & maskA) と同じ。
		 */
		static bool ShouldCollide(const RigidBody* a, const RigidBody* b)
		{
			return (b->CollisionMask & (1 << a->Group)) != 0
				&& (a->CollisionMask & (1 << b->Group)) != 0;
		}

		int32 DebugContactCount = 0; // 診断用

		// 検証用の読み取り専用診断フック。nullptr (既定) の間は何もせず、挙動・性能に影響しない。
		// 回帰テスト (非貫入押し出しの検出など) が接触の Distance/法線インパルスを参照するために使う。
		TArray<FMmdDebugContact>* DebugContacts = nullptr;

	private:
		TMap<int64, TSharedPtr<PersistentManifold>> _manifolds;
		TArray<ContactConstraint> _contacts;
		TArray<ContactPoint> _detectBuffer; // Detect の返り値受け取り
		float _accumulator = 0.0f;

		TArray<RigidTransform> _frameKinStart; // フレーム開始時のキネマティック姿勢 (body index 単位)

		// --- ブロードフェーズの候補ペア (最適化, 2026-08-09) ---
		// static/kinematic 同士の除外と ShouldCollide(Group/Mask) は「不変な情報」なので毎サブステップ
		// 総当たりで再判定する必要がない。初回に候補ペアを作り置きし、以後はそれだけを走査する。
		// (IA: 総当たり6786 → 候補のみへ削減。ペア順序は i昇順→k昇順 で従来と同一=結果ビット不変)
		//
		// ★このキャッシュが依存している前提 (崩れると「本来当たるペアが当たらない」形で静かに壊れる):
		//   1. Body の Group / CollisionMask が実行中に変わらない
		//   2. Body の Mode (static/kinematic/dynamic の別) が実行中に変わらない
		//   3. Bodies の増減は AddBody 経由 (自動で無効化する)
		// 現状の運用では全て成立するが、将来インパルスモーフ配線・剛体の動的追加/削除・
		// 実行時のモード切替を入れる場合は必ず InvalidateCollisionPairs() を呼ぶこと。
		// 壊れ方が静か(例外も警告も出ず、単に衝突しなくなる)なので、疑わしいときは
		// DebugCollisionPairCount を before/after で比較すること。
		TArray<int32> _pairA, _pairB;
		int32 _pairCount = -1;
		int32 _pairBuiltForCount = -1;
		TArray<Aabb> _aabbScratch;
		TSet<int64> _seenScratch;
		TArray<int64> _deadScratch;

		// マニフォールドを決定的順序で解くための再利用バッファ (毎ステップのアロケーションを避ける)。
		TArray<PersistentManifold*> _sortedManifolds;

		// union-find (スリープのアイランド判定)。
		TArray<int32> _uf;              // union-find の親 (剛体 index)
		TArray<bool> _wantsSleep;       // その剛体が眠りたがっているか
		TArray<bool> _islandBlocked;    // その根のアイランドは眠らせない
		TArray<bool> _islandWants;      // その根のアイランドは全員が眠りたがっている

		int32 UfFind(int32 x) { while (_uf[x] != x) { _uf[x] = _uf[_uf[x]]; x = _uf[x]; } return x; }
		void UfUnion(int32 a, int32 b) { a = UfFind(a); b = UfFind(b); if (a != b) _uf[a] = b; }

		void InternalStep(float dt, int32 gBase, int32 TotalSub);
		void SubStep(float dt, float Frac);
		void UpdateSleeping(float dt);
		void IntegrateVelocities(float dt);
		void IntegratePositions(float dt);
		void BuildCollisionPairs();
		void BroadphaseNarrowphase();
		void BuildContactConstraints(float dt);
		void StoreImpulses();
		void WarmStart();
		void SolveContacts();
		void SolveSplitImpulse();

		static float DampingFactor(float Damping, float dt);
		static Vec3 QuatToAngularVelocity(Quat dq, float dt);
		static RigidTransform InterpTransform(const RigidTransform& From, const RigidTransform& To, float Frac);
		static int64 PairKey(int32 a, int32 b);
		static float EffectiveMass(const RigidBody* a, const RigidBody* b, const Vec3& rA, const Vec3& rB, const Vec3& Dir);
		static void BuildTangentBasis(const Vec3& n, Vec3& t1, Vec3& t2);
		static void SolveNormal(ContactConstraint& c, RigidBody* a, RigidBody* b);
		static void SolveFriction(ContactConstraint& c, RigidBody* a, RigidBody* b,
			const Vec3& Tangent, float Mass, float& Accum);
	};
}
