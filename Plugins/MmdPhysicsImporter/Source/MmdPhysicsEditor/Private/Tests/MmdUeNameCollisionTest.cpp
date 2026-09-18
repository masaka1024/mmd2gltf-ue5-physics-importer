// Copyright (c) 2026 masaka1024. MIT License.
//
// 半角化で名前がぶつかるときの振り分け。データ不要 (常に走る)。
//   MmdPhysics.Editor.UeNameCollision
//
// ★何を守っているか。
//   UE 上の名前は半角化した名前にそろえる。`右腕1` と `右腕１` が並存するモデルでは
//   半角化で同じ綴りになるので、取り込み時に検出して警告を出し、一意な名前へ振り分ける。
//   1. 振り分けの規則 (元から半角の名前が優先、残りは `_2` `_3` …、既存の名前は避ける)。
//   2. 規則が名前の**集合だけ**で決まること (並び順に依存しない)。
//      取り込み (Editor) と照合 (Runtime の物理・モーフ) が別々に同じ対応を作り直すため、
//      ここが崩れると振り分けたボーンを別のボーンと取り違える。
//   3. 取り込み用の .glb が実際にその名前で書き換わり、物理リーダから見た名前と
//      照合側の対応 (FUeNameMap::ToUe) が 1 本ずつ一致すること。

#include "Misc/AutomationTest.h"
#include "MmdGlbNameNormalize.h"
#include "MmdGlbPhysicsReader.h"
#include "MmdNameNormalize.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

namespace
{
	void AppendU32LE(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(static_cast<uint8>(V & 0xFF));
		Out.Add(static_cast<uint8>((V >> 8) & 0xFF));
		Out.Add(static_cast<uint8>((V >> 16) & 0xFF));
		Out.Add(static_cast<uint8>((V >> 24) & 0xFF));
	}

	/** JSON だけの GLB を組む。 */
	TArray<uint8> MakeJsonOnlyGlb(const FString& Json)
	{
		const FTCHARToUTF8 JsonUtf8(*Json);
		TArray<uint8> JsonChunk;
		JsonChunk.Append(reinterpret_cast<const uint8*>(JsonUtf8.Get()), JsonUtf8.Length());
		while (JsonChunk.Num() % 4 != 0) JsonChunk.Add(0x20);

		TArray<uint8> Glb;
		AppendU32LE(Glb, 0x46546C67);   // "glTF"
		AppendU32LE(Glb, 2);
		AppendU32LE(Glb, 12 + 8 + JsonChunk.Num());
		AppendU32LE(Glb, JsonChunk.Num());
		AppendU32LE(Glb, 0x4E4F534A);   // "JSON"
		Glb.Append(JsonChunk);
		return Glb;
	}

	/** 大文字小文字も区別して比べる (FString の == は区別しない)。 */
	bool SameExact(const FString& A, const TCHAR* B)
	{
		return A.Equals(B, ESearchCase::CaseSensitive);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdUeNameCollisionTest, "MmdPhysics.Editor.UeNameCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdUeNameCollisionTest::RunTest(const FString& Parameters)
{
	// --- 1. 衝突しなければ半角化するだけ ---
	{
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(
			{ TEXT("右人指１"), TEXT("右人指２"), TEXT("センター") }, TEXT("ボーン"));
		TestEqual(TEXT("衝突なし"), M.Collisions.Num(), 0);
		TestTrue(TEXT("右人指１ → 右人指1"), SameExact(M.ToUe(TEXT("右人指１")), TEXT("右人指1")));
		TestTrue(TEXT("センター はそのまま"), SameExact(M.ToUe(TEXT("センター")), TEXT("センター")));
	}

	// --- 2. 元から半角の名前が H を取り、全角側が H_2 へ ---
	{
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(
			{ TEXT("右腕１"), TEXT("右腕1") }, TEXT("ボーン"));
		TestEqual(TEXT("衝突を 1 件検出する"), M.Collisions.Num(), 1);
		TestTrue(TEXT("半角の 右腕1 は名前を変えない"), SameExact(M.ToUe(TEXT("右腕1")), TEXT("右腕1")));
		TestTrue(TEXT("全角の 右腕１ は 右腕1_2 へ"), SameExact(M.ToUe(TEXT("右腕１")), TEXT("右腕1_2")));
		if (M.Collisions.Num() == 1)
		{
			AddInfo(M.Collisions[0]);
		}
	}

	// --- 3. 並び順に依存しない (取り込みと照合で同じ対応になる) ---
	{
		const TArray<FString> A = { TEXT("髪１"), TEXT("髪1"), TEXT("髪2"), TEXT("髪２"), TEXT("首") };
		TArray<FString> B = A;
		Algo::Reverse(B);
		const NameNormalize::FUeNameMap MA = NameNormalize::BuildUeNameMap(A, TEXT("ボーン"));
		const NameNormalize::FUeNameMap MB = NameNormalize::BuildUeNameMap(B, TEXT("ボーン"));
		bool bSame = MA.OriginalToUe.Num() == MB.OriginalToUe.Num();
		for (const TPair<FString, FString>& P : MA.OriginalToUe)
		{
			const FString* Other = MB.OriginalToUe.Find(P.Key);
			bSame &= (Other != nullptr && Other->Equals(P.Value, ESearchCase::CaseSensitive));
		}
		TestTrue(TEXT("逆順に並べても同じ対応になる"), bSame);
		TestEqual(TEXT("2 グループとも検出する"), MA.Collisions.Num(), 2);
	}

	// --- 4. 振り分け先が既存の名前と重なるなら飛ばす ---
	{
		// `指1_2` が実在するので、`指１` は `指1_3` へ。
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(
			{ TEXT("指1"), TEXT("指１"), TEXT("指1_2") }, TEXT("ボーン"));
		TestTrue(TEXT("実在の 指1_2 はそのまま"), SameExact(M.ToUe(TEXT("指1_2")), TEXT("指1_2")));
		TestTrue(TEXT("指１ は 指1_3 へ"), SameExact(M.ToUe(TEXT("指１")), TEXT("指1_3")));
	}

	// --- 5. 3 人以上のグループ ---
	{
		// 半角・全角の混在で 3 通りが同じ `あ12` になる。
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(
			{ TEXT("あ１２"), TEXT("あ12"), TEXT("あ1２") }, TEXT("モーフ"));
		TSet<FString> Ue;
		for (const TPair<FString, FString>& P : M.OriginalToUe) Ue.Add(P.Value);
		TestEqual(TEXT("3 本とも別の名前になる"), Ue.Num(), 3);
		TestTrue(TEXT("半角の あ12 はそのまま"), SameExact(M.ToUe(TEXT("あ12")), TEXT("あ12")));
	}

	// --- 6. 取り込み用 .glb と照合側が同じ名前になる ---
	{
		const FString Json = TEXT(R"({"asset":{"version":"2.0"},
"nodes":[{"name":"センター"},{"name":"右腕１"},{"name":"右腕1"},{"name":"右人指１"}],
"skins":[{"joints":[0,1,2,3]}],
"extras":{"mmd":{"unitScale":0.08}}})");
		const TArray<uint8> In = MakeJsonOnlyGlb(Json);

		AddExpectedMessage(TEXT("半角化で '右腕1' に重なる"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
		TArray<uint8> Out;
		const FMmdGlbNormalizeResult R = FMmdGlbNameNormalize::NormalizeBytes(In, Out);
		TestTrue(TEXT("衝突があっても取り込みを続ける"), R.bSuccess);
		TestEqual(TEXT("衝突を 1 件報告する"), R.Collisions.Num(), 1);

		float UnitScale = 0.0f;
		TArray<FString> Warnings;
		const TSharedPtr<PmxPhysicsModel> Orig = GlbPhysicsReader::LoadBytes(In, UnitScale, Warnings);
		const TSharedPtr<PmxPhysicsModel> Norm = GlbPhysicsReader::LoadBytes(Out, UnitScale, Warnings);
		if (!TestTrue(TEXT("両方読める"), Orig.IsValid() && Norm.IsValid())) return false;
		if (!TestEqual(TEXT("ボーン数が同じ"), Norm->BoneNames.Num(), Orig->BoneNames.Num())) return false;

		// 照合側 (AnimNode_MmdPhysics) は原本の名前の集合から対応を作り直す。
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(Orig->BoneNames, TEXT("ボーン"));
		for (int32 i = 0; i < Orig->BoneNames.Num(); ++i)
		{
			TestTrue(FString::Printf(TEXT("'%s' の UE 名が .glb と照合で一致 (%s / %s)"),
					*Orig->BoneNames[i], *Norm->BoneNames[i], *M.ToUe(Orig->BoneNames[i])),
				Norm->BoneNames[i].Equals(M.ToUe(Orig->BoneNames[i]), ESearchCase::CaseSensitive));
		}

		TSet<FString> Unique;
		for (const FString& N : Norm->BoneNames) Unique.Add(N);
		TestEqual(TEXT("取り込み後のボーン名に重複が無い"), Unique.Num(), Norm->BoneNames.Num());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
