// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NetLensTypes.h"
#include "NetLensSettings.generated.h"

/**
 * Project-wide settings for NetLens, under Project Settings -> Plugins -> NetLens.
 *
 * Half of what is here is presentation and half of it is the ceiling NetLens puts on itself. The second half
 * is the important one and it is deliberately in the same place as the first, where you will see it: a
 * replication diagnostic that walks every actor in the level and every property on every one of them will
 * cost more than the traffic it was bought to find, and then you have two problems. Every limit in the
 * Budget category has a default that is meant to be left alone and a comment saying what it buys.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "NetLens"))
class NETLENS_API UNetLensSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNetLensSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const UNetLensSettings& Get();

	//~ Measurement -------------------------------------------------------------------------------------

	/**
	 * Seconds of history every rate is averaged over.
	 *
	 * Ten, because that is roughly how long it takes to notice a hitch, say "what was that", and look at the
	 * screen. Shorter and the table moves faster than you can read it; longer and a spike is diluted into
	 * invisibility - though the peak column keeps that from being fatal either way.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement",
		meta = (ClampMin = "1.0", ClampMax = "120.0", ForceUnits = "s"))
	float WindowSeconds = 10.0f;

	/**
	 * How finely the window is divided.
	 *
	 * This is the resolution of the peak column and the granularity at which the window forgets. Twenty
	 * buckets over ten seconds is half a second each, which is about as fine as a peak is meaningful given
	 * that the sampler itself runs at ten hertz.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement", meta = (ClampMin = "2", ClampMax = "256"))
	int32 BucketsPerWindow = 20;

	/**
	 * How often NetLens looks at the world, in hertz.
	 *
	 * Not the game's frame rate and deliberately far below it. Every sample walks the open actor channels and
	 * compares the shadow state of the tracked actors; doing that sixty times a second would buy nothing,
	 * because an actor channel does not change state between two frames of a sixty hertz game often enough
	 * to matter, and it would cost six times what ten hertz costs.
	 *
	 * Updates are still counted exactly at any sample rate: they are read from a timestamp the engine wrote,
	 * not from how many samples happened to see them.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement",
		meta = (ClampMin = "1.0", ClampMax = "60.0", ForceUnits = "Hz"))
	float SamplesPerSecond = 10.0f;

	/** Follow individual replicated properties. Off leaves the actor rows and drops the breakdown. */
	UPROPERTY(config, EditAnywhere, Category = "Measurement")
	bool bTrackProperties = true;

	/**
	 * Follow RPCs.
	 *
	 * Automatic capture uses the engine's own send hook, which exists in Development and Test builds but not
	 * in Shipping - Epic compiles it out. In a Shipping build this setting still governs whether manual
	 * NoteRPC calls are recorded, so a project that wants RPC numbers in Shipping can have them by calling
	 * NoteRPC from the functions it cares about.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Measurement")
	bool bTrackRPCs = true;

	//~ Budget ------------------------------------------------------------------------------------------

	/**
	 * The most actors NetLens will follow in detail. Everything past it collapses into one row.
	 *
	 * Two hundred. This is the number that makes NetLens something you can leave switched on: the cost of a
	 * sample is bounded by this multiplied by MaxPropertiesPerObject, whatever the level does, so a hundred
	 * spawned projectiles cannot turn the diagnostic into the bottleneck.
	 *
	 * Actors with a UNetLensComponent set to always track are followed on top of this ceiling and are never
	 * the ones evicted - that is what the component is for.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "8", ClampMax = "2000"))
	int32 MaxTrackedActors = 200;

	/**
	 * The most replicated properties NetLens will shadow on one object.
	 *
	 * Sixty-four covers every hand-written replicated class and most Blueprints. A class with more than this
	 * has its remaining properties dropped from the breakdown, not from the actor's total - the actor is
	 * still priced, it just cannot tell you which of its ninetieth property moved.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "4", ClampMax = "512"))
	int32 MaxPropertiesPerObject = 64;

	/**
	 * The most replicated components NetLens will shadow on one actor, on top of the actor itself.
	 *
	 * Components matter more than they look: the single most expensive replicated property in a typical
	 * project is not on the Character, it is on its movement component. Eight is enough for anything normal
	 * and is a hard stop on an actor that has assembled two hundred of them at runtime.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", ClampMax = "64"))
	int32 MaxReplicatedComponentsPerActor = 8;

	/**
	 * How many RPC functions are kept. The rest are dropped, cheapest first.
	 *
	 * A separate ceiling from the actor one because RPCs are keyed by function rather than by instance, so
	 * the set is naturally small: it is bounded by how many different replicated functions your game has,
	 * not by how many actors are calling them.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "8", ClampMax = "1000"))
	int32 MaxTrackedRPCs = 128;

	//~ Cost model --------------------------------------------------------------------------------------

	/**
	 * Bytes charged to every replication update on top of the properties that changed.
	 *
	 * A bunch is not free: it carries a channel index, a sequence, the flags, and for a partial bunch a
	 * fragment header. Eleven bytes is the working figure for a property bunch on an open channel. It is
	 * exposed because it is an assumption rather than a measurement, and an assumption you can see and change
	 * is worth four you cannot.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Cost Model", meta = (ClampMin = "0", ClampMax = "128"))
	int32 BunchOverheadBytes = 11;

	/** Bytes charged to every RPC on top of its parameters: the function index and the channel header. */
	UPROPERTY(config, EditAnywhere, Category = "Cost Model", meta = (ClampMin = "0", ClampMax = "128"))
	int32 RPCOverheadBytes = 4;

	/**
	 * Characters assumed for a string whose length NetLens cannot see.
	 *
	 * Only ever used for RPC parameters priced from their type alone. Actor properties are priced from the
	 * live value, where the length is known exactly and this number is never consulted.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Cost Model", meta = (ClampMin = "0", ClampMax = "256"))
	int32 AssumedStringLength = 16;

	//~ Presentation ------------------------------------------------------------------------------------

	/**
	 * Show the panel as soon as a world with a net driver starts.
	 *
	 * Off. NetLens is a tool you reach for, not a HUD element, and a panel nobody asked for over the top of
	 * a playtest is a panel that gets the plugin uninstalled. NetLens.Show 1 turns it on at any moment.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowOverlayByDefault = false;

	/**
	 * Draw the panel through AHUD::OnHUDPostRender, so a project keeps its own HUD class.
	 *
	 * On, because the alternative is asking a shipping project to reparent its HUD to a diagnostic tool's
	 * class in order to look at a number, and nobody is going to do that. ANetLensHUD exists for projects
	 * that would rather be explicit; the two never double-draw, because both go through the same function and
	 * it refuses to draw twice in one frame.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bAutoDrawOverlayOnAnyHUD = true;

	/** Where the panel sits, in pixels from the top left of the viewport. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	FVector2D OverlayOrigin = FVector2D(28.0f, 60.0f);

	/** How wide the panel is drawn, in pixels. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation",
		meta = (ClampMin = "320.0", ClampMax = "1600.0"))
	float OverlayWidth = 620.0f;

	/**
	 * How many actor rows the panel prints.
	 *
	 * Twelve fits comfortably above a 1080p viewport's lower half and is more than the number of actors that
	 * are ever actually the problem. NetLens.TopN changes it without leaving the game.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "1", ClampMax = "60"))
	int32 TopN = 12;

	/**
	 * How many of those rows are opened up into their individual properties.
	 *
	 * Five, and not "all of them", for the same reason the panel has a Top N at all: the breakdown is what
	 * you read once you already know which row you care about, and printing forty properties turns the answer
	 * back into the haystack it came from. This is also a cost ceiling - only these rows carry a property
	 * list out of the sampler.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "0", ClampMax = "20"))
	int32 PropertyBreakdownRows = 5;

	/** How many properties are printed under one opened row. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "1", ClampMax = "32"))
	int32 PropertiesPerRow = 5;

	/** How many RPC rows the panel prints under the actor table. Zero hides the section. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation", meta = (ClampMin = "0", ClampMax = "30"))
	int32 TopRPCRows = 6;

	/** What the table is sorted by when the panel opens. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	ENetLensSort SortBy = ENetLensSort::Bytes;

	//~ Export ------------------------------------------------------------------------------------------

	/**
	 * Folder the CSV is written to, relative to the project's Saved directory.
	 *
	 * Relative on purpose. An absolute path baked into a project setting is a path that exists on exactly one
	 * machine, and the first thing that happens to it is that somebody commits it. NetLens.Export with no
	 * argument writes here under a timestamped name; NetLens.Export with a path writes exactly where you say.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	FString CsvSubdirectory = TEXT("NetLens");

	/** Include a row per property, not just per actor. Makes the file bigger and the analysis possible. */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	bool bExportPropertyRows = true;
};
