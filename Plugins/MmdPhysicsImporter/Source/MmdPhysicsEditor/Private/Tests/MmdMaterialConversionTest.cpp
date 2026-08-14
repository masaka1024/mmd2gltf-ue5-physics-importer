// Copyright (c) 2026 masaka1024. MIT License.
//
// 【2】マテリアル変換の検証。MMD_PARITY_GLB / MMD_CONV_SKELMESH が未設定ならスキップ。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MmdGlbMaterialReader.h"
#include "MmdMaterialConversion.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdMaterialConversionTest, "MmdPhysics.Editor.ConvertMaterials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdMaterialConversionTest::RunTest(const FString& Parameters)
{
	const FString GlbPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB"));
	const FString MeshPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_CONV_SKELMESH"));
	if (GlbPath.IsEmpty() || MeshPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_PARITY_GLB / MMD_CONV_SKELMESH が未設定のためスキップ。"));
		return true;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (Mesh == nullptr)
	{
		AddError(FString::Printf(TEXT("スケルタルメッシュを読めない: %s"), *MeshPath));
		return false;
	}

	const FMmdMaterialResult R = FMmdMaterialConversion::ConvertMaterials(Mesh, GlbPath);
	AddInfo(R.Message);

	if (!TestTrue(TEXT("マテリアル変換が成功する"), R.bSuccess)) return false;
	TestEqual(TEXT("全スロットが変換された"), R.Converted, R.Total);

	// 実際に MI が割り当たっていて、パラメータが入っているかを見る。
	int32 WithToon = 0, WithSphere = 0, MiCount = 0;
	for (const FSkeletalMaterial& Slot : Mesh->GetMaterials())
	{
		UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Slot.MaterialInterface);
		if (MI == nullptr) continue;
		MiCount++;

		float UseToon = 0.0f, MulW = 0.0f, AddW = 0.0f;
		MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("UseToon")), UseToon);
		MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SphereMulWeight")), MulW);
		MI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SphereAddWeight")), AddW);
		if (UseToon > 0.5f) WithToon++;
		if (MulW > 0.5f || AddW > 0.5f) WithSphere++;
	}

	AddInfo(FString::Printf(TEXT("マテリアルインスタンス %d / トゥーン有効 %d / スフィア有効 %d"),
		MiCount, WithToon, WithSphere));

	TestEqual(TEXT("全スロットにマテリアルインスタンスが割り当たっている"), MiCount, Mesh->GetMaterials().Num());

	// このモデルはスフィアが 14 材質にあるので、少なくとも数件は有効になるはず。
	// (トゥーンは共有トゥーン画像が無いと 0 件になり得るので、ここでは必須にしない)
	TestTrue(FString::Printf(TEXT("スフィアマップが適用された材質がある (%d)"), WithSphere), WithSphere > 0);

	if (WithToon == 0)
	{
		AddWarning(TEXT("トゥーンが 1 件も有効になっていません。")
			TEXT("共有トゥーン (toon01..toon10) はモデルに同梱されないため、")
			TEXT("プロジェクトへ別途取り込む必要があります。"));
	}

	// --- alphaMode=BLEND が Translucent マスターへ繋がっているか ---
	// .glb 側の alphaMode を正として、スロット名で突き合わせる。
	MmdPhysics::MmdMaterialSet Set;
	TArray<FString> Warnings;
	if (!MmdPhysics::GlbMaterialReader::LoadFile(GlbPath, Set, Warnings))
	{
		AddError(TEXT(".glb からマテリアル情報を読めない (alphaMode の照合をスキップ)。"));
		return false;
	}

	int32 ExpectedBlend = 0, ActualTranslucent = 0, Mismatched = 0;
	const TArray<FSkeletalMaterial>& CheckSlots = Mesh->GetMaterials();
	for (int32 i = 0; i < CheckSlots.Num(); i++)
	{
		const FString SlotName = CheckSlots[i].MaterialSlotName.ToString();
		const MmdPhysics::MmdMaterialInfo* Info = Set.Materials.FindByPredicate(
			[&SlotName](const MmdPhysics::MmdMaterialInfo& M) { return M.Name == SlotName; });
		if (Info == nullptr && Set.Materials.IsValidIndex(i)) Info = &Set.Materials[i];
		if (Info == nullptr) continue;

		UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(CheckSlots[i].MaterialInterface);
		if (MI == nullptr) continue;

		const UMaterial* Parent = MI->GetMaterial();
		const bool bWantBlend = Info->AlphaMode.Equals(TEXT("BLEND"), ESearchCase::IgnoreCase);
		// GetBlendMode() はインスタンスの BasePropertyOverrides まで解決した実効値を返す。
		const bool bIsTranslucent = (MI->GetBlendMode() == BLEND_Translucent);

		if (bWantBlend) ExpectedBlend++;
		if (bIsTranslucent) ActualTranslucent++;
		if (bWantBlend != bIsTranslucent)
		{
			Mismatched++;
			AddError(FString::Printf(
				TEXT("スロット '%s': alphaMode=%s なのに親は %s (Translucent=%s)"),
				*SlotName, *Info->AlphaMode,
				Parent ? *Parent->GetName() : TEXT("null"),
				bIsTranslucent ? TEXT("true") : TEXT("false")));
		}
	}

	AddInfo(FString::Printf(TEXT("alphaMode=BLEND %d 件 / Translucent 親 %d 件"),
		ExpectedBlend, ActualTranslucent));
	TestEqual(TEXT("alphaMode=BLEND の材質だけが Translucent マスターへ繋がっている"), Mismatched, 0);
	TestEqual(TEXT("戻り値の Translucent 件数が実際と一致する"), R.Translucent, ActualTranslucent);

	if (ExpectedBlend == 0)
	{
		AddWarning(TEXT("このモデルには alphaMode=BLEND の材質がありません。")
			TEXT("半透明の経路は実質未検証です。"));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
