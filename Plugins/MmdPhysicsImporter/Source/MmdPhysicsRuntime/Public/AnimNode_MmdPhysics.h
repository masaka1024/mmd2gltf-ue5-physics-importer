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

	/** 実効刻み = FixedTimeStep / SubSteps。既定 (1/30)/4 = 1/120。 */
	UPROPERTY(EditAnywhere, Category = "Solver", meta = (ClampMin = "1"))
	int32 SubSteps = 4;

	UPROPERTY(EditAnywhere, Category = "Solver", meta = (ClampMin = "0.0001"))
	float FixedTimeStep = 1.0f / 30.0f;

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
