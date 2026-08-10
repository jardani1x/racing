// Copyright RacingSim. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class RacingSimTarget : TargetRules
{
	public RacingSimTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("RacingSim");
	}
}
