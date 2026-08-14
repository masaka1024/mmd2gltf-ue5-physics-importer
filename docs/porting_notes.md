# 移植ノート — C# ↔ C++

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
| `MmdPhysics.Bridge.UeSpace` | 位置と回転が同一の純回転か（行列式 +1） |
| `MmdPhysics.Bridge.ImportConvention` | 実際に取り込んだスケルトンと座標系が合うか |
| `MmdPhysics.Editor.WirePhysics` | 配線 → 評価 → 書き戻しが端から端まで通るか |

### 自動化していない部分

エディタでの目視確認（髪が体を貫通しないか、スカートの挙動が MMD と近いか）は
自動テストでは代替できません。実機で確認してください。
