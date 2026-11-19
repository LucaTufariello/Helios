#!/usr/bin/env python3
"""Install or remove only this relocatable application, preserving user data."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile

APP_ID = "io.helios.desktop"


def desktop_quote(value: str) -> str:
    # Desktop Entry Exec is not interpreted by a shell. Escape its double-quoted
    # argument syntax, then apply the desktop file's general backslash escaping.
    if any(character in value for character in "\r\n\0"):
        raise ValueError("Installation paths cannot contain newlines or NUL.")
    quoted = '"' + ''.join(('\\' if char in '\\"`$' else '') + char for char in value.replace('%', '%%')) + '"'
    return quoted.replace('\\', '\\\\')


def main() -> None:
    data_home = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=data_home / "helios-desktop")
    parser.add_argument("--desktop-dir", type=Path, default=data_home / "applications")
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    target = args.prefix.expanduser().absolute()
    desktop_file = args.desktop_dir.expanduser().absolute() / (APP_ID + ".desktop")
    marker = target / ".helios-installed.json"
    if target in (Path("/"), Path.home(), data_home.absolute()) or target.is_symlink():
        raise SystemExit("Choose a dedicated application directory, not a home/data root or symlink.")
    desktop_quote(str(target))
    if args.uninstall:
        if not marker.is_file():
            raise SystemExit("This directory is not a Helios user installation.")
        record = json.loads(marker.read_text())
        if record != {"application": APP_ID, "prefix": str(target), "desktop": str(desktop_file)}:
            raise SystemExit("Installation marker does not match these application paths.")
        known = {line.split("  ", 1)[1] for line in (target / "SHA256SUMS").read_text().splitlines()}
        known.update({"SHA256SUMS", ".helios-installed.json"})
        directories = {str(parent) for name in known for parent in Path(name).parents}
        for file in target.rglob("*"):
            name = str(file.relative_to(target))
            if name not in known and name not in directories:
                raise SystemExit("Removal stopped to preserve a file added to the app directory: " + name)
        if desktop_file.exists():
            # Never remove an entry someone replaced with a different launcher.
            if "X-Helios-Install=" + str(target) not in desktop_file.read_text():
                raise SystemExit("The desktop entry changed; preserve or remove it manually.")
            desktop_file.unlink()
        shutil.rmtree(target)
        print("Removed the application and its desktop entry. Working profiles and jobs are preserved.")
        return
    if target.exists() or desktop_file.exists():
        raise SystemExit("The application directory or desktop entry already exists. Remove the prior installation first; profiles are separate.")
    if source in target.parents:
        raise SystemExit("Install outside the extracted package directory.")
    if not (source / "package-manifest.json").is_file() or not (source / "helios-desktop").is_file():
        raise SystemExit("Run this installer from an extracted Helios package.")
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".helios-install-", dir=target.parent))
    try:
        shutil.copytree(source, staging, dirs_exist_ok=True, symlinks=True)
        (staging / ".helios-installed.json").write_text(json.dumps({"application": APP_ID, "prefix": str(target), "desktop": str(desktop_file)}) + "\n")
        staging.rename(target)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    desktop_file.parent.mkdir(parents=True, exist_ok=True)
    entry = "\n".join((
        "[Desktop Entry]", "Type=Application", "Name=Helios",
        "Comment=Native imagery, infrastructure and mission workspace",
        "Exec=" + desktop_quote(str(target / "helios-desktop")) + " %u",
        "Icon=" + str(target / "share/helios/helios.svg").replace("\\", "\\\\"),
        "Terminal=false", "Categories=Science;Geography;", "StartupNotify=true",
        "MimeType=x-scheme-handler/helios;",
        "X-Helios-Install=" + str(target), "",
    ))
    try:
        with desktop_file.open("x") as output:
            output.write(entry)
    except BaseException:
        shutil.rmtree(target)
        raise
    print(f"Installed {target}\nDesktop entry: {desktop_file}\nProfiles and jobs use the app's separate user data location.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
