// Copyright (c) 2026 masaka1024. MIT License.
//
// 取り込み用 .glb の半角化。手で組んだ GLB で完結する (常に走る)。
//   MmdPhysics.Editor.GlbNameNormalize
//
// ★何を守っているか。
//   1. 直すのは**ボーン名とモーフ名の全角数字だけ**。マテリアル名も仮名・漢字も触らない。
//   2. BIN チャンクは 1 バイトも変わらない (メッシュ・テクスチャを壊さない)。
//   3. 半角化で名前がぶつかるなら**一意な名前へ振り分けて**続ける (中止しない)。
//      黙って潰すと取り込みでトラックが落ちる元の症状に戻るので、警告は必ず出す。
//      振り分けの詳細は MmdPhysics.Editor.UeNameCollision で見る。
//   4. `１` と書かれたエスケープ形式も拾う (JSON の綴りはエクスポーター次第)。

#include "Misc/AutomationTest.h"
#include "MmdGlbNameNormalize.h"
#include "MmdGlbPhysicsReader.h"

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

	/** JSON と BIN から GLB を組む (チャンクは 4 バイト境界、JSON は空白・BIN は 0 で詰める)。 */
	TArray<uint8> MakeGlb(const FString& Json, const TArray<uint8>& Bin)
	{
		const FTCHARToUTF8 JsonUtf8(*Json);
		TArray<uint8> JsonChunk;
		JsonChunk.Append(reinterpret_cast<const uint8*>(JsonUtf8.Get()), JsonUtf8.Length());
		while (JsonChunk.Num() % 4 != 0) JsonChunk.Add(0x20);

		TArray<uint8> BinChunk = Bin;
		while (BinChunk.Num() % 4 != 0) BinChunk.Add(0x00);

		TArray<uint8> Glb;
		AppendU32LE(Glb, 0x46546C67);                                    // "glTF"
		AppendU32LE(Glb, 2);
		AppendU32LE(Glb, 12 + 8 + JsonChunk.Num() + 8 + BinChunk.Num());
		AppendU32LE(Glb, JsonChunk.Num());
		AppendU32LE(Glb, 0x4E4F534A);                                    // "JSON"
		Glb.Append(JsonChunk);
		AppendU32LE(Glb, BinChunk.Num());
		AppendU32LE(Glb, 0x004E4942);                                    // "BIN\0"
		Glb.Append(BinChunk);
		return Glb;
	}

	/** GLB から JSON チャンクを文字列で取り出す (検証用)。 */
	FString JsonOf(const TArray<uint8>& Glb)
	{
		int32 Off = 12;
		while (Off + 8 <= Glb.Num())
		{
			const int32 Clen = static_cast<int32>(
				Glb[Off] | (Glb[Off + 1] << 8) | (Glb[Off + 2] << 16) | (static_cast<uint32>(Glb[Off + 3]) << 24));
			const uint32 Ctype = static_cast<uint32>(
				Glb[Off + 4] | (Glb[Off + 5] << 8) | (Glb[Off + 6] << 16) | (static_cast<uint32>(Glb[Off + 7]) << 24));
			const int32 Cdata = Off + 8;
			if (Cdata + Clen > Glb.Num()) break;
			if (Ctype == 0x4E4F534A)
			{
				return FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Glb.GetData() + Cdata), Clen));
			}
			Off = Cdata + Clen;
			if ((Clen & 3) != 0) Off += 4 - (Clen & 3);
		}
		return FString();
	}

	/** GLB から BIN チャンクを取り出す (検証用)。 */
	TArray<uint8> BinOf(const TArray<uint8>& Glb)
	{
		TArray<uint8> Out;
		int32 Off = 12;
		while (Off + 8 <= Glb.Num())
		{
			const int32 Clen = static_cast<int32>(
				Glb[Off] | (Glb[Off + 1] << 8) | (Glb[Off + 2] << 16) | (static_cast<uint32>(Glb[Off + 3]) << 24));
			const uint32 Ctype = static_cast<uint32>(
				Glb[Off + 4] | (Glb[Off + 5] << 8) | (Glb[Off + 6] << 16) | (static_cast<uint32>(Glb[Off + 7]) << 24));
			const int32 Cdata = Off + 8;
			if (Cdata + Clen > Glb.Num()) break;
			if (Ctype == 0x004E4942)
			{
				Out.Append(Glb.GetData() + Cdata, Clen);
				return Out;
			}
			Off = Cdata + Clen;
			if ((Clen & 3) != 0) Off += 4 - (Clen & 3);
		}
		return Out;
	}

	/**
	 * PMX の指ボーンを模した最小の glTF。
	 * ボーン名に全角数字、モーフ名にも全角数字、マテリアル名にも全角数字を入れてある
	 * (マテリアルは**直ってはいけない**側の見張り)。
	 */
	FString MakeJson()
	{
		return TEXT(R"({
"asset":{"version":"2.0"},
"nodes":[
 {"name":"センター"},
 {"name":"右人指１"},
 {"name":"右人指２"},
 {"name":"メッシュ","mesh":0}
],
"skins":[{"joints":[0,1,2]}],
"meshes":[{"name":"IA","extras":{"targetNames":["まばたき","あ１","あ２"]}}],
"materials":[{"name":"肌２"}],
"extras":{"mmd":{"unitScale":0.08}}
})");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdGlbNameNormalizeTest, "MmdPhysics.Editor.GlbNameNormalize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdGlbNameNormalizeTest::RunTest(const FString& Parameters)
{
	// --- 1. ボーン名とモーフ名だけが半角になる ---
	{
		TArray<uint8> Bin;
		for (int32 i = 0; i < 10; ++i) Bin.Add(static_cast<uint8>(i * 7 + 1));

		const TArray<uint8> In = MakeGlb(MakeJson(), Bin);
		TArray<uint8> Out;
		const FMmdGlbNormalizeResult R = FMmdGlbNameNormalize::NormalizeBytes(In, Out);

		TestTrue(TEXT("半角化に成功する"), R.bSuccess);
		TestEqual(TEXT("ボーンは 2 件直る"), R.BonesRenamed, 2);
		TestEqual(TEXT("モーフは 2 件直る"), R.MorphsRenamed, 2);
		TestEqual(TEXT("衝突は無い"), R.Collisions.Num(), 0);

		const FString J = JsonOf(Out);
		TestTrue(TEXT("右人指1 になっている"), J.Contains(TEXT("右人指1")));
		TestTrue(TEXT("右人指2 になっている"), J.Contains(TEXT("右人指2")));
		TestFalse(TEXT("全角の右人指１ は残らない"), J.Contains(TEXT("右人指１")));
		TestTrue(TEXT("モーフ あ1 になっている"), J.Contains(TEXT("あ1")));
		TestFalse(TEXT("全角の あ１ は残らない"), J.Contains(TEXT("あ１")));

		// ★マテリアル名は対象外。サニタイズの経路に乗らないので直す理由が無い。
		TestTrue(TEXT("マテリアル名 肌２ は全角のまま"), J.Contains(TEXT("肌２")));

		// 触っていない側も確かめる。
		TestTrue(TEXT("センターはそのまま"), J.Contains(TEXT("センター")));

		// --- 2. BIN チャンクは 1 バイトも変わらない ---
		//   ★元の Bin 配列とではなく **入力 GLB から取り出した BIN** と比べる。
		//     MakeGlb はチャンク長に 4 バイト境界の詰め物を含めた長さを書くので
		//     (既存テストのヘルパと同じ作り)、Bin そのものとは長さが合わない。
		//     ここで見たいのは「半角化を通しても BIN が変わらないこと」なので、
		//     入力から取り出したものと突き合わせるのが正しい。
		const TArray<uint8> BinIn = BinOf(In);
		const TArray<uint8> BinOut = BinOf(Out);
		TestTrue(TEXT("入力に BIN がある"), BinIn.Num() > 0);
		TestEqual(TEXT("BIN の長さが同じ"), BinOut.Num(), BinIn.Num());
		TestTrue(TEXT("BIN が元のまま"), BinOut == BinIn);

		// --- 対応表が原本名を保っている ---
		const FString* Mapped = R.NameMap.Find(TEXT("右人指１"));
		TestTrue(TEXT("対応表に原本名がある"), Mapped != nullptr);
		if (Mapped != nullptr) TestEqual(TEXT("対応表の値が半角"), *Mapped, FString(TEXT("右人指1")));
	}

	// --- 3. 半角化した GLB を物理リーダが読める。ボーン名も半角で返る ---
	{
		TArray<uint8> Out;
		FMmdGlbNameNormalize::NormalizeBytes(MakeGlb(MakeJson(), TArray<uint8>()), Out);

		float UnitScale = 0.0f;
		TArray<FString> Warnings;
		const TSharedPtr<PmxPhysicsModel> Model = GlbPhysicsReader::LoadBytes(Out, UnitScale, Warnings);

		TestTrue(TEXT("半角化後も GLB として読める"), Model.IsValid());
		if (Model.IsValid())
		{
			TestEqual(TEXT("ボーン 3 本"), Model->BoneNames.Num(), 3);
			if (Model->BoneNames.Num() == 3)
			{
				TestEqual(TEXT("リーダから見ても半角"), Model->BoneNames[1], FString(TEXT("右人指1")));
			}
		}
	}

	// --- 4. 直すところが無ければ原本のまま ---
	{
		const FString Json = TEXT(R"({"asset":{"version":"2.0"},"nodes":[{"name":"センター"}],"skins":[{"joints":[0]}]})");
		const TArray<uint8> In = MakeGlb(Json, TArray<uint8>());
		TArray<uint8> Out;
		const FMmdGlbNormalizeResult R = FMmdGlbNameNormalize::NormalizeBytes(In, Out);

		TestTrue(TEXT("成功する"), R.bSuccess);
		TestTrue(TEXT("書き換え 0 件"), R.IsNoOp());
		TestTrue(TEXT("バイト列が原本と同一"), Out == In);
	}

	// --- 5. 半角化するとぶつかる場合は振り分けて続ける ---
	{
		// `右人指1` (半角) が既にあるところへ `右人指１` (全角) を入れる。
		const FString Json = TEXT(R"({"asset":{"version":"2.0"},
"nodes":[{"name":"右人指1"},{"name":"右人指１"}],"skins":[{"joints":[0,1]}]})");
		AddExpectedMessage(TEXT("半角化で '右人指1' に重なる"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
		TArray<uint8> Out;
		const FMmdGlbNormalizeResult R = FMmdGlbNameNormalize::NormalizeBytes(MakeGlb(Json, TArray<uint8>()), Out);

		TestTrue(TEXT("中止しない"), R.bSuccess);
		TestEqual(TEXT("衝突を 1 件報告する"), R.Collisions.Num(), 1);
		const FString* Mapped = R.NameMap.Find(TEXT("右人指１"));
		TestTrue(TEXT("全角側が振り分けられる"), Mapped != nullptr && *Mapped == TEXT("右人指1_2"));
	}

	// --- 6. １ のエスケープ形式でも拾う ---
	{
		const FString Json = TEXT("{\"asset\":{\"version\":\"2.0\"},")
			TEXT("\"nodes\":[{\"name\":\"\\u53f3\\u4eba\\u6307\\uFF11\"}],\"skins\":[{\"joints\":[0]}]}");
		TArray<uint8> Out;
		const FMmdGlbNormalizeResult R = FMmdGlbNameNormalize::NormalizeBytes(MakeGlb(Json, TArray<uint8>()), Out);

		TestTrue(TEXT("成功する"), R.bSuccess);
		TestEqual(TEXT("ボーン 1 件直る"), R.BonesRenamed, 1);

		float UnitScale = 0.0f;
		TArray<FString> Warnings;
		const TSharedPtr<PmxPhysicsModel> Model = GlbPhysicsReader::LoadBytes(Out, UnitScale, Warnings);
		TestTrue(TEXT("読める"), Model.IsValid());
		if (Model.IsValid() && Model->BoneNames.Num() == 1)
		{
			TestEqual(TEXT("エスケープ形式でも半角になる"), Model->BoneNames[0], FString(TEXT("右人指1")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
