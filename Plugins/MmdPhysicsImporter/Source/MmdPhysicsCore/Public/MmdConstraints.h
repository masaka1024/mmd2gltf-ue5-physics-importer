// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// Bullet 互換物理エンジン – Joint Constraints
// PMX Joint 6 種を Bullet 相当の Sequential-Impulse で解く。
//   0: ﾊﾞﾈ付6DOF -> btGeneric6DofSpringConstraint
//   1: 6DOF      -> btGeneric6DofConstraint
//   2: P2P       -> btPoint2PointConstraint
//   3: ConeTwist -> btConeTwistConstraint
//   4: Slider    -> btSliderConstraint
//   5: Hinge     -> btHingeConstraint
//
// 移植元: Assets/MMD_Scripts/MmdPhysics/Core/Constraints.cs (namespace BulletPhysics)
// ===========================================================================

#pragma once

#include "MmdRigidBody.h"

namespace MmdPhysics
{
	enum class EJointType : uint8
	{
		Spring6Dof = 0,
		Generic6Dof = 1,
		Point2Point = 2,
		ConeTwist = 3,
		Slider = 4,
		Hinge = 5,
	};

	/** 制約ソルバの 1 行 (linear または angular)。 */
	struct ConstraintRow
	{
		Vec3 Axis;                        // ワールド軸 (単位)
		bool Angular = false;             // true=回転行, false=並進行
		Vec3 RelA, RelB;                  // 重心からのアンカーオフセット (linear 用)
		float LowerImpulse = 0.0f;
		float UpperImpulse = 0.0f;
		float TargetVel = 0.0f;           // 目標相対速度 (split時=バイアス抜きの速度目標, 非split時=Baumgarte込み)
		float EffMass = 0.0f;
		float Accumulated = 0.0f;
		float PositionBias = 0.0f;        // split時: 位置補正の目標擬似速度 (err*Beta/dt)。非split時0。
		float PseudoAccumulated = 0.0f;   // split時: 擬似速度側の累積インパルス。
		int32 Dof = 0;                    // 0-2: DOF番号 (warm-start のキャッシュキー)。
		bool WarmStartable = false;       // (a-1) この行を warm-start 対象にするか (直線ロック行のみ)。
	};

	/**
	 * 全 Joint 種の基盤となる 6DOF 制約。
	 * PMX の位置/回転/移動制限/回転制限/バネ定数をそのまま解釈する。
	 */
	class MMDPHYSICSCORE_API Joint
	{
	public:
		FString Name;
		EJointType Type = EJointType::Spring6Dof;

		// ★所有権は持たない。実体は PhysicsWorld::Bodies が保持する。
		RigidBody* BodyA = nullptr;
		RigidBody* BodyB = nullptr;

		// 剛体ローカルに変換済みのジョイントフレーム。
		RigidTransform FrameInA = RigidTransform::Identity;
		RigidTransform FrameInB = RigidTransform::Identity;

		// 移動/回転の制限 (下限, 上限)。
		Vec3 LinearLowerLimit;
		Vec3 LinearUpperLimit;
		Vec3 AngularLowerLimit;
		Vec3 AngularUpperLimit;

		// バネ定数 (移動/回転)。0 で無効。
		Vec3 SpringLinear;
		Vec3 SpringAngular;

		// バネのダンピング比 (PMX には無いので既定値)。
		float SpringDamping = 0.1f;

		// 位置補正係数 (Baumgarte)。
		float Beta = 0.2f;
		// 位置補正速度の上限。Bullet の線形行には存在しない自前のみの安全弁 (監査#3)。
		// 既定 10 =従来値(ビット不変)。1e9 相当で実質無効=Bullet同等。static につき env から A/B 可。
		static float MaxCorrectionVel;

		// 角度リミット行の軸: Bullet 混合軸 (calculateAngleInfo, 2026-08-09 実ソース取得済み)。
		//   axis0 = B基底列0, axis2 = A基底列2,
		//   axis[1]=axis2×axis0, axis[0]=axis[1]×axis2, axis[2]=axis0×axis[1] (各 normalize, 一般に非直交)。
		// 自前既定は A基底の直交列(_axesA)。8/07に一度試して破棄したが、当時は
		// (1)軸と誤差の対応順序が未確認のままで実装誤りの可能性 (2)判定相手が補正ON版のみ。
		// 補正OFF版(純Bullet)を相手に再評価するため復活。既定 false=従来(ビット不変)。
		static bool AngularMixedAxes;

		// 線形ロック行のレバーアーム基準 (線形行監査#1, 2026-08-09)。既定 0=従来(ビット不変)。
		//  0=従来: rA=anchorA-comA, rB=anchorB-comB (各剛体が自分側のアンカーを使う)
		//  1=Bullet2.75系(非offset): 両剛体とも B側アンカー基準 ("Linear Torque Decoupling")
		//     J1ang=(anchorB-posA)×ax, J2ang=-(anchorB-posB)×ax → 誤差があると親へ e×P の結合トルクが伝わる
		//  2=Bullet2.8x系(offset, D6_USE_FRAME_OFFSET true 既定): 軸平行成分を除去した ortho +
		//     totalDist を質量比 factA=miB/(miA+miB) で分配。hasStaticBody&&!rotAllowed で fact スケール。
		static int32 LinearLeverMode;

	private:
		// 内部状態。
		TArray<ConstraintRow> _rows;
		RigidTransform _worldA, _worldB;
		Vec3 _anchorA, _anchorB;
		Vec3 _axesA[3];
		// ステップ2(b): Baumgarte バイアスを split-impulse(擬似速度)側へ分離するか。
		// false(既定)で従来どおり実速度側へバイアスを乗せる (挙動不変)。
		bool _splitBias = false;
		// ステップ2(a-1): 直線ロック行のみ warm-start (蓄積インパルスをサブステップ間で引き継ぐ)。
		// 直線ロック行は常時アクティブで DOF が安定 (角度行のような軸/側のトグルが無い) ため安全。
		bool _warmStart = false;
		float _warmLin[3] = { 0.0f, 0.0f, 0.0f };      // 直線 DOF 別の前サブステップ蓄積インパルス。
		bool _warmLinSeen[3] = { false, false, false }; // このサブステップで当該 DOF の warm 対象行が出たか。
		// ステップ2(a-2): 角度 warm-start。同一性キー=軸(dof)+側(sideCode: 0=locked/1=lower/2=upper)。
		// 前サブステップと同じ側なら引き継ぎ、変われば破棄する (Euler 分解で軸/側がトグルする問題対策)。
		bool _warmStartAng = false;
		float _warmAng[3] = { 0.0f, 0.0f, 0.0f };         // 角度 DOF 別の蓄積インパルス。
		int32 _warmAngPrevSide[3] = { -1, -1, -1 };        // 前サブステップの側 (-1=不在)。
		bool _warmAngSeen[3] = { false, false, false };    // このサブステップで当該 DOF の角度 warm 行が出たか。

	public:
		// 診断: 角度 warm 行の総数と、同一性(側)が前サブステップから変わった行数。
		static int64 WarmAngRows;
		static int64 WarmAngToggles;
		// warm-start 引継ぎ係数。過拘束系で引継ぎインパルスがラチェット的に積み上がるのを抑える。
		// ★2026-08-12: ジョイントの warm-start 自体を既定OFFにしたので、この値は
		//   PhysicsWorld::UseJointWarmStart(Angular) を true に戻したときだけ効く (A/B用)。
		//   0.85 は Bullet の接触側 m_warmstartingFactor から借りた値で、旧既定の再現用に残してある。
		//   ★この値でジョイントを warm-start してはいけない: 0.85 でもラチェット共振が残り、
		//   臨界係数がモデル依存で動くため中間値はどれかのモデルで悪化する (PhysicsWorld.cpp の注記参照)。
		static float WarmStartFactor;

		// --- ファクトリ: PMX Joint 種から生成 ---

		/** PMX の raw パラメータから Joint を構築する。 */
		static TSharedPtr<Joint> FromPmx(
			EJointType InType, RigidBody* a, RigidBody* b,
			const RigidTransform& WorldFrame,
			const Vec3& LinLo, const Vec3& LinHi, const Vec3& AngLo, const Vec3& AngHi,
			const Vec3& SpringLin, const Vec3& SpringAng);

		// --- 準備: フレーム/アンカー/行を構築 ---
		void Prepare(float dt, bool bSplitBias = false, bool bWarmStart = false, bool bWarmStartAng = false);

		// --- バネ (明示力積, サブステップ毎に 1 回) ---
		void ApplySprings(float dt);

		// --- 速度反復 (world から複数回呼ばれる) ---
		void SolveVelocity();

		// --- 位置補正の split-impulse 反復 (擬似速度側)。UseJointSplitImpulse 有効時のみ world から呼ばれる ---
		void SolveSplitPosition();

		// --- Euler XYZ 抽出 (Bullet の matrixToEulerXYZ 相当) ---
		// ★PmxPhysicsBuilder の ComputeAlignedBonePoses からも使うため public。
		static Vec3 ToEulerXYZ(const Quat& q);

	private:
		static bool IsLocked(float lo, float hi) { return lo == hi; }
		static bool IsFree(float lo, float hi) { return lo > hi; }

		static float Clamp(float v);

		void AddLinearRow(const Vec3& Axis, const Vec3& rA, const Vec3& rB, float TargetVel, float lo, float hi, int32 Dof, bool bLocked);
		void AddAngularRow(const Vec3& Axis, float TargetVel, float lo, float hi, int32 Dof, int32 SideCode);

		static float ClampSpringImpulse(float Impulse, float Err, float InvMEff, float InvDt);
		static float ClampToLimit(float v, float lo, float hi);
	};
}
