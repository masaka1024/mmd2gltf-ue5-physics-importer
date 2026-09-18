# Windows での確認手順（ue5.8-mac ブランチ）

Mac（UE 5.8.2 / Apple Silicon）では確認済みの変更を、Windows で確かめるための手順です。
**Windows では一度もビルド・実行していません。** 特に下の「A. 未確認の要点」を見てください。

---

## A. 未確認の要点（Windows で結果を知りたいもの）

| # | 確かめること | 見る場所 | Mac での結果 |
|---|---|---|---|
| A1 | **ビルドが通るか**（`FMmdScopedUtf8CType` の Windows 分岐 `_configthreadlocale` は Mac では一度もコンパイルされていない） | ビルドのログ | ―（Mac 側の分岐のみ） |
| A2 | **C ロケールのままで仮名・漢字が「文字」と判定されるか**。`MmdPhysics.Locale.ScopedUtf8CType` の `Outside scope: Japanese alpha = …` | テストのログ | `false`（だから Mac は起動時に切り替えている） |
| A3 | 起動時の LC_CTYPE 切り替えが**入っていないこと**（Mac 限定の処理） | 起動ログに `LC_CTYPE を` が**出ない** | 出る |
| A4 | エンジンの `System::Core::Misc::Char` テスト | テストのログ | 失敗（Mac の既知の副作用）。**Windows では成功するはず** |
| A5 | IA_Anim をロードしたときのエラー | 起動ログの `CONTROL というボーン` / `CONTROL というカーブ` | 0 件 |
| A6 | `MmdPhysics.Editor.BuildActor` がクラッシュしないか | テストのログ | 成功 |

**A2 がいちばん大事です。**

- `true` なら、Windows では何もしなくても日本語名が通ります（今の作りのまま問題なし）
- `false` なら、Windows でも Mac と同じ問題（ロード時の `_____CONTROL` エラー、BuildActor のクラッシュ）が起きます。
  A5・A6 もあわせて見てください。対策（起動時の切り替えを Windows にも入れる）が要ります

---

## B. 準備

1. **UE 5.8** と **Visual Studio 2022**（「C++ によるゲーム開発」ワークロード）を入れる
2. このリポジトリを取得して `ue5.8-mac` ブランチにする
   ```powershell
   git clone https://github.com/masaka1024/mmd2gltf-ue5-physics-importer.git
   cd mmd2gltf-ue5-physics-importer
   git switch ue5.8-mac
   ```
3. UE 5.8 の **C++ プロジェクト**を用意し（空のプロジェクトでよい。名前は例として `MMDWin`）、
   `Plugins/MmdPhysicsImporter` を丸ごと `<プロジェクト>/Plugins/` へコピーする
4. **IA.glb は同梱されていません**（再配布できないため）。Mac の `~/Downloads/IA 2/IA.glb` を
   Windows へコピーする（例 `C:\MmdData\IA.glb`）
5. `.uproject` を右クリック →「Generate Visual Studio project files」→ `.sln` を開いて
   **Development Editor / Win64** でビルド（**A1**）

## C. 取り込み（エディタで）

1. エディタを起動し、**出力ログで `LC_CTYPE を` が出ていないこと**を確認（**A3**）
2. **Tools → MMD Physics インポーター** →「.glb」に `C:\MmdData\IA.glb` →
   **「0. .glb を取り込む」** → 1 → 2 → 3 の順に押す
3. もう一度「0.」を押し、**上書き確認のダイアログが出る**ことを確認（Mac では未確認の UI）。
   キャンセルで何も変わらないこと
4. エディタを閉じて開き直し、出力ログで `CONTROL というボーン` / `CONTROL というカーブ` を検索（**A5**）

## D. 自動テスト（コマンドラインで）

エディタを**閉じてから**、PowerShell で実行します（パスは環境に合わせて直してください）。

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

& $UE $Proj -ExecCmds="Automation RunTests MmdPhysics+System.Core.Misc.Char; Quit" `
    -unattended -nullrhi -nosplash -nosound -abslog="C:\Work\mmd_tests.log"
```

結果の見方（`C:\Work\mmd_tests.log`）:

```powershell
Select-String -Path C:\Work\mmd_tests.log -Pattern "Test Completed" | ForEach-Object { $_.Line }
Select-String -Path C:\Work\mmd_tests.log -Pattern "Outside scope|Applied locale|ボーン名の解決|トラック 155|スケルトンに無いトラック"
```

- Mac では **28 件中 27 件成功**（失敗は `System.Core.Misc.Char` のみ）。
  Windows では **28 件すべて成功**が期待値です（Char も通るはず）
- `Outside scope: Japanese alpha = true/false` → **A2**
- `Applied locale:` → `FMmdScopedUtf8CType` が Windows で選んだロケール（`.UTF-8` か `ja-JP` のはず）
- `MmdPhysics.Editor.BuildActor` の結果 → **A6**
- テストのあと `__MmdTest` フォルダは消してよい（ToonRampAsset が作る）

## E. 送ってほしいもの

- `C:\Work\mmd_tests.log`（テストのログ）
- エディタを開き直したときの出力ログ（`<プロジェクト>\Saved\Logs\<プロジェクト名>.log`）
- ビルドで失敗した場合はそのエラー
