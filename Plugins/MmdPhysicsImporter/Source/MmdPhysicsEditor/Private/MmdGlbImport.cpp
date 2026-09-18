// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdGlbImport.h"

#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "MmdActorBuilder.h"
#include "MmdGlbNameNormalize.h"
#include "MmdMaterialConversion.h"
#include "MmdPhysicsCoreLog.h"
#include "MmdPhysicsWiring.h"
#include "MmdScopedUtf8CType.h"

FMmdGlbImportResult FMmdGlbImport::Import(const FString& GlbPath)
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
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Obj))
		{
			Result.Mesh = Mesh;
			break;
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

namespace
{
	/** MmdPhysics.ImportPipeline <.glb>。ウィンドウの 0→3 を順に押すのと同じ。 */
	void RunImportPipeline(const TArray<FString>& Args)
	{
		// パスに空白があっても通るよう、引数はつなぎ直す (引用符は外す)。
		FString GlbPath = FString::Join(Args, TEXT(" ")).TrimStartAndEnd().TrimQuotes();
		if (GlbPath.IsEmpty())
		{
			UE_LOG(LogMmdPhysics, Error, TEXT("[MmdPhysics] 使い方: MmdPhysics.ImportPipeline <.glb の絶対パス>"));
			return;
		}

		const FMmdGlbImportResult Imported = FMmdGlbImport::Import(GlbPath);
		UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 【0】%s"), *Imported.Message);
		for (const FString& C : Imported.Collisions)
		{
			UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] 【0】%s"), *C);
		}
		if (!Imported.bSuccess) return;

		const FMmdWireResult Wire = FMmdPhysicsWiring::WirePhysics(Imported.Mesh, Imported.SourcePath);
		UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 【1】%s"), *Wire.Message);
		if (!Wire.bSuccess) return;

		const FMmdMaterialResult Mat = FMmdMaterialConversion::ConvertMaterials(Imported.Mesh, Imported.SourcePath);
		UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 【2】%s"), *Mat.Message);
		if (!Mat.bSuccess) return;

		const FMmdActorResult Actor = FMmdActorBuilder::BuildActor(Imported.Mesh, Imported.SourcePath);
		UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 【3】%s"), *Actor.Message);
		if (!Actor.bSuccess) return;

		// 取り込みは保存しない (bSave=false) ので、ここでまとめて保存する。
		const bool bSaved = UEditorLoadingAndSavingUtils::SaveDirtyPackages(
			/*bSaveMapPackages=*/false, /*bSaveContentPackages=*/true);
		UE_LOG(LogMmdPhysics, Display, TEXT("[MmdPhysics] 保存: %s"), bSaved ? TEXT("成功") : TEXT("失敗"));
	}

	FAutoConsoleCommand GMmdImportPipelineCommand(
		TEXT("MmdPhysics.ImportPipeline"),
		TEXT("MmdPhysics.ImportPipeline <.glb の絶対パス> : 取り込み→物理配線→マテリアル変換→アクター生成を行い保存する"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunImportPipeline));
}
