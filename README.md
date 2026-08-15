# mmd2gltf UE5 物理インポーター

[`mmd2gltf-gui`](https://github.com/masaka1024/mmd2gltf-gui) が出力した `.glb` の
`extras.mmd` から MMD 固有の物理データ（剛体・ジョイント）を読み、
Unreal Engine 5 のスケルタルメッシュ上で動かす **C++ エディタプラグイン**です。

[`mmd2gltf-unity-physics-importer`](https://github.com/masaka1024/mmd2gltf-unity-physics-importer)
の UE5 版にあたります。非公式・独立の個人プロジェクトです。

英語版: [README.en.md](README.en.md)

---

## 特徴

### UE の Chaos PhysicsAsset は使いません

Unity 版と同じく、**自作の Bullet 互換物理エンジンを同梱**してボーンを直接駆動します。
UE の `PhysicsAsset` へマッピングする方式は採っていません。理由は次のとおりです。

- UE の拘束は Swing1 / Swing2 / Twist の**対称制限**しか持たず、MMD のジョイントが持つ
  軸ごとの**非対称な回転制限**（`rot_min` / `rot_max`）を表現できません
- `FLinearConstraint::Limit` は全有効軸で共通のスカラ 1 個なので、XYZ 独立の移動制限も表現できません
- Unity 版は PhysX 経由の実装を一度作って捨てています（インポーター本体 3,541 行・調整スライダー 49 個 →
  現行は 1,160 行・スライダー 0 個）。同じ道を辿らない設計にしました

### 移植元とビット単位で一致します

物理エンジンは Unity 版の C# を 1:1 で C++ へ直訳したものです。実モデル（剛体 117・ジョイント 165）を
**300 フレーム回した後の全剛体の位置・回転が、C# 版と厳密に一致**することを自動テストで検証しています。

詳細と、一致させるために必要だった浮動小数点モードの設定は [docs/porting_notes.md](docs/porting_notes.md) を参照。

---

## 必要なもの

- **Unreal Engine 5.5**（5.6 はソース上の互換分岐のみ。後述の「対応バージョン」参照）
- Visual Studio 2022 + 「C++ によるデスクトップ開発」ワークロード + Windows SDK
- `mmd2gltf-gui` が出力した `.glb`
  - 一般の glTF エクスポータの出力では動作しません（`extras.mmd` が無いため）

---

## インストール

[INSTALL.md](INSTALL.md) を参照。

---

## 使い方

1. `.glb` を **UE 標準の Interchange glTF インポータ**でプロジェクトへ取り込む
   （既定の経路です。glTFRuntime や非推奨の旧 GLTFImporter、FBX 経由では座標系が一致しません）
2. メニューの **Tools → MMD Physics インポーター** を開く
3. 対象スケルタルメッシュと `.glb` を指定して **「1. 物理を配線 / 再配線」**
4. 続けて **「2. マテリアルを MMD トゥーンへ変換」**
5. 再生して確認

【1】を実行すると `ABP_<メッシュ名>_MmdPhysics` という Post-Process Anim Blueprint が
スケルタルメッシュの隣に作られ、割り当てられます。

**輪郭線を出すには**、レベルに置いたアクターを選び、詳細パネルの「+ 追加」から
**`Mmd Outline Component`** を足してください。メッシュ・追従・材質ごとの輪郭線色は
追加した時点で自動設定されます。太さはコンポーネントの `Outline Width Scale`（既定 0.15）で
その場で調整できます。PMX の `flags` bit4 が立っていない材質には輪郭線が出ません（MMD と同じ）。

【2】を実行すると、マスターマテリアル（`M_MmdToon` / 半透明があれば `M_MmdToonTranslucent`）と
材質ごとのマテリアルインスタンス `MI_<メッシュ名>_<材質名>` が同じフォルダに作られます。
トゥーン・スフィア・`origTexture`（プリベイク前の無加工テクスチャ）は glTF の標準マテリアルから
参照されておらず、取り込み設定によってはアセット化されません。その場合は `.glb` から直接取り出して
`MMD_ExtractedTextures` フォルダへ保存します（既にプロジェクトにあるものは再利用します）。

### 取り込み経路が違うと教えてくれます

起動時に `extras.mmd` のボーン位置とスケルトンの参照ポーズを突き合わせ、
座標系が合わない場合は具体的な対処付きで LogError を出します。

### 困ったときは

| 症状 | 見るところ |
|---|---|
| 髪やスカートの揺れが荒い / カクつく | AnimBP の MMD Physics ノードの **`FixedTimeStep` が `1/60`（0.01667）**、**`SubSteps` が 2** になっているか。`1/30` だと 1 フレームあたりの内部ステップ数が 0,1,0,1,1,... と変動して更新間隔がばらつきます。細かく刻みたいときは `FixedTimeStep` ではなく `SubSteps` を増やしてください（[docs/porting_notes.md](docs/porting_notes.md) の「ソルバ既定値はノードとコアで違う」参照） |
| 揺れが MMD の 3 倍くらい速い | `Gravity` が 98（PMX 単位）になっているか。UE のワールド重力とは無関係です |
| 半透明の材質がギザギザに切り抜かれる | その材質のマテリアルインスタンスの親が `M_MmdToonTranslucent` になっているか。ならない材質は `.glb` 側の `alphaMode` が `BLEND` でなく、`extras.mmd` の `alphaClass` も `"blend"` でないということです（`alphaClass` が `"mask"` の材質は意図的に `M_MmdToon` のままにしています。下の「半透明の描画順」参照） |
| 髪の向こうの眉やまつげが見えない | その材質に `origTexture`（プリベイク前の無加工テクスチャ）があるか。出力ログの `無加工テクスチャ N` が 0 なら、`.glb` が `origTexture` を持たない古い出力です。エクスポーター（mmd2gltf-gui）を新しくして出し直してください |
| 半透明の縁が硬い / しきい値で切れる | `M_MmdToon`（Masked）側の材質は `.glb` の `alphaCutoff` でカットします。マテリアルインスタンスの `OpacityMaskClipValue` を下げると多く残ります |
| 髪が透けすぎる / 透けなさすぎる | `M_MmdToonTranslucent` の `SubpassCutoff`（既定 0.5）を調整してください。これ以上のアルファは不透明として描かれます（移植元 lilToon の `_SubpassCutoff` と同じ既定値）。下げるほど不透明寄りになります |
| 半透明の材質どうしの前後関係がおかしい | 材質の並び順（スロット順）で描画されます。モデル側の材質順を直すか、レベルでスケルタルメッシュコンポーネントの `TranslucencySortPriority` を調整してください（モデル全体 vs 他アクターの順序にのみ効きます） |
| 輪郭線が出ない | アクターに `MmdOutlineComponent` を足したか。足しても出ないなら `Outline Width Scale` を上げる。その材質の PMX `flags` bit4 が立っていない場合は出ないのが正常です |
| 輪郭線が全部同じ色 | 本体マテリアルインスタンスの `EdgeColor` が入っているか確認してください。コンポーネントはそこから色を読みます |
| 表情を変えると輪郭線だけ取り残される | `MmdOutlineComponent` が毎フレーム動いているか（`Draw Outline` が有効か、コンポーネントの Tick が切られていないか） |
| マテリアルが灰色になる | 出力ログの `[MmdPhysics] マテリアルのコンパイルエラー` を確認 |
| 物理が NaN になったとログに出る | `SubSteps` を増やす。ばね定数が大きすぎる／質量が小さすぎるモデルで起きます |

---

## 仕組み

```
PMX ──[mmd2gltf-gui]──▶ .glb (extras.mmd に PMX 生値を温存)
                              │
      ┌───────────────────────┤
      │ メッシュ/スケルトン     │ 物理データ
      ▼                       ▼
 Interchange glTF        MmdGlbPhysicsReader
 → スケルタルメッシュ      → PmxPhysicsModel (PMX 生単位)
      │                       │
      │                 PmxPhysicsBuilder
      │                       │
      │                 PhysicsWorld (PMX 単位のまま駆動)
      │                       │
      └── FAnimNode_MmdPhysics ┘
          Post-Process AnimBP でボーンへ書き戻し
```

**設計の要点**: 物理演算は最後まで PMX ネイティブの座標系・単位のまま行い、
UE へ出す境界でだけ変換します。変換は `FMmdUeSpace` の 1 箇所に集約しています。

座標変換の導出と実測値は [docs/coordinate_transform.md](docs/coordinate_transform.md)。

---

## 対応バージョン

| | 状況 |
|---|---|
| UE 5.5 | 開発・検証環境。自動テスト 12 件が green |
| UE 5.6 | ソース上の互換分岐のみ。**実機検証していません** |

---

## 既知の制限

- **モデルはワールド原点にある前提です。** シミュレーションはコンポーネント空間で行うため、
  キャラクターが移動したときの慣性の持ち越しはありません。これは移植元 Unity 版から
  継承した制限です（移植元もモデル原点前提でワールド姿勢を書き込んでいました）
- **輪郭線（エッジ）は専用コンポーネントを 1 つ足して描きます。** 自動では付きません。
  レベルに置いたアクターへ `MmdOutlineComponent` を追加してください（下の「使い方」参照）。
  太さはコンポーネントの `Outline Width Scale` で調整でき、値を変えるとその場で反映されます
- **マテリアルは Unlit です。** MMD が `KHR_materials_unlit` で出力されるのに合わせた選択で、
  陰影はライト方向（`M_MmdToon` / `M_MmdToonTranslucent` の `LightDir` パラメータ）と
  トゥーンランプで作ります。そのぶん UE のシーンライトや Lumen には反応しません
- **共有トゥーン（`toon01`〜`toon10`）は同梱されません。** モデルにも含まれないので、
  別途プロジェクトへ取り込んでください。置き場所は自由です（名前でプロジェクト全体から探します）
- **半透明材質どうしの前後関係は、材質（スロット）の並び順で決まります。** UE の半透明ソートキーは
  `Priority → Distance → セクション順` で、同じスケルタルメッシュのセクションは前 2 つが同値になるため、
  結果として MMD の材質順がそのまま再現されます。ただし
  **`TranslucencySortPriority` は `UPrimitiveComponent` のプロパティでスロット単位には設定できない**ため、
  移植元 Unity 版の `renderQueue = 3000 + slotIdx` のような材質ごとの微調整はできません
- **半透明材質の内部の前後関係は完全には再現していません。** 移植元 lilToon の TwoPass は
  「α ≥ 0.5 を深度書き込み付きの不透明サブパスで描く」もので、この**不透明サブパス自体は
  不透明度の付け替えで再現してあります**（マスターの `SubpassCutoff` / `SubpassWeight`）。
  再現できないのは深度書き込みのほうで、α < 0.5 の裾どうしが重なると移植元より濃くなります
  （透け髪の房どうしなど）
- **見え方はマテリアルインスタンスのパラメータで調整できます。** `SubpassCutoff`（半透明の
  不透明サブパスのしきい値）や `AlphaCutoff`（マスクのしきい値）を出してあるので、
  再インポートせずにエディタ上で詰められます（[docs/porting_notes.md](docs/porting_notes.md)）
- **肌などの不透明な材質は、`origTexture` があっても半透明にしません。**
  移植元は `origTexture` を持つ材質を一律に半透明へ昇格させ、深度は lilToon の TwoPass が
  書いていました。UE の半透明は深度を書けないため、肌まで半透明にすると後段の半透明
  （後ろ髪など）が顔を突き抜けます。そこで**材質が使う UV 領域のアルファ分布を測り**、
  眉・まつげ・額の影のような「肌に貼り付けた半透明の材質」だけを昇格させています
  （下地の肌が深度を書くので安全）。判定の実測値は
  [docs/porting_notes.md](docs/porting_notes.md) を参照
- `sphereMode: 3`（サブテクスチャ）と `ambient` / `specular` は未対応です
  （移植元 Unity 版も `ambient`/`specular` 未対応）
- **当たり判定生成（Unity 版の【3】）は対象外です**
- `SoftBody`（PMX 2.1）は移植していません（移植元でもビルダーから未使用）
- `.pmx` 直読みの検証経路（`PmxReader`）は未移植です
- 静止時の微振動は移植元から引き継いでいます

---

## ライセンス

MIT License. [LICENSE](LICENSE) を参照。

同梱している物理エンジンは
[`mmd2gltf-cs-physics`](https://github.com/masaka1024) を移植元とする C++ 実装です。
モデルデータやトゥーンテクスチャは同梱していません。
