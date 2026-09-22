// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "NetLensSubsystem.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/ActorChannel.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "GlobalRenderResources.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "NetLensComponent.h"
#include "NetLensLog.h"
#include "NetLensSettings.h"
#include "NetLensShadowState.h"
#include "NetLensStatics.h"
#include "SceneTypes.h"
#include "UObject/UnrealType.h"

namespace NetLensPrivate
{
	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;

	/**
	 * How much a channel's timestamp has to move to count as a new update.
	 *
	 * The timestamp is a double of seconds since the driver started, and two genuinely distinct updates are
	 * at least one frame apart. A tolerance this small only ever rejects a bit-identical read of the same
	 * value, which is exactly what it is for.
	 */
	static constexpr double UpdateEpsilon = 1.0e-6;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.68f);
	static const FLinearColor HeadingColor(0.55f, 0.85f, 1.0f, 1.0f);
	static const FLinearColor BodyColor(0.88f, 0.88f, 0.88f, 1.0f);
	static const FLinearColor DimColor(0.55f, 0.55f, 0.55f, 1.0f);
	static const FLinearColor GoodColor(0.45f, 0.95f, 0.55f, 1.0f);
	static const FLinearColor WarnColor(1.0f, 0.80f, 0.35f, 1.0f);
	static const FLinearColor HotColor(1.0f, 0.42f, 0.38f, 1.0f);
	static const FLinearColor HighlightColor(0.65f, 0.95f, 1.0f, 1.0f);
	static const FLinearColor BarColor(0.30f, 0.55f, 0.95f, 0.55f);

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size,
		const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	/** Colour a row by how large a share of the attributed traffic it is. Red is not decoration. */
	static const FLinearColor& ShareColor(double Share)
	{
		if (Share >= 0.30)
		{
			return HotColor;
		}
		if (Share >= 0.12)
		{
			return WarnColor;
		}
		return BodyColor;
	}

	/** Pad or clip a name so the columns line up in a monospaced-enough font. */
	static FString Fit(const FString& Text, int32 Width)
	{
		if (Text.Len() >= Width)
		{
			return Text.Left(Width);
		}
		return Text + FString::ChrN(Width - Text.Len(), TEXT(' '));
	}

	static UNetLensSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UNetLensSubsystem>() : nullptr;
	}
}

//~ Lifetime -----------------------------------------------------------------------------------------------

UNetLensSubsystem::UNetLensSubsystem() = default;

// Out of line, where FNetLensObjectShadow is a complete type. See the declaration for why.
UNetLensSubsystem::~UNetLensSubsystem() = default;

void UNetLensSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();

	OverflowWindow.Configure(WindowSeconds, BucketsPerWindow);

	bShowOverlay = UNetLensSettings::Get().bShowOverlayByDefault;
	TimeUntilSnapshot = 0.0;

	RefreshHudDelegate();
}

void UNetLensSubsystem::Deinitialize()
{
	if (HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}

	ReleaseRPCHook();

	// The shadows own raw buffers with constructed properties in them. Dropping the map here, while the
	// classes those properties belong to are certainly still alive, is the tidy place to do it.
	TrackedActors.Empty();
	TrackedRPCs.Empty();
	OverflowChannelTimes.Empty();
	HighlightedActors.Empty();
	RegisteredComponents.Empty();

	ActorStats.Empty();
	RPCStats.Empty();

	Super::Deinitialize();
}

void UNetLensSubsystem::ApplySettings()
{
	const UNetLensSettings& Settings = UNetLensSettings::Get();

	WindowSeconds = Settings.WindowSeconds;
	BucketsPerWindow = Settings.BucketsPerWindow;
	SampleInterval = 1.0 / FMath::Max(1.0f, Settings.SamplesPerSecond);
	MaxTrackedActors = Settings.MaxTrackedActors;
	MaxPropertiesPerObject = Settings.MaxPropertiesPerObject;
	MaxReplicatedComponentsPerActor = Settings.MaxReplicatedComponentsPerActor;
	MaxTrackedRPCs = Settings.MaxTrackedRPCs;
	BunchOverheadBytes = Settings.BunchOverheadBytes;
	RPCOverheadBytes = Settings.RPCOverheadBytes;
	TopN = Settings.TopN;
	PropertyBreakdownRows = Settings.PropertyBreakdownRows;
	PropertiesPerRow = Settings.PropertiesPerRow;
	TopRPCRows = Settings.TopRPCRows;
	bTrackProperties = Settings.bTrackProperties;
	bTrackRPCs = Settings.bTrackRPCs;
	bAutoDrawOverlayOnAnyHUD = Settings.bAutoDrawOverlayOnAnyHUD;
	bExportPropertyRows = Settings.bExportPropertyRows;
	OverlayOrigin = Settings.OverlayOrigin;
	OverlayWidth = Settings.OverlayWidth;
	SortBy = Settings.SortBy;
	CsvSubdirectory = Settings.CsvSubdirectory;
}

TStatId UNetLensSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNetLensSubsystem, STATGROUP_Tickables);
}

double UNetLensSubsystem::GetNetLensTime() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetRealTimeSeconds() : 0.0;
}

void UNetLensSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const double Now = GetNetLensTime();

	// The channel scan runs every frame and the snapshot rebuild does not, and that split is the whole
	// performance design. Scanning is a walk over the open channels reading one double each; it has to be
	// every frame because a server replicates an actor at most once per frame and a slower scan would merge
	// two updates into one. Rebuilding the public stat arrays is the expensive half, and nothing reads it
	// faster than a human can look at it.
	if (!bFrozen)
	{
		ScanFrame(Now);
	}

	TimeUntilSnapshot -= DeltaTime;
	if (TimeUntilSnapshot <= 0.0)
	{
		TimeUntilSnapshot = SampleInterval;
		RebuildSnapshot(Now);
	}
}

//~ Control ------------------------------------------------------------------------------------------------

void UNetLensSubsystem::SetShowOverlay(bool bShow)
{
	bShowOverlay = bShow;
}

void UNetLensSubsystem::Freeze()
{
	bFrozen = true;
	Totals.bFrozen = true;
}

void UNetLensSubsystem::Resume()
{
	bFrozen = false;
	Totals.bFrozen = false;
}

void UNetLensSubsystem::ResetMeasurements()
{
	TrackedActors.Empty();
	TrackedRPCs.Empty();
	OverflowChannelTimes.Empty();
	OverflowWindow.Configure(WindowSeconds, BucketsPerWindow);

	ActorStats.Reset();
	RPCStats.Reset();
	Totals = FNetLensTotals();

	bFrozen = false;
	TimeUntilSnapshot = 0.0;
}

void UNetLensSubsystem::SetTopN(int32 InTopN)
{
	TopN = FMath::Clamp(InTopN, 1, 60);
}

void UNetLensSubsystem::SetSortBy(ENetLensSort InSortBy)
{
	SortBy = InSortBy;
}

//~ Components ---------------------------------------------------------------------------------------------

void UNetLensSubsystem::RegisterTrackedComponent(UNetLensComponent* Component)
{
	if (!Component)
	{
		return;
	}

	RegisteredComponents.AddUnique(Component);

	if (const AActor* Owner = Component->GetOwner())
	{
		HighlightedActors.Add(FObjectKey(Owner));

		if (FTrackedActor* Tracked = TrackedActors.Find(FObjectKey(Owner)))
		{
			Tracked->bHighlighted = true;
		}
	}
}

void UNetLensSubsystem::UnregisterTrackedComponent(UNetLensComponent* Component)
{
	if (!Component)
	{
		return;
	}

	RegisteredComponents.Remove(Component);

	if (const AActor* Owner = Component->GetOwner())
	{
		const FObjectKey Key(Owner);

		// Only drop the highlight if no other NetLens component on the same actor still wants it. Two of
		// them on one actor is unusual but not wrong, and the second should not be switched off by the first
		// one going away.
		bool bStillWanted = false;
		for (const TObjectPtr<UNetLensComponent>& Other : RegisteredComponents)
		{
			if (Other && Other->GetOwner() == Owner)
			{
				bStillWanted = true;
				break;
			}
		}

		if (!bStillWanted)
		{
			HighlightedActors.Remove(Key);

			if (FTrackedActor* Tracked = TrackedActors.Find(Key))
			{
				Tracked->bHighlighted = false;
			}
		}
	}
}

bool UNetLensSubsystem::IsActorHighlighted(const AActor* Actor) const
{
	return Actor && HighlightedActors.Contains(FObjectKey(Actor));
}

//~ Scanning -----------------------------------------------------------------------------------------------

void UNetLensSubsystem::GatherConnections(UNetDriver* Driver, TArray<UNetConnection*>& OutConnections) const
{
	OutConnections.Reset();

	if (!Driver)
	{
		return;
	}

	// A client has exactly one connection - to the server - and its channels carry what it is *receiving*.
	// A server has one per client. Both are worth measuring and the code below does not care which it got.
	if (UNetConnection* ServerConnection = Driver->ServerConnection)
	{
		OutConnections.Add(ServerConnection);
	}

	for (const TObjectPtr<UNetConnection>& Connection : Driver->ClientConnections)
	{
		if (Connection)
		{
			OutConnections.Add(Connection);
		}
	}
}

void UNetLensSubsystem::ScanFrame(double Now)
{
	UWorld* World = GetWorld();
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;

	EnsureRPCHook(Driver);

	// Every tracked actor starts the frame with nothing counted. Anything that does not turn up on a channel
	// this frame ends it with zero relevant connections, which is how a row goes grey when its actor leaves
	// the relevancy set rather than freezing at its last value.
	for (TPair<FObjectKey, FTrackedActor>& Pair : TrackedActors)
	{
		Pair.Value.RelevantConnections = 0;
		Pair.Value.UpdatedConnectionsThisFrame = 0;
	}

	if (!Driver)
	{
		LiveChannelCount = 0;
		LiveSeenActors = 0;
		return;
	}

	TArray<UNetConnection*> Connections;
	GatherConnections(Driver, Connections);

	SeenActorScratch.Reset();

	int32 ChannelCount = 0;
	int32 OverflowUpdates = 0;

	for (UNetConnection* Connection : Connections)
	{
		if (!Connection)
		{
			continue;
		}

		const FObjectKey ConnectionKey(Connection);

		for (auto It = Connection->ActorChannelConstIterator(); It; ++It)
		{
			UActorChannel* Channel = It.Value();
			if (!Channel)
			{
				continue;
			}

			AActor* Actor = Channel->GetActor();
			if (!Actor)
			{
				continue;
			}

			++ChannelCount;

			const FObjectKey ActorKey(Actor);
			SeenActorScratch.Add(ActorKey);

			FTrackedActor* Tracked = TrackedActors.Find(ActorKey);

			if (!Tracked)
			{
				// The ceiling. An actor a NetLens component asked for is admitted regardless of it - that is
				// the entire purpose of the component, and there are never many of them.
				const bool bForced = HighlightedActors.Contains(ActorKey);

				if (bForced || TrackedActors.Num() < MaxTrackedActors)
				{
					Tracked = &TrackedActors.Add(ActorKey);
					Tracked->Actor = Actor;
					Tracked->ActorName = Actor->GetFName();
					Tracked->ClassName = Actor->GetClass()->GetFName();
					Tracked->bHighlighted = bForced;
					Tracked->Window.Configure(WindowSeconds, BucketsPerWindow);
				}
			}

			if (Tracked)
			{
				Tracked->LastSeenTime = Now;
				++Tracked->RelevantConnections;

				double* LastUpdate = Tracked->ChannelUpdateTimes.Find(ConnectionKey);
				if (!LastUpdate)
				{
					// First sight of this actor on this connection. Recorded, not charged: an actor that has
					// just become relevant would otherwise be billed for the whole of its initial state on
					// the frame it appears, and every spawn would look like a bandwidth spike.
					Tracked->ChannelUpdateTimes.Add(ConnectionKey, Channel->LastUpdateTime);
				}
				else
				{
					if (Channel->LastUpdateTime > *LastUpdate + NetLensPrivate::UpdateEpsilon)
					{
						++Tracked->UpdatedConnectionsThisFrame;
					}
					*LastUpdate = Channel->LastUpdateTime;
				}
			}
			else
			{
				// Past the ceiling. One timestamp, keyed on the channel, and nothing else - no shadow, no
				// property map. Real updates are still counted, so the aggregate row is a count rather than
				// an estimate; only the bytes are averaged.
				const FObjectKey ChannelKey(Channel);

				double* LastUpdate = OverflowChannelTimes.Find(ChannelKey);
				if (!LastUpdate)
				{
					OverflowChannelTimes.Add(ChannelKey, Channel->LastUpdateTime);
				}
				else
				{
					if (Channel->LastUpdateTime > *LastUpdate + NetLensPrivate::UpdateEpsilon)
					{
						++OverflowUpdates;
					}
					*LastUpdate = Channel->LastUpdateTime;
				}
			}
		}
	}

	LiveChannelCount = ChannelCount;
	LiveSeenActors = SeenActorScratch.Num();

	// The comparison happens once per actor per frame, not once per connection. The bunch that goes to three
	// clients has the same contents three times; comparing three times would cost three times as much and
	// give the same answer.
	for (TPair<FObjectKey, FTrackedActor>& Pair : TrackedActors)
	{
		FTrackedActor& Tracked = Pair.Value;
		if (Tracked.UpdatedConnectionsThisFrame <= 0)
		{
			continue;
		}

		if (AActor* Actor = Tracked.Actor.Get())
		{
			ChargeActor(Tracked, Actor, Now);
		}
	}

	if (OverflowUpdates > 0)
	{
		const double BytesPerUpdate = ComputeAverageBytesPerUpdate();
		OverflowWindow.Add(Now, BytesPerUpdate * OverflowUpdates, OverflowUpdates);
	}
}

void UNetLensSubsystem::RefreshShadows(FTrackedActor& Tracked, AActor* Actor)
{
	// Slot zero is always the actor itself.
	if (Tracked.Shadows.Num() == 0 || !Tracked.Shadows[0].IsValid() || !Tracked.Shadows[0]->IsValidFor(Actor))
	{
		Tracked.Shadows.Reset();
		Tracked.Shadows.Add(MakeShared<FNetLensObjectShadow>(Actor, MaxPropertiesPerObject));
	}

	if (MaxReplicatedComponentsPerActor <= 0)
	{
		Tracked.Shadows.SetNum(1, EAllowShrinking::Yes);
		return;
	}

	const TArray<UActorComponent*>& Replicated = Actor->GetReplicatedComponents();
	const int32 Wanted = FMath::Min(Replicated.Num(), MaxReplicatedComponentsPerActor);

	bool bRebuild = Tracked.Shadows.Num() != Wanted + 1;
	if (!bRebuild)
	{
		for (int32 Index = 0; Index < Wanted; ++Index)
		{
			const TSharedPtr<FNetLensObjectShadow>& Shadow = Tracked.Shadows[Index + 1];
			if (!Shadow.IsValid() || !Shadow->IsValidFor(Replicated[Index]))
			{
				bRebuild = true;
				break;
			}
		}
	}

	if (!bRebuild)
	{
		return;
	}

	Tracked.Shadows.SetNum(1, EAllowShrinking::Yes);

	for (int32 Index = 0; Index < Wanted; ++Index)
	{
		if (UActorComponent* Component = Replicated[Index])
		{
			Tracked.Shadows.Add(MakeShared<FNetLensObjectShadow>(Component, MaxPropertiesPerObject));
		}
	}
}

void UNetLensSubsystem::ChargeActor(FTrackedActor& Tracked, AActor* Actor, double Now)
{
	const int32 Updates = Tracked.UpdatedConnectionsThisFrame;
	if (Updates <= 0)
	{
		return;
	}

	Tracked.NetUpdateFrequency = Actor->GetNetUpdateFrequency();

	// A plain allocator, because FNetLensObjectShadow::CollectChanges takes a TArray by reference and an
	// inline-allocated array is a different type. Reserving is the cheap half of what an inline allocator
	// would have bought, and this runs once per replicating actor per frame, not once per property.
	TArray<FNetLensShadowChange> Changes;
	Changes.Reserve(24);

	int32 PayloadBits = 0;

	if (bTrackProperties)
	{
		RefreshShadows(Tracked, Actor);

		for (const TSharedPtr<FNetLensObjectShadow>& Shadow : Tracked.Shadows)
		{
			if (Shadow.IsValid())
			{
				Shadow->CollectChanges(Shadow->GetObject(), Changes);
			}
		}

		for (const FNetLensShadowChange& Change : Changes)
		{
			PayloadBits += Change.Bits;
		}
	}

	// Every update pays the bunch overhead whether or not anything in it changed. That is not a rounding
	// convenience - an actor that replicates thirty times a second and changes nothing really does cost
	// thirty bunch headers, and that cost is precisely the one a NetUpdateFrequency fix removes. Charging
	// zero for it would hide the most common replication mistake there is.
	const double BytesThisUpdate = (PayloadBits / 8.0) + static_cast<double>(BunchOverheadBytes);

	Tracked.Window.Add(Now, BytesThisUpdate * Updates, Updates);

	for (const FNetLensShadowChange& Change : Changes)
	{
		FPropertyTrack* Track = Tracked.Properties.Find(Change.PropertyName);
		if (!Track)
		{
			if (Tracked.Properties.Num() >= MaxPropertiesPerObject * (1 + MaxReplicatedComponentsPerActor))
			{
				continue;
			}

			Track = &Tracked.Properties.Add(Change.PropertyName);
			Track->Window.Configure(WindowSeconds, BucketsPerWindow);
		}

		Track->OwnerClassName = Change.OwnerClassName;
		Track->LastChangeBits = Change.Bits;
		Track->Window.Add(Now, (Change.Bits / 8.0) * Updates, Updates);
	}
}

double UNetLensSubsystem::ComputeAverageBytesPerUpdate() const
{
	double Bytes = 0.0;
	double Updates = 0.0;

	for (const TPair<FObjectKey, FTrackedActor>& Pair : TrackedActors)
	{
		Bytes += Pair.Value.Window.GetTotalBytes();
		Updates += Pair.Value.Window.GetTotalEvents();
	}

	if (Updates <= 0.0)
	{
		// Nothing measured yet. The bunch overhead is the floor every update pays, so it is the honest
		// minimum rather than a made-up number.
		return static_cast<double>(BunchOverheadBytes);
	}

	return Bytes / Updates;
}

//~ Snapshot -----------------------------------------------------------------------------------------------

void UNetLensSubsystem::RebuildSnapshot(double Now)
{
	if (!bFrozen)
	{
		// Advance every window even when nothing was added to it, so an actor that has gone quiet decays to
		// zero instead of holding the last rate it had.
		for (TPair<FObjectKey, FTrackedActor>& Pair : TrackedActors)
		{
			Pair.Value.Window.Advance(Now);
			for (TPair<FName, FPropertyTrack>& PropertyPair : Pair.Value.Properties)
			{
				PropertyPair.Value.Window.Advance(Now);
			}
		}

		for (TPair<FObjectKey, FRPCTrack>& Pair : TrackedRPCs)
		{
			Pair.Value.Window.Advance(Now);
		}

		OverflowWindow.Advance(Now);

		RetireStaleEntries(Now);
	}

	if (bFrozen)
	{
		// The whole point of a freeze is that what is on screen stops moving. Rebuilding would recompute the
		// same rates from windows that are still decaying and the table would drain while you read it.
		return;
	}

	ActorStats.Reset(TrackedActors.Num() + 1);

	for (const TPair<FObjectKey, FTrackedActor>& Pair : TrackedActors)
	{
		const FTrackedActor& Tracked = Pair.Value;

		FNetLensActorStat& Stat = ActorStats.AddDefaulted_GetRef();
		Stat.Actor = Tracked.Actor;
		Stat.ActorName = Tracked.ActorName;
		Stat.ClassName = Tracked.ClassName;
		Stat.BytesPerSecond = static_cast<float>(Tracked.Window.GetBytesPerSecond());
		Stat.PeakBytesPerSecond = static_cast<float>(Tracked.Window.GetPeakBytesPerSecond());
		Stat.UpdatesPerSecond = static_cast<float>(Tracked.Window.GetEventsPerSecond());
		Stat.TotalBytes = static_cast<float>(Tracked.Window.GetTotalBytes());
		Stat.NetUpdateFrequency = Tracked.NetUpdateFrequency;
		Stat.RelevantConnections = Tracked.RelevantConnections;
		Stat.bHighlighted = Tracked.bHighlighted;
	}

	// The overflow appears as an ordinary row with the aggregate flag set, so a caller that ignores the flag
	// still gets the right total and a caller that reads it can label the row honestly.
	if (OverflowWindow.GetTotalEvents() > 0.0)
	{
		const int32 Untracked = FMath::Max(0, LiveSeenActors - TrackedActors.Num());

		FNetLensActorStat& Stat = ActorStats.AddDefaulted_GetRef();
		Stat.ActorName = FName(*FString::Printf(TEXT("(%d untracked actors)"), Untracked));
		Stat.ClassName = TEXT("NetLensOverflow");
		Stat.bIsAggregate = true;
		Stat.AggregatedActors = Untracked;
		Stat.BytesPerSecond = static_cast<float>(OverflowWindow.GetBytesPerSecond());
		Stat.PeakBytesPerSecond = static_cast<float>(OverflowWindow.GetPeakBytesPerSecond());
		Stat.UpdatesPerSecond = static_cast<float>(OverflowWindow.GetEventsPerSecond());
		Stat.TotalBytes = static_cast<float>(OverflowWindow.GetTotalBytes());
	}

	// Property breakdowns are filled in only for the rows that will actually be opened. Building them for
	// every tracked actor would be two hundred arrays of sixty structs, ten times a second, so that a panel
	// could print five of them.
	{
		TArray<int32> Order;
		Order.Reserve(ActorStats.Num());
		for (int32 Index = 0; Index < ActorStats.Num(); ++Index)
		{
			Order.Add(Index);
		}

		Order.Sort([this](int32 A, int32 B)
		{
			return ActorStats[A].BytesPerSecond > ActorStats[B].BytesPerSecond;
		});

		const int32 BreakdownCount = FMath::Min(Order.Num(), FMath::Max(PropertyBreakdownRows, 0));

		for (int32 Rank = 0; Rank < BreakdownCount; ++Rank)
		{
			FNetLensActorStat& Stat = ActorStats[Order[Rank]];
			if (Stat.bIsAggregate || !Stat.Actor.IsValid())
			{
				continue;
			}

			const FTrackedActor* Tracked = TrackedActors.Find(FObjectKey(Stat.Actor.Get()));
			if (!Tracked)
			{
				continue;
			}

			Stat.Properties.Reset(Tracked->Properties.Num());

			for (const TPair<FName, FPropertyTrack>& PropertyPair : Tracked->Properties)
			{
				const FPropertyTrack& Track = PropertyPair.Value;
				if (Track.Window.IsEmpty())
				{
					continue;
				}

				FNetLensPropertyStat& PropertyStat = Stat.Properties.AddDefaulted_GetRef();
				PropertyStat.PropertyName = PropertyPair.Key;
				PropertyStat.OwnerClassName = Track.OwnerClassName;
				PropertyStat.BytesPerSecond = static_cast<float>(Track.Window.GetBytesPerSecond());
				PropertyStat.ChangesPerSecond = static_cast<float>(Track.Window.GetEventsPerSecond());
				PropertyStat.BytesPerChange = Track.LastChangeBits / 8.0f;
				PropertyStat.TotalBytes = static_cast<float>(Track.Window.GetTotalBytes());
			}

			Stat.Properties.Sort([](const FNetLensPropertyStat& A, const FNetLensPropertyStat& B)
			{
				return A.BytesPerSecond > B.BytesPerSecond;
			});
		}
	}

	RPCStats.Reset(TrackedRPCs.Num());

	for (const TPair<FObjectKey, FRPCTrack>& Pair : TrackedRPCs)
	{
		const FRPCTrack& Track = Pair.Value;

		FNetLensRPCStat& Stat = RPCStats.AddDefaulted_GetRef();
		Stat.FunctionName = Track.FunctionName;
		Stat.ClassName = Track.ClassName;
		Stat.bReliable = Track.bReliable;
		Stat.bMulticast = Track.bMulticast;
		Stat.BytesPerSecond = static_cast<float>(Track.Window.GetBytesPerSecond());
		Stat.CallsPerSecond = static_cast<float>(Track.Window.GetEventsPerSecond());
		Stat.BytesPerCall = Track.LastCallBits / 8.0f;
		Stat.CallsInWindow = static_cast<int32>(Track.Window.GetTotalEvents());
		Stat.TotalBytes = static_cast<float>(Track.Window.GetTotalBytes());
	}

	//~ Totals ---------------------------------------------------------------------------------------------

	UWorld* World = GetWorld();
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;

	FNetLensTotals NewTotals;
	NewTotals.bFrozen = bFrozen;
	NewTotals.WindowSeconds = static_cast<float>(WindowSeconds);
	NewTotals.TrackedActors = TrackedActors.Num();
	NewTotals.SeenActors = LiveSeenActors;
	NewTotals.ActorChannelCount = LiveChannelCount;

	for (const FNetLensActorStat& Stat : ActorStats)
	{
		NewTotals.AttributedActorBytesPerSecond += Stat.BytesPerSecond;
	}
	for (const FNetLensRPCStat& Stat : RPCStats)
	{
		NewTotals.AttributedRPCBytesPerSecond += Stat.BytesPerSecond;
	}

	if (Driver)
	{
		NewTotals.bHasNetDriver = true;
		NewTotals.bIsServer = Driver->IsServer();
		NewTotals.DriverOutBytesPerSecond = static_cast<float>(Driver->OutBytesPerSecond);
		NewTotals.DriverInBytesPerSecond = static_cast<float>(Driver->InBytesPerSecond);
		NewTotals.OutPacketsPerSecond = static_cast<int32>(Driver->OutPackets);
		NewTotals.InPacketsPerSecond = static_cast<int32>(Driver->InPackets);
		NewTotals.OutPacketsLostPercent = static_cast<int32>(Driver->OutPacketsLost);

		TArray<UNetConnection*> Connections;
		GatherConnections(Driver, Connections);
		NewTotals.ConnectionCount = Connections.Num();

		float LagSum = 0.0f;
		for (const UNetConnection* Connection : Connections)
		{
			LagSum += Connection->AvgLag;
		}

		if (Connections.Num() > 0)
		{
			NewTotals.AveragePingMs = (LagSum / Connections.Num()) * 1000.0f;
		}
	}

	Totals = NewTotals;
}

void UNetLensSubsystem::RetireStaleEntries(double Now)
{
	// An actor is retired when it has had no open channel for a whole window and has nothing left in that
	// window. Both conditions matter: dropping it the moment its channel closes would erase the evidence of
	// what it just cost, which is usually the thing you were about to read.
	const double Cutoff = Now - WindowSeconds;

	for (auto It = TrackedActors.CreateIterator(); It; ++It)
	{
		FTrackedActor& Tracked = It.Value();

		const bool bGone = !Tracked.Actor.IsValid();
		const bool bIdle = Tracked.RelevantConnections == 0 && Tracked.LastSeenTime < Cutoff;

		if (bGone || (bIdle && Tracked.Window.IsEmpty()))
		{
			It.RemoveCurrent();
		}
	}

	// Channel keys outlive their channels. Resolving them at the sample rate is cheap and keeps the map from
	// growing for the whole session in a game that opens and closes channels constantly.
	for (auto It = OverflowChannelTimes.CreateIterator(); It; ++It)
	{
		if (It.Key().ResolveObjectPtr() == nullptr)
		{
			It.RemoveCurrent();
		}
	}

	if (TrackedRPCs.Num() > MaxTrackedRPCs)
	{
		// Drop the quietest first. A function that has not been called inside the window is not information.
		TArray<FObjectKey> Keys;
		TrackedRPCs.GetKeys(Keys);

		Keys.Sort([this](const FObjectKey& A, const FObjectKey& B)
		{
			const FRPCTrack* TrackA = TrackedRPCs.Find(A);
			const FRPCTrack* TrackB = TrackedRPCs.Find(B);
			const double RateA = TrackA ? TrackA->Window.GetBytesPerSecond() : 0.0;
			const double RateB = TrackB ? TrackB->Window.GetBytesPerSecond() : 0.0;
			return RateA < RateB;
		});

		const int32 ToRemove = TrackedRPCs.Num() - MaxTrackedRPCs;
		for (int32 Index = 0; Index < ToRemove && Index < Keys.Num(); ++Index)
		{
			TrackedRPCs.Remove(Keys[Index]);
		}
	}
}

bool UNetLensSubsystem::FindActorStat(const AActor* Actor, FNetLensActorStat& OutStat) const
{
	if (!Actor)
	{
		return false;
	}

	for (const FNetLensActorStat& Stat : ActorStats)
	{
		if (Stat.Actor.Get() == Actor)
		{
			OutStat = Stat;
			return true;
		}
	}

	return false;
}

//~ RPCs ---------------------------------------------------------------------------------------------------

void UNetLensSubsystem::EnsureRPCHook(UNetDriver* Driver)
{
#if !UE_BUILD_SHIPPING
	if (!bTrackRPCs || !Driver)
	{
		return;
	}

	if (bHasRPCHook && HookedDriver.Get() == Driver && Driver->SendRPCDel.IsBoundToObject(this))
	{
		return;
	}

	// Either we have never taken the hook, or the driver was replaced under us - a seamless travel does
	// exactly that - or somebody else has taken it since. In all three cases the safe move is to let go of
	// whatever we were holding and take it again from scratch.
	ReleaseRPCHook();

	PreviousSendRPC = Driver->SendRPCDel;
	Driver->SendRPCDel.BindUObject(this, &UNetLensSubsystem::HandleSendRPC);

	HookedDriver = Driver;
	bHasRPCHook = true;

	if (PreviousSendRPC.IsBound())
	{
		UE_LOG(LogNetLens, Log,
			TEXT("NetLens took the net driver's RPC hook and will forward to the delegate that had it."));
	}
#endif
}

void UNetLensSubsystem::ReleaseRPCHook()
{
#if !UE_BUILD_SHIPPING
	if (bHasRPCHook)
	{
		if (UNetDriver* Driver = HookedDriver.Get())
		{
			// Only give it back if it is still ours. If somebody else has taken it since, overwriting them
			// with a stale delegate would be worse than leaving it alone.
			if (Driver->SendRPCDel.IsBoundToObject(this))
			{
				Driver->SendRPCDel = PreviousSendRPC;
			}
		}

		PreviousSendRPC.Unbind();
		bHasRPCHook = false;
	}
#endif

	HookedDriver.Reset();
}

#if !UE_BUILD_SHIPPING
void UNetLensSubsystem::HandleSendRPC(AActor* Actor, UFunction* Function, void* Parms, FOutParmRec* OutParms,
	FFrame* Stack, UObject* SubObject, bool& bBlockSendRPC)
{
	if (!bFrozen && bTrackRPCs)
	{
		RecordRPC(Actor, Function, Parms, FMath::Max(1, Totals.ConnectionCount), GetNetLensTime());
	}

	// Forward, and do not touch bBlockSendRPC. NetLens is a lens: whatever the call was going to do before
	// it was measured is what it does after.
	if (PreviousSendRPC.IsBound())
	{
		PreviousSendRPC.Execute(Actor, Function, Parms, OutParms, Stack, SubObject, bBlockSendRPC);
	}
}
#endif

void UNetLensSubsystem::RecordRPC(const AActor* Actor, const UFunction* Function, const void* Parms,
	int32 ConnectionCount, double Now)
{
	if (!Function)
	{
		return;
	}

	const FObjectKey Key(Function);

	FRPCTrack* Track = TrackedRPCs.Find(Key);
	if (!Track)
	{
		Track = &TrackedRPCs.Add(Key);
		Track->Window.Configure(WindowSeconds, BucketsPerWindow);
		Track->FunctionName = Function->GetFName();

		const UStruct* Owner = Function->GetOwnerStruct();
		Track->ClassName = Owner ? Owner->GetFName()
			: (Actor ? Actor->GetClass()->GetFName() : NAME_None);

		Track->bReliable = Function->HasAnyFunctionFlags(FUNC_NetReliable);
		Track->bMulticast = Function->HasAnyFunctionFlags(FUNC_NetMulticast);
	}

	const int32 Bits = UNetLensStatics::EstimateFunctionParameterBits(Function, Parms) + RPCOverheadBytes * 8;
	Track->LastCallBits = Bits;

	// A multicast is paid for once per connection. A server or client RPC goes to one place however many
	// connections exist, and charging it per connection would make a single Server_Fire look like a crowd.
	const double Multiplier = Track->bMulticast ? FMath::Max(1, ConnectionCount) : 1;

	Track->Window.Add(Now, (Bits / 8.0) * Multiplier, Multiplier);
}

void UNetLensSubsystem::NoteRPC(AActor* Actor, FName FunctionName)
{
	if (!bTrackRPCs || bFrozen || !Actor || FunctionName.IsNone())
	{
		return;
	}

	const UFunction* Function = Actor->FindFunction(FunctionName);
	if (!Function)
	{
		UE_LOG(LogNetLens, Warning, TEXT("NoteRPC: %s has no function called %s."),
			*Actor->GetName(), *FunctionName.ToString());
		return;
	}

	RecordRPC(Actor, Function, nullptr, FMath::Max(1, Totals.ConnectionCount), GetNetLensTime());
}

//~ Export -------------------------------------------------------------------------------------------------

bool UNetLensSubsystem::ExportCsv(const FString& Path, FString& OutPath)
{
	TArray<FNetLensActorStat> Ranked;
	UNetLensStatics::RankActors(ActorStats, 0, SortBy, Ranked);

	TArray<FNetLensRPCStat> RankedRPCs;
	UNetLensStatics::RankRPCs(RPCStats, 0, RankedRPCs);

	const FString Csv = UNetLensStatics::BuildCsv(Ranked, RankedRPCs, Totals, bExportPropertyRows);

	FString Target = Path;

	if (Target.IsEmpty())
	{
		Target = FString::Printf(TEXT("NetLens-%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	}

	if (FPaths::IsRelative(Target))
	{
		// Relative to the project's Saved folder, never to the working directory. A tool that writes files
		// next to whichever executable happened to launch it is a tool whose exports get lost.
		Target = FPaths::Combine(FPaths::ProjectSavedDir(), CsvSubdirectory, Target);
	}

	Target = FPaths::ConvertRelativePathToFull(Target);

	const FString Directory = FPaths::GetPath(Target);
	if (!Directory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
	}

	// Without a byte order mark, so SplitCsvLine and every other plain parser sees the first column and not
	// three invisible bytes glued to the front of it.
	if (!FFileHelper::SaveStringToFile(Csv, *Target, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogNetLens, Warning, TEXT("NetLens could not write %s."), *Target);
		return false;
	}

	OutPath = Target;

	UE_LOG(LogNetLens, Display, TEXT("NetLens exported %d actor rows and %d RPC rows to %s"),
		Ranked.Num(), RankedRPCs.Num(), *Target);

	return true;
}

void UNetLensSubsystem::LogSummary() const
{
	UE_LOG(LogNetLens, Display,
		TEXT("NetLens: out %s, in %s, attributed %s (actors) + %s (rpc), %d/%d actors tracked, %d channels%s"),
		*UNetLensStatics::FormatBytesPerSecond(Totals.DriverOutBytesPerSecond),
		*UNetLensStatics::FormatBytesPerSecond(Totals.DriverInBytesPerSecond),
		*UNetLensStatics::FormatBytesPerSecond(Totals.AttributedActorBytesPerSecond),
		*UNetLensStatics::FormatBytesPerSecond(Totals.AttributedRPCBytesPerSecond),
		Totals.TrackedActors, Totals.SeenActors, Totals.ActorChannelCount,
		Totals.bFrozen ? TEXT(" [frozen]") : TEXT(""));

	TArray<FNetLensActorStat> Ranked;
	UNetLensStatics::RankActors(ActorStats, TopN, SortBy, Ranked);

	for (int32 Index = 0; Index < Ranked.Num(); ++Index)
	{
		const FNetLensActorStat& Stat = Ranked[Index];
		UE_LOG(LogNetLens, Display, TEXT("  %2d. %-32s %-24s %10s  %5.1f upd/s  x%d"),
			Index + 1, *Stat.ActorName.ToString(), *Stat.ClassName.ToString(),
			*UNetLensStatics::FormatBytesPerSecond(Stat.BytesPerSecond),
			Stat.UpdatesPerSecond, Stat.RelevantConnections);
	}

	TArray<FNetLensRPCStat> RankedRPCs;
	UNetLensStatics::RankRPCs(RPCStats, TopRPCRows, RankedRPCs);

	for (const FNetLensRPCStat& Stat : RankedRPCs)
	{
		UE_LOG(LogNetLens, Display, TEXT("  RPC %-32s %-24s %10s  %5.1f calls/s  %s"),
			*Stat.FunctionName.ToString(), *Stat.ClassName.ToString(),
			*UNetLensStatics::FormatBytesPerSecond(Stat.BytesPerSecond),
			Stat.CallsPerSecond, Stat.bReliable ? TEXT("reliable") : TEXT("unreliable"));
	}
}

//~ Drawing ------------------------------------------------------------------------------------------------

void UNetLensSubsystem::RefreshHudDelegate()
{
	if (bAutoDrawOverlayOnAnyHUD && !HudPostRenderHandle.IsValid())
	{
		HudPostRenderHandle = AHUD::OnHUDPostRender.AddUObject(this, &UNetLensSubsystem::OnAnyHUDPostRender);
	}
	else if (!bAutoDrawOverlayOnAnyHUD && HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}
}

void UNetLensSubsystem::OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas)
{
	if (!bShowOverlay || !HUD || !Canvas)
	{
		return;
	}

	// The delegate is global; the subsystem is per world. Without this, a PIE session with two windows draws
	// the server's numbers on the client's screen and nobody can tell which is which.
	if (HUD->GetWorld() != GetWorld())
	{
		return;
	}

	DrawOverlay(Canvas, OverlayOrigin, OverlayWidth);
}

bool UNetLensSubsystem::HasDrawnOverlayThisFrame() const
{
	return LastOverlayDrawFrame == GFrameCounter;
}

int32 UNetLensSubsystem::GetOverlayLineCount() const
{
	return LastOverlayLineCount;
}

void UNetLensSubsystem::DrawOverlay(UCanvas* Canvas, const FVector2D& Origin, float Width) const
{
	using namespace NetLensPrivate;

	if (!Canvas)
	{
		return;
	}

	// Both draw routes call this. Whichever gets there first this frame wins and the other does nothing,
	// which is what makes it safe for a project to use ANetLensHUD *and* leave the any-HUD setting on.
	if (HasDrawnOverlayThisFrame())
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	LastOverlayDrawFrame = GFrameCounter;

	struct FOverlayLine
	{
		FString Text;
		FLinearColor Color;

		/** Share of the attributed traffic, for the bar behind the row. Negative means no bar. */
		float BarAlpha = -1.0f;
	};

	TArray<FOverlayLine> Lines;
	Lines.Reserve(32);

	auto AddLine = [&Lines](FString&& Text, const FLinearColor& Color, float BarAlpha = -1.0f)
	{
		FOverlayLine& Line = Lines.AddDefaulted_GetRef();
		Line.Text = MoveTemp(Text);
		Line.Color = Color;
		Line.BarAlpha = BarAlpha;
	};

	//~ Header

	if (!Totals.bHasNetDriver)
	{
		// The single most common reason for an empty panel, said out loud. A standalone game has no net
		// driver, replicates nothing, and there is nothing wrong with NetLens.
		AddLine(TEXT("NetLens - no net driver in this world (standalone game: nothing is replicating)"),
			WarnColor);
	}
	else
	{
		AddLine(FString::Printf(TEXT("NetLens  |  %s  |  %d connection%s  |  window %.0fs%s"),
			Totals.bIsServer ? TEXT("SERVER") : TEXT("CLIENT"),
			Totals.ConnectionCount, Totals.ConnectionCount == 1 ? TEXT("") : TEXT("s"),
			Totals.WindowSeconds,
			Totals.bFrozen ? TEXT("  |  [FROZEN]") : TEXT("")),
			Totals.bFrozen ? WarnColor : HeadingColor);

		AddLine(FString::Printf(TEXT("OUT %s  %d pkt/s      IN %s  %d pkt/s      loss %d%%   ping %.0f ms"),
			*UNetLensStatics::FormatBytesPerSecond(Totals.DriverOutBytesPerSecond),
			Totals.OutPacketsPerSecond,
			*UNetLensStatics::FormatBytesPerSecond(Totals.DriverInBytesPerSecond),
			Totals.InPacketsPerSecond,
			Totals.OutPacketsLostPercent,
			Totals.AveragePingMs),
			BodyColor);

		AddLine(FString::Printf(
			TEXT("attributed %s actors + %s rpc   |   tracked %d/%d of %d   |   %d channels"),
			*UNetLensStatics::FormatBytesPerSecond(Totals.AttributedActorBytesPerSecond),
			*UNetLensStatics::FormatBytesPerSecond(Totals.AttributedRPCBytesPerSecond),
			Totals.TrackedActors, MaxTrackedActors, Totals.SeenActors,
			Totals.ActorChannelCount),
			DimColor);
	}

	//~ Actors

	TArray<FNetLensActorStat> Ranked;
	UNetLensStatics::RankActors(ActorStats, TopN, SortBy, Ranked);

	const double Denominator = FMath::Max<double>(Totals.AttributedActorBytesPerSecond, 1.0);

	AddLine(FString::Printf(TEXT("%s %s %10s %10s %8s %5s"),
		TEXT("  #"), *Fit(TEXT("ACTOR"), 30), TEXT("BYTES/s"), TEXT("PEAK"), TEXT("UPD/s"), TEXT("CONN")),
		HeadingColor);

	for (int32 Index = 0; Index < Ranked.Num(); ++Index)
	{
		const FNetLensActorStat& Stat = Ranked[Index];
		const double Share = Stat.BytesPerSecond / Denominator;

		const FLinearColor& Color = Stat.bHighlighted ? HighlightColor
			: (Stat.bIsAggregate ? DimColor : ShareColor(Share));

		AddLine(FString::Printf(TEXT("%3d %s %10s %10s %8.1f %5d"),
			Index + 1,
			*Fit(Stat.ActorName.ToString(), 30),
			*UNetLensStatics::FormatBytesPerSecond(Stat.BytesPerSecond),
			*UNetLensStatics::FormatBytesPerSecond(Stat.PeakBytesPerSecond),
			Stat.UpdatesPerSecond,
			Stat.RelevantConnections),
			Color, static_cast<float>(FMath::Clamp(Share, 0.0, 1.0)));

		if (Index >= PropertyBreakdownRows)
		{
			continue;
		}

		const int32 PropertyCount = FMath::Min(Stat.Properties.Num(), PropertiesPerRow);
		for (int32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
		{
			const FNetLensPropertyStat& Property = Stat.Properties[PropertyIndex];

			AddLine(FString::Printf(TEXT("      %s %10s %10s %8.1f"),
				*Fit(Property.PropertyName.ToString() + TEXT(" (")
					+ Property.OwnerClassName.ToString() + TEXT(")"), 30),
				*UNetLensStatics::FormatBytesPerSecond(Property.BytesPerSecond),
				*UNetLensStatics::FormatBytes(Property.BytesPerChange),
				Property.ChangesPerSecond),
				DimColor);
		}
	}

	if (Ranked.Num() == 0 && Totals.bHasNetDriver)
	{
		AddLine(TEXT("    nothing has replicated inside the window yet"), DimColor);
	}

	//~ RPCs

	if (TopRPCRows > 0)
	{
		TArray<FNetLensRPCStat> RankedRPCs;
		UNetLensStatics::RankRPCs(RPCStats, TopRPCRows, RankedRPCs);

		AddLine(FString::Printf(TEXT("%s %s %10s %10s %8s %5s"),
			TEXT("RPC"), *Fit(TEXT("FUNCTION"), 30), TEXT("BYTES/s"), TEXT("PER CALL"), TEXT("CALL/s"),
			TEXT("REL")),
			HeadingColor);

		if (RankedRPCs.Num() == 0)
		{
#if UE_BUILD_SHIPPING
			AddLine(TEXT("    no RPCs recorded - in Shipping, call NoteRPC to record them"), DimColor);
#else
			AddLine(TEXT("    no RPCs inside the window"), DimColor);
#endif
		}

		for (const FNetLensRPCStat& Stat : RankedRPCs)
		{
			AddLine(FString::Printf(TEXT("    %s %10s %10s %8.1f %5s"),
				*Fit(Stat.FunctionName.ToString(), 30),
				*UNetLensStatics::FormatBytesPerSecond(Stat.BytesPerSecond),
				*UNetLensStatics::FormatBytes(Stat.BytesPerCall),
				Stat.CallsPerSecond,
				Stat.bReliable ? TEXT("yes") : TEXT("no")),
				Stat.bReliable ? WarnColor : BodyColor);
		}
	}

	//~ Draw

	LastOverlayLineCount = Lines.Num();

	const float BoxHeight = Lines.Num() * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);

	for (const FOverlayLine& Line : Lines)
	{
		if (Line.BarAlpha > 0.0f)
		{
			// The bar sits behind the text rather than beside it, so a row stays readable at any share and
			// the eye still finds the long ones without reading a single number.
			DrawFilledRect(Canvas,
				FVector2D(Origin.X - BoxPadding * 0.5f, LineY - 1.0f),
				FVector2D((Width - BoxPadding) * Line.BarAlpha, LineHeight - 1.0f),
				BarColor);
		}

		FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), FStringView(Line.Text), Font, Line.Color);
		Canvas->DrawItem(Item);

		LineY += LineHeight;
	}
}

//~ Console commands ---------------------------------------------------------------------------------------

namespace NetLensPrivate
{
	static FAutoConsoleCommandWithWorldAndArgs CmdShow(
		TEXT("NetLens.Show"),
		TEXT("NetLens.Show [0|1] - draw the replication panel. No argument toggles it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UNetLensSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogNetLens, Warning, TEXT("NetLens.Show: no NetLens subsystem in this world."));
				return;
			}

			const bool bShow = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : !Subsystem->IsShowingOverlay();
			Subsystem->SetShowOverlay(bShow);

			UE_LOG(LogNetLens, Display, TEXT("NetLens.Show: %s"), bShow ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdFreeze(
		TEXT("NetLens.Freeze"),
		TEXT("NetLens.Freeze [0|1] - hold the table still without pausing the game. No argument toggles it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UNetLensSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogNetLens, Warning, TEXT("NetLens.Freeze: no NetLens subsystem in this world."));
				return;
			}

			const bool bFreeze = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : !Subsystem->IsFrozen();
			if (bFreeze)
			{
				Subsystem->Freeze();
			}
			else
			{
				Subsystem->Resume();
			}

			UE_LOG(LogNetLens, Display, TEXT("NetLens.Freeze: %s"), bFreeze ? TEXT("frozen") : TEXT("running"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdExport(
		TEXT("NetLens.Export"),
		TEXT("NetLens.Export [path] - write the table to CSV. No argument writes a timestamped file "
			"under Saved."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UNetLensSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogNetLens, Warning, TEXT("NetLens.Export: no NetLens subsystem in this world."));
				return;
			}

			FString OutPath;
			if (Subsystem->ExportCsv(Args.Num() > 0 ? Args[0] : FString(), OutPath))
			{
				UE_LOG(LogNetLens, Display, TEXT("NetLens.Export: %s"), *OutPath);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdTopN(
		TEXT("NetLens.TopN"),
		TEXT("NetLens.TopN <n> - how many actor rows the panel prints."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UNetLensSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogNetLens, Warning, TEXT("NetLens.TopN: no NetLens subsystem in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogNetLens, Display, TEXT("NetLens.TopN is %d."), Subsystem->GetTopN());
				return;
			}

			Subsystem->SetTopN(FCString::Atoi(*Args[0]));
			UE_LOG(LogNetLens, Display, TEXT("NetLens.TopN: %d"), Subsystem->GetTopN());
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdReset(
		TEXT("NetLens.Reset"),
		TEXT("NetLens.Reset - throw away every measurement and start from an empty window."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			UNetLensSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogNetLens, Warning, TEXT("NetLens.Reset: no NetLens subsystem in this world."));
				return;
			}

			Subsystem->ResetMeasurements();
			UE_LOG(LogNetLens, Display, TEXT("NetLens.Reset: cleared."));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStats(
		TEXT("NetLens.Stats"),
		TEXT("NetLens.Stats - print the current table to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			const UNetLensSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogNetLens, Warning, TEXT("NetLens.Stats: no NetLens subsystem in this world."));
				return;
			}

			Subsystem->LogSummary();
		}));
}
