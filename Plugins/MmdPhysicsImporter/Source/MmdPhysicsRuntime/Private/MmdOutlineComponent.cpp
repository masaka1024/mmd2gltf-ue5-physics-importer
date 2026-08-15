// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdOutlineComponent.h"

#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

void UMmdOutlineComponent::RebuildMaterials()
{
	USkeletalMeshComponent* Leader = FindLeader();
	USkeletalMesh* Mesh = GetSkeletalMeshAsset();
	if (Leader == nullptr || Mesh == nullptr) return;

	UMaterialInterface* Master = OutlineMaterial;
	if (Master == nullptr)
	{
		Master = FindMasterMaterial(TEXT("M_MmdOutline"));
	}
	if (Master == nullptr)
	{
		// 素材が無ければ何もしない。輪郭線が出ないだけで本体には影響しない。
		return;
	}

	// ★輪郭線は半透明 (エッジ色のアルファを効かせるため)。本体の半透明 (髪など) より
	//   先に描かないと、髪の上に輪郭線が乗る。膨らませた輪郭線は常に本体の外側なので
	//   先に描くのが正しい。
	SetTranslucentSortPriority(-1);

	const int32 NumSlots = Mesh->GetMaterials().Num();
	for (int32 Slot = 0; Slot < NumSlots; Slot++)
	{
		// 材質ごとの輪郭線の色と太さは、本体のマテリアルインスタンスが持っている
		// (マテリアル変換が EdgeColor / EdgeSize / UseOutline を入れてある)。
		FLinearColor EdgeColor = FLinearColor::Black;
		float EdgeSize = 1.0f;
		float UseOutline = 0.0f;
		if (UMaterialInterface* Body = Leader->GetMaterial(Slot))
		{
			Body->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EdgeColor")), EdgeColor);
			Body->GetScalarParameterValue(FMaterialParameterInfo(TEXT("EdgeSize")), EdgeSize);
			Body->GetScalarParameterValue(FMaterialParameterInfo(TEXT("UseOutline")), UseOutline);
		}

		const bool bDraw = bDrawOutline && UseOutline > 0.5f;
		// 描かないセクションは丸ごと非表示にする (透明なマテリアルで描くより軽い)。
		ShowMaterialSection(Slot, Slot, bDraw, 0);
		if (!bDraw) continue;

		UMaterialInstanceDynamic* Mid = CreateDynamicMaterialInstance(Slot, Master);
		if (Mid == nullptr) continue;
		Mid->SetVectorParameterValue(TEXT("EdgeColor"), EdgeColor);
		Mid->SetScalarParameterValue(TEXT("EdgeSize"), EdgeSize);
		Mid->SetScalarParameterValue(TEXT("OutlineWidthScale"), OutlineWidthScale);
		Mid->SetScalarParameterValue(TEXT("UseOutline"), 1.0f);
	}
}
