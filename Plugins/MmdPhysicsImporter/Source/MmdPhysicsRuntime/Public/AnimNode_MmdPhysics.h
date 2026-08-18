// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// MMD 物理をスケルタルメッシュへ適用する AnimGraph ノード。
//
// ★配置場所: スケルタルメッシュの Post-Process Anim Blueprint。
//   移植元 Unity 版は LateUpdate (= Animator 適用後) で書き戻していた。
//   FixedUpdate で書くと、揺れ物ボーンにカーブを持つクリップでは Animator に
//   毎フレーム上書きされて物理が一切見えなくなる、という記録が残っている。
//   UE で同じ位置にあたるのが Post-Process AnimBP のスケルタルコントロールで、
//   メインの AnimGraph 評価の後に走る。コンポーネント Tick から
//   SetBoneTransformByName を叩く方式は、次のアニメーション評価で上書きされるため使えない。
//
// 参考実装: Engine/Source/Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_RigidBody.h
// ===========================================================================

#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "MmdPmxPhysicsBuilder.h"
#include "AnimNode_MmdPhysics.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct MMDPHYSICSRUNTIME_API FAnimNode_MmdPhysics : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	// --- 入力 ---

	/**
	 * mmd2gltf-gui が出力した .glb の絶対パス。extras.mmd から剛体とジョイントを読む。
	 * 一般の glTF エクスポータの出力には extras.mmd が無いので動作しない。
	 */
	UPROPERTY(EditAnywhere, Category = "Source")
	FString GlbPath;

	/**
	 * PMX 1 単位あたりのメートル。extras.mmd の unitScale と一致させること。
	 * 既定 0.08 は初音ミク標準モデルの身長 20 単位 ≒ 158cm に由来する。
	 * ★物理演算そのものは PMX ネイティブ単位のまま行い、この値は UE への出力時にしか掛からない。
	 */
	UPROPERTY(EditAnywhere, Category = "Source", meta = (ClampMin = "0.0001"))
	float UnitScale = 0.08f;

	// --- ソルバ (移植元 MmdPhysicsBehaviour の Inspector 値をそのまま移す) ---

	/**
	 * PMX 単位系での重力。9.8 * 10 = 98。
	 * ★UE のワールド重力とは無関係。エンジンが PMX 単位のまま回るので単位換算しない。
	 *   長さだけスケールして重力を据え置くと振り子の周期が √0.08 ≒ 0.283 倍になり、
	 *   MMD より 3.5 倍速く揺れてしまう。
	 */
	UPROPERTY(EditAnywhere, Category = "Solver")
	float Gravity = 98.0f;

	UPROPERTY(EditAnywhere, Category = "Solver", meta = (ClampMin = "1"))
	int32 SolverIterations = 10;

	/**
	 * ジョイントだけを追加で回す速度反復の回数。既定 40。
	 *
	 * ★揺れ物の長い鎖が伸び続けるのを止めるための値。
	 *   Gauss-Seidel は 1 反復でジョイント 1 本ぶんしか情報を伝えないので、
	 *   節数が反復数を超える鎖では根元の拘束が末端まで届かない。届かなかった相対速度が
	 *   毎サブステップ残り、位置誤差として**単調に溜まる**(戻らないラチェットになる)。
	 *   実測 (Tda式初音ミク・アペンド Ver1.10 の しっぽ = 13 節、アニメ再生 20 秒):
	 *     10 (= SolverIterations と同じ = 従来) … しっぽ３ が 220 倍。鎖が真下へ 55cm/秒 で落ち続ける
	 *     20                                    … 20 秒では収まるが 60 秒で再発 (123 倍)
	 *     40 (+下の上限 30)                     … 233 秒 (曲の全長) でも 1.20 倍で収まる
	 *   40 は しっぽ 13 節に対して 3 倍の余裕。もっと長い鎖のモデルが出たら上げる。
	 *
	 * ★接触 (SolverIterations) は 10 のまま据え置くこと。接触の反復を増やすと
	 *   待機区間の揺れが悪化する (IA 実測 8.91cm/1.72cm、既定 7.17/0.96 より悪い)。
	 *   鎖に要るのはジョイントの伝播だけなので、ここだけ伸ばす。
	 *
	 * ★位置補正 (bUseSplitImpulse / Baumgarte) を強めても直らない。
	 *   位置補正側の反復を 40 回にしても 178 倍のままだった。効くのは速度側だけ。
	 */
	UPROPERTY(EditAnywhere, Category = "Solver", meta = (ClampMin = "1"))
	int32 JointVelocityIterations = 40;

	/**
	 * ジョイントの位置補正 (Baumgarte) 速度の上限 [PMX単位/秒]。既定 30。
	 *
	 * ★上の JointVelocityIterations と**対で**効く。片方だけでは鎖の伸びは止まらない:
	 *     反復 40 / 上限 10 (従来)  … 120 秒で しっぽ３ が 201 倍
	 *     反復 10 / 上限 無制限     … 20 秒で 426 倍 (悪化する。速度が収束していないのに
	 *                                 補正だけ強めると行き過ぎる)
	 *     反復 40 / 上限 30         … 233 秒 (曲の全長) でも 1.20 倍
	 *
	 * ★機構: 速度求解の解き残しが位置誤差として毎サブステップ積もる。誤差が
	 *   上限 / (Beta * 1/dt) ≒ 0.42 PMX単位 (3.3cm) を超えると補正速度が頭打ちになり、
	 *   重力が開く速さに追いつけなくなる。以後その誤差は二度と戻らない
	 *   (ChainStability で最小比が 1.00 のまま最大比だけ伸び続けるのはこのため)。
	 *
	 * ★上限は低すぎても高すぎても壊れる。120 秒までは 20 / 30 / 100 / 1e9 が同結果に
	 *   見えるが、233 秒 (曲の全長) では **100 が しっぽ３ 468 倍で破綻し、30 は 1.20 倍で
	 *   通る**。強い補正はアンカーの開きに比例した反動も強めるので、
	 *   「発散を止められる範囲で一番弱い補正」を選ぶこと。30 は本モデル実測での通過値。
	 */
	UPROPERTY(EditAnywhere, Category = "Solver", meta = (ClampMin = "0.0"))
	float JointMaxCorrectionVel = 30.0f;


	/**
	 * 位置補正 (Baumgarte) を擬似速度で解き、実速度に残さない。
	 *
	 * ★静止しているはずの場面で揺れ物が揺れ続けるのを抑える。既定 ON。
	 *
	 * 切ると、貫入やジョイントのずれを直すための補正がそのまま実速度になり、
	 * 揺れ物へエネルギーを注ぎ続ける。減衰が強くても止まらない (減衰は拘束を解く前に
	 * 掛かるため、解いた後の速度には効かない) ので、固有振動数での共振が持続する。
	 *
	 * IA での実測 (ボーンを動かさず 30 秒。最後の 5 秒の振れ幅):
	 *   両方 OFF … 最大 14.18cm / 平均 3.77cm (スカートが 1.5Hz で振れ続け、時間とともに増える)
	 *   両方 ON  … 最大  7.17cm / 平均 0.96cm (振れは時間とともに減る)
	 *   接触のみ 13.99 / 3.28、ジョイントのみ 12.63 / 2.32 = **両方要る**
	 * 反復を増やす (8.91/1.72) / Bullet 順 (8.67/1.56) はいずれも悪化した。
	 *
	 * ★コア (PhysicsWorld) の既定は OFF のまま。数値パリティテストは移植元 C# と
	 *   ビット一致で比べるので、コア側の既定は動かせない。刻み (1/60x2) と同じく、
	 *   **ノードだけが移植元の調整値を持つ**構造にしてある
	 *   (docs/porting_notes.md「ソルバ既定値はノードとコアで違う」)。
	 */
	UPROPERTY(EditAnywhere, Category = "Solver")
	bool bUseSplitImpulse = true;

	/** ジョイント側の位置補正を擬似速度で解く。上の bUseSplitImpulse と対で使う。 */
	UPROPERTY(EditAnywhere, Category = "Solver")
	bool bUseJointSplitImpulse = true;

	/**
	 * 実効刻み = FixedTimeStep / SubSteps。既定 (1/60)/2 = 1/120。
	 * 細刻み化は SubSteps 側で行うこと (下の FixedTimeStep の注記を参照)。
	 */
	UPROPERTY(EditAnywhere, Category = "Solver", meta = (ClampMin = "1"))
	int32 SubSteps = 2;

	/**
	 * ★1/60 のまま触らないこと。細刻みが欲しいときは SubSteps を増やす。
	 *
	 * 移植元 MmdPhysicsBehaviour.cs の既定に合わせている (2026-08-09 のジャダー調査で
	 * 1/30×2サブ から 1/60×1サブ へ、2026-08-13 に貫入対策で 1/60×2サブ へ)。
	 * 実効刻みは 1/120 で MmdPhysics::PhysicsWorld の既定 (1/30×4) と同じだが、
	 * 「1 フレームに何ステップ進むか」が違う。
	 *
	 * PhysicsWorld はアキュムレータ方式なので、FixedTimeStep がフレーム間隔より長いと
	 * 1 フレームあたりの内部ステップが 0,1,0,1,1,... と変動する。物理の進む量は合っていても
	 * 実時間の更新間隔が 20ms/40ms とばらつくので、髪やスカートがコマ落ちして見える。
	 * 60fps 想定で 1/60 にしておくと毎フレームちょうど 1 ステップ進み、間隔が揃う。
	 * (移植元は Time.fixedDeltaTime と一致させることで同じ状態を作っていた。UE の
	 *  Post-Process AnimBP は可変フレームレートで評価されるため厳密な等間隔にはならないが、
	 *  刻みがフレーム間隔以下なら取りこぼしが出ないぶんばらつきは小さくなる。)
	 * 実機でも 1/30 のままだと髪・スカートの揺れが移植元より大きく荒れて見えた。
	 */
	UPROPERTY(EditAnywhere, Category = "Solver", meta = (ClampMin = "0.0001"))
	float FixedTimeStep = 1.0f / 60.0f;

	// --- 書き戻しの補正 ---

	/** PMX mode2 (物理演算+ボーン位置合わせ) を再現する。 */
	UPROPERTY(EditAnywhere, Category = "Correction")
	bool bEnableBoneMergeMode = true;

	/** 全剛体に位置合わせ補正を掛ける (mode2 かどうかに関わらず)。 */
	UPROPERTY(EditAnywhere, Category = "Correction")
	bool bAlignBonePositions = false;

	/** 親側ジョイントのリミットへ回転を戻す割合 (0=そのまま / 1=完全clamp)。 */
	UPROPERTY(EditAnywhere, Category = "Correction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AlignRotClampAlpha = 0.5f;

	// --- 起動 ---

	/**
	 * 開始から数フレーム、剛体をボーン姿勢へ再整合する。
	 * アニメーションがフレーム0を適用した後の骨格に合わせないと、
	 * 体のコライダーだけが動いて揺れ物がバインド位置に取り残され、貫入平衡に落ちる。
	 */
	UPROPERTY(EditAnywhere, Category = "Startup", meta = (ClampMin = "0"))
	int32 PoseResetDelayFrames = 2;

	/**
	 * ワープ検出のしきい値 [PMX単位]。0 で無効。
	 *
	 * 駆動 (mode0) 剛体のターゲットが 1 フレームでこれ以上動いたら「テレポート」とみなし、
	 * 物理を現在のボーン姿勢へ再整合する。アニメの**ループ境界** (最終フレーム→先頭)、
	 * シーク、アクターのワープで骨格が一瞬で飛ぶと、前姿勢との差がそのまま kinematic
	 * 速度になって鎖を薙ぎ払う (実測: 曲の全長 233 秒は通るのに、ループを 1 回またぐ
	 * 466 秒では しっぽ２ が一過性で 3.25 倍まで伸びた)。
	 *
	 * 既定 3 = unitScale 0.08 で 24cm/フレーム (30fps なら 7.2m/s)。ダンス実測の
	 * 最大剛体速度は ~1m/s なので、通常モーションでは発火しない。
	 */
	UPROPERTY(EditAnywhere, Category = "Startup", meta = (ClampMin = "0.0"))
	float TeleportResetThreshold = 3.0f;

	// --- 検査 ---

	/**
	 * 取り込み経路の検査。extras.mmd のボーン位置と参照ポーズを突き合わせ、
	 * UE 標準の Interchange glTF 以外で取り込まれていないかを起動時に確認する。
	 */
	UPROPERTY(EditAnywhere, Category = "Diagnostics")
	bool bCheckImportConvention = true;

	// --- FAnimNode_SkeletalControlBase ---
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

	/** 物理を現在のボーン姿勢へ再整合する (移植元 ResetPhysicsToBones 相当)。 */
	void RequestPoseReset() { StartupResetCountdown = FMath::Max(1, PoseResetDelayFrames); }

	/**
	 * 診断専用: 内部の物理ワールドを覗く。
	 * ★自動テストが「ボーンではなく剛体そのもの」を見るための口。
	 *   ボーンの書き戻しを通すと、剛体の暴れとボーン合わせの補正が混ざって
	 *   どちらが壊れているのか分からなくなる。通常の再生経路では使わない。
	 */
	const MmdPhysics::PmxPhysicsBuilder* GetBuilderForDiagnostics() const { return Builder.Get(); }

private:
	TSharedPtr<MmdPhysics::PmxPhysicsModel> Model;
	TSharedPtr<MmdPhysics::PmxPhysicsBuilder> Builder;

	/** PMX ボーン index → コンパクトポーズ index。未解決は INDEX_NONE。 */
	TArray<FCompactPoseBoneIndex> PmxBoneToCompact;

	float AccumulatedDelta = 0.0f;
	int32 StartupResetCountdown = 0;

	bool bLoadAttempted = false;
	bool bNanReported = false;
	bool bConventionChecked = false;

	void EnsureLoaded();
	void ApplySolverSettings();
	void ResolveBones(const FBoneContainer& RequiredBones);
	void CheckImportConvention(const FBoneContainer& RequiredBones);
};
