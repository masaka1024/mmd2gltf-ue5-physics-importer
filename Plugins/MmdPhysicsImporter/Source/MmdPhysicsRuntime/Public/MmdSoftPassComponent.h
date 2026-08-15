// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 半透明材質の 2 パス目 (柔らかい毛先) を描くコンポーネント。
//
// 移植元 lilToon の TwoPass は 2 回描く:
//   1 パス目 … α >= 0.5 を **不透明** で描き、**深度も書く**  ← 本体側が Masked で担当
//   2 パス目 … 全面を素のアルファでブレンド                   ← ここが担当
//
// UE のマテリアルは 1 枚で 2 パスを持てない (Masked と Translucent は排他)。
// そこで本体を Masked にして芯と深度を作り、このコンポーネントが同じメッシュを
// もう一度 Translucent で描く。深度テストは効くので、1 パス目が書いた深度によって
// 奥の房が正しく弾かれる。
//
// ★2 パス目は「α < 0.5 だけ」に絞る必要はない。α >= 0.5 の部分は 1 パス目が既に
//   同じ色で不透明に塗っているので、その上から同じ色を重ねても変わらないため。
//   絞る (αを圧縮する) と毛先が本来より濃くなる。
//
// これを入れないと、髪は次のどちらかにしかならない:
//   ・全部を Translucent にする → 房が重なると足し算で飽和し、グラデーションが潰れる
//   ・全部を Masked にする      → 毛先が硬く切り落とされる
// ===========================================================================

#pragma once

#include "MmdFollowerMeshComponent.h"
#include "MmdSoftPassComponent.generated.h"

class UMaterialInterface;

UCLASS(ClassGroup = (MmdPhysics), meta = (BlueprintSpawnableComponent),
	HideCategories = (Physics, Collision, Lighting, Navigation, VirtualTexture))
class MMDPHYSICSRUNTIME_API UMmdSoftPassComponent : public UMmdFollowerMeshComponent
{
	GENERATED_BODY()

public:
	/**
	 * 2 パス目に使う半透明マスター。空なら本体メッシュと同じフォルダの
	 * "M_MmdToonTranslucent" を探す (マテリアル変換がそこに作る)。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MMD Soft Pass")
	TObjectPtr<UMaterialInterface> SoftPassMaterial;

	/** 2 パス目を描くか。切ると毛先が硬くなる (本体の Masked だけの絵になる)。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Soft Pass")
	bool bDrawSoftPass = true;

	virtual void RebuildMaterials() override;
};
