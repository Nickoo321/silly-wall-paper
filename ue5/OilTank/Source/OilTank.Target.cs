using UnrealBuildTool;
using System.Collections.Generic;

public class OilTankTarget : TargetRules
{
	public OilTankTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("OilTank");
	}
}
