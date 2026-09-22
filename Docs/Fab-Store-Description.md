# NetLens - Fab Store Description

## Title

NetLens - Replication Diagnostics In The Running Game

## Short description (one line)

Shows live in the running build which actors, properties and RPCs eat your bandwidth - on-screen panel,
per-actor breakdown and CSV export, with no external profiler session.

## Long description

**Epic's network tools are other programs. NetLens is a panel inside your game.**

The Network Profiler records a session and you open the recording afterwards. Networking Insights reads a
trace in a second window. Both are the right tool for a planned investigation, and neither is any use in
the second that actually matters: the second the hitch happens, with your hands on the controller, in a
packaged build on somebody else's machine.

NetLens is that second. It draws a panel inside the running game. At the top is what the net driver really
sent and received. Underneath is the list of actors costing you the most, sorted, with a bar, each openable
into the individual properties that are moving. Below that, the RPCs, with the reliable ones marked. One
command freezes it. One command writes it to CSV.

---

### Every number comes from something

This is the part most tools are vague about, so it is on the front of the listing.

**Update counts are read, not inferred.** NetLens walks the engine's own actor channels every frame and
reads `UActorChannel::LastUpdateTime` - a timestamp the engine writes when it genuinely replicates. An
actor that replicated eleven times in the last second is reported as eleven, not as its NetUpdateFrequency.

**Which properties moved is compared, not assumed.** A shadow copy of each tracked actor's replicated state
is compared against the live one, the same way the replication system itself decides what to send. A
property that never changes never appears, however often its actor is considered.

**Bytes are modelled - and the panel says so.** Each change is priced from its type using the engine's
serialisation rules: a bool is one bit, a quantised vector is sixty, `FRepMovement` is a hundred and
seventy-six. The panel prints NetLens's attributed total right next to the driver's real outgoing rate, so
how much has been accounted for is on screen instead of assumed. There is no packet capture in here and the
documentation never pretends there is.

---

### It cannot become the problem

A diagnostic that costs more than the traffic it was bought to find is worse than none. NetLens has a hard,
visible ceiling on itself:

* **200 actors** tracked in detail - everything past that keeps its exact update count and folds into one
  aggregate row, so the ceiling can hide *which* actor is costing you, never *that* something is
* **64 replicated properties** and **8 replicated components** shadowed per actor
* property breakdown for the **top 5 rows only**
* the per-frame work is one `double` comparison per open channel; everything else runs at 10 Hz

Every one of those numbers is a setting you can see in Project Settings, with a comment saying what it
buys.

---

### What is in the box

* **`UNetLensSubsystem`** - the counter. Ring buffer over the last ten seconds so the table is readable,
  plus a peak column so a one-frame spike is not averaged into invisibility.
* **`ANetLensHUD`** - a HUD that draws the panel. Most projects will not need it: NetLens draws on whatever
  HUD class you already have, through `AHUD::OnHUDPostRender`.
* **`UNetLensComponent`** - hang it on one actor to follow that actor exactly, past the ceiling,
  highlighted. For the actor that goes expensive for half a second and is gone by the time you look.
* **`UNetLensStatics`** - the whole API as Blueprint nodes, plus the ranking, the CSV and the cost model as
  **static, world-free, unit-tested** functions you can call from anywhere.
* **`UNetLensSettings`** - Project Settings → Plugins → NetLens.
* Console commands: `NetLens.Show`, `NetLens.Freeze`, `NetLens.Export`, `NetLens.TopN`, `NetLens.Reset`,
  `NetLens.Stats`.
* **A demo map** with fifty deliberately badly replicated actors and a polite/greedy switch. Flip it and
  watch the rows shoot to the top, with the property breakdown naming the reason.
* **Five automation tests** covering the ranking, the ring buffer, the CSV round trip, the ceiling and the
  cost model.

---

### The CSV

One header line, one line per entry, twelve columns, a `Kind` column telling you whether a line is the
driver, an actor, a property or an RPC. Actors, properties and RPCs come back into a spreadsheet through a
single parse instead of four. Names containing commas and quotes survive the round trip, and the quote-aware
splitter is shipped as a Blueprint node rather than left for you to write.

---

### What NetLens is not

**NetLens does not replicate anything and it does not optimise anything.** It will not throttle an actor,
change a NetUpdateFrequency, mark a property conditional or block a call. It measures and it shows. What to
change is your decision - the tool's job is to make sure it is the right actor.

---

### Requirements and honest limits

* Unreal Engine 5.8, Win64. One runtime module. No UMG in the product, no editor module, no third-party
  code.
* The panel is `UCanvas` from end to end and works in a packaged build with no widget tree.
* **Automatic RPC capture needs a Development or Test build.** The engine's send hook is compiled out of
  Shipping by Epic; NetLens follows that line exactly rather than pretending otherwise, and ships a
  `NoteRPC` node for the Shipping case. Everything else - the panel, the actor rows, the property
  breakdown, the driver totals, freeze and CSV - works in every configuration.
* NetLens filters replicated properties on `CPF_Net`, which is a superset of what any one connection
  receives; conditional properties are counted for every connection. This is written down in the
  documentation, not discovered later.
* The actor rows come from the channel-based replication path. A project on Iris sees the driver totals and
  the RPC rows, and thinner actor rows.

---

## Technical details

**Modules:** 1 (Runtime, `PreDefault`)
**Number of Blueprints:** demo content only (game mode, controller, director, spammer, HUD widget)
**Number of C++ Classes:** 5 public (`UNetLensSubsystem`, `ANetLensHUD`, `UNetLensComponent`,
`UNetLensStatics`, `UNetLensSettings`) plus 4 USTRUCTs and 1 UENUM
**Network Replicated:** No - NetLens observes replication, it does not take part in it
**Supported Development Platforms:** Windows
**Supported Target Build Platforms:** Windows (Win64)
**Documentation:** https://wiki.teufel-engineering.com/en/NetLens/documentation
**Support:** teufelsilvan@gmail.com

## Tags

networking, replication, multiplayer, debug, profiling, diagnostics, bandwidth, optimization, hud, tools

---

© 2026 Silvan Teufel. All rights reserved.
