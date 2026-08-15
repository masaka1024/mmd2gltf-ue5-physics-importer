// Copyright (c) 2026 masaka1024. MIT License.
//
// マテリアル情報の読み取り検証。
//   MmdPhysics.Core.MaterialReader   実モデル (MMD_PARITY_GLB) が要る。未設定ならスキップ
//   MmdPhysics.Core.GlbImageExtract  手で組んだ GLB で完結する (常に走る)

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "MmdGlbMaterialReader.h"

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

	/**
	 * JSON と BIN から GLB (glTF binary) のバイト列を組み立てる。
	 * チャンクは 4 バイト境界に揃える必要があり、JSON はスペース、BIN は 0 で埋める (glTF 仕様)。
	 */
	TArray<uint8> MakeGlb(const FString& Json, const TArray<uint8>& Bin)
	{
		const FTCHARToUTF8 JsonUtf8(*Json);
		TArray<uint8> JsonChunk;
		JsonChunk.Append(reinterpret_cast<const uint8*>(JsonUtf8.Get()), JsonUtf8.Length());
		while (JsonChunk.Num() % 4 != 0) JsonChunk.Add(0x20);

		TArray<uint8> BinChunk = Bin;
		while (BinChunk.Num() % 4 != 0) BinChunk.Add(0x00);

		TArray<uint8> Glb;
		AppendU32LE(Glb, 0x46546C67);                                   // "glTF"
		AppendU32LE(Glb, 2);                                            // version
		AppendU32LE(Glb, 12 + 8 + JsonChunk.Num() + 8 + BinChunk.Num()); // total length
		AppendU32LE(Glb, JsonChunk.Num());
		AppendU32LE(Glb, 0x4E4F534A);                                   // "JSON"
		Glb.Append(JsonChunk);
		AppendU32LE(Glb, BinChunk.Num());
		AppendU32LE(Glb, 0x004E4942);                                   // "BIN\0"
		Glb.Append(BinChunk);
		return Glb;
	}
}

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

// ===========================================================================
// alphaCutoff / origTexture の読み取りと、GLB バイナリからの画像抽出。
// 手で組んだ GLB で完結するので、モデルを持たない環境でも走る。
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdGlbImageExtractTest, "MmdPhysics.Core.GlbImageExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdGlbImageExtractTest::RunTest(const FString& Parameters)
{
	// bufferView 1 は byteOffset=8 にしてある (bufferView がバッファ先頭から始まる
	// という思い込みがあると、ここでずれた画像を切り出す)。
	const FString Json =
		TEXT("{\"asset\":{\"version\":\"2.0\"},")
		TEXT("\"buffers\":[{\"byteLength\":12}],")
		TEXT("\"bufferViews\":[")
		TEXT("{\"buffer\":0,\"byteOffset\":0,\"byteLength\":4},")
		TEXT("{\"buffer\":0,\"byteOffset\":8,\"byteLength\":3}],")
		TEXT("\"images\":[")
		TEXT("{\"name\":\"face.png\",\"mimeType\":\"image/png\",\"bufferView\":0},")
		TEXT("{\"name\":\"face_orig.png\",\"bufferView\":1}],")
		TEXT("\"textures\":[{\"source\":0},{\"source\":1}],")
		TEXT("\"materials\":[")
		TEXT("{\"name\":\"face\",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.25,")
		TEXT("\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}},")
		TEXT("\"extras\":{\"mmd\":{\"alphaClass\":\"blend\",\"origTexture\":1,\"sphereMode\":0,\"toonShared\":0}}},")
		TEXT("{\"name\":\"body\",\"alphaMode\":\"MASK\",")
		TEXT("\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.25,0.125,0.7]},")
		TEXT("\"extras\":{\"mmd\":{\"alphaClass\":\"mask\",\"origTexture\":-1,\"sphereMode\":0}}}]}");

	const TArray<uint8> Bin = { 1, 2, 3, 4, 0xEE, 0xEE, 0xEE, 0xEE, 9, 8, 7, 0 };
	const TArray<uint8> Glb = MakeGlb(Json, Bin);

	// --- マテリアル側: alphaCutoff と origTexture ---
	{
		MmdMaterialSet Set;
		TArray<FString> Warnings;
		if (!TestTrue(TEXT("組み立てた GLB を読める"), GlbMaterialReader::LoadBytes(Glb, Set, Warnings)))
		{
			for (const FString& W : Warnings) AddError(W);
			return false;
		}
		if (!TestEqual(TEXT("マテリアルが 2 つ"), Set.Materials.Num(), 2)) return false;

		const MmdMaterialInfo& Face = Set.Materials[0];
		const MmdMaterialInfo& Body = Set.Materials[1];

		TestEqual(TEXT("alphaCutoff を読める"), Face.AlphaCutoff, 0.25f);
		TestEqual(TEXT("alphaCutoff は書いてあればそのまま使う"), Face.EffectiveAlphaCutoff(), 0.25f);
		TestEqual(TEXT("alphaCutoff 未記載は 0 のまま"), Body.AlphaCutoff, 0.0f);
		TestEqual(TEXT("alphaCutoff 未記載は glTF 既定の 0.5 として使う"), Body.EffectiveAlphaCutoff(), 0.5f);

		// ★テクスチャを持たない材質はここにしか色が無い (レンズ・金属パーツ・単色のベルト)。
		TestEqual(TEXT("baseColorFactor を読める (R)"), Body.BaseColorFactor[0], 0.5f);
		TestEqual(TEXT("baseColorFactor を読める (G)"), Body.BaseColorFactor[1], 0.25f);
		TestEqual(TEXT("baseColorFactor を読める (B)"), Body.BaseColorFactor[2], 0.125f);
		TestEqual(TEXT("baseColorFactor のアルファを読める"), Body.BaseColorFactor[3], 0.7f);
		TestEqual(TEXT("baseColorFactor 未記載は白のまま (R)"), Face.BaseColorFactor[0], 1.0f);
		TestEqual(TEXT("baseColorFactor 未記載は白のまま (A)"), Face.BaseColorFactor[3], 1.0f);
		TestEqual(TEXT("baseColorTexture が無ければ -1"), Body.BaseColorTexture, -1);

		TestEqual(TEXT("origTexture を読める"), Face.OrigTexture, 1);
		TestEqual(TEXT("origTexture 無しは -1"), Body.OrigTexture, -1);
		TestTrue(TEXT("alphaClass=blend を真の半透明と判定する"), Face.IsAlphaClassBlend());
		TestFalse(TEXT("alphaClass=mask は真の半透明ではない"), Body.IsAlphaClassBlend());
	}

	// --- 画像抽出 ---
	{
		GlbImageExtractor Extractor;
		TArray<FString> Warnings;
		if (!TestTrue(TEXT("画像抽出の準備ができる"), Extractor.PrepareBytes(Glb, Warnings)))
		{
			for (const FString& W : Warnings) AddError(W);
			return false;
		}
		TestEqual(TEXT("テクスチャが 2 枚"), Extractor.NumTextures(), 2);

		MmdGlbImage Image;
		TArray<FString> W0;
		if (TestTrue(TEXT("プリベイク版を切り出せる"), Extractor.ExtractTexture(0, Image, W0)))
		{
			TestEqual(TEXT("画像名"), Image.Name, FString(TEXT("face.png")));
			TestEqual(TEXT("バイト数"), Image.Bytes.Num(), 4);
			const TArray<uint8> Expected0 = { 1, 2, 3, 4 };
			TestTrue(TEXT("中身が bufferView のとおり"), Image.Bytes == Expected0);
			TestEqual(TEXT("mimeType から拡張子を決める"), Image.Extension(), FString(TEXT("png")));
		}

		MmdGlbImage OrigImage;
		TArray<FString> W1;
		if (TestTrue(TEXT("無加工版 (origTexture) を切り出せる"), Extractor.ExtractTexture(1, OrigImage, W1)))
		{
			// byteOffset=8 から 3 バイト。0 から読んでいるとここで落ちる。
			const TArray<uint8> Expected1 = { 9, 8, 7 };
			TestTrue(TEXT("bufferView.byteOffset が効いている"), OrigImage.Bytes == Expected1);
			// mimeType が無い画像。先頭バイトが JPEG でなければ png 扱いにする。
			TestEqual(TEXT("mimeType 欠損時は先頭バイトで判別する"), OrigImage.Extension(), FString(TEXT("png")));
		}

		// 範囲外は黙って壊れた画像を返さず、false + 警告で返る。
		MmdGlbImage Bogus;
		TArray<FString> W2;
		TestFalse(TEXT("範囲外のテクスチャ番号は false"), Extractor.ExtractTexture(99, Bogus, W2));
		TestTrue(TEXT("範囲外のときは理由が警告に残る"), W2.Num() > 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
