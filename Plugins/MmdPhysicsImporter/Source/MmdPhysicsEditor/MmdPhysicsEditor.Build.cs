// Copyright (c) 2026 masaka1024. MIT License.

using UnrealBuildTool;

public class MmdPhysicsEditor : ModuleRules
{
	public MmdPhysicsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MmdPhysicsCore",
			"MmdPhysicsRuntime",
			// AnimGraph ノードのエディタ表現を配線するために必要。
			"MmdPhysicsAnimGraph",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Slate",
			"SlateCore",
			"InputCore",
			"ToolMenus",
			"Projects",
			"AssetRegistry",
			"AssetTools",
			// Post-Process Anim Blueprint をプログラムで生成・配線・コンパイルする。
			"AnimGraph",
			"AnimGraphRuntime",
			"BlueprintGraph",
			"Kismet",
			"KismetCompiler",
			// インポーターウィンドウ (アセット選択欄とファイル選択ダイアログ)。
			// マスターマテリアルのノードグラフをプログラムで組み立てる。
			"MaterialEditor",
			// マテリアルのコンパイル結果 (GMaxRHIFeatureLevel) を検査するのに必要。
			"RHI",
			"PropertyEditor",
			"DesktopPlatform",
			"WorkspaceMenuStructure",
			// 【2】マテリアル変換で GLB バイナリからテクスチャを直接デコードするのに使う
			// （Interchange は glTF マテリアルから参照されていない画像をアセット化しない可能性があるため）。
			"ImageWrapper",
		});
	}
}
