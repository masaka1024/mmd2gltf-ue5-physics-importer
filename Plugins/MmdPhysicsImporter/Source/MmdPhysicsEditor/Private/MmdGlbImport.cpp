// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdGlbImport.h"

#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Animation/Skeleton.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "MmdActorBuilder.h"
#include "MmdGlbNameNormalize.h"
#include "MmdMaterialConversion.h"
#include "MmdNameNormalize.h"
#include "MmdPhysicsCoreLog.h"
#include "MmdPhysicsWiring.h"
#include "MmdScopedUtf8CType.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"

FString FMmdGlbImport::DestinationFolderFor(const FString& GlbPath)
{
	return FString(TEXT("/Game/")) + FPaths::GetBaseFilename(GlbPath);
}

FMmdImportPrecheck FMmdGlbImport::Precheck(const FString& GlbPath)
{
	return PrecheckFolder(DestinationFolderFor(GlbPath));
}

FMmdImportPrecheck FMmdGlbImport::PrecheckFolder(const FString& Folder)
{
	FMmdImportPrecheck Check;
	Check.Folder = Folder;

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	// 起動直後やコマンドラインからだと走査が終わっていないことがあるので、このフォルダだけ同期で走査する。
	Registry.ScanPathsSynchronous({ Folder }, /*bForceRescan=*/true);

	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*Folder), Assets, /*bRecursive=*/true);
	if (Assets.Num() == 0)
	{
		Check.Message = FString::Printf(TEXT("取り込み先 %s に既存アセットはありません。"), *Folder);
		return Check;
	}

	for (const FAssetData& A : Assets)
	{
		Check.ExistingAssets.Add(A.GetObjectPathString());
	}
	Check.ExistingAssets.Sort();

	// ★旧形式 (全角数字のボーン名) のスケルトンは上書きでは直らない。Interchange は既存の
	//   スケルトンへボーンを足す方向で取り込むので、全角と半角の指ボーンが混在しかねない。
	//   削除してから取り込み直してもらう。
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != USkeleton::StaticClass()->GetClassPathName()) continue;
		const USkeleton* Skeleton = Cast<USkeleton>(A.GetAsset());
		if (Skeleton == nullptr) continue;

		const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
		for (int32 b = 0; b < Ref.GetNum(); ++b)
		{
			const FString Bone = Ref.GetBoneName(b).ToString();
			if (MmdPhysics::NameNormalize::HasFullWidthDigit(Bone))
			{
				if (Check.LegacyBones.Num() < 5) Check.LegacyBones.Add(Bone);
				Check.LegacySkeleton = A.GetObjectPathString();
			}
		}
		if (!Check.LegacySkeleton.IsEmpty()) break;
	}

	if (!Check.LegacySkeleton.IsEmpty())
	{
		Check.State = FMmdImportPrecheck::EState::Legacy;
		Check.Message = FString::Printf(
			TEXT("旧形式のアセットです。削除してから取り込み直してください。")
			TEXT("(%s のボーン名に全角数字があります: %s …。取り込み先: %s)"),
			*Check.LegacySkeleton, *FString::Join(Check.LegacyBones, TEXT(", ")), *Folder);
		return Check;
	}

	Check.State = FMmdImportPrecheck::EState::Existing;
	Check.Message = FString::Printf(TEXT("取り込み先 %s に既存アセットが %d 件あります (上書きされます)。"),
		*Folder, Check.ExistingAssets.Num());
	return Check;
}

FMmdGlbImportResult FMmdGlbImport::Import(const FString& GlbPath, bool bAllowOverwrite)
{
	FMmdGlbImportResult Result;
	const FString SourcePath = FPaths::ConvertRelativePathToFull(GlbPath);
	Result.SourcePath = SourcePath;

	if (!FPaths::FileExists(SourcePath))
	{
		Result.Failure = FMmdGlbImportResult::EFailure::NotFound;
		Result.Message = FString::Printf(TEXT(".glb が見つかりません: %s"), *SourcePath);
		return Result;
	}

	// --- 取り込み先の事前確認 (取り込みは確認なしで置き換えるので、ここで止める) ---
	Result.Precheck = Precheck(SourcePath);
	if (Result.Precheck.State == FMmdImportPrecheck::EState::Legacy)
	{
		Result.Failure = FMmdGlbImportResult::EFailure::LegacyAssets;
		Result.Message = Result.Precheck.Message;
		return Result;
	}
	if (Result.Precheck.State == FMmdImportPrecheck::EState::Existing && !bAllowOverwrite)
	{
		Result.Failure = FMmdGlbImportResult::EFailure::ExistingAssets;
		Result.Message = Result.Precheck.Message + TEXT(" 上書きの許可が無いため中止しました。");
		return Result;
	}

	// --- 全角数字を半角へ直した複製を作る (原本は 1 バイトも触らない) ---
	const FString NormalizedPath = FMmdGlbNameNormalize::MakeNormalizedPath(SourcePath);
	const FMmdGlbNormalizeResult Norm = FMmdGlbNameNormalize::NormalizeFile(SourcePath, NormalizedPath);
	Result.NormalizeMessage = Norm.Message;
	Result.Collisions = Norm.Collisions;
	if (!Norm.bSuccess)
	{
		// 読めない・書けない GLB。名前の衝突では失敗しない (振り分けて続ける)。
		Result.Failure = FMmdGlbImportResult::EFailure::Normalize;
		Result.Message = Norm.Message;
		return Result;
	}

	// 直すところが無ければ原本をそのまま取り込む (中間物を増やさない)。
	const FString ImportFrom = Norm.IsNoOp() ? SourcePath : NormalizedPath;

	// ★取り込み先は /Game (Content 直下へ D&D したのと同じ)。
	//   UE 5.8 の glTF 既定パイプライン (DefaultGLTFAssetsPipeline) はシーン名と
	//   アセット種別のサブフォルダを自分で作るので、/Game/<名前>/SkeletalMeshes/<名前> になる。
	//   ここで /Game/<名前> を渡すと /Game/<名前>/<名前>/… と 1 段深くなる。
	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = ImportFrom;
	Task->DestinationPath = TEXT("/Game");
	Task->bAutomated = true;        // 取り込みダイアログを出さない
	Task->bReplaceExisting = true;
	Task->bAsync = false;           // GetObjects() で待てるが、ここは同期でよい
	Task->bSave = false;

	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	{
		// ★Interchange が AnimSequence を作る途中で Control Rig の SanitizeName
		//   (FChar::IsAlpha = iswalpha) を通る。C ロケールのままだと日本語の
		//   ボーン名・モーフ名が '_' に潰れてトラックが捨てられるので、
		//   取り込みの間だけこのスレッドの LC_CTYPE を UTF-8 にする。
		//   Mac のエディタは起動時に上げてあるので、ここは Windows 向けの保険。
		//   (MmdPhysicsEditorModule.cpp の【日本語名と LC_CTYPE】)
		FMmdScopedUtf8CType Utf8CType;
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
	}

	for (UObject* Obj : Task->GetObjects())
	{
		if (Obj == nullptr) continue;
		Result.ImportedPackages.AddUnique(Obj->GetOutermost()->GetName());
		if (Result.Mesh == nullptr)
		{
			Result.Mesh = Cast<USkeletalMesh>(Obj);
		}
	}
	if (Result.Mesh == nullptr)
	{
		Result.Failure = FMmdGlbImportResult::EFailure::NoMesh;
		Result.Message = TEXT("取り込みでスケルタルメッシュができませんでした。出力ログを確認してください。");
		return Result;
	}

	// ★インポート元は**原本**へ差し戻す。複製は Saved/ の中間物でいつ消えてもよく、
	//   由来としても原本を指しておく方が正しい。物理もモーフも全角/半角のどちらでも
	//   名前を引けるようにしてあるので、原本を指していて構わない
	//   (AnimNode_MmdPhysics の FindBoneIndexForPmxName と
	//    MmdMorphAnimation の ApplyMorphCurves を参照)。
	if (!Norm.IsNoOp())
	{
		if (UAssetImportData* ImportData = Result.Mesh->GetAssetImportData())
		{
			ImportData->Update(SourcePath);
			Result.Mesh->MarkPackageDirty();
		}
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("取り込み: %s  /  %s"), *Result.Mesh->GetName(), *Norm.Message);
	return Result;
}

FMmdImportPipelineResult FMmdGlbImport::RunPipeline(const FString& GlbPath, bool bForce)
{
	FMmdImportPipelineResult Out;

	// ★保存するのは**このパイプラインが作成・変更したパッケージだけ**。
	//   開始前から未保存だったパッケージ (利用者の編集中のアセット) は、最後の一括保存から外す。
	//   各段 (配線・マテリアル・アクター) が自分で保存するものは、保存イベントで拾ってログに出す。
	TSet<FName> DirtyBefore;
	{
		TArray<UPackage*> Dirty;
		FEditorFileUtils::GetDirtyContentPackages(Dirty);
		for (const UPackage* P : Dirty) DirtyBefore.Add(P->GetFName());
	}

	TArray<FString> Saved;
	const FDelegateHandle SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
		[&Saved](const FString&, UPackage* Package, FObjectPostSaveContext)
		{
			if (Package != nullptr) Saved.AddUnique(Package->GetName());
		});
	ON_SCOPE_EXIT { UPackage::PackageSavedWithContextEvent.Remove(SavedHandle); };

	auto Finish = [&Out, &Saved](bool bSuccess, const FString& Message)
	{
		Out.bSuccess = bSuccess;
		Out.Message = Message;
		Saved.Sort();
		Out.SavedPackages = Saved;
		return Out;
	};

	const FMmdGlbImportResult Imported = Import(GlbPath, bForce);
	UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 【0】%s"), *Imported.Message);
	for (const FString& C : Imported.Collisions)
	{
		UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] 【0】%s"), *C);
	}
	if (!Imported.bSuccess) return Finish(false, Imported.Message);

	const FMmdWireResult Wire = FMmdPhysicsWiring::WirePhysics(Imported.Mesh, Imported.SourcePath);
	UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 【1】%s"), *Wire.Message);
	if (!Wire.bSuccess) return Finish(false, Wire.Message);

	const FMmdMaterialResult Mat = FMmdMaterialConversion::ConvertMaterials(Imported.Mesh, Imported.SourcePath);
	UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 【2】%s"), *Mat.Message);
	if (!Mat.bSuccess) return Finish(false, Mat.Message);

	const FMmdActorResult Actor = FMmdActorBuilder::BuildActor(Imported.Mesh, Imported.SourcePath);
	UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 【3】%s"), *Actor.Message);
	if (!Actor.bSuccess) return Finish(false, Actor.Message);

	// --- 開始後に未保存になったものだけを保存する ---
	TArray<UPackage*> ToSave;
	{
		const FString FolderPrefix = Imported.Precheck.Folder + TEXT("/");
		TArray<UPackage*> Dirty;
		FEditorFileUtils::GetDirtyContentPackages(Dirty);
		for (UPackage* P : Dirty)
		{
			if (!DirtyBefore.Contains(P->GetFName()))
			{
				ToSave.Add(P);
			}
			else if (P->GetName().StartsWith(FolderPrefix))
			{
				Out.SkippedPreDirty.Add(P->GetName());
			}
		}
	}
	const bool bSaved = ToSave.Num() == 0 || UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty=*/true);

	for (const FString& P : Out.SkippedPreDirty)
	{
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] 開始前から未保存だったため保存しませんでした (パイプラインが変更した可能性があります): %s"), *P);
	}
	return Finish(bSaved, bSaved ? TEXT("完了") : TEXT("保存に失敗したパッケージがあります"));
}

namespace
{
	/** MmdPhysics.ImportPipeline <.glb> [-Force]。ウィンドウの 0→3 を順に押すのと同じ。 */
	void RunImportPipelineCommand(const TArray<FString>& Args)
	{
		// パスに空白があっても通るよう、-Force 以外の引数はつなぎ直す (引用符は外す)。
		bool bForce = false;
		TArray<FString> PathParts;
		for (const FString& A : Args)
		{
			if (A.Equals(TEXT("-Force"), ESearchCase::IgnoreCase)) { bForce = true; continue; }
			PathParts.Add(A);
		}
		const FString GlbPath = FString::Join(PathParts, TEXT(" ")).TrimStartAndEnd().TrimQuotes();
		if (GlbPath.IsEmpty())
		{
			UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] 使い方: MmdPhysics.ImportPipeline <.glb の絶対パス> [-Force]"));
			return;
		}

		const FMmdImportPipelineResult R = FMmdGlbImport::RunPipeline(GlbPath, bForce);
		if (!R.bSuccess)
		{
			UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] ImportPipeline を中止しました: %s%s"), *R.Message,
				R.Message.Contains(TEXT("上書きの許可")) ? TEXT(" (上書きするには -Force を付けてください)") : TEXT(""));
		}
		UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 保存したパッケージ: %d 件"), R.SavedPackages.Num());
		for (const FString& P : R.SavedPackages)
		{
			UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics]   保存: %s"), *P);
		}
	}

	FAutoConsoleCommand GMmdImportPipelineCommand(
		TEXT("MmdPhysics.ImportPipeline"),
		TEXT("MmdPhysics.ImportPipeline <.glb の絶対パス> [-Force] : 取り込み→物理配線→マテリアル変換→アクター生成を行い、")
		TEXT("作成・変更したパッケージだけを保存する。取り込み先に既存アセットがあれば -Force が無い限り中止する"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunImportPipelineCommand));
}
