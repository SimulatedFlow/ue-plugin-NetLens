# NetLens — Documentation

**Replication diagnostics inside the running game.**

Unreal Engine 5.8 · Win64 · one runtime module · no UMG in the product, no editor module, no third-party code

---

## Contents

1. [What NetLens is, and is not](#1-what-netlens-is-and-is-not)
2. [Supported engine and platforms](#2-supported-engine-and-platforms)
3. [Installation](#3-installation)
4. [Quick start](#4-quick-start)
5. [The demo map](#5-the-demo-map)
6. [Reading the panel](#6-reading-the-panel)
7. [Class overview](#7-class-overview)
8. [Blueprint API](#8-blueprint-api)
9. [C++ examples](#9-c-examples)
10. [The NetLens component](#10-the-netlens-component)
11. [Console commands](#11-console-commands)
12. [Where every number comes from](#12-where-every-number-comes-from)
13. [The cost model](#13-the-cost-model)
14. [The ceiling NetLens puts on itself](#14-the-ceiling-netlens-puts-on-itself)
15. [CSV export format](#15-csv-export-format)
16. [Project settings reference](#16-project-settings-reference)
17. [Build configurations, and the one thing Shipping cannot do](#17-build-configurations-and-the-one-thing-shipping-cannot-do)
18. [Automation tests](#18-automation-tests)
19. [Troubleshooting](#19-troubleshooting)

---

## 1. What NetLens is, and is not

NetLens is a magnifying glass on replication that draws inside the running game. It answers three questions
without you leaving the build:

* Which actors are costing the most bandwidth right now?
* Which of their replicated properties is doing it?
* Which RPCs are being called, how often, and which of them are reliable?

**NetLens measures and shows. It does not replicate anything and it does not optimise anything.** It will
not throttle an actor, will not change a `NetUpdateFrequency`, will not mark a property conditional and will
not block a call. Every decision stays yours. The tool's only job is to make sure the decision you make is
about the right actor.

It is also not a packet capture. Read [section 12](#12-where-every-number-comes-from) before you quote a
number in a bug report — the difference between what NetLens measures and what it models is written down, in
public, on purpose.

---

## 2. Supported engine and platforms

| | |
|---|---|
| **Engine version** | Unreal Engine **5.8** (`"EngineVersion": "5.8.0"`) |
| **Supported development platforms** | Windows |
| **Supported target build platforms** | **Win64** (`PlatformAllowList: [ "Win64" ]`) |
| **Modules** | 1 — `NetLens`, type `Runtime`, loading phase `PreDefault` |
| **Module dependencies** | Public: `Core`, `CoreUObject`, `Engine`, `NetCore`, `DeveloperSettings`. Private: `RenderCore`. |
| **Third-party code** | None |
| **Editor module** | None — everything in this plugin ships |
| **UMG** | Not used by the product. The panel is `UCanvas` from end to end, so it survives into a packaged build with no widget tree. (The demo map's button strip is UMG; that is demo content, not product.) |
| **Build configurations** | Development, Test, Shipping and Editor all build and run. One feature is Development/Test only — see [section 17](#17-build-configurations-and-the-one-thing-shipping-cannot-do). |
| **Network role** | Works on a listen server, a dedicated server and a client. NetLens measures the net driver of the world it lives in. |
| **Replicated** | No. NetLens observes replication; it does not take part in it. |

`LoadingPhase` is `PreDefault` so the console commands are registered before any game module starts
spawning things worth measuring.

---

## 3. Installation

### From the Fab library

1. Install NetLens for Unreal Engine 5.8 from the Epic Games Launcher.
2. Open your project → **Edit → Plugins → Network → NetLens** → tick **Enabled**.
3. Restart the editor when prompted.

### From a folder

1. Copy the `NetLens` folder into your project's `Plugins` directory, so you have
   `<YourProject>/Plugins/NetLens/NetLens.uplugin`.
2. Right-click your `.uproject` → **Generate Visual Studio project files** (C++ projects only).
3. Open the project. Confirm the compile prompt if you are asked.

### Using NetLens from your own C++ module

Add the module to your `Build.cs`. Nothing else is required — there is no subsystem to register, no actor to
place and no settings you must fill in before the plugin works.

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "NetLens",
});
```

`NetCore` is a **public** dependency of NetLens, so a consuming module that includes NetLens headers does not
have to add it separately.

---

## 4. Quick start

**Five minutes, from a fresh install to a diagnosis.**

1. **Set PIE to a networked mode.** This is the step people skip, and skipping it makes NetLens look broken.
   In the editor toolbar, open the dropdown next to the Play button:
   * **Net Mode → Play As Listen Server**
   * **Number of Players → 2**

   Without a second player there is no net driver, nothing replicates, and NetLens will honestly print
   *"no net driver in this world (standalone game: nothing is replicating)"* rather than an empty table.

2. **Press Play.**

3. **Press `~` for the console and type:**

   ```
   NetLens.Show 1
   ```

   The panel appears in the top left. It draws on whatever HUD class your project already uses, through
   `AHUD::OnHUDPostRender`, so you do not have to reparent anything.

4. **Read the top row of the actor table.** That is the actor costing you the most bytes per second right
   now. Its property breakdown, indented underneath, names the individual property responsible.

5. **Sort the other way** to find the second kind of problem:

   ```
   NetLens.TopN 20
   ```

   and click **SORT UPDATES** in the demo, or call `Set Sort By (Updates Per Second)`. An actor sending
   thirty updates a second with four bytes of change in each is not a payload problem, it is an update-rate
   problem, and it is fixed somewhere completely different.

6. **Freeze and export** when you see something:

   ```
   NetLens.Freeze
   NetLens.Export
   ```

   Freezing does **not** pause the game. The traffic carries on while you read the numbers from the moment
   the hitch happened. `NetLens.Export` writes a timestamped CSV under `Saved/NetLens/` and prints the path
   to the log.

### In a packaged build

Nothing extra. Open the console in a Development or Test build and type the same commands. In a Shipping
build the console may be unavailable, so bind a key to `Set Show Overlay` from the Blueprint library — and
read [section 17](#17-build-configurations-and-the-one-thing-shipping-cannot-do) about RPCs.

---

## 5. The demo map

`Content/NetLens/Maps/L_NetLensDemo` — in the content browser, `/NetLens/NetLens/Maps/L_NetLensDemo`.

> **Open it with PIE set to Play As Listen Server with 2 players.** The map is a replication demo; with one
> player it has nothing to show, and that is the single most common reason a first run looks empty. The map's
> World Settings carry the game mode, but the PIE net mode is an editor preference and no asset can set it
> for you. The map ships with two player starts (`PlayerStart` and `PlayerStart_Client`) so the second
> player has somewhere to arrive.

Everything is under `Content/NetLens/` in a single pack folder.

| Asset | What it does |
|---|---|
| `Maps/L_NetLensDemo` | The map. Floor, sky, lighting, three landing pads, two player starts. |
| `Blueprints/BP_NetLensDemoGameMode` | Sets the pawn, controller and HUD classes below. Referenced from World Settings. |
| `Blueprints/BP_NetLensDemoHUD` | Reparented to **`ANetLensHUD`**. This is why the panel is on screen with no console command in the demo. |
| `Blueprints/BP_NetLensDemoController` | On the local controller: enables `net.EnableNetStats`, creates the button strip, adds it to the viewport, shows the cursor and sets input mode to UI-only. |
| `Blueprints/BP_NetLensDemoPawn` | A `Character` for the two players to possess. |
| `Blueprints/BP_NetLensFleetDirector` | Placed once. On BeginPlay, on the server only, it spawns **`DroneCount`** drones (default **240**). |
| `Blueprints/BP_NetLensDrone` | The cheap actor, in bulk. Replicates `CargoLevel`, `RouteIndex`, `BeaconOn` and its orbit. |
| `Blueprints/BP_NetLensCargoHauler` | The expensive actor. Three are placed by hand: `CargoHauler_Alpha`, `_Bravo`, `_Charlie`. Carries a **NetLens Probe**. |
| `Blueprints/BP_NetLensChatterTower` | The chatty actor. Two are placed: `RelayMast_North`, `RelayMast_South`. Carries a **NetLens Probe**. |
| `UI/WBP_NetLensDemoPanel` | The button strip. Each button is exactly one call into `UNetLensStatics`. |
| `Materials/M_NetLensSurface` + five instances | Ground, pads, drones, haulers, towers. |

### What the demo is arranged to prove

**1 — The property breakdown names the real culprit.** `BP_NetLensCargoHauler` replicates seven properties
and rebuilds several of them every single tick on the server: a `TArray<FString>` crew roster of twelve
entries with two of them rewritten per frame, an `FString` manifest code rebuilt per frame, an `FVector`
docking target, plus a float throttle, a float hull integrity, a bool for the bay doors and its own movement.
Three haulers are enough to sit at the top of a table containing two hundred and forty other replicated
actors. Open the row and the breakdown says *`CrewRoster`* and *`ManifestCode`* — not "your bandwidth is
high", but *this property, on this actor, this many times a second*.

**2 — The ceiling folds rather than drops.** The director spawns **240 drones** on purpose. Add the three
haulers and the two towers and the map has **245 replicated actors**, against a default `MaxTrackedActors` of
**200**. So the demo map always produces the grey aggregate row — the one that reads *"(N more actors)"* with
a real count and a real summed cost. That row is the visible half of the promise in
[section 14](#14-the-ceiling-netlens-puts-on-itself): the ceiling can hide *which* actor is costing you, never
*that* something is.

**3 — A probe pins an actor past the ceiling.** The haulers and the towers each carry a `UNetLensComponent`,
so they are tracked and drawn highlighted whatever else the two hundred and forty drones are doing.

**4 — Bytes and updates are different questions.** `BP_NetLensChatterTower` moves a small amount of data
often. Sort by bytes and it is unremarkable; sort by updates and it climbs. That is the whole reason
`ENetLensSort::Updates` exists.

### The button strip

| Button | The one call behind it |
|---|---|
| **FREEZE** | `Freeze Net Lens` |
| **RESUME** | `Resume Net Lens` |
| **SORT BYTES** | `Set Sort By (Bytes Per Second)` |
| **SORT UPDATES** | `Set Sort By (Updates Per Second)` |
| **EXPORT CSV** | `Export Net Lens Csv` — the status line prints the path it actually wrote to |
| **RESET** | `Reset Net Lens` |
| **HIDE PANEL** / **SHOW PANEL** | `Set Show Overlay` |

The status line under the buttons explains what each one just did, so the demo reads without this document
open next to it.

The UMG widget in the demo is demo content, not product. The product's own panel has no UMG in it at all —
that is deliberate, because the build where you most need these numbers is the one where the widget tree is
the thing you are least sure about.

---

## 6. Reading the panel

```
NetLens  |  SERVER  |  1 connection  |  window 10s
OUT 138.4 KB/s  32 pkt/s      IN 4.1 KB/s  30 pkt/s      loss 0%   ping 24 ms
attributed 121.6 KB/s actors + 6.2 KB/s rpc   |   tracked 200/200 of 245   |   245 channels

  #  ACTOR                             BYTES/s       PEAK    UPD/s  CONN
  1  CargoHauler_Alpha                41.2 KB/s  88.0 KB/s     30.0     1
       CrewRoster (BP_NetLensCargoHauler_C) 30.1 KB/s  1.1 KB     29.8
       ManifestCode (BP_NetLensCargoHauler_C) 8.7 KB/s   296 B     29.8
       DockingTarget (BP_NetLensCargoHauler_C) 0.4 KB/s    12 B     29.8
  2  CargoHauler_Bravo                38.7 KB/s  81.2 KB/s     30.0     1
  ...
 13  (45 more actors)                  9.1 KB/s   1.2 KB/s     12.0     1

RPC  FUNCTION                          BYTES/s   PER CALL   CALL/s   REL
     Multicast_Bang                    4.8 KB/s     120 B     40.0    no
     Server_RequestFire                1.4 KB/s      36 B     38.0   yes
```

**Line 1** — which end you are on, how many connections, and the length of the averaging window. `[FROZEN]`
appears here when the measurement is held.

**Line 2** — the net driver's own counters. These are the engine's numbers, not NetLens's.

**Line 3** — what NetLens accounted for, against line 2; how many actors are tracked against the ceiling and
how many were seen at all; and the number of open actor channels.

If line 3's attributed total is close to line 2's outgoing rate, the table below is the whole story. If it
is far below, the difference is handshakes, acknowledgements, NetGUID traffic, initial actor state and
anything your game sends by a route NetLens does not model — and you know to stop hunting for a missing
actor row. **That gap being visible is the point of printing both.**

**The actor rows.** Sorted by bytes per second by default. The bar behind each row is that row's share of
the attributed total; rows above 12% turn amber and above 30% turn red. A row in pale blue is one that a
`UNetLensComponent` pinned. The last row, in grey, is the aggregate — it stands for every actor past the
ceiling and the count in its name is real.

**PEAK** is the busiest half-second that actor has had inside the window, expressed as a rate. A row whose
peak is many times its average is a row that spikes; a row whose peak is close to its average is a steady
cost. Those are different problems with different fixes, which is why both columns are there.

**UPD/s against a class's `NetUpdateFrequency`** is the other half of the diagnosis. An actor sending 30
updates a second with four bytes of change in each is not a payload problem, it is an update-rate problem.

**The property rows** under the top few actors name the individual replicated properties that moved,
sorted by cost. The class in brackets is where the property is *declared* — very often not the class you
would have gone looking in. The middle column is what one change of it is priced at; the right-hand column
is how often it really changed.

**The RPC rows.** `REL` is the column to look at when the bytes look fine and the game still feels like
treacle: a reliable RPC holds a slot in the channel's outgoing reliable queue until it is acknowledged, and
a queue that fills stalls everything behind it.

---

## 7. Class overview

Five public classes, four `USTRUCT`s and one `UENUM`. Nothing has to be placed in the level for NetLens to
work.

| Type | Base | What it is |
|---|---|---|
| `UNetLensSubsystem` | `UTickableWorldSubsystem` | The counter. One per world. Scans channels, compares shadows, prices changes, holds the windows, draws the panel. |
| `ANetLensHUD` | `AHUD` | A HUD that draws the panel, for projects that prefer being explicit. Most projects do not need it. |
| `UNetLensComponent` | `UActorComponent` | *NetLens Probe.* Hang it on one actor to follow that actor exactly, past the ceiling, highlighted. |
| `UNetLensStatics` | `UBlueprintFunctionLibrary` | *Net Lens.* The whole API as Blueprint nodes, plus the ranking, CSV and cost model as static, world-free functions. |
| `UNetLensSettings` | `UDeveloperSettings` | Project Settings → Plugins → NetLens. |
| `FNetLensActorStat` | `USTRUCT` | One actor's row, with its property breakdown. |
| `FNetLensPropertyStat` | `USTRUCT` | One replicated property that actually changed, and what it cost. |
| `FNetLensRPCStat` | `USTRUCT` | One replicated function, aggregated over the window. |
| `FNetLensTotals` | `USTRUCT` | The header line: the driver's numbers next to NetLens's attributed ones. |
| `ENetLensSort` | `UENUM` | `Bytes`, `Updates`, `Name`. |
| `FNetLensWindow` | plain struct | The ring buffer. Not reflected, deliberately — it is the hottest thing in the plugin and every field it does not have is a field the property table does not walk. |

### `UNetLensSubsystem` — the C++ surface

```cpp
// Control
void  SetShowOverlay(bool bShow);
bool  IsShowingOverlay() const;
void  Freeze();
void  Resume();
bool  IsFrozen() const;
void  ResetMeasurements();
bool  ExportCsv(const FString& Path, FString& OutPath);
void  SetTopN(int32 InTopN);
int32 GetTopN() const;
void  SetSortBy(ENetLensSort InSortBy);
ENetLensSort GetSortBy() const;

// Reading
const TArray<FNetLensActorStat>& GetActorStats() const;   // unranked, uncapped
const TArray<FNetLensRPCStat>&   GetRPCStats() const;
const FNetLensTotals&            GetTotals() const;
bool  FindActorStat(const AActor* Actor, FNetLensActorStat& OutStat) const;
void  LogSummary() const;

// Recording
void  NoteRPC(AActor* Actor, FName FunctionName);         // for Shipping builds

// Drawing
void  DrawOverlay(UCanvas* Canvas, const FVector2D& Origin, float Width) const;
int32 GetOverlayLineCount() const;
```

`GetActorStats()` returns the raw tracked set. To get what the panel draws — sorted, capped, with the
overflow folded — pass it through `UNetLensStatics::RankActors`.

### `ANetLensHUD`

| Property | Default | |
|---|---|---|
| `bDrawNetLensPanel` | `true` | Off stops *this class* being one of the draw routes. It does not hide the panel; the any-HUD route may still be drawing it. |
| `bShowPanelOnBeginPlay` | `true` | The reason to place this HUD is usually that you want the panel. |
| `PanelOrigin` | `(-1, -1)` | Negative on either axis uses the project setting. |
| `PanelWidth` | `0` | Zero or less uses the project setting. |

Using `ANetLensHUD` **and** the any-HUD delegate at once is safe: both go through one function that refuses
to draw twice in a frame.

---

## 8. Blueprint API

Everything is on **`Net Lens`** (`UNetLensStatics`), so nothing has to be placed in the level. Nodes with a
world context work from any Blueprint that has one.

### Control

| Node | |
|---|---|
| `Get Net Lens` | The world's subsystem, or null outside a game world. |
| `Set Show Overlay` | Show or hide the panel. |
| `Is Showing Overlay` | |
| `Freeze Net Lens` / `Resume Net Lens` / `Toggle Freeze Net Lens` | |
| `Is Net Lens Frozen` | |
| `Reset Net Lens` | Clears everything and unfreezes. |
| `Export Net Lens Csv` | Returns success **and** the path it actually wrote to — print that on screen; a tool that says "exported" without saying where has not finished the job. |
| `Set Top N` / `Set Sort By` | |

### Reading

| Node | |
|---|---|
| `Get Top Actors` | The ranked, capped table with the overflow folded, exactly as the panel draws it. |
| `Get Top RPCs` | |
| `Get Net Lens Totals` | The header line as a struct. |
| `Get Actor Stat` | One actor's row, whether or not it is in the top rows. |
| `Get Stat Actor` | The actor behind a row (the row holds a weak pointer, which Blueprint has no pin for). |
| `Note RPC` | Record an RPC by hand. See [section 17](#17-build-configurations-and-the-one-thing-shipping-cannot-do). |

### Maths — static, world-free, unit-tested

| Node | |
|---|---|
| `Rank Actors` | Sort, cap, fold. The function the whole product rests on. |
| `Rank RPCs` | |
| `Build Csv` / `Get Csv Header` / `Split Csv Line` | |
| `Format Bytes` / `Format Bytes Per Second` | One place, so the panel and the log never disagree about a number. |

These take arrays and integers and touch nothing else, which is why they have tests. A
`UTickableWorldSubsystem` cannot be constructed in an automation test — it needs a world — so logic that
lives inside one is logic that never gets tested. That is why the arithmetic is out here.

---

## 9. C++ examples

All of these compile against `#include "NetLensStatics.h"`, `"NetLensSubsystem.h"`, `"NetLensComponent.h"`
and `"NetLensTypes.h"`.

### Bind the panel to a debug key

```cpp
#include "NetLensStatics.h"

void ADebugPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ADebugPlayerController::ToggleNetLens);
    InputComponent->BindKey(EKeys::F10, IE_Pressed, this, &ADebugPlayerController::FreezeNetLens);
}

void ADebugPlayerController::ToggleNetLens()
{
    const bool bShowing = UNetLensStatics::IsShowingOverlay(this);
    UNetLensStatics::SetShowOverlay(this, !bShowing);
}

void ADebugPlayerController::FreezeNetLens()
{
    // Freezing holds the table. It does not pause the game - the traffic carries on
    // while you read the numbers from the moment the hitch happened.
    const bool bNowFrozen = UNetLensStatics::ToggleFreezeNetLens(this);
    UE_LOG(LogTemp, Log, TEXT("NetLens %s"), bNowFrozen ? TEXT("frozen") : TEXT("running"));
}
```

### Read the table and log the worst offender

```cpp
#include "NetLensStatics.h"
#include "NetLensTypes.h"

void ULagReport::LogWorstActor(const UObject* WorldContext)
{
    const FNetLensTotals Totals = UNetLensStatics::GetNetLensTotals(WorldContext);
    if (!Totals.bHasNetDriver)
    {
        UE_LOG(LogTemp, Warning, TEXT("Not a networked world - nothing to measure."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("driver OUT %s, NetLens attributed %s across %d tracked of %d seen"),
        *UNetLensStatics::FormatBytesPerSecond(Totals.DriverOutBytesPerSecond),
        *UNetLensStatics::FormatBytesPerSecond(Totals.AttributedActorBytesPerSecond),
        Totals.TrackedActors, Totals.SeenActors);

    const TArray<FNetLensActorStat> Top = UNetLensStatics::GetTopActors(WorldContext, 5);
    for (const FNetLensActorStat& Row : Top)
    {
        if (Row.bIsAggregate)
        {
            // The fold. Never silently dropped - this row stands for everything past the ceiling.
            UE_LOG(LogTemp, Log, TEXT("  ... and %d more actors, together %s"),
                Row.AggregatedActors, *UNetLensStatics::FormatBytesPerSecond(Row.BytesPerSecond));
            continue;
        }

        UE_LOG(LogTemp, Log, TEXT("  %s (%s)  %s  %.1f upd/s (asked for %.1f)"),
            *Row.ActorName.ToString(), *Row.ClassName.ToString(),
            *UNetLensStatics::FormatBytesPerSecond(Row.BytesPerSecond),
            Row.UpdatesPerSecond, Row.NetUpdateFrequency);

        for (const FNetLensPropertyStat& Prop : Row.Properties)
        {
            UE_LOG(LogTemp, Log, TEXT("      %s declared on %s: %s, %.1f changes/s"),
                *Prop.PropertyName.ToString(), *Prop.OwnerClassName.ToString(),
                *UNetLensStatics::FormatBytesPerSecond(Prop.BytesPerSecond),
                Prop.ChangesPerSecond);
        }
    }
}
```

### Watch one specific actor from its own code

```cpp
#include "NetLensStatics.h"

void AMySuspiciousActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    FNetLensActorStat Stat;
    if (UNetLensStatics::GetActorStat(this, this, Stat))
    {
        // Works whether or not this actor is inside the top rows.
        if (Stat.PeakBytesPerSecond > 4.0f * Stat.BytesPerSecond && Stat.BytesPerSecond > 0.0f)
        {
            UE_LOG(LogTemp, Warning, TEXT("%s is spiky: avg %s, peak %s"),
                *GetName(),
                *UNetLensStatics::FormatBytesPerSecond(Stat.BytesPerSecond),
                *UNetLensStatics::FormatBytesPerSecond(Stat.PeakBytesPerSecond));
        }
    }
}
```

### Pin an actor past the ceiling with a probe

```cpp
#include "NetLensComponent.h"

AMySuspiciousActor::AMySuspiciousActor()
{
    // Exempt from MaxTrackedActors, never the one evicted, drawn highlighted on the panel.
    Probe = CreateDefaultSubobject<UNetLensComponent>(TEXT("NetLensProbe"));
    Probe->DisplayLabel = TEXT("The one I am suspicious of");
}

void AMySuspiciousActor::PrintCost() const
{
    // Note: IsTrackedByNetLens, NOT IsRegistered - UActorComponent already has one of those
    // and it means something completely different.
    if (Probe && Probe->IsTrackedByNetLens())
    {
        UE_LOG(LogTemp, Log, TEXT("%s"), *Probe->GetCostText());   // "12.4 KB/s  (30.0 upd/s)"
    }
}
```

### Export a CSV and read it back

```cpp
#include "NetLensStatics.h"
#include "Misc/FileHelper.h"

void UBugReport::AttachNetLensCsv(const UObject* WorldContext)
{
    FString WrittenPath;
    if (!UNetLensStatics::ExportNetLensCsv(WorldContext, TEXT("bugreport.csv"), WrittenPath))
    {
        UE_LOG(LogTemp, Error, TEXT("NetLens CSV export failed."));
        return;
    }

    // Always tell the user where it went.
    UE_LOG(LogTemp, Log, TEXT("NetLens CSV written to %s"), *WrittenPath);

    // And it reads back - the quote-aware splitter ships with the plugin.
    TArray<FString> Lines;
    FFileHelper::LoadFileToStringArray(Lines, *WrittenPath);

    check(Lines.Num() > 0 && Lines[0] == UNetLensStatics::GetCsvHeader());

    for (int32 i = 1; i < Lines.Num(); ++i)
    {
        TArray<FString> Fields;
        UNetLensStatics::SplitCsvLine(Lines[i], Fields);

        if (Fields.Num() > 0 && Fields[0] == TEXT("Property"))
        {
            // Kind, Name, Class, Owner, BytesPerSecond, ...
            UE_LOG(LogTemp, Log, TEXT("property %s on %s: %s B/s"),
                *Fields[1], *Fields[3], *Fields[4]);
        }
    }
}
```

### Rank a table yourself, with no world at all

`RankActors` is the function the whole product rests on, and it is a pure static so you can call it from a
commandlet, a test or a tools build.

```cpp
#include "NetLensStatics.h"

TArray<FNetLensActorStat> Rows = LoadRowsFromSomewhere();

TArray<FNetLensActorStat> Ranked;
UNetLensStatics::RankActors(Rows, /*TopN=*/12, ENetLensSort::Bytes, Ranked);

// Ranked has at most 13 entries: the top 12, plus one trailing row with
// bIsAggregate == true whose AggregatedActors says how many it stands for.
// Not one byte is lost on the way through the ceiling.
```

Passing `TopN <= 0` means no cap and no aggregate row.

### Record an RPC in a Shipping build

```cpp
#include "NetLensStatics.h"

void AMyTurret::Multicast_Fire_Implementation(const FVector& Target)
{
    // Automatic capture uses the engine's send hook, which Epic compiles out of Shipping.
    // One line at the top of the body gives you the row there too.
    UNetLensStatics::NoteRPC(this, this, TEXT("Multicast_Fire"));

    PlayFireEffects(Target);
}
```

### Price a property yourself

The cost model is public C++ (not a `UFUNCTION` — `FProperty` is not a Blueprint type and never will be).

```cpp
#include "NetLensStatics.h"

// From the type alone: containers are priced at one element.
const FProperty* Prop = FindFProperty<FProperty>(AMyActor::StaticClass(), TEXT("Inventory"));
const int32 BitsFromType = UNetLensStatics::EstimateNetPropertyBits(Prop);

// From the live value: the array's real length is used, which is what makes
// "the big array is your problem" a thing NetLens can actually say.
const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(MyActorInstance);
const int32 BitsFromValue = UNetLensStatics::EstimateNetValueBits(Prop, ValuePtr);
```

---

## 10. The NetLens component

Add **NetLens Probe** (`UNetLensComponent`) to an actor to follow that actor exactly.

The subsystem measures the busiest two hundred actors, which is the right answer to "what is eating my
bandwidth" and the wrong answer to "what is *this* actor costing" — because the actor you are suspicious of
is often not in the top two hundred. It is the one that goes expensive for half a second when the player
does something, and by the time you look it is gone again.

The component makes its actor exempt from the ceiling, never the one evicted, and drawn in a distinct
colour on the panel. It costs one shadow buffer, the same as any tracked actor.

| Member | |
|---|---|
| `bAlwaysTrack` | On. Off leaves the component as a convenient way to read the actor's row from its own Blueprint without pinning it. |
| `DisplayLabel` | A name for the panel, when the instance name is a number. |
| `Is Tracked By Net Lens` | **Not** called `IsRegistered` — `UActorComponent` already has one of those and it means something else entirely. |
| `Get Net Lens Stat` | The full row with its property breakdown. |
| `Get Bytes Per Second` / `Get Updates Per Second` / `Get Peak Bytes Per Second` | |
| `Get Cost Text` | `"12.4 KB/s  (30.0 upd/s)"`, ready for a widget. Returns `"not measured"` rather than `"0 KB/s"` — nothing measured and nothing costing are different states. |

Nothing about the component replicates and nothing about it changes how the actor replicates. It is an
instruction to the measuring, not a change to the thing measured.

---

## 11. Console commands

| Command | What it does |
|---|---|
| `NetLens.Show [0\|1]` | Draw the panel. No argument toggles. |
| `NetLens.Freeze [0\|1]` | Hold the table still. **Does not pause the game** — the point is that the traffic carries on while you read the numbers from the moment the hitch happened. No argument toggles. |
| `NetLens.Export [path]` | Write the table to CSV. No argument writes a timestamped file under `Saved/NetLens/`. Relative paths are relative to the same place; absolute paths are used as given. The destination is printed to the log. |
| `NetLens.TopN <n>` | How many actor rows the panel prints. No argument prints the current value. |
| `NetLens.Reset` | Clear every measurement and start from an empty window. |
| `NetLens.Stats` | Print the current table to the log — useful over a remote console or in a server log. |

---

## 12. Where every number comes from

The engine gives a plugin no public hook on the bunch writer. `UActorChannel::ReplicateActor` returns the
bit count of an update, but the engine calls it, not you. NetLens therefore does not claim to capture
packets. What it does instead, and what each part of it is worth:

### Measured

**Update counts.** Every frame, NetLens walks the open actor channels of every connection and reads
`UActorChannel::LastUpdateTime` — a timestamp the engine writes when it genuinely replicates that actor to
that connection. When it moves, that is one real update.

The scan runs **every frame**, not at the sample rate, and that is deliberate: a server replicates a given
actor at most once per frame, so a per-frame scan catches every update and a slower one would merge two
into one.

**Which properties changed.** When an actor's channel has moved, its replicated properties are compared
against a shadow copy of their previous values and only what differs is charged. This is the same decision
`FRepLayout` makes, against the same kind of shadow buffer, for the same reason: a property that has not
changed is not in the bunch, however often its actor is considered.

**Relevant connections.** The number of open actor channels for that actor.

**Driver totals.** `UNetDriver::OutBytesPerSecond`, `InBytesPerSecond`, packets, packet loss, and
`UNetConnection::AvgLag` for the ping. Entirely the engine's numbers.

**RPC calls.** `UNetDriver::SendRPCDel` is the engine's own send hook, and NetLens takes it in Development
and Test builds. Whatever was bound before is kept and called, so a project running NetcodeUnitTest is not
disturbed, and `bBlockSendRPC` is never touched — NetLens is a lens, not a valve.

### Modelled

**Bytes.** Each changed property is priced from its type ([section 13](#13-the-cost-model)), and every update
additionally pays `BunchOverheadBytes`, whether or not anything in it changed.

That last part is not a rounding convenience. An actor that replicates thirty times a second and changes
nothing genuinely costs thirty bunch headers, and that cost is exactly the one a `NetUpdateFrequency` fix
removes. Charging zero for it would hide the single most common replication mistake there is.

### Known limits, stated plainly

* **Conditional properties.** NetLens filters on `CPF_Net`, which is a superset of what any one connection
  receives: a `COND_OwnerOnly` property is flagged the same way as an unconditional one. Getting at the
  conditions would mean reaching into `FRepLayout`'s private state. A figure that is right for the owning
  connection and generous for the others is far more useful than no figure.
* **Initial state.** The first time an actor becomes relevant to a connection carries its full state and is
  usually much more expensive than its steady cost. NetLens records that first sight without charging for
  it, so an actor spawning does not look like a bandwidth spike.
* **Iris.** The numbers above come from the channel-based replication path. A project running the Iris
  replication system will see the driver totals and the RPC rows, and thinner actor rows.
* **Aggregate rows.** Actors past the ceiling have their updates counted exactly and their bytes priced at
  the average cost of a tracked update. The count is a measurement; the bytes on that one row are an
  average.

---

## 13. The cost model

`UNetLensStatics::EstimateNetPropertyBits` (from a type) and `EstimateNetValueBits` (from a live value).
Both are public statics, both are unit-tested, and both are callable from your own code.

| Type | Bits | Why |
|---|---|---|
| `bool` | **1** | The replication system packs bools into the bunch bit by bit. A model that charged a byte would rank sixteen flags above a vector. |
| `uint8`, `int8`, enum | 8 | An enum uses its underlying type's width. |
| `int16`, `uint16` | 16 | |
| `int32`, `uint32`, `float` | 32 | Floats go as floats even in a double-precision engine. |
| `int64`, `uint64`, `double` | 64 | |
| `FName` | 48 | A table index plus a number suffix, once both ends have seen it. |
| `FString` | 32 + 8·(len+1) | Length prefix, characters, terminator. From the live value where there is one. |
| `FText` | 8 + 32 + 8·(assumed+1) | Priced at the assumed string length; see `AssumedStringLength`. An `FText` replicated every frame is a mistake worth pointing at even when it cannot be priced exactly. |
| object / class / soft / interface reference | 48 | A NetGUID for an object both ends already know about — not a pointer and not a path. |
| `TArray`, `TSet` | 32 + n·element | `n` is the real element count when the value is in hand, otherwise 1. |
| `TMap` | 32 + n·(key+value) | |
| `FVector` | 96 | Three floats. The wire format did not grow when the in-memory one did. |
| `FVector_NetQuantize` / `10` / `100` / `Normal` | 60 / 70 / 80 / 48 | What the quantised serialisers actually write. |
| `FRotator` | 48 | Three compressed shorts. |
| `FQuat` 128 · `FTransform` 240 · `FColor` 32 · `FLinearColor` 128 | | |
| `FRepMovement` | 176 | **The one that matters most.** `AActor::ReplicatedMovement` is this, so it is on the critical path of every moving actor in every project: quantised location, compressed rotation, quantised velocity, flags. |
| `FGameplayTag` | 32 | An index into the shared tag table, not a name. |
| any other `USTRUCT` | sum of members | Right for a plain struct. Inside a struct there is no `CPF_Net` filtering — a struct is serialised whole or not at all. |
| anything unrecognised | size in memory · 8 | Always an over-estimate, which is the safe direction: it draws attention to something unusual rather than hiding it. |

Two deliberate simplifications, both documented rather than hidden: array elements are priced by type
rather than walked one by one (walking a thousand-element array every sample would make NetLens the thing
it was bought to find, and would move the row by a few per cent at most), and a struct nested more than
four deep is priced by its size in memory.

---

## 14. The ceiling NetLens puts on itself

A diagnostic that becomes the problem is worse than no diagnostic. Every limit is a visible setting under
**Project Settings → Plugins → NetLens → Budget**:

| Setting | Default | What it buys |
|---|---|---|
| `MaxTrackedActors` | **200** | The cost of a sample is bounded by this times `MaxPropertiesPerObject`, whatever the level does. A hundred spawned projectiles cannot turn the diagnostic into the bottleneck. |
| `MaxPropertiesPerObject` | 64 | Covers every hand-written replicated class and most Blueprints. |
| `MaxReplicatedComponentsPerActor` | 8 | Components matter: the most expensive property in a typical project is not on the Character, it is on its movement component. |
| `MaxTrackedRPCs` | 128 | Keyed by function, so the set is bounded by how many replicated functions your game has, not by how many actors call them. |
| `PropertyBreakdownRows` | 5 | Only these rows carry a property list out of the sampler. |
| `SamplesPerSecond` | 10 | How often the table is rebuilt. The channel scan is separate and runs every frame — it is one `double` comparison per open channel. |

**Actors past the ceiling are not dropped, they are folded.** They keep their exact update count (one
timestamp per open channel and nothing else) and appear as one aggregate row. So the ceiling can hide
*which* actor is costing you; it can never hide *that* something is. A tool that silently dropped them
would one day report that your bandwidth is fine while four hundred small actors ate it.

An actor carrying a `UNetLensComponent` is exempt from the ceiling and is never the one evicted.

---

## 15. CSV export format

One header line, then exactly one line per entry, in this order: two driver rows, every actor row, every
property row (when `bExportPropertyRows` is on), every RPC row. Twelve columns on every line, UTF-8 with no
byte order mark, CRLF endings, fields quoted with inner quotes doubled.

```
Kind,Name,Class,Owner,BytesPerSecond,PeakBytesPerSecond,EventsPerSecond,BytesPerEvent,TotalBytes,Connections,Reliable,Count
```

| Column | `Driver` | `Actor` / `Aggregate` | `Property` | `RPC` |
|---|---|---|---|---|
| `Name` | `Out` / `In` | actor name | property name | function name |
| `Class` | `NetDriver` | actor class | the actor's class | the function's class |
| `Owner` | – | – | class the property is declared on | `Multicast` when it is one |
| `BytesPerSecond` | driver rate | | | |
| `PeakBytesPerSecond` | **what NetLens attributed in that direction** | busiest bucket | – | – |
| `EventsPerSecond` | packets/s | updates/s | changes/s | calls/s |
| `BytesPerEvent` | ping in ms | bytes per update | bytes per change | bytes per call |
| `Connections` | connection count | relevant connections | | connection count |
| `Reliable` | 0 | 0 | 0 | 1 when reliable |
| `Count` | channels / actors seen | actors folded into the row | 0 | calls in the window |

The two `Driver` rows re-use `PeakBytesPerSecond` and `BytesPerEvent` for the attributed total and the ping
rather than adding two columns that would be blank on every other line. That keeps the far end to a single
parse, which is the whole reason the `Kind` column exists.

`Split Csv Line` is shipped rather than kept in the tests, because "and it reads back" is half of what an
export is worth and you should not have to write a quote-aware splitter to collect on it.

---

## 16. Project settings reference

**Project Settings → Plugins → NetLens**

### Measurement

| Setting | Default | |
|---|---|---|
| `WindowSeconds` | 10 | Roughly how long it takes to notice a hitch, say "what was that", and look at the screen. |
| `BucketsPerWindow` | 20 | Half a second each. The resolution of the peak column and the granularity at which the window forgets. |
| `SamplesPerSecond` | 10 | How often the table is rebuilt. Update counts stay exact at any value — they come from a timestamp, not from how many samples saw them. |
| `bTrackProperties` | on | Off keeps the actor rows and drops the breakdown. |
| `bTrackRPCs` | on | |

### Budget

See [section 14](#14-the-ceiling-netlens-puts-on-itself).

### Cost Model

| Setting | Default | |
|---|---|---|
| `BunchOverheadBytes` | 11 | Channel index, sequence, flags, fragment header. An assumption you can see and change is worth four you cannot. |
| `RPCOverheadBytes` | 4 | Function index and channel header. |
| `AssumedStringLength` | 16 | Only for values priced from their type alone — RPC parameters. Actor properties are priced from the live value, where the length is known exactly. |

### Presentation

| Setting | Default | |
|---|---|---|
| `bShowOverlayByDefault` | off | NetLens is a tool you reach for, not a HUD element. |
| `bAutoDrawOverlayOnAnyHUD` | on | Draws through `AHUD::OnHUDPostRender` so a project keeps its own HUD class. The alternative is asking a shipping project to reparent its HUD to see a number. |
| `OverlayOrigin` / `OverlayWidth` | (28, 60) / 620 | |
| `TopN` | 12 | |
| `PropertyBreakdownRows` / `PropertiesPerRow` | 5 / 5 | |
| `TopRPCRows` | 6 | Zero hides the RPC section. |
| `SortBy` | Bytes | |

### Export

| Setting | Default | |
|---|---|---|
| `CsvSubdirectory` | `NetLens` | Relative to `Saved/`, on purpose. An absolute path in a project setting is a path that exists on one machine and then gets committed. |
| `bExportPropertyRows` | on | |

---

## 17. Build configurations, and the one thing Shipping cannot do

Everything in NetLens works in every build configuration **except automatic RPC capture**.

`UNetDriver::SendRPCDel` — the engine's own send hook — is declared inside `#if !UE_BUILD_SHIPPING`. Epic
compiles it out of Shipping builds and there is no supported replacement. NetLens follows that line exactly
rather than pretending otherwise:

| | Development / Test | Shipping |
|---|---|---|
| Panel, actor rows, property breakdown | yes | yes |
| Driver totals, freeze, CSV export | yes | yes |
| RPC rows, automatic | yes | **no** |
| RPC rows, from `Note RPC` | yes | yes |

In a Shipping build the RPC section says so rather than showing an empty table. To get RPC numbers there,
call `Note RPC` from the replicated functions you care about — one node, or one line, at the top of the
function body. Parameters are then priced from the function's declared signature rather than from the live
values, which is the same number for everything but variable-length arrays and strings.

When NetLens takes the hook it keeps whatever was bound before and calls it, and it never touches
`bBlockSendRPC`. A project running NetcodeUnitTest keeps working.

---

## 18. Automation tests

Session Frontend → Automation → **NetLens**. Five tests, all of which run without a world.

| Test | What it holds |
|---|---|
| `NetLens.Rank.SortsAndCaps` | The ranking sorts by each of the three orders and never returns more rows than it was asked for. |
| `NetLens.Window.ForgetsOlderThanTheWindow` | The ring buffer forgets what falls out of the window, decays to zero, keeps the peak separate from the average, and behaves identically after a gap longer than the whole ring. |
| `NetLens.Csv.HeaderPlusOneLinePerEntry` | Header plus exactly one line per entry, twelve columns on every line, and a name containing a comma and a name containing quotes both survive the round trip. |
| `NetLens.Rank.OverflowFoldsIntoOneRow` | Five hundred actors capped at twelve give thirteen rows, the fold carries the full count including a nested aggregate's, and **not one byte is lost on the way through the ceiling**. |
| `NetLens.Cost.PricesPrimitivesLikeTheWire` | A bool is one bit, an int is thirty-two, an array priced from a live value beats one priced from its type, and the formatter agrees with itself. |

They run without a world, because every function they test was written not to need one.

---

## 19. Troubleshooting

**The panel says "no net driver in this world".**
The world is not networked. In PIE set Net Mode to Play As Listen Server and Number of Players to 2. In a
standalone game there is genuinely nothing to measure and NetLens is telling you so rather than showing an
empty table.

**The panel is empty but there is a net driver.**
Nothing has replicated inside the window yet. Move something, or wait a second. If the channel count on
line 3 is zero, no actor is relevant to any connection — check `bReplicates` and relevancy.

**The attributed total is far below the driver's outgoing rate.**
Expected in three cases: the first seconds after connecting (initial actor state, which NetLens
deliberately does not charge), a game that sends a lot through custom channels or `FFastArraySerializer`
deltas, or a project on the Iris replication system. If it persists in steady state, something in your game
is sending data by a route NetLens does not model — which is itself worth knowing before you spend a day
tuning the wrong actor.

**An actor I care about is not in the table.**
Either it is past the ceiling — the aggregate row will be there instead — or it is not relevant to any
connection. Add a **NetLens Probe** component to it and it is followed regardless.

**The property breakdown is empty on a row that clearly costs something.**
The breakdown is only produced for the top `PropertyBreakdownRows` rows. Raise the setting, or sort so the
row you want is near the top. If it is empty on the top row too, check `bTrackProperties`.

**Numbers jump about.**
Lower them by raising `WindowSeconds`. If a row's PEAK is far above its BYTES/s, the jumping is real and
the actor is spiky — that is a finding, not noise.

**Nothing in the RPC section, in a packaged build.**
If it is a Shipping build, that is [section 17](#17-build-configurations-and-the-one-thing-shipping-cannot-do)
and it is expected. In Development or Test, check `bTrackRPCs`, and check the log for the line NetLens writes
when it takes the hook.

**A Blueprint recompile in PIE and the numbers reset for that class.**
Reinstancing gives the actor a new class, so its shadow is rebuilt from scratch. The window keeps its
history; the next update is measured against the new layout.

**The demo map shows the panel but no rows.**
The demo runs its fleet on the server only (`HasAuthority`). If you started PIE as a client-only session,
there is no authority in that world. Use **Play As Listen Server** with **2 players**.

---

© 2026 Silvan Teufel. All rights reserved.
Support: teufelsilvan@gmail.com
