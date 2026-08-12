// Copyright RacingSim. All Rights Reserved.

using UnrealBuildTool;

public class RacingSim : ModuleRules
{
	public RacingSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Required for the layer-folder layout to compile.
		//
		// DefaultBuildSettings = V7 sets bLegacyPublicIncludePaths = false, so UBT
		// does not add the module root to the include path -- only a Public/ folder,
		// which this module deliberately does not have. Without this line,
		// #include "Core/RacingSimLog.h" fails with C1083 even though the file is
		// right there. (It cost one failed build to learn; RacingSim.h resolved fine
		// only because it sits at the module root.)
		//
		// The alternative was a Public/Private split with the layers nested inside.
		// This keeps the flat layer layout that Docs/15-ProjectStructure.md, README.md
		// and Docs/Tickets.md all specify. Exported publicly because RacingSimTests
		// includes layer headers.
		PublicIncludePaths.Add(ModuleDirectory);

		// CORE-001 delivered the layer layout: Core/, Vehicle/, Race/, UI/ and
		// Streaming/ are folders in this module, not separate modules (decision
		// 2026-08-12, Docs/15-ProjectStructure.md). Tests live in the separate
		// UncookedOnly RacingSimTests module so they cannot ship.
		//
		// Dependencies stay minimal deliberately. Add one only when a layer actually
		// needs it -- ChaosVehicles at VEH-002, UMG at UI-001, PixelStreaming2 at
		// STREAM-001 -- so the dependency list stays evidence of what is really used.
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
