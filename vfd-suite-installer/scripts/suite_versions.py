#!/usr/bin/env python3
"""Keep the suite's version numbers in step, so a release can't ship a stale or mislabelled build.

  suite_versions.py check              every copy of the suite version agrees, and every plug-in's version
                                       in the installers matches its CMakeLists.txt
  suite_versions.py check --release    ... and START HERE.txt and whats-new-<suite>.json are written for it
  suite_versions.py check --bundles    ... and each built Mac bundle (VST3 + AU in its build folder) carries
                                       its CMake version
  suite_versions.py bump 1.6.2         writes 1.6.2 everywhere the suite version lives and copies each
                                       plug-in's CMake version into the installers (START HERE and
                                       whats-new are prose: edit those by hand)

Run from anywhere; paths are relative to the repo root. Standard library only.
"""
import argparse
import plistlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# folder, CMake project / target name, bundle name, Windows installer component
PLUGINS = [
    ("yoi-boi", "YoiBoi", "YOI-BOI", "yoiboi"),
    ("throw-up", "ThrowUp", "THROW-UP", "throwup"),
    ("back-scratch", "BackScratch", "BACK-SCRATCH", "backscratch"),
    ("shifty", "Shifty", "SHIFTY", "shifty"),
    ("energy-harvester", "EnergyHarvester", "ENERGY HARVESTER", "energyharvester"),
    ("stutter-step", "StutterStep", "STUTTER-STEP", "stutterstep"),
]
BUILD_DIR = "build"  # one build folder per plug-in; release-mac.sh rebuilds it before signing

SUITE_SPOTS = [
    # file, pattern whose group 1 is the suite version
    ("shared/VfdSuiteVersion.h", r'#define VFD_SUITE_VERSION "([0-9.]+)"'),
    (".github/workflows/windows.yml", r'SUITE_VERSION: "([0-9.]+)"'),
    (".github/workflows/linux.yml", r'SUITE_VERSION: "([0-9.]+)"'),
    ("vfd-suite-installer/windows/VividFeverDreamsSuite.iss", r'#define SuiteVersion "([0-9.]+)"'),
    ("vfd-suite-installer/windows/VividFeverDreamsSuite.iss", r"/DSuiteVersion=([0-9.]+)"),
    ("vfd-suite-installer/InstallerApp.swift", r'"CFBundleShortVersionString"\) as\? String \?\? "([0-9.]+)"'),
]
CATALOG = "vfd-suite-installer/InstallerCore.swift"
ISS = "vfd-suite-installer/windows/VividFeverDreamsSuite.iss"


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def cmake_version(folder):
    match = re.search(r"project\(\s*\w+\s+VERSION\s+([0-9.]+)", read(f"{folder}/CMakeLists.txt"))
    if not match:
        raise SystemExit(f"{folder}/CMakeLists.txt has no project(... VERSION x.y.z)")
    return match.group(1)


def catalog_pattern(bundle):
    return r'(PluginDefinition\(name: "' + re.escape(bundle) + r'",[^)]*?version: ")([0-9.]+)(")'


def iss_pattern(component, bundle):
    return r'(Name: "' + re.escape(component) + r'"; Description: "' + re.escape(bundle) + r' )([0-9.]+)( )'


def check(release, bundles):
    problems = []
    suite = {}
    for rel, pattern in SUITE_SPOTS:
        found = re.findall(pattern, read(rel))
        if not found:
            problems.append(f"{rel}: no suite version found ({pattern})")
        for value in found:
            suite.setdefault(value, []).append(rel)
    if len(suite) > 1:
        problems.append("suite version differs: " + "; ".join(f"{v} in {', '.join(sorted(set(f)))}" for v, f in suite.items()))
    version = next(iter(suite), None)

    catalog, iss = read(CATALOG), read(ISS)
    for folder, target, bundle, component in PLUGINS:
        want = cmake_version(folder)
        for rel, text, pattern in ((CATALOG, catalog, catalog_pattern(bundle)), (ISS, iss, iss_pattern(component, bundle))):
            match = re.search(pattern, text)
            if not match:
                problems.append(f"{rel}: no entry for {bundle}")
            elif match.group(2) != want:
                problems.append(f"{rel}: {bundle} is {match.group(2)}, CMakeLists.txt says {want}")
        if bundles:
            artefacts = ROOT / folder / BUILD_DIR / f"{target}_artefacts" / "Release"
            for kind, ext in (("VST3", "vst3"), ("AU", "component")):
                plist = artefacts / kind / f"{bundle}.{ext}" / "Contents" / "Info.plist"
                if not plist.exists():
                    problems.append(f"{plist.relative_to(ROOT)} is missing (build {target}_{kind})")
                    continue
                built = plistlib.loads(plist.read_bytes()).get("CFBundleShortVersionString")
                if built != want:
                    problems.append(f"{plist.parent.parent.relative_to(ROOT)} is {built}, CMakeLists.txt says {want} (stale build?)")

    if release and version:
        if not (ROOT / f"vfd-suite-installer/whats-new-{version}.json").exists():
            problems.append(f"vfd-suite-installer/whats-new-{version}.json is missing")
        if f"SUITE {version} " not in read("vfd-suite-installer/START HERE.txt"):
            problems.append(f"vfd-suite-installer/START HERE.txt is not written for SUITE {version}")

    for p in problems:
        print("FAIL", p)
    if problems:
        return 1
    print(f"OK suite {version}; " + ", ".join(f"{b} {cmake_version(f)}" for f, _, b, _ in PLUGINS))
    return 0


def bump(version):
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("version must look like 1.6.2")
    for rel, pattern in SUITE_SPOTS:
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        text, n = re.subn(pattern, lambda m: m.group(0).replace(m.group(1), version), text)
        if n == 0:
            raise SystemExit(f"{rel}: no suite version found ({pattern})")
        path.write_text(text, encoding="utf-8")
    for rel, make in ((CATALOG, lambda f, b, c: catalog_pattern(b)), (ISS, lambda f, b, c: iss_pattern(c, b))):
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        for folder, _, bundle, component in PLUGINS:
            text, n = re.subn(make(folder, bundle, component), lambda m, v=cmake_version(folder): m.group(1) + v + m.group(3), text)
            if n != 1:
                raise SystemExit(f"{rel}: expected one entry for {bundle}, found {n}")
        path.write_text(text, encoding="utf-8")
    print(f"Suite version set to {version}. Still to write by hand: vfd-suite-installer/START HERE.txt, "
          f"vfd-suite-installer/whats-new-{version}.json")
    return check(release=False, bundles=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("check")
    c.add_argument("--release", action="store_true")
    c.add_argument("--bundles", action="store_true")
    b = sub.add_parser("bump")
    b.add_argument("version")
    args = parser.parse_args()
    return check(args.release, args.bundles) if args.command == "check" else bump(args.version)


if __name__ == "__main__":
    sys.exit(main())
