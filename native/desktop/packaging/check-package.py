#!/usr/bin/env python3
"""Verify a built Linux package from a disposable location outside the checkout."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path, help="Extracted helios-desktop directory.")
    parser.add_argument("--output", required=True, type=Path, help="New evidence directory.")
    parser.add_argument("--gpu", action="store_true", help="Run generated checks in the actual graphical session.")
    parser.add_argument("--live", action="store_true", help="Also opt in to public HTTPS map/terrain requests.")
    parser.add_argument("--raster-python", type=Path, help="Explicit external scientific Python for generated worker checks.")
    parser.add_argument("--inference-python", type=Path, help="Explicit external CPU inference Python for generated model checks.")
    args = parser.parse_args()
    source, output = args.package.resolve(), args.output.resolve()
    if output.exists():
        raise ValueError("Choose a new evidence directory.")
    output.mkdir(parents=True)
    manifest = json.loads((source / "package-manifest.json").read_text())
    checks = []
    with tempfile.TemporaryDirectory(prefix="helios-package-check-") as temporary:
        root = Path(temporary)
        app = root / "extracted app"
        shutil.copytree(source, app, symlinks=True)
        environment = {**os.environ, "XDG_CONFIG_HOME": str(root / "config"),
                       "XDG_DATA_HOME": str(root / "data"), "XDG_CACHE_HOME": str(root / "cache")}
        if args.inference_python:
            # Keep a virtual environment's interpreter entrypoint: resolving
            # its symlink selects the base Python and loses installed packages.
            environment["HELIOS_INFERENCE_PYTHON"] = str(args.inference_python.expanduser().absolute())

        def run(name: str, command: list[str], *, graphical: bool = False, timeout: int = 120) -> None:
            current_environment = environment if graphical else {**environment, "QT_QPA_PLATFORM": "offscreen"}
            with (output / (name + ".log")).open("w") as log:
                result = subprocess.run(command, cwd=root, env=current_environment, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=timeout)
            if result.returncode:
                raise ValueError(f"Package check failed: {name} (exit {result.returncode}); inspect {output / (name + '.log')}")
            checks.append(name)

        # The sum file uses paths relative to the app; validate without changing
        # the executable's test working directory back into its installation.
        for line in (app / "SHA256SUMS").read_text().splitlines():
            checksum, name = line.split("  ", 1)
            path = app / name
            if not path.resolve().is_relative_to(app) or sha256(path) != checksum:
                raise ValueError("Package checksum or path admission failed: " + name)
        checks.append("package-checksums")
        required_resources = ("SAR/sentinel1.py", "SAR/change_review.py", "SAR/registration.py",
                              "SAR/requirements.txt", "SAR/requirements-monitoring.txt",
                              "Optical/sentinel2.py", "Optical/optical_sources.py",
                              "Inference/infer.py", "Inference/requirements.txt",
                              "Inference/tinyyolov2-voc.contract.json")
        for resource in required_resources:
            if not (app / "share/helios/workers" / resource).is_file():
                raise ValueError("Packaged worker resource is missing: " + resource)
        for resource in ("transit-feeds.json", "SGP4-LICENSE.txt", "datacenters/datacenters.geojsonl",
                         "dams/dams.geojsonl", "telegeography_submarine_cables/cable-geo.json"):
            if not (app / "share/helios/sources" / resource).is_file():
                raise ValueError("Packaged source resource is missing: " + resource)
        if any(path.name == "__pycache__" or path.suffix in (".pyc", ".pyo")
               for path in (app / "share/helios/workers").rglob("*")):
            raise ValueError("Machine-specific Python bytecode entered the runtime package.")
        checks.append("worker-and-source-resources")
        if any(component in library for library in manifest["bundledLibraries"]
               for component in ("Qml", "Quick", "WebEngine", "WebChannel")):
            raise ValueError("A browser or JavaScript UI runtime entered the package.")
        checks.append("native-ui-runtime")
        run("runtime", [str(app / "helios-desktop"), "--check-runtime"])
        run("version", [str(app / "helios-desktop"), "--version"])
        run("native-media-runtime", ["env", "LD_LIBRARY_PATH=" + str(app / "lib"),
                                     str(app / "bin/ffmpeg"), "-hide_banner", "-version"])
        install = root / "installed app with spaces"
        desktop = root / "desktop entries"
        installer = ["python3", str(app / "install-user.py"), "--prefix", str(install), "--desktop-dir", str(desktop)]
        run("user-install", installer)
        run("installed-runtime", [str(install / "helios-desktop"), "--check-runtime"])
        run("installed-version", [str(install / "helios-desktop"), "--version"])
        if shutil.which("desktop-file-validate"):
            run("desktop-entry", ["desktop-file-validate", str(desktop / "io.helios.desktop.desktop")])
        refused = subprocess.run(installer, cwd=root, env=environment, capture_output=True)
        if refused.returncode == 0:
            raise ValueError("The installer overwrote an existing installation.")
        checks.append("installer-refuses-overwrite")
        independent_profile = root / "independent-profile"
        independent_profile.mkdir()
        (independent_profile / "mission.json").write_text("preserve this user data\n")
        (install / "personal-note.txt").write_text("preserve unexpected files\n")
        remove_command = ["python3", str(install / "install-user.py"), "--prefix", str(install),
                          "--desktop-dir", str(desktop), "--uninstall"]
        refused = subprocess.run(remove_command, cwd=root, env=environment, capture_output=True)
        if refused.returncode == 0 or not (install / "personal-note.txt").is_file():
            raise ValueError("The installer removed a file added to the application directory.")
        checks.append("uninstall-preserves-added-files")
        (install / "personal-note.txt").unlink()
        # Exercise removal from the installed directory as documented.
        run("user-uninstall", remove_command)
        if install.exists() or (desktop / "io.helios.desktop.desktop").exists():
            raise ValueError("The app or desktop entry survived removal.")
        if (independent_profile / "mission.json").read_text() != "preserve this user data\n":
            raise ValueError("An independent user profile changed during removal.")
        checks.append("uninstall-preserves-profile")
        gpu_results = {}
        for kind, enabled, option in (("generated", args.gpu, "--validate"), ("live", args.live, "--validate-live")):
            if not enabled:
                continue
            command = [str(app / "helios-desktop"), option, "--qa-output", str(output / kind)]
            if args.raster_python and kind == "generated":
                command += ["--raster-python", str(args.raster_python.expanduser().absolute())]
            run(kind, command, graphical=True, timeout=240)
            report = json.loads((output / kind / "validation.json").read_text())
            if not report.get("passed") or report.get("backend") != "Vulkan":
                raise ValueError("Packaged hardware validation did not pass on Vulkan.")
            gpu_results[kind] = {key: report[key] for key in ("gpu", "backend", "elapsedSeconds", "peakResidentBytes", "processCpuSeconds")}
            gpu_results[kind]["checkCount"] = len(report["checks"])
        result = {"passed": True, "checks": checks, "runtimeLocation": str(app),
                  "packagedExecutableSha256": sha256(app / "bin/Helios"),
                  "workerHostSha256": sha256(app / "bin/HeliosWorkerHost"), "sourceSha256": manifest["sourceSha256"],
                  "qtVersion": manifest["qtVersion"], "gpuResults": gpu_results}
        (output / "package-check.json").write_text(json.dumps(result, indent=2) + "\n")
        shutil.copy2(source / "package-manifest.json", output / "package-manifest.json")
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        raise SystemExit(str(error)) from error
