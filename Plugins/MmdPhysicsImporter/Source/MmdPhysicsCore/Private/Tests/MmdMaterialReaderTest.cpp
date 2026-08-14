// Copyright (c) 2026 masaka1024. MIT License.
//
// マテリアル情報の読み取り検証。MMD_PARITY_GLB が未設定ならスキップ。

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "MmdGlbMaterialReader.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdMaterialReaderTest, "MmdPhysics.Core.MaterialReader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdMaterialReaderTest::RunTest(const FString& Parameters)
{
	const FString GlbPath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_PARITY_GLB"));
	if (GlbPath.IsEmpty())
	{
		AddInfo(TEXT("MMD_PARITY_GLB が未設定のためスキップ。"));
		return true;
	}

	MmdMaterialSet Set;
	TArray<FString> Warnings;
	const bool bOk = GlbMaterialReader::LoadFile(GlbPath, Set, Warnings);
	for (const FString& W : Warnings) AddInfo(FString::Printf(TEXT("[warn] %s"), *W));

	if (!TestTrue(TEXT("マテリアルを読める"), bOk)) return false;
	TestTrue(TEXT("マテリアルが 1 つ以上ある"), Set.Materials.Num() > 0);

	AddInfo(FString::Printf(TEXT("マテリアル %d / テクスチャ %d"), Set.Materials.Num(), Set.TextureImageNames.Num()));

	// 実際に何が入っているかを一覧で残す (UE 側の変換を書くときの参照になる)。
	int32 WithToon = 0, WithSphere = 0, WithEdge = 0, WithSharedToon = 0;
	for (const MmdMaterialInfo& M : Set.Materials)
	{
		if (M.ToonTexture >= 0) WithToon++;
		if (M.ToonShared >= 0) WithSharedToon++;
		if (M.SphereTexture >= 0 && M.SphereMode != 0) WithSphere++;
		if (M.HasEdge()) WithEdge++;
	}
	AddInfo(FString::Printf(TEXT("個別トゥーン %d / 共有トゥーン %d / スフィア %d / エッジ %d"),
		WithToon, WithSharedToon, WithSphere, WithEdge));

	for (int32 i = 0; i < FMath::Min(Set.Materials.Num(), 5); i++)
	{
		const MmdMaterialInfo& M = Set.Materials[i];
		AddInfo(FString::Printf(
			TEXT("  [%d] %s base=%s sphere=%s(mode %d) toon=%s shared=%d edge=%d(%.3f) alpha=%s"),
			i, *M.Name,
			Set.HasTexture(M.BaseColorTexture) ? *Set.TextureImageNames[M.BaseColorTexture] : TEXT("-"),
			Set.HasTexture(M.SphereTexture) ? *Set.TextureImageNames[M.SphereTexture] : TEXT("-"),
			M.SphereMode,
			Set.HasTexture(M.ToonTexture) ? *Set.TextureImageNames[M.ToonTexture] : TEXT("-"),
			M.ToonShared, M.HasEdge() ? 1 : 0, M.EdgeSize, *M.AlphaClass));
	}

	// テクスチャ index が範囲内であること (壊れた index を後段へ流さない)。
	for (const MmdMaterialInfo& M : Set.Materials)
	{
		const bool bValid =
			(M.BaseColorTexture < 0 || Set.TextureImageNames.IsValidIndex(M.BaseColorTexture)) &&
			(M.SphereTexture < 0 || Set.TextureImageNames.IsValidIndex(M.SphereTexture)) &&
			(M.ToonTexture < 0 || Set.TextureImageNames.IsValidIndex(M.ToonTexture));
		if (!TestTrue(FString::Printf(TEXT("'%s' のテクスチャ index が範囲内"), *M.Name), bValid)) break;
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
