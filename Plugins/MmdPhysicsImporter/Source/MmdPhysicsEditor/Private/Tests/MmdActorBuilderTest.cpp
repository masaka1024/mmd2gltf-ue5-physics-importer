// Copyright (c) 2026 masaka1024. MIT License.
//
// 【3】アクター生成の検証。MMD_CONV_SKELMESH が未設定ならスキップ。
//
// 見ているのは「組み上がった Blueprint に本体と輪郭線が入っているか」。
// 輪郭線を描く材質があるかどうかは【2】の結果 (マテリアルの UseOutline) から決まるので、
// このテストは【2】まで済んだプロジェクトで走らせる前提。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "MmdActorBuilder.h"
#include "MmdOutlineComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdActorBuilderTest, "MmdPhysics.Editor.BuildActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdActorBuilderTest::RunTest(const FString& Parameters)
{
	const FString MeshPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_CONV_SKELMESH"));
	if (MeshPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_CONV_SKELMESH が未設定のためスキップ。"));
		return true;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (Mesh == nullptr)
	{
		AddError(FString::Printf(TEXT("スケルタルメッシュを読めない: %s"), *MeshPath));
		return false;
	}

	const FMmdActorResult R = FMmdActorBuilder::BuildActor(Mesh);
	AddInfo(R.Message);
	if (!TestTrue(TEXT("アクターを生成できる"), R.bSuccess)) return false;
	if (!TestNotNull(TEXT("Blueprint が返る"), R.Blueprint)) return false;

	USimpleConstructionScript* SCS = R.Blueprint->SimpleConstructionScript;
	if (!TestNotNull(TEXT("コンポーネント構成がある"), SCS)) return false;

	// --- 本体 ---
	USkeletalMeshComponent* MeshTemplate = nullptr;
	UMmdOutlineComponent* OutlineTemplate = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node == nullptr) continue;
		if (auto* Outline = Cast<UMmdOutlineComponent>(Node->ComponentTemplate))
		{
			OutlineTemplate = Outline;
		}
		else if (auto* SkelMesh = Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
		{
			MeshTemplate = SkelMesh;
		}
	}

	if (TestNotNull(TEXT("スケルタルメッシュコンポーネントがある"), MeshTemplate))
	{
		TestEqual(TEXT("対象のメッシュが割り当たっている"),
			MeshTemplate->GetSkeletalMeshAsset(), Mesh);
	}

	// --- 輪郭線 ---
	// 付くかどうかは材質側の UseOutline 次第。戻り値と実物が一致していることを見る。
	int32 WithOutline = 0;
	for (const FSkeletalMaterial& Slot : Mesh->GetMaterials())
	{
		if (Slot.MaterialInterface == nullptr) continue;
		float UseOutline = 0.0f;
		Slot.MaterialInterface->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("UseOutline")), UseOutline);
		if (UseOutline > 0.5f) WithOutline++;
	}
	AddInfo(FString::Printf(TEXT("輪郭線を描く材質: %d 件"), WithOutline));

	TestEqual(TEXT("輪郭線の有無が材質の状態と一致する"), R.bHasOutline, WithOutline > 0);
	if (WithOutline > 0)
	{
		TestNotNull(TEXT("輪郭線コンポーネントが入っている"), OutlineTemplate);
	}
	else
	{
		TestNull(TEXT("輪郭線が無いモデルにはコンポーネントを付けない"), OutlineTemplate);
		AddWarning(TEXT("このモデルには輪郭線を描く材質がありません。")
			TEXT("【2】マテリアル変換を先に実行していない可能性があります。"));
	}

	// 作り直しても増殖しないこと (同じアセットを更新する)。
	const FMmdActorResult Again = FMmdActorBuilder::BuildActor(Mesh);
	TestTrue(TEXT("2 回目も成功する"), Again.bSuccess);
	TestEqual(TEXT("同じ Blueprint を更新する (増殖しない)"), Again.Blueprint, R.Blueprint);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
