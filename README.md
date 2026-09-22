# NetLens - Replication Diagnostics In The Running Game

**Which actors, which properties and which RPCs are eating your bandwidth - on screen, in the build you
are playing, with no profiler session to record and no second window to open.**

Unreal Engine 5.8 · C++ code plugin · one runtime module · Win64

---

## The gap this fills

Epic's network tooling is excellent and all of it lives somewhere else. The Network Profiler records a
session and you open the recording afterwards. Networking Insights reads a trace in a second application.
Both are the right tool for a planned investigation.

Neither is any use in the second that actually matters: the second the hitch happens, with your hands on
the controller, in a packaged build on somebody else's machine. That second is what NetLens is for.

NetLens draws a panel inside the running game. At the top is what the net driver really sent and received.
Underneath is the list of actors that cost the most, sorted, with a bar, openable into the individual
properties that are moving. Below that, the RPCs. One key freezes it. One key writes it to CSV.

**NetLens measures and shows. It does not replicate anything and it does not optimise anything.** What to
change is your decision - the tool's job is to make sure you are changing the right thing.

## What is measured and what is modelled

This distinction is the plugin, so it is on the front page rather than buried in an appendix.

| | Where it comes from |
|---|---|
| Updates per second, per actor | **Measured.** Read from `UActorChannel::LastUpdateTime`, per connection - a timestamp the engine writes when it genuinely replicates. |
| Which properties changed | **Measured.** A shadow copy of the actor's replicated state compared against the live one, the same way `FRepLayout` decides what to send. |
| Relevant connections | **Measured.** Open actor channels for that actor. |
| Driver in/out rate, packets, loss, ping | **Measured by the engine.** Read straight off `UNetDriver` and `UNetConnection`. |
| RPC calls and reliability | **Measured** in Development and Test builds, through the engine's own send hook. |
| Bytes per second | **Modelled** from the three measured things above, by pricing each changed property from its type. |

The panel prints the attributed total next to the driver's real total, so how much NetLens has accounted
for is on screen rather than assumed. There is no packet capture in here and the documentation never
pretends there is.

## The ceiling

A diagnostic that becomes the problem it was bought to find is worse than no diagnostic. NetLens has a hard
ceiling on itself, and every number in it is a visible setting:

* **200 actors** tracked in detail. Everything past that has its updates counted and is reported as one
  aggregate row - so the ceiling can hide *which* actor is costing you, never *that* something is.
* **64 replicated properties** and **8 replicated components** shadowed per actor.
* **Property breakdown for the top 5 rows only** - the breakdown is what you read once you know which row
  you care about.
* The channel scan is one `double` comparison per open channel per frame. Everything else runs at 10 Hz.

## Classes

| Class | What it is |
|---|---|
| `UNetLensSubsystem` | The counter. A tickable world subsystem: scans channels, compares shadows, prices changes, draws the panel. |
| `ANetLensHUD` | An `AHUD` that draws the panel, for projects that prefer being explicit. Most projects do not need it - NetLens draws on any HUD through `AHUD::OnHUDPostRender`. |
| `UNetLensComponent` | Hang it on one actor to follow that actor exactly, past the ceiling, highlighted. |
| `UNetLensStatics` | Blueprint library. Show, freeze, export, read the table - plus the ranking, CSV and cost model as **static, world-free** functions. |
| `UNetLensSettings` | Project Settings → Plugins → NetLens. Window, ceilings, cost model, panel layout, CSV folder. |

## Console commands

```
NetLens.Show   [0|1]     draw the panel (no argument toggles)
NetLens.Freeze [0|1]     hold the table still without pausing the game
NetLens.Export [path]    write the table to CSV
NetLens.TopN   <n>       how many actor rows to print
NetLens.Reset            clear every measurement
NetLens.Stats            print the table to the log
```

## Getting started

1. Enable the plugin, restart the editor.
2. Play in Editor with **Net Mode: Play As Listen Server** and **Number of Players: 2** - without a second
   player nothing replicates and the panel will honestly tell you there is no net driver.
3. Press `~` and type `NetLens.Show 1`.

The full walkthrough, the CSV format, the cost model's rules and the demo map are in
[`Docs/DOCUMENTATION.md`](Docs/DOCUMENTATION.md).

## Requirements

Unreal Engine 5.8, Win64. No UMG in the product, no editor module, no third-party code, no external
profiler session.

Automatic RPC capture uses the engine's send hook, which Epic compiles out of Shipping builds; in Shipping,
`NoteRPC` records them by hand. Everything else works identically in every build configuration.

---

© 2026 Silvan Teufel. All rights reserved.
