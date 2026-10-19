#!/usr/bin/env python3
"""Build and smoke-test relocatable Linux and macOS download archives."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import signal
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# These are part of glibc and must match the host's dynamic loader. Graphics
# drivers and the running desktop/keyring services also remain host facilities.
GLIBC = re.compile(r"^(?:ld-linux|ld64|linux-vdso|lib(?:c|m|mvec|dl|pthread|rt|util|resolv|anl|nss_[^.]+)\.so)")


def run(*args: str | Path, env: dict[str, str] | None = None, timeout: int = 120) -> str:
    with subprocess.Popen([str(a) for a in args], text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, env=env, start_new_session=True) as process:
        try:
            output = process.communicate(timeout=timeout)[0]
        except subprocess.TimeoutExpired:
            diagnostic = ""
            if platform.system() == "Darwin":
                sample = subprocess.run(["sample", str(process.pid), "1", "1"], text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
                diagnostic = sample.stdout[:9000]
            os.killpg(process.pid, signal.SIGKILL)
            output = process.communicate(timeout=10)[0]
            raise RuntimeError(f"Command timed out after {timeout}s: {args}\n{output[-4000:]}\n{diagnostic}")
        if process.returncode:
            raise RuntimeError(f"Command failed: {args}\n{output}")
        return output


def copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    destination.chmod(destination.stat().st_mode | 0o200)


def project_files(destination: Path, build: Path) -> None:
    for name in ("LICENSE", "NOTICE.md", "README.md"):
        copy(ROOT / name, destination / name)
    for name, source in {
        "RavenCore": build / "_deps/ravencore-src/LICENSE",
        "DearImGui": build / "_deps/imgui-src/LICENSE.txt",
        "stb": build / "_deps/stb-src/LICENSE",
        "rcheevos": build / "_deps/rcheevos-src/LICENSE",
    }.items():
        copy(source, destination / "licenses" / name / source.name)


def linux_dependencies(binary: Path) -> dict[str, Path]:
    output = run("ldd", binary, env={**os.environ, "LC_ALL": "C"})
    if "=> not found" in output:
        raise RuntimeError(f"Missing runtime dependency:\n{output}")
    return {match[1]: Path(match[2]) for line in output.splitlines()
            if (match := re.match(r"\s*(\S+)\s+=>\s+(.+?)\s+\(0x[0-9a-f]+\)", line))
            and not GLIBC.match(match[1])}


def debian_license(library: Path, destination: Path, records: dict) -> None:
    resolved = library.resolve(strict=True)
    # The glob handles both Debian and Ubuntu's merged /usr layouts.
    owner = run("dpkg-query", "--search", f"*/{resolved.name}").splitlines()[0].split(": ", 1)[0]
    package = owner.split(":", 1)[0]
    if package in records:
        return
    fields = run("dpkg-query", "--show", "--showformat=${Version}\t${source:Package}\t${source:Version}",
                 owner).strip().split("\t")
    copy(Path("/usr/share/doc") / package / "copyright",
         destination / "licenses" / package / "copyright")
    records[package] = {"version": fields[0], "source_package": fields[1], "source_version": fields[2]}


def verify(binary: Path, directory: Path, macos: bool) -> None:
    env = dict(os.environ)
    for name in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH"):
        env.pop(name, None)
    if macos:
        run("codesign", "--verify", "--deep", "--strict", directory)
    with tempfile.TemporaryDirectory(prefix="ravenemu-smoke-") as data:
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   RAVENEMUDSK_UI_SMOKE="1", XDG_DATA_HOME=data)
        print(f"Verifying {binary}", flush=True)
        run(binary, "--help", env=env, timeout=30)
        for page in ("home", "settings", "profile", "tv"):
            print(f"Smoke test: {page}", flush=True)
            env["RAVENEMUDSK_UI_SMOKE_PAGE"] = page
            run(binary, env=env, timeout=30)
    if macos:
        run("codesign", "--verify", "--deep", "--strict", directory)
    else:
        for name, path in linux_dependencies(binary).items():
            if not path.resolve().is_relative_to(directory / "lib"):
                raise RuntimeError(f"Library was not bundled: {name}: {path}")


def linux(build: Path, stage: Path) -> tuple[Path, Path]:
    app = stage / "RavenEmu"
    app.mkdir()
    binary = app / "RavenEmuDSK"
    copy(build / "RavenEmuDSK", binary)
    shutil.copytree(build / "assets", app / "assets")
    project_files(app, build)
    records: dict = {}
    dependencies = linux_dependencies(build / "RavenEmuDSK")
    print(f"Bundling {len(dependencies)} Linux shared libraries", flush=True)
    if not any(name.startswith("libSDL2") for name in dependencies):
        raise RuntimeError("SDL2 is missing from the runtime dependency list")
    for name, source in dependencies.items():
        target = app / "lib" / name
        copy(source, target)
        debian_license(source, app, records)
        run("patchelf", "--set-rpath", "$ORIGIN", target)
    run("patchelf", "--set-rpath", "$ORIGIN/lib", binary)
    shutil.copytree("/usr/share/common-licenses", app / "licenses/common-licenses")
    (app / "dependencies.json").write_text(json.dumps(records, indent=2) + "\n")
    (app / "LISEZ-MOI.txt").write_text(
        "RavenEmu pour Linux\n\n"
        "Extrayez toute l’archive, puis lancez RavenEmuDSK. Conservez les dossiers lib et assets.\n"
        "Les bibliothèques sont incluses, sans modification du système ni installation par apt.\n"
        "La version CI x86_64 nécessite glibc 2.39 ou plus récente (Ubuntu 24.04+, Debian 13).\n"
        "Votre environnement de bureau fournit les pilotes graphiques, le son, le magasin de\n"
        "certificats HTTPS et, pour mémoriser RetroAchievements, un trousseau Secret Service.\n\n"
        "Les licences sont dans licenses ; dependencies.json indique les paquets source et versions.\n"
        "Sur la distribution de compilation, apt-get source NOM=VERSION récupère ces sources\n"
        "(dépôts deb-src nécessaires). Sources du programme : https://github.com/GhostPunishR/RavenEmuDSK\n",
        encoding="utf-8")
    verify(binary, app, macos=False)
    return app, binary


def brew_licenses(paths: list[str], resources: Path) -> None:
    records = {}
    for value in paths:
        source = Path(value).resolve(strict=True)
        if "Cellar" not in source.parts:
            raise RuntimeError(f"Unknown non-system dependency license location: {source}")
        index = source.parts.index("Cellar")
        name, version = source.parts[index + 1:index + 3]
        if name in records:
            continue
        cellar = Path(*source.parts[:index + 3])
        licenses = [p for p in cellar.rglob("*") if p.is_file() and
                    p.name.lower().startswith(("license", "copying", "copyright", "notice"))]
        if not licenses:
            raise RuntimeError(f"Missing license for bundled Homebrew formula {name}")
        for license_file in licenses:
            copy(license_file, resources / "licenses" / name / license_file.relative_to(cellar))
        # The installed formula records the source URL and checksum for this
        # exact bottle. Read it locally instead of making Homebrew API requests.
        formula = cellar / ".brew" / f"{name}.rb"
        copy(formula, resources / "licenses" / name / "source-formula.rb")
        source_url = re.search(r'^\s*url\s+"([^"]+)"', formula.read_text(), re.MULTILINE)
        records[name] = {"version": version, "source": source_url[1] if source_url else
                         f"https://formulae.brew.sh/formula/{name}",
                         "source_formula": f"licenses/{name}/source-formula.rb"}
    (resources / "dependencies.json").write_text(json.dumps(records, indent=2) + "\n")


def macos(build: Path, stage: Path) -> tuple[Path, Path]:
    app = stage / "RavenEmu.app"
    resources = app / "Contents/Resources"
    binary = app / "Contents/MacOS/RavenEmuDSK"
    copy(build / "RavenEmuDSK", binary)
    shutil.copytree(build / "assets", resources / "assets")
    project_files(resources, build)
    version = re.search(r"project\(RavenEmuDSK VERSION ([0-9.]+)",
                        (ROOT / "CMakeLists.txt").read_text())[1]
    with (app / "Contents/Info.plist").open("wb") as stream:
        plistlib.dump({"CFBundleExecutable": "RavenEmuDSK", "CFBundleName": "RavenEmu",
                      "CFBundleDisplayName": "RavenEmu", "CFBundleIdentifier": "org.ravenemu.desktop",
                      "CFBundlePackageType": "APPL", "CFBundleVersion": version,
                      "CFBundleShortVersionString": version, "LSMinimumSystemVersion": "15.0",
                      "NSHighResolutionCapable": True, "SDL_FILESYSTEM_BASE_DIR": "resource"}, stream)
    print("Bundling macOS libraries", flush=True)
    run("cmake", f"-DAPP={app}", "-P", ROOT / "cmake/bundle_macos.cmake", timeout=180)
    paths_file = resources / "dependency-paths.txt"
    print("Collecting macOS dependency licenses", flush=True)
    brew_licenses(paths_file.read_text().splitlines(), resources)
    paths_file.unlink()
    # Sign only after every binary and resource has reached its final location.
    # This is an ad-hoc signature, not Developer ID notarization.
    print("Signing macOS application", flush=True)
    run("codesign", "--force", "--deep", "--sign", "-", app)
    verify(binary, app, macos=True)
    return app, binary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    system = platform.system()
    if system not in ("Linux", "Darwin"):
        parser.error("This packager supports Linux and macOS")
    output.mkdir(parents=True, exist_ok=True)
    # Spaces in this staging path intentionally exercise relocation and quoting.
    with tempfile.TemporaryDirectory(prefix="RavenEmu package ") as directory:
        stage = Path(directory)
        app, _ = (macos if system == "Darwin" else linux)(build, stage)
        stem = f"RavenEmu-{'macOS' if system == 'Darwin' else 'Linux'}-{platform.machine()}"
        if system == "Darwin":
            archive = output / f"{stem}.zip"
            run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, archive)
        else:
            archive = output / f"{stem}.tar.gz"
            with tarfile.open(archive, "w:gz") as bundle:
                bundle.add(app, arcname=app.name)
        # Validate the archive users will download, not just the staging folder.
        with tempfile.TemporaryDirectory(prefix="RavenEmu extracted ") as extracted:
            if system == "Darwin":
                run("ditto", "-x", "-k", archive, extracted)
                installed = Path(extracted) / "RavenEmu.app"
                verify(installed / "Contents/MacOS/RavenEmuDSK", installed, macos=True)
            else:
                with tarfile.open(archive) as bundle:
                    bundle.extractall(extracted, filter="data")
                installed = Path(extracted) / "RavenEmu"
                verify(installed / "RavenEmuDSK", installed, macos=False)
        print(f"Packaged and verified: {archive}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        if os.environ.get("GITHUB_ACTIONS") == "true":
            message = str(error).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
            print(f"::error title=Unix packaging failed::{message}", flush=True)
        raise
