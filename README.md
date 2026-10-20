# Bloodfeud and Ledger

Two SKSE plugins for Skyrim SE/AE that work alone or together:

- **Bloodfeud** — Nemesis-style feuds and faction wars, inspired by enRAGE for RDR2. See `Bloodfeud/README.md`.
- **Ledger** — a living economy that reacts to Bloodfeud's outlaw wars, the civil war, dragons and seasons, with investments. See `Ledger/README.md`.

This folder builds both plugins and packages them into **one archive that Mod Organizer 2 installs directly**, with a FOMOD installer page to choose Bloodfeud, Ledger or both. Vortex reads the same archive.

## Getting the MO2 archive

The plugins are compiled C++ and have to be built with Microsoft's compiler on Windows. The included GitHub workflow does that for free, so you don't need to install anything.

1. **Make a GitHub repository.** Sign in at github.com, choose **New repository**, give it a name and create it.
2. **Upload this folder's contents.** The easiest way is [GitHub Desktop](https://desktop.github.com/): add this folder as a local repository, then publish it to your new repository. Make sure the hidden `.github` folder goes up too; the build lives there.
3. **Wait for the build.** Open the repository's **Actions** tab and wait for the **build** run to show a green check. It takes about 10–20 minutes the first time.
4. **Download the archive.** Open the finished run. Under **Artifacts**, download **Bloodfeud-and-Ledger-MO2**. That zip is the mod.

Optional: push a tag like `v0.1.0` and the archive is also attached to a GitHub Release.

## Installing in Mod Organizer 2

1. **Install the requirements.**
   - [SKSE64](https://skse.silverlock.org/): copy its files into the game folder, and run the game through `skse64_loader.exe` from MO2's executable list.
   - [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444): install it as a normal mod.
2. **Install the archive.** In MO2, click **Install a new mod from archive** (the CD-and-box icon), pick `Bloodfeud-and-Ledger-MO2.zip`, and choose what to install on the installer page.
3. **Enable it.** Tick the new mod in the left pane. There's no ESP, so nothing to sort in the plugin list.
4. **Play.** Launch with SKSE. **F7** opens Bloodfeud and **F8** opens Ledger.

To change settings, edit `SKSE/Plugins/Bloodfeud.ini` or `Ledger.ini` inside the mod. In MO2, right-click the mod, choose **Information**, then **Text Files** or **Filetree**.

To check it loaded, look in `Documents/My Games/Skyrim Special Edition/SKSE/` for `Bloodfeud.log` and `Ledger.log`.

## Building locally instead

On Windows with Visual Studio 2022, CMake and vcpkg (`VCPKG_ROOT` set):

```
cmake --preset release
cmake --build --preset release
ctest --preset release
python tools/package_mo2.py
```

The archive is written to `dist/Bloodfeud-and-Ledger-0.1.0.zip`.

Tests for the simulation cores run on any OS:

```
cmake --preset tests && cmake --build --preset tests && ctest --preset tests
```

## Status

Early access. Both simulation cores are fully unit-tested, and the plugin code is checked against the CommonLibSSE-NG headers. Neither plugin has been run in a live game yet, so please report what breaks (the logs above help).
