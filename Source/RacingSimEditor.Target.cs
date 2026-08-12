// Copyright RacingSim. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class RacingSimEditorTarget : TargetRules
{
	public RacingSimEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("RacingSim");

		// Editor target only. RacingSim.Target.cs must NOT add this -- the module is
		// declared UncookedOnly in RacingSim.uproject so it cannot enter a cooked Game
		// target, and CORE-001 verifies that against the packaged artifact.
		ExtraModuleNames.Add("RacingSimTests");
	}
}
