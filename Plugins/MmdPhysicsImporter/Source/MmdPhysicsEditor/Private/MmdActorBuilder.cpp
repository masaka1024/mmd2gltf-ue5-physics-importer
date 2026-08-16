// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdActorBuilder.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
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
#include "MmdMorphAnimation.h"
#include "MmdOutlineComponent.h"
#include "MmdPhysicsCoreLog.h"
#include "MmdSoftPassComponent.h"
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

	/** 指定のスカラーパラメータが立っている材質が 1 つでもあるか。 */
	bool AnyMaterialHasFlag(USkeletalMesh* Mesh, const TCHAR* ParameterName)
	{
		for (const FSkeletalMaterial& Slot : Mesh->GetMaterials())
		{
			if (Slot.MaterialInterface == nullptr) continue;
			float Value = 0.0f;
			if (Slot.MaterialInterface->GetScalarParameterValue(
				FMaterialParameterInfo(ParameterName), Value) && Value > 0.5f)
			{
				return true;
			}
		}
		return false;
	}
}

UAnimSequence* FMmdActorBuilder::FindAnimationFor(USkeletalMesh* Mesh, int32& OutCandidates)
{
	OutCandidates = 0;
	if (Mesh == nullptr) return nullptr;

	USkeleton* Skeleton = Mesh->GetSkeleton();
	if (Skeleton == nullptr) return nullptr;

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	// ★スケルトンの判定はアセットレジストリのタグで行い、アセットは読み込まない。
	//   UAnimationAsset::Skeleton は AssetRegistrySearchable なので、値は
	//   "/Script/Engine.Skeleton'/Game/IA/IA_Skeleton.IA_Skeleton'" の形で入っている。
	//   ここで全部 GetAsset() すると、プロジェクト内の全モーションを展開することになる
	//   (実測: IA のモーション 1 本で圧縮データ 72MB)。
	const FString SkeletonTag = FAssetData(Skeleton).GetExportTextName();
	const FString MeshFolder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	const FString MeshName = Mesh->GetName();

	FAssetData Best;
	int32 BestScore = -1;
	for (const FAssetData& A : Assets)
	{
		FString Value;
		if (!A.GetTagValue(TEXT("Skeleton"), Value) || Value != SkeletonTag) continue;
		OutCandidates++;

		// 同じフォルダ (+2) > 名前がメッシュ名で始まる (+1)。
		// Interchange は "<メッシュ名>_Anim" をメッシュの隣に置くので、通常は 3 点で一意に決まる。
		int32 Score = 0;
		if (A.PackagePath.ToString() == MeshFolder) Score += 2;
		if (A.AssetName.ToString().StartsWith(MeshName)) Score += 1;

		// 同点は名前順で決める (レジストリの列挙順に依存させないため)。
		if (Score > BestScore
			|| (Score == BestScore && A.AssetName.ToString() < Best.AssetName.ToString()))
		{
			BestScore = Score;
			Best = A;
		}
	}

	return Best.IsValid() ? Cast<UAnimSequence>(Best.GetAsset()) : nullptr;
}

FMmdActorResult FMmdActorBuilder::BuildActor(USkeletalMesh* Mesh, const FString& GlbPath)
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

		// --- アニメーション (VMD 由来のモーション) ---
		// ★mmd2gltf-gui は VMD を glTF 標準のアニメーションとして焼き込む (IK 解決済み・30fps)。
		//   UE 標準の Interchange がそれを AnimSequence として取り込んでいるので、
		//   ここで割り当てるだけで動く。これをしないと、置いたアクターが
		//   参照ポーズのまま立っているだけになる。
		//
		// ★物理との共存: Post-Process AnimBP は再生モードに関わらず後段で必ず走る。
		//   単発再生 (AnimationSingleNode) にしても【1】の物理はそのまま効き、
		//   ボーン追従の剛体には書き戻さないので体のモーションも保たれる。
		//
		// ★bForceInitAnimScriptInstance=false にすること。ここで触っているのは
		//   ワールドに登録されていない **コンポーネントテンプレート**で、
		//   アニメーションインスタンスを作らせる場面ではない。
		Result.Animation = FindAnimationFor(Mesh, Result.AnimationCandidates);
		if (Result.Animation != nullptr)
		{
			MeshTemplate->SetAnimationMode(EAnimationMode::AnimationSingleNode,
				/*bForceInitAnimScriptInstance=*/false);
			MeshTemplate->AnimationData.AnimToPlay = Result.Animation;
			MeshTemplate->AnimationData.bSavedLooping = true;
			MeshTemplate->AnimationData.bSavedPlaying = true;

			UE_LOG(LogMmdPhysics, Log,
				TEXT("[MmdPhysics] アニメーション '%s' を割り当てました (同じスケルトンの候補 %d 本)。"),
				*Result.Animation->GetName(), Result.AnimationCandidates);

			// ★表情モーフのアニメーションを補う。
			//   UE 5.5 の Interchange は glTF の weights チャンネルを取りこぼすので、
			//   .glb を渡してもらえるならここで直接読んでカーブを足す
			//   (詳しくは MmdMorphAnimation.h)。
			if (!GlbPath.IsEmpty())
			{
				const FMmdMorphAnimResult Morph =
					FMmdMorphAnimation::ApplyMorphCurves(Mesh, Result.Animation, GlbPath);
				Result.MorphCurvesAdded = Morph.CurvesAdded;
				if (!Morph.bSuccess)
				{
					// 致命ではない。ボーンのモーションはそのまま動く。
					UE_LOG(LogMmdPhysics, Warning,
						TEXT("[MmdPhysics] 表情モーフのアニメーションを補えませんでした: %s"), *Morph.Message);
				}
				else
				{
					if (Morph.CurvesAdded > 0)
					{
						SaveAsset(Result.Animation);
					}
					// ★スケルトンも保存する。「このカーブはモーフを動かす」という登録は
					//   スケルトン側にあり、アニメーションを保存しても付いてこない。
					//   保存しないとエディタを開き直した時点で登録が消え、カーブはあるのに
					//   顔だけ動かない状態になる (詳しくは MmdMorphAnimation.h の注記)。
					if (Morph.MorphMetaDataSet > 0)
					{
						SaveAsset(Result.Animation->GetSkeleton());
					}
				}
			}
			if (Result.AnimationCandidates > 1)
			{
				// 1 本しか割り当てられないので、選んだことを明示する。
				UE_LOG(LogMmdPhysics, Log,
					TEXT("[MmdPhysics] 候補が複数あるため 1 本を選びました。")
					TEXT("別のモーションにしたい場合は BP_%s の Mesh コンポーネントの ")
					TEXT("Anim to Play を差し替えてください。"), *Mesh->GetName());
			}
		}
		else
		{
			// モーション無しの .glb (モデルだけの変換) はこちら。異常ではない。
			UE_LOG(LogMmdPhysics, Log,
				TEXT("[MmdPhysics] このスケルトンで再生できる AnimSequence が見つからないため、")
				TEXT("アニメーションは割り当てませんでした (モデルだけの .glb なら正常です)。"));
		}
	}
	SCS->AddNode(MeshNode);   // 最初に足したものがルートになる

	// --- 半透明の 2 パス目 (柔らかい毛先) ---
	// 本体は 1 パス目 (Masked) を描いている。lilToon の TwoPass を成立させるには
	// もう一度 Translucent で描く必要があるので、そのコンポーネントを足す。
	Result.bHasSoftPass = AnyMaterialHasFlag(Mesh, TEXT("SoftPass"));
	if (Result.bHasSoftPass)
	{
		USCS_Node* SoftPassNode = SCS->CreateNode(UMmdSoftPassComponent::StaticClass(), TEXT("SoftPass"));
		if (SoftPassNode != nullptr)
		{
			MeshNode->AddChildNode(SoftPassNode);
		}
	}

	// --- 輪郭線 ---
	// 輪郭線を描く材質が 1 つも無いモデルには付けない (何も描かないコンポーネントを
	// 置いても紛らわしいだけなので)。
	Result.bHasOutline = AnyMaterialHasFlag(Mesh, TEXT("UseOutline"));
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
	Result.Message = FString::Printf(
		TEXT("アクターを生成しました: %s (輪郭線 %s / 毛先パス %s / アニメーション %s / 表情モーフ %d 本)"),
		*FullPath,
		Result.bHasOutline ? TEXT("あり") : TEXT("なし"),
		Result.bHasSoftPass ? TEXT("あり") : TEXT("なし"),
		Result.Animation != nullptr ? *Result.Animation->GetName() : TEXT("なし"),
		Result.MorphCurvesAdded);
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] %s"), *Result.Message);
	return Result;
}

#undef LOCTEXT_NAMESPACE
