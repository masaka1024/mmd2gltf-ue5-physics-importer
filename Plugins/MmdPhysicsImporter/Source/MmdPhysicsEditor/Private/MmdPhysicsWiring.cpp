// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdPhysicsWiring.h"

#include "AnimGraphNode_MmdPhysics.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimationGraphSchema.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "FileHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdPhysicsCoreLog.h"
#include "MmdPhysicsDataAsset.h"
#include "Misc/FileHelper.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "MmdPhysicsWiring"

namespace
{
	/** AnimBlueprint 内の AnimGraph (スキーマが UAnimationGraphSchema のグラフ) を返す。 */
	UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBP)
	{
		TArray<UEdGraph*> Graphs;
		AnimBP->GetAllGraphs(Graphs);
		for (UEdGraph* G : Graphs)
		{
			if (G != nullptr && G->Schema != nullptr && G->Schema->IsChildOf(UAnimationGraphSchema::StaticClass()))
			{
				return G;
			}
		}
		return nullptr;
	}

	template <typename T>
	T* FindNode(UEdGraph* Graph)
	{
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (T* Typed = Cast<T>(N)) return Typed;
		}
		return nullptr;
	}

	UEdGraphPin* FindPin(UEdGraphNode* Node, const TCHAR* Name, EEdGraphPinDirection Dir)
	{
		if (Node == nullptr) return nullptr;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P->Direction == Dir && P->PinName == FName(Name)) return P;
		}
		return nullptr;
	}

	/** アセットのパッケージをディスクへ書き出す。 */
	bool SavePackageOf(UObject* Asset)
	{
		if (Asset == nullptr) return false;
		UPackage* Package = Asset->GetOutermost();
		if (Package == nullptr) return false;

		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		Args.Error = GWarn;

		const bool bSaved = UPackage::SavePackage(Package, nullptr, *FileName, Args);
		if (!bSaved)
		{
			UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] パッケージを保存できませんでした: %s"), *FileName);
		}
		return bSaved;
	}

	/** グラフへノードを追加して初期化する定型。 */
	template <typename T>
	T* SpawnNode(UEdGraph* Graph, int32 PosX, int32 PosY)
	{
		T* Node = NewObject<T>(Graph);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Graph->AddNode(Node, /*bFromUI=*/false, /*bSelectNewNode=*/false);
		return Node;
	}

	/**
	 * .glb から JSON チャンクだけを取り出して UMmdPhysicsData アセットへ格納する。
	 * 既にあれば中身を作り直す (名前を変えて増やさない)。失敗したら nullptr と理由を返す。
	 */
	UMmdPhysicsData* CreateOrUpdatePhysicsData(USkeletalMesh* Mesh, const FString& GlbPath,
		float UnitScale, FString& OutMessage)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *GlbPath))
		{
			OutMessage = FString::Printf(TEXT(".glb を読めません: %s"), *GlbPath);
			return nullptr;
		}

		TArray<uint8> Minimal;
		TArray<FString> Warnings;
		if (!MmdPhysics::GlbPhysicsReader::ExtractJsonChunkGlb(Bytes, Minimal, Warnings))
		{
			OutMessage = FString::Printf(TEXT("物理データを取り出せません: %s"),
				Warnings.Num() > 0 ? *Warnings[0] : TEXT("原因不明"));
			return nullptr;
		}

		// ★取り出した最小 GLB が本当に読めるか、ここで検証してから保存する。
		//   壊れたものを保存すると、失敗が実行時まで遅れてビルドでしか気付けなくなる。
		float CheckScale = 0.0f;
		TArray<FString> CheckWarnings;
		TSharedPtr<MmdPhysics::PmxPhysicsModel> Check =
			MmdPhysics::GlbPhysicsReader::LoadBytes(Minimal, CheckScale, CheckWarnings);
		if (!Check.IsValid())
		{
			OutMessage = TEXT("取り出した物理データを読み直せませんでした (GLB の構造が想定と違う可能性)。");
			return nullptr;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		const FString AssetName = FString::Printf(TEXT("%s_Physics"), *Mesh->GetName());
		const FString FullPath = PackagePath / AssetName + TEXT(".") + AssetName;

		UMmdPhysicsData* Data = LoadObject<UMmdPhysicsData>(nullptr, *FullPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Data == nullptr)
		{
			UPackage* Package = CreatePackage(*(PackagePath / AssetName));
			if (Package == nullptr)
			{
				OutMessage = FString::Printf(TEXT("パッケージを作れません: %s"), *FullPath);
				return nullptr;
			}
			Package->FullyLoad();
			Data = NewObject<UMmdPhysicsData>(Package, *AssetName, RF_Public | RF_Standalone);
			if (Data == nullptr)
			{
				OutMessage = FString::Printf(TEXT("物理データアセットを作れません: %s"), *FullPath);
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(Data);
		}

		Data->Modify();
		Data->Glb = MoveTemp(Minimal);
		Data->SourceGlbName = FPaths::GetCleanFilename(GlbPath);
		Data->UnitScale = CheckScale > 0.0f ? CheckScale : UnitScale;
		Data->NumRigidBodies = Check->RigidBodies.Num();
		Data->NumJoints = Check->Joints.Num();
		Data->MarkPackageDirty();

		UE_LOG(LogMmdPhysics, Log,
			TEXT("[MmdPhysics] 物理データを保存: %s (%d バイト / 元 .glb の %.1f%%、剛体%d ジョイント%d)"),
			*FullPath, Data->Glb.Num(),
			Bytes.Num() > 0 ? 100.0 * Data->Glb.Num() / Bytes.Num() : 0.0,
			Data->NumRigidBodies, Data->NumJoints);

		return Data;
	}
}

FMmdWireResult FMmdPhysicsWiring::WirePhysics(USkeletalMesh* Mesh, const FString& GlbPath, float UnitScale)
{
	FMmdWireResult Result;

	if (Mesh == nullptr)
	{
		Result.Message = TEXT("スケルタルメッシュが指定されていません。");
		return Result;
	}
	USkeleton* Skeleton = Mesh->GetSkeleton();
	if (Skeleton == nullptr)
	{
		Result.Message = TEXT("スケルタルメッシュにスケルトンがありません。");
		return Result;
	}
	if (GlbPath.IsEmpty() || !FPaths::FileExists(GlbPath))
	{
		Result.Message = FString::Printf(TEXT(".glb が見つかりません: %s"), *GlbPath);
		return Result;
	}

	// UnitScale は extras.mmd を正とする (呼び出し側が 0 以下を渡したとき、および食い違うとき)。
	{
		float FileUnitScale = 0.0f;
		TArray<FString> Warnings;
		TSharedPtr<MmdPhysics::PmxPhysicsModel> Probe = MmdPhysics::GlbPhysicsReader::LoadFile(GlbPath, FileUnitScale, Warnings);
		if (!Probe.IsValid())
		{
			Result.Message = FString::Printf(TEXT(".glb を読めません: %s"), *GlbPath);
			return Result;
		}
		if (Probe->RigidBodies.Num() == 0)
		{
			Result.Message = TEXT("この .glb には extras.mmd の剛体がありません。")
				TEXT("mmd2gltf-gui が出力したファイルを指定してください")
				TEXT("(一般の glTF エクスポータの出力では動作しません)。");
			return Result;
		}
		if (UnitScale <= 0.0f) UnitScale = FileUnitScale;
	}

	// --- Post-Process Anim Blueprint を用意する ---
	const FString MeshPackage = Mesh->GetOutermost()->GetName();          // 例 /Game/IA/IA
	const FString PackagePath = FPackageName::GetLongPackagePath(MeshPackage);
	const FString AssetName = FString::Printf(TEXT("ABP_%s_MmdPhysics"), *Mesh->GetName());
	const FString FullPath = PackagePath / AssetName;

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *(FullPath + TEXT(".") + AssetName));
	const bool bCreated = (AnimBP == nullptr);
	if (bCreated)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->TargetSkeleton = Skeleton;
		AnimBP = Cast<UAnimBlueprint>(AssetTools.CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory));
		if (AnimBP == nullptr)
		{
			Result.Message = FString::Printf(TEXT("Anim Blueprint を作成できませんでした: %s"), *FullPath);
			return Result;
		}
	}

	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (AnimGraph == nullptr)
	{
		Result.Message = TEXT("Anim Blueprint に AnimGraph が見つかりません。");
		return Result;
	}

	// --- ノードを用意する (再配線なら既存を使い回す) ---
	UAnimGraphNode_MmdPhysics* PhysNode = FindNode<UAnimGraphNode_MmdPhysics>(AnimGraph);
	UAnimGraphNode_Root* Root = FindNode<UAnimGraphNode_Root>(AnimGraph);
	if (Root == nullptr)
	{
		Result.Message = TEXT("AnimGraph に Output Pose ノードが見つかりません。");
		return Result;
	}

	if (PhysNode == nullptr)
	{
		PhysNode = SpawnNode<UAnimGraphNode_MmdPhysics>(AnimGraph, Root->NodePosX - 400, Root->NodePosY);

		// Post-Process AnimBP では、メインの AnimGraph が評価した姿勢が Input Pose から入ってくる。
		// これを繋がないと参照ポーズから始まってしまい、アニメーションが消える。
		UAnimGraphNode_LinkedInputPose* InputPose = FindNode<UAnimGraphNode_LinkedInputPose>(AnimGraph);
		if (InputPose == nullptr)
		{
			InputPose = SpawnNode<UAnimGraphNode_LinkedInputPose>(AnimGraph, Root->NodePosX - 800, Root->NodePosY);
		}

		const UEdGraphSchema* Schema = AnimGraph->GetSchema();

		// Input Pose (ローカル空間) → MMD Physics (コンポーネント空間)。
		// 空間が違うので、スキーマが変換ノードを自動で挟む。
		UEdGraphPin* InPosePin = FindPin(InputPose, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* PhysInPin = FindPin(PhysNode, TEXT("ComponentPose"), EGPD_Input);
		if (InPosePin != nullptr && PhysInPin != nullptr)
		{
			Schema->TryCreateConnection(InPosePin, PhysInPin);
		}

		// MMD Physics (コンポーネント空間) → Output Pose (ローカル空間)。こちらも自動変換。
		UEdGraphPin* PhysOutPin = FindPin(PhysNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* RootInPin = FindPin(Root, TEXT("Result"), EGPD_Input);
		if (PhysOutPin != nullptr && RootInPin != nullptr)
		{
			Schema->TryCreateConnection(PhysOutPin, RootInPin);
		}

		// ★繋がったことを必ず確かめる。ピンが見つからないときも TryCreateConnection が
		//   false を返したときも、ここまでは黙って素通りしてしまう。繋がっていない
		//   AnimGraph は **コンパイルだけ通る** ので、「物理もアニメーションも効かない
		//   Post-Process AnimBP ができあがり、エラーも警告も出ない」という壊れ方をする。
		//   ★空間が違うぶんスキーマが変換ノードを挟むので、相手ピンと直結しているかではなく
		//     **リンクが 1 本でも張られたか** で判定する (直結を期待すると誤検知する)。
		if (PhysInPin == nullptr || PhysInPin->LinkedTo.Num() == 0
			|| RootInPin == nullptr || RootInPin->LinkedTo.Num() == 0)
		{
			Result.Message = TEXT("AnimGraph のノードを繋げませんでした ")
				TEXT("(Input Pose → MMD Physics → Output Pose)。")
				TEXT("UE のバージョン差でピン名が変わった可能性があります。");
			return Result;
		}
	}

	// --- 物理データをアセット化する ---
	// ★.glb は UAsset ではないのでクックされず、GlbPath の絶対パスは配布先に存在しない。
	//   アセットにしておけば Anim Blueprint から参照されてビルドへ同梱される。
	//   これを怠ると「エディタでは動くのにパッケージしたビルドでは無言で物理が効かない」
	//   という壊れ方をする (移植元の Unity 版が APK で踏んだ。詳細は MmdPhysicsDataAsset.h)。
	UMmdPhysicsData* DataAsset = CreateOrUpdatePhysicsData(Mesh, GlbPath, UnitScale, Result.Message);
	if (DataAsset == nullptr)
	{
		// Message には理由が入っている。配線そのものは続けず、原因を返す。
		return Result;
	}

	// --- 設定を書き込む ---
	PhysNode->Node.PhysicsData = DataAsset;
	PhysNode->Node.GlbPath = GlbPath;   // エディタでの確認用に残す (ビルドでは使えない)
	PhysNode->Node.UnitScale = UnitScale;
	PhysNode->Modify();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	FKismetEditorUtilities::CompileBlueprint(AnimBP);

	if (AnimBP->GeneratedClass == nullptr)
	{
		Result.Message = TEXT("Anim Blueprint のコンパイルに失敗しました。");
		return Result;
	}

	// --- スケルタルメッシュへ割り当てる ---
	Mesh->Modify();
	Mesh->SetPostProcessAnimBlueprint(TSubclassOf<UAnimInstance>(AnimBP->GeneratedClass.Get()));
	Mesh->MarkPackageDirty();
	AnimBP->MarkPackageDirty();

	// ★保存まで行う。AssetTools::CreateAsset で作った Anim Blueprint はメモリ上にあるだけなので、
	//   ここで保存しないとエディタを閉じた時点で消え、スケルタルメッシュ側の参照だけが残る。
	//   FEditorFileUtils::PromptForCheckoutAndSave は unattended 実行では何も保存しないため、
	//   パッケージを直接書き出す。
	//   ★物理データアセットも必ず保存すること。保存し忘れると、Anim Blueprint だけが
	//     ディスクに残って参照先が消え、次にプロジェクトを開いたとき物理が無効になる。
	SavePackageOf(DataAsset);
	SavePackageOf(AnimBP);
	SavePackageOf(Mesh);

	Result.bSuccess = true;
	Result.AnimBlueprint = AnimBP;
	Result.Message = FString::Printf(
		TEXT("%s: %s を %s の Post-Process Anim Blueprint に設定しました (UnitScale=%g)。"),
		bCreated ? TEXT("作成") : TEXT("再配線"), *AssetName, *Mesh->GetName(), UnitScale);
	UE_LOG(LogMmdPhysics, Log, TEXT("[MmdPhysics] %s"), *Result.Message);
	return Result;
}

FMmdWireResult FMmdPhysicsWiring::UnwirePhysics(USkeletalMesh* Mesh)
{
	FMmdWireResult Result;
	if (Mesh == nullptr)
	{
		Result.Message = TEXT("スケルタルメッシュが指定されていません。");
		return Result;
	}
	Mesh->Modify();
	Mesh->SetPostProcessAnimBlueprint(TSubclassOf<UAnimInstance>(nullptr));
	Mesh->MarkPackageDirty();
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("%s の Post-Process Anim Blueprint を外しました。"), *Mesh->GetName());
	return Result;
}

#undef LOCTEXT_NAMESPACE
