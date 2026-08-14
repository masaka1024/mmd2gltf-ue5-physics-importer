// Copyright (c) 2026 masaka1024. MIT License.
//
// 【2】マテリアル変換の検証。MMD_PARITY_GLB / MMD_CONV_SKELMESH が未設定ならスキップ。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceConstant.h"
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

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
