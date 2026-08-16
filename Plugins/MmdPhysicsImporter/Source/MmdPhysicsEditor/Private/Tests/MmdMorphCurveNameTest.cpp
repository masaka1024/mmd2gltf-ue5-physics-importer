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

	// --- 長さの上限 (URigHierarchy::GetMaxNameLength() = 100) ---
	const FString Long = FString::ChrN(120, TEXT('a'));
	TestEqual(TEXT("101 文字以上は 100 文字に切られる"),
		FMmdMorphAnimation::MakeRigSafeName(Long).Len(), 100);
	TestFalse(TEXT("切られる名前は載らない"), IsSafe(*Long));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
