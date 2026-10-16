#!/usr/bin/env python3
"""Collect notices from the installed Homebrew runtime used by a Mac bundle."""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess


def command(*arguments: str) -> str:
    return subprocess.check_output(arguments, text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--formula", action="append", default=[])
    args = parser.parse_args()
    if platform.system() != "Darwin":
        raise ValueError("Collect the Mac notices on the actual Mac build host.")
    app = args.app.resolve()
    target = app / "Contents/Resources/third-party"
    target.mkdir(parents=True, exist_ok=True)
    roots = args.formula or ["qtbase"]
    formulas = set(roots)
    for formula in roots:
        formulas.update(command("brew", "deps", "--formula", "--installed", formula).splitlines())
    records = []
    missing = []
    for formula in sorted(formulas):
        prefix = Path(command("brew", "--prefix", formula)).resolve()
        destination = target / formula
        destination.mkdir(parents=True, exist_ok=True)
        files = []
        for path in prefix.iterdir():
            if path.name.upper().startswith(("LICENSE", "COPYING", "NOTICE")):
                if path.is_dir():
                    shutil.copytree(path, destination / path.name, dirs_exist_ok=True)
                elif path.is_file():
                    shutil.copy2(path, destination / path.name)
                files.append(path.name)
        for path in (prefix / "share/qt/licenses", prefix / "share/licenses"):
            if path.is_dir():
                shutil.copytree(path, destination / "licenses", dirs_exist_ok=True)
                files.append(str(path.relative_to(prefix)))
        # Homebrew's saved recipe preserves the exact source URL and patches at
        # installation time, even if the formula in the public tap moves later.
        recipe = prefix / ".brew" / (formula.split("/")[-1] + ".rb")
        if recipe.is_file():
            shutil.copy2(recipe, destination / "homebrew-formula.rb")
        records.append({"formula": formula, "installedVersion": prefix.name,
                        "localNotices": files, "savedFormula": recipe.is_file()})
        if not files:
            missing.append(formula)
    linkage = {}
    for path in (app / "Contents").rglob("*"):
        if path.is_symlink() or not path.is_file():
            continue
        if "Mach-O" not in command("file", "-b", str(path)):
            continue
        links = command("otool", "-L", str(path)).splitlines()[1:]
        linkage[str(path.relative_to(app))] = [line.strip() for line in links]
        for line in links:
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith("/") and not dependency.startswith(("/System/Library/", "/usr/lib/")):
                raise ValueError("Undeployed absolute dependency: " + dependency)
    (target / "inventory.json").write_text(json.dumps({"roots": roots, "formulas": records,
                                                      "missingLocalNotices": missing, "bundleLinkage": linkage}, indent=2) + "\n")
    if missing:
        (target / "INCOMPLETE-NOTICES.txt").write_text("Local license files were unavailable for these installed formulas:\n" + "\n".join(missing) + "\nRetain their matching license texts and corresponding sources before public redistribution.\n")
        print("Local notices missing for: " + ", ".join(missing))
    for formula in roots:
        if formula in missing:
            raise ValueError("The Mac package is missing its Qt runtime license texts; supply them from the matching installed distribution.")
    print("Mac runtime notices and deployed linkage recorded in " + str(target))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
