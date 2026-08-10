// Copyright RacingSim. All Rights Reserved.

using UnrealBuildTool;

public class RacingSim : ModuleRules
{
	public RacingSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Phase 0 scope: toolchain verification only. The Core/Vehicle/Race/UI/
		// Streaming/Tests layout in Docs/15-ProjectStructure.md is delivered by
		// CORE-001, not here. Dependencies stay minimal until then.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
