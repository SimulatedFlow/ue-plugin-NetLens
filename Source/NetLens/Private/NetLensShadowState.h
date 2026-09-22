// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class FProperty;
class UClass;
class UObject;

/** One replicated property that moved since the last time we looked, and what that move was priced at. */
struct FNetLensShadowChange
{
	FName PropertyName;
	FName OwnerClassName;

	/** Serialised size of the new value, from UNetLensStatics::EstimateNetValueBits. */
	int32 Bits = 0;
};

/**
 * A copy of one object's replicated state, kept so we can tell what actually changed.
 *
 * This is the part of NetLens that does the real work, and it is worth saying plainly why it exists at all.
 *
 * The engine gives a plugin no way to ask "how many bytes did this actor's last update cost". There is no
 * public hook on the bunch writer; UActorChannel::ReplicateActor returns the bit count but the engine calls
 * it, not us. What the engine *does* expose is everything needed to work it out honestly:
 *
 *   - UActorChannel::LastUpdateTime, so we know exactly when an actor really replicated;
 *   - the object's replicated properties, so we know what could have been in that update;
 *   - the values themselves, so we can compare them against a copy and know what *was*.
 *
 * That last comparison is the same one FRepLayout makes, against the same kind of shadow buffer, for the
 * same reason. A property that has not changed is not in the bunch, whatever its actor's update rate is, and
 * a diagnostic that reported it would send you to fix a property that costs nothing.
 *
 * The buffer is laid out exactly like an instance of the class - the full UClass::GetPropertiesSize, with
 * each tracked property constructed at its own offset - so a property's own Identical and CopyCompleteValue
 * can be used unchanged. The memory in between is never touched. That costs a few kilobytes per tracked
 * actor, which is why there is a ceiling on how many are tracked at once and why the ceiling is a documented
 * setting rather than a constant buried in here.
 */
class FNetLensObjectShadow
{
public:
	/**
	 * Snapshot Source now.
	 *
	 * The first snapshot is deliberately never reported as a change. An actor that has just come into view
	 * has "changed" every property it owns compared to nothing, and counting that would put a spike on the
	 * first row of every actor that spawns.
	 */
	FNetLensObjectShadow(const UObject* Source, int32 MaxProperties);

	~FNetLensObjectShadow();

	FNetLensObjectShadow(const FNetLensObjectShadow&) = delete;
	FNetLensObjectShadow& operator=(const FNetLensObjectShadow&) = delete;

	/** The object this shadows, while it is alive. */
	const UObject* GetObject() const { return SourceObject.Get(); }

	/** False once the object has gone away or been reinstanced onto a different class. */
	bool IsValidFor(const UObject* Object) const;

	/**
	 * Compare against the live object, append what moved, and re-snapshot.
	 *
	 * Re-snapshotting inside the same call is what makes the accounting add up: a change is charged exactly
	 * once, on the sample that first sees it, and never again until the value moves a second time.
	 */
	void CollectChanges(const UObject* Source, TArray<FNetLensShadowChange>& OutChanges);

	/** How many replicated properties this shadow follows. */
	int32 GetTrackedPropertyCount() const { return Properties.Num(); }

	/** Bytes of shadow buffer this instance holds. What the ceiling in the settings is really limiting. */
	int32 GetShadowBytes() const { return BufferSize; }

private:
	void Allocate(const UObject* Source, int32 MaxProperties);
	void Release();

	TWeakObjectPtr<const UObject> SourceObject;

	/**
	 * Held strongly, and that is not paranoia.
	 *
	 * Properties are fields owned by the class. If the class were collected while we still held raw FProperty
	 * pointers into it - which a Blueprint recompile in PIE can arrange - the destructor would run
	 * DestroyValue through a dangling pointer. Holding the class costs one reference and removes the entire
	 * failure mode. Reinstancing is still handled, by IsValidFor noticing the live object's class no longer
	 * matches and the owner throwing the shadow away.
	 */
	TStrongObjectPtr<UClass> ShadowClass;

	TArray<const FProperty*> Properties;

	uint8* Buffer = nullptr;
	int32 BufferSize = 0;
};
