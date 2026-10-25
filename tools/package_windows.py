#!/usr/bin/env python3
"""Build and check the Windows download archive: the program, every DLL it
loads that Windows does not provide (the Visual C++ runtime included), its
assets and its licenses; then the archive, as downloaded, is checked again."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

from package_unix import ROOT, copy, project_files

# Names that Windows resolves itself (API sets).
API_SET = re.compile(r"^(?:api|ext)-ms-", re.IGNORECASE)
# Runtimes that are never part of Windows, even when another program left a
# copy in System32 (the build machines have Visual Studio's): Microsoft's
# Visual C++ runtime and MinGW's.
REDISTRIBUTABLE = re.compile(
    r"^(?:vcruntime|msvcp|concrt|vccorlib|vcomp|mfc|ucrtbased|libgcc|libstdc\+\+|libwinpthread)", re.IGNORECASE)


def imports(binary: Path) -> list[str]:
    """The DLLs that a program or DLL loads: at start (its import table), or
    on the first call into them (its delay-load table)."""
    data = binary.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError(f"Not a Windows binary: {binary}")
    header = struct.unpack_from("<I", data, 0x3C)[0]
    if data[header:header + 4] != b"PE\0\0":
        raise ValueError(f"Not a Windows binary: {binary}")
    sections, optional_size = struct.unpack_from("<H", data, header + 6)[0], struct.unpack_from("<H", data, header + 20)[0]
    optional = header + 24
    wide = struct.unpack_from("<H", data, optional)[0] == 0x20B  # PE32+ (64-bit)
    image_base = struct.unpack_from("<Q" if wide else "<I", data, optional + (24 if wide else 28))[0]
    directory_count = struct.unpack_from("<I", data, optional + (108 if wide else 92))[0]
    directories = optional + (112 if wide else 96)
    section_table = optional + optional_size

    def offset(rva: int) -> int:
        for index in range(sections):
            size, address, raw_size, raw = struct.unpack_from("<IIII", data, section_table + 40 * index + 8)
            if address <= rva < address + max(size, raw_size):
                return rva - address + raw
        raise ValueError(f"{binary}: address 0x{rva:x} outside its sections")

    def name(rva: int) -> str:
        start = offset(rva)
        return data[start:data.index(b"\0", start)].decode("ascii")

    names: list[str] = []
    # Directory 1: 20-byte import descriptors, the name at 12. Directory 13:
    # 32-byte delay-load descriptors, the name at 4, an address unless their
    # first bit says it is relative.
    for directory, size, field in ((1, 20, 12), (13, 32, 4)):
        if directory >= directory_count:
            continue
        rva = struct.unpack_from("<I", data, directories + 8 * directory)[0]
        if not rva:
            continue
        entry = offset(rva)
        while any(descriptor := data[entry:entry + size]):
            target = struct.unpack_from("<I", descriptor, field)[0]
            if directory == 13 and not struct.unpack_from("<I", descriptor, 0)[0] & 1:
                target -= image_base
            names.append(name(target))
            entry += size
    return names


def missing(folder: Path, system: Path) -> dict[str, list[str]]:
    """Each DLL loaded by a program or DLL of `folder` that is neither beside
    it nor part of Windows, with what loads it. The DLLs found beside the
    program are checked in turn, so the whole chain is."""
    binaries = sorted(path for path in folder.iterdir() if path.suffix.lower() in (".exe", ".dll"))
    shipped = {path.name.lower() for path in binaries if path.suffix.lower() == ".dll"}
    absent: dict[str, list[str]] = {}
    for binary in binaries:
        for dll in imports(binary):
            if dll.lower() in shipped or API_SET.match(dll):
                continue
            if (system / dll).is_file() and not REDISTRIBUTABLE.match(dll):
                continue
            absent.setdefault(dll, []).append(binary.name)
    return absent


def vcpkg_licenses(folder: Path, installed: Path, destination: Path) -> dict:
    """The license of each library that vcpkg provided, found from the list of
    files each of its packages installed ("sdl2_2.32.10-1_x64-windows.list"),
    and its version. A DLL without one stops the build; the Visual C++ runtime
    comes with Visual Studio, whose terms allow it."""
    owners: dict[str, tuple[str, str]] = {}
    for listing in (installed / "vcpkg" / "info").glob("*.list"):
        package, version = listing.name.split("_")[:2]
        for line in listing.read_text(encoding="utf-8").splitlines():
            if line.lower().endswith(".dll"):
                owners[Path(line).name.lower()] = (package, version)
    records: dict = {}
    for dll in sorted(path for path in folder.glob("*.dll")):
        if REDISTRIBUTABLE.match(dll.name):
            records.setdefault("visual-cpp-runtime", {"source": "Visual Studio, Microsoft.VC14x.CRT",
                                                      "files": []})["files"].append(dll.name)
            continue
        package, version = owners.get(dll.name.lower(), ("", ""))
        if not package:
            raise RuntimeError(f"No license found for {dll.name}")
        records.setdefault(package, {"version": version, "source": f"https://vcpkg.io/en/package/{package}",
                                     "files": []})["files"].append(dll.name)
        notice = next(installed.glob(f"*/share/{package}/copyright"), None)
        if not notice:
            raise RuntimeError(f"No license found for {dll.name} ({package})")
        copy(notice, destination / "licenses" / package / "copyright")
    return records


def product_name(binary: Path) -> str | None:
    """The ProductName of a program's version resource: the name Windows shows
    in Explorer, the taskbar and Task Manager instead of the file name."""
    data = binary.read_bytes()
    # A String of the resource: three 16-bit fields, its UTF-16 key, then its
    # value, aligned on 32 bits from the start of the String.
    key = "ProductName\0".encode("utf-16-le")
    at = data.find(key)
    if at < 6:
        return None
    start = at - 6 + ((6 + len(key) + 3) & ~3)
    end = start
    while data[end:end + 2] not in (b"\0\0", b""):
        end += 2
    return data[start:end].decode("utf-16-le", errors="replace")


def run(program: Path, *args: str, env: dict[str, str]) -> None:
    result = subprocess.run([program, *args], env=env, timeout=30, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{program.name} {' '.join(args)} failed ({result.returncode}):\n"
                           f"{result.stdout[-4000:]}{result.stderr[-4000:]}")


def verify(app: Path) -> None:
    system = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    absent = missing(app, system)
    for binary in sorted(path for path in app.iterdir() if path.suffix.lower() in (".exe", ".dll")):
        print(f"{binary.name}: {', '.join(sorted(imports(binary), key=str.lower))}", flush=True)
    if absent:
        lines = "\n".join(f"  {dll} (loaded by {', '.join(users)})" for dll, users in sorted(absent.items()))
        raise RuntimeError(f"DLLs missing from the archive:\n{lines}")
    program = app / "RavenEmu.exe"
    if (name := product_name(program)) != "RavenEmu":
        raise RuntimeError(f"{program.name} is named {name!r} in Windows, not RavenEmu")
    env = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "RAVENEMUDSK_UI_SMOKE": "1"}
    print(f"Verifying {program}", flush=True)
    run(program, "--help", env=env)
    for page in ("home", "settings", "profile", "tv"):
        print(f"Smoke test: {page}", flush=True)
        run(program, env={**env, "RAVENEMUDSK_UI_SMOKE_PAGE": page})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build" / "Release")
    parser.add_argument("--vcpkg-installed", type=Path,
                        default=Path(os.environ.get("VCPKG_ROOT", r"C:\vcpkg")) / "installed")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="RavenEmu package ") as directory:
        app = Path(directory) / "RavenEmu"
        app.mkdir()
        copy(build / "RavenEmu.exe", app / "RavenEmu.exe")
        # vcpkg's DLLs and the Visual C++ runtime, copied beside the program by the build.
        for dll in build.glob("*.dll"):
            copy(dll, app / dll.name)
        shutil.copytree(build / "assets", app / "assets")
        project_files(app, build.parent)
        records = vcpkg_licenses(app, args.vcpkg_installed, app)
        (app / "dependencies.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        (app / "LISEZ-MOI.txt").write_text(
            "RavenEmu pour Windows\n\n"
            "Extrayez toute l’archive, puis lancez RavenEmu.exe. Conservez les DLL et le dossier assets à côté.\n"
            "Ce programme s’appelait RavenEmuDSK.exe : si vous extrayez sur une ancienne version, vous pouvez\n"
            "supprimer ce fichier. Vos sauvegardes, votre bibliothèque et vos réglages sont conservés.\n"
            "Rien à installer : SDL2, libcurl, zlib et le runtime Visual C++ sont inclus ; Windows 10 ou 11\n"
            "(64 bits) fournit le reste.\n\n"
            "Les licences sont dans licenses ; dependencies.json indique les versions des bibliothèques (vcpkg).\n"
            "Sources du programme : https://github.com/GhostPunishR/RavenEmuDSK\n",
            encoding="utf-8")
        archive = output / "RavenEmu-Windows-x64.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(app.rglob("*")):
                bundle.write(path, Path(app.name) / path.relative_to(app))
    # Check the archive users will download, not just the staging folder.
    with tempfile.TemporaryDirectory(prefix="RavenEmu extracted ") as extracted:
        with zipfile.ZipFile(archive) as bundle:
            bundle.extractall(extracted)
        verify(Path(extracted) / "RavenEmu")
    print(f"Packaged and verified: {archive}")


if __name__ == "__main__":
    main()
