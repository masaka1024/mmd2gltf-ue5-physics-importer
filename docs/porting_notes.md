# 移植ノート — C# ↔ C++

## 役割分担 — `extras.mmd` に何を入れてよいか

このプロジェクトの構成は次のとおりです。

- **エクスポーター（Python, `mmd2gltf-gui`）** … PMX を glTF へ出し、glTF で失われる情報を
  `extras.mmd` へ書き出す。**仕様の正本**
- **インポーター（この UE プラグイン）** … `extras.mmd` から PMX 由来のデータを復元し、UE5 へ取り込む

★**`extras.mmd` の目的は MMD 資産の復元です。** PMX エディタがメンテされなくなり使えなくなったとき、
MMD という資産を形を変えてでも残すことがねらいです。したがって **MMD 由来でないデータを
`extras.mmd` に足すのは NG**。測定値・分類のヒント・特定レンダラ向けの指示は入れません。

この線引きから、次が決まります。

| | 置き場所 |
|---|---|
| PMX の生値（材質フラグ、`edgeColor`、`sphereMode`、剛体、ジョイント…） | `extras.mmd`（正本） |
| プリベイクで失われる元テクスチャ（`origTexture`）、元のα表現（`alphaClass`） | `extras.mmd`（復元のための情報） |
| 「UE の半透明は深度を書けないので、この材質を昇格させてよいか」 | **インポーター**（受け手の都合） |

だから「肌に貼り付けた半透明の材質か」の判定は UE 側に持っています（`MeasureUvAlpha`）。
エクスポーターにも似た測定（`_material_uv_alpha_stats`）がありますが、あちらは
`alphaMode` / プリベイクを決めるための内部処理で、しきい値も別の目的に校正されています。
**同じ数値を要求するのではなく、受け手が自分で測って自分で決める**のが正しい形です。

★移植元の Unity 版は「同じ仕様の別の受け手」であって、仕様の正本ではありません。
Unity 版の実装をそのまま写すのではなく、`extras.mmd` の意味に従って UE 版として判断します
（Unity 版が一律に半透明へ昇格できるのは lilToon の TwoPass が深度を書くからで、
UE ではその前提が成り立ちません）。

## 方針

`masaka1024/mmd2gltf-unity-physics-importer` の物理エンジン（純 C#、`UnityEngine` 非依存）を
**1:1 の直訳**で C++ へ移植しています。UE 向けの「改善」は入れません。目的は数値の一致です。

- **C# の `float` は C++ でも `float`。** UE の `FVector`(double) を混ぜない
- **演算順序を変えない。** 数式は一字一句同じ順に書く
- ファイル名・クラス名・メソッド名を対応させ、本家の差分パッチを目視で当てられる状態を保つ
- コンテナのみ UE 型を使う（`List<T>`→`TArray<T>`、`Dictionary`→`TMap`、`string`→`FString`）
- **数学型は自前を維持**（`Vec3` / `Quat` / `Matrix4x4` / `RigidTransform` / `Matrix3x3`）
- コード内の日本語コメント（事故記録・実測値）はすべて残す。本家の設計判断の記録そのもの

## ファイル対応表

移植元は `.upstream/unity/Assets/MMD_Scripts/MmdPhysics/`。
移植先は `Plugins/MmdPhysicsImporter/Source/MmdPhysicsCore/`。

| 移植元 | 移植先 | 状態 |
|---|---|---|
| `Core/MathTypes.cs` | `MmdMathTypes.h/.cpp` | 済 |
| `Core/Transform.cs` | `MmdRigidTransform.h/.cpp` | 済 |
| `Core/CollisionShape.cs` | `MmdCollisionShape.h/.cpp` | 済 |
| `Core/RigidBody.cs` | `MmdRigidBody.h` | 済 |
| `Core/Collision.cs` | `MmdCollision.h/.cpp` | 済 |
| `Core/Constraints.cs` | `MmdConstraints.h/.cpp` | 済 |
| `Core/PhysicsWorld.cs` | `MmdPhysicsWorld.h/.cpp` | 済 |
| `Core/SoftBody.cs` | — | 未（ビルダーから未使用） |
| `Pmx/PmxPhysicsData.cs` | `MmdPmxPhysicsData.h` | 済 |
| `Pmx/MiniJson.cs` | `MmdMiniJson.h/.cpp` | 済 |
| `Pmx/GlbPhysicsReader.cs` | `MmdGlbPhysicsReader.h/.cpp` | 済 |
| `Pmx/PmxPhysicsBuilder.cs` | `MmdPmxPhysicsBuilder.h/.cpp` | 済 |
| `Pmx/PmxReader.cs` | — | 未（PMX 直読みの検証経路） |
| `Unity/MmdPhysicsBehaviour.cs` | `MmdPhysicsRuntime/AnimNode_MmdPhysics` | UE 版として作り直し |

## upstream の取得

```
git clone --depth 1 https://github.com/masaka1024/mmd2gltf-unity-physics-importer.git .upstream/unity
```

現在の対応コミット: `0fcbd57`（2026-08-13「sync: カプセル慣性の Bullet 準拠マージンを同梱コピーへ反映」）。
`.upstream/` は `.gitignore` 済みです。

## ソルバ既定値はノードとコアで違う（意図的）

同じ「刻み」の既定値が 2 箇所にあります。**値が違っていて正しい**ので、揃えないでください。

| | `FixedTimeStep` | `SubSteps` | 実効刻み | 対応する移植元 |
|---|---|---|---|---|
| `PhysicsWorld`（コア） | 1/30 | 4 | 1/120 | `Core/PhysicsWorld.cs` |
| `FAnimNode_MmdPhysics`（ノード） | 1/60 | 2 | 1/120 | `Unity/MmdPhysicsBehaviour.cs` |

移植元も同じ二重構造です。エンジン単体の既定は 1/30×4、それを使う `MmdPhysicsBehaviour` が
インスペクタ値 1/60×2 で上書きします。コア側は数値パリティテストが素の既定で回るため、
**移植元コアと同じ値のまま**にしてあります（`ApplySolverSettings` が毎評価で
ノードの値を `World` へ書くので、実行時に効くのはノード側だけです）。

**2026-08-14 にノード側を 1/30×4 → 1/60×2 へ修正しました。** 移植時に移植元コアの既定を
そのままノードへ写してしまい、`MmdPhysicsBehaviour.cs` の調整済み値（2026-08-09 のジャダー調査で
1/30×2 → 1/60×1、2026-08-13 の貫入対策で 1/60×2）が反映されていませんでした。

実効刻みはどちらも 1/120 で同じですが、**1 フレームに何ステップ進むか**が変わります。
`PhysicsWorld` はアキュムレータ方式なので、`FixedTimeStep` がフレーム間隔より長いと
1 フレームあたりの内部ステップが 0,1,0,1,1,... と変動し、物理の進む量は合っていても
実時間の更新間隔が 20ms/40ms とばらつきます。これが髪・スカートのコマ落ち（ジャダー）の原因で、
移植元では `Time.fixedDeltaTime` と一致させて解消していました。UE の Post-Process AnimBP は
可変フレームレートで評価されるため厳密な等間隔にはなりませんが、刻みがフレーム間隔以下なら
取りこぼしが出ないぶんばらつきは小さくなります。実機でも 1/30 のままだと揺れが移植元より
大きく荒れて見えました。

★`FixedTimeStep` は 1/60 のまま触らないこと。細刻みが欲しいときは `SubSteps` を増やします
（移植元 `MmdPhysicsBehaviour.cs` の注記と同じ方針）。

## 半透明の扱い（lilToon → UE マテリアル）

移植元 `Editor/MmdPhysicsImporterWindow.cs` の lilToon 設定を UE へ写した対応表です。

| 移植元 | UE 版 |
|---|---|
| `alphaMode` OPAQUE/MASK → RenderingMode 0/1 | `M_MmdToon`（`BLEND_Masked`）。OPAQUE はインスタンスで `OpacityMaskClipValue=0` |
| `alphaMode` BLEND → RenderingMode 2 | `M_MmdToonTranslucent`（`BLEND_Translucent`）。アルファは `MP_Opacity` へ |
| `_Cutoff = alphaCutoff > 0 ? alphaCutoff : 0.5` | 同じ。MASK の材質だけ `OpacityMaskClipValue` に入れる（`MmdMaterialInfo::EffectiveAlphaCutoff`） |
| `_Color = baseColorFactor` | 同じ。マスターの `BaseColor` パラメータ。色とアルファの両方に掛ける |
| `origTexture` によるプリベイク前テクスチャの差し替え | **半透明として描く材質だけ**差し替える（下記） |
| `origTexture` による MASK→Transparent 昇格 | **`alphaClass=="blend"` のものだけ**昇格させる（下記） |
| `renderQueue`: mask 由来の昇格組 → AlphaTest 帯 `2452 + slotIdx` | 昇格させず Masked のまま置くことで代替。不透明パスなので必ず半透明より先に描かれ、深度も書く |
| `renderQueue`: 真の半透明 → `3000 + slotIdx` | 不要。UE の半透明ソートキーは `Priority(16) → Distance(32) → MeshIdInPrimitive(16)` で、同一プリミティブのセクションは前 2 つが同値になるため最下位のセクション順（= スロット順）で決まる |
| TransparentMode = TwoPass | 不透明度の付け替えで再現（下記）。適用条件も移植元と同じ「半透明かつテクスチャあり」 |
| `flags` bit4 → `_UseOutline` / `_OutlineWidth` | **未対応**（README の既知の制限）。反転メッシュ法にはメッシュのセクション追加が要るため |

ブレンドモードは UE ではマテリアル単位の静的スイッチなので、1 枚のマスターでは両立できません
（インスタンスの `BasePropertyOverrides` で上書きするとインスタンスごとに静的 permutation が増え、
かつ Masked 用グラフには `Opacity` 入力が繋がっていないので不透明になります）。
そのためマスターを 2 枚生成し、どちらへ繋ぐかを材質ごとに決めます。
判断は `FMmdMaterialConversion::PlanMaterial` に純関数として切り出してあり、
アセットを触らずに分岐だけテストできます（`MmdPhysics.Editor.MaterialPlan`）。

### 描画順の 2 帯構成をどう写したか

移植元は「`origTexture` があれば一律に Transparent へ昇格 → `alphaClass` で 2 つのキュー帯に分ける」
という 2 段構えでした。帯を分けないと、共有テクスチャのモデル（Tda式ミクV4X 等）で全材質が
透明キューへ落ち、TwoPass の深度書き込みのせいで**前髪の向こうのメガネが消えます**（移植元で実測）。

UE には `renderQueue` がありません。代わりに「どのパスで描かれるか」が帯そのものなので、
**帯の振り分けをマスターの選択に畳み込みました**。

| `alphaClass` | 移植元のキュー | UE 版 |
|---|---|---|
| `"blend"`（透け髪・チーク） | Transparent 帯 `3000 + 材質番号` | `M_MmdToonTranslucent`（半透明パス） |
| `"mask"` / 未記載（肌・服・メガネ） | AlphaTest 帯 `2452 + 材質番号` | `M_MmdToon` のまま（不透明パス・深度書き込み） |
| 同上でも**肌に貼り付けた半透明**（眉・まつげ・額の影） | 同上 | `M_MmdToonTranslucent` へ昇格（下記） |

結果として、移植元が帯で解決していた「不透明寄りが先、真の半透明が後」「材質順の維持」は
どちらも UE のパス構造とソートキーで自動的に満たされ、振り分けるコードが要らなくなりました。

### 肌に貼り付けた半透明の材質は、UV 領域を測って見分ける

移植元は `origTexture` を持つ材質を**一律に** Transparent へ昇格させます。`alphaClass` は
キューの帯を選ぶだけで、昇格の条件ではありません。深度は lilToon の TwoPass が書くので、
一律に昇格させても破綻しませんでした。

UE の半透明は深度を書けないので、一律には昇格できません。肌まで半透明にすると、
材質順があとの後ろ髪が顔を突き抜けて見えます。かといって `alphaClass` だけで絞ると、
**眉・まつげが硬い黒線になります**（実機で確認）。エクスポーター側の
`_prebake_mask_alpha` の注記がその理由を説明しています。

> MASK shows whichever pixels pass the cutoff at their *raw, unblended* color —
> so a soft ~25%-opacity dark stroke shows up as a harsh, nearly-black line
> instead of the intended light brown.

MMD のモデルは **1 枚のテクスチャを「不透明な肌」と「半透明の貼り付け」で共有**します。
テクスチャ全体のヒストグラムでは区別できないので、**材質が実際に使っている UV 領域**の
アルファ分布を測ります（`FMmdMaterialConversion::MeasureUvAlpha`）。IA での実測:

| 材質 | 三角形数 | 半透明率 | 正体 | UE 版の扱い |
|---|---|---|---|---|
| `mat1` | 660 | 0% | 肌 | Masked（深度を書く） |
| `mat3` | 4576 | 1% | 肌 | Masked |
| `mat4` | 1136 | **29%** | 眉・まつげ | Translucent へ昇格 |
| `顔3` | 112 | **21%** | 額の影 | Translucent へ昇格 |

分離が明確なのでしきい値は 10% に置いています。貼り付け材質は下地の肌が深度を書くため、
昇格させても後ろ髪が顔を突き抜ける問題は起きません。

これはエクスポーター側の `_material_uv_alpha_stats` / `_is_translucent_material` と
同じ考え方です。ただしエクスポーターのしきい値（`frac_semi > 0.5`）では IA の眉（約 0.43）が
拾えないため、`extras.mmd` の `alphaClass` は `"mask"` のままになります。
**この判定を UE 側に置いたのは、「深度を書けないレンダラで半透明にして安全か」が
UE 固有の都合だからです**。`alphaClass` は「MMD 的にどう描くべきか」の仕様であって、
そこに UE の制約を持ち込むと他の受け手に影響します。

### 無加工テクスチャとディザはセットで使う

移植元は `origTexture` があれば無条件で `_MainTex` を差し替えていました。これは**同時に無条件で
Transparent + TwoPass へ昇格させていたから**成り立っていた組み合わせです。
UE 版は `"mask"` 側を Masked（アルファテスト）に残すので、そこへ無加工版をそのまま入れると壊れます。

プリベイク版は「アルファを 0/255 に平坦化しただけの版」ではありません。IA の `肌4.png` と
`orig_肌4.png` を全画素比較した実測:

| | 結果 |
|---|---|
| 残る画素の集合（シルエット） | **完全に一致**（`alphaCutoff=0.1` で切っても差が 0 画素） |
| 半透明かつ暗い画素（眉・アイラインの縁）の平均明度 | プリベイク **92** / 無加工 **56**（最大差 151） |

つまりプリベイク版は**縁の画素を下地と合成して明るくしてある**、アルファテスト前提のテクスチャです。
無加工版をアルファテストで描くと、その縁の暗い画素がフルの濃さで出ます。
実機では **IA の眉と目の輪郭に黒いフチがはっきり出る**という形で現れました。

したがって**無加工版を使うのは、実際にアルファブレンドできる材質（Translucent）だけ**です。
アルファテストで描く材質にはプリベイク版が正しい相方になります。

| | プリベイク版 | 無加工版 |
|---|---|---|
| 想定する描き方 | 硬いアルファテスト | アルファブレンド |
| UE 側の組み合わせ | Masked + `UseOrigTexture=0` | Translucent + `UseOrigTexture=1` + サブパス |

**試して捨てた案 — ディザ**: Masked のまま `OpacityMask` を確率的に抜けば、深度を書いたまま
縁を柔らかくできます（移植元 TwoPass の目的に構造的には一致）。マスターに
`DitherWeight` として実装してありますが、**顔のような近距離の面では市松模様がそのまま見えた**
ため既定は 0 にしています（TemporalAA を有効にした実機ビューポートで確認）。
必要なら手動で 1 にして再検証できます。

## TwoPass（不透明サブパス）は「不透明度の付け替え」で再現する

lilToon の TwoPass は 2 パス構成で、1 パス目が**フルカラー・深度書き込みで α をクリップ**します。

```hlsl
// Shader/Includes/lil_common_frag_alpha.hlsl
clip(alphaRef - _SubpassCutoff);   // _SubpassCutoff の既定は 0.5
```

`lts_twotrans.shader` の `_PreZWrite = 1` / `_PreColorMask = 15`、SubShader のキューも
`"AlphaTest+10"`。つまり **α ≥ 0.5 の部分は完全な不透明として描かれ**、0.5 未満だけがブレンドされます。

UE のマテリアルは 1 パスしか持てないので、同じ見え方を不透明度の付け替えで作ります。

```
α >= SubpassCutoff → 1.0    (サブパスが不透明で描いた分)
α <  SubpassCutoff → α      (ブレンドパスの分)
```

1 層なら lilToon と同じ結果です（サブパスが塗った色の上に同じ色を混ぜても変わらないため）。
マスターの `SubpassCutoff`（既定 0.5）と `SubpassWeight`（0/1）で制御します。

**これを落とすと透けすぎます。** IA の `orig_髪の毛.png` はアルファの 98.4% が 0.75 以上で、
lilToon ではほぼ不透明に描かれます。素のアルファブレンドにすると髪全体が一様に薄くなり、
「前髪ごしに眉が透けすぎる／隠れない」形で移植元と食い違いました（2026-08-15 に修正）。

適用条件も移植元と同じで、**半透明かつテクスチャを持つ材質だけ**です
（移植元 434 行 `if (modeInt == 2 && (hasBaseTex || useOrigTexture)) transparentMode = 2;`）。
テクスチャの無い単色ガラス（IA のレンズ = 拡散色アルファ 0.7）は素のブレンドのままにします。

**再現しきれない部分**: サブパスの深度書き込みまでは再現できません。lilToon では
α < 0.5 の裾どうしが重なっても手前の 1 層しかブレンドされませんが、UE では重なった分だけ濃くなります。
`DitherTemporalAA`（`/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA`）を
`OpacityMask` に噛ませれば深度を書いたまま確率的に抜けますが、TemporalAA/TSR が前提でノイズが乗り、
かつこの組はエクスポーター自身が「見た目ほぼ不透明」と分類したものなので採っていません。
実測（IA）では昇格対象は前髪と後ろ髪の 2 材質だけで、`"mask"` 側は肌・口内・顔でした。

★`TranslucencySortPriority` は `UMaterialInterface` ではなく `UPrimitiveComponent` のプロパティで、
1 コンポーネントに 1 個しかありません。スロット単位の並び替えには使えないため、インポーターからは触りません。

## マスターは「方式を切り替えられる検証台」になっている

どの方式が MMD / 移植元に近いかはモデルの作り方でも変わるので、**アセットを作り直さずに
エディタ上で見比べられる**ようにしてあります。マスター 2 枚は同じパラメータ一式を持つので、
マテリアルインスタンスの `Parent` を付け替えても設定が生きます。

| パラメータ | 効く親 | 意味 |
|---|---|---|
| `UseOrigTexture` | 両方 | 0 = プリベイク版 / 1 = 無加工版。両方のテクスチャを常に割り当ててある |
| `AlphaCutoff` | Masked | アルファのしきい値。負 = アルファを見ない（`alphaMode=OPAQUE`） |
| `DitherWeight` | Masked | 0 = 硬いカット / 1 = ディザで縁を柔らかく |
| `SubpassCutoff` | Translucent | TwoPass 相当のしきい値（既定 0.5 = lilToon の `_SubpassCutoff`） |
| `SubpassWeight` | Translucent | 0 = 素のブレンド / 1 = α ≥ Cutoff を不透明として描く |

★マスク閾値は `BasePropertyOverrides.OpacityMaskClipValue` ではなく**マテリアルのパラメータ**
（`AlphaCutoff`）で持っています。BasePropertyOverrides は値ごとに静的 permutation が増えるうえ、
ディザと硬いカットで必要な閾値が違って両立できないためです。マテリアル側の
`OpacityMaskClipValue` は 0.5 固定にして、「出力 > 0.5 なら描く」という約束にそろえてあります。

★ディザはエンジンの `DitherTemporalAA` マテリアル関数を使わず、Custom ノードに HLSL を直接
書いています。エンジン関数は出力がどの閾値で切られる前提なのかがアセットの中にあって
読めず（Python からも取り出せませんでした）、正しさを目視でしか確認できないためです。

## GLB バイナリからのテクスチャ抽出

移植元 `PrepareGlbBinaryAccess` / `ExtractTextureFromGlb` に対応するのが
`MmdPhysics::GlbImageExtractor`（`MmdGlbMaterialReader.h`）です。
`textures → images → bufferViews` を辿って BIN チャンクから生の PNG/JPEG を切り出し、
UE 側（`MmdMaterialConversion.cpp` の `FTextureResolver`）が `UTextureFactory` に渡して
`UTexture2D` アセットにします。保存先はモデルのフォルダ配下の `MMD_ExtractedTextures`。

これが要るのは、トゥーン / スフィア / `origTexture` の画像が **glTF の標準マテリアルから
参照されていない**ためです。移植元では UniGLTF がそうした画像をインポートしませんでした。
UE の Interchange は実測（IA）では 22 枚すべてを取り込んでいましたが、取り込み設定や
エクスポーター次第で落ちうるので、**名前で見つからなければ抽出**というフォールバックにしています
（移植元の `FindOrExtractTextureByIndex` と同じ形）。

移植元との差:

- BIN チャンクの先頭を `20 + chunk0Length` と直接計算せず、既にある
  `GlbPhysicsReader::ParseGlb` でチャンクを走査して取り出します（並び順を仮定しないぶん安全で、
  物理側と同じ経路になります）
- `mimeType` が欠けている `.glb` に備えて、拡張子はファイル先頭のマジックでも判別します
  （移植元は `mimeType` に `jpeg` を含まなければ png 決め打ち）
- GLB の読み込みは**最初に抽出が要ったときだけ**行います（全部インポート済みなら 40MB を読まない）
- 抽出結果は失敗（`nullptr`）も含めてキャッシュします（移植元 `extractedTextureCache` 相当）

## 移植で数値がずれる箇所（対処済み）

翻訳としては自然に見えるのに、数値が食い違う原因になる箇所です。

### 1. 浮動小数点モード（最重要）

UE の既定は高速モードで、`a*b+c` の FMA 収縮や式の再結合を許します。C# の RyuJIT は
演算ごとに厳密なので、既定のままだと 1〜2 ULP ずれます。

単発では無視できる差ですが、スカート剛体どうしが接触し続ける系では指数的に増幅します。実測:

| フレーム | 最大位置差 |
|---|---|
| 1 | 2.4e-07 |
| 20 | 2.0e-04 |
| 60 | 5.4e-01 |

`MmdPhysicsCore.Build.cs` で `FPSemantics = FPSemanticsMode.Precise` を指定すると
**300 フレームでビット完全一致**になります。

### 2. `Math.Sqrt` / `Math.Sin` などの中間精度

C# の `Math.*` は引数を double に昇格して計算し、呼び出し側で `(float)` にキャストします。
`sqrtf` / `sinf` を使うと最終ビットが食い違います。`MmdMathTypes.h` の
`MSqrt` / `MSin` / `MCos` / `MAcos` / `MAsin` / `MAtan2` が中間 double を再現します。
`Math.Pow` も同様（`PhysicsWorld::DampingFactor`）。

### 3. `Vec3 / float` は逆数 1 回の乗算

C# は `var i = 1f/s; return a * i;` です。成分ごとの除算に書き換えると数値が変わります。

### 4. `Quat` の既定値は w=0

移植元の `new Quat()` は全ゼロです。単位が必要な箇所は必ず `Quat::Identity` を使います。
`Matrix4x4` も既定は全ゼロで、単位行列は `Matrix4x4::Identity()`。

### 5. `Math.Round` は銀行家丸め

`MiniJson::Int` は `FMath::RoundHalfToEven` を使います（JSON の index は整数なので
実害は出ませんが、式を一致させています）。

## C++ / UE 固有のハマりどころ

### MSVC は dllexport クラスの静的メンバをまとめて宣言できない

```cpp
static double ProfBroad, ProfBuild;   // error C2487
```

1 宣言ずつに分けます（`PhysicsWorld` のプロファイル変数）。

### 静的定数は個別に API マクロが要る

`struct Vec3` 自体は非エクスポートですが、`Vec3::Zero` などの静的データメンバは
`static MMDPHYSICSCORE_API const Vec3 Zero;` としないと他モジュールからリンクできません。

### 静的初期化順序

`RigidTransform::Identity` を `Quat::Identity` から作ると、TU をまたぐ初期化順序が未定義なので
全ゼロを掴む可能性があります。**静的定数はすべてリテラルから直接構築**しています。

### ラムダの自己再帰が書けない

移植元のローカル再帰関数（`PmxPhysicsBuilder` の `Fk` / `Align`、`GlbPhysicsReader` の `World`）は
プライベートメソッドへ切り出しています。

### 例外

UE は既定で C++ 例外が無効です。移植元がコンテナ不正時に `InvalidDataException` を投げていた箇所
（`GlbPhysicsReader.ParseGlb`）は、警告を積んで `nullptr` を返す形にしています。
`extras.mmd` の中身に対する「例外を投げず読み進める」方針は移植元のままです。

## 検証

### A. 数値パリティ（C# 版との突き合わせ）

`Tools/CsReference/` は移植元 C# を **コピーせず csproj からリンク**してビルドする
.NET コンソールハーネスです。upstream を更新すれば基準側も自動で追従します。
`MathTypes.cs` が唯一持つ `UnityEngine` 依存（explicit operator 4 箇所）は
`UnityShim.cs` の最小定義で満たしており、**移植元は 1 行も改変していません**。

基準 CSV を作る:

```
dotnet run --project Tools/CsReference -c Release -- <glb> 300 Tools/CsReference/out/ia_300_cs.csv
```

### テストの走らせ方

テストデータ（モデル）は再配布できないため、環境変数で外から与えます。未設定のテストは
黙ってスキップするので、データを持たない環境でも suite は green になります。

```powershell
$env:MMD_PARITY_GLB    = "<...>\IA.glb"
$env:MMD_PARITY_CSV    = "Tools\CsReference\out\ia_300_cs.csv"
$env:MMD_PARITY_FRAMES = "300"
$env:MMD_CONV_SKELMESH = "/Game/IA/IA"

& "<UE>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" <project>.uproject `
  -ExecCmds="Automation RunTests MmdPhysics" `
  -unattended -nopause -nullrhi -nosplash -testexit="Automation Test Queue Empty"
```

| テスト | 見ているもの |
|---|---|
| `MmdPhysics.Core.Math` | YXZ オイラー、XYZ 分解の往復、カプセル慣性マージン |
| `MmdPhysics.Core.CollisionMask` | 非衝突グループのビット解釈（反転していないか） |
| `MmdPhysics.Core.Equilibrium` | 拘束が保持され Baumgarte が余計なエネルギーを注いでいないか |
| `MmdPhysics.Core.Pendulum` | 重力・並進ロック・減衰が機能しているか |
| `MmdPhysics.Core.GlbParity` | **C# 版とビット一致するか**（実モデル 300 フレーム） |
| `MmdPhysics.Core.MaterialReader` | 各マテリアルの `extras.mmd` を読めているか |
| `MmdPhysics.Core.GlbImageExtract` | 手で組んだ GLB で `alphaCutoff` / `origTexture` の読み取りと画像の切り出し（`bufferView.byteOffset` の適用、範囲外の拒否）。**データ不要** |
| `MmdPhysics.Editor.MaterialPlan` | `alphaMode` / `alphaClass` / `origTexture` / `alphaCutoff` から親マスターとマスク閾値が決まる分岐。**データ不要** |
| `MmdPhysics.Bridge.UeSpace` | 位置と回転が同一の純回転か（行列式 +1） |
| `MmdPhysics.Bridge.ImportConvention` | 実際に取り込んだスケルトンと座標系が合うか |
| `MmdPhysics.Editor.WirePhysics` | 配線 → 評価 → 書き戻しが端から端まで通るか |
| `MmdPhysics.Editor.ConvertMaterials` | 全スロットに MI が付くか、半透明にすべき材質だけが Translucent 親か、マスク閾値が `alphaCutoff` と一致するか、`origTexture` が無加工版へ差し替わっているか |

### 自動化していない部分

エディタでの目視確認は自動テストでは代替できません。実機で確認してください。

- 髪が体を貫通しないか、スカートの挙動が MMD と近いか
- **眉やまつげが前髪越しにふんわり透けるか**（`origTexture` + Translucent 昇格が効いているか）。
  効いていれば出力ログに `半透明 N〈うち origTexture 昇格 M〉/ 無加工テクスチャ K` が出ます。
  IA では「半透明 4〈うち origTexture 昇格 2〉/ 無加工テクスチャ 7」になります
- 透け髪の房どうしの重なり（TwoPass 相当が無いぶん、移植元と差が出うる唯一の箇所）
