// Copyright RacingSim. All Rights Reserved.

using UnrealBuildTool;

/**
 * Automation specs for RacingSim.
 *
 * Declared "UncookedOnly" in RacingSim.uproject, which is the point of this module
 * existing at all: an UncookedOnly module is not built into a cooked Game or Client
 * target, so test code cannot ship. That is a stronger guarantee than wrapping tests
 * in WITH_AUTOMATION_TESTS inside the runtime module and trusting every future author
 * to remember the guard.
 *
 * CORE-001 requires this to be verified against the packaged artifact, not against
 * this file: RacingSimTests must appear in no staging manifest and in no "Mounting"
 * line of the packaged runtime log.
 */
public class RacingSimTests : ModuleRules
{
	public RacingSimTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Same reason as RacingSim.Build.cs: V7 build settings drop the module root
		// from the include path, and this module mirrors the layer folders.
		PrivateIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// The module under test. Tests depend on the runtime module; nothing in the
		// runtime module may ever depend on this one.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RacingSim"
		});
	}
}
