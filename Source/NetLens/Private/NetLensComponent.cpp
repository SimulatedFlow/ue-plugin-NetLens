// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "NetLensComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NetLensStatics.h"
#include "NetLensSubsystem.h"

UNetLensComponent::UNetLensComponent()
{
	// Nothing here ticks. The measuring is the subsystem's job; this component is a flag and four accessors,
	// and a diagnostic component that added a tick to every actor it was placed on would be a poor joke.
	PrimaryComponentTick.bCanEverTick = false;

	// Never replicated. It is an instruction to the local measuring tool and means nothing on another
	// machine - and a component that replicated itself would show up in its own table, which is a special
	// kind of unhelpful.
	SetIsReplicatedByDefault(false);
}

void UNetLensComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bAlwaysTrack)
	{
		return;
	}

	if (UNetLensSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UNetLensSubsystem>() : nullptr)
	{
		Subsystem->RegisterTrackedComponent(this);
	}
}

void UNetLensComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNetLensSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UNetLensSubsystem>() : nullptr)
	{
		Subsystem->UnregisterTrackedComponent(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool UNetLensComponent::IsTrackedByNetLens() const
{
	FNetLensActorStat Stat;
	return GetNetLensStat(Stat);
}

bool UNetLensComponent::GetNetLensStat(FNetLensActorStat& OutStat) const
{
	OutStat = FNetLensActorStat();

	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return false;
	}

	const UNetLensSubsystem* Subsystem = World->GetSubsystem<UNetLensSubsystem>();
	if (!Subsystem || !Subsystem->FindActorStat(Owner, OutStat))
	{
		return false;
	}

	if (!DisplayLabel.IsNone())
	{
		OutStat.ActorName = DisplayLabel;
	}

	return true;
}

float UNetLensComponent::GetBytesPerSecond() const
{
	FNetLensActorStat Stat;
	return GetNetLensStat(Stat) ? Stat.BytesPerSecond : 0.0f;
}

float UNetLensComponent::GetUpdatesPerSecond() const
{
	FNetLensActorStat Stat;
	return GetNetLensStat(Stat) ? Stat.UpdatesPerSecond : 0.0f;
}

float UNetLensComponent::GetPeakBytesPerSecond() const
{
	FNetLensActorStat Stat;
	return GetNetLensStat(Stat) ? Stat.PeakBytesPerSecond : 0.0f;
}

FString UNetLensComponent::GetCostText() const
{
	FNetLensActorStat Stat;
	if (!GetNetLensStat(Stat))
	{
		// Not "0 KB/s". Nothing measured and nothing costing are different states, and a debug string that
		// conflated them would have you looking for a bug in the actor instead of switching NetLens on.
		return TEXT("not measured");
	}

	return FString::Printf(TEXT("%s  (%.1f upd/s)"),
		*UNetLensStatics::FormatBytesPerSecond(Stat.BytesPerSecond), Stat.UpdatesPerSecond);
}
