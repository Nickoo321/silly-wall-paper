using UnrealBuildTool;
using System.Collections.Generic;

public class OilTankEditorTarget : TargetRules
{
	public OilTankEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("OilTank");
	}
}
