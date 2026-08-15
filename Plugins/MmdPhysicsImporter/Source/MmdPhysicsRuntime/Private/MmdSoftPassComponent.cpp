// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdSoftPassComponent.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/** 本体のマテリアルから 2 パス目へ写す値。同じ絵を重ねるので見た目に関わる全部が要る。 */
	const TCHAR* KScalarParams[] = {
		TEXT("UseToon"), TEXT("SphereMulWeight"), TEXT("SphereAddWeight"),
	};
	const TCHAR* KTextureParams[] = {
		TEXT("BaseColorTex"), TEXT("ToonTex"), TEXT("SphereTex"),
	};
	const TCHAR* KVectorParams[] = {
		TEXT("BaseColor"), TEXT("LightDir"),
	};
}

void UMmdSoftPassComponent::RebuildMaterials()
{
	USkeletalMeshComponent* Leader = FindLeader();
	USkeletalMesh* Mesh = GetSkeletalMeshAsset();
	if (Leader == nullptr || Mesh == nullptr) return;

	UMaterialInterface* Master = SoftPassMaterial;
	if (Master == nullptr)
	{
		Master = FindMasterMaterial(TEXT("M_MmdToonTranslucent"));
	}
	if (Master == nullptr)
	{
		// 半透明マスターが無い = 半透明の材質が 1 つも無いモデル。何もしない。
		return;
	}

	// 本体 (不透明パス) の後、輪郭線より後に描く。
	SetTranslucentSortPriority(0);

	const int32 NumSlots = Mesh->GetMaterials().Num();
	for (int32 Slot = 0; Slot < NumSlots; Slot++)
	{
		UMaterialInterface* Body = Leader->GetMaterial(Slot);

		// 2 パス目が要る材質かどうかは、変換が本体側へ入れた印で判断する。
		float SoftPass = 0.0f;
		if (Body != nullptr)
		{
			Body->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SoftPass")), SoftPass);
		}

		const bool bDraw = bDrawSoftPass && SoftPass > 0.5f;
		// 描かないセクションは丸ごと非表示にする (透明なマテリアルで描くより軽い)。
		ShowMaterialSection(Slot, Slot, bDraw, 0);
		if (!bDraw || Body == nullptr) continue;

		UMaterialInstanceDynamic* Mid = CreateDynamicMaterialInstance(Slot, Master);
		if (Mid == nullptr) continue;

		// 本体と同じ絵になるよう、見た目に関わるパラメータを丸ごと写す。
		for (const TCHAR* Name : KScalarParams)
		{
			float Value = 0.0f;
			if (Body->GetScalarParameterValue(FMaterialParameterInfo(Name), Value))
			{
				Mid->SetScalarParameterValue(Name, Value);
			}
		}
		for (const TCHAR* Name : KVectorParams)
		{
			FLinearColor Value = FLinearColor::White;
			if (Body->GetVectorParameterValue(FMaterialParameterInfo(Name), Value))
			{
				Mid->SetVectorParameterValue(Name, Value);
			}
		}
		for (const TCHAR* Name : KTextureParams)
		{
			UTexture* Value = nullptr;
			if (Body->GetTextureParameterValue(FMaterialParameterInfo(Name), Value) && Value != nullptr)
			{
				Mid->SetTextureParameterValue(Name, Value);
			}
		}

		// ★2 パス目は素のα。不透明化 (SubpassWeight) は 1 パス目の役目なので効かせない。
		Mid->SetScalarParameterValue(TEXT("SubpassWeight"), 0.0f);
	}
}
