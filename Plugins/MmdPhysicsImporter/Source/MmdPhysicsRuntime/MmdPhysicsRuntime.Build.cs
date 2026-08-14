// Copyright (c) 2026 masaka1024. MIT License.

using UnrealBuildTool;

public class MmdPhysicsRuntime : ModuleRules
{
	public MmdPhysicsRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimGraphRuntime",
			"AnimationCore",
			"MmdPhysicsCore",
		});
	}
}
