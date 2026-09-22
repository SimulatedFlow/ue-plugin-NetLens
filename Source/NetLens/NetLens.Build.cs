// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class NetLens : ModuleRules
{
	public NetLens(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else. The entire point of this plugin is that the numbers appear
		// inside the game you are actually playing, including a packaged one, so there is nowhere an
		// editor-only module could sit without breaking the promise on the store page.
		//
		// LoadingPhase is PreDefault so the subsystem's console commands are registered before any game
		// module starts spawning things worth measuring.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",

			// Engine carries every hook this plugin stands on: UNetDriver's counters, UNetConnection's actor
			// channel map, UActorChannel::LastUpdateTime, and AHUD::OnHUDPostRender.
			"Engine",

			// NetCore is where the shared replication vocabulary lives (FNetworkGUID and the
			// UE::Net namespace). It is a public dependency because our public headers speak about
			// replication and a consuming module that includes them should not have to add it itself.
			"NetCore",

			"DeveloperSettings",
		});

		// RenderCore - GWhiteTexture, the one-pixel texture the panel background and the cost bars are
		//              tiled from. UCanvas has no untextured rectangle, so there is no way to draw a
		//              filled bar without it.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
		});

		// Deliberately NOT here:
		//   UMG      - the panel is drawn on UCanvas from AHUD. A widget tree is the one thing that is
		//              reliably missing from the build where you most need to see these numbers.
		//   UnrealEd - everything in this plugin ships.
		//   Any third-party code, and any external profiler session.
	}
}
