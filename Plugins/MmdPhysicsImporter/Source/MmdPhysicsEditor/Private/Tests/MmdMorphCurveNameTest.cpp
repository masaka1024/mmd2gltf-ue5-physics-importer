// Copyright (c) 2026 masaka1024. MIT License.
//
// モーフ名がカーブに載るかどうかの判定。データ不要 (常に走る)。
//   MmdPhysics.Editor.MorphCurveName
//
// ★何を守っているか。
//   UE 5.5 の AnimSequence は float カーブの実体を FK ControlRig のチャンネルで持ち、
//   その引き当てに **サニタイズした名前** を使う (英数字と _ - . | 以外は `_` に潰れる)。
//   `▲` と `∧` はどちらも `_` に潰れて同じチャンネルを掴むので、
//   両方をカーブにすると UE 5.5 は check に引っかかって**エディタごと落ちる**
//   (AnimSequencerController.cpp の
//    `check(bUpdateKey || ...FindKey(FrameNumber) == INDEX_NONE)`)。
//   なので ApplyMorphCurves は「サニタイズで名前が変わるモーフ」を足さない。
//   このテストはその判定 (MakeRigSafeName) が MMD の実際のモーフ名で
//   期待どおりに効くかを見る。詳しい経緯は MmdMorphAnimation.h の注記。

#include "Misc/AutomationTest.h"
#include "MmdMorphAnimation.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdMorphCurveNameTest, "MmdPhysics.Editor.MorphCurveName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdMorphCurveNameTest::RunTest(const FString& Parameters)
{
	auto IsSafe = [](const TCHAR* Name)
	{
		return FMmdMorphAnimation::MakeRigSafeName(FString(Name)) == FString(Name);
	};

	// --- 通る名前: MMD の表情モーフの大半は仮名・漢字なので無事 ---
	// ここが落ちると「顔が全く動かない」に化けるので、代表的なものを固定で見る。
	TestTrue(TEXT("まばたき"), IsSafe(TEXT("まばたき")));
	TestTrue(TEXT("笑い"), IsSafe(TEXT("笑い")));
	TestTrue(TEXT("ウィンク"), IsSafe(TEXT("ウィンク")));
	TestTrue(TEXT("英数字"), IsSafe(TEXT("Blink2")));
	TestTrue(TEXT("_ - . | は使える"), IsSafe(TEXT("a_b-c.d|e")));
	TestTrue(TEXT("2 文字目以降の空白は使える"), IsSafe(TEXT("Mouth Open")));

	// --- 判定が処理系まかせで読めないもの ---
	// ★MakeRigSafeName は FChar::IsAlpha / IsDigit (= iswalpha / iswdigit) を使う。
	//   全角数字や半角カナがどちらに入るかは処理系の分類表しだいで、
	//   こちらで決められない。決め打ちで通す/落とすと嘘になるので、
	//   固定はせず**実際の判定を記録**するだけにする。
	//   ここに出た名前が「載らない」なら、そのモーフは飛ばされている。
	for (const TCHAR* Name : { TEXT("ウィンク２"), TEXT("ｷﾘｯ"), TEXT("ω"), TEXT("・"), TEXT("￥") })
	{
		AddInfo(FString::Printf(TEXT("'%s' → '%s' (%s)"), Name,
			*FMmdMorphAnimation::MakeRigSafeName(FString(Name)),
			IsSafe(Name) ? TEXT("載る") : TEXT("載らない")));
	}

	// --- 落ちる名前: 記号だけのモーフ。MMD 標準の口の形に実在する ---
	TestFalse(TEXT("▲ は載らない"), IsSafe(TEXT("▲")));
	TestFalse(TEXT("∧ は載らない"), IsSafe(TEXT("∧")));
	TestFalse(TEXT("□ は載らない"), IsSafe(TEXT("□")));
	TestFalse(TEXT("！を含む名前は載らない"), IsSafe(TEXT("はんっ！")));
	TestFalse(TEXT("先頭の空白は載らない"), IsSafe(TEXT(" Mouth")));

	// --- 衝突すること自体の確認 (これが起きるとエディタが落ちる) ---
	// ★ここが「同じ名前になる」ことを確かめておかないと、
	//   除外を消したときに何が起きるのか分からなくなる。
	const FString Triangle = FMmdMorphAnimation::MakeRigSafeName(TEXT("▲"));
	const FString Wedge = FMmdMorphAnimation::MakeRigSafeName(TEXT("∧"));
	const FString Square = FMmdMorphAnimation::MakeRigSafeName(TEXT("□"));
	TestEqual(TEXT("▲ と ∧ は同じリグ名に潰れる"), Triangle, Wedge);
	TestEqual(TEXT("▲ と □ は同じリグ名に潰れる"), Triangle, Square);

	// --- 潰し方そのもの: 記号 1 文字が _ 1 文字になり、前後は残る ---
	TestEqual(TEXT("記号は 1 文字ずつ _ になる"),
		FMmdMorphAnimation::MakeRigSafeName(TEXT("a▲b")), FString(TEXT("a_b")));

	// --- 化けた既存カーブを見分けられるか (CollectRigMangledNames) ---
	// ★何を守っているか。
	//   Interchange が weights を取り込めた .glb では、記号モーフのカーブが
	//   **化けた名前のまま AnimSequence に残る** (Tda式 V4X の実例:
	//   `▲` `∧` `□` → `_` / `恐ろしい子！` → `恐ろしい子_`)。
	//   ApplyMorphCurves はこれを消すが、判定に使えるのはカーブ名だけで、
	//   化けた名前は既にリグ規則に載っている (MakeRigSafeName を通しても変わらない)。
	//   そこで**元のモーフ名から化けた姿を作って**突き合わせる。
	//   ここが壊れると、消せない (BuildActor が Fail し続ける) か、
	//   実在のモーフのカーブを消す (顔が動かなくなる) かのどちらかになる。
	{
		const TArray<FString> MorphNames = {
			TEXT("▲"), TEXT("∧"), TEXT("□"), TEXT("恐ろしい子！"), TEXT("まばたき"), TEXT("笑い") };
		const TSet<FName> Mangled = FMmdMorphAnimation::CollectRigMangledNames(MorphNames);

		TestTrue(TEXT("記号モーフの化けた名前 '_' が入る"), Mangled.Contains(FName(TEXT("_"))));
		TestTrue(TEXT("'恐ろしい子！' の化けた名前が入る"),
			Mangled.Contains(FName(TEXT("恐ろしい子_"))));

		// ★載る名前を入れてはいけない。入れると、実在のモーフと同じ名前のカーブが
		//   削除の候補に挙がってしまう。
		TestFalse(TEXT("載る名前は入らない (まばたき)"), Mangled.Contains(FName(TEXT("まばたき"))));
		TestFalse(TEXT("載る名前は入らない (笑い)"), Mangled.Contains(FName(TEXT("笑い"))));

		// 3 つの記号は 1 本に潰れるので、6 件入れても像は 2 件。
		TestEqual(TEXT("潰れたぶんは 1 つにまとまる"), Mangled.Num(), 2);

		// モーフ名が `_` そのものだった場合 (サニタイズで変わらない) は像を作らない。
		// 化けたカーブと区別が付かなくなるため。
		const TArray<FString> AlreadySafe = { TEXT("_") };
		TestFalse(TEXT("元から '_' のモーフは像にしない"),
			FMmdMorphAnimation::CollectRigMangledNames(AlreadySafe).Contains(FName(TEXT("_"))));
	}

	// --- 長さの上限 (URigHierarchy::GetMaxNameLength() = 100) ---
	const FString Long = FString::ChrN(120, TEXT('a'));
	TestEqual(TEXT("101 文字以上は 100 文字に切られる"),
		FMmdMorphAnimation::MakeRigSafeName(Long).Len(), 100);
	TestFalse(TEXT("切られる名前は載らない"), IsSafe(*Long));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
