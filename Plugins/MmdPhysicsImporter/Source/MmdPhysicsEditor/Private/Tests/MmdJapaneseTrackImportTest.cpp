// Copyright (c) 2026 masaka1024. MIT License.
//
// 日本語のボーン名・モーフ名が取り込みで脱落しないかの検証。
//   MmdPhysics.Editor.JapaneseTrackImport
//   MMD_LOCALE_IMPORT_GLB (.glb の絶対パス) が未設定ならスキップ。
//
// ★何を守っているか。
//   起動時にプロセス全体の LC_CTYPE を上げるのをやめ、取り込みの間だけ
//   FMmdScopedUtf8CType (スレッド局所の uselocale) で上げる方式にした。
//   Control Rig の SanitizeName (iswalpha) が別スレッドで走っていると
//   スコープが届かず、日本語名が '_' に潰れてトラックが捨てられる。
//   このテストは SMmdImporterWindow::OnImportGlb と同じ手順
//   (全角数字の半角化 → スコープ内で ImportAssetTasks → ApplyMorphCurves) で取り込み、
//   .glb のアニメーションが動かすボーンが全部トラックとして残っているかを見る。
//
// ★MMD_LOCALE_CONTROL=1 のときはスコープ無しで取り込む (対照実験)。
//   C ロケールのままだと潰れることを確かめるためのもので、脱落していてもエラーにしない。
//
// ★取り込み先は /Game/__MmdLocaleCheck/ で、保存しない (bSave=false)。
//   既存の /Game/<名前>/ には触れない。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimSequence.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MmdGlbNameNormalize.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdMiniJson.h"
#include "MmdMorphAnimation.h"
#include "MmdScopedUtf8CType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

namespace
{
	/** .glb のアニメーションが translation / rotation / scale を動かすノード名。 */
	TSet<FString> CollectAnimatedNodeNames(const FString& GlbPath)
	{
		TSet<FString> Names;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *GlbPath)) return Names;

		TSharedPtr<MmdJsonValue> RootVal;
		TArray<uint8> Bin;
		TArray<FString> Warnings;
		if (!GlbPhysicsReader::ParseGlb(Bytes, RootVal, Bin, Warnings)) return Names;
		const FMmdJsonObject* Root = MiniJson::Obj(RootVal);
		if (Root == nullptr) return Names;

		const FMmdJsonArray* Nodes = MiniJson::Arr(MiniJson::Get(Root, TEXT("nodes")));
		const FMmdJsonArray* Anims = MiniJson::Arr(MiniJson::Get(Root, TEXT("animations")));
		if (Nodes == nullptr || Anims == nullptr) return Names;

		for (const TSharedPtr<MmdJsonValue>& A : *Anims)
		{
			const FMmdJsonArray* Channels = MiniJson::Arr(MiniJson::Get(MiniJson::Obj(A), TEXT("channels")));
			if (Channels == nullptr) continue;
			for (const TSharedPtr<MmdJsonValue>& C : *Channels)
			{
				const FMmdJsonObject* Target = MiniJson::Obj(MiniJson::Get(MiniJson::Obj(C), TEXT("target")));
				if (Target == nullptr) continue;
				const FString Path = MiniJson::Str(MiniJson::Get(Target, TEXT("path")));
				if (Path != TEXT("translation") && Path != TEXT("rotation") && Path != TEXT("scale")) continue;
				const int32 NodeIndex = MiniJson::Int(MiniJson::Get(Target, TEXT("node")));
				if (!Nodes->IsValidIndex(NodeIndex)) continue;
				Names.Add(MiniJson::Str(MiniJson::Get(MiniJson::Obj((*Nodes)[NodeIndex]), TEXT("name"))));
			}
		}
		return Names;
	}

	bool IsAllUnderscore(const FString& S)
	{
		if (S.IsEmpty()) return false;
		for (const TCHAR C : S)
		{
			if (C != TEXT('_')) return false;
		}
		return true;
	}

	bool HasNonAscii(const FString& S)
	{
		for (const TCHAR C : S)
		{
			if (C > 0x7F) return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdJapaneseTrackImportTest, "MmdPhysics.Editor.JapaneseTrackImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdJapaneseTrackImportTest::RunTest(const FString& Parameters)
{
	const FString SourcePath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_LOCALE_IMPORT_GLB"));
	if (SourcePath.IsEmpty())
	{
		AddInfo(TEXT("MMD_LOCALE_IMPORT_GLB が未設定のためスキップ。"));
		return true;
	}
	const bool bControl = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_LOCALE_CONTROL")) == TEXT("1");
	if (!TestTrue(TEXT(".glb がある"), FPaths::FileExists(SourcePath))) return false;

	// --- OnImportGlb と同じ: 全角数字を半角へ直した複製を作る ---
	const FString NormalizedPath = FMmdGlbNameNormalize::MakeNormalizedPath(SourcePath);
	const FMmdGlbNormalizeResult Norm = FMmdGlbNameNormalize::NormalizeFile(SourcePath, NormalizedPath);
	if (!TestTrue(TEXT("半角化の複製を作れる"), Norm.bSuccess)) return false;
	const FString ImportFrom = Norm.IsNoOp() ? SourcePath : NormalizedPath;

	const TSet<FString> Expected = CollectAnimatedNodeNames(ImportFrom);
	int32 ExpectedNonAscii = 0;
	for (const FString& N : Expected) if (HasNonAscii(N)) ExpectedNonAscii++;
	AddInfo(FString::Printf(TEXT("モード: %s / .glb が動かすボーン %d 本 (うち日本語 %d 本)"),
		bControl ? TEXT("対照 (スコープ無し)") : TEXT("スコープ有り"), Expected.Num(), ExpectedNonAscii));
	if (!TestTrue(TEXT(".glb にアニメーションがある"), Expected.Num() > 0)) return false;

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = ImportFrom;
	Task->DestinationPath = FString(TEXT("/Game/__MmdLocaleCheck/"))
		+ (bControl ? TEXT("NoScope") : TEXT("Scoped"));
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bAsync = false;
	Task->bSave = false;
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);

	AddInfo(FString::Printf(TEXT("取り込み前: このスレッドで日本語は文字 = %s"),
		FMmdScopedUtf8CType::IsJapaneseAlphaOnThisThread() ? TEXT("true") : TEXT("false")));
	if (bControl)
	{
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
	}
	else
	{
		FMmdScopedUtf8CType Utf8CType;   // OnImportGlb と同じ置き方
		AddInfo(FString::Printf(TEXT("スコープ内: Applied=%s, 日本語は文字 = %s"),
			*Utf8CType.GetAppliedLocale(),
			FMmdScopedUtf8CType::IsJapaneseAlphaOnThisThread() ? TEXT("true") : TEXT("false")));
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
	}

	USkeletalMesh* Mesh = nullptr;
	UAnimSequence* Anim = nullptr;
	for (UObject* Obj : Task->GetObjects())
	{
		if (Mesh == nullptr) Mesh = Cast<USkeletalMesh>(Obj);
		if (Anim == nullptr) Anim = Cast<UAnimSequence>(Obj);
	}
	if (!TestNotNull(TEXT("スケルタルメッシュが取り込まれる"), Mesh)) return false;
	if (!TestNotNull(TEXT("AnimSequence が取り込まれる"), Anim)) return false;

	// --- ボーントラック ---
	TArray<FName> TrackNames;
	Anim->GetDataModel()->GetBoneTrackNames(TrackNames);
	TSet<FString> Actual;
	int32 Collapsed = 0;
	for (const FName& N : TrackNames)
	{
		const FString S = N.ToString();
		Actual.Add(S);
		if (IsAllUnderscore(S)) Collapsed++;
	}
	TArray<FString> Missing;
	for (const FString& N : Expected)
	{
		if (!Actual.Contains(N)) Missing.Add(N);
	}
	Missing.Sort();
	AddInfo(FString::Printf(TEXT("ボーントラック: %d 本 / 期待 %d 本 / 欠落 %d 本 / '_' だけの名前 %d 本"),
		TrackNames.Num(), Expected.Num(), Missing.Num(), Collapsed));
	if (Missing.Num() > 0)
	{
		AddInfo(FString::Printf(TEXT("欠落 (先頭 20): %s"),
			*FString::Join(TArrayView<const FString>(Missing).Left(20), TEXT(", "))));
	}

	// --- モーフカーブ (BuildActor と同じく原本の .glb を渡す) ---
	const FMmdMorphAnimResult Morph = FMmdMorphAnimation::ApplyMorphCurves(Mesh, Anim, SourcePath);
	AddInfo(FString::Printf(TEXT("ApplyMorphCurves: %s"), *Morph.Message));

	int32 JapaneseCurves = 0, CollapsedCurves = 0;
	TArray<FString> CurveSample;
	for (const FFloatCurve& Curve : Anim->GetDataModel()->GetFloatCurves())
	{
		const FString S = Curve.GetName().ToString();
		if (HasNonAscii(S)) JapaneseCurves++;
		if (IsAllUnderscore(S)) CollapsedCurves++;
		if (CurveSample.Num() < 10) CurveSample.Add(S);
	}
	AddInfo(FString::Printf(TEXT("モーフカーブ: %d 本 (日本語 %d 本 / '_' だけ %d 本) 例: %s"),
		Anim->GetDataModel()->GetFloatCurves().Num(), JapaneseCurves, CollapsedCurves,
		*FString::Join(CurveSample, TEXT(", "))));

	if (bControl)
	{
		// 対照実験は記録するだけ。
		return true;
	}

	TestEqual(TEXT("日本語ボーン名のトラックが脱落していない"), Missing.Num(), 0);
	TestEqual(TEXT("'_' に潰れたトラックが無い"), Collapsed, 0);
	TestTrue(TEXT("日本語名のモーフカーブがある"), JapaneseCurves > 0);
	TestTrue(TEXT("まばたき のカーブがある"),
		Anim->GetDataModel()->GetFloatCurves().ContainsByPredicate([](const FFloatCurve& C)
		{
			return C.GetName() == FName(TEXT("まばたき"));
		}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
