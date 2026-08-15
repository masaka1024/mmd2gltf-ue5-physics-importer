// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdActorBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "MmdOutlineComponent.h"
#include "MmdPhysicsCoreLog.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "MmdActorBuilder"

namespace
{
	bool SaveAsset(UObject* Asset)
	{
		if (Asset == nullptr) return false;
		UPackage* Package = Asset->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		Args.Error = GWarn;
		return UPackage::SavePackage(Package, nullptr, *FileName, Args);
	}

	/** 輪郭線を描く材質が 1 つでもあるか (本体マテリアルの UseOutline を見る)。 */
	bool HasAnyOutline(USkeletalMesh* Mesh)
	{
		for (const FSkeletalMaterial& Slot : Mesh->GetMaterials())
		{
			if (Slot.MaterialInterface == nullptr) continue;
			float UseOutline = 0.0f;
			if (Slot.MaterialInterface->GetScalarParameterValue(
				FMaterialParameterInfo(TEXT("UseOutline")), UseOutline) && UseOutline > 0.5f)
			{
				return true;
			}
		}
		return false;
	}
}

FMmdActorResult FMmdActorBuilder::BuildActor(USkeletalMesh* Mesh)
{
	FMmdActorResult Result;

	if (Mesh == nullptr)
	{
		Result.Message = TEXT("スケルタルメッシュが指定されていません。");
		return Result;
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	const FString AssetName = FString::Printf(TEXT("BP_%s"), *Mesh->GetName());
	const FString FullPath = PackagePath / AssetName + TEXT(".") + AssetName;

	// ★既にあるものは中身を作り直す。作り直さないと、材質の変換をやり直したあとに
	//   古い構成が残る。名前を変えて増やすより、同じアセットを更新するほうが扱いやすい。
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *FullPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Blueprint == nullptr)
	{
		UPackage* Package = CreatePackage(*(PackagePath / AssetName));
		if (Package == nullptr)
		{
			Result.Message = FString::Printf(TEXT("パッケージを作れません: %s"), *FullPath);
			return Result;
		}
		Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(), Package, *AssetName, BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (Blueprint == nullptr)
		{
			Result.Message = TEXT("Blueprint を作れませんでした。");
			return Result;
		}
		FAssetRegistryModule::AssetCreated(Blueprint);
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (SCS == nullptr)
	{
		Result.Message = TEXT("Blueprint のコンポーネント構成を取得できません。");
		return Result;
	}

	// 既存のコンポーネントを一度落としてから組み直す。
	{
		TArray<USCS_Node*> Existing = SCS->GetAllNodes();
		for (USCS_Node* Node : Existing)
		{
			if (Node != nullptr) SCS->RemoveNodeAndPromoteChildren(Node);
		}
	}

	// --- 本体 ---
	USCS_Node* MeshNode = SCS->CreateNode(USkeletalMeshComponent::StaticClass(), TEXT("Mesh"));
	if (MeshNode == nullptr)
	{
		Result.Message = TEXT("スケルタルメッシュコンポーネントを作れません。");
		return Result;
	}
	if (auto* MeshTemplate = Cast<USkeletalMeshComponent>(MeshNode->ComponentTemplate))
	{
		MeshTemplate->SetSkeletalMeshAsset(Mesh);
		// 物理は【1】がメッシュ側に割り当てた Post-Process AnimBP で効くので、
		// ここでアニメーションの設定は触らない。
	}
	SCS->AddNode(MeshNode);   // 最初に足したものがルートになる

	// --- 輪郭線 ---
	// 輪郭線を描く材質が 1 つも無いモデルには付けない (何も描かないコンポーネントを
	// 置いても紛らわしいだけなので)。
	Result.bHasOutline = HasAnyOutline(Mesh);
	if (Result.bHasOutline)
	{
		USCS_Node* OutlineNode = SCS->CreateNode(UMmdOutlineComponent::StaticClass(), TEXT("Outline"));
		if (OutlineNode != nullptr)
		{
			// 本体の子にする。輪郭線コンポーネントは親を辿って追従先を決める。
			MeshNode->AddChildNode(OutlineNode);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAsset(Blueprint);

	Result.bSuccess = true;
	Result.Blueprint = Blueprint;
	Result.Message = FString::Printf(TEXT("アクターを生成しました: %s%s"),
		*FullPath, Result.bHasOutline ? TEXT(" (輪郭線あり)") : TEXT(" (輪郭線なし)"));
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] %s"), *Result.Message);
	return Result;
}

#undef LOCTEXT_NAMESPACE
