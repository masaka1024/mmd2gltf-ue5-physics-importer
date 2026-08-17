// Copyright (c) 2026 masaka1024. MIT License.
//
// 共有トゥーンが無いときの近似ランプの検証。
//   MmdPhysics.Editor.ToonRamp        テーブル → 画素の対応。データ不要 (常に走る)
//   MmdPhysics.Editor.ToonRampAsset   実際に UTexture2D アセットを作る経路。
//                                     MMD_TOON_RAMP_PACKAGE が未設定ならスキップ

#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Engine/Texture2D.h"
#include "MmdMaterialConversion.h"
#include "MmdToonRamp.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MmdToonRampConst;

// ===========================================================================
// テーブルの 4 パラメータ (明部色 / 陰部色 / 境界位置 / ぼかし幅) が、
// 生成される画素にそのまま出ているか。
//
// ★ここが崩れると「共有トゥーンが無くても陰が付く」という保証そのものが消える。
//   色の値自体は人間が目視で詰める前提なので固定値は問わず、
//   **テーブルと生成物の対応**だけを見る。
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdToonRampTest, "MmdPhysics.Editor.ToonRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdToonRampTest::RunTest(const FString& Parameters)
{
	// --- アセット名 (toonShared は 0 起点、名前は 1 起点) ---
	TestEqual(TEXT("toonShared=0 は T_MmdToonApprox01"), FMmdToonRamp::AssetNameFor(0), FString(TEXT("T_MmdToonApprox01")));
	TestEqual(TEXT("toonShared=9 は T_MmdToonApprox10"), FMmdToonRamp::AssetNameFor(9), FString(TEXT("T_MmdToonApprox10")));

	for (int32 Index = 0; Index < NumSharedToons; Index++)
	{
		const FMmdToonRampDef& Def = FMmdToonRamp::GetDef(Index);
		const FString Tag = FString::Printf(TEXT("toon%02d"), Index + 1);

		// ★帯 (ハイライトの筋) を持つランプは、明部→陰部の単調な遷移ではなくなる。
		//   下の「間にある / 単調 / 平坦」の 3 つは帯の内側では成り立たないので、
		//   帯の範囲だけ除いて検査する (帯そのものは後段で個別に見る)。
		const bool bHasBand = Def.BandWidth > 0.0f;
		const float BandHalf = Def.BandWidth * 0.5f;

		// --- テーブルの値が「ランプとして成立する」範囲にあるか ---
		// ★ぼかしが端まではみ出すと、最上段/最下段が明部色/陰部色そのものにならない。
		//   目視調整でうっかり 0.9 のような境界を入れたときにここで落ちる。
		const float Half = Def.Softness * 0.5f;
		TestTrue(*FString::Printf(TEXT("%s: ぼかし幅が正"), *Tag), Def.Softness > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s: 境界のぼかしが上端をはみ出さない"), *Tag), Def.Boundary - Half > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s: 境界のぼかしが下端をはみ出さない"), *Tag), Def.Boundary + Half < 1.0f);

		// --- 境界位置: ちょうど境界で明部色と陰部色の中間になる ---
		{
			const FColor Mid = FMmdToonRamp::Sample(Def, Def.Boundary);
			auto MidOf = [](uint8 A, uint8 B) -> uint8
			{
				return static_cast<uint8>(FMath::RoundToInt((static_cast<float>(A) + static_cast<float>(B)) * 0.5f));
			};
			TestEqual(*FString::Printf(TEXT("%s: 境界 V=%.3f の R が中間色"), *Tag, Def.Boundary),
				(int32)Mid.R, (int32)MidOf(Def.Light.R, Def.Shadow.R));
			TestEqual(*FString::Printf(TEXT("%s: 境界 V=%.3f の G が中間色"), *Tag, Def.Boundary),
				(int32)Mid.G, (int32)MidOf(Def.Light.G, Def.Shadow.G));
			TestEqual(*FString::Printf(TEXT("%s: 境界 V=%.3f の B が中間色"), *Tag, Def.Boundary),
				(int32)Mid.B, (int32)MidOf(Def.Light.B, Def.Shadow.B));
		}

		// --- 生成画素 ---
		TArray<FColor> Pixels;
		FMmdToonRamp::BuildPixels(Def, Pixels);
		if (!TestEqual(*FString::Printf(TEXT("%s: 画素数が %dx%d"), *Tag, RampSize, RampSize),
			Pixels.Num(), RampSize * RampSize))
		{
			continue;
		}

		// 最上段 = 明部色 / 最下段 = 陰部色。
		// ★マテリアルは ToonUV = (0.5, 1 - saturate(N・L)) で引くので、
		//   V=0 (上端) が明部、V=1 (下端) が陰部。上下が逆だと陰影が反転する。
		const FColor Top = Pixels[0];
		const FColor Bottom = Pixels[(RampSize - 1) * RampSize];
		TestEqual(*FString::Printf(TEXT("%s: 最上段が明部色 (R)"), *Tag), (int32)Top.R, (int32)Def.Light.R);
		TestEqual(*FString::Printf(TEXT("%s: 最上段が明部色 (G)"), *Tag), (int32)Top.G, (int32)Def.Light.G);
		TestEqual(*FString::Printf(TEXT("%s: 最上段が明部色 (B)"), *Tag), (int32)Top.B, (int32)Def.Light.B);
		TestEqual(*FString::Printf(TEXT("%s: 最下段が陰部色 (R)"), *Tag), (int32)Bottom.R, (int32)Def.Shadow.R);
		TestEqual(*FString::Printf(TEXT("%s: 最下段が陰部色 (G)"), *Tag), (int32)Bottom.G, (int32)Def.Shadow.G);
		TestEqual(*FString::Printf(TEXT("%s: 最下段が陰部色 (B)"), *Tag), (int32)Bottom.B, (int32)Def.Shadow.B);
		TestEqual(*FString::Printf(TEXT("%s: 不透明"), *Tag), (int32)Top.A, 255);

		// 横方向は一様 (マテリアルは U=0.5 でしか引かないが、値が横に振れていないこと)。
		int32 RowVaried = 0, OutOfRange = 0, NotMonotonic = 0, PlateauBroken = 0;
		int32 PrevDistToShadow = MAX_int32;
		for (int32 Y = 0; Y < RampSize; Y++)
		{
			const FColor Row = Pixels[Y * RampSize];
			for (int32 X = 1; X < RampSize; X++)
			{
				if (Pixels[Y * RampSize + X] != Row) RowVaried++;
			}

			// 横方向の一様さは帯があっても崩れない (帯は V にしか効かない) ので常に見る。
			const float V = static_cast<float>(Y) / static_cast<float>(RampSize - 1);
			if (bHasBand && FMath::Abs(V - Def.BandCenter) < BandHalf)
			{
				continue;   // 帯の内側は下の 3 つの対象外
			}

			// 明部色と陰部色の間に収まっているか (各チャンネル)。
			auto Between = [](uint8 Value, uint8 A, uint8 B)
			{
				return Value >= FMath::Min(A, B) && Value <= FMath::Max(A, B);
			};
			if (!Between(Row.R, Def.Light.R, Def.Shadow.R)
				|| !Between(Row.G, Def.Light.G, Def.Shadow.G)
				|| !Between(Row.B, Def.Light.B, Def.Shadow.B))
			{
				OutOfRange++;
			}

			// 下へ行くほど陰部色へ寄る (行き過ぎて戻らない)。
			const int32 DistToShadow = FMath::Abs((int32)Row.R - (int32)Def.Shadow.R)
				+ FMath::Abs((int32)Row.G - (int32)Def.Shadow.G)
				+ FMath::Abs((int32)Row.B - (int32)Def.Shadow.B);
			if (DistToShadow > PrevDistToShadow) NotMonotonic++;
			PrevDistToShadow = DistToShadow;

			// ★境界位置が指定どおりか: ぼかし幅の外側は明部色/陰部色そのもので、
			//   変化はすべて [境界-幅/2, 境界+幅/2] の中で起きる。
			if (V <= Def.Boundary - Half && Row != Def.Light) PlateauBroken++;
			if (V >= Def.Boundary + Half && Row != Def.Shadow) PlateauBroken++;
		}
		TestEqual(*FString::Printf(TEXT("%s: 横方向は一様"), *Tag), RowVaried, 0);
		TestEqual(*FString::Printf(TEXT("%s: 全画素が明部色と陰部色の間にある"), *Tag), OutOfRange, 0);
		TestEqual(*FString::Printf(TEXT("%s: 下へ行くほど陰部色へ寄る"), *Tag), NotMonotonic, 0);
		TestEqual(*FString::Printf(TEXT("%s: ぼかし幅の外は明部色/陰部色のまま"), *Tag), PlateauBroken, 0);

		// --- 帯 (ハイライトの筋) ---
		if (bHasBand)
		{
			// ★帯と境界の遷移が重なると、どちらの検査も意味を失う。
			//   重ならないことをモデルの前提として固定しておく。
			TestTrue(*FString::Printf(TEXT("%s: 帯が上端をはみ出さない"), *Tag),
				Def.BandCenter - BandHalf > 0.0f);
			TestTrue(*FString::Printf(TEXT("%s: 帯と境界のぼかしが重ならない"), *Tag),
				Def.BandCenter + BandHalf < Def.Boundary - Half);

			// 中心でちょうど帯の色になる。
			const FColor Peak = FMmdToonRamp::Sample(Def, Def.BandCenter);
			TestEqual(*FString::Printf(TEXT("%s: 帯の中心が帯の色 (R)"), *Tag), (int32)Peak.R, (int32)Def.Band.R);
			TestEqual(*FString::Printf(TEXT("%s: 帯の中心が帯の色 (G)"), *Tag), (int32)Peak.G, (int32)Def.Band.G);
			TestEqual(*FString::Printf(TEXT("%s: 帯の中心が帯の色 (B)"), *Tag), (int32)Peak.B, (int32)Def.Band.B);

			// ★帯の外では「帯が無いとき」と 1 ビットも変わらない。
			//   これが崩れると、帯を足したことが他の V へ漏れている。
			FMmdToonRampDef NoBand = Def;
			NoBand.BandWidth = 0.0f;
			int32 Leaked = 0;
			for (int32 Y = 0; Y < RampSize; Y++)
			{
				const float V = static_cast<float>(Y) / static_cast<float>(RampSize - 1);
				if (FMath::Abs(V - Def.BandCenter) < BandHalf) continue;
				if (FMmdToonRamp::Sample(Def, V) != FMmdToonRamp::Sample(NoBand, V)) Leaked++;
			}
			TestEqual(*FString::Printf(TEXT("%s: 帯の外へ影響が漏れない"), *Tag), Leaked, 0);
		}
	}

	// 帯を持たないランプでは Band* が一切効かない (既定値のまま素通り)。
	{
		FMmdToonRampDef Plain;
		Plain.Light = FColor(255, 255, 255);
		Plain.Shadow = FColor(100, 100, 100);
		Plain.Boundary = 0.5f;
		Plain.Softness = 0.1f;
		Plain.Band = FColor(255, 0, 0);   // 効いてしまったら赤が出る
		Plain.BandCenter = 0.25f;
		Plain.BandWidth = 0.0f;           // 帯なし
		const FColor At25 = FMmdToonRamp::Sample(Plain, 0.25f);
		TestEqual(TEXT("BandWidth=0 なら帯の色は出ない"), (int32)At25.R, (int32)At25.G);
	}

	// 範囲外は toon01 相当 (落とさない)。
	TestEqual(TEXT("範囲外 (-1) は toon01 相当"),
		(int32)FMmdToonRamp::GetDef(-1).Shadow.R, (int32)FMmdToonRamp::GetDef(0).Shadow.R);
	TestEqual(TEXT("範囲外 (10) は toon01 相当"),
		(int32)FMmdToonRamp::GetDef(NumSharedToons).Shadow.R, (int32)FMmdToonRamp::GetDef(0).Shadow.R);

	return true;
}

// ===========================================================================
// 実際にアセットを作る経路。テストがプロジェクトへ .uasset を書くので、
// 生成先を環境変数で明示したときだけ走らせる。
//   $env:MMD_TOON_RAMP_PACKAGE = "/Game/MmdToonRampTest"
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdToonRampAssetTest, "MmdPhysics.Editor.ToonRampAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMmdToonRampAssetTest::RunTest(const FString& Parameters)
{
	const FString PackagePath = FPlatformMisc::GetEnvironmentVariable(TEXT("MMD_TOON_RAMP_PACKAGE"));
	if (PackagePath.IsEmpty())
	{
		AddInfo(TEXT("MMD_TOON_RAMP_PACKAGE が未設定のためスキップ (アセットを書き込むため)。"));
		return true;
	}

	constexpr int32 Index = 1;   // toon02 (肌向け)。明部と陰部の差がはっきりある枠。
	const FMmdToonRampDef& Def = FMmdToonRamp::GetDef(Index);

	UTexture2D* Tex = FMmdToonRamp::EnsureApproxToonTexture(Index, PackagePath);
	if (!TestNotNull(TEXT("近似ランプを生成できる"), Tex)) return false;

	TestEqual(TEXT("アセット名がテーブルと対応している"), Tex->GetName(), FMmdToonRamp::AssetNameFor(Index));

	// --- テクスチャ設定 ---
	TestTrue(TEXT("sRGB"), Tex->SRGB);
	TestEqual(TEXT("AddressX は Clamp"), (int32)Tex->AddressX, (int32)TA_Clamp);
	TestEqual(TEXT("AddressY は Clamp"), (int32)Tex->AddressY, (int32)TA_Clamp);
	TestEqual(TEXT("ミップ無し"), (int32)Tex->MipGenSettings, (int32)TMGS_NoMipmaps);
	TestEqual(TEXT("Bilinear"), (int32)Tex->Filter, (int32)TF_Bilinear);
	// ★BC 圧縮すると 4x4 ブロックで色が混ざり、ぼかし幅が指定どおりにならない。
	TestEqual(TEXT("無圧縮 (TC_EditorIcon)"), (int32)Tex->CompressionSettings, (int32)TC_EditorIcon);
	// ★この設定が「色として読めない」と判定されないこと。
	//   ConvertMaterials 側の検査が TC_Default を要求していたため、共有トゥーンを
	//   取り込んでいない環境 (= 近似ランプが実際に使われる環境) でだけ、この正しい設定が
	//   誤りと判定されていた。ここは共有トゥーンの有無に関係なく走るので、再発を止められる。
	TestTrue(TEXT("色として読める設定になっている (IsColorTexture)"),
		FMmdMaterialConversion::IsColorTexture(Tex));

	// --- 画素 (アセットの中身) ---
	FTextureSource& Src = Tex->Source;
	if (!TestTrue(TEXT("ソース画像を持っている"), Src.IsValid())) return false;
	TestEqual(TEXT("幅"), static_cast<int32>(Src.GetSizeX()), RampSize);
	TestEqual(TEXT("高さ"), static_cast<int32>(Src.GetSizeY()), RampSize);

	TArray64<uint8> Mip;
	if (!TestTrue(TEXT("ソース画像を展開できる"), Src.GetMipData(Mip, 0, nullptr))) return false;
	if (!TestTrue(TEXT("画素数が足りている"), Mip.Num() >= static_cast<int64>(RampSize) * RampSize * 4)) return false;

	// TSF_BGRA8 なので B,G,R,A の順。
	auto PixelAt = [&Mip](int32 X, int32 Y)
	{
		const int64 Offset = (static_cast<int64>(Y) * RampSize + X) * 4;
		return FColor(Mip[Offset + 2], Mip[Offset + 1], Mip[Offset + 0], Mip[Offset + 3]);
	};

	const FColor Top = PixelAt(0, 0);
	const FColor Bottom = PixelAt(0, RampSize - 1);
	TestEqual(TEXT("最上段が明部色 (R)"), (int32)Top.R, (int32)Def.Light.R);
	TestEqual(TEXT("最上段が明部色 (G)"), (int32)Top.G, (int32)Def.Light.G);
	TestEqual(TEXT("最上段が明部色 (B)"), (int32)Top.B, (int32)Def.Light.B);
	TestEqual(TEXT("最下段が陰部色 (R)"), (int32)Bottom.R, (int32)Def.Shadow.R);
	TestEqual(TEXT("最下段が陰部色 (G)"), (int32)Bottom.G, (int32)Def.Shadow.G);
	TestEqual(TEXT("最下段が陰部色 (B)"), (int32)Bottom.B, (int32)Def.Shadow.B);

	// ★2 回目の呼び出しで作り直さない (変換を何度走らせてもアセットが増殖しない)。
	UTexture2D* Again = FMmdToonRamp::EnsureApproxToonTexture(Index, PackagePath);
	TestTrue(TEXT("2 回目は既存アセットを返す"), Again == Tex);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
