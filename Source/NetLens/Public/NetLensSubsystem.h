// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"

// For FOnSendRPC, the engine's own RPC send hook. It is declared in this header and only outside Shipping,
// which is exactly the shape of NetLens's automatic RPC capture, so the include and the guard travel together.
#include "Engine/NetDriver.h"

#include "NetLensTypes.h"
#include "NetLensSubsystem.generated.h"

class AActor;
class UCanvas;
class UFunction;
class UNetConnection;
class UNetLensComponent;
class FNetLensObjectShadow;

/**
 * The counter. One per world, alive exactly as long as the world it measures.
 *
 * Where the numbers come from, because a diagnostic that will not say is not one:
 *
 *   **Updates are read, not inferred.** Every frame the subsystem walks the open actor channels of every
 *   connection and looks at UActorChannel::LastUpdateTime - a timestamp the engine wrote when it genuinely
 *   replicated that actor to that connection. When it moves, that is one real update. This is why the scan
 *   runs every frame rather than at the sample rate: on a server, replication happens at most once per actor
 *   per frame, so a per-frame scan sees all of them and a slower one would alias.
 *
 *   **Content is compared, not assumed.** When an actor's channel has moved, its replicated properties are
 *   compared against a shadow copy (see FNetLensObjectShadow) and only what changed is charged. That is the
 *   same decision the replication system makes, made the same way, so a property that never moves never
 *   appears however often its actor is considered.
 *
 *   **Cost is modelled, and says so.** Each changed property is priced from its type using the engine's own
 *   serialisation rules; the bunch overhead is a documented setting. The panel prints the attributed total
 *   next to the driver's real OutBytesPerSecond so the gap between the two is on screen rather than hidden.
 *
 *   **RPCs are hooked where the engine allows it.** UNetDriver::SendRPCDel is the engine's own send hook.
 *   It exists in Development and Test builds and Epic compiles it out of Shipping, so automatic RPC capture
 *   follows that line exactly, and NoteRPC is there for the Shipping case. NetLens chains whatever was bound
 *   before it and never blocks a call - it is a lens, not a valve.
 *
 * And what it costs. NetLens is bounded by MaxTrackedActors shadow buffers, each of at most
 * MaxPropertiesPerObject properties on the actor plus MaxReplicatedComponentsPerActor components. Nothing in
 * here grows with the size of the level. Actors past the ceiling still have their updates counted - that
 * costs one timestamp per open channel - and are reported as a single aggregate row, so the ceiling can hide
 * the identity of a cost but never the cost itself.
 */
UCLASS()
class NETLENS_API UNetLensSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UNetLensSubsystem();

	/**
	 * Declared here and defined in the .cpp, so the one place that tears the tracking state down is the one
	 * file where every type in it is complete.
	 */
	virtual ~UNetLensSubsystem() override;

	//~ USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	//~ Control ------------------------------------------------------------------------------------------

	/** Show or hide the panel. */
	void SetShowOverlay(bool bShow);

	/** Whether the panel is being drawn. */
	bool IsShowingOverlay() const { return bShowOverlay; }

	/**
	 * Stop measuring. The table holds whatever it had when you called this.
	 *
	 * Freezing does not stop the game, does not stop replication and does not stop the driver's own counters.
	 * It stops NetLens writing into its windows, which is the only thing you actually wanted stopped.
	 */
	void Freeze();

	/** Start measuring again, keeping the window's contents. */
	void Resume();

	/** Whether the measurement is frozen. */
	bool IsFrozen() const { return bFrozen; }

	/** Throw everything away and start from an empty window. Also unfreezes. */
	void ResetMeasurements();

	/**
	 * Write the current table to a CSV.
	 *
	 * Empty path - a timestamped file under Saved/<CsvSubdirectory>. Relative path - relative to the same
	 * place. Absolute path - exactly there. OutPath is always filled in with where it went.
	 */
	bool ExportCsv(const FString& Path, FString& OutPath);

	/** How many rows the panel prints. */
	void SetTopN(int32 InTopN);

	/** How many rows the panel prints. */
	int32 GetTopN() const { return TopN; }

	/** What the table is sorted by. */
	void SetSortBy(ENetLensSort InSortBy);

	/** What the table is sorted by. */
	ENetLensSort GetSortBy() const { return SortBy; }

	//~ Reading ------------------------------------------------------------------------------------------

	/** Every actor NetLens is following, unranked and uncapped. Rebuilt at the settings' sample rate. */
	const TArray<FNetLensActorStat>& GetActorStats() const { return ActorStats; }

	/** Every RPC NetLens has seen inside the window, unranked. */
	const TArray<FNetLensRPCStat>& GetRPCStats() const { return RPCStats; }

	/** The header line: the driver's own numbers, and how much of them NetLens accounted for. */
	const FNetLensTotals& GetTotals() const { return Totals; }

	/** One actor's row, whether or not it is inside the top N. False when it is not being followed. */
	bool FindActorStat(const AActor* Actor, FNetLensActorStat& OutStat) const;

	/** Print the current table to the log. What NetLens.Stats does. */
	void LogSummary() const;

	//~ Recording ----------------------------------------------------------------------------------------

	/**
	 * Record one RPC by hand, for Shipping builds where the engine's send hook does not exist.
	 *
	 * The parameters are priced from the function's declared signature rather than from the values, because
	 * a caller has no way to hand us the parameter frame. For everything but variable-length arrays and
	 * strings that is the same number.
	 */
	void NoteRPC(AActor* Actor, FName FunctionName);

	//~ Components ---------------------------------------------------------------------------------------

	/** A UNetLensComponent asking for its actor to be followed and highlighted for as long as it lives. */
	void RegisterTrackedComponent(UNetLensComponent* Component);

	/** The same component letting go. */
	void UnregisterTrackedComponent(UNetLensComponent* Component);

	/** Whether an actor is carrying a NetLens component that asked to be highlighted. */
	bool IsActorHighlighted(const AActor* Actor) const;

	//~ Drawing ------------------------------------------------------------------------------------------

	/**
	 * Draw the panel.
	 *
	 * Called from two places that must never both take effect: ANetLensHUD::DrawHUD for projects that chose
	 * our HUD class, and AHUD::OnHUDPostRender for the far more common case of a project that has its own.
	 * The frame counter guard below is what makes having both safe.
	 */
	void DrawOverlay(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

	/** True when the panel has already been drawn this frame, whichever route it came in by. */
	bool HasDrawnOverlayThisFrame() const;

	/** How many lines the panel will draw, so the background can be sized before the text is laid out. */
	int32 GetOverlayLineCount() const;

private:
	//~ Sampling -----------------------------------------------------------------------------------------

	/** One replicated property being followed on one actor. */
	struct FPropertyTrack
	{
		FNetLensWindow Window;
		FName OwnerClassName;

		/** Bits the last observed change was priced at. What the "bytes per change" column reads. */
		int32 LastChangeBits = 0;
	};

	/** One actor being followed in detail. */
	struct FTrackedActor
	{
		TWeakObjectPtr<AActor> Actor;
		FName ActorName;
		FName ClassName;

		FNetLensWindow Window;
		TMap<FName, FPropertyTrack> Properties;

		/**
		 * The actor's own shadow first, then one per replicated component, up to the ceiling.
		 *
		 * Shared rather than unique, and for one concrete reason: FNetLensObjectShadow is a private type
		 * that this public header only forward-declares, and a TUniquePtr cannot be destroyed where its
		 * pointee is incomplete - every translation unit that includes this header would have to see the
		 * shadow's definition. A TSharedPtr captures its deleter at construction, in the .cpp where the type
		 * is complete, so the private type stays private. The reference count is paid once per tracked actor
		 * and is never touched again.
		 */
		TArray<TSharedPtr<FNetLensObjectShadow>> Shadows;

		/** Last seen UActorChannel::LastUpdateTime, per connection. The source of every update count. */
		TMap<FObjectKey, double> ChannelUpdateTimes;

		/** Rebuilt every frame by the channel scan. */
		int32 RelevantConnections = 0;
		int32 UpdatedConnectionsThisFrame = 0;

		float NetUpdateFrequency = 0.0f;
		bool bHighlighted = false;

		/** World seconds the actor was last seen on an open channel. Drives retirement. */
		double LastSeenTime = 0.0;
	};

	/** One replicated function being followed, keyed by the UFunction rather than by the caller. */
	struct FRPCTrack
	{
		FName FunctionName;
		FName ClassName;
		FNetLensWindow Window;
		bool bReliable = false;
		bool bMulticast = false;
		int32 LastCallBits = 0;
		int32 CallsInWindow = 0;
	};

	/** Pull the settings into members once, so a sample never walks the CDO. */
	void ApplySettings();

	/** Walk every open actor channel, count real updates, and charge what changed. Runs every frame. */
	void ScanFrame(double Now);

	/** Charge one actor for the updates the scan found on it this frame. */
	void ChargeActor(FTrackedActor& Tracked, AActor* Actor, double Now);

	/** Make sure the shadow list matches the actor and its replicated components. */
	void RefreshShadows(FTrackedActor& Tracked, AActor* Actor);

	/** Rebuild the public stat arrays. Runs at the settings' sample rate, not every frame. */
	void RebuildSnapshot(double Now);

	/** Drop actors that have no channel left and nothing in their window. */
	void RetireStaleEntries(double Now);

	/** The connections of the world's net driver, server side or client side. */
	void GatherConnections(UNetDriver* Driver, TArray<UNetConnection*>& OutConnections) const;

	/** Bind or rebind the engine's RPC send hook. Cheap, and safe to call when already bound. */
	void EnsureRPCHook(UNetDriver* Driver);

	/** Let go of the RPC send hook, restoring whatever was bound before us. */
	void ReleaseRPCHook();

	/** Record one RPC. Shared by the automatic hook and by NoteRPC. */
	void RecordRPC(const AActor* Actor, const UFunction* Function, const void* Parms, int32 ConnectionCount,
		double Now);

#if !UE_BUILD_SHIPPING
	/** The engine's send hook. Records, then hands the call on to whoever had the hook before us. */
	void HandleSendRPC(AActor* Actor, UFunction* Function, void* Parms, FOutParmRec* OutParms, FFrame* Stack,
		UObject* SubObject, bool& bBlockSendRPC);
#endif

	/**
	 * Real seconds, not game seconds.
	 *
	 * A window measured in game time would stretch and shrink with time dilation and stop entirely on a
	 * pause, and "kilobytes per second" would stop meaning kilobytes per second. Freeze is the tool for
	 * holding the numbers still; the clock underneath them is the wall.
	 */
	double GetNetLensTime() const;

	/** Bytes one update costs on average across the tracked set. What an overflow update is priced at. */
	double ComputeAverageBytesPerUpdate() const;

	/** Bind or unbind the any-HUD draw delegate to match the setting. */
	void RefreshHudDelegate();

	/** The any-HUD draw route. */
	void OnAnyHUDPostRender(class AHUD* HUD, UCanvas* Canvas);

	//~ State --------------------------------------------------------------------------------------------

	TMap<FObjectKey, FTrackedActor> TrackedActors;
	TMap<FObjectKey, FRPCTrack> TrackedRPCs;

	/**
	 * Last seen update time of a channel whose actor is past the tracking ceiling.
	 *
	 * One double per open channel and nothing else - no shadow buffer, no property map. This is what lets
	 * the aggregate row be an honest count of real updates rather than a guess, at a cost that does not
	 * scale with anything but the number of channels the game itself opened.
	 */
	TMap<FObjectKey, double> OverflowChannelTimes;

	/** What the actors past the ceiling cost, together. */
	FNetLensWindow OverflowWindow;

	/** Actors a UNetLensComponent asked to be followed regardless of the ceiling. */
	TSet<FObjectKey> HighlightedActors;

	UPROPERTY()
	TArray<TObjectPtr<UNetLensComponent>> RegisteredComponents;

	TArray<FNetLensActorStat> ActorStats;
	TArray<FNetLensRPCStat> RPCStats;
	FNetLensTotals Totals;

	/** Distinct actors seen on an open channel this frame. Reused rather than reallocated. */
	TSet<FObjectKey> SeenActorScratch;

	/** Open actor channels counted by the last frame's scan. */
	int32 LiveChannelCount = 0;

	/** Distinct actors counted by the last frame's scan, tracked and untracked together. */
	int32 LiveSeenActors = 0;

	//~ Settings, copied at Initialize --------------------------------------------------------------------

	double WindowSeconds = 10.0;
	int32 BucketsPerWindow = 20;
	double SampleInterval = 0.1;
	int32 MaxTrackedActors = 200;
	int32 MaxPropertiesPerObject = 64;
	int32 MaxReplicatedComponentsPerActor = 8;
	int32 MaxTrackedRPCs = 128;
	int32 BunchOverheadBytes = 11;
	int32 RPCOverheadBytes = 4;
	int32 TopN = 12;
	int32 PropertyBreakdownRows = 5;
	int32 PropertiesPerRow = 5;
	int32 TopRPCRows = 6;
	bool bTrackProperties = true;
	bool bTrackRPCs = true;
	bool bAutoDrawOverlayOnAnyHUD = true;
	bool bExportPropertyRows = true;
	FVector2D OverlayOrigin = FVector2D(28.0f, 60.0f);
	float OverlayWidth = 620.0f;
	ENetLensSort SortBy = ENetLensSort::Bytes;
	FString CsvSubdirectory = TEXT("NetLens");

	//~ Runtime -------------------------------------------------------------------------------------------

	bool bShowOverlay = false;
	bool bFrozen = false;

	/** Seconds until the next snapshot rebuild. */
	double TimeUntilSnapshot = 0.0;

	/**
	 * Guards against the panel being drawn twice in one frame by two different routes.
	 *
	 * Starts at the largest possible frame number rather than at zero, because zero is a frame the engine
	 * really has - the first one - and starting there would silently skip the panel on it.
	 */
	mutable uint64 LastOverlayDrawFrame = TNumericLimits<uint64>::Max();

	/** Lines the last draw produced, so anything laying out around the panel knows how tall it was. */
	mutable int32 LastOverlayLineCount = 0;

	FDelegateHandle HudPostRenderHandle;

	/** The driver we took the RPC hook from, so we know when it has been replaced under us. */
	TWeakObjectPtr<UNetDriver> HookedDriver;

#if !UE_BUILD_SHIPPING
	/**
	 * Whatever was bound to the driver's RPC hook before NetLens took it.
	 *
	 * The engine's hook is a single-cast delegate, so taking it means displacing whoever had it - and in a
	 * project running NetcodeUnitTest, somebody does. Keeping the old one and calling it means NetLens is
	 * transparent rather than merely polite about it.
	 */
	FOnSendRPC PreviousSendRPC;

	bool bHasRPCHook = false;
#endif
};
