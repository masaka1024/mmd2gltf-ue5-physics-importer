// Copyright (c) 2026 masaka1024. MIT License.
//
// 「translation を持って良いボーン」の判定。データ不要 (常に走る)。
//   MmdPhysics.Editor.BoneTranslationFlags
//
// ★何を守っているか。
//   修正前の mmd2gltf-gui (`aa5cc7b` より前) は、フルキー VMD に残っていた
//   移動値を回転専用ボーンへも適用し、glTF の translation チャンネルとして
//   出していた。取り込むと鎖のボーン間距離が参照ポーズから外れる。
//   FMmdBoneTranslationFix はそれを参照ポーズへ戻すが、**戻して良いボーンを
//   間違えると正当な並進 (移動可ボーン・物理ベイク) まで殺す**ので、
//   判定そのものをここで固定する。
//
//   併せて「古い .glb でも判定できる」ことを見る。保険が効くのは
//   `extras.mmd.bones` を持たない古い出力なので、`nodes[].extras.mmd.flags`
//   からの読み取りが動かないと保険自体が無意味になる。
//   詳しい経緯は MmdBoneTranslationFix.h の注記。
//
// ★MiniJson::Parse の戻り値は必ず変数で受けること。
//   MiniJson::Obj() が返すのは TSharedPtr が持つ実体への**ポインタ**なので、
//   Obj(Parse(...)) と書くと一時オブジェクトが即座に解放されて宙を指す。

#include "Misc/AutomationTest.h"
#include "MmdBoneTranslationFix.h"
#include "MmdMiniJson.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdPhysics;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdBoneTranslationFlagsTest, "MmdPhysics.Editor.BoneTranslationFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdBoneTranslationFlagsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("移動可フラグは PMX の 0x0004"), FMmdBoneTranslationFix::KBoneFlagMovable, 0x0004);

	// --- (1) 新しい .glb: extras.mmd.bones から読む ---
	// センター     flags 0x001F … 移動可 → 並進は正当
	// しっぽ１     flags 0x2001 … 回転のみ + mode0 剛体 → 並進は不正
	// しっぽ２     flags 0x2001 … 回転のみ + mode1 剛体 → 物理ベイクが焼くので正当
	{
		const FString Json = TEXT(R"({
			"extras": { "mmd": {
				"bones": [
					{ "name": "センター",  "flags": 31 },
					{ "name": "しっぽ１", "flags": 8193 },
					{ "name": "しっぽ２", "flags": 8193 }
				],
				"rigidBodies": [
					{ "name": "rb-tail1", "bone": 1, "mode": 0 },
					{ "name": "rb-tail2", "bone": 2, "mode": 1 }
				]
			} }
		})");
		const TSharedPtr<MmdJsonValue> RootVal = MiniJson::Parse(Json);
		const FMmdJsonObject* Root = MiniJson::Obj(RootVal);
		TArray<FMmdGlbBone> Bones;
		bool bFromBonesArray = false;
		TestTrue(TEXT("新しい .glb のボーンを読める"),
			FMmdBoneTranslationFix::ReadBones(Root, Bones, bFromBonesArray));
		TestTrue(TEXT("extras.mmd.bones から読んだと分かる"), bFromBonesArray);
		TestEqual(TEXT("ボーン 3 本"), Bones.Num(), 3);

		TSet<FString> Movable, Baked;
		FMmdBoneTranslationFix::CollectBoneCategories(Root, Bones, Movable, Baked);
		TestTrue(TEXT("移動可ボーンは常に残す側"), Movable.Contains(TEXT("センター")));
		TestFalse(TEXT("回転のみ + mode0 剛体はどちらにも入らない (無条件で戻す)"),
			Movable.Contains(TEXT("しっぽ１")) || Baked.Contains(TEXT("しっぽ１")));
		TestTrue(TEXT("mode1 剛体は物理ベイク側 (長さで判定する)"), Baked.Contains(TEXT("しっぽ２")));
		TestFalse(TEXT("mode1 剛体は移動可ではない"), Movable.Contains(TEXT("しっぽ２")));
	}

	// --- (2) 古い .glb: skins[0].joints → nodes[].extras.mmd.flags から読む ---
	// ★ボーン番号は joints の並びで決まる (nodes の並びではない)。
	//   剛体の bone=1 が「しっぽ１」を指すことをここで固定する。
	//   ここを nodes の添字で読むと、剛体が別のボーンに付いていることになる。
	{
		const FString Json = TEXT(R"({
			"nodes": [
				{ "name": "Armature", "children": [1] },
				{ "name": "センター", "extras": { "mmd": { "flags": 30 } }, "children": [2] },
				{ "name": "しっぽ１", "extras": { "mmd": { "flags": 8193 } } },
				{ "name": "Mesh" }
			],
			"skins": [ { "joints": [1, 2] } ],
			"extras": { "mmd": {
				"rigidBodies": [ { "name": "rb-tail1", "bone": 1, "mode": 2 } ]
			} }
		})");
		const TSharedPtr<MmdJsonValue> RootVal = MiniJson::Parse(Json);
		const FMmdJsonObject* Root = MiniJson::Obj(RootVal);
		TArray<FMmdGlbBone> Bones;
		bool bFromBonesArray = true;
		TestTrue(TEXT("古い .glb でもボーンを読める"),
			FMmdBoneTranslationFix::ReadBones(Root, Bones, bFromBonesArray));
		TestFalse(TEXT("nodes 側から読んだと分かる"), bFromBonesArray);
		if (TestEqual(TEXT("ボーンは joints の 2 本 (メッシュノードは入らない)"), Bones.Num(), 2))
		{
			TestEqual(TEXT("ボーン 0 は joints[0] のノード"), Bones[0].Name, FString(TEXT("センター")));
			TestEqual(TEXT("ボーン 1 は joints[1] のノード"), Bones[1].Name, FString(TEXT("しっぽ１")));
			TestEqual(TEXT("フラグはノードの extras.mmd から来る"), Bones[0].Flags, 30);
		}

		TSet<FString> Movable, Baked;
		FMmdBoneTranslationFix::CollectBoneCategories(Root, Bones, Movable, Baked);
		TestTrue(TEXT("移動可ボーンは常に残す側"), Movable.Contains(TEXT("センター")));
		TestTrue(TEXT("mode2 剛体も物理ベイク側"), Baked.Contains(TEXT("しっぽ１")));
	}

	// --- (3) 判定材料が無い .glb: 何も言わない (触らせない) ---
	// ★ここが true を返すと、フラグ 0 = 移動不可とみなして**全トラックを戻して**しまう。
	{
		const FString Json = TEXT(R"({
			"nodes": [ { "name": "センター" }, { "name": "しっぽ１" } ],
			"skins": [ { "joints": [0, 1] } ]
		})");
		const TSharedPtr<MmdJsonValue> RootVal = MiniJson::Parse(Json);
		const FMmdJsonObject* Root = MiniJson::Obj(RootVal);
		TArray<FMmdGlbBone> Bones;
		bool bFromBonesArray = false;
		TestFalse(TEXT("フラグがどこにも無ければ判定しない"),
			FMmdBoneTranslationFix::ReadBones(Root, Bones, bFromBonesArray));
		TestEqual(TEXT("読めなかったときは空で返す"), Bones.Num(), 0);
	}

	// --- (4) 剛体の bone が範囲外でも落ちない ---
	{
		const FString Json = TEXT(R"({
			"extras": { "mmd": {
				"bones": [ { "name": "しっぽ１", "flags": 8193 } ],
				"rigidBodies": [ { "name": "rb", "bone": 99, "mode": 1 } ]
			} }
		})");
		const TSharedPtr<MmdJsonValue> RootVal = MiniJson::Parse(Json);
		const FMmdJsonObject* Root = MiniJson::Obj(RootVal);
		TArray<FMmdGlbBone> Bones;
		bool bFromBonesArray = false;
		TestTrue(TEXT("ボーンは読める"),
			FMmdBoneTranslationFix::ReadBones(Root, Bones, bFromBonesArray));
		TSet<FString> Movable, Baked;
		FMmdBoneTranslationFix::CollectBoneCategories(Root, Bones, Movable, Baked);
		TestEqual(TEXT("範囲外の剛体は無視する"), Baked.Num(), 0);
		TestEqual(TEXT("移動可ボーンも無い"), Movable.Num(), 0);
	}

	// --- (5) 焼き損じの判定しきい値は ChainStability の合否と揃える ---
	// ★片方だけ動かすと「テストは落ちるのに保険は直さない (逆もある)」が起きる。
	TestEqual(TEXT("伸び側のしきい値は 2 倍"), FMmdBoneTranslationFix::KBakedLenRatioMax, 2.0);
	TestEqual(TEXT("縮み側のしきい値は 1/2 倍"), FMmdBoneTranslationFix::KBakedLenRatioMin, 0.5);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
