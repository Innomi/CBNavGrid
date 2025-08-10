// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CBNavGrid : ModuleRules
{
	public CBNavGrid(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"NavigationSystem"
			});

		PrivateDependencyModuleNames.AddRange(new string[] {
				"AIModule",
				"RenderCore",
				"RHI"
			});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd"
			});
		}
	}
}
