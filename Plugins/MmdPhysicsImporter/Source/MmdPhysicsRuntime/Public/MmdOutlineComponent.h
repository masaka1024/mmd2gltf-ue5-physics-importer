// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// MMD の輪郭線 (エッジ) を描くコンポーネント。
//
// MMD は輪郭線を「モデルをもう一度、法線方向へ膨らませて、表面を捨てて描く」
// 反転ハルで出している。ここでも同じことをする。
//
// メッシュを複製せずに済ませる仕組み (同じメッシュ + ボーン追従 + モーフ複製) は
// 基底クラス UMmdFollowerMeshComponent を参照。
//
// 使い方: 本体のスケルタルメッシュコンポーネントの子として追加するだけ。
//         メッシュ・追従・材質ごとの輪郭線色は登録時に自動で設定する。
// ===========================================================================

#pragma once

#include "MmdFollowerMeshComponent.h"
#include "MmdOutlineComponent.generated.h"

class UMaterialInterface;

UCLASS(ClassGroup = (MmdPhysics), meta = (BlueprintSpawnableComponent),
	HideCategories = (Physics, Collision, Lighting, Navigation, VirtualTexture))
class MMDPHYSICSRUNTIME_API UMmdOutlineComponent : public UMmdFollowerMeshComponent
{
	GENERATED_BODY()

public:
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

	virtual void RebuildMaterials() override;
};
