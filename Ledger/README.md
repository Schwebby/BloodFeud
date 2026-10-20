
# Ledger

A living economy for **Skyrim Special/Anniversary Edition**, built as an SKSE plugin. Every hold has its own market that reacts to what happens in Skyrim, and you can invest in its businesses. It runs on its own and becomes much more reactive alongside **Bloodfeud**.

> Status: **early access / untested in-game.** The economy simulation is fully unit-tested. The SKSE layer is written against CommonLibSSE-NG and syntax-checked against its headers, but it hasn't been run in a live game yet.

## What it does

**Nine markets, eight kinds of goods.** Food, drink, ore, arms, alchemy, lumber, pelts and luxuries are produced, consumed and stocked separately in each hold. Whiterun's farms feed Skyrim, the Rift brews mead, Dawnstar and Markarth mine, Falkreath and Morthal cut timber, and Solitude and Windhelm import luxuries by sea. Goods flow between neighbouring holds along the roads, and prices come from supply and demand.

**It reacts to the world.**

| Event | Effect |
|---|---|
| Dangerous roads (from Bloodfeud, or estimated without it) | Caravans stop, so imports dry up and prices rise. Production suffers, merchants get poorer, and outlaws raid businesses. |
| Killing a Bloodfeud captain or warlord | That hold's roads get safer, and trade and prices recover. |
| A bandit clan seizing a hold | Unrest, and merchants charge more. |
| Legion vs Stormcloak fighting | Arms and potions get dearer, food runs short, and trade stalls. |
| A dragon attacking a town | Panic prices, empty markets, and damaged or destroyed businesses. |
| A merchant dying | The hold is short of coin and goods for a while. |
| Seasons | Food is cheap after the harvest and dear in winter; firewood and pelts are needed in winter. |
| Your own trading | Dump fifty iron daggers in Riften and arms prices there crash, then slowly recover as goods flow out. |

**What you see in game:**
- **Prices change by hold and by category** while you trade, both when buying and when selling.
- **Merchants' gold** rises and falls with local prosperity.
- **Merchants' stock** grows with what's plentiful and shrinks with what's scarce.
- **News** appears as notifications, for example "Supplies of ore are running short in Whiterun Hold (+54%)."

**Investing.** Twenty businesses are open for investment, including Honningbrew and Black-Briar Meaderies, Riverwood Mill, Quicksilver and Iron-Breaker Mines, Cidhna Mine, the East Empire Company warehouse and Hollyfrost Farm.
- **Buying:** buy shares from a hold's stewards while you're in one of its towns.
- **Dividends:** these arrive weekly by courier. Their size follows the business's real output and the local price of what it makes.
- **Share prices:** these move with the business's profit and condition.
- **Controlling stake:** owning more than half raises your dividend to 60% of profit and lets you **hire guards** against raids.
- **Risk:** outlaws can raid and burn businesses and dragons can wreck them. Ruined businesses are rebuilt after a couple of weeks if the hold calms down. Buying after a disaster and selling after recovery is a real strategy.

## Requirements

- Skyrim SE 1.5.97 or AE 1.6.x
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Optional: **Bloodfeud**, which provides road danger and outlaw wars.

No ESP. Safe to add mid-playthrough.

## Playing

- **F8** opens the Ledger menu: Local market, All markets, Invest here, My holdings, News.
- Market reports show each good's price relative to normal (+40%, -25%), whether it's rising or falling, and the hold's road danger, war and dragon levels.
- Tune everything in `Ledger.ini`: how strong price changes are (set `fPriceStrength = 0` to keep vanilla prices but keep the rest), merchant gold, stock shifting, fees, raid chance and the hotkey.

Logs: `Documents/My Games/Skyrim Special Edition/SKSE/Ledger.log`.

## How it changes prices without engine hooks

When you start talking to a merchant, Ledger works out the local multiplier for every item in your inventory and theirs, and temporarily rescales those items' values. When the dialogue and barter menus close, the original values are restored. Skyrim's own barter maths, perks and Speech skill all still apply on top. Potions and ingredients use their cost-override field the same way.

## Building

The same as Bloodfeud:
- **GitHub:** push the folder to a repository. The workflow tests it and builds a ready-to-install **Ledger** artifact.
- **Windows:** `cmake --preset release && cmake --build --preset release && ctest --preset release`. The mod lands in `dist/Ledger/`.
- **Core tests on any OS:** `cmake --preset tests && cmake --build --preset tests && ctest --preset tests`.

## How it talks to Bloodfeud

Through SKSE messaging (`api/BloodfeudAPI.h`). Bloodfeud broadcasts each hold's road danger and controlling faction once a day, plus events as they happen: officers slain, holds seized, skirmishes. Ledger listens. Neither mod needs the other installed.

## Known limitations

- **Untested price display timing.** Prices, gold and stock are adjusted when dialogue with a merchant opens, so the barter list should be built with them. If the first barter after opening dialogue shows unadjusted numbers, that timing is the first thing to look at in `plugin/src/Game.cpp`.
- **Stock shifting** only adds copies of items the merchant already carries, or removes some of them. It doesn't invent new stock.
- **Clothing and books** aren't part of any market and keep vanilla prices.
- **Civil war detection** uses the Legion and Stormcloak soldier factions (IDs confirmed on UESP). It sees battles where they happen, not which side holds a city.
- **Menu button mapping** uses the community-documented message-box offset (see `plugin/src/UI.cpp`).
