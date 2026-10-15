using UnrealBuildTool;

public class OilTank : ModuleRules
{
	public OilTank(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine",
			"ProceduralMeshComponent",
			"Json",
			"ImageWrapper",
			"RenderCore",
			"RHI",
		});

		// Escape hatches for APIs this text-only port could not compile-check (see README "VERIFY").
		// If one of these fails to compile on 5.8, set it to 0 and rebuild; the code falls back.
		//   OILTANK_WITH_LFV            ALocalFogVolume / ULocalFogVolumeComponent (water_mode "localfog")
		//   OILTANK_HAS_VFOG_START      UExponentialHeightFogComponent::VolumetricFogStartDistance
		//   OILTANK_PP_EXTRAS           FPostProcessSettings ToneCurveAmount / LocalExposure* / physical-exposure toggle
		//   OILTANK_PERF_COUNTERS       RHIGetGPUFrameCycles / GGameThreadTime / GRenderThreadTime
		PublicDefinitions.Add("OILTANK_WITH_LFV=1");
		PublicDefinitions.Add("OILTANK_HAS_VFOG_START=1");
		PublicDefinitions.Add("OILTANK_PP_EXTRAS=1");
		PublicDefinitions.Add("OILTANK_PERF_COUNTERS=1");
	}
}
