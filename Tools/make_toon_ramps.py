#!/usr/bin/env python3
# Copyright (c) 2026 masaka1024. MIT License.
"""共有トゥーンの近似ランプを PNG で吐く（色調整用のプレビュー）。

なぜこれがあるか
----------------
MMD の共有トゥーン（toon01〜toon10）は MMD 本体に付属する画像で、モデルにも
`.glb` にも入っていません。プラグインはそれが見つからないときに **自作の近似ランプ**
を生成して代用します（`MmdToonRamp.cpp`）。その近似色を人間が目視で詰めるための道具です。

★本家 toon01〜10.bmp のピクセル値は読み取っていません（再配布に当たる恐れがあるため、
  本家の値をコードやこのスクリプトへ埋め込むことは意図的に避けています）。
  ここにある色はすべて「MMD の共有トゥーンはこういう傾向だろう」という一般論に沿って
  手で置いた **自作の近似値** です。

★テーブルは C++ 側と二重管理です。
  正本はどちらでもなく「両方そろっていること」が条件です:
      Tools/make_toon_ramps.py                                            ← このファイル
      Plugins/MmdPhysicsImporter/Source/MmdPhysicsEditor/Private/MmdToonRamp.cpp
  **片方だけ直さないでください。**

調整の手順
----------
1. このスクリプトを走らせて PNG を出す::

       py -3 Tools/make_toon_ramps.py -o out/toon_preview --scale 8

2. 出た PNG と、手元にある本家の `toon01.bmp`〜`toon10.bmp` を画像ビューアで並べ、
   明部色 / 陰部色 / 境界位置 / ぼかし幅 を見比べる
   （本家 BMP はリポジトリに置かないこと。各自の MMD インストール先から開く）
3. 下の ``RAMPS`` の値を直し、1. に戻る
4. 納得できたら **同じ値を C++ 側の ``KRampTable`` へ転記する**
   （並び・単位はどちらも `明部色 / 陰部色 / 境界位置 / ぼかし幅` でそろえてあります）
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


# =============================================================================
# 近似トゥーンランプのテーブル（toon01〜toon10）。
#
# ★ここが調整対象です。直したら C++ 側 MmdToonRamp.cpp の KRampTable へ転記すること。
#   コメントも含めて 1 対 1 で対応させてあります。
# =============================================================================
RAMPS: tuple[RampDef, ...] = (
    # 01: ほぼ陰なしの白。MMD で「陰を付けたくない材質」に使われる枠。
    RampDef((255, 255, 255), (250, 250, 250), 0.50, 0.10),
    # 02: 肌向け。淡い暖色の陰。
    RampDef((255, 255, 255), (235, 205, 195), 0.55, 0.18),
    # 03: 紫灰。白〜淡色の衣装向け。
    RampDef((255, 255, 255), (205, 195, 215), 0.55, 0.16),
    # 04: 青灰。寒色の衣装・髪向け。
    RampDef((255, 255, 255), (195, 205, 225), 0.55, 0.16),
    # 05: 緑がかった灰。
    RampDef((255, 255, 255), (200, 215, 200), 0.55, 0.16),
    # 06: 赤みのある茶。暖色の衣装向け。
    RampDef((255, 255, 255), (215, 185, 170), 0.55, 0.16),
    # 07: 黄土。
    RampDef((255, 255, 255), (200, 180, 150), 0.55, 0.16),
    # 08: 濃いめの青灰。陰がはっきり出る枠。
    RampDef((255, 255, 255), (160, 170, 195), 0.55, 0.14),
    # 09: 無彩色の薄い灰。
    RampDef((255, 255, 255), (200, 200, 200), 0.55, 0.14),
    # 10: 無彩色の濃い灰。境界を硬めにして、いちばんコントラストが強い枠にしてある。
    RampDef((255, 255, 255), (150, 150, 150), 0.50, 0.08),
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
    return tuple(
        min(max(_round_half_away(a + (b - a) * t), 0), 255)
        for a, b in zip(ramp.light, ramp.shadow)
    )


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
    print("本家 toon01〜10.bmp と並べて見比べ、RAMPS を直したら")
    print("MmdToonRamp.cpp の KRampTable へ同じ値を転記してください。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
