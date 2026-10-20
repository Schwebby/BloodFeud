#!/usr/bin/env python3
"""Assemble an archive Mod Organizer 2 (or Vortex) can install.

Layout produced in dist/MO2/ and zipped to dist/Bloodfeud-and-Ledger-<version>.zip:

    fomod/info.xml
    fomod/ModuleConfig.xml
    Bloodfeud/SKSE/Plugins/Bloodfeud.dll, Bloodfeud.ini
    Ledger/SKSE/Plugins/Ledger.dll, Ledger.ini

The installer page lets the player pick Bloodfeud, Ledger or both.

Usage:  python tools/package_mo2.py [--allow-missing-dll]
(--allow-missing-dll is for testing the layout without a Windows build.)
"""
import argparse
import shutil
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODS = {
    "Bloodfeud": ROOT / "Bloodfeud",
    "Ledger": ROOT / "Ledger",
}


def find_dll(project: Path, name: str) -> Path | None:
    # The CMake post-build step copies the DLL here.
    staged = project / "dist" / name / "SKSE" / "Plugins" / f"{name}.dll"
    if staged.exists():
        return staged
    hits = sorted(ROOT.glob(f"build/**/Release/{name}.dll"))
    return hits[0] if hits else None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--allow-missing-dll", action="store_true")
    args = ap.parse_args()

    info = ET.parse(ROOT / "fomod" / "info.xml").getroot()
    version = info.find("Version").attrib["MachineVersion"]
    config = ET.parse(ROOT / "fomod" / "ModuleConfig.xml").getroot()

    out = ROOT / "dist" / "MO2"
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ROOT / "fomod", out / "fomod")

    missing = []
    for name, project in MODS.items():
        plugins = out / name / "SKSE" / "Plugins"
        plugins.mkdir(parents=True)
        shutil.copy2(project / "dist" / "SKSE" / "Plugins" / f"{name}.ini", plugins / f"{name}.ini")
        dll = find_dll(project, name)
        if dll:
            shutil.copy2(dll, plugins / f"{name}.dll")
        else:
            missing.append(name)

    if missing and not args.allow_missing_dll:
        print(f"error: no built DLL for {', '.join(missing)}. Build on Windows first.", file=sys.stderr)
        return 1

    # Every folder the installer references must exist in the archive.
    for folder in config.iter("folder"):
        if not (out / folder.attrib["source"]).is_dir():
            print(f"error: ModuleConfig references missing folder {folder.attrib['source']}", file=sys.stderr)
            return 1

    archive = ROOT / "dist" / f"Bloodfeud-and-Ledger-{version}.zip"
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(out.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(out).as_posix())
    print(f"wrote {archive.relative_to(ROOT)}")
    for path in sorted(out.rglob("*")):
        if path.is_file():
            print("  " + path.relative_to(out).as_posix())
    if missing:
        print(f"warning: built without DLLs for {', '.join(missing)} (test layout only)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
