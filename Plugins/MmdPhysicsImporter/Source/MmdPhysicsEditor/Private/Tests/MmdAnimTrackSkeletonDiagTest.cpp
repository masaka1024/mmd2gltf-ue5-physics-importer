// Copyright (c) 2026 masaka1024. MIT License.
//
// 取り込み済み AnimSequence のボーントラックとスケルトンの突き合わせ (診断用)。
//   MmdPhysics.Editor.AnimTrackSkeletonDiag
//
//   MMD_DIAG_ANIM   … AnimSequence のパス (例 /Game/IA/SkeletalMeshes/IA_Anim)。未設定ならスキップ
//   MMD_PARITY_GLB  … 原本の .glb (任意)。あれば原本側の名前も引き当てる
//
// ★何を見ているか。
//   ロード時に UAnimSequence::OnAnimModelLoaded が RemoveBoneTracksMissingFromSkeleton を呼び、
//   「FK セクションのパラメータ名から得たボーン名がスケルトンに無い」トラックを消そうとする。
//   その対象を、エンジンと同じ判定 (GetBoneTrackNames → FindBoneIndex) で列挙し、
//   原本名・半角化後の名前・スケルトンに在るかを並べる。
//   名前の対応ずれ (半角化すれば在る) か、本当にスケルトンに無いボーンかを切り分けるためのもの。
//   判定は記録するだけで、失敗にはしない。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Misc/FileHelper.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdMiniJson.h"
#include "MmdNameNormalize.h"

#if WITH_DEV_AUTOMATION_TESTS

// ★ファイルごとの名前空間で囲む。unity build では複数の .cpp が 1 つにまとめてコンパイルされ、
//   匿名名前空間の補助関数 (AppendU32LE など) が他のファイルと同名だと再定義エラーになる。
namespace MmdPhysicsTests_MmdAnimTrackSkeletonDiagTest
{

using namespace MmdPhysics;

namespace
{
	/** .glb の全ノード名。 */
	TArray<FString> CollectGlbNodeNames(const FString& GlbPath)
	{
		TArray<FString> Names;
		TArray<uint8> Bytes;
		if (GlbPath.IsEmpty() || !FFileHelper::LoadFileToArray(Bytes, *GlbPath)) return Names;
		TSharedPtr<MmdJsonValue> RootVal;
		TArray<uint8> Bin;
		TArray<FString> Warnings;
		if (!GlbPhysicsReader::ParseGlb(Bytes, RootVal, Bin, Warnings)) return Names;
		const FMmdJsonArray* Nodes = MiniJson::Arr(MiniJson::Get(MiniJson::Obj(RootVal), TEXT("nodes")));
		if (Nodes == nullptr) return Names;
		for (const TSharedPtr<MmdJsonValue>& N : *Nodes)
		{
			Names.Add(MiniJson::Str(MiniJson::Get(MiniJson::Obj(N), TEXT("name"))));
		}
		return Names;
	}

	FString Codepoints(const FString& S)
	{
		FString Out;
		for (const TCHAR C : S)
		{
			if (!Out.IsEmpty()) Out += TEXT(" ");
			Out += FString::Printf(TEXT("U+%04X"), static_cast<uint32>(C));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdAnimTrackSkeletonDiagTest, "MmdPhysics.Editor.AnimTrackSkeletonDiag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdAnimTrackSkeletonDiagTest::RunTest(const FString& Parameters)
{
	const FString AnimPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_DIAG_ANIM"));
	if (AnimPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_DIAG_ANIM が未設定のためスキップ。"));
		return true;
	}

	UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *AnimPath);
	if (!TestNotNull(TEXT("AnimSequence を読める"), Anim)) return false;
	USkeleton* Skeleton = Anim->GetSkeleton();
	if (!TestNotNull(TEXT("スケルトンがある"), Skeleton)) return false;
	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();

	TArray<FName> TrackNames;
	Anim->GetDataModel()->GetBoneTrackNames(TrackNames);
	AddInfo(FString::Printf(TEXT("%s: トラック %d 本 / スケルトン %s のボーン %d 本"),
		*AnimPath, TrackNames.Num(), *Skeleton->GetPathName(), RefSkel.GetNum()));

	// スケルトン側の名前を半角化して引ける表 (全角/半角の対応ずれを見つけるため)。
	TMap<FString, FString> SkelByHalf;
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		const FString B = RefSkel.GetBoneName(i).ToString();
		SkelByHalf.Add(NameNormalize::ToHalfWidthDigits(B), B);
	}

	// 原本 .glb 側の名前を半角化して引ける表。
	TMultiMap<FString, FString> GlbByHalf;
	for (const FString& G : CollectGlbNodeNames(FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB"))))
	{
		GlbByHalf.Add(NameNormalize::ToHalfWidthDigits(G), G);
	}

	int32 Missing = 0;
	for (const FName& Track : TrackNames)
	{
		if (RefSkel.FindBoneIndex(Track) != INDEX_NONE) continue;
		Missing++;

		const FString T = Track.ToString();
		const FString Half = NameNormalize::ToHalfWidthDigits(T);
		const FString* SkelMatch = SkelByHalf.Find(Half);
		TArray<FString> GlbNames;
		GlbByHalf.MultiFind(Half, GlbNames);

		AddInfo(FString::Printf(
			TEXT("[欠落 %d] トラック名='%s' (%s) / 半角化='%s' / スケルトンに在る: そのまま=no, 半角化で=%s / 原本 .glb の名前=%s"),
			Missing, *T, *Codepoints(T), *Half,
			SkelMatch ? *FString::Printf(TEXT("yes ('%s')"), **SkelMatch) : TEXT("no"),
			GlbNames.Num() ? *FString::Join(GlbNames, TEXT(", ")) : TEXT("(無し)")));
	}
	AddInfo(FString::Printf(TEXT("スケルトンに無いトラック: %d 本"), Missing));

	// 指ボーン (PMX 標準の `右人指1` など。全角数字を含んでいたもの) のトラック名を並べる。
	TArray<FString> Fingers;
	for (const FName& Track : TrackNames)
	{
		const FString T = Track.ToString();
		if (T.Contains(TEXT("指"))) Fingers.Add(T);
	}
	Fingers.Sort();
	AddInfo(FString::Printf(TEXT("指ボーンのトラック: %d 本: %s"), Fingers.Num(), *FString::Join(Fingers, TEXT(" "))));
	return true;
}

} // namespace MmdPhysicsTests_MmdAnimTrackSkeletonDiagTest

#endif // WITH_DEV_AUTOMATION_TESTS
