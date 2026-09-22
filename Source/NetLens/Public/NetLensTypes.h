// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NetLensTypes.generated.h"

class AActor;

/**
 * What the table is sorted by.
 *
 * Bytes is the answer to "what is eating my bandwidth" and is the default. Updates answers a different and
 * often more useful question - "what is being *considered* far too often" - because an actor replicating
 * sixty times a second to send four bytes each time is a NetUpdateFrequency problem, not a payload problem,
 * and the two are fixed in completely different places.
 */
UENUM(BlueprintType)
enum class ENetLensSort : uint8
{
	/** Bytes per second, descending. The default and the one the header bar is scaled against. */
	Bytes			UMETA(DisplayName = "Bytes Per Second"),

	/** Replication updates per second, descending. Finds actors that are talking too often, not too loudly. */
	Updates			UMETA(DisplayName = "Updates Per Second"),

	/** Alphabetical by name. Not a diagnosis - a way to find the same row twice while you change something. */
	Name			UMETA(DisplayName = "Name"),
};

/**
 * One replicated property, and what it cost over the measurement window.
 *
 * A property only ever appears here if it actually changed while its actor was replicating. That is the
 * difference between this and a list of an actor's replicated properties: the list is a fact about the
 * class, this is a fact about the last ten seconds of your game.
 */
USTRUCT(BlueprintType)
struct NETLENS_API FNetLensPropertyStat
{
	GENERATED_BODY()

	/** The property, as it is spelled in the header or in the Blueprint's variable list. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	FName PropertyName;

	/**
	 * The class the property is declared on.
	 *
	 * Worth showing, because the most expensive property on a Character is very often not on the Character:
	 * it is ReplicatedMovement on AActor or a component property on CharacterMovement, and looking for it in
	 * the Blueprint is a waste of an afternoon.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	FName OwnerClassName;

	/** Averaged over the measurement window. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float BytesPerSecond = 0.0f;

	/** How often the value actually changed, per second, averaged over the window. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float ChangesPerSecond = 0.0f;

	/** What one change of this property is priced at, in bytes. See UNetLensStatics::EstimateNetPropertyBits. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float BytesPerChange = 0.0f;

	/** Total bytes attributed to this property inside the window. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float TotalBytes = 0.0f;
};

/**
 * One actor's replication cost.
 *
 * Three of these numbers are measured and one is modelled, and it is worth being exact about which is which
 * because the whole value of the tool rests on it:
 *
 *   UpdatesPerSecond is measured. It is read off the engine's own UActorChannel::LastUpdateTime, once per
 *   connection, so it is the number of times the actor genuinely replicated - not the number of times it
 *   was allowed to.
 *
 *   The property list is measured. It comes from comparing a shadow copy of the actor's replicated state
 *   against the live one, which is how the replication system itself decides what to send.
 *
 *   RelevantConnections is measured. It is the number of open actor channels for this actor.
 *
 *   BytesPerSecond is modelled from those three, by pricing each changed property from its type using the
 *   engine's serialisation rules. It is not a packet capture. The overlay prints the attributed total next
 *   to the driver's real total so the size of the gap is never hidden from you.
 */
USTRUCT(BlueprintType)
struct NETLENS_API FNetLensActorStat
{
	GENERATED_BODY()

	/**
	 * The actor, while it is alive. Null on an aggregate row and on a row read back from a CSV.
	 *
	 * Weak, and not exposed to Blueprint directly. Weak because a table of stats must never be the reason a
	 * destroyed actor is still in memory; not exposed because Blueprint has no weak pointer pin, so the
	 * accessor is UNetLensStatics::GetStatActor, which resolves it and hands back a plain reference.
	 */
	UPROPERTY()
	TWeakObjectPtr<AActor> Actor;

	/** The instance name, which is what you type into the outliner's search box to find it. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	FName ActorName;

	/** The class, which is what you actually have to open to fix anything. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	FName ClassName;

	/** Averaged over the measurement window. The column the table is sorted by. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float BytesPerSecond = 0.0f;

	/**
	 * The most expensive single bucket in the window, expressed as a rate.
	 *
	 * The reason the window exists at all is to stop the table flickering, and the reason this number exists
	 * is that a window which stops it flickering also hides the one frame where an actor sent forty kilobytes
	 * at once. Averaging is how you make a number readable; the peak is how you avoid lying with it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float PeakBytesPerSecond = 0.0f;

	/** Measured replication updates per second, summed across connections. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float UpdatesPerSecond = 0.0f;

	/** What the actor asked for, from GetNetUpdateFrequency. Compare against UpdatesPerSecond. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float NetUpdateFrequency = 0.0f;

	/** Open actor channels for this actor - that is, connections it is currently relevant to. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 RelevantConnections = 0;

	/** Total bytes attributed to this actor inside the window. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float TotalBytes = 0.0f;

	/** True when a UNetLensComponent asked for this actor to be followed and highlighted. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	bool bHighlighted = false;

	/**
	 * True on the single row that stands for everything the ceiling left out.
	 *
	 * There is at most one of these and it is always last. It is the visible half of the promise that NetLens
	 * cannot grow without limit: the rows you are not looking at do not disappear, they collapse.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	bool bIsAggregate = false;

	/** How many actors an aggregate row stands for. Zero on a normal row. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 AggregatedActors = 0;

	/** The properties that actually moved, most expensive first. Empty outside the breakdown depth. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	TArray<FNetLensPropertyStat> Properties;
};

/**
 * One remote procedure call, aggregated over the window by function.
 *
 * Rows are per function per class, not per call and not per instance. Fifty turrets firing the same
 * multicast is one row with a large CallsPerSecond, which is the shape of the answer you want: the fix is in
 * the function, not in one of the fifty turrets.
 */
USTRUCT(BlueprintType)
struct NETLENS_API FNetLensRPCStat
{
	GENERATED_BODY()

	/** The UFunction's name. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	FName FunctionName;

	/** The class the function is declared on. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	FName ClassName;

	/** Averaged over the measurement window. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float BytesPerSecond = 0.0f;

	/** Calls per second, averaged over the window. Multicasts count once per receiving connection. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float CallsPerSecond = 0.0f;

	/**
	 * Reliable calls are the expensive ones, and not because of their size.
	 *
	 * A reliable RPC occupies a slot in the channel's outgoing reliable queue until it is acknowledged, and a
	 * queue that fills stalls everything behind it. This is the column to look at when the bytes look fine
	 * and the game still feels like treacle.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	bool bReliable = false;

	/** Multicast, so its cost multiplies by the number of connections rather than being paid once. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	bool bMulticast = false;

	/** Priced parameter payload plus the RPC header, in bytes, for one call. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float BytesPerCall = 0.0f;

	/** Calls seen inside the window. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 CallsInWindow = 0;

	/** Total bytes attributed to this function inside the window. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float TotalBytes = 0.0f;
};

/**
 * The header line: what the net driver itself reports, next to what NetLens managed to attribute.
 *
 * Printing both is the honest thing and also the useful thing. If the driver says 140 KB/s and NetLens has
 * accounted for 130, the ten that are missing are handshakes, acknowledgements, NetGUID traffic and the
 * things NetLens does not model - and you know to stop looking for a missing actor. If it has accounted for
 * 40, something in your game is sending data by a route this tool cannot see, and that is worth knowing
 * before you spend a day tuning the wrong actor.
 */
USTRUCT(BlueprintType)
struct NETLENS_API FNetLensTotals
{
	GENERATED_BODY()

	/** The driver's own outgoing rate. Measured by the engine, not by us. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float DriverOutBytesPerSecond = 0.0f;

	/** The driver's own incoming rate. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float DriverInBytesPerSecond = 0.0f;

	/** Packets per second out, from the driver. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 OutPacketsPerSecond = 0;

	/** Packets per second in, from the driver. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 InPacketsPerSecond = 0;

	/** Percentage of outgoing packets the driver believes were lost. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 OutPacketsLostPercent = 0;

	/** Sum of every actor row NetLens produced, including the aggregate. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float AttributedActorBytesPerSecond = 0.0f;

	/** Sum of every RPC row NetLens produced. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float AttributedRPCBytesPerSecond = 0.0f;

	/** Open connections. One on a listen server with a single client; zero in a standalone game. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 ConnectionCount = 0;

	/** Open actor channels, summed across connections. The real size of the replication working set. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 ActorChannelCount = 0;

	/** How many actors NetLens is following in detail, against MaxTrackedActors. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 TrackedActors = 0;

	/** How many actors were seen replicating at all, tracked or not. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	int32 SeenActors = 0;

	/** Average round trip of the open connections, in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float AveragePingMs = 0.0f;

	/** The measurement is frozen; the numbers are the ones from the moment you froze it. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	bool bFrozen = false;

	/** This machine is the authority for the driver being measured. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	bool bIsServer = false;

	/** There is a net driver at all. False in a standalone game, and the reason the panel would be empty. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	bool bHasNetDriver = false;

	/** Seconds of history the rates are averaged over. */
	UPROPERTY(BlueprintReadOnly, Category = "NetLens")
	float WindowSeconds = 0.0f;
};

/**
 * A ring of fixed-length buckets over the last N seconds.
 *
 * Not a USTRUCT and not reflected, deliberately. It is two arrays and three doubles, it is the single
 * hottest thing in the plugin - there is one per tracked actor and one per tracked property - and every
 * reflected field it does not have is a field the property table does not have to walk. Keeping it plain is
 * also what lets the automation test drive it with a hand-written clock and no world at all.
 *
 * The bucket, rather than a list of timestamped samples, is the whole design. A sample list is O(samples) to
 * sum and grows with the sample rate; a bucket ring is O(buckets) to sum, is a fixed size forever, and
 * forgets by overwriting rather than by shifting. Twenty buckets over ten seconds means the window is
 * accurate to half a second, which is far finer than anybody reads a table at.
 */
struct NETLENS_API FNetLensWindow
{
	/**
	 * Set the length of the window and how finely it is divided. Clears whatever was in it.
	 *
	 * Buckets are clamped to at least two, because a single-bucket window cannot forget anything gradually -
	 * it would drop the whole window's contents at once every N seconds and the table would blink.
	 */
	void Configure(double InWindowSeconds, int32 InBucketCount);

	/** Drop everything, keeping the configuration. */
	void Reset();

	/**
	 * Add a measurement at time Now.
	 *
	 * Time only ever moves forward here. A sample older than the newest one already in the ring lands in the
	 * newest bucket rather than corrupting an old one - that costs a little accuracy in a case that should
	 * not happen and avoids a class of bug that would be invisible.
	 */
	void Add(double Now, double Bytes, double Events = 1.0);

	/**
	 * Move the ring forward to Now without adding anything, clearing buckets that have fallen out.
	 *
	 * This is what makes an actor that has gone quiet fade to zero instead of holding its last rate forever.
	 * Every tracked entry gets this every sample whether or not it replicated.
	 */
	void Advance(double Now);

	/** Bytes per second over the window. */
	double GetBytesPerSecond() const;

	/** Events - updates, changes, calls - per second over the window. */
	double GetEventsPerSecond() const;

	/** Bytes in the window. */
	double GetTotalBytes() const { return TotalBytes; }

	/** Events in the window. */
	double GetTotalEvents() const { return TotalEvents; }

	/** The busiest single bucket in the window, expressed as a rate. */
	double GetPeakBytesPerSecond() const;

	/** True when nothing has been recorded in the window. Used to retire entries that have gone silent. */
	bool IsEmpty() const { return TotalBytes <= 0.0 && TotalEvents <= 0.0; }

	/** Length of the window in seconds. */
	double GetWindowSeconds() const { return WindowSeconds; }

private:
	/** Clear the buckets between the last one written and the one Now falls in. */
	void RollTo(int64 TargetIndex);

	TArray<double> BucketBytes;
	TArray<double> BucketEvents;

	double WindowSeconds = 10.0;
	double BucketSeconds = 0.5;

	/** Running sums, so reading a rate is O(1) and only Advance is O(buckets it had to clear). */
	double TotalBytes = 0.0;
	double TotalEvents = 0.0;

	/** Absolute bucket index of the newest bucket written. MIN_int64 means "nothing yet". */
	int64 NewestIndex = MIN_int64;
};
