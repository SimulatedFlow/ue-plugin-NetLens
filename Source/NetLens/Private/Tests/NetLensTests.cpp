// Copyright 2026 Silvan Teufel. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "NetLensStatics.h"
#include "NetLensTypes.h"

/**
 * Everything under test here is static and world-free, which is exactly why the ranking, the ring buffer,
 * the CSV and the cost model were put in a Blueprint function library and a plain struct rather than inside
 * the subsystem. A UTickableWorldSubsystem needs a world, cannot be NewObject'd in an automation test, and
 * every line of logic that lives in one is a line that never gets a test.
 *
 * These call the same functions the sampler calls on every frame. There is no second implementation here
 * that agrees with the first until somebody edits one of the two.
 */
namespace NetLensTest
{
	static FNetLensActorStat MakeActor(const TCHAR* Name, float BytesPerSecond, float UpdatesPerSecond = 1.0f)
	{
		FNetLensActorStat Stat;
		Stat.ActorName = Name;
		Stat.ClassName = TEXT("BP_Test_C");
		Stat.BytesPerSecond = BytesPerSecond;
		Stat.PeakBytesPerSecond = BytesPerSecond * 2.0f;
		Stat.UpdatesPerSecond = UpdatesPerSecond;
		Stat.TotalBytes = BytesPerSecond * 10.0f;
		Stat.RelevantConnections = 1;
		return Stat;
	}

	static FNetLensRPCStat MakeRPC(const TCHAR* Name, float BytesPerSecond, bool bReliable = false)
	{
		FNetLensRPCStat Stat;
		Stat.FunctionName = Name;
		Stat.ClassName = TEXT("BP_Test_C");
		Stat.BytesPerSecond = BytesPerSecond;
		Stat.CallsPerSecond = 4.0f;
		Stat.bReliable = bReliable;
		Stat.BytesPerCall = BytesPerSecond / 4.0f;
		Stat.CallsInWindow = 40;
		Stat.TotalBytes = BytesPerSecond * 10.0f;
		return Stat;
	}
}

//~ 1. Ranking -------------------------------------------------------------------------------------------

/**
 * RankActors sorts by cost and honours its cap.
 *
 * The sort is the obvious half. The cap is the half that matters: the panel has a fixed number of rows and
 * a ranking that returned more than it was asked for would silently overflow whatever was drawing it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetLensRankActorsTest, "NetLens.Rank.SortsAndCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNetLensRankActorsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace NetLensTest;

	TArray<FNetLensActorStat> Input;
	Input.Add(MakeActor(TEXT("Quiet"), 100.0f, 2.0f));
	Input.Add(MakeActor(TEXT("Loudest"), 9000.0f, 3.0f));
	Input.Add(MakeActor(TEXT("Middle"), 4000.0f, 30.0f));

	TArray<FNetLensActorStat> Ranked;

	// No cap: the same three rows, most expensive first, and no aggregate row invented out of nothing.
	UNetLensStatics::RankActors(Input, 0, ENetLensSort::Bytes, Ranked);

	TestEqual(TEXT("uncapped keeps every row"), Ranked.Num(), 3);
	TestEqual(TEXT("loudest first"), Ranked[0].ActorName, FName(TEXT("Loudest")));
	TestEqual(TEXT("middle second"), Ranked[1].ActorName, FName(TEXT("Middle")));
	TestEqual(TEXT("quiet last"), Ranked[2].ActorName, FName(TEXT("Quiet")));
	TestFalse(TEXT("no aggregate row when nothing was folded"), Ranked[2].bIsAggregate);

	// Sorting by updates is a different question and has to give a different answer - the actor that talks
	// most often is not the actor that says the most.
	UNetLensStatics::RankActors(Input, 0, ENetLensSort::Updates, Ranked);
	TestEqual(TEXT("update sort puts the chattiest first"), Ranked[0].ActorName, FName(TEXT("Middle")));

	UNetLensStatics::RankActors(Input, 0, ENetLensSort::Name, Ranked);
	TestEqual(TEXT("name sort is alphabetical"), Ranked[0].ActorName, FName(TEXT("Loudest")));

	// Capped at one: one real row plus the fold, and never more than that.
	UNetLensStatics::RankActors(Input, 1, ENetLensSort::Bytes, Ranked);

	TestEqual(TEXT("cap of one yields the row plus the aggregate"), Ranked.Num(), 2);
	TestEqual(TEXT("the kept row is the loudest"), Ranked[0].ActorName, FName(TEXT("Loudest")));
	TestTrue(TEXT("the trailing row is the aggregate"), Ranked[1].bIsAggregate);

	return true;
}

//~ 2. The ring buffer -----------------------------------------------------------------------------------

/**
 * The window forgets what falls out of it, and the peak survives being averaged away.
 *
 * Both halves are load-bearing. Without the forgetting, every rate would be a lifetime average and an actor
 * that misbehaved once would stay at the top of the table forever. Without the peak, the averaging that
 * makes the table readable would hide the one frame that mattered.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetLensWindowTest, "NetLens.Window.ForgetsOlderThanTheWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNetLensWindowTest::RunTest(const FString& /*Parameters*/)
{
	FNetLensWindow Window;
	Window.Configure(10.0, 20);

	TestTrue(TEXT("a fresh window is empty"), Window.IsEmpty());
	TestEqual(TEXT("an empty window has no rate"), Window.GetBytesPerSecond(), 0.0);

	// Ten kilobytes spread over the first second of a ten second window is one kilobyte a second.
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Window.Add(100.0 + Index * 0.1, 1024.0, 1.0);
	}

	TestEqual(TEXT("everything added is in the window"), Window.GetTotalBytes(), 10240.0);
	TestEqual(TEXT("the rate is the window average"), Window.GetBytesPerSecond(), 1024.0, 0.001);
	TestEqual(TEXT("ten events in ten seconds is one a second"), Window.GetEventsPerSecond(), 1.0, 0.001);

	// The peak is a per-bucket rate, so ten kilobytes inside one half-second bucket reads as twenty
	// kilobytes a second even though the window average is one.
	TestTrue(TEXT("the peak is far above the average"), Window.GetPeakBytesPerSecond() > 10000.0);

	// Five seconds on, still inside the window: nothing has been forgotten.
	Window.Advance(105.0);
	TestEqual(TEXT("still inside the window"), Window.GetTotalBytes(), 10240.0);

	// Eleven seconds on: the samples are older than the window and are gone.
	Window.Advance(111.0);
	TestTrue(TEXT("everything older than the window is forgotten"), Window.IsEmpty());
	TestEqual(TEXT("and the rate falls to zero"), Window.GetBytesPerSecond(), 0.0);
	TestEqual(TEXT("and so does the peak"), Window.GetPeakBytesPerSecond(), 0.0);

	// A gap longer than the whole ring takes the fast path in RollTo. It has to end in the same state as a
	// slow walk would, or a game that sat on a breakpoint would come back with stale numbers.
	Window.Add(200.0, 512.0, 1.0);
	TestEqual(TEXT("the window works again after a long gap"), Window.GetTotalBytes(), 512.0);
	Window.Advance(400.0);
	TestTrue(TEXT("and forgets across the long gap too"), Window.IsEmpty());

	return true;
}

//~ 3. CSV -----------------------------------------------------------------------------------------------

/**
 * The export writes a header and exactly one line per entry, and reads back into the same fields.
 *
 * "And it is readable again" is half of what an export is worth. The awkward names are in here on purpose:
 * an actor called BP_Turret,Left and one with a quote in its name have both broken a CSV before, and both
 * have to survive the round trip.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetLensCsvTest, "NetLens.Csv.HeaderPlusOneLinePerEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNetLensCsvTest::RunTest(const FString& /*Parameters*/)
{
	using namespace NetLensTest;

	TArray<FNetLensActorStat> Actors;
	Actors.Add(MakeActor(TEXT("BP_Turret,Left"), 5000.0f));
	Actors.Add(MakeActor(TEXT("BP_Spammer"), 2000.0f));

	// One property row on the first actor, so the property branch of the writer is exercised too.
	FNetLensPropertyStat Property;
	Property.PropertyName = TEXT("BigStruct");
	Property.OwnerClassName = TEXT("Actor");
	Property.BytesPerSecond = 4000.0f;
	Property.ChangesPerSecond = 10.0f;
	Property.BytesPerChange = 400.0f;
	Property.TotalBytes = 40000.0f;
	Actors[0].Properties.Add(Property);

	TArray<FNetLensRPCStat> RPCs;
	RPCs.Add(MakeRPC(TEXT("Multicast_\"Bang\""), 800.0f, true));

	FNetLensTotals Totals;
	Totals.bHasNetDriver = true;
	Totals.DriverOutBytesPerSecond = 9000.0f;
	Totals.DriverInBytesPerSecond = 400.0f;
	Totals.ConnectionCount = 1;

	const FString Csv = UNetLensStatics::BuildCsv(Actors, RPCs, Totals, true);

	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines);

	// Header, two driver rows, two actors, one property, one RPC.
	const int32 Expected = 1 + 2 + Actors.Num() + 1 + RPCs.Num();
	TestEqual(TEXT("header plus one line per entry"), Lines.Num(), Expected);

	TestEqual(TEXT("the first line is the header"), Lines[0], UNetLensStatics::GetCsvHeader());

	TArray<FString> HeaderFields;
	UNetLensStatics::SplitCsvLine(Lines[0], HeaderFields);
	TestEqual(TEXT("twelve columns"), HeaderFields.Num(), 12);

	// Every data line has to have the same shape as the header, or a single parse on the far end is a lie.
	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		TArray<FString> Fields;
		UNetLensStatics::SplitCsvLine(Lines[Index], Fields);

		TestEqual(*FString::Printf(TEXT("line %d has twelve columns"), Index), Fields.Num(), 12);
	}

	// The awkward names come back exactly as they went in. The comma did not split a field and the quotes
	// did not swallow one.
	bool bFoundComma = false;
	bool bFoundQuote = false;

	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		TArray<FString> Fields;
		UNetLensStatics::SplitCsvLine(Lines[Index], Fields);

		if (Fields.Num() > 1 && Fields[1] == TEXT("BP_Turret,Left"))
		{
			bFoundComma = true;
			TestEqual(TEXT("the actor's rate survived the round trip"), FCString::Atod(*Fields[4]), 5000.0, 0.01);
		}
		if (Fields.Num() > 1 && Fields[1] == TEXT("Multicast_\"Bang\""))
		{
			bFoundQuote = true;
			TestEqual(TEXT("a reliable RPC comes back reliable"), Fields[10], FString(TEXT("1")));
		}
	}

	TestTrue(TEXT("a name with a comma in it survived"), bFoundComma);
	TestTrue(TEXT("a name with quotes in it survived"), bFoundQuote);

	// Without property rows the file is exactly one line shorter, which is the whole meaning of the flag.
	const FString Lean = UNetLensStatics::BuildCsv(Actors, RPCs, Totals, false);
	TArray<FString> LeanLines;
	Lean.ParseIntoArrayLines(LeanLines);
	TestEqual(TEXT("dropping properties drops exactly the property rows"), LeanLines.Num(), Expected - 1);

	return true;
}

//~ 4. The ceiling ---------------------------------------------------------------------------------------

/**
 * Past the ceiling the table folds instead of growing, and the fold keeps the money.
 *
 * This is the promise the store page makes about NetLens not becoming the problem, tested at the one place
 * where it could quietly stop being true. Two things have to hold: the output is bounded, and the bytes that
 * fell off the end are still in the total. A ranking that dropped them would let the panel report that the
 * bandwidth is fine while four hundred small actors eat it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetLensCeilingTest, "NetLens.Rank.OverflowFoldsIntoOneRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNetLensCeilingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace NetLensTest;

	constexpr int32 ActorCount = 500;
	constexpr int32 Cap = 12;

	TArray<FNetLensActorStat> Input;
	Input.Reserve(ActorCount);

	double ExpectedTotal = 0.0;
	for (int32 Index = 0; Index < ActorCount; ++Index)
	{
		// Descending, so the cap keeps the first twelve and folds the other 488.
		const float Bytes = static_cast<float>(ActorCount - Index);
		Input.Add(MakeActor(*FString::Printf(TEXT("Actor_%03d"), Index), Bytes));
		ExpectedTotal += Bytes;
	}

	TArray<FNetLensActorStat> Ranked;
	UNetLensStatics::RankActors(Input, Cap, ENetLensSort::Bytes, Ranked);

	TestEqual(TEXT("the output is bounded at the cap plus one fold row"), Ranked.Num(), Cap + 1);

	const FNetLensActorStat& Aggregate = Ranked.Last();
	TestTrue(TEXT("the last row is the fold"), Aggregate.bIsAggregate);
	TestEqual(TEXT("the fold counts everything it stands for"), Aggregate.AggregatedActors, ActorCount - Cap);

	double Total = 0.0;
	for (const FNetLensActorStat& Stat : Ranked)
	{
		Total += Stat.BytesPerSecond;
	}

	TestEqual(TEXT("nothing was lost on the way through the ceiling"), Total, ExpectedTotal, 1.0);

	// An aggregate row folded a second time carries its count with it. This is the subsystem's own overflow
	// row - which already stands for the untracked actors - being folded again by the panel's Top N, and it
	// must not collapse to "and 1 more".
	TArray<FNetLensActorStat> Nested;
	Nested.Add(MakeActor(TEXT("Big"), 1000.0f));
	Nested.Add(MakeActor(TEXT("Small"), 10.0f));

	FNetLensActorStat Overflow = MakeActor(TEXT("(388 untracked actors)"), 500.0f);
	Overflow.bIsAggregate = true;
	Overflow.AggregatedActors = 388;
	Nested.Add(Overflow);

	UNetLensStatics::RankActors(Nested, 1, ENetLensSort::Bytes, Ranked);

	TestEqual(TEXT("one kept row and one fold"), Ranked.Num(), 2);
	TestEqual(TEXT("the nested aggregate kept its count"), Ranked[1].AggregatedActors, 389);

	// And the same for RPCs, which have their own ceiling for their own reasons.
	TArray<FNetLensRPCStat> RPCs;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		RPCs.Add(MakeRPC(*FString::Printf(TEXT("Rpc_%02d"), Index), static_cast<float>(40 - Index)));
	}

	TArray<FNetLensRPCStat> RankedRPCs;
	UNetLensStatics::RankRPCs(RPCs, 6, RankedRPCs);

	TestEqual(TEXT("the RPC table is bounded too"), RankedRPCs.Num(), 7);
	TestEqual(TEXT("and the loudest RPC is first"), RankedRPCs[0].FunctionName, FName(TEXT("Rpc_00")));

	return true;
}

//~ 5. The cost model ------------------------------------------------------------------------------------

/**
 * The cost model prices the primitives the way the wire does.
 *
 * A bool is one bit and not one byte. That is not pedantry: a class with sixteen replicated flags and one
 * replicated vector should rank the vector above the flags, and a model that charged a byte per bool would
 * rank them the other way round and send you optimising the wrong thing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetLensCostModelTest, "NetLens.Cost.PricesPrimitivesLikeTheWire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNetLensCostModelTest::RunTest(const FString& /*Parameters*/)
{
	// FProperty instances cannot be conjured standalone, so the model is exercised through the properties of
	// a struct the engine already has. FNetLensPropertyStat is ours, always loaded, and made of exactly the
	// primitives worth checking.
	const UScriptStruct* Struct = FNetLensPropertyStat::StaticStruct();
	if (!TestNotNull(TEXT("the stat struct is loaded"), Struct))
	{
		return false;
	}

	const FProperty* NameProperty = Struct->FindPropertyByName(TEXT("PropertyName"));
	const FProperty* FloatProperty = Struct->FindPropertyByName(TEXT("BytesPerSecond"));

	if (!TestNotNull(TEXT("the name property was found"), NameProperty)
		|| !TestNotNull(TEXT("the float property was found"), FloatProperty))
	{
		return false;
	}

	TestEqual(TEXT("a float is thirty-two bits"),
		UNetLensStatics::EstimateNetPropertyBits(FloatProperty), 32);

	TestTrue(TEXT("a name costs more than a float and less than a kilobyte"),
		UNetLensStatics::EstimateNetPropertyBits(NameProperty) > 32
		&& UNetLensStatics::EstimateNetPropertyBits(NameProperty) < 8192);

	// A bool. FNetLensActorStat has three of them, and the answer has to be one bit.
	const FProperty* BoolProperty = FNetLensActorStat::StaticStruct()->FindPropertyByName(TEXT("bHighlighted"));
	if (TestNotNull(TEXT("the bool property was found"), BoolProperty))
	{
		TestEqual(TEXT("a bool is one bit"), UNetLensStatics::EstimateNetPropertyBits(BoolProperty), 1);
	}

	const FProperty* IntProperty = FNetLensActorStat::StaticStruct()->FindPropertyByName(TEXT("AggregatedActors"));
	if (TestNotNull(TEXT("the int property was found"), IntProperty))
	{
		TestEqual(TEXT("an int is thirty-two bits"),
			UNetLensStatics::EstimateNetPropertyBits(IntProperty), 32);
	}

	// An array priced from a live value uses the real element count; priced from the type alone it assumes
	// one. That difference is what lets NetLens say "the big array is your problem" rather than guess.
	const FProperty* ArrayProperty = FNetLensActorStat::StaticStruct()->FindPropertyByName(TEXT("Properties"));
	if (TestNotNull(TEXT("the array property was found"), ArrayProperty))
	{
		FNetLensActorStat Stat;
		const int32 EmptyBits = UNetLensStatics::EstimateNetValueBits(ArrayProperty, &Stat.Properties);

		Stat.Properties.AddDefaulted(8);
		const int32 FullBits = UNetLensStatics::EstimateNetValueBits(ArrayProperty, &Stat.Properties);

		TestTrue(TEXT("eight elements cost more than none"), FullBits > EmptyBits);
	}

	// The formatter is what every one of those numbers is read through, so it is worth one line.
	TestEqual(TEXT("bytes format as bytes"), UNetLensStatics::FormatBytesPerSecond(512.0f),
		FString(TEXT("512 B/s")));
	TestEqual(TEXT("kilobytes format as kilobytes"), UNetLensStatics::FormatBytesPerSecond(2048.0f),
		FString(TEXT("2.0 KB/s")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
