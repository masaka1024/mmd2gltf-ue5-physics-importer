// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 本体と同じスケルタルメッシュをもう一度描くコンポーネントの共通部分。
//
// MMD の描き方には「同じメッシュを 2 回描く」ものが 2 つある。
//   ・輪郭線 (反転ハル)          … UMmdOutlineComponent
//   ・半透明の 2 パス目 (毛先)    … UMmdSoftPassComponent
//
// どちらも「本体と同じメッシュ・同じポーズ・同じ表情で、別のマテリアルで描く」点は
// 同じなので、そこをここへ集めてある。
//
// ★メッシュは複製しない。同じアセットを参照し、
//   ・ボーン … SetLeaderPoseComponent で本体に追従 (MMD 物理の結果込み)
//   ・モーフ … MorphTargetWeights を毎フレーム複製 (同じメッシュなので添字が一致)
//   とする。セクションを複製して焼き込む方式だとモーフのデルタまで作り直すことになり、
//   しかも取り込んだアセットを書き換えるのでモデルを再インポートすると消える。
// ===========================================================================

#pragma once

#include "Components/SkeletalMeshComponent.h"
#include "MmdFollowerMeshComponent.generated.h"

UCLASS(Abstract, HideCategories = (Physics, Collision, Lighting, Navigation, VirtualTexture))
class MMDPHYSICSRUNTIME_API UMmdFollowerMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UMmdFollowerMeshComponent();

	/** 本体からマテリアルを読み直して割り当てる。 */
	UFUNCTION(BlueprintCallable, Category = "MMD")
	virtual void RebuildMaterials() {}

	//~ Begin UActorComponent
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UActorComponent

protected:
	/** 追従先 (親のスケルタルメッシュコンポーネント)。 */
	USkeletalMeshComponent* FindLeader() const;

	/**
	 * メッシュと同じフォルダにあるマスターマテリアルを探す。
	 * マテリアル変換がそこに作る (M_MmdToon / M_MmdToonTranslucent / M_MmdOutline)。
	 */
	UMaterialInterface* FindMasterMaterial(const TCHAR* AssetName) const;
};
