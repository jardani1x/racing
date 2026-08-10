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
	}
}
