// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NetLensTypes.h"
#include "NetLensStatics.generated.h"

class AActor;
class UFunction;
class UNetLensSubsystem;
class FProperty;

/**
 * Everything NetLens can do, reachable from a Blueprint node with nothing placed in the level.
 *
 * Two halves, and the split is not cosmetic.
 *
 * The first half forwards to the world's subsystem: show, freeze, reset, export, read the table. A demo
 * button strip, a debug menu or a bug-report button calls these and nothing else.
 *
 * The second half is the arithmetic - the ranking, the ceiling, the CSV, the cost model - and it is
 * **static, pure and world-free**. That is the lesson every plugin in this line has had to learn once: a
 * UTickableWorldSubsystem cannot be NewObject'd in an automation test because it needs a world, so any logic
 * that lives inside one is logic that is never tested. Here the maths sits in functions that take an array
 * and an integer, and the tests exercise the same code the sampler runs rather than a second copy of it that
 * agrees with the first until somebody edits one of the two.
 */
UCLASS(meta = (DisplayName = "Net Lens"))
class NETLENS_API UNetLensStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The subsystem -----------------------------------------------------------------------------------

	/** The NetLens subsystem for the context object's world, or null outside a game world. */
	UFUNCTION(BlueprintPure, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static UNetLensSubsystem* GetNetLens(const UObject* WorldContextObject);

	/** Show or hide the panel. Same thing NetLens.Show does. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static void SetShowOverlay(const UObject* WorldContextObject, bool bShow);

	/** Whether the panel is being drawn. */
	UFUNCTION(BlueprintPure, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static bool IsShowingOverlay(const UObject* WorldContextObject);

	/**
	 * Stop measuring and hold the table where it is.
	 *
	 * The reason this exists rather than "pause the game" is that pausing the game stops the traffic too, and
	 * what you wanted was to read the numbers from the moment the hitch happened - which are gone half a
	 * second later. Freezing keeps them and lets the game carry on.
	 */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static void FreezeNetLens(const UObject* WorldContextObject);

	/** Start measuring again. The window keeps whatever was in it; it does not restart empty. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static void ResumeNetLens(const UObject* WorldContextObject);

	/** Toggle between frozen and running. What a single button on a debug HUD wants. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static bool ToggleFreezeNetLens(const UObject* WorldContextObject);

	/** Whether the measurement is frozen. */
	UFUNCTION(BlueprintPure, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static bool IsNetLensFrozen(const UObject* WorldContextObject);

	/** Throw away every measurement and start from an empty window. Also unfreezes. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static void ResetNetLens(const UObject* WorldContextObject);

	/**
	 * Write the current table to a CSV.
	 *
	 * An empty path writes a timestamped file into the project's Saved folder, under the subdirectory named
	 * in the settings. A relative path is taken relative to the same place. An absolute path is used as
	 * given. OutPath comes back with wherever it actually went, which is the thing you want to print on
	 * screen - a tool that says "exported" and does not say where has not finished the job.
	 */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static bool ExportNetLensCsv(const UObject* WorldContextObject, const FString& Path, FString& OutPath);

	/** The actor table as the panel would draw it: ranked, capped at TopN, overflow folded into one row. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNetLensActorStat> GetTopActors(const UObject* WorldContextObject, int32 TopN = 10);

	/** The RPC table, ranked by bytes per second. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static TArray<FNetLensRPCStat> GetTopRPCs(const UObject* WorldContextObject, int32 TopN = 10);

	/** The header line: what the driver reports, and how much of it NetLens has accounted for. */
	UFUNCTION(BlueprintPure, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static FNetLensTotals GetNetLensTotals(const UObject* WorldContextObject);

	/** What one particular actor is costing, whether or not it is in the top rows. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static bool GetActorStat(const UObject* WorldContextObject, AActor* Actor, FNetLensActorStat& OutStat);

	/** How many rows the panel prints. Same thing NetLens.TopN does. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static void SetTopN(const UObject* WorldContextObject, int32 TopN);

	/** How the table is sorted. */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static void SetSortBy(const UObject* WorldContextObject, ENetLensSort SortBy);

	/**
	 * Record an RPC by hand.
	 *
	 * Not needed in a Development or Test build, where NetLens takes the engine's own send hook and sees
	 * every RPC without being told. It is needed in Shipping, where Epic compiles that hook out: call this
	 * from the replicated function itself and the row appears exactly as it would have. The parameters cannot
	 * be priced from here, so the function's declared parameter list is used instead - which is the same
	 * number for everything but variable-length arrays and strings.
	 */
	UFUNCTION(BlueprintCallable, Category = "NetLens", meta = (WorldContext = "WorldContextObject"))
	static void NoteRPC(const UObject* WorldContextObject, AActor* Actor, FName FunctionName);

	/** The actor behind a stat row, while it is alive. Null on an aggregate row. */
	UFUNCTION(BlueprintPure, Category = "NetLens")
	static AActor* GetStatActor(const FNetLensActorStat& Stat);

	//~ The arithmetic ----------------------------------------------------------------------------------

	/**
	 * Sort, cap, and fold the remainder into one row.
	 *
	 * This is the function the whole product rests on and the reason it is a static taking an array. Three
	 * things happen here, in this order, and the order is the specification:
	 *
	 *   1. The input is sorted by SortBy, descending for the two numeric sorts.
	 *   2. The first TopN entries are kept as they are.
	 *   3. Everything after them is summed into a single trailing row with bIsAggregate set, whose
	 *      AggregatedActors says how many it stands for.
	 *
	 * The fold is the point. A diagnostic that shows the top twelve and silently drops the rest is a
	 * diagnostic that will one day tell you your bandwidth is fine when four hundred projectiles are eating
	 * it, because none of the four hundred was individually big enough to make the list. The aggregate row
	 * makes that case impossible to miss: it is the row that says "and 388 more, together 91 KB/s".
	 *
	 * TopN at or below zero means no cap and no aggregate row.
	 */
	UFUNCTION(BlueprintCallable, Category = "NetLens|Maths")
	static void RankActors(const TArray<FNetLensActorStat>& Actors, int32 TopN, ENetLensSort SortBy,
		TArray<FNetLensActorStat>& OutRanked);

	/** The same treatment for RPCs: sorted by bytes per second, capped, remainder folded into one row. */
	UFUNCTION(BlueprintCallable, Category = "NetLens|Maths")
	static void RankRPCs(const TArray<FNetLensRPCStat>& RPCs, int32 TopN, TArray<FNetLensRPCStat>& OutRanked);

	/**
	 * The whole table as a CSV.
	 *
	 * One header line, then exactly one line per entry, in this order: the driver row, every actor row,
	 * every property row of every actor when bIncludeProperties is set, then every RPC row. Every line has
	 * the same twelve columns and a Kind column saying which of the four kinds it is, so the file comes back
	 * into a spreadsheet or a script through a single parse instead of four.
	 *
	 * Fields are quoted and inner quotes doubled, because an actor called BP_Turret,Left has ended more than
	 * one export before.
	 */
	UFUNCTION(BlueprintPure, Category = "NetLens|Maths")
	static FString BuildCsv(const TArray<FNetLensActorStat>& Actors, const TArray<FNetLensRPCStat>& RPCs,
		const FNetLensTotals& Totals, bool bIncludeProperties = true);

	/** The header line BuildCsv writes. Exposed so a reader can check a file is one of ours before parsing. */
	UFUNCTION(BlueprintPure, Category = "NetLens|Maths")
	static FString GetCsvHeader();

	/**
	 * Split one CSV line into its fields, honouring quotes and doubled inner quotes.
	 *
	 * Shipped rather than kept in the tests, because "and it is readable again" is half of what an export is
	 * worth and a customer should not have to write this to collect on it.
	 */
	UFUNCTION(BlueprintCallable, Category = "NetLens|Maths")
	static void SplitCsvLine(const FString& Line, TArray<FString>& OutFields);

	/** "12.4 KB/s" and so on. One place, so the panel and the log never disagree about a number. */
	UFUNCTION(BlueprintPure, Category = "NetLens|Maths")
	static FString FormatBytesPerSecond(float BytesPerSecond);

	/** "12.4 KB". The same rounding, without the rate. */
	UFUNCTION(BlueprintPure, Category = "NetLens|Maths")
	static FString FormatBytes(float Bytes);

	//~ The cost model ----------------------------------------------------------------------------------
	//
	// Not UFUNCTIONs: FProperty is not a Blueprint type and never will be. They are public statics because
	// the automation tests call them, and because a project doing something unusual - pricing a struct it is
	// about to add to a replicated array, say - has as much right to the answer as the sampler does.

	/**
	 * What one change of this property is priced at, in bits, from its type alone.
	 *
	 * The rules follow the engine's own serialisation: a bool is one bit, a byte or an enum is eight, an int
	 * is thirty-two, a float is thirty-two and a double sixty-four. Object references are priced at a NetGUID
	 * rather than at a pointer. Structs the engine gives a hand-written NetSerialize - the quantised vectors,
	 * FRotator, FRepMovement - are priced at what that implementation writes; every other struct is summed
	 * from its members. Anything the model does not recognise falls back to its size in memory, which is
	 * always an over-estimate and is marked as such in the documentation rather than quietly presented as a
	 * measurement.
	 *
	 * Containers whose length is not knowable from the type - arrays, sets, maps, strings - are priced at one
	 * element and the assumed string length. Use EstimateNetValueBits instead wherever the value is in hand;
	 * everything NetLens does to actor properties goes through that one.
	 */
	static int32 EstimateNetPropertyBits(const FProperty* Property);

	/**
	 * The same price, with the live value in hand.
	 *
	 * The difference is the containers: an array of forty elements is priced at forty, a string of two
	 * hundred characters at two hundred. This is what makes "the big array is your problem" a thing NetLens
	 * can actually say rather than guess at.
	 *
	 * ValuePtr points at the value itself, not at the object that owns it.
	 */
	static int32 EstimateNetValueBits(const FProperty* Property, const void* ValuePtr);

	/**
	 * The parameter payload of one RPC, in bits.
	 *
	 * Parms may be null, in which case the parameters are priced from their types. Return and out parameters
	 * are skipped: an RPC does not send them back, whatever the signature says.
	 */
	static int32 EstimateFunctionParameterBits(const UFunction* Function, const void* Parms);
};
