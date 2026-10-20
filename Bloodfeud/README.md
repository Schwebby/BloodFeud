# Bloodfeud

A Nemesis-style feud and faction-war system for **Skyrim Special/Anniversary Edition**, built as an SKSE plugin. Inspired by nicklovin99's **enRAGE** for Red Dead Redemption 2 — the same idea, rebuilt around Skyrim's bandits, Forsworn, Thalmor, vampires, necromancers and the Silver Hand.

> Status: **early access / untested in-game.** The simulation core is fully unit-tested (see *Building*). The SKSE layer is written against CommonLibSSE-NG and syntax-checked against its headers, but it has not yet been run in a live game. Expect to fix a few things on first launch.

## What it does

| enRAGE (RDR2) | Bloodfeud (Skyrim) |
|---|---|
| Strangers get names, temperaments, traits, trades, faiths, backstories | Every bandit/Forsworn/Thalmor/vampire/necromancer you fight gets a culture-appropriate name, temperament (hot-headed, cowardly, cunning, zealous, honorable…), quirks, a former trade, a god and a backstory. Their in-game name changes to match. |
| NPCs remember how you treated them | Each person keeps memories: wounded by you, fled from you, left for dead, kin slain, spared, humiliated, betrayed you. Hatred, fear and respect follow from those. |
| Push someone too far and they become your nemesis | Enough hatred and they swear a blood-oath (max 10 at a time). Nemeses **ambush you in the wilds**, scale with your level and taunt you about what you did. |
| — | **Cheating death**: a named enemy you "kill" can come back scarred, with a new epithet ("the Burned", "Twice-Dead") and injuries that change their stats. |
| — | **Kin vengeance**: kill someone and a brother, daughter or widow may come looking for you. |
| — | **Nemesis growth**: every escape makes them stronger — new strengths, promotions up the faction ladder. Fire them up enough and they become Fireproof (or learn to fear fire). |
| Eight factions with leaders, resources, territory, alliances and replacements | Eight factions — the Forsworn, Silver Hand, Thalmor Justiciars, Volkihar Thralls, Necromancer Conclave and three bandit clans (Bloodhorn, Ashen Hand, Greymane) — each with a warchief, captains and lieutenants, influence over the nine holds, coin, and relations. They skirmish off-screen, hire sellswords, seize holds, lose officers and appoint successors whether you're involved or not. |
| — | **Dungeon bosses are real officers**: the Bandit Chief at the end of a dungeon is the clan captain you've been hearing about. Kill them and the faction feels it. |
| Interrogations, and prisoners who lie and lure you into an ambush | Badly wounded enemies may **yield**. Interrogate them for the name, hold and weak spot of an officer or nemesis — but cunning or loyal ones lie, and the "lead" is a trap. Then spare or execute them. Spared honorable enemies may leave the life and later send you a tip. |
| Reconcile with enemies by telegram | Send a **weregild** (blood-price) by courier from a nemesis's dossier. Honorable or greedy enemies may accept; proud ones and zealots burn the letter and come for you sooner. |
| World chronicle / newspapers | A **Chronicle** of everything — your feuds, faction wars, successions, territory changes and small life events (someone took to drink, adopted a dog, won a duel). Headlines show as notifications. |

## Requirements

- Skyrim SE 1.5.97 or AE 1.6.x
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

No ESP — it doesn't take a load-order slot.

## Install

Install the built `Bloodfeud` folder with your mod manager (it contains `SKSE/Plugins/Bloodfeud.dll` and `Bloodfeud.ini`), or copy `SKSE/` into `Skyrim Special Edition/Data/`.

Safe to add mid-playthrough. Uninstalling mid-playthrough leaves renamed NPCs renamed and any stat changes already applied.

## Playing

- **F7** opens the Bloodfeud menu: Nemeses, Known enemies (with dossiers), Factions (with hierarchies), the Chronicle, and your Leads.
- Ambushes only happen **outdoors**, out of combat, off horseback and away from towns.
- To follow a lead, travel the wilds of the hold you were told about.
- Dossiers show strengths after you've fought someone twice; weaknesses only after someone gives them up under interrogation.
- Everything is tunable in `Bloodfeud.ini` (cheat-death chance, ambush frequency, escort count, max nemeses, hotkey, detection names…).

Logs: `Documents/My Games/Skyrim Special Edition/SKSE/Bloodfeud.log`.

## Building

### Easiest: let GitHub build it
Push this folder to a GitHub repository. The included workflow (`.github/workflows/build.yml`) runs the tests on Linux and builds the DLL on Windows, then uploads a ready-to-install **Bloodfeud** artifact on the run's page.

### Locally on Windows
1. Install Visual Studio 2022 (Desktop C++), CMake 3.24+, Git, and [vcpkg](https://github.com/microsoft/vcpkg); set `VCPKG_ROOT`.
2. From this folder:
   ```
   cmake --preset release
   cmake --build --preset release
   ctest --preset release
   ```
3. The installable mod is in `dist/Bloodfeud/`.

CommonLibSSE-NG is fetched automatically at a pinned commit.

### Core tests only (any OS)
```
cmake --preset tests && cmake --build --preset tests && ctest --preset tests
```

## Layout

```
api/       Public message API for other mods (BloodfeudAPI.h)
core/      Game-independent simulation (identity, grudges, nemesis, factions, chronicle, save format)
plugin/    SKSE glue: events, spawning, renaming, stat changes, menus, co-save, INI
tests/     Unit, simulation, save/load and fuzz tests for the core; INI parser tests
dist/      Default INI
```

The core never touches the game, so the interesting logic can be tested and tuned without launching Skyrim. The plugin translates game events into core calls and core decisions into game actions.

## For other mods: the Bloodfeud API

Bloodfeud broadcasts through SKSE messaging, so other plugins can react without depending on it. `api/BloodfeudAPI.h` documents the messages:
- **HoldStatus**, sent daily and after a load: each hold's road danger (0 to 1) and which faction controls it.
- **WorldEvent**, sent as things happen: officer slain, hold seized, skirmish, ambush, faction broken.

**Ledger**, the companion economy mod, uses this.

## Known limitations

- **Appearance can change between encounters.** Nemeses respawn from the leveled template they came from, so their face/gear can differ from the first meeting. Their name, history, injuries and stat changes carry over.
- **Ambushers spawn behind you** with a simple offset and drop to the ground; on steep terrain they can land awkwardly.
- **Yielding is a menu, not an animation.** When someone yields the game pauses on a message box (Interrogate / Spare / Execute).
- **Menu button mapping** uses the button-index offset common in SKSE message-box helpers. If picking a button does the wrong thing, that offset in `plugin/src/UI.cpp` is the first place to look.
- **Faction form IDs**: Silver Hand and Necromancer IDs are confirmed; Bandit, Forsworn, Thalmor and Vampire defaults should be checked in xEdit. If one is wrong the log says so and name matching takes over.
- Followers' kills count as yours, which is usually what you want.

## Credits

Concept inspired by **enRAGE** by nicklovin99 (Red Dead Redemption 2). No code or assets from that mod are used. Built on [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG).
