# 座標変換 — 導出と実測

このプラグインで**唯一**座標変換を行ってよい場所は `FMmdUeSpace`
(`Source/MmdPhysicsRuntime/Public/MmdUeSpace.h`) です。他の場所に軸の入れ替えや符号反転を
書かないでください。物理エンジンは最後まで PMX ネイティブの座標系・単位のまま動きます。

## 結論

```
位置   UE = (px, -pz, py) × UnitScale × 100      // UnitScale=0.08 なら ×8 (cm)
回転   UE = (qx, -qz, qy, qw)
```

この写像は **行列式 +1 の純粋な回転（X 軸まわり +90°）であり、鏡映ではありません。**

## 導出

### 1. PMX → glTF（`mmd2gltf-gui` 側）

`mmd2gltf` は PMX(左手 Y-up) → glTF(右手 Y-up) で Z を反転します。

```
gltf = (px, py, -pz) × unitScale
```

根拠: 移植元 `GlbPhysicsReader.cs` が glTF のローカル並進から raw PMX へ戻すときに
`(lt.x / s, lt.y / s, -lt.z / s)` としていること。その逆写像が上式です。

### 2. glTF → UE（UE 標準の Interchange glTF インポータ）

UE 5.5 のエンジンソース
`Engine/Plugins/Interchange/Runtime/Source/Parsers/GLTFCore/Private/GLTF/ConversionUtilities.h`
の実装値:

```cpp
ConvertVec3(V) -> { V.X, V.Z, V.Y }
ConvertQuat(Q) -> { -Q.X, -Q.Z, -Q.Y, Q.W }
```

さらに glTF はメートル、UE はセンチメートルなので ×100 されます。

### 3. 合成

位置:
```
UE = ConvertVec3(gltf) × 100
   = ConvertVec3((px, py, -pz) × unitScale) × 100
   = (px, -pz, py) × unitScale × 100
```

回転: glTF 側の Z 反転は鏡映なので、回転は軸を鏡映して角度の符号を反転する形
`q_gltf = (-qx, -qy, qz, qw)` になります。これに `ConvertQuat` を適用すると

```
UE = (-(-qx), -(qz), -(-qy), qw) = (qx, -qz, qy, qw)
```

位置と同じ `(x, -z, y)` のパターンになります。

### 4. なぜ鏡映にならないか

PMX(左手) → glTF(右手) → UE(左手) と、手系の反転が **2 回**（偶数回）起きるため、
トータルでは純粋な回転に戻ります。

行列で書くと `(x,y,z) → (x,-z,y)` は

```
| 1  0  0 |
| 0  0 -1 |     det = +1
| 0  1  0 |
```

で、X 軸まわり +90° の回転です。

**この帰結が実装上とても重要です。** Unity 版の旧 PhysX 実装は
「角度制限の鏡像処理（lower/upper の入れ替え＋符号反転）」を必要としていましたが、
UE 版ではカイラリティが保存されるため**不要**です。MMD のジョイント回転制限を
そのまま渡してかまいません。

## 実測による裏付け

`IA.glb`（ボーン179）を UE 標準の Interchange glTF で取り込み、`extras.mmd` のボーン位置を
上式で変換した点と、インポートされたスケルトンの参照ポーズを突き合わせた結果:

| 仮定した取り込み経路 | 参照ポーズとの最大差 |
|---|---|
| **UE 標準 Interchange glTF（本式）** | **0.0000 cm** |
| Z 反転なし | 105.82 cm |
| 軸変換なし（PMX 生値） | 221.26 cm |
| glTFRuntime の基底 | 110.04 cm |

ボーン名の解決は 179 / 179。日本語ボーン名は Interchange を通っても無傷で残ります。

再現方法: `MmdPhysics.Bridge.ImportConvention`（`docs/porting_notes.md` の「テストの走らせ方」参照）。
このテストは変換式を実装と共有せず独立に書き下しているので、同じ間違いで照合してしまうことはありません。

## スケール ×8 の根拠

PMX 1 単位 ≒ 0.08 m。初音ミク標準モデルの身長がおよそ 20 単位 = 158cm であることに由来します。
`extras.mmd` の `unitScale` に実際の値が入っているので、プラグインは**それを正**として使い、
ノード設定と食い違う場合は警告してファイル側を採用します。

## 既知の副作用: 正面方向

この変換で MMD の正面 `(0,0,-1)` は UE で **+Y** を向きます（UE の慣例はキャラ正面 = +X）。
物理の正しさには影響しませんが、見た目を UE 慣例に合わせたい場合はアクター／ルート側で
Yaw -90° を掛けてください。

## 単位と重力について

物理演算は **PMX ネイティブ単位のまま**行います。`Gravity = 98`（= 9.8 × 10）も PMX 単位系の値で、
UE のワールド重力とは無関係です。

長さだけスケールして重力を据え置くと、振り子の周期が `√0.08 ≒ 0.283` 倍になり
MMD より約 3.5 倍速く揺れてしまいます。エンジンを PMX 単位のまま回すことで、この問題を
そもそも発生させない設計にしています。
