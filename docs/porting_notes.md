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
- ファイル名・クラス名・メソッド名を対応させ、移植元の差分パッチを目視で当てられる状態を保つ
- コンテナのみ UE 型を使う（`List<T>`→`TArray<T>`、`Dictionary`→`TMap`、`string`→`FString`）
- **数学型は自前を維持**（`Vec3` / `Quat` / `Matrix4x4` / `RigidTransform` / `Matrix3x3`）
- コード内の日本語コメント（事故記録・実測値）はすべて残す。移植元の設計判断の記録そのもの

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

| | `FixedTimeStep` | `SubSteps` | 実効刻み | Split Impulse | 対応する移植元 |
|---|---|---|---|---|---|
| `PhysicsWorld`（コア） | 1/30 | 4 | 1/120 | OFF | `Core/PhysicsWorld.cs` |
| `FAnimNode_MmdPhysics`（ノード） | 1/60 | 2 | 1/120 | **ON** | `Unity/MmdPhysicsBehaviour.cs` |

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

### 静止しているのに揺れ続ける（Baumgarte が実速度へ漏れる）

**症状。** ダンスが始まる前のほとんど動かない区間で、髪とスカートが揺れ続けます。

**実測。** `MmdPhysics.Core.IdleSettle` が、ボーンを一切動かさずにステップだけ回して残留振動を測ります
（刻みは再生時と同じ 1/60×2。パリティテストはコア既定 1/30×4 なので、
**再生される設定での挙動はそちらでは見ていません**）。IA での値:

- 60 秒回しても **12〜14cm の振れ幅のリミットサイクル**が続く（髪は 66 → 9cm と収束する）
- 騒いでいるのはスカート第 2 段。**1.0〜1.5Hz**、最大速度 120cm/s = 細かい震えではなく**大きな振り子運動**
- 重力 98・振り子長 2 単位の固有振動数 `(1/2π)√(98/2) ≒ 1.1Hz` と一致 = **固有振動数での共振**

**切り分け。**

| 条件 | 最後の 5 秒の最大振れ | 平均 |
|---|---|---|
| 既定（1/60×2） | 14.18cm | 3.77cm |
| コア既定（1/30×4） | 14.18cm | 3.75cm |
| 重力 0 | **0.06cm** | 0.001cm |
| 反復 10→20 | 13.55cm | 3.99cm |
| SubSteps 2→4 | 16.89cm | 5.71cm |
| Bullet 順（ジョイント→接触） | 16.69cm | 4.11cm |

刻みを変えても一致するので **UE 側の統合ではなく移植した物理そのもの**（＝移植元 C# でも同じ）。
重力 0 で完全に静止するので、ソルバが無条件に注いでいるのではなく減衰も正しく効いています。

**原因。** 位置補正（Baumgarte）が実速度に残ることです。貫入やジョイントのずれを直す補正が
そのまま速度になり、揺れ物へエネルギーを注ぎ続けます。減衰を強くしても止まりません
（**減衰は拘束を解く前に掛かる**ので、解いた後に拘束が決めた速度には効かない）。
剛体の減衰は 0.9（速度が毎秒 10% まで落ちる）なのに 60 秒後も揺れているのはこのためです。

**対策。** 位置補正を擬似速度で解いて実速度に残さない（split impulse）。

| | 最大振れ | 平均 | スカート_2_6 |
|---|---|---|---|
| 両方 OFF | 14.18cm | 3.77cm | 14.09cm / 1.5Hz / 123cm/s |
| 接触のみ | 13.99cm | 3.28cm | 13.99cm / 154cm/s |
| ジョイントのみ | 12.63cm | 2.32cm | 11.70cm / 85cm/s |
| **両方 ON** | **7.17cm** | **0.96cm** | **5.56cm / 53cm/s** |

**両方要ります**（スカートはジョイントと脚との接触の両方で支えられているため）。
両方 ON では振れが時間とともに**減り**ます（8.53 → 5.56）。OFF は増えます（13.62 → 14.09）。
両方 ON の上に反復 20（8.91/1.72）や Bullet 順（8.67/1.56）を足すと悪化するので、素直に両方だけ。

ノード側の既定を ON にしました。コア側は OFF のまま（パリティテストがビット一致で比べるため）。
刻みと同じ「ノードだけが調整値を持つ」構造です。戻すときは AnimBP の MMD Physics ノードの
`Use Split Impulse` / `Use Joint Split Impulse` を切ります。

★残り 7cm は消えていません。原因は同じ系統（拘束が静的な釣り合いに収束しきらない）と見ていますが、
未追跡です。

### 積み残した時間は捨てる（移植元との意図的な差）

`StepSimulation` は 1 回の呼び出しで走らせる内部ステップを `MaxStepsPerCall`（8）で打ち切りますが、
**超過分の時間は `_accumulator` に残さず捨てます**。

捨てないと、シェーダーコンパイルやアセットロードで数秒止まったぶんが借金として積まれ、
以後しばらく毎フレーム 8 ステップ = **実時間の 8 倍速**で回り続けます。髪とスカートが暴れて
体に潜り込み、**貫入したまま釣り合って戻らなくなります**。起動時の再整合
（`PoseResetDelayFrames`）は最初の数フレームで終わっているので、復帰もしません。
「アクターを作った直後に一度崩れて、そのまま直らない」という壊れ方の正体がこれでした。

落ちたフレームの時間を取り戻す価値は無いので、実時間へ復帰させる側を採ります
（いわゆる spiral of death 対策で、固定刻みアキュムレータでは標準的な処理です）。
捨てた時間は診断用に `PhysicsWorld::DiscardedTime` へ積んでいます。

数値パリティ（`MmdPhysics.Core.GlbParity`）には影響しません。パリティテストは固定 dt で
300 フレーム回すので、積み残しが上限に達すること自体がありません。

### 走行中に物理を初期状態へ戻す

貫入平衡に落ちたときの復帰手段として、Blueprint から
**`Reset MMD Physics`**（`UMmdPhysicsFunctionLibrary::ResetMmdPhysics`）を呼べます。
スケルタルメッシュコンポーネントを渡すと、Post-Process AnimBP（と通常の AnimGraph）の中の
`FAnimNode_MmdPhysics` をすべて探して `RequestPoseReset()` を立てます。
再整合は次の評価から `PoseResetDelayFrames` 回ぶん行います（アニメーションが新しい姿勢を
適用した後の骨格に合わせる必要があるため、1 フレームでは足りません）。

モーションの切り替え直後・ループの折り返し・テレポート直後に呼ぶのが想定用途です。

## 半透明の扱い（lilToon → UE マテリアル）

移植元 `Editor/MmdPhysicsImporterWindow.cs` の lilToon 設定を UE へ写した対応表です。

| 移植元 | UE 版 |
|---|---|
| `alphaMode` OPAQUE/MASK → RenderingMode 0/1 | `M_MmdToon`（`BLEND_Masked`）。OPAQUE はインスタンスの `AlphaCutoff` を負の値にして「アルファを見ない」を表す |
| `alphaMode` BLEND → RenderingMode 2 | `M_MmdToonTranslucent`（`BLEND_Translucent`）。アルファは `MP_Opacity` へ |
| `_Cutoff = alphaCutoff > 0 ? alphaCutoff : 0.5` | 同じ。MASK の材質だけインスタンスの `AlphaCutoff` パラメータに入れる（`MmdMaterialInfo::EffectiveAlphaCutoff`。下の「マスターのパラメータ」も参照） |
| `_Color = baseColorFactor` | 同じ。マスターの `BaseColor` パラメータ。色とアルファの両方に掛ける |
| `origTexture` によるプリベイク前テクスチャの差し替え | **半透明として描く材質だけ**差し替える（下記） |
| `origTexture` による MASK→Transparent 昇格 | **`alphaClass=="blend"` のものだけ**昇格させる（下記） |
| `renderQueue`: mask 由来の昇格組 → AlphaTest 帯 `2452 + slotIdx` | 昇格させず Masked のまま置くことで代替。不透明パスなので必ず半透明より先に描かれ、深度も書く |
| `renderQueue`: 真の半透明 → `3000 + slotIdx` | 不要。UE の半透明ソートキーは `Priority(16) → Distance(32) → MeshIdInPrimitive(16)` で、同一プリミティブのセクションは前 2 つが同値になるため最下位のセクション順（= スロット順）で決まる |
| TransparentMode = TwoPass | コンポーネントを 2 つ使って再現（下記）。ただし髪だけに適用し、貼り付け材質（眉）は素のブレンド |
| `flags` bit4 → `_UseOutline` / `_OutlineWidth` | `M_MmdOutline` + `UMmdOutlineComponent`（下記）。太さは `edgeSize × OutlineWidthScale`（既定 0.15。移植元は lilToon の `_OutlineWidth` 既定 0.08 に合わせた係数） |

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
| UE 側の組み合わせ | Masked（`AlphaCutoff` でカット） | Translucent + 不透明サブパス |

**試して捨てた案 — ディザ**: Masked のまま `OpacityMask` を確率的に抜けば、深度を書いたまま
縁を柔らかくできます（移植元 TwoPass の目的に構造的には一致）。4x4 の市松をフレームごとに
位相をずらす Custom ノードとして実装して実機で見ましたが、**顔のような近距離の面では
市松模様がそのまま見えた**ため捨てました（TemporalAA を有効にしたビューポートで確認）。
コードは残していません。再挑戦するなら、この結果を踏まえて別の手を考えてください。

## TwoPass はコンポーネントを 2 つ使って再現する

lilToon の TwoPass は 2 パス構成で、1 パス目が**フルカラー・深度書き込みで α をクリップ**します。

```hlsl
// Shader/Includes/lil_common_frag_alpha.hlsl
clip(alphaRef - _SubpassCutoff);   // _SubpassCutoff の既定は 0.5
```

`lts_twotrans.shader` の `_PreZWrite = 1` / `_PreColorMask = 15`、SubShader のキューも
`"AlphaTest+10"`。つまり **α ≥ 0.5 の部分は完全な不透明として描かれ**、0.5 未満だけがブレンドされます。

**UE のマテリアルは 1 枚で 2 パスを持てません**（Masked と Translucent は排他）。
そこで **2 つのコンポーネントに分けて 2 回描きます**。

```
本体 SkeletalMeshComponent  … Masked (α >= 0.5 の芯 + 深度)      ← 1 パス目
└─ UMmdSoftPassComponent    … 同じメッシュを Translucent で素のα ← 2 パス目
```

2 パス目は「α < 0.5 だけ」に絞る必要はありません。α ≥ 0.5 の部分は 1 パス目が既に同じ色で
不透明に塗っているので、上から同じ色を重ねても変わらないためです（絞ると毛先が本来より濃くなります）。
新しいマテリアルも不要で、既存の `M_MmdToon` と `M_MmdToonTranslucent` をそのまま使います。

### 髪には使い、眉には使わない

**この使い分けが要ります。** 判定は `bOverlay`（UV 領域のアルファ分布）で行います。

| | 性質 | 描き方 |
|---|---|---|
| 髪（前髪・後ろ髪） | 房が何枚も重なる | **TwoPass**。芯で深度を書かないと足し算で飽和し、グラデーションが潰れる |
| 貼り付け材質（眉・まつげ・額の影） | 顔に貼り付いた 1 枚。自分同士は重ならない | **素のアルファでブレンド**。下地の顔が不透明で深度を書くので安全 |
| テクスチャの無い半透明（レンズ） | 単色ガラス | 素のブレンド（移植元も TwoPass にしない） |

★眉に TwoPass を当てると**黒フチが出ます**（実機で確認）。1 パス目は α ≥ 0.5 を
**生の色のまま不透明に塗る**ので、0.5〜0.7 の薄い墨で描かれた眉はそこで濃さが戻ります。
**lilToon の TwoPass 自体がこの帯を濃くする近似**であり、MMD は全部を素のアルファで
混ぜています。目標は lilToon ではなく MMD なので、貼り付け材質は素直に混ぜます。

### 試して捨てた案

1 枚のマテリアルで済ませようとして 3 回失敗しました。同じ道を辿らないよう記録します。

| 案 | 結果 |
|---|---|
| Translucent + 不透明度の付け替え（α ≥ 0.5 → 1.0） | 深度を書けないので房が重なると飽和し、グラデーションが潰れる |
| Translucent + `Output Depth and Velocity` | しきい値（`OpacityMaskClipValue`）が効くのは**速度出力だけで深度書き込みには効かない**。毛先まで深度を書き、奥の髪が消えて背景が透ける |
| Masked + ディザ（確率的に抜く） | 顔のような近距離の面で市松模様がそのまま見える |

`SubpassWeight` / `SubpassCutoff` は上記 1 案目の名残ですが、2 パス目の濃さ調整に使えるので
パラメータとしては残してあります（2 パス目は `SubpassWeight = 0` で使う）。

★`TranslucencySortPriority` はコンポーネント単位なので、輪郭線(-1) → 毛先パス(0) の順に
描かれるよう設定しています。

★`TranslucencySortPriority` は `UMaterialInterface` ではなく `UPrimitiveComponent` のプロパティで、
1 コンポーネントに 1 個しかありません。スロット単位の並び替えには使えないため、インポーターからは触りません。

## マスターのパラメータ

| パラメータ | 効く親 | 意味 |
|---|---|---|
| `BaseColorTex` | 両方 | プリベイク版 or 無加工版（どちらを入れるかは `PlanMaterial` が決める） |
| `BaseColor` | 両方 | 拡散色（`baseColorFactor`）。色とアルファの両方に掛かる |
| `AlphaCutoff` | Masked | アルファのしきい値。負 = アルファを見ない（`alphaMode=OPAQUE`） |
| `SubpassCutoff` | Translucent | TwoPass 相当のしきい値（既定 0.5 = lilToon の `_SubpassCutoff`） |
| `SubpassWeight` | Translucent | 0 = 素のブレンド / 1 = α ≥ Cutoff を不透明として描く |
| `ToonTex` / `UseToon` / `SphereTex` / `SphereMulWeight` / `SphereAddWeight` | 両方 | トゥーンとスフィア |
| `EdgeColor` / `EdgeSize` | 両方 | 輪郭線用。本体マテリアルは描画に使わず、`MmdOutlineComponent` がここから読んで輪郭線マテリアルへ渡す |

★マスク閾値は `BasePropertyOverrides.OpacityMaskClipValue` ではなく**マテリアルのパラメータ**
（`AlphaCutoff`）で持っています。BasePropertyOverrides は値ごとに静的 permutation が増えるためです。
マテリアル側の `OpacityMaskClipValue` は 0.5 固定にして、「出力 > 0.5 なら描く」という約束に
そろえてあります。

★`BaseColorTex` は 1 枚だけです。プリベイク版と無加工版の両方をサンプラで持って切り替える
作りも試しましたが、マテリアルには 16 サンプラの上限があり、常時 1 枚余計に使うのは
割に合いません。判定を外したいときはインスタンスの `BaseColorTex` をもう一方のアセットへ
差し替えられます（無加工版も取り込み済みなので選べます）。

## 輪郭線はメッシュを複製せずに描く

MMD の輪郭線は「モデルをもう一度、法線方向へ膨らませて表面を捨てて描く」反転ハルです。
UE でも同じ描き方をしますが、**メッシュには一切手を入れません**。

```
SkeletalMeshActor
├── SkeletalMeshComponent（本体）      … 物理 ABP が付く
└── UMmdOutlineComponent（輪郭線）     … 同じスケルタルメッシュをもう一度描く
    ├── SetLeaderPoseComponent(本体)   … ボーンは本体の結果をそのまま使う
    ├── MorphTargetWeights を毎フレーム複製
    └── 全スロットに M_MmdOutline の動的インスタンス
```

`M_MmdOutline`（Unlit / Masked / 両面）:

| 出力 | 内容 |
|---|---|
| WorldPositionOffset | `VertexNormalWS × (EdgeSize × OutlineWidthScale)` |
| OpacityMask | `saturate(-TwoSidedSign) × UseOutline` … 表面を落として裏面だけ残す |
| EmissiveColor | `EdgeColor`（材質ごと） |

★**セクションを複製して焼き込む方式は採りません。** MMD モデルは表情モーフを大量に持ちます
（IA は全 19 セクションが 45 モーフ）。複製するとモーフのデルタまで作り直すことになり、
しかも取り込んだアセットを書き換えるので**モデルを再インポートすると消えます**。
同じメッシュを参照すればモーフは元のまま効き、ウェイトも `MorphTargetWeights` を
配列ごとコピーするだけで揃います（同じメッシュなので添字が一致するため）。

★**モーフだけは `LeaderPoseComponent` が運んでくれません**（ボーンだけです）。
毎フレーム写さないと、表情を変えたときに輪郭線だけ元の顔のまま取り残されます。

代償はメッシュ 1 体分の描画が増えることですが、これは反転ハルの原理的なコストで、
MMD 自身も同じ枚数を描いています。

★輪郭線マテリアルは**動的インスタンス**です（コンポーネントが登録時に作る）。
そのため詳細パネルで太さを触っても自動では反映されず、`PostEditChangeProperty` で
作り直しています。太さは MMD と見比べて決める値なので、その場で効くことが要件です。

## モーション（VMD）はエクスポーターが焼き込む — プラグインに VMD リーダーは無い

`mmd2gltf-gui` は VMD を **glTF 標準のアニメーション**としてベイクします
（`mmd2gltf/animation.py`：ベジェ補間の評価 → MMD と同じ変形順（変形階層 → 付与 → CCD-IK + 軸制限）
→ 30fps で記録）。つまりモーションは `extras.mmd` ではなく **glTF の本体側**に載っており、
UE 標準の Interchange が `AnimSequence` として取り込みます。

したがってインポーター側に VMD の読み取りも IK ソルバも要りません。実際 IA の `.glb` には
`rotation` 53ch + `translation` 1ch（各 7001 キー = 30fps 丁度）が入っており、UE では
`IA_Anim`（ボーン 54 トラック / 7000 フレーム）として正しく取り込まれています。

`extras.mmd` 側にはボーンの `flags` / `layer` / `inherit_parent` / `fixed_axis` / `ik{...}` が
残っていますが、これは**実行時 IK を組み直したいエンジン向けの保存**であって、
ベイク済みモーションを再生するだけなら使いません。

### 【3】がアニメーションを割り当てる

取り込まれた `AnimSequence` は、コンポーネントに割り当てないと再生されません。以前は
「ここでアニメーションの設定は触らない」としていたため、`BP_<メッシュ名>` を置いても
参照ポーズのまま立っているだけでした。現在は `FMmdActorBuilder::FindAnimationFor` が
**メッシュのスケルトンで再生できる `AnimSequence`** を探して割り当てます。

- 探索はアセットレジストリの **タグ**（`UAnimationAsset::Skeleton` は `AssetRegistrySearchable`）
  で絞り、候補以外は読み込みません。全部 `GetAsset()` するとプロジェクト内の全モーションを
  展開することになるためです（実測: IA のモーション 1 本で圧縮データ 72MB）
- 優先順は「同じフォルダ (+2) > 名前がメッシュ名で始まる (+1)」。Interchange は
  `<メッシュ名>_Anim` をメッシュの隣に置くので通常は一意に決まります。同点は名前順
  （レジストリの列挙順に依存させないため）
- 再生モードは `AnimationSingleNode`（ループ）。**Post-Process AnimBP は再生モードに関わらず
  後段で必ず走る**ので物理と共存し、ボーン追従の剛体には書き戻さないため体はモーションどおりに動きます
- `SetAnimationMode` は `bForceInitAnimScriptInstance=false` で呼びます。触っているのは
  ワールドに登録されていない**コンポーネントテンプレート**で、アニメーションインスタンスを
  作らせる場面ではないためです

### モーフ（表情）のアニメーションは UE が取りこぼす

`.glb` には `weights` チャンネルが 1 本入っています（IA で 1699 キー × 45 トラック、0〜221.3 秒）。
UE がモーフ名を読む場所（`mesh.extras.targetNames`）にも 45 件が入っており、データは仕様どおりです。
それでも取り込み後の `AnimSequence` はカーブ 0 本になります。

原因は `Engine/Plugins/Interchange/Runtime/Source/Import/Private/Gltf/InterchangeGltfAnimation.cpp`：

1. `weights` チャンネルは `MorphTargetAnimations.FindOrAdd(AnimatedNodeIndex)` で
   **メッシュノード番号**をキーに積まれる
2. それを `ProcessRiggedAnimations()` にそのまま渡す（= メッシュノードをスケルトンルート扱い）
3. `AcquireTrackNode(メッシュノードUid)` → `SetCustomSkeletonNodeUid(メッシュノードUid)`

ボーン側は **root joint の Uid** でトラックノードを作るのでキーが一致せず、別々のトラックノードに
なります。モーフ側は「スケルトン」としてメッシュノードを指しているため後段で解決できず消えます。
素の設定で取り込み直しても再現しました（生成物は `AnimSequence` 1 本・ボーン 54 トラック・カーブ 0 本）。

`weights` チャンネルの target は glTF 仕様上メッシュを持つノードしか指定できないので、
**エクスポーター側で回避する余地はありません**。そこでプラグイン側で埋めます。

### 埋め方（`FMmdMorphAnimation::ApplyMorphCurves`）

【3】アクター生成が、割り当てた `AnimSequence` に対して実行します（`.glb` を渡したときだけ）。

1. `GlbPhysicsReader::ParseGlb` で JSON チャンクと BIN チャンクに分ける（物理と同じ経路）
2. `weights` チャンネルを 1 本探し、`target.node → nodes[n].mesh` からメッシュ番号を得る
3. `meshes[i].extras.targetNames` でモーフ名を得る（**UE がモーフ名を読むのと同じ場所**）
4. サンプラの `input`（時刻）/ `output`（ウェイト）を `ReadFloatAccessor` で読む
5. トラックごとに `UAnimationBlueprintLibrary::AddCurve` + `AddFloatCurveKeys`
6. `Skeleton->AccumulateCurveMetaData(name, false, true)` で「モーフを動かすカーブ」と登録し、
   **スケルトンのアセットも保存する**（FBX インポータの `SkeletalMeshEdit.cpp` と Interchange の
   `InterchangeAnimSequenceFactory.cpp` が同じことをしている）

### カーブを足すだけでは動かない（スケルトンの curve metadata と、その保存）

UE 5.5 は「そのカーブがモーフを動かすか」を**カーブ名では判断しません**。
`FBoneContainer::CacheRequiredAnimCurves`（`BoneContainer.cpp`）が
**スケルトン**（または `SkeletalMesh` の `UAnimCurveMetaData`）のメタデータだけを見て
`ECurveElementFlags::MorphTarget` を組み立て、`FAnimInstanceProxy::UpdateCurvesToEvaluationContext`
（`AnimInstanceProxy.cpp`）はそのフラグが立ったカーブしか `MorphTargetCurve` に入れません。
名前がモーフターゲットと完全に一致していても、フラグが無ければ何も起きません。

ここに 2 つ落とし穴があります。どちらも**「体は踊るのに顔だけ動かない」**という壊れ方をします。

- **登録先はスケルトンで、アニメーションを保存しても付いてこない。**
  `AccumulateCurveMetaData` は `MarkPackageDirty()` するだけなので、スケルトンのアセットを
  明示的に保存しないとエディタを閉じた時点で登録が消え、カーブだけが残ります。
  【3】は `MorphMetaDataSet > 0` のときスケルトンも `SavePackage` します
- **既存カーブを飛ばすときも登録は確かめる。** キーの二重追加は避けたいが、
  登録が失われている状態からの復旧は「カーブが既にある」ケースそのものです。
  カーブの有無とメタデータの有無は独立に判定します（`EnsureMorphMetaData`）

自動テストは名前の一致だけでなく、**スケルトンに登録されているか**と
**スケルトンが未保存のまま残っていないか**まで見ます（前者だけ見ていたので取りこぼしました）。

注意している点:

- 出力の並びは **フレームごとに全トラック**（t0 の track0..N-1 → t1 の track0..N-1 …）。
  `CUBICSPLINE` のときは 1 キーにつき `in / value / out` が並ぶので真ん中だけ取る
  （接線は捨てる。Interchange のモーフ経路も捨てている）
- **メッシュに同名のモーフターゲットが無いトラックは飛ばす。** UV モーフはここに落ちる
- **既にカーブがあるものは触らない。** UE 側が直った将来に二重で入れないため。
  作り直しても増えないことは自動テストで見ている
- `componentType` が FLOAT (5126) でなければ何もしない（glTF 的には正規化整数もあり得るが、
  エクスポーターは float で出す）。`ReadFloatAccessor` は `byteStride` を見ない = 詰まっている前提

★これは **UE の不具合を埋めるための処理**であって仕様の実装ではないので、1 ファイルに閉じてあります。
UE 側が直ったら `MmdMorphAnimation.*` を消して【3】の呼び出しを外すだけで済みます。

### 取り込みの時点で化けたカーブは消す

`weights` を Interchange が取り込めた `.glb` では、**記号モーフのカーブが化けた名前のまま残ります**。
UE はカーブを足したあと `RegenerateLegacyCurveData()` でカーブ一覧を**リグ側の名前から作り直す**ため、
`▲` `∧` `□` は 1 本の `_` に、`ω□` は `ω_`、`恐ろしい子！` は `恐ろしい子_` になります。
これらは**どのモーフターゲットにも対応せず、スケルトンにも登録されていない**ので何も動かしませんが、
上の「既にカーブがあるものは触らない」に当たるため放っておくと残り続けます
（`MmdPhysics.Editor.BuildActor` の「カーブ名がすべてモーフターゲットに対応している」で Fail します）。

消す条件は **2 つ揃ったときだけ**です:

1. `Mesh->FindMorphTarget(カーブ名) == nullptr`（何も動かさないカーブ）
2. カーブ名が「カーブにできないモーフ名」のサニタイズ像と一致する（化け方の説明が付く）

片方だけなら消さずに Warning に出します。1 だけで消すと事情の分からない壊れたカーブまで黙って消し、
2 だけで消すと化けていない実在のモーフを消すことになります。

★**カーブ名だけでは判定できません。** 化けた名前（`_`、`ω_`）は既にリグ規則に載っているので
`MakeRigSafeName` を通しても変わりません。`.glb` のモーフ名から像を作って突き合わせる必要があります
（`FMmdMorphAnimation::CollectRigMangledNames`）。

- 削除も**追加と同じブラケットの中**で行います（外に出すと 1 本ごとに再圧縮が走る）
- スケルトンの curve metadata は触りません（他のアセットと共有しているため）
- **削除だけの回でも AnimSequence を保存します。** 保存を飛ばすと、エディタを開き直した時点で
  消したカーブが戻ってきます（【3】の `bAnimDirty` は追加と削除の両方で立てる）

掃除しても `▲` `□` `ω□` などのモーフ自体は動きません。動かすにはメッシュ側のモーフターゲットを
改名する必要があり、PMX との名前対応が崩れる別の判断になります。

なお UV モーフ（PMX のモーフ種別 3。IA では 8 件）は、UE のモーフターゲットが頂点位置・法線・接線の
デルタしか持てないため、そもそも取り込まれません（45 件中 37 件だけが `MorphTarget` になります）。
カーブを足す側でも同じ 8 件が「モーフターゲット無し」で除外されます。

## 共有トゥーンが無いときは近似ランプを生成する

MMD の共有トゥーン（`toon01`〜`toon10`）は **MMD 本体に付属する画像**で、PMX にはその名前しか
入っていません。モデルにも `.glb` にも実体が無いので、上の抽出経路でも取り出せません。
移植元 Unity 版はここで諦めて「陰なし」に倒していました（`FindSharedToonTexture` が
`null` を返したら警告して終わり）。UE 版は代わりに `FMmdToonRamp`（`MmdToonRamp.h`）が
**自作の近似ランプ**を `UTexture2D` として生成し、それを当てます。

解決は 3 段構えで、**1 段目はこれまでと完全に同じ**です:

1. 名前 `toonXX` でプロジェクト全体を検索（最優先）
2. 生成済みの `T_MmdToonApproxXX`
3. 無ければ生成する（`{モデルのフォルダ}/SharedToon/` 配下）

つまり MMD 付属の画像を取り込んでいるプロジェクトの結果は 1 ビットも変わらず、
取り込んでいないプロジェクトだけが「陰なし」から「近似の陰付き」へ変わります。
3 に落ちたときのログは**警告ではなく情報**です。共有トゥーンが無いのは異常ではなく既定の状態で、
それでも変換はワンボタンで完走するようになったためです。

### なぜ同梱ではなく生成か

MMD 付属の `toon01`〜`10.bmp` をリポジトリへ同梱するのは再配布に当たります。
かといってプラグインが `/Game` の外（プラグインのコンテンツフォルダ）に固定のアセットを持つと、
今度はユーザーが色を詰められません。生成にすると:

- リポジトリにバイナリのアセットが増えない（マスターマテリアルを `.uasset` で置かず
  ノードグラフから組み立てているのと同じ判断）
- モデルのフォルダ配下に出るので、モデルごと持ち出せる
- ユーザーが生成後のアセットを直接いじって詰められる。**同名アセットがあれば再生成しない**ので、
  変換を何度走らせても上書きされない

### 何を持っていて、何を持っていないか

各ランプは `明部色 / 陰部色 / 境界位置 / ぼかし幅 / 帯` の数個のパラメータで表した近似で、
**MMD 付属の画像そのものではありません**。値は、手元にある MMD 付属の共有トゥーンと
見比べながら手で調整したものです。MMD 側の見た目が要るなら `toonXX` を取り込めば、
解決の 1 段目でそちらが優先されます。

画像そのものをリポジトリへ同梱するのは再配布に当たるため、していません。

### 2026-08-17: 形だけ MMD 側に合わせ直した

当初の色は「番号が上がるほど陰が濃い」「肌向けは暖色、衣装向けは紫灰/青灰/茶系」という
一般的な傾向を仮定して手で置いたものでした。手元の MMD 付属の BMP と突き合わせたところ、
**色以前に形の仮定が 2 つとも外れていた**ので、色 (色相) は自作のまま、構造だけ直しました。

| 実測して分かったこと | 直した内容 |
|---|---|
| 01〜04 の遷移は **1 行 (≒0.03) の硬い 2 値**。中間色の画素が 1 つも無い | ぼかし幅 0.10〜0.18 → **0.03**。テストが `Softness > 0` を要求するので 0 にはできない |
| **07〜10 は真っ白で陰が無い**。番号は濃さの順ではなく、いちばん濃いのは 03 | 07〜10 は明部色＝陰部色にして**陰を作らない**。03 を最も濃い枠へ |
| なだらかなのは 05・06 だけで、遷移は**下半分に寄る** (V=0.6〜0.85 付近) | 05 は境界 0.70/幅 0.26、06 は境界 0.78/幅 0.13 |

06 だけは **明部色も白ではなく、帯 (ハイライトの筋) を持ちます**。MMD 側の toon06 はランプ全体が黄色く、
明部寄り (V≒0.3) に白い筋が 1 本入ります。実物を 2D で見ると、この筋は計算されたグラデーションではなく
**手で引いた線を縮小したもの** (不規則に途切れ、アンチエイリアスがかかっている) です。
地の黄 + 光沢の筋 + 濃い山吹の陰、という構成なので、**金属 (金) 用の枠**と見られます。
他の 9 本が「陰の付け方」を変えるだけの無彩色なのに対し、06 だけ素材そのものを表しています。

明部を白のままにすると、遷移位置を正しくするほど白い領域が増えて**かえって離れました**
(RMS 89.7 → 19.2)。黄にして 19.2、さらに帯を足して **13.5** です。

### 帯 (`Band` / `BandCenter` / `BandWidth`)

明部→陰部の単調な遷移とは別に、途中へ明るい線を 1 本重ねるためのパラメータです。
**`BandWidth` が 0 なら一切効かない**ので、帯を持たない 9 本の挙動は追加前と 1 ビットも変わりません
(テストでも「帯の外へ影響が漏れない」ことを見ています)。

- 端で 0・中心で 1 になる滑らかな山 (smoothstep を反転したもの) で重ねます。両端で傾きが 0 になるので継ぎ目が出ません
- 帯と境界のぼかしが**重ならないこと**をテストで固定しています。重なると、どちらの検査も意味を失うためです
- 帯の内側では「明部色と陰部色の間にある / 単調に陰へ寄る / ぼかし幅の外は平坦」の 3 つが成り立たないので、
  テストはその範囲を除いて検査し、帯そのものは別に見ます
- ★既定値に `FColor::White` は使えません。constexpr でないため、帯を書かないエントリがあると
  `KRampTable` の定数評価が通りません (`FColor(255,255,255,255)` と書くこと)

当プラグインのマテリアルは `ToonUV = (0.5, 1 - saturate(N・L))` で**中央列**を引きます。
MMD 側の筋は破線状ですが中央列は明るい部分を通るため、この帯は実用上も見える差です。

### MMD 側との差 (RMS, V 方向 32 段)

構造を直した効果の実測です。値そのものを合わせにいったわけではないので、
これは「近さの目安」であって回帰テストではありません (テストは表と生成画素の対応だけを見ます)。

| | 旧 | 新 |
|---|---|---|
| toon01 / 02 / 03 | 31.9 / 14.0 / 40.7 | **3.5 / 0.6 / 0.7** |
| toon04 / 05 / 06 | 24.0 / 25.8 / 83.9 | **1.4 / 2.1 / 13.5** |
| toon07〜10 (陰なし) | 36〜73 | **0.0** |
| 全体 | 48.5 | **4.5** |

当初の色相の推測は **03・04・05 で向きごと外していました** (MMD 側は 03 が無彩色、04・05 は暖色。
こちらは紫・青灰・緑灰)。並べれば一目で違うと分かる差だったので、色相も直しています。
06 の明るい帯は当初表現できませんでしたが、あとから帯のパラメータを足して入れています (下記)。

★**07〜10 で陰が付かないのは不具合ではありません**。MMD 側がそうなっているのを再現しています。

### パラメータのテーブル

ランプ 1 本を `明部色 / 陰部色 / 境界位置 / ぼかし幅` の 4 つで表しています。V 座標は
マテリアル側の引き方（`ToonUV = (0.5, 1 - saturate(N・L))`）に合わせて **V=0 が明部、V=1 が陰部**。

同じテーブルが 2 箇所にあります。**片方だけ直さないこと**:

| 場所 | 役割 |
|---|---|
| `MmdToonRamp.cpp` の `KRampTable` | 実際に生成に使う（正本の複製） |
| `Tools/make_toon_ramps.py` の `RAMPS` | PNG を吐いて MMD 付属の BMP と目視で見比べる |

運用は「Python で PNG を出す → 手元の MMD 付属の BMP と並べて見比べる → Python 側を直す →
同じ値を C++ へ転記する」。二重管理を承知で分けているのは、色を詰める作業に UE の起動を
挟みたくないからです（1 回の見比べに数分かかると、そもそも詰めなくなる）。

補間は smoothstep ですが、`t = (V - 境界) / ぼかし幅 + 0.5` という形で書いています。
数学的には `(V - (境界 - 幅/2)) / 幅` と同じですが、こちらは `V == 境界` でちょうど `0.5` に
なることが浮動小数点でも保証されます。前者だと 1ulp ずれることがあり、明部色と陰部色の和が
奇数のとき中間色が 1 だけ振れて、境界位置のテストが不安定になりました。

### テクスチャ設定の理由

32x32 / sRGB=on / Clamp / ミップ無し / Bilinear / **無圧縮**（`TC_EditorIcon`）です。
BC で圧縮すると 4x4 ブロック単位で色が混ざってぼかし幅が指定どおりにならず、
ミップを作ると縮小版で明部と陰部が平均化されて陰そのものが消えます。

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

### 髪が黄緑になる（テクスチャが法線マップと誤認される）

UE のテクスチャインポータは、**画像の中身から法線マップかどうかを推定**します。
MMD の髪テクスチャは青緑〜紫が主で、接空間法線マップの基準色 `(128,128,255)` に近いため、
これに引っかかります（Tda式初音ミク・アペンドの `hair_MikuAp.tga` は平均 `(135,159,208)` で誤認）。

誤認されると `CompressionSettings=TC_Normalmap` / `sRGB=false` が立ちます。
`TC_Normalmap` は BC5 圧縮で **青チャンネルを持たない**ため、色として読むと青が 0 になり、
さらに sRGB が外れて残った RG がリニア値として扱われます。結果、
青緑の髪 `(135,159,208)` が **黄緑 `(196,210,46)`** で描かれます
（計算どおり: 青を落として sRGB エンコードすると `(192,207,0)`）。

材質が指すテクスチャ（base / toon / sphere）は **すべて色**で、法線マップであることはありません。
そこで `FTextureResolver::EnsureColorTexture` が引き当てた全テクスチャの取り込み設定を検査し、
**色として読めないもの**を `TC_Default` / `sRGB=true` に直して保存し直します（直した枚数は
変換結果の「色に直した N」に出ます）。この経路は `MmdPhysics.Editor.ConvertMaterials` が
同じ判定（`FMmdMaterialConversion::IsColorTexture`）で検証します。

★判定は **「`TC_Default` かどうか」ではありません**。要るのは次の 2 つだけです:

- `sRGB=true` … 8bit の色値としてガンマが掛かって読まれること
- **RGB が残る圧縮形式** … `TC_Default` / `TC_BC7` / `TC_EditorIcon` / `TC_VectorDisplacementmap`

`TC_Default` を要求すると、色として何の問題も無い設定まで誤りと判定します。実例が 2 つあります:

- **近似トゥーンランプ `T_MmdToonApproxNN` は意図的に無圧縮の `TC_EditorIcon`** です
  （BC 圧縮すると 4x4 ブロックで色が混ざりぼかし幅が崩れる。`MmdToonRamp.h`）。
  共有トゥーン `toon01`〜`toon10` を取り込んでいる環境では名前検索が最優先で拾うため
  近似ランプ経路が発火せず、**この誤判定はランプを実際に使う環境でだけ出ます**
- **`TC_BC7`（高品質 RGBA・sRGB=on）は利用者が意図して選ぶことがあります**。α の階段を
  減らす目的で髪のテクスチャを手で `TC_BC7` にしている例が実際にあり、これを一律に
  `TC_Default` へ焼き戻すのは **意図した設定を壊す**動作です
  （`TC_BC7` は取り込みの既定ではありません。既定は `TC_Default` です）

★α の階段について。`BC3`（`TC_Default`）の α は 4x4 ブロックごとに 2 端点＋補間の
8 段階へ量子化されるため、髪の縁の中間値が階段状に潰れます。`BC7` は **`BC3` と同じ 8bpp**
でサイズが変わらないまま、これを改善します。IA での実測（1024x1024・同一画角で画素比較）は
変化した画素 71.28% / 平均差 3.27 / 最大差 175 で、**変化は髪の房の輪郭に沿った細い筋だけ**に
集中していました（顔・肌・服はほぼ無変化）。

### α を使う材質のベーステクスチャを BC7 にする

上を受けて、`UpgradeAlphaTextureToBC7` が変換時に `TC_Default` → `TC_BC7` へ上げます
（上げた枚数は変換結果の「BC7 に上げた N」に出ます）。**条件を 2 つとも満たすときだけ**です。

1. **その材質が α を使う** … ブレンドする（Translucent）か、アルファテストで切る
   （`AlphaCutoff >= 0`。2 パス目を持つ材質は `PlanMaterial` が `KSubpassCutoff` を置くので
   ここに入る）。α を見ない材質では縁の階段は絵に出ません
2. **そのテクスチャが中間の α を持つ**（`HasSoftAlpha`）… α が `0/255` だけの
   プリベイク版は BC3 の端点で正確に出るので上げる意味がなく、α が全部 255 のものは
   UE が **DXT1（4bpp）** で焼くため、BC7（8bpp）にするとメモリが**倍**になります。
   中間値があるときだけ得があり、そのとき UE は元から DXT5（8bpp）なので**サイズは不変**です

★`HasSoftAlpha` はソース画像を見ます。実行時のミップは BC 圧縮で α が量子化済みなうえ、
`UTexture2D::HasAlphaChannel()` は**テクスチャがまだビルドされていない場面で常に false** を
返すためです（実測: `-nullrhi` の自動テストでは全テクスチャが「α 無し」に見え、
1 枚も上がりませんでした）。

IA での実測は **3 枚**（`orig_肌4_png` / `orig_髪の毛_png` / `orig_後ろ髪_png` = 無加工版）。
プリベイク版（`肌4_png` / `口_png`）は条件 2 で除外されます。2 回目の変換は 0 枚（冪等）。
`MmdPhysics.Editor.ConvertMaterials` が同じ条件で「α を使う材質のベーステクスチャが
BC3 のまま残っていないか」を検証します。

近似ランプ側の設定が「色として読める」ことは `MmdPhysics.Editor.ToonRampAsset` が見ています
（共有トゥーンの有無に関係なく走るので、上の穴が再発しません）。

★取り込み側の設定（Interchange の法線マップ推定を切る）で回避する手もありますが、
プロジェクト設定に依存させると「設定を変えていない環境では化ける」ことになるため、
**受け手が自分で直す**形にしています。

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
# 近似トゥーンのアセット生成まで見るとき (指定したフォルダへ .uasset を書きます)
$env:MMD_TOON_RAMP_PACKAGE = "/Game/MmdToonRampTest"

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
| `MmdPhysics.Core.Accumulator` | 固定刻みアキュムレータが積み残しを捨てて実時間へ復帰するか。**データ不要** |
| `MmdPhysics.Core.IdleSettle` | 静止入力で揺れ物が収まるか（振れ幅・周波数・最大速度を剛体別に出す）。`MMD_PARITY_GLB` を使う。切り分け用に `MMD_IDLE_SECONDS` / `_FIXED_HZ` / `_SUBSTEPS` / `_ITER` / `_JOINTS_FIRST` / `_SPLIT` / `_JOINT_SPLIT` / `_GRAVITY` で設定を差し替えられる |
| `MmdPhysics.Core.MaterialReader` | 各マテリアルの `extras.mmd` を読めているか |
| `MmdPhysics.Core.GlbImageExtract` | 手で組んだ GLB で `alphaCutoff` / `origTexture` の読み取りと画像の切り出し（`bufferView.byteOffset` の適用、範囲外の拒否）。**データ不要** |
| `MmdPhysics.Editor.MaterialPlan` | `alphaMode` / `alphaClass` / `origTexture` / `alphaCutoff` から親マスターとマスク閾値が決まる分岐。**データ不要** |
| `MmdPhysics.Editor.ToonRamp` | 近似トゥーンのテーブル（明部色 / 陰部色 / 境界位置 / ぼかし幅）が生成画素に出ているか。最上段＝明部色・最下段＝陰部色、境界でちょうど中間色、ぼかし幅の外は平坦。**データ不要** |
| `MmdPhysics.Editor.ToonRampAsset` | 近似トゥーンを実際に `UTexture2D` アセットにする経路（テクスチャ設定と、2 回目に作り直さないこと）。`MMD_TOON_RAMP_PACKAGE` が未設定ならスキップ |
| `MmdPhysics.Editor.MorphCurveName` | モーフ名が UE のリグ名規則に載るか（`▲` `∧` `□` が同じ名前に潰れて衝突すること、仮名・漢字・英数字は無事なこと、100 文字で切られること）と、化けたカーブを見分ける像（`CollectRigMangledNames`）が載る名前を巻き込まないか。**データ不要** |
| `MmdPhysics.Editor.ChainStability` | **アニメーションを再生しながら**、揺れ物の鎖のボーン間距離が参照ポーズから**伸びも縮みも**しないか（親フレームを取り違えて焼かれた鎖は末端が伸びて手前が縮むので、片側だけ見ると取りこぼす）。`MMD_CONV_SKELMESH` と `MMD_PARITY_GLB` を使う。切り分け用に `MMD_CHAIN_SECONDS` / `MMD_CHAIN_NOANIM`（静止のまま回す）/ `MMD_CHAIN_NOPHYS`（物理を切る）で条件を変えられる。★見ているのは長さだけで、向きの誤りは検出できない |
| `MmdPhysics.Bridge.UeSpace` | 位置と回転が同一の純回転か（行列式 +1） |
| `MmdPhysics.Bridge.ImportConvention` | 実際に取り込んだスケルトンと座標系が合うか |
| `MmdPhysics.Editor.WirePhysics` | 配線 → 評価 → 書き戻しが端から端まで通るか |
| `MmdPhysics.Editor.ConvertMaterials` | 全スロットに MI が付くか、半透明にすべき材質だけが Translucent 親か、マスク閾値が `alphaCutoff` と一致するか、`origTexture` が無加工版へ差し替わっているか、輪郭線フラグが入っているか |
| `MmdPhysics.Editor.BuildActor` | 生成した Blueprint に本体と輪郭線コンポーネントが入っているか、モーションが割り当たっているか（単発再生・ループ）、表情モーフのカーブが足されているか（名前がモーフターゲットと一致し、値が 0 のままでなく、作り直しても二重にならないこと）、作り直しても増殖しないか。★化けたカーブの掃除は手元のデータでは踏めないので、**化けた姿のカーブを 1 本その場で作ってから**消えることと実在のカーブを巻き添えにしないことを見る |

### 自動化していない部分

エディタでの目視確認は自動テストでは代替できません。実機で確認してください。

- 髪が体を貫通しないか、スカートの挙動が MMD と近いか
- **眉やまつげが前髪越しにふんわり透けるか**（`origTexture` + Translucent 昇格が効いているか）。
  効いていれば出力ログに `半透明 N〈うち origTexture 昇格 M〉/ 無加工テクスチャ K` が出ます。
  IA では「半透明 4〈うち origTexture 昇格 2〉/ 無加工テクスチャ 7」になります
- 透け髪の房どうしの重なり（TwoPass 相当が無いぶん、移植元と差が出うる唯一の箇所）
- **モーションを流したときの見え方。** 自動テストが見ているのは「`AnimSequence` が
  割り当たっているか」までで、実際に踊らせたときに髪・スカートの物理が破綻しないか、
  センター移動で慣性の持ち越しが無いことがどれだけ目立つかは実機でしか分かりません
