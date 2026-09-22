// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "NetLensStatics.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/StringBuilder.h"
#include "NetLensLog.h"
#include "NetLensSettings.h"
#include "NetLensSubsystem.h"
#include "UObject/Class.h"

// Every property class the cost model casts to lives in UnrealType.h - except FTextProperty, which has its
// own header. The editor's shared PCH pulls it in by accident and a game build does not, so leaving it out
// compiles in the editor and fails on the target the plugin actually ships to.
#include "UObject/TextProperty.h"

#include "UObject/UnrealType.h"

namespace NetLensCost
{
	/**
	 * What a replicated object reference costs.
	 *
	 * Not a pointer and not a path. The engine sends a NetGUID, which for an object both ends already know
	 * about is a packed integer; forty-eight bits is the working figure for one that has been sent before.
	 * The first send of a previously unknown object is far more expensive - it carries a path - and NetLens
	 * does not try to model that, because it happens once and the thing worth finding is what happens every
	 * frame.
	 */
	static constexpr int32 ObjectReferenceBits = 48;

	/** An FName goes as a table index plus a number suffix once both ends have seen it. */
	static constexpr int32 NameBits = 48;

	/** Length prefix on a string, an array, a set or a map. */
	static constexpr int32 ContainerCountBits = 32;

	/** How far the model will follow a struct into its members before it gives up and prices by size. */
	static constexpr int32 MaxStructDepth = 4;

	/**
	 * Structs the engine serialises with hand-written code, and what that code writes.
	 *
	 * Everything in this table is a struct with STRUCT_NetSerializeNative, where summing the members would
	 * give the wrong answer - usually far too large, because the whole point of these types is that they
	 * quantise. FVector_NetQuantize is three doubles in memory and about sixty bits on the wire; a model that
	 * did not know that would report movement as five times its real cost and send you optimising the one
	 * thing Epic already optimised.
	 *
	 * Anything not in here is summed from its members, which is right for a plain USTRUCT.
	 */
	static const TMap<FName, int32>& KnownStructBits()
	{
		static const TMap<FName, int32> Table = {
			// Packed vectors. The quantisation level decides how many bits the mantissa keeps.
			{ TEXT("Vector_NetQuantize"),		60 },
			{ TEXT("Vector_NetQuantize10"),		70 },
			{ TEXT("Vector_NetQuantize100"),	80 },
			{ TEXT("Vector_NetQuantizeNormal"),	48 },

			// A plain FVector is sent as three floats, not three doubles - the wire format did not grow when
			// the in-memory one did.
			{ TEXT("Vector"),					96 },
			{ TEXT("Vector3f"),					96 },
			{ TEXT("Vector2D"),					64 },
			{ TEXT("Vector4"),					128 },

			// Rotators go as three compressed shorts.
			{ TEXT("Rotator"),					48 },
			{ TEXT("Quat"),						128 },
			{ TEXT("Transform"),				240 },

			{ TEXT("Color"),					32 },
			{ TEXT("LinearColor"),				128 },
			{ TEXT("IntPoint"),					64 },
			{ TEXT("IntVector"),				96 },
			{ TEXT("Guid"),						128 },

			/**
			 * The one that matters most in practice.
			 *
			 * FRepMovement is what AActor::ReplicatedMovement is, so it is on the critical path of every
			 * moving actor in every project. Its NetSerialize writes a quantised location, a compressed
			 * rotation, a quantised velocity and a flags byte, and optionally angular velocity. This is the
			 * figure for the common case with linear velocity and no angular.
			 */
			{ TEXT("RepMovement"),				176 },

			{ TEXT("RepAttachment"),			160 },
			{ TEXT("UniqueNetIdRepl"),			128 },

			// GameplayTags replicate as an index into the shared tag table, not as their names.
			{ TEXT("GameplayTag"),				32 },
		};

		return Table;
	}

	static int32 Estimate(const FProperty* Property, const void* ValuePtr, int32 Depth);

	/** Sum every member of a struct. Used for anything without a hand-written NetSerialize. */
	static int32 EstimateStructMembers(const UScriptStruct* Struct, const void* ValuePtr, int32 Depth)
	{
		if (!Struct)
		{
			return 0;
		}

		int32 Bits = 0;

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Member = *It;
			if (!Member)
			{
				continue;
			}

			// Inside a struct there is no CPF_Net filtering. A struct is serialised whole or not at all, so
			// every member of one that is being sent is on the wire whether it is marked replicated or not.
			const void* MemberValue = ValuePtr ? Member->ContainerPtrToValuePtr<void>(ValuePtr) : nullptr;
			Bits += Estimate(Member, MemberValue, Depth + 1) * FMath::Max(1, Member->ArrayDim);
		}

		return Bits;
	}

	static int32 Estimate(const FProperty* Property, const void* ValuePtr, int32 Depth)
	{
		if (!Property)
		{
			return 0;
		}

		// A struct that nests four deep is either generated or pathological. Pricing by machine size at that
		// point is an over-estimate, which is the safe direction for a diagnostic: it draws attention to
		// something unusual rather than hiding it.
		if (Depth > MaxStructDepth)
		{
			return Property->GetElementSize() * 8;
		}

		if (CastField<FBoolProperty>(Property))
		{
			// One bit, genuinely. The replication system packs bools into the bunch bit by bit; a replicated
			// bool does not cost a byte and a cost model that said it did would rank flags above vectors.
			return 1;
		}

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const FNumericProperty* Underlying = EnumProperty->GetUnderlyingProperty();
			return Underlying ? Underlying->GetSize() * 8 : 8;
		}

		if (CastField<FByteProperty>(Property) || CastField<FInt8Property>(Property))
		{
			return 8;
		}

		if (CastField<FInt16Property>(Property) || CastField<FUInt16Property>(Property))
		{
			return 16;
		}

		if (CastField<FIntProperty>(Property) || CastField<FUInt32Property>(Property)
			|| CastField<FFloatProperty>(Property))
		{
			return 32;
		}

		if (CastField<FInt64Property>(Property) || CastField<FUInt64Property>(Property)
			|| CastField<FDoubleProperty>(Property))
		{
			return 64;
		}

		if (CastField<FNameProperty>(Property))
		{
			return NameBits;
		}

		if (CastField<FStrProperty>(Property))
		{
			const int32 Length = ValuePtr
				? static_cast<const FString*>(ValuePtr)->Len()
				: UNetLensSettings::Get().AssumedStringLength;

			// Length prefix, then the characters, then the terminator the engine writes with them.
			return ContainerCountBits + (Length + 1) * 8;
		}

		if (CastField<FTextProperty>(Property))
		{
			// FText carries its flags and either a literal or a namespace/key/source triple. There is no
			// cheap way to know which from here, so it is priced at a string of the assumed length plus the
			// header - and the documentation says so, because an FText you replicate every frame is a
			// mistake NetLens should point at even if it cannot price it exactly.
			const int32 Length = UNetLensSettings::Get().AssumedStringLength;
			return 8 + ContainerCountBits + (Length + 1) * 8;
		}

		if (CastField<FObjectPropertyBase>(Property) || CastField<FInterfaceProperty>(Property))
		{
			return ObjectReferenceBits;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			const int32 ElementBits = Estimate(ArrayProperty->Inner, nullptr, Depth + 1);

			int32 Count = 1;
			if (ValuePtr)
			{
				FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
				Count = Helper.Num();
			}

			// Elements are priced by type rather than one by one. Walking a thousand-element array on every
			// sample to price it exactly would make NetLens the thing it was bought to find, and the answer
			// would move the row by a few per cent at most.
			return ContainerCountBits + Count * ElementBits;
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			const int32 ElementBits = Estimate(SetProperty->ElementProp, nullptr, Depth + 1);

			int32 Count = 1;
			if (ValuePtr)
			{
				FScriptSetHelper Helper(SetProperty, ValuePtr);
				Count = Helper.Num();
			}

			return ContainerCountBits + Count * ElementBits;
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			const int32 PairBits = Estimate(MapProperty->KeyProp, nullptr, Depth + 1)
				+ Estimate(MapProperty->ValueProp, nullptr, Depth + 1);

			int32 Count = 1;
			if (ValuePtr)
			{
				FScriptMapHelper Helper(MapProperty, ValuePtr);
				Count = Helper.Num();
			}

			return ContainerCountBits + Count * PairBits;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (!Struct)
			{
				return Property->GetElementSize() * 8;
			}

			if (const int32* Known = KnownStructBits().Find(Struct->GetFName()))
			{
				return *Known;
			}

			return EstimateStructMembers(Struct, ValuePtr, Depth);
		}

		// Anything the model has never heard of. Its size in memory is always at least as large as its size
		// on the wire, so this errs towards drawing attention rather than towards hiding a cost.
		//
		// Per *element*, not per property: the callers above multiply by ArrayDim themselves, and GetSize()
		// already includes it. Using it here would charge a static array of eight its cost sixty-four times.
		return Property->GetElementSize() * 8;
	}
}

//~ Subsystem forwarding ---------------------------------------------------------------------------------

UNetLensSubsystem* UNetLensStatics::GetNetLens(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UNetLensSubsystem>() : nullptr;
}

void UNetLensStatics::SetShowOverlay(const UObject* WorldContextObject, bool bShow)
{
	if (UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		Subsystem->SetShowOverlay(bShow);
	}
}

bool UNetLensStatics::IsShowingOverlay(const UObject* WorldContextObject)
{
	const UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject);
	return Subsystem && Subsystem->IsShowingOverlay();
}

void UNetLensStatics::FreezeNetLens(const UObject* WorldContextObject)
{
	if (UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		Subsystem->Freeze();
	}
}

void UNetLensStatics::ResumeNetLens(const UObject* WorldContextObject)
{
	if (UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		Subsystem->Resume();
	}
}

bool UNetLensStatics::ToggleFreezeNetLens(const UObject* WorldContextObject)
{
	UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject);
	if (!Subsystem)
	{
		return false;
	}

	if (Subsystem->IsFrozen())
	{
		Subsystem->Resume();
	}
	else
	{
		Subsystem->Freeze();
	}

	return Subsystem->IsFrozen();
}

bool UNetLensStatics::IsNetLensFrozen(const UObject* WorldContextObject)
{
	const UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject);
	return Subsystem && Subsystem->IsFrozen();
}

void UNetLensStatics::ResetNetLens(const UObject* WorldContextObject)
{
	if (UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		Subsystem->ResetMeasurements();
	}
}

bool UNetLensStatics::ExportNetLensCsv(const UObject* WorldContextObject, const FString& Path, FString& OutPath)
{
	OutPath.Reset();

	UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogNetLens, Warning, TEXT("ExportNetLensCsv: no NetLens subsystem in this world."));
		return false;
	}

	return Subsystem->ExportCsv(Path, OutPath);
}

TArray<FNetLensActorStat> UNetLensStatics::GetTopActors(const UObject* WorldContextObject, int32 TopN)
{
	TArray<FNetLensActorStat> Result;

	if (const UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		RankActors(Subsystem->GetActorStats(), TopN, Subsystem->GetSortBy(), Result);
	}

	return Result;
}

TArray<FNetLensRPCStat> UNetLensStatics::GetTopRPCs(const UObject* WorldContextObject, int32 TopN)
{
	TArray<FNetLensRPCStat> Result;

	if (const UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		RankRPCs(Subsystem->GetRPCStats(), TopN, Result);
	}

	return Result;
}

FNetLensTotals UNetLensStatics::GetNetLensTotals(const UObject* WorldContextObject)
{
	if (const UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		return Subsystem->GetTotals();
	}

	return FNetLensTotals();
}

bool UNetLensStatics::GetActorStat(const UObject* WorldContextObject, AActor* Actor, FNetLensActorStat& OutStat)
{
	OutStat = FNetLensActorStat();

	const UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject);
	return Subsystem && Subsystem->FindActorStat(Actor, OutStat);
}

void UNetLensStatics::SetTopN(const UObject* WorldContextObject, int32 TopN)
{
	if (UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		Subsystem->SetTopN(TopN);
	}
}

void UNetLensStatics::SetSortBy(const UObject* WorldContextObject, ENetLensSort SortBy)
{
	if (UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		Subsystem->SetSortBy(SortBy);
	}
}

void UNetLensStatics::NoteRPC(const UObject* WorldContextObject, AActor* Actor, FName FunctionName)
{
	if (UNetLensSubsystem* Subsystem = GetNetLens(WorldContextObject))
	{
		Subsystem->NoteRPC(Actor, FunctionName);
	}
}

AActor* UNetLensStatics::GetStatActor(const FNetLensActorStat& Stat)
{
	return Stat.Actor.Get();
}

//~ Ranking ----------------------------------------------------------------------------------------------

void UNetLensStatics::RankActors(const TArray<FNetLensActorStat>& Actors, int32 TopN, ENetLensSort SortBy,
	TArray<FNetLensActorStat>& OutRanked)
{
	OutRanked = Actors;

	switch (SortBy)
	{
	case ENetLensSort::Updates:
		OutRanked.Sort([](const FNetLensActorStat& A, const FNetLensActorStat& B)
		{
			return A.UpdatesPerSecond > B.UpdatesPerSecond;
		});
		break;

	case ENetLensSort::Name:
		OutRanked.Sort([](const FNetLensActorStat& A, const FNetLensActorStat& B)
		{
			return A.ActorName.LexicalLess(B.ActorName);
		});
		break;

	case ENetLensSort::Bytes:
	default:
		OutRanked.Sort([](const FNetLensActorStat& A, const FNetLensActorStat& B)
		{
			return A.BytesPerSecond > B.BytesPerSecond;
		});
		break;
	}

	if (TopN <= 0 || OutRanked.Num() <= TopN)
	{
		return;
	}

	// Fold everything past the cap into one row rather than dropping it. See the header: the rows you are not
	// looking at have to still be counted somewhere, or the panel can tell you the traffic is fine while four
	// hundred small actors are eating it.
	FNetLensActorStat Aggregate;
	Aggregate.bIsAggregate = true;

	for (int32 Index = TopN; Index < OutRanked.Num(); ++Index)
	{
		const FNetLensActorStat& Folded = OutRanked[Index];

		// A row that is itself an aggregate - the subsystem's overflow row - stands for more than one actor,
		// and folding it as "one" would quietly shrink the count the panel prints. This is the difference
		// between "and 11 more actors" and "and 388 more actors" on a level with four hundred projectiles.
		Aggregate.AggregatedActors += Folded.bIsAggregate ? FMath::Max(1, Folded.AggregatedActors) : 1;

		Aggregate.BytesPerSecond += Folded.BytesPerSecond;
		Aggregate.UpdatesPerSecond += Folded.UpdatesPerSecond;
		Aggregate.TotalBytes += Folded.TotalBytes;
		Aggregate.RelevantConnections = FMath::Max(Aggregate.RelevantConnections, Folded.RelevantConnections);

		// The aggregate's peak is the largest peak among the actors in it, not their sum. A sum would claim a
		// spike that never happened, because the spikes did not land in the same bucket.
		Aggregate.PeakBytesPerSecond = FMath::Max(Aggregate.PeakBytesPerSecond, Folded.PeakBytesPerSecond);
	}

	Aggregate.ActorName = FName(*FString::Printf(TEXT("(%d more actors)"), Aggregate.AggregatedActors));
	Aggregate.ClassName = TEXT("NetLensAggregate");

	OutRanked.SetNum(TopN, EAllowShrinking::Yes);
	OutRanked.Add(MoveTemp(Aggregate));
}

void UNetLensStatics::RankRPCs(const TArray<FNetLensRPCStat>& RPCs, int32 TopN, TArray<FNetLensRPCStat>& OutRanked)
{
	OutRanked = RPCs;

	OutRanked.Sort([](const FNetLensRPCStat& A, const FNetLensRPCStat& B)
	{
		return A.BytesPerSecond > B.BytesPerSecond;
	});

	if (TopN <= 0 || OutRanked.Num() <= TopN)
	{
		return;
	}

	FNetLensRPCStat Aggregate;
	int32 Folded = 0;

	for (int32 Index = TopN; Index < OutRanked.Num(); ++Index)
	{
		Aggregate.BytesPerSecond += OutRanked[Index].BytesPerSecond;
		Aggregate.CallsPerSecond += OutRanked[Index].CallsPerSecond;
		Aggregate.CallsInWindow += OutRanked[Index].CallsInWindow;
		Aggregate.TotalBytes += OutRanked[Index].TotalBytes;
		++Folded;
	}

	Aggregate.FunctionName = FName(*FString::Printf(TEXT("(%d more functions)"), Folded));
	Aggregate.ClassName = TEXT("NetLensAggregate");

	OutRanked.SetNum(TopN, EAllowShrinking::Yes);
	OutRanked.Add(MoveTemp(Aggregate));
}

//~ CSV --------------------------------------------------------------------------------------------------

namespace NetLensCsv
{
	/** Quote a field and double any quote inside it. Actor labels with commas in them are not rare. */
	static void AppendField(FStringBuilderBase& Builder, const FString& Value, bool bLast)
	{
		Builder.AppendChar(TEXT('"'));
		for (const TCHAR Character : Value)
		{
			if (Character == TEXT('"'))
			{
				Builder.AppendChar(TEXT('"'));
			}
			Builder.AppendChar(Character);
		}
		Builder.AppendChar(TEXT('"'));

		if (!bLast)
		{
			Builder.AppendChar(TEXT(','));
		}
	}

	static void AppendNumber(FStringBuilderBase& Builder, double Value, bool bLast)
	{
		AppendField(Builder, FString::Printf(TEXT("%.3f"), Value), bLast);
	}

	static void AppendInt(FStringBuilderBase& Builder, int32 Value, bool bLast)
	{
		AppendField(Builder, FString::FromInt(Value), bLast);
	}

	/**
	 * One row, twelve columns, in the order GetCsvHeader lists them.
	 *
	 * Everything goes through here - actors, properties, RPCs and the two driver rows - so a column can never
	 * drift out of alignment between one kind of row and another. That is the only way a single parse on the
	 * far end stays possible.
	 */
	static void AppendRow(FStringBuilderBase& Builder, const TCHAR* Kind, const FString& Name,
		const FString& Class, const FString& Owner, double BytesPerSecond, double PeakBytesPerSecond,
		double EventsPerSecond, double BytesPerEvent, double TotalBytes, int32 Connections, bool bReliable,
		int32 Count)
	{
		AppendField(Builder, Kind, false);
		AppendField(Builder, Name, false);
		AppendField(Builder, Class, false);
		AppendField(Builder, Owner, false);
		AppendNumber(Builder, BytesPerSecond, false);
		AppendNumber(Builder, PeakBytesPerSecond, false);
		AppendNumber(Builder, EventsPerSecond, false);
		AppendNumber(Builder, BytesPerEvent, false);
		AppendNumber(Builder, TotalBytes, false);
		AppendInt(Builder, Connections, false);
		AppendInt(Builder, bReliable ? 1 : 0, false);
		AppendInt(Builder, Count, true);
		Builder.Append(TEXT("\r\n"));
	}
}

FString UNetLensStatics::GetCsvHeader()
{
	return TEXT("Kind,Name,Class,Owner,BytesPerSecond,PeakBytesPerSecond,EventsPerSecond,BytesPerEvent,")
		TEXT("TotalBytes,Connections,Reliable,Count");
}

FString UNetLensStatics::BuildCsv(const TArray<FNetLensActorStat>& Actors, const TArray<FNetLensRPCStat>& RPCs,
	const FNetLensTotals& Totals, bool bIncludeProperties)
{
	using namespace NetLensCsv;

	TStringBuilder<4096> Builder;

	Builder.Append(GetCsvHeader());
	Builder.Append(TEXT("\r\n"));

	// Two driver rows first, so the file carries the ground truth it should be compared against. On a Driver
	// row PeakBytesPerSecond means "what NetLens attributed in this direction" - NetLens only attributes
	// outgoing traffic, so the incoming row's is zero. That is spelled out in the documentation as well;
	// re-using a column rather than adding two that are blank on every other row keeps the parse single.
	AppendRow(Builder, TEXT("Driver"), TEXT("Out"), TEXT("NetDriver"), FString(),
		Totals.DriverOutBytesPerSecond,
		static_cast<double>(Totals.AttributedActorBytesPerSecond) + Totals.AttributedRPCBytesPerSecond,
		Totals.OutPacketsPerSecond, Totals.AveragePingMs, 0.0, Totals.ConnectionCount, false,
		Totals.ActorChannelCount);

	AppendRow(Builder, TEXT("Driver"), TEXT("In"), TEXT("NetDriver"), FString(),
		Totals.DriverInBytesPerSecond, 0.0, Totals.InPacketsPerSecond, Totals.AveragePingMs, 0.0,
		Totals.ConnectionCount, false, Totals.SeenActors);

	for (const FNetLensActorStat& Actor : Actors)
	{
		const double BytesPerUpdate = Actor.UpdatesPerSecond > KINDA_SMALL_NUMBER
			? Actor.BytesPerSecond / Actor.UpdatesPerSecond
			: 0.0;

		AppendRow(Builder, Actor.bIsAggregate ? TEXT("Aggregate") : TEXT("Actor"),
			Actor.ActorName.ToString(), Actor.ClassName.ToString(), FString(),
			Actor.BytesPerSecond, Actor.PeakBytesPerSecond, Actor.UpdatesPerSecond, BytesPerUpdate,
			Actor.TotalBytes, Actor.RelevantConnections, false, Actor.AggregatedActors);

		if (!bIncludeProperties)
		{
			continue;
		}

		for (const FNetLensPropertyStat& Property : Actor.Properties)
		{
			AppendRow(Builder, TEXT("Property"), Property.PropertyName.ToString(),
				Actor.ClassName.ToString(), Property.OwnerClassName.ToString(),
				Property.BytesPerSecond, 0.0, Property.ChangesPerSecond, Property.BytesPerChange,
				Property.TotalBytes, Actor.RelevantConnections, false, 0);
		}
	}

	for (const FNetLensRPCStat& RPC : RPCs)
	{
		AppendRow(Builder, TEXT("RPC"), RPC.FunctionName.ToString(), RPC.ClassName.ToString(),
			RPC.bMulticast ? TEXT("Multicast") : TEXT(""),
			RPC.BytesPerSecond, 0.0, RPC.CallsPerSecond, RPC.BytesPerCall, RPC.TotalBytes,
			Totals.ConnectionCount, RPC.bReliable, RPC.CallsInWindow);
	}

	return Builder.ToString();
}

void UNetLensStatics::SplitCsvLine(const FString& Line, TArray<FString>& OutFields)
{
	OutFields.Reset();

	FString Current;
	bool bInQuotes = false;

	for (int32 Index = 0; Index < Line.Len(); ++Index)
	{
		const TCHAR Character = Line[Index];

		if (bInQuotes)
		{
			if (Character == TEXT('"'))
			{
				// A doubled quote inside a quoted field is one literal quote. Anything else ends the field.
				if (Index + 1 < Line.Len() && Line[Index + 1] == TEXT('"'))
				{
					Current.AppendChar(TEXT('"'));
					++Index;
				}
				else
				{
					bInQuotes = false;
				}
			}
			else
			{
				Current.AppendChar(Character);
			}

			continue;
		}

		if (Character == TEXT('"'))
		{
			bInQuotes = true;
		}
		else if (Character == TEXT(','))
		{
			OutFields.Add(Current);
			Current.Reset();
		}
		else if (Character != TEXT('\r') && Character != TEXT('\n'))
		{
			Current.AppendChar(Character);
		}
	}

	OutFields.Add(Current);
}

//~ Formatting -------------------------------------------------------------------------------------------

FString UNetLensStatics::FormatBytes(float Bytes)
{
	const float Absolute = FMath::Abs(Bytes);

	if (Absolute < 1024.0f)
	{
		return FString::Printf(TEXT("%.0f B"), Bytes);
	}

	if (Absolute < 1024.0f * 1024.0f)
	{
		return FString::Printf(TEXT("%.1f KB"), Bytes / 1024.0f);
	}

	return FString::Printf(TEXT("%.2f MB"), Bytes / (1024.0f * 1024.0f));
}

FString UNetLensStatics::FormatBytesPerSecond(float BytesPerSecond)
{
	return FormatBytes(BytesPerSecond) + TEXT("/s");
}

//~ Cost model -------------------------------------------------------------------------------------------

int32 UNetLensStatics::EstimateNetPropertyBits(const FProperty* Property)
{
	return NetLensCost::Estimate(Property, nullptr, 0);
}

int32 UNetLensStatics::EstimateNetValueBits(const FProperty* Property, const void* ValuePtr)
{
	return NetLensCost::Estimate(Property, ValuePtr, 0);
}

int32 UNetLensStatics::EstimateFunctionParameterBits(const UFunction* Function, const void* Parms)
{
	if (!Function)
	{
		return 0;
	}

	int32 Bits = 0;

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		const FProperty* Parameter = *It;

		// An RPC sends its inputs and nothing else. Return and out parameters exist in the signature of a
		// replicated function only because the reflection system allows them; nothing travels back.
		if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
		{
			continue;
		}

		const void* ValuePtr = Parms ? Parameter->ContainerPtrToValuePtr<void>(Parms) : nullptr;
		Bits += NetLensCost::Estimate(Parameter, ValuePtr, 0) * FMath::Max(1, Parameter->ArrayDim);
	}

	return Bits;
}
