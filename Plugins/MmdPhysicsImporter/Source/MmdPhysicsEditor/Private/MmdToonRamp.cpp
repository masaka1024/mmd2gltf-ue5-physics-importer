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
	// ★ここに並んでいる色は **すべて自作の近似値** で、本家 MMD 付属の
	//   toon01〜10.bmp を読み取ったものではない (再配布に当たる恐れがあるため、
	//   本家のピクセル値をコードへ埋め込むことは意図的に避けている)。
	//   「共有トゥーンは番号が上がるほど陰が濃くなり、肌向けは暖色、
	//     衣装向けは紫灰/青灰/茶系が多い」という一般的な傾向に沿って手で置いた。
	//
	// ★後で人間が目視で詰める前提なので、調整はこの表 1 箇所だけで完結するようにしてある。
	//   同じ表が Tools/make_toon_ramps.py にもある (PNG を吐いて本家 BMP と並べるため)。
	//   **片方だけ直さないこと。** 運用は Python 側で見比べて決め、ここへ転記する。
	//
	// 明部色 / 陰部色は sRGB の 8bit 値。境界位置とぼかし幅は V 座標 (0=明部, 1=陰部)。
	// =======================================================================
	constexpr FMmdToonRampDef KRampTable[NumSharedToons] =
	{
		// 01: ほぼ陰なしの白。MMD で「陰を付けたくない材質」に使われる枠。
		{ FColor(255, 255, 255), FColor(250, 250, 250), 0.50f, 0.10f },
		// 02: 肌向け。淡い暖色の陰。
		{ FColor(255, 255, 255), FColor(235, 205, 195), 0.55f, 0.18f },
		// 03: 紫灰。白〜淡色の衣装向け。
		{ FColor(255, 255, 255), FColor(205, 195, 215), 0.55f, 0.16f },
		// 04: 青灰。寒色の衣装・髪向け。
		{ FColor(255, 255, 255), FColor(195, 205, 225), 0.55f, 0.16f },
		// 05: 緑がかった灰。
		{ FColor(255, 255, 255), FColor(200, 215, 200), 0.55f, 0.16f },
		// 06: 赤みのある茶。暖色の衣装向け。
		{ FColor(255, 255, 255), FColor(215, 185, 170), 0.55f, 0.16f },
		// 07: 黄土。
		{ FColor(255, 255, 255), FColor(200, 180, 150), 0.55f, 0.16f },
		// 08: 濃いめの青灰。陰がはっきり出る枠。
		{ FColor(255, 255, 255), FColor(160, 170, 195), 0.55f, 0.14f },
		// 09: 無彩色の薄い灰。
		{ FColor(255, 255, 255), FColor(200, 200, 200), 0.55f, 0.14f },
		// 10: 無彩色の濃い灰。境界を硬めにして、いちばんコントラストが強い枠にしてある。
		{ FColor(255, 255, 255), FColor(150, 150, 150), 0.50f, 0.08f },
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

	auto Mix = [T](uint8 A, uint8 B) -> uint8
	{
		// ★sRGB の 8bit 値のまま混ぜる。テクスチャは sRGB=on で、ここで書いた
		//   バイト列がそのままアセットの中身になるので、テーブルの値と
		//   生成物の画素が一致する (テストはこれを突き合わせている)。
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp<float>(A, B, T)), 0, 255));
	};

	return FColor(
		Mix(Def.Light.R, Def.Shadow.R),
		Mix(Def.Light.G, Def.Shadow.G),
		Mix(Def.Light.B, Def.Shadow.B),
		255);   // トゥーンランプは不透明 (マテリアルは RGB しか使わない)
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
