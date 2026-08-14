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

### 取り込み経路が違うと教えてくれます

起動時に `extras.mmd` のボーン位置とスケルトンの参照ポーズを突き合わせ、
座標系が合わない場合は具体的な対処付きで LogError を出します。

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
| UE 5.5 | 開発・検証環境。自動テスト 8 件が green |
| UE 5.6 | ソース上の互換分岐のみ。**実機検証していません** |

---

## 既知の制限

- **モデルはワールド原点にある前提です。** シミュレーションはコンポーネント空間で行うため、
  キャラクターが移動したときの慣性の持ち越しはありません。これは移植元 Unity 版から
  継承した制限です（移植元もモデル原点前提でワールド姿勢を書き込んでいました）
- **アウトライン（エッジ）は未対応です。** `edgeColor` / `edgeSize` はマテリアル
  インスタンスのパラメータとして保存してあるので、後から実装できます
- **マテリアルは Unlit です。** MMD が `KHR_materials_unlit` で出力されるのに合わせた選択で、
  陰影はライト方向（`M_MmdToon` の `LightDir` パラメータ）とトゥーンランプで作ります。
  そのぶん UE のシーンライトや Lumen には反応しません
- **共有トゥーン（`toon01`〜`toon10`）は同梱されません。** モデルにも含まれないので、
  別途プロジェクトへ取り込んでください。置き場所は自由です（名前でプロジェクト全体から探します）
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
