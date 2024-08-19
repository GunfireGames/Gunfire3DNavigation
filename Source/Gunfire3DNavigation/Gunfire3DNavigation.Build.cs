// Copyright Gunfire Games, LLC. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class Gunfire3DNavigation : ModuleRules
	{
		public Gunfire3DNavigation(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			// This code can be pretty slow when it's unoptimized, so we always optimize
			// it by default. Otherwise it can be painful building nav when you're trying
			// to debug some other issue in DebugGame. If you need to debug the plugin
			// just comment this line out.
			OptimizeCode = CodeOptimization.Always;

			PrivateIncludePaths.AddRange(
				new string[] {
					"Gunfire3DNavigation/Private",
					"ThirdParty/libmorton/include"
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"AIModule",
					"Core",
					"CoreUObject",
					"Engine",
					"GameplayTasks",
					"PhysicsCore",
					"Navmesh",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"RHI",
					"RenderCore",
					"NavigationSystem",
				});

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
			}
		}
	}
}