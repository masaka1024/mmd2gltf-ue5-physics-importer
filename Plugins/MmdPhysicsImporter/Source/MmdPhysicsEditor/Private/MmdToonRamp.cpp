// Copyright (c) 2026 masaka1024. MIT License.

#include "MmdToonRamp.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "MmdPhysicsCoreLog.h"
#include "UObject/SavePackage.h"

using namespace MmdToonRampConst;

namespace
{
	// =======================================================================
	// 近似トゥーンランプのテーブル (toon01〜toon10)。
	//
	// ★この表は `明部色 / 陰部色 / 境界位置 / ぼかし幅 / 帯` の数個のパラメータで表した近似で、
	//   MMD 付属の画像そのものではない。値は、手元にある MMD 付属の共有トゥーンと
	//   見比べながら手で調整した。
	//
	// ★2026-08-17: 手元の MMD 付属の BMP と突き合わせて詰め直した。分かったのは次の 3 点:
	//     1. 01〜04 の遷移は **1 行 (=1/31≒0.03) の硬い 2 値**。中間色の画素が 1 つも無い。
	//        以前の当ファイルは 0.10〜0.18 の smoothstep で、明らかに柔らかすぎた。
	//     2. **07〜10 には陰が無い** (真っ白で一様)。したがって近似側も陰を作らない。
	//        「共有トゥーンは番号が上がるほど陰が濃い」という以前の前提は誤りだった。
	//        実際にいちばん濃いのは 03 で、番号は濃さの順ではない。
	//     3. なだらかなのは 05 と 06 だけで、遷移は **下半分に寄っている** (V=0.6〜0.85 付近)。
	//   06 だけは **明部色も白ではなく、帯 (ハイライトの筋) も持つ** (MMD 側の toon06 は
	//   ランプ全体が黄色く、明部寄りに白い筋が 1 本入る)。筋のために Band* を足した。
	//
	// ★後で人間が目視で詰める前提なので、調整はこの表 1 箇所だけで完結するようにしてある。
	//   同じ表が Tools/make_toon_ramps.py にもある (PNG を吐いて MMD 付属の BMP と並べるため)。
	//   **片方だけ直さないこと。** 運用は Python 側で見比べて決め、ここへ転記する。
	//
	// 明部色 / 陰部色は sRGB の 8bit 値。境界位置とぼかし幅は V 座標 (0=明部, 1=陰部)。
	// =======================================================================
	constexpr FMmdToonRampDef KRampTable[NumSharedToons] =
	{
		// --- 01〜04: 硬い 2 値 (遷移は境界の 1 行だけ)。ぼかし幅 0.03 = 32 画素で 1 行。
		//     ★0 にはできない (テストが Softness > 0 を要求する。0 だと境界で中間色にならない)。
		// 01: 中間グレー。陰の深さは中くらい。
		{ FColor(255, 255, 255), FColor(210, 210, 210), 0.50f, 0.03f },
		// 02: 肌向け。淡い暖色の陰。
		{ FColor(255, 255, 255), FColor(245, 226, 224), 0.50f, 0.03f },
		// 03: 無彩色の灰。**この表でいちばん陰が濃い枠** (MMD 側もここが最も濃い)。
		{ FColor(255, 255, 255), FColor(155, 155, 155), 0.50f, 0.03f },
		// 04: ごく淡い暖色。陰はほとんど出ない。
		{ FColor(255, 255, 255), FColor(245, 238, 234), 0.50f, 0.03f },

		// --- 05〜06: なだらかな遷移。ただし中央ではなく下半分に寄る。
		// 05: 淡い暖色。下半分で長くぼかす。
		{ FColor(255, 255, 255), FColor(255, 235, 225), 0.70f, 0.26f },
		// 06: 黄系 + 光沢の筋。金属向けの枠。
		//     ★この枠だけ **明部色が白でなく、帯を持つ**。MMD 側の toon06 はランプ全体が黄色く、
		//       明部寄り (V≒0.3) に白い筋が 1 本入る。見れば一目で他の 9 本と違う。
		//     ★明部を白のままにすると、遷移位置を正しくするほど白い領域が増えて
		//       かえって離れた (RMS 89.7 → 19.2 → 帯を足して更に改善)。
		{ FColor(255, 240, 110), FColor(200, 175, 20), 0.78f, 0.13f,
		  FColor(255, 252, 230), 0.30f, 0.12f },

		// --- 07〜10: 陰なし (MMD 側が真っ白で一様なので、近似側も陰を作らない)。
		//     明部色と陰部色を同じにすると、ぼかし幅に関係なく一様な白いランプになる。
		//     ★「陰が付かない」のは不具合ではなく MMD 側の再現。陰を出したい材質は
		//       toon01〜06 側の番号を使うか、生成された .uasset を直接いじる。
		// 07: 陰なし。
		{ FColor(255, 255, 255), FColor(255, 255, 255), 0.50f, 0.10f },
		// 08: 陰なし。
		{ FColor(255, 255, 255), FColor(255, 255, 255), 0.50f, 0.10f },
		// 09: 陰なし。
		{ FColor(255, 255, 255), FColor(255, 255, 255), 0.50f, 0.10f },
		// 10: 陰なし。
		{ FColor(255, 255, 255), FColor(255, 255, 255), 0.50f, 0.10f },
	};

	/** マテリアル変換側と同じ保存処理 (このファイルだけで完結させたいので小さく持つ)。 */
	bool SaveRampPackage(UObject* Asset)
	{
		if (Asset == nullptr) return false;
		UPackage* Package = Asset->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		Args.Error = GWarn;
		return UPackage::SavePackage(Package, nullptr, *FileName, Args);
	}
}

const FMmdToonRampDef& FMmdToonRamp::GetDef(int32 Index)
{
	if (Index < 0 || Index >= NumSharedToons)
	{
		// 範囲外は toon01 (ほぼ陰なし) 扱い。陰を付けすぎるより無害。
		return KRampTable[0];
	}
	return KRampTable[Index];
}

FColor FMmdToonRamp::Sample(const FMmdToonRampDef& Def, float V)
{
	const float Width = FMath::Max(Def.Softness, 0.0f);

	float T;
	if (Width <= UE_KINDA_SMALL_NUMBER)
	{
		// ぼかし幅 0 = 硬い 2 値。
		T = (V >= Def.Boundary) ? 1.0f : 0.0f;
	}
	else
	{
		// ★境界からの差で書く ((V - Boundary) / Width + 0.5)。
		//   「(V - (Boundary - Width/2)) / Width」と数学的には同じだが、
		//   こちらは V == Boundary でちょうど 0.5 になることが浮動小数点でも保証される。
		//   前の書き方だと 0.5 から 1ulp ずれることがあり、明部色と陰部色の和が奇数のとき
		//   中間色が 1 だけ上下に振れた (境界位置のテストが不安定になる)。
		T = FMath::Clamp((V - Def.Boundary) / Width + 0.5f, 0.0f, 1.0f);
		// smoothstep。★境界 (T=0.5) では 0.5 のままなので、
		//   「境界位置でちょうど明部色と陰部色の中間になる」という定義は線形補間と同じ。
		T = T * T * (3.0f - 2.0f * T);
	}

	auto Mix = [](uint8 A, uint8 B, float Alpha) -> uint8
	{
		// ★sRGB の 8bit 値のまま混ぜる。テクスチャは sRGB=on で、ここで書いた
		//   バイト列がそのままアセットの中身になるので、テーブルの値と
		//   生成物の画素が一致する (テストはこれを突き合わせている)。
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp<float>(A, B, Alpha)), 0, 255));
	};

	FColor Out(
		Mix(Def.Light.R, Def.Shadow.R, T),
		Mix(Def.Light.G, Def.Shadow.G, T),
		Mix(Def.Light.B, Def.Shadow.B, T),
		255);   // トゥーンランプは不透明 (マテリアルは RGB しか使わない)

	// --- 帯 (ハイライトの筋) を重ねる。BandWidth が 0 なら何もしない。 ---
	if (Def.BandWidth > 0.0f)
	{
		const float Half = Def.BandWidth * 0.5f;
		const float D = FMath::Abs(V - Def.BandCenter) / Half;   // 中心で 0、端で 1
		if (D < 1.0f)
		{
			// ★端で 0・中心で 1 になる滑らかな山 (smoothstep を反転したもの)。
			//   両端で傾きが 0 になるので、帯の外へ継ぎ目なく溶ける。
			const float U = 1.0f - D;
			const float BandT = U * U * (3.0f - 2.0f * U);
			Out = FColor(
				Mix(Out.R, Def.Band.R, BandT),
				Mix(Out.G, Def.Band.G, BandT),
				Mix(Out.B, Def.Band.B, BandT),
				255);
		}
	}

	return Out;
}

void FMmdToonRamp::BuildPixels(const FMmdToonRampDef& Def, TArray<FColor>& OutPixels)
{
	OutPixels.SetNumUninitialized(RampSize * RampSize);
	for (int32 Y = 0; Y < RampSize; Y++)
	{
		// 最上段 (Y=0) が V=0 = 明部、最下段 (Y=RampSize-1) が V=1 = 陰部。
		const float V = static_cast<float>(Y) / static_cast<float>(RampSize - 1);
		const FColor Color = Sample(Def, V);
		for (int32 X = 0; X < RampSize; X++)
		{
			OutPixels[Y * RampSize + X] = Color;
		}
	}
}

FString FMmdToonRamp::AssetNameFor(int32 Index)
{
	return FString::Printf(TEXT("T_MmdToonApprox%02d"), FMath::Clamp(Index, 0, NumSharedToons - 1) + 1);
}

UTexture2D* FMmdToonRamp::EnsureApproxToonTexture(int32 Index, const FString& PackagePath)
{
	if (Index < 0 || Index >= NumSharedToons)
	{
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] 共有トゥーン番号 %d は範囲外です (0〜%d)。"), Index, NumSharedToons - 1);
		return nullptr;
	}

	const FString AssetName = AssetNameFor(Index);
	const FString FolderPath = PackagePath / FolderName;
	const FString PackageName = FolderPath / AssetName;

	// ★同名アセットがあれば再生成しない。変換は何度でも走るので、
	//   毎回作り直すと参照が切り替わるうえ、目視で詰めた値をユーザーが
	//   アセット側で上書きしていた場合にそれを潰してしまう。
	//   ★探すのはモデルのフォルダ配下だけ。マスターマテリアル (M_MmdToon) と同じで、
	//     モデルごとに一式が揃っているほうが持ち出しやすいため。
	if (UTexture2D* Existing = LoadObject<UTexture2D>(
		nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		UE_LOG(LogMmdPhysics, Warning,
			TEXT("[MmdPhysics] 近似トゥーンの保存先パッケージを作れません: %s"), *PackageName);
		return nullptr;
	}
	Package->FullyLoad();

	UTexture2D* Tex = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
	if (Tex == nullptr) return nullptr;

	TArray<FColor> Pixels;
	BuildPixels(GetDef(Index), Pixels);

	// FTextureSource::Init は PreEdit/PostEdit の間で呼ぶ約束になっている (Texture.h の注記)。
	Tex->PreEditChange(nullptr);

	Tex->Source.Init(RampSize, RampSize, /*NumSlices=*/1, /*NumMips=*/1, TSF_BGRA8);
	uint8* Dest = Tex->Source.LockMip(0);
	if (Dest == nullptr)
	{
		Tex->PostEditChange();   // PreEditChange と対にしておく
		UE_LOG(LogMmdPhysics, Warning, TEXT("[MmdPhysics] 近似トゥーンの画素を書けません: %s"), *PackageName);
		return nullptr;
	}
	// TSF_BGRA8 は名前どおり B,G,R,A の並び。FColor のメモリ配置に頼らず明示的に書く。
	for (int32 i = 0; i < Pixels.Num(); i++)
	{
		Dest[i * 4 + 0] = Pixels[i].B;
		Dest[i * 4 + 1] = Pixels[i].G;
		Dest[i * 4 + 2] = Pixels[i].R;
		Dest[i * 4 + 3] = Pixels[i].A;
	}
	Tex->Source.UnlockMip(0);

	// --- テクスチャ設定 ---
	// ★sRGB=on。テーブルの値は sRGB の 8bit 値として置いてあり、
	//   マテリアルはそれを色として掛ける。
	Tex->SRGB = true;
	// ★圧縮しない。32x32 のランプを BC で潰すと 4x4 ブロック単位で色が混ざり、
	//   境界のぼかし幅が指定どおりにならない。TC_EditorIcon は無圧縮 RGBA。
	Tex->CompressionSettings = TC_EditorIcon;
	// ★ミップ無し。縮小版を引くと明部と陰部が平均化されて陰が消える。
	Tex->MipGenSettings = TMGS_NoMipmaps;
	// ★Clamp。ランプは V=0..1 の外を引かない前提で、繰り返すと明部と陰部が隣り合ってしまう。
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	// 32 段しかないので、段差が出ないよう線形で引く。
	Tex->Filter = TF_Bilinear;
	// 32x32 の常駐アセット。ストリーミングに乗せる意味が無く、
	// 低解像度ミップを掴んで一瞬色が違う、という事故も避けられる。
	Tex->NeverStream = true;

	// PostEditChange がソース画像から実行時データを組み直す (UpdateResource を含む)。
	Tex->PostEditChange();
	FAssetRegistryModule::AssetCreated(Tex);
	Tex->MarkPackageDirty();
	SaveRampPackage(Tex);

	UE_LOG(LogMmdPhysics, Log,
		TEXT("[MmdPhysics] 近似トゥーンランプを生成しました: %s"), *PackageName);
	return Tex;
}
