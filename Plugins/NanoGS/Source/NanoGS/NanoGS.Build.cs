// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NanoGS : ModuleRules
{
	public NanoGS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				// Access Renderer private headers for FViewInfo::ViewRect (screen percentage support)
				System.IO.Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"),
				System.IO.Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"),
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
				"RHI",
				"Renderer",
				"Projects"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"Json",
				"JsonUtilities"
			}
		);

		// Editor-only: read the active level-editor viewport camera so tile
		// streaming works while previewing in the editor (not just in PIE).
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
