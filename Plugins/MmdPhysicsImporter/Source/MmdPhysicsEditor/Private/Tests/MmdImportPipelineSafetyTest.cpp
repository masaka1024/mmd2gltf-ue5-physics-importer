// Copyright (c) 2026 masaka1024. MIT License.
//
// 取り込みパイプラインの安全確認。**取り込み先のアセットを上書き保存する**ので既定ではスキップ。
//   MmdPhysics.Editor.ImportPipelineSafety
//
//   MMD_PIPELINE_GLB … 取り込む .glb。/Game/<ファイル名>/ に**取り込み済み**であること
//                      (既存アセットがある状態での中止を確かめるため)。
//
// ★何を守っているか。
//   1. 取り込み先に既存アセットがあれば、許可なしの取り込み・パイプラインは中止する。
//   2. 許可ありのパイプラインは、自分が作成・変更したパッケージだけを保存する。
//      開始前から未保存だった無関係のアセットは未保存のまま残す。
//   3. 全角数字のボーン名を持つ旧形式のスケルトンがあれば、上書きせずに中止する。

#include "Misc/AutomationTest.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "HAL/PlatformMisc.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MmdGlbImport.h"
#include "MmdPhysicsDataAsset.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdImportPipelineSafetyTest, "MmdPhysics.Editor.ImportPipelineSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdImportPipelineSafetyTest::RunTest(const FString& Parameters)
{
	const FString GlbPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PIPELINE_GLB"));
	if (GlbPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_PIPELINE_GLB が未設定のためスキップ (取り込み先のアセットを上書き保存するため)。"));
		return true;
	}

	// --- 1. 既存アセットがあると中止する ---
	const FMmdImportPrecheck Check = FMmdGlbImport::Precheck(GlbPath);
	AddInfo(Check.Message);
	if (!TestEqual(TEXT("取り込み済みのフォルダは Existing と判定される"),
		static_cast<int32>(Check.State), static_cast<int32>(FMmdImportPrecheck::EState::Existing)))
	{
		return false;
	}

	const FMmdGlbImportResult NoForceImport = FMmdGlbImport::Import(GlbPath, /*bAllowOverwrite=*/false);
	TestFalse(TEXT("[0] 相当: 許可なしの取り込みは中止する"), NoForceImport.bSuccess);
	TestEqual(TEXT("[0] 相当: 中止の理由は既存アセット"),
		static_cast<int32>(NoForceImport.Failure), static_cast<int32>(FMmdGlbImportResult::EFailure::ExistingAssets));
	TestNull(TEXT("[0] 相当: 何も取り込まない"), NoForceImport.Mesh);

	const FMmdImportPipelineResult NoForce = FMmdGlbImport::RunPipeline(GlbPath, /*bForce=*/false);
	TestFalse(TEXT("コマンド相当: -Force なしは中止する"), NoForce.bSuccess);
	TestEqual(TEXT("コマンド相当: 中止時は何も保存しない"), NoForce.SavedPackages.Num(), 0);

	// --- 2. 無関係の未保存アセットを作ってから -Force で流す ---
	const FString ProbeName = TEXT("/Game/__MmdDirtyProbe/Probe");
	UPackage* ProbePackage = CreatePackage(*ProbeName);
	UMmdPhysicsData* Probe = NewObject<UMmdPhysicsData>(ProbePackage, TEXT("Probe"), RF_Public | RF_Standalone);
	ProbePackage->MarkPackageDirty();
	const FString ProbeFile = FPackageName::LongPackageNameToFilename(ProbeName, FPackageName::GetAssetPackageExtension());
	TestTrue(TEXT("無関係アセットが未保存で存在する"), ProbePackage->IsDirty() && !FPaths::FileExists(ProbeFile));

	const FMmdImportPipelineResult Forced = FMmdGlbImport::RunPipeline(GlbPath, /*bForce=*/true);
	TestTrue(FString::Printf(TEXT("-Force ありは完走する (%s)"), *Forced.Message), Forced.bSuccess);
	AddInfo(FString::Printf(TEXT("保存したパッケージ %d 件: %s"),
		Forced.SavedPackages.Num(), *FString::Join(Forced.SavedPackages, TEXT(", "))));

	const FString Folder = Check.Folder + TEXT("/");
	int32 Outside = 0;
	for (const FString& P : Forced.SavedPackages)
	{
		if (!P.StartsWith(Folder)) { Outside++; AddInfo(FString::Printf(TEXT("取り込み先の外を保存: %s"), *P)); }
	}
	TestTrue(TEXT("取り込み先のパッケージを保存している"), Forced.SavedPackages.Num() > 0);
	TestFalse(TEXT("無関係アセットを保存していない (一覧)"), Forced.SavedPackages.Contains(ProbeName));
	TestTrue(TEXT("無関係アセットは未保存のまま"), ProbePackage->IsDirty());
	TestFalse(TEXT("無関係アセットのファイルが作られていない"), FPaths::FileExists(ProbeFile));
	AddInfo(FString::Printf(TEXT("取り込み先の外で保存したパッケージ: %d 件"), Outside));

	// 後片付け: 探り用のアセットは保存せずに捨てる (終了時に保存を促されないように)。
	ProbePackage->SetDirtyFlag(false);
	Probe->ClearFlags(RF_Public | RF_Standalone);

	// --- 3. 旧形式 (全角数字のボーン名) のスケルトンがあると中止する ---
	//   原本の .glb を半角化せずに取り込んで、旧形式を再現する (保存しない)。
	{
		const FString LegacyFolder = TEXT("/Game/__MmdLegacyProbe");
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = GlbPath;
		Task->DestinationPath = LegacyFolder;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bAsync = false;
		Task->bSave = false;
		// 全角のまま取り込むので、指ボーンのコントロールが潰れて衝突するエラーが出る (再現したい症状そのもの)。
		// 件数はモデル次第なので「1 回以上」で受ける。
		AddExpectedError(TEXT("ボーン コントロールを追加できません"), EAutomationExpectedErrorFlags::Contains, 0);
		FAssetToolsModule::GetModule().Get().ImportAssetTasks({ Task });

		const FMmdImportPrecheck Legacy = FMmdGlbImport::PrecheckFolder(LegacyFolder);
		AddInfo(Legacy.Message);
		TestEqual(TEXT("全角数字のボーン名を持つスケルトンは Legacy と判定される"),
			static_cast<int32>(Legacy.State), static_cast<int32>(FMmdImportPrecheck::EState::Legacy));
		TestTrue(TEXT("案内文が指定どおり"), Legacy.Message.StartsWith(TEXT("旧形式のアセットです。削除してから取り込み直してください。")));

		for (UObject* Obj : Task->GetObjects())
		{
			if (Obj != nullptr) Obj->GetOutermost()->SetDirtyFlag(false);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
