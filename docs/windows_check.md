# Windows での確認記録（UE 5.8）

Mac（UE 5.8.2 / Apple Silicon）でだけ確認していた `ue5.8-mac` の変更を、
**Windows で確認した結果の記録**です。確認は 2026-09-20 に
UE 5.8 / Windows 11 / Visual Studio 2022 / クリーンチェックアウトで行いました。
確認後、`ue5.8-mac` は `main` へ合流しています。

結論から書くと、**A2 が `true`** でした。Windows では何もしなくても日本語名が通るので、
Mac に入れている起動時の `LC_CTYPE` 切り替えを Windows へ持ち込む必要はありません。

---

## A. 確認結果

| # | 確かめたこと | Mac | **Windows（実測）** |
|---|---|---|---|
| A1 | **ビルドが通るか**（`FMmdScopedUtf8CType` の Windows 分岐 `_configthreadlocale` は Mac では一度もコンパイルされない） | ―（Mac 側の分岐のみ） | ✅ **成功・警告 0 件** |
| A2 | **C ロケールのままで仮名・漢字が「文字」と判定されるか** | `false`（だから Mac は起動時に切り替えている） | ✅ **`true`** — `Outside scope: Japanese alpha = true (global LC_CTYPE='C')` |
| A3 | 起動時の `LC_CTYPE` 切り替えが**入っていないこと**（Mac 限定の処理） | 出る | ✅ `LC_CTYPE を` は **0 件** |
| A4 | エンジンの `System.Core.Misc.Char` | 失敗（Mac の既知の副作用） | ✅ **Success** |
| A5 | `IA_Anim` をロードしたときのエラー | 0 件 | ✅ `CONTROL というボーン` / `CONTROL というカーブ` とも **0 件**（`_____` の化けも 0） |
| A6 | `MmdPhysics.Editor.BuildActor` がクラッシュしないか | 成功 | ✅ **Success** |

補足:

- `FMmdScopedUtf8CType` 自体は Windows でも動いており、`Applied locale: .UTF-8` を選んでいます。
  A2 が `true` なので出番はありませんが、保険として機能することは確認できています。
- **全角数字（０-９）はプラットフォームに関係なく通りません。** `iswalpha` / `iswdigit` の
  どちらでも弾かれるので、そちらは取り込み時の半角化で扱います（A2 とは別の問題です）。
  Windows では取り込み時に `ボーン 30 件 / モーフ 1 件` を半角化しました。

### テストとパリティ

| | 結果 |
|---|---|
| 自動テスト | **28 件すべて成功**（Mac は 28 件中 27 件成功。失敗は `System.Core.Misc.Char` のみ） |
| C# 版とのパリティ | **6 ケースすべてビット一致**（`全 60 フレームで基準と一致 (許容差 0)`） |

パリティは IA（179 ボーン / 117 剛体 / 165 ジョイント）と Tda 式ミク（206 / 90 / 69）を、
既定 / 駆動あり / 再生時ソルバの 3 構成で回した結果です。

`MmdPhysics.Bridge.ImportConvention` も全数で一致しました
（名前解決 179/179、座標系は「UE 標準の Interchange glTF」候補が最大差 **0.0000 cm**。
Z 反転なしは 105.8 cm、軸変換なしは 221.3 cm）。

---

## B. 再現手順

1. **UE 5.8** と **Visual Studio 2022**（「C++ によるデスクトップ開発」ワークロード）を入れる
2. リポジトリを取得する（`main` に合流済みです）
   ```powershell
   git clone https://github.com/masaka1024/mmd2gltf-ue5-physics-importer.git
   ```
3. UE 5.8 の **C++ プロジェクト**を用意し（空でよい。名前は例として `MMDWin`）、
   `Plugins/MmdPhysicsImporter` を丸ごと `<プロジェクト>/Plugins/` へコピーする
4. **IA.glb は同梱されていません**（再配布できないため）。手元のものを置いてください
   （例 `C:\MmdData\IA.glb`）
5. ビルドする（**A1**）
   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" `
     MMDWinEditor Win64 Development -Project="C:\Work\MMDWin\MMDWin.uproject" -WaitMutex
   ```
   ★エディタが起動していると `Unable to build while Live Coding is active` で止まります。

## C. 取り込み

エディタで **Tools → MMD Physics インポーター** を開き、「0. .glb を取り込む」→ 1 → 2 → 3 の順に
押します。もう一度「0.」を押すと上書き確認のダイアログが出て、キャンセルで何も変わりません。

コマンドラインだけで済ませることもできます（**A3・A5** はこのログで見られます）:

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
& $UE "C:\Work\MMDWin\MMDWin.uproject" `
    -ExecCmds="MmdPhysics.ImportPipeline C:/MmdData/IA.glb -Force,Quit" `
    -unattended -nullrhi -nosplash -nosound -abslog="C:\Work\mmd_import.log"
```

★**`-ExecCmds` の区切りは `,` です。** `"... ; Quit"` と書いても分割されず、`; Quit` が
そのまま引数の一部になって取り込みが中止されます（UE 5.8 実測）。

取り込み先は `/Game/IA/SkeletalMeshes/` です（glTF 既定パイプラインがサブフォルダを作ります）。

## D. 自動テスト

エディタを**閉じてから**実行します。

```powershell
$UE   = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Proj = "C:\Work\MMDWin\MMDWin.uproject"
$Glb  = "C:\MmdData\IA.glb"

# ★Content をバックアップしてから実行する (BuildActor などが /Game/IA を上書き保存する)
Copy-Item -Recurse "C:\Work\MMDWin\Content" "C:\Work\MMDWin_Content_backup"

$env:MMD_PARITY_GLB        = $Glb
$env:MMD_CONV_SKELMESH     = "/Game/IA/SkeletalMeshes/IA.IA"
$env:MMD_CONV_EXPECT_BONES = "179"
$env:MMD_DIAG_ANIM         = "/Game/IA/SkeletalMeshes/IA_Anim.IA_Anim"
$env:MMD_LOCALE_IMPORT_GLB = $Glb
$env:MMD_PIPELINE_GLB      = $Glb
$env:MMD_TOON_RAMP_PACKAGE = "/Game/__MmdTest/ToonRamp"

& $UE $Proj -ExecCmds="Automation RunTests MmdPhysics+System.Core.Misc.Char" `
    -unattended -nullrhi -nosplash -nosound `
    -testexit="Automation Test Queue Empty" -abslog="C:\Work\mmd_tests.log"
```

結果の見方:

```powershell
Select-String -Path C:\Work\mmd_tests.log -Pattern "Test Completed" | ForEach-Object { $_.Line }
Select-String -Path C:\Work\mmd_tests.log -Pattern "Outside scope|Applied locale|ボーン名の解決"
```

- `Outside scope: Japanese alpha = true/false` → **A2**
- `Applied locale:` → `FMmdScopedUtf8CType` が選んだロケール
- `MmdPhysics.Editor.BuildActor` の結果 → **A6**
- テストのあと `__MmdTest` フォルダは消してよい（`ToonRampAsset` が作る）

★**この設定では `MmdPhysics.Core.GlbParity` は走りません。** `MMD_PARITY_CSV`（または
`MMD_PARITY_CSVS`）が無いと**黙ってスキップして緑になる**ためです。パリティまで見るには
基準 CSV を作って渡してください。手順は
[porting_notes.md の「テストの走らせ方」](porting_notes.md) を参照してください。

---

## E. Windows で確認していないこと

- **目視確認。** 髪の貫通、スカートの挙動、眉・まつげの透け、モーションを流したときの
  破綻は自動テストでは代替できません（`porting_notes.md` の「自動化していない部分」参照）。
- **パッケージしたビルド。** エディタ上でしか確認していません。
