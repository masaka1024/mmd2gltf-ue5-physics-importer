// Copyright (c) 2026 masaka1024. MIT License.
// ===========================================================================
// 共有トゥーン (toon01〜toon10) が見つからないときに使う、近似トゥーンランプの生成。
//
// MMD の共有トゥーンはモデルに同梱されず、MMD 本体に付属する画像を指す名前だけが
// PMX に入っている。それが無いプロジェクトでは陰色が一切付かず「のっぺりした素の色」に
// なるので、代わりに**自前で作ったランプ**を生成してあてる。
//
// ★各ランプは `明部色 / 陰部色 / 境界位置 / ぼかし幅 / 帯` の数個のパラメータで表した近似で、
//   MMD 付属の画像そのものではない。値は、手元にある MMD 付属の共有トゥーンと
//   見比べながら手で調整した。MMD 側の見た目が要るなら toon01〜10 をプロジェクトへ
//   取り込めば、解決の 1 段目でそちらが優先される
//   (FMmdMaterialConversion の共有トゥーン解決は「名前で探す」が最優先のまま)。
//
// ★色の調整運用: Tools/make_toon_ramps.py が同じテーブルを持っていて、PNG を吐く。
//   手元の MMD 付属の BMP と並べて目視で比べ、Python 側のテーブルを直してからここへ転記する。
//   (テーブルが 2 箇所にあるのはそのため。片方だけ直さないこと)
// ===========================================================================

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace MmdToonRampConst
{
	/** 共有トゥーンの数 (toon01〜toon10)。 */
	inline constexpr int32 NumSharedToons = 10;

	/**
	 * 生成するランプの一辺。トゥーンランプは V 方向にしか意味が無い (マテリアルは U=0.5 で引く)
	 * ので 1 列でも足りるが、テクスチャの扱いが素直な正方形にしてある。
	 */
	inline constexpr int32 RampSize = 32;

	/** 生成先フォルダ名 ({PackagePath}/SharedToon/)。 */
	inline const TCHAR* FolderName = TEXT("SharedToon");
}

/**
 * 近似ランプ 1 本の定義。
 *
 * V 座標はマテリアル側の引き方に合わせてある:
 *   ToonUV = (0.5, 1 - saturate(N・L))  →  V=0 が明部 (上端)、V=1 が陰部 (下端)
 */
struct MMDPHYSICSEDITOR_API FMmdToonRampDef
{
	/** 明部色 (V=0 側)。sRGB の 8bit 値。 */
	FColor Light = FColor::White;
	/** 陰部色 (V=1 側)。sRGB の 8bit 値。 */
	FColor Shadow = FColor::White;
	/** 境界位置。この V でちょうど明部色と陰部色の中間になる。 */
	float Boundary = 0.5f;
	/** ぼかし幅。境界の前後この幅で明部→陰部へ移る。0 に近いほど硬い 2 値のトゥーンになる。 */
	float Softness = 0.15f;

	// --- 帯 (ハイライトの筋)。既定は「帯なし」で、使うのは toon06 だけ。 ---
	//
	// ★明部→陰部の単調な遷移とは別に、途中へ明るい線を 1 本重ねるためのもの。
	//   金属系のトゥーンが持つ「光沢の筋」を表す。BandWidth が 0 なら一切効かない
	//   ので、帯を持たないランプの挙動は従来と 1 ビットも変わらない。

	/** 帯の色。中心 (BandCenter) でちょうどこの色になる。
	 *  ★既定値に `FColor::White` は使えない (constexpr でないため、帯を書かない
	 *    エントリがあると KRampTable の定数評価が通らなくなる)。 */
	FColor Band = FColor(255, 255, 255, 255);
	/** 帯の中心 V。 */
	float BandCenter = 0.0f;
	/** 帯の幅 (V 座標)。**0 = 帯なし**。端で 0・中心で 1 になる滑らかな山で重ねる。 */
	float BandWidth = 0.0f;
};

class MMDPHYSICSEDITOR_API FMmdToonRamp
{
public:
	/**
	 * 近似ランプの定義を引く。
	 * @param Index 0 起点 (PMX の toonShared と同じ。0 → toon01)。範囲外は toon01 相当。
	 */
	static const FMmdToonRampDef& GetDef(int32 Index);

	/**
	 * ランプの V 座標 (0=明部, 1=陰部) におけるピクセル色。
	 * テクスチャ生成もテストもこの 1 関数を通すので、両者がずれない。
	 */
	static FColor Sample(const FMmdToonRampDef& Def, float V);

	/** RampSize×RampSize 枚分の画素を作る (行優先、先頭が最上段 = 明部)。 */
	static void BuildPixels(const FMmdToonRampDef& Def, TArray<FColor>& OutPixels);

	/** 近似ランプのアセット名 ("T_MmdToonApprox01")。Index は 0 起点。 */
	static FString AssetNameFor(int32 Index);

	/**
	 * {PackagePath}/SharedToon/T_MmdToonApproxNN を用意する。
	 *
	 * 既に同名アセットがあれば再生成せずそれを返す (2 回目以降の変換で増殖させない)。
	 * Transient ではなく永続アセットにするのは、マテリアルインスタンスから参照が残るため。
	 *
	 * @param Index       0 起点 (PMX の toonShared と同じ)
	 * @param PackagePath 生成先の親フォルダ (例 "/Game/IA")
	 * @return 失敗したら nullptr
	 */
	static UTexture2D* EnsureApproxToonTexture(int32 Index, const FString& PackagePath);
};
