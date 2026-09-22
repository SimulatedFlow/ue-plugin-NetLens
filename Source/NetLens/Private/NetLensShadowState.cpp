// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "NetLensShadowState.h"

#include "NetLensStatics.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

FNetLensObjectShadow::FNetLensObjectShadow(const UObject* Source, int32 MaxProperties)
{
	Allocate(Source, MaxProperties);
}

FNetLensObjectShadow::~FNetLensObjectShadow()
{
	Release();
}

void FNetLensObjectShadow::Allocate(const UObject* Source, int32 MaxProperties)
{
	if (!Source)
	{
		return;
	}

	UClass* Class = Source->GetClass();
	if (!Class)
	{
		return;
	}

	SourceObject = Source;
	ShadowClass.Reset(Class);

	// CPF_Net is the flag the reflection system sets on anything declared Replicated. It is a superset of
	// what a given connection receives - a COND_OwnerOnly property is flagged the same way as an unconditional
	// one - and NetLens says so in its documentation rather than pretending otherwise. Getting at the
	// conditions would mean reaching into FRepLayout's private state, and a number that is right for the
	// owning connection and generous for the others is far more useful than no number.
	const int32 PropertyCap = FMath::Max(1, MaxProperties);

	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Net))
		{
			continue;
		}

		Properties.Add(Property);

		if (Properties.Num() >= PropertyCap)
		{
			break;
		}
	}

	if (Properties.Num() == 0)
	{
		// Nothing replicated on this class. Keep the (empty) shadow rather than returning null, so the owner
		// does not try to build one again on every single sample.
		return;
	}

	BufferSize = Class->GetPropertiesSize();
	if (BufferSize <= 0)
	{
		Properties.Reset();
		return;
	}

	Buffer = static_cast<uint8*>(FMemory::Malloc(BufferSize, Class->GetMinAlignment()));
	FMemory::Memzero(Buffer, BufferSize);

	// Construct only the properties we track, each at its own offset. Zeroed memory is already a valid empty
	// TArray or FString, so the untouched space between them is inert - but a property that owns memory still
	// needs its constructor run before anything is copied over it.
	for (const FProperty* Property : Properties)
	{
		Property->InitializeValue_InContainer(Buffer);
		Property->CopyCompleteValue_InContainer(Buffer, Source);
	}
}

void FNetLensObjectShadow::Release()
{
	if (Buffer)
	{
		// Only if the class is still with us. It is held strongly precisely so that this is always true, but
		// a plugin that dereferences a class pointer in a destructor should check it anyway.
		if (ShadowClass.IsValid())
		{
			for (const FProperty* Property : Properties)
			{
				Property->DestroyValue_InContainer(Buffer);
			}
		}

		FMemory::Free(Buffer);
		Buffer = nullptr;
	}

	BufferSize = 0;
	Properties.Reset();
	ShadowClass.Reset();
	SourceObject.Reset();
}

bool FNetLensObjectShadow::IsValidFor(const UObject* Object) const
{
	if (!Object || !SourceObject.IsValid())
	{
		return false;
	}

	if (SourceObject.Get() != Object)
	{
		return false;
	}

	// A Blueprint recompiled during PIE hands the same actor a different UClass. The properties we hold are
	// the old class's, so the shadow is not merely stale, it is describing a layout that is no longer there.
	return Object->GetClass() == ShadowClass.Get();
}

void FNetLensObjectShadow::CollectChanges(const UObject* Source, TArray<FNetLensShadowChange>& OutChanges)
{
	if (!Buffer || !IsValidFor(Source))
	{
		return;
	}

	const FName OwnerClassNameFallback = ShadowClass.IsValid() ? ShadowClass->GetFName() : NAME_None;

	for (const FProperty* Property : Properties)
	{
		const int32 ArrayDim = FMath::Max(1, Property->ArrayDim);

		int32 ChangedBits = 0;

		for (int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ++ArrayIndex)
		{
			if (Property->Identical_InContainer(Source, Buffer, ArrayIndex))
			{
				continue;
			}

			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Source, ArrayIndex);
			ChangedBits += UNetLensStatics::EstimateNetValueBits(Property, ValuePtr);
		}

		if (ChangedBits <= 0)
		{
			continue;
		}

		FNetLensShadowChange& Change = OutChanges.AddDefaulted_GetRef();
		Change.PropertyName = Property->GetFName();

		// The declaring class, not the instance's class. "ReplicatedMovement on AActor" is a far more useful
		// thing to read than "ReplicatedMovement on BP_Spammer_C", because it tells you where to go and fix it.
		const UStruct* Owner = Property->GetOwnerStruct();
		Change.OwnerClassName = Owner ? Owner->GetFName() : OwnerClassNameFallback;
		Change.Bits = ChangedBits;

		Property->CopyCompleteValue_InContainer(Buffer, Source);
	}
}
