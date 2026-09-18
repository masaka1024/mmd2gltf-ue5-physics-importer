// Copyright (c) 2026 masaka1024. MIT License.
//
// NameNormalize::BuildUeNameMap の正解表。データ不要 (常に走る)。
//   MmdPhysics.Bridge.UeNameGolden
//
// ★期待値はすべて**手で書いた表**。関数の出力どうしを比べない。
//   取り込み (Editor) と照合 (Runtime) は同じ関数を使うので、関数自体が間違っていると
//   両側が同じ間違いで一致してしまい、他のテストでは気付けない。ここだけは独立した正解で縛る。
//
// 規則 (MmdNameNormalize.h より):
//   - UE 名 = 全角数字 (U+FF10-FF19) だけを半角にした名前。他の文字は変えない
//   - 半角化で重なるグループでは、元から半角の名前がそのまま、残りがコードポイント順に _2, _3 …
//   - 振り分け先が集合内の他の名前と重なるなら飛ばす

#include "Misc/AutomationTest.h"
#include "MmdNameNormalize.h"

#include <algorithm>

#if WITH_DEV_AUTOMATION_TESTS

// ★ファイルごとの名前空間で囲む。unity build では複数の .cpp が 1 つにまとめてコンパイルされ、
//   匿名名前空間の補助関数 (AppendU32LE など) が他のファイルと同名だと再定義エラーになる。
namespace MmdPhysicsTests_MmdUeNameGoldenTest
{

using namespace MmdPhysics;

namespace
{
	struct FGolden
	{
		const TCHAR* Original;
		const TCHAR* Ue;
	};

	/** 表の全行について、UE 名が期待どおり (大文字小文字まで一致) かを見る。戻り値は不一致の数。 */
	int32 CheckTable(FAutomationTestBase& Test, const NameNormalize::FUeNameMap& Map,
		const TArray<FGolden>& Table, const FString& Label, bool bReport)
	{
		int32 Mismatch = 0;
		for (const FGolden& G : Table)
		{
			const FString* Actual = Map.OriginalToUe.Find(G.Original);
			const bool bOk = Actual != nullptr && Actual->Equals(G.Ue, ESearchCase::CaseSensitive);
			if (!bOk)
			{
				Mismatch++;
				if (bReport)
				{
					Test.AddError(FString::Printf(TEXT("%s: '%s' → 期待 '%s' / 実際 '%s'"),
						*Label, G.Original, G.Ue, Actual ? **Actual : TEXT("(対応表に無い)")));
				}
			}
		}
		if (Map.OriginalToUe.Num() != Table.Num())
		{
			Mismatch++;
			if (bReport)
			{
				Test.AddError(FString::Printf(TEXT("%s: 対応表の件数 期待 %d / 実際 %d"),
					*Label, Table.Num(), Map.OriginalToUe.Num()));
			}
		}
		return Mismatch;
	}

	TArray<FString> OriginalsOf(const TArray<FGolden>& Table)
	{
		TArray<FString> Out;
		for (const FGolden& G : Table) Out.Add(G.Original);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdUeNameGoldenTest, "MmdPhysics.Bridge.UeNameGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdUeNameGoldenTest::RunTest(const FString& Parameters)
{
	// --- 1. 全角数字だけが半角になる。仮名・漢字・英数字・全角英字は変わらない ---
	{
		const TArray<FGolden> Table = {
			{ TEXT("右親指１"),   TEXT("右親指1") },
			{ TEXT("まばたき２"), TEXT("まばたき2") },
			{ TEXT("右人指３"),   TEXT("右人指3") },
			{ TEXT("右腕１０"),   TEXT("右腕10") },       // 2 桁
			{ TEXT("センター"),   TEXT("センター") },       // 仮名
			{ TEXT("全ての親"),   TEXT("全ての親") },       // 仮名 + 漢字
			{ TEXT("上半身2"),    TEXT("上半身2") },        // 元から半角数字
			{ TEXT("Blink"),      TEXT("Blink") },          // 英字
			{ TEXT("a_b-c.d"),    TEXT("a_b-c.d") },        // 記号 (半角化の対象外)
			{ TEXT("左足ＩＫ"),   TEXT("左足ＩＫ") },       // 全角英字は対象外
			{ TEXT("あ"),         TEXT("あ") },
		};
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(OriginalsOf(Table), TEXT("正解表1"));
		TestEqual(TEXT("1: 衝突なし"), M.Collisions.Num(), 0);
		TestEqual(TEXT("1: 正解表と一致"), CheckTable(*this, M, Table, TEXT("1"), true), 0);
	}

	// --- 2. 半角化で衝突: 元から半角の側は変わらず、全角側に _2 ---
	{
		const TArray<FGolden> Table = {
			{ TEXT("髪１"), TEXT("髪1_2") },
			{ TEXT("髪1"),  TEXT("髪1") },
		};
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(OriginalsOf(Table), TEXT("正解表2"));
		TestEqual(TEXT("2: 衝突 1 件"), M.Collisions.Num(), 1);
		TestEqual(TEXT("2: 正解表と一致"), CheckTable(*this, M, Table, TEXT("2"), true), 0);
	}

	// --- 3. 振り分け先 (_2) が実在するなら飛ばして _3 ---
	{
		const TArray<FGolden> Table = {
			{ TEXT("指1"),   TEXT("指1") },
			{ TEXT("指1_2"), TEXT("指1_2") },
			{ TEXT("指１"),  TEXT("指1_3") },
		};
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(OriginalsOf(Table), TEXT("正解表3"));
		TestEqual(TEXT("3: 正解表と一致"), CheckTable(*this, M, Table, TEXT("3"), true), 0);
	}

	// --- 4. 3 人のグループ: 半角の あ12 がそのまま、残りはコードポイント順 ---
	//   `あ1２` (2 文字目 U+0031) < `あ１２` (2 文字目 U+FF11) なので あ1２ が先に _2 を取る。
	{
		const TArray<FGolden> Table = {
			{ TEXT("あ１２"), TEXT("あ12_3") },
			{ TEXT("あ12"),   TEXT("あ12") },
			{ TEXT("あ1２"),  TEXT("あ12_2") },
		};
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(OriginalsOf(Table), TEXT("正解表4"));
		TestEqual(TEXT("4: 衝突 1 グループ"), M.Collisions.Num(), 1);
		TestEqual(TEXT("4: 正解表と一致"), CheckTable(*this, M, Table, TEXT("4"), true), 0);
	}

	// --- 5. 入力の順番を入れ替えても同じ結果 (7 要素の全順列 5040 通りを同じ正解表と突き合わせる) ---
	{
		const TArray<FGolden> Table = {
			{ TEXT("髪１"),  TEXT("髪1_2") },
			{ TEXT("髪1"),   TEXT("髪1") },
			{ TEXT("髪２"),  TEXT("髪2") },
			{ TEXT("首"),    TEXT("首") },
			{ TEXT("指1"),   TEXT("指1") },
			{ TEXT("指１"),  TEXT("指1_3") },
			{ TEXT("指1_2"), TEXT("指1_2") },
		};
		TArray<int32> Order;
		for (int32 i = 0; i < Table.Num(); ++i) Order.Add(i);

		int32 Permutations = 0;
		int32 FailedPermutations = 0;
		do
		{
			TArray<FString> Input;
			for (const int32 i : Order) Input.Add(Table[i].Original);
			const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap(Input, TEXT("正解表5"));
			// 最初に外れた並びだけ詳しく出す (5040 通り全部を並べるとログが埋まる)。
			const bool bReport = FailedPermutations == 0;
			if (CheckTable(*this, M, Table, FString::Printf(TEXT("5 (順列 %d)"), Permutations), bReport) > 0
				|| M.Collisions.Num() != 2)
			{
				FailedPermutations++;
			}
			Permutations++;
		}
		while (std::next_permutation(Order.GetData(), Order.GetData() + Order.Num()));

		TestEqual(TEXT("5: 全順列を試した"), Permutations, 5040);
		TestEqual(TEXT("5: どの順番でも正解表と一致する"), FailedPermutations, 0);
	}

	// --- 6. 集合に無い名前を引いたときは半角化だけ ---
	{
		const NameNormalize::FUeNameMap M = NameNormalize::BuildUeNameMap({ TEXT("髪1") }, TEXT("正解表6"));
		TestTrue(TEXT("6: 集合外の 髪２ → 髪2"), M.ToUe(TEXT("髪２")).Equals(TEXT("髪2"), ESearchCase::CaseSensitive));
		TestTrue(TEXT("6: 集合外の センター → センター"), M.ToUe(TEXT("センター")).Equals(TEXT("センター"), ESearchCase::CaseSensitive));
	}

	return true;
}

} // namespace MmdPhysicsTests_MmdUeNameGoldenTest

#endif // WITH_DEV_AUTOMATION_TESTS
