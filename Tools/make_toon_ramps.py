#!/usr/bin/env python3
# Copyright (c) 2026 masaka1024. MIT License.
"""共有トゥーンの近似ランプを PNG で吐く（色調整用のプレビュー）。

なぜこれがあるか
----------------
MMD の共有トゥーン（toon01〜toon10）は MMD 本体に付属する画像で、モデルにも
`.glb` にも入っていません。プラグインはそれが見つからないときに **自作の近似ランプ**
を生成して代用します（`MmdToonRamp.cpp`）。その近似色を人間が目視で詰めるための道具です。

★各ランプは `明部色 / 陰部色 / 境界位置 / ぼかし幅 / 帯` の数個のパラメータで表した近似で、
  MMD 付属の画像そのものではありません。値は、手元にある MMD 付属の共有トゥーンと
  見比べながら手で調整したものです。MMD 側の見た目が要るなら `toonXX` を取り込めば、
  解決の 1 段目でそちらが優先されます。

★テーブルは C++ 側と二重管理です。
  正本はどちらでもなく「両方そろっていること」が条件です:
      Tools/make_toon_ramps.py                                            ← このファイル
      Plugins/MmdPhysicsImporter/Source/MmdPhysicsEditor/Private/MmdToonRamp.cpp
  **片方だけ直さないでください。**

調整の手順
----------
1. このスクリプトを走らせて PNG を出す::

       py -3 Tools/make_toon_ramps.py -o out/toon_preview --scale 8

2. 出た PNG と、手元にある `toon01.bmp`〜`toon10.bmp` を画像ビューアで並べ、
   明部色 / 陰部色 / 境界位置 / ぼかし幅 / 帯 を見比べる
   （MMD 付属の BMP はリポジトリに置かないこと。各自の MMD インストール先から開く）
3. 下の ``RAMPS`` の値を直し、1. に戻る
4. 納得できたら **同じ値を C++ 側の ``KRampTable`` へ転記する**
   （並び・単位はどちらも `明部色 / 陰部色 / 境界位置 / ぼかし幅 / 帯` でそろえてあります）
5. `MmdPhysics.Editor.ToonRamp` を走らせる。ぼかしが端をはみ出すような値は落ちます

必要なもの: Pillow (``pip install Pillow``)
"""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - 実行環境の案内
    sys.exit("Pillow が要ります: pip install Pillow")

# Pillow 9.1 でリサンプル定数が Image.Resampling へ移った（Image.NEAREST も当面残るが、
# 古い環境と新しい環境の両方で動くようにここで解決しておく）。
_NEAREST = getattr(Image, "Resampling", Image).NEAREST


# ランプの一辺。C++ 側 MmdToonRampConst::RampSize と同じ値にすること。
RAMP_SIZE = 32


@dataclass(frozen=True)
class RampDef:
    """近似ランプ 1 本の定義。

    V 座標はマテリアル側の引き方に合わせてある::

        ToonUV = (0.5, 1 - saturate(N・L))   →   V=0 が明部（上端）、V=1 が陰部（下端）
    """

    light: tuple[int, int, int]   # 明部色（V=0 側）。sRGB の 8bit 値
    shadow: tuple[int, int, int]  # 陰部色（V=1 側）。sRGB の 8bit 値
    boundary: float               # 境界位置。この V でちょうど明部色と陰部色の中間になる
    softness: float               # ぼかし幅。境界の前後この幅で明部→陰部へ移る

    # 帯（ハイライトの筋）。既定は「帯なし」で、使うのは toon06 だけ。
    band: tuple[int, int, int] = (255, 255, 255)  # 帯の色。中心でちょうどこの色になる
    band_center: float = 0.0                      # 帯の中心 V
    band_width: float = 0.0                       # 帯の幅。**0 = 帯なし**


# =============================================================================
# 近似トゥーンランプのテーブル（toon01〜toon10）。
#
# ★ここが調整対象です。直したら C++ 側 MmdToonRamp.cpp の KRampTable へ転記すること。
#   コメントも含めて 1 対 1 で対応させてあります。
# =============================================================================
RAMPS: tuple[RampDef, ...] = (
    # --- 01〜04: 硬い 2 値（遷移は境界の 1 行だけ）。ぼかし幅 0.03 = 32 画素で 1 行。
    #     ★0 にはできない（C++ 側テストが softness > 0 を要求する）。
    # 01: 中間グレー。陰の深さは中くらい。
    RampDef((255, 255, 255), (210, 210, 210), 0.50, 0.03),
    # 02: 肌向け。淡い暖色の陰。
    RampDef((255, 255, 255), (245, 226, 224), 0.50, 0.03),
    # 03: 無彩色の灰。**この表でいちばん陰が濃い枠**（MMD 側もここが最も濃い）。
    RampDef((255, 255, 255), (155, 155, 155), 0.50, 0.03),
    # 04: ごく淡い暖色。陰はほとんど出ない。
    RampDef((255, 255, 255), (245, 238, 234), 0.50, 0.03),

    # --- 05〜06: なだらかな遷移。ただし中央ではなく下半分に寄る。
    # 05: 淡い暖色。下半分で長くぼかす。
    RampDef((255, 255, 255), (255, 235, 225), 0.70, 0.26),
    # 06: 黄系 + 光沢の筋。金属向けの枠。
    #     ★この枠だけ **明部色が白でなく、帯を持つ**（MMD 側の toon06 はランプ全体が黄色く、
    #       明部寄り V≒0.3 に白い筋が 1 本入る）。
    RampDef((255, 240, 110), (200, 175, 20), 0.78, 0.13,
            band=(255, 252, 230), band_center=0.30, band_width=0.12),

    # --- 07〜10: 陰なし（MMD 側が真っ白で一様なので、近似側も陰を作らない）。
    #     明部色と陰部色を同じにすると、ぼかし幅に関係なく一様な白いランプになる。
    # 07: 陰なし。
    RampDef((255, 255, 255), (255, 255, 255), 0.50, 0.10),
    # 08: 陰なし。
    RampDef((255, 255, 255), (255, 255, 255), 0.50, 0.10),
    # 09: 陰なし。
    RampDef((255, 255, 255), (255, 255, 255), 0.50, 0.10),
    # 10: 陰なし。
    RampDef((255, 255, 255), (255, 255, 255), 0.50, 0.10),
)


def _round_half_away(value: float) -> int:
    """C++ 側 FMath::RoundToInt と同じ丸め。

    Python 組み込みの round() は偶数丸め（round-half-to-even）なので、そのまま使うと
    ちょうど .5 になる画素で C++ と 1 ずれる。
    """
    return math.floor(value + 0.5) if value >= 0.0 else math.ceil(value - 0.5)


def sample(ramp: RampDef, v: float) -> tuple[int, int, int]:
    """V 座標（0=明部, 1=陰部）における色。C++ 側 FMmdToonRamp::Sample と同じ式。"""
    width = max(ramp.softness, 0.0)

    if width <= 1e-4:
        # ぼかし幅 0 = 硬い 2 値。
        t = 1.0 if v >= ramp.boundary else 0.0
    else:
        # 境界からの差で書く。v == boundary でちょうど 0.5 になることが保証される
        # （C++ 側と同じ理由。詳しくは MmdToonRamp.cpp の Sample の注記）。
        t = min(max((v - ramp.boundary) / width + 0.5, 0.0), 1.0)
        # smoothstep。境界（t=0.5）では 0.5 のままなので、
        # 「境界位置でちょうど中間色になる」という定義は線形補間と同じ。
        t = t * t * (3.0 - 2.0 * t)

    # sRGB の 8bit 値のまま混ぜる（C++ 側もそうしている）。
    out = tuple(
        min(max(_round_half_away(a + (b - a) * t), 0), 255)
        for a, b in zip(ramp.light, ramp.shadow)
    )

    # 帯（ハイライトの筋）を重ねる。band_width が 0 なら何もしない。
    if ramp.band_width > 0.0:
        half = ramp.band_width * 0.5
        d = abs(v - ramp.band_center) / half   # 中心で 0、端で 1
        if d < 1.0:
            # 端で 0・中心で 1 になる滑らかな山（smoothstep を反転したもの）。
            u = 1.0 - d
            band_t = u * u * (3.0 - 2.0 * u)
            out = tuple(
                min(max(_round_half_away(a + (b - a) * band_t), 0), 255)
                for a, b in zip(out, ramp.band)
            )

    return out


def build_image(ramp: RampDef, size: int = RAMP_SIZE) -> Image.Image:
    """ランプ 1 本を size×size の画像にする（先頭行が明部）。"""
    image = Image.new("RGB", (size, size))
    pixels = image.load()
    for y in range(size):
        color = sample(ramp, y / (size - 1))
        for x in range(size):
            pixels[x, y] = color
    return image


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="共有トゥーンの近似ランプを PNG で出力する（目視調整用）。")
    parser.add_argument(
        "-o", "--out", type=Path, default=Path("out/toon_preview"),
        help="出力先フォルダ (既定: out/toon_preview)")
    parser.add_argument(
        "--scale", type=int, default=1,
        help="拡大した確認用 PNG も出す倍率 (最近傍。既定: 1 = 出さない)")
    args = parser.parse_args(argv)

    args.out.mkdir(parents=True, exist_ok=True)

    for index, ramp in enumerate(RAMPS):
        # 名前は C++ 側が生成するアセット名 (T_MmdToonApproxNN) に合わせてある。
        name = f"T_MmdToonApprox{index + 1:02d}"
        image = build_image(ramp)
        image.save(args.out / f"{name}.png")

        if args.scale > 1:
            big = image.resize(
                (RAMP_SIZE * args.scale, RAMP_SIZE * args.scale), _NEAREST)
            big.save(args.out / f"{name}_x{args.scale}.png")

        top = sample(ramp, 0.0)
        bottom = sample(ramp, 1.0)
        mid = sample(ramp, ramp.boundary)
        print(f"{name}: 明部 {top} / 陰部 {bottom} / 境界 {ramp.boundary:.2f} で {mid} "
              f"/ ぼかし {ramp.softness:.2f}")

    print(f"\n{len(RAMPS)} 本を {args.out} へ出しました。")
    print("MMD 付属の toon01〜10.bmp と並べて見比べ、RAMPS を直したら")
    print("MmdToonRamp.cpp の KRampTable へ同じ値を転記してください。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
