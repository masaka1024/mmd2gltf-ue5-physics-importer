# インストール

## 1. 前提

- **Unreal Engine 5.5**
- **Visual Studio 2022** に以下が入っていること
  - ワークロード「C++ によるデスクトップ開発」
  - MSVC v143 ビルドツール（x64/x86）
  - Windows 10 SDK または Windows 11 SDK

VS 2022 は入っているが C++ ワークロードが無い場合、UnrealBuildTool は
`Platform Win64 is not a valid platform to build` で止まります。次のコマンドで追加できます
（管理者権限が必要です）:

```
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe" modify `
  --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" `
  --add Microsoft.VisualStudio.Workload.NativeDesktop `
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  --add Microsoft.VisualStudio.Component.Windows11SDK.22621 `
  --includeRecommended --passive --norestart
```

## 2. プラグインを配置する

C++ プロジェクト（Blueprint 専用プロジェクトでは不可）の `Plugins/` へ置きます。

```
<YourProject>/
  YourProject.uproject
  Source/
  Plugins/
    MmdPhysicsImporter/     ← このリポジトリの Plugins/MmdPhysicsImporter
```

コピーでも、ジャンクション（`mklink /J`）でも構いません。開発中はジャンクションが便利です:

```
mklink /J "<YourProject>\Plugins\MmdPhysicsImporter" "<このリポジトリ>\Plugins\MmdPhysicsImporter"
```

## 3. ビルドする

`.uproject` を右クリック → 「Generate Visual Studio project files」してから VS でビルドするか、
コマンドラインで:

```
& "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" `
  <YourProject>Editor Win64 Development -Project="<...>\<YourProject>.uproject" -WaitMutex
```

## 4. 確認

エディタを開き、メニューに **Tools → MMD Physics インポーター** が出れば導入できています。

---

## 開発者向け: テストの実行

`docs/porting_notes.md` の「テストの走らせ方」を参照してください。
テストデータ（モデル）はリポジトリに含まれていないため、環境変数で指定します。
未指定のテストは黙ってスキップします。

C# 版との数値パリティを取るには .NET SDK 9 以降と、移植元リポジトリのクローンが要ります:

```
git clone --depth 1 https://github.com/masaka1024/mmd2gltf-unity-physics-importer.git .upstream/unity
dotnet run --project Tools/CsReference -c Release -- <glb> 300 Tools/CsReference/out/ia_300_cs.csv
```
