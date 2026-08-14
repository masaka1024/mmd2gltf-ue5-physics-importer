// Copyright (c) 2026 masaka1024. MIT License.

using UnrealBuildTool;

public class MmdPhysicsAnimGraph : ModuleRules
{
	public MmdPhysicsAnimGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// AnimGraph ノードのエディタ表現のみを持つ UncookedOnly モジュール。
		// UE の AnimNode プラグインの標準構成（Runtime にノード本体 / UncookedOnly に UAnimGraphNode）。
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimGraph",
			"AnimGraphRuntime",
			"BlueprintGraph",
			"MmdPhysicsCore",
			"MmdPhysicsRuntime",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
		});
	}
}
