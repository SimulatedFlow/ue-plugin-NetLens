// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NetLensTypes.h"
#include "NetLensComponent.generated.h"

/**
 * Hang this on an actor to follow that one actor exactly, whatever else is going on.
 *
 * The subsystem measures the busiest two hundred actors, which is the right answer when the question is
 * "what is eating my bandwidth". It is the wrong answer when the question is "what is *this* actor costing",
 * because the actor you are suspicious of is very often not in the top two hundred - it is the one that goes
 * expensive for half a second when the player does something, and by the time you look it is gone again.
 *
 * A component makes that actor exempt from the ceiling: it is followed for as long as the component lives,
 * it is never the one evicted when the ceiling is reached, and its row on the panel is drawn in a colour
 * that says so. It costs one shadow buffer, which is the same thing any tracked actor costs.
 *
 * Nothing here replicates and nothing here changes how the actor replicates. The component is an
 * instruction to the measuring, not a change to the thing being measured.
 */
UCLASS(ClassGroup = (NetLens), meta = (BlueprintSpawnableComponent, DisplayName = "NetLens Probe"))
class NETLENS_API UNetLensComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNetLensComponent();

	//~ UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Whether NetLens is currently following this component's actor.
	 *
	 * Deliberately **not** called IsRegistered. UActorComponent already has an IsRegistered(), it means
	 * something completely different - whether the component has been registered with the world - and a
	 * non-virtual method with the same name on a subclass hides it. Every call site that meant the engine's
	 * one would silently get this one instead, and the compiler would not say a word.
	 */
	UFUNCTION(BlueprintPure, Category = "NetLens")
	bool IsTrackedByNetLens() const;

	/** This actor's row, with its property breakdown. False when nothing has been measured for it yet. */
	UFUNCTION(BlueprintCallable, Category = "NetLens")
	bool GetNetLensStat(FNetLensActorStat& OutStat) const;

	/** What this actor is costing, in bytes per second, averaged over the measurement window. */
	UFUNCTION(BlueprintPure, Category = "NetLens")
	float GetBytesPerSecond() const;

	/** How often this actor really replicated, per second, averaged over the window. */
	UFUNCTION(BlueprintPure, Category = "NetLens")
	float GetUpdatesPerSecond() const;

	/** The busiest half-second this actor has had inside the window, as a rate. */
	UFUNCTION(BlueprintPure, Category = "NetLens")
	float GetPeakBytesPerSecond() const;

	/** "12.4 KB/s", ready to put on a widget or a debug string. */
	UFUNCTION(BlueprintPure, Category = "NetLens")
	FString GetCostText() const;

	/**
	 * Keep this actor tracked even when the ceiling has been reached.
	 *
	 * On, because it is the only reason to place the component. Turning it off leaves the component as a
	 * convenient way to read an actor's row from its own Blueprint without also pinning it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NetLens")
	bool bAlwaysTrack = true;

	/**
	 * A name for this actor on the panel, when its instance name is not what you would recognise it by.
	 *
	 * Empty uses the actor's own name. Worth setting on a spawned actor, whose name is a number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NetLens")
	FName DisplayLabel;
};
