// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// MMD の輪郭線 (エッジ) を描くコンポーネント。
//
// MMD は輪郭線を「モデルをもう一度、法線方向へ膨らませて、表面を捨てて描く」
// 反転ハルで出している。ここでも同じことをする。
//
// ★メッシュは複製しない。**同じスケルタルメッシュ**をもう一度描くコンポーネントを
//   本体の子として置き、ボーンは SetLeaderPoseComponent で本体に追従させる。
//
//   セクションを複製してメッシュに焼き込む方式も検討したが、MMD モデルは
//   表情モーフを大量に持つ (IA は全 19 セクションが 45 モーフ)。複製すると
//   モーフのデルタも作り直すことになり、しかも取り込んだアセットを書き換えるので
//   モデルを再インポートすると消える。同じメッシュを参照すればモーフは元のまま使え、
//   ウェイトも MorphTargetWeights を配列ごとコピーするだけで揃う (添字が一致するため)。
//
// 代償は「メッシュ 1 体分の描画が増える」こと。これは反転ハルの原理的なコストで、
// MMD 自身も同じ枚数を描いている。
//
// 使い方: 本体のスケルタルメッシュコンポーネントの子として追加するだけ。
//         メッシュ・マテリアル・追従は登録時に自動で設定する。
// ===========================================================================

#pragma once

#include "Components/SkeletalMeshComponent.h"
#include "MmdOutlineComponent.generated.h"

class UMaterialInterface;

UCLASS(ClassGroup = (MmdPhysics), meta = (BlueprintSpawnableComponent),
	HideCategories = (Physics, Collision, Lighting, Navigation, VirtualTexture))
class MMDPHYSICSRUNTIME_API UMmdOutlineComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UMmdOutlineComponent();

	/**
	 * 輪郭線マスター。空なら本体メッシュと同じフォルダの "M_MmdOutline" を探す
	 * (マテリアル変換がそこに作る)。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MMD Outline")
	TObjectPtr<UMaterialInterface> OutlineMaterial;

	/**
	 * PMX の edgeSize を UE の cm へ落とす係数。
	 * MMD の見た目に合わせる値なので、モデルによって調整する
	 * (IA の edgeSize は 0.596)。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Outline",
		meta = (ClampMin = "0.0", UIMax = "1.0"))
	float OutlineWidthScale = 0.15f;

	/** 輪郭線を描くか。切ると何も描かない (材質ごとの ON/OFF は PMX のフラグに従う)。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Outline")
	bool bDrawOutline = true;

	/** 本体から材質ごとの輪郭線設定を読み直して割り当てる。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Outline")
	void RebuildOutlineMaterials();

	//~ Begin UActorComponent
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
	/** 詳細パネルで太さなどを触ったら、その場で材質へ流し込む。 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UActorComponent

private:
	/** 追従先 (親のスケルタルメッシュコンポーネント)。 */
	USkeletalMeshComponent* FindLeader() const;
};
