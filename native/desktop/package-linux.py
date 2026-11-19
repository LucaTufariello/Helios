#!/usr/bin/env python3
"""Package an existing Linux build with its matching Qt runtime; never install it."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import time
from functools import lru_cache

ROOT = Path(__file__).resolve().parents[2]
PACKAGING = ROOT / "native/desktop/packaging"


def command(*args: str, **kwargs) -> str:
    return subprocess.check_output(args, text=True, **kwargs).strip()


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def dependencies(path: Path) -> dict[str, Path]:
    text = command("ldd", str(path), env={**os.environ, "LC_ALL": "C", "LD_LIBRARY_PATH": ""})
    if "not found" in text:
        raise ValueError(f"Unresolved build runtime dependency: {path}\n{text}")
    return {name: Path(file).resolve() for name, file in re.findall(r"^\s*(\S+) => (/\S+) \(", text, re.MULTILINE)}


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def normalized(info: tarfile.TarInfo) -> tarfile.TarInfo:
    info.uid = info.gid = 0
    info.uname = info.gname = "root"
    return info


def write_tar(path: Path, entries: list[tuple[Path, str]]) -> None:
    # Use zstd externally so packaging does not require a particular Python version.
    with tempfile.NamedTemporaryFile(dir=path.parent, suffix=".tar") as temporary:
        with tarfile.open(fileobj=temporary, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for source, name in entries:
                archive.add(source, arcname=name, filter=normalized)
        temporary.flush()
        subprocess.run(["zstd", "--quiet", "-T2", "-10", "--force", temporary.name, "-o", str(path)], check=True)


def source_snapshot(destination: Path) -> tuple[Path, dict[str, str]]:
    files = command("git", "-C", str(ROOT), "ls-files", "--cached", "--others", "--exclude-standard", "-z").split("\0")
    path = destination / "helios-source.tar.zst"
    hashes = {}
    with tempfile.TemporaryDirectory(prefix="source-snapshot-", dir=destination) as temporary:
        staging = Path(temporary)
        for name in sorted(set(files)):
            source = ROOT / name
            if not name or not source.is_file() or source.resolve().is_relative_to(destination):
                continue
            if not source.resolve().is_relative_to(ROOT):
                raise ValueError("A source symlink leaves the project; preserve it separately: " + name)
            before = source.stat()
            copy_file(source, staging / name)
            after = source.stat()
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError("A source changed while packaging. Wait for edits to finish: " + name)
            hashes[name] = digest(staging / name)
        write_tar(path, [(staging / name, "Helios/" + name) for name in hashes])
    return path, hashes


def pinned_checkout(url: str, tag: str, directory: Path) -> Path:
    if not directory.exists():
        subprocess.run(["git", "clone", "--quiet", "--depth", "1", "--branch", tag, url, str(directory)], check=True)
    revision = command("git", "-C", str(directory), "rev-parse", "HEAD")
    tagged = command("git", "-C", str(directory), "rev-parse", tag + "^{commit}")
    if revision != tagged or command("git", "-C", str(directory), "status", "--porcelain"):
        raise ValueError("The cached dependency source does not match its clean pinned tag: " + str(directory))
    return directory


def ffmpeg_source(executable: Path, destination: Path) -> tuple[Path, Path, Path, dict]:
    version_text = command(str(executable), "-version")
    match = re.match(r"ffmpeg version n?(\d+\.\d+(?:\.\d+)?)\b", version_text)
    if not match:
        raise ValueError("Cannot identify the installed FFmpeg release for source packaging.")
    version = match.group(1)
    owner, distribution_version = package_owner(executable)
    if owner != "ffmpeg" or not re.fullmatch(r"(?:\d+:)?[0-9.]+-\d+", distribution_version):
        raise ValueError("Source packaging currently supports the distribution's released FFmpeg package.")
    tag = distribution_version.replace(":", "-")
    vendor = ROOT / "output/vendor"
    vendor.mkdir(parents=True, exist_ok=True)
    source = pinned_checkout("https://github.com/FFmpeg/FFmpeg.git", "n" + version, vendor / ("ffmpeg-source-" + version))
    recipe = pinned_checkout("https://gitlab.archlinux.org/archlinux/packaging/packages/ffmpeg.git", tag,
                             vendor / ("ffmpeg-packaging-" + tag))
    entries = []
    for directory, prefix in ((source, "ffmpeg-" + version), (recipe, "arch-packaging")):
        names = command("git", "-C", str(directory), "ls-files", "-z").split("\0")
        entries += [(directory / name, prefix + "/" + name) for name in names if name and (directory / name).is_file()]
    path = destination / ("ffmpeg-" + version + "-source-and-arch-recipe.tar.zst")
    write_tar(path, entries)
    return path, source, recipe, {"version": version, "distributionPackage": distribution_version,
                                 "sourceRevision": command("git", "-C", str(source), "rev-parse", "HEAD"),
                                 "recipeRevision": command("git", "-C", str(recipe), "rev-parse", "HEAD"),
                                 "versionOutput": version_text, "licenseOutput": command(str(executable), "-hide_banner", "-L"),
                                 "sourceArchive": {"file": path.name, "sha256": digest(path)}}


@lru_cache(maxsize=None)
def package_owner(file: Path) -> tuple[str, str]:
    name = command("pacman", "-Qqo", str(file))
    fields = command("pacman", "-Q", name).split()
    return fields[0], fields[1]


def soname_requirements(libraries: dict[str, Path]) -> list[str]:
    provided = {}
    requirements = set()
    for soname, source in libraries.items():
        package, _ = package_owner(source)
        if package not in provided:
            info = command("pacman", "-Qi", package, env={**os.environ, "LC_ALL": "C"})
            match = re.search(r"^Provides\s*:\s*([^\n]+)", info, re.MULTILINE)
            provided[package] = set(match.group(1).split()) if match else set()
        match = re.fullmatch(r"(.+\.so)\.(.+)", soname)
        if match:
            requirement = match.group(1) + "=" + match.group(2) + "-64"
            if requirement in provided[package]:
                requirements.add(requirement)
    return sorted(requirements)


def native_package(application: Path, destination: Path, version: str, runtime: dict[str, str], sonames: list[str]) -> Path:
    staging = destination / "arch-root"
    shutil.copytree(application, staging / "opt/helios-desktop", symlinks=True)
    (staging / "usr/bin").mkdir(parents=True)
    (staging / "usr/bin/helios-desktop").symlink_to("../../opt/helios-desktop/helios-desktop")
    desktop = staging / "usr/share/applications/io.helios.desktop.desktop"
    desktop.parent.mkdir(parents=True)
    desktop.write_text("\n".join(("[Desktop Entry]", "Type=Application", "Name=Helios",
                                 "Comment=Native imagery, infrastructure and mission workspace",
                                 "Exec=/opt/helios-desktop/helios-desktop %u",
                                 "Icon=/opt/helios-desktop/share/helios/helios.svg", "Terminal=false",
                                 "Categories=Science;Geography;", "StartupNotify=true",
                                 "MimeType=x-scheme-handler/helios;", "")))
    # The package manager supplies the host libraries, fonts, trust store and GPU
    # loader; its installed driver/ICDs remain entirely under the user's control.
    requirements = sorted(set(runtime) | {"vulkan-icd-loader", "ca-certificates", "ttf-dejavu"})
    strict_minimum = {name + ">=" + runtime[name] for name in ("glibc", "libstdc++", "libgcc") if name in runtime}
    requirements = [name for name in requirements if name not in ("glibc", "libstdc++", "libgcc")] + sorted(strict_minimum) + sonames
    contents_size = sum(file.stat().st_size for file in staging.rglob("*") if file.is_file() and not file.is_symlink())
    metadata = ["pkgname = helios-desktop", "pkgbase = helios-desktop", "pkgver = " + version + "-1",
                "pkgdesc = Native Helios Qt desktop (migration build)",
                "builddate = " + str(int(time.time())), "packager = Helios local build",
                "size = " + str(contents_size), "arch = x86_64", "license = MIT", "license = LGPL-3.0-only",
                "license = Apache-2.0", "license = ODbL-1.0", "license = CC-BY-NC-SA-3.0", "license = GPL-3.0-or-later"]
    metadata += ["depend = " + name for name in requirements]
    metadata += ["optdepend = libsecret: provider keys using an available Secret Service",
                 "optdepend = python: externally configured scientific worker environment"]
    (staging / ".PKGINFO").write_text("\n".join(metadata) + "\n")
    mtree = subprocess.check_output(["bsdtar", "-c", "-f", "-", "--format=mtree",
                                    "--options=!all,use-set,type,uid,gid,mode,time,size,sha256,link",
                                    "--uid", "0", "--gid", "0", "-C", str(staging), ".PKGINFO", "opt", "usr"])
    with gzip.open(staging / ".MTREE", "wb") as output:
        output.write(mtree)
    path = destination / f"helios-desktop-{version}-1-x86_64.pkg.tar.zst"
    write_tar(path, [(staging / name, name) for name in (".PKGINFO", ".MTREE", "opt", "usr")])
    command("pacman", "-Qip", str(path))
    return path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "output/desktop/build")
    parser.add_argument("--output", type=Path, default=ROOT / "output/packages" / time.strftime("linux-%Y%m%dT%H%M%SZ", time.gmtime()))
    parser.add_argument("--native", action="store_true", help="Also create a pacman package for the current Arch generation.")
    parser.add_argument("--ffmpeg", type=Path, default=Path(shutil.which("ffmpeg") or "/usr/bin/ffmpeg"))
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise ValueError("This packager targets Linux x86_64; use build.sh/macdeployqt for macOS.")
    for tool in ("ldd", "qmake6", "readelf", "strip", "zstd", "pacman"):
        if not shutil.which(tool):
            raise ValueError("Required packaging tool is unavailable: " + tool)
    if args.native and not shutil.which("bsdtar"):
        raise ValueError("bsdtar is required for the native pacman package.")
    build = args.build.resolve()
    if not (build / "bin/Helios").is_file() or not (build / "bin/HeliosWorkerHost").is_file():
        raise ValueError("Build Helios and its worker host before packaging.")
    ffmpeg = args.ffmpeg.resolve()
    if not ffmpeg.is_file():
        raise ValueError("Install the distribution FFmpeg package explicitly before creating a media-enabled Linux artifact.")
    destination = args.output.resolve()
    if destination.exists():
        raise ValueError("Choose a new output directory; packaging never replaces an existing artifact.")
    destination.mkdir(parents=True)
    application = destination / "helios-desktop"
    before = digest(build / "bin/Helios")
    # Do not run the upstream scene libraries' install rules: some use absolute
    # development export paths. The application runtime has this bounded layout.
    for name in ("Helios", "HeliosWorkerHost"):
        copy_file(build / "bin" / name, application / "bin" / name)
    copy_file(ffmpeg, application / "bin/ffmpeg")
    # Worker tests may generate interpreter caches beside these read-only
    # sources. Ship the portable sources, not machine-specific bytecode.
    shutil.copytree(build / "share/helios", application / "share/helios",
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"))
    if before != digest(application / "bin/Helios") or before != digest(build / "bin/Helios"):
        raise ValueError("The executable changed while packaging. Wait for the build and choose a new output directory.")
    qt_version = command("qmake6", "-query", "QT_VERSION")
    qt_plugins = Path(command("qmake6", "-query", "QT_INSTALL_PLUGINS"))
    # These formats cover map imagery and native dialogs. PNG is built into Qt
    # Gui. Do not sweep in unrelated plugins (QML, WebEngine, multimedia, etc.).
    selected = ["platforms/libqwayland.so", "platforms/libqxcb.so", "platforms/libqoffscreen.so",
                "imageformats/libqjpeg.so", "imageformats/libqgif.so", "imageformats/libqico.so",
                "tls/libqopensslbackend.so", "tls/libqcertonlybackend.so",
                "wayland-decoration-client/libbradient.so"]
    for group in ("wayland-shell-integration", "wayland-graphics-integration-client", "xcbglintegrations", "networkinformation"):
        selected += [str(file.relative_to(qt_plugins)) for file in (qt_plugins / group).glob("*.so")]
    for name in selected:
        source = qt_plugins / name
        if not source.is_file():
            raise ValueError("Required matching Qt plugin is unavailable: " + str(source))
        copy_file(source, application / "plugins" / name)
    qt_libraries: dict[str, Path] = {}
    media_libraries: dict[str, Path] = {}
    host_libraries: dict[str, Path] = {}
    for file in [build / "bin/Helios", ffmpeg] + [qt_plugins / name for name in selected]:
        for name, source in dependencies(file).items():
            if name.startswith("libQt6"):
                if any(module in name for module in ("Qml", "Quick", "WebEngine", "WebChannel")):
                    raise ValueError("Unexpected UI/runtime component in package: " + name)
                qt_libraries[name] = source
            elif re.match(r"lib(?:avcodec|avformat|avutil|avdevice|avfilter|swscale|swresample|postproc)\.so", name):
                media_libraries[name] = source
            else:
                host_libraries[name] = source
    qt_packages: dict[str, str] = {}
    for name, source in qt_libraries.items():
        if not source.name.endswith("." + qt_version):
            raise ValueError("Qt library and qmake version differ: " + str(source))
        copy_file(source, application / "lib" / source.name)
        (application / "lib" / name).symlink_to(source.name)
        package, version = package_owner(source)
        qt_packages[package] = version
    for name in selected:
        package, version = package_owner(qt_plugins / name)
        qt_packages[package] = version
    for name, source in media_libraries.items():
        copy_file(source, application / "lib" / source.name)
        (application / "lib" / name).symlink_to(source.name)
    runtime_packages = dict(package_owner(source) for source in host_libraries.values())
    runtime_sonames = soname_requirements(host_libraries)
    licensing = application / "share/helios/licenses"
    for source, name in ((ROOT / "LICENSE", "PROJECT-LICENSE.txt"), (ROOT / "NOTICE.md", "PROJECT-NOTICE.md"),
                         (ROOT / "native/desktop/NOTICE.md", "DESKTOP-NOTICE.md"),
                         (ROOT / "DATA_SOURCES.md", "DATA-SOURCES.md"),
                         (build / "SCENE-NOTICES.txt", "SCENE-NOTICES.txt")):
        copy_file(source, licensing / name)
    resources = ROOT / "native/resources"
    copy_file(resources / "SGP4-LICENSE.txt", licensing / "SGP4-LICENSE.txt")
    for source in resources.glob("*/README.md"):
        copy_file(source, licensing / "datasets" / source.parent.name / source.name)
    for name in qt_packages:
        source = Path("/usr/share/licenses") / name
        if not source.is_dir():
            raise ValueError("No installed license files for bundled Qt component: " + name)
        shutil.copytree(source, licensing / name, dirs_exist_ok=True)
    media_source, media_source_directory, media_recipe_directory, media = ffmpeg_source(ffmpeg, destination)
    for name in ("COPYING.GPLv3", "COPYING.LGPLv3", "COPYING.LGPLv2.1", "LICENSE.md"):
        if (media_source_directory / name).is_file():
            copy_file(media_source_directory / name, licensing / "ffmpeg" / name)
    shutil.copytree(media_recipe_directory, licensing / "ffmpeg/arch-packaging", ignore=shutil.ignore_patterns(".git"))
    (licensing / "ffmpeg/build-configuration.txt").write_text(media["versionOutput"] + "\n\n" + media["licenseOutput"] + "\n")
    (application / "bin/qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nTranslations=translations\n")
    for name in ("helios-desktop", "install-user.py"):
        copy_file(PACKAGING / name, application / name)
        (application / name).chmod(0o755)
    copy_file(PACKAGING / "helios.svg", application / "share/helios/helios.svg")
    copy_file(ROOT / "native/desktop/PACKAGING.md", application / "README.md")
    for executable in ("Helios", "HeliosWorkerHost", "ffmpeg"):
        subprocess.run(["strip", "--strip-unneeded", str(application / "bin" / executable)], check=True)
    snapshot, source_hashes = source_snapshot(destination)
    revision = command("git", "-C", str(ROOT), "rev-parse", "HEAD")
    source_identity = hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode()).hexdigest()
    version = "0.1.0.r" + revision[:8] + "." + source_identity[:8]
    manifest = {
        "schemaVersion": 1, "application": "io.helios.desktop", "version": version,
        "packagedAtUTC": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "platform": "linux-x86_64",
        "qtVersion": qt_version, "qtPackages": qt_packages, "hostPackages": runtime_packages,
        "hostSonameDependencies": runtime_sonames,
        "hostLibraries": {name: str(path) for name, path in sorted(host_libraries.items())},
        "sourceRevision": revision, "sourceSha256": source_identity,
        "sourceSnapshot": {"file": snapshot.name, "sha256": digest(snapshot)}, "sourceFiles": source_hashes,
        "worktreeStatus": command("git", "-C", str(ROOT), "status", "--short"),
        "inputExecutableSha256": before, "bundledLibraries": sorted(qt_libraries), "plugins": sorted(selected),
        "ffmpeg": {**media, "bundledLibraries": sorted(media_libraries)},
        "externalRequirements": ["Host Vulkan/OpenGL loader, driver and ICDs", "Graphical Wayland or X11 session",
                                 "Host fonts and CA certificate trust", "Optional Secret Service/libsecret",
                                 "Configured scientific Python and optional SNAP GPT"],
    }
    (application / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    sources = ["# Bundled runtime sources and replacement", "",
               "Helios is dynamically linked to the Qt files under lib/ and plugins/. You may replace them",
               "with compatible rebuilt libraries. The private RHI ABI requires rebuilding Helios when",
               "using a different Qt build. The matching Helios source snapshot accompanies this package:",
               "`" + snapshot.name + "` (SHA-256 `" + digest(snapshot) + "`).", "",
               "Qt license texts are under share/helios/licenses/. The original Qt source is available at:",
               f"https://download.qt.io/archive/qt/{'.'.join(qt_version.split('.')[:2])}/{qt_version}/submodules/", "",
               "Arch packaging recipes and distributor patches corresponding to the bundled binaries:"]
    for name, version in sorted(qt_packages.items()):
        sources.append(f"- {name} {version}: https://gitlab.archlinux.org/archlinux/packaging/packages/{name}/-/tree/{version}")
    sources += ["", "Cesium and its static dependencies are pinned in the source build. Their license texts",
                "and component names are in SCENE-NOTICES.txt. The source build scripts fetch their pinned",
                "upstream revisions. Scientific workers are shared source files staged by the CMake build.",
                "The package also contains the distribution's FFmpeg executable and libav/libsw runtime",
                "libraries for isolated native clip playback. Their matching upstream source and Arch",
                "recipe/patch archive accompanies this package as `" + media_source.name + "` (SHA-256 `" + digest(media_source) + "`).",
                "Its configure options and GPL3-or-later notice are under share/helios/licenses/ffmpeg/.",
                "Python, SNAP, GPU drivers and models are external and are not included in this package.", "",
                "This is a local migration artifact. Before external binary redistribution, retain the",
                "matching source archives/distributor patches required by each dependency license and",
                "verify any bundled dataset's redistribution terms. Source links alone are not a release audit.", ""]
    (application / "SOURCES.md").write_text("\n".join(sources))
    inventory = {str(file.relative_to(application)): digest(file) for file in sorted(application.rglob("*")) if file.is_file()}
    (application / "SHA256SUMS").write_text("".join(f"{checksum}  {name}\n" for name, checksum in inventory.items()))
    subprocess.run([str(application / "helios-desktop"), "--check-runtime"], check=True)
    artifacts = [snapshot, media_source]
    portable = destination / f"helios-desktop-{manifest['version']}-linux-x86_64.tar.zst"
    write_tar(portable, [(application, "helios-desktop")])
    artifacts.append(portable)
    if args.native:
        artifacts.append(native_package(application, destination, manifest["version"], runtime_packages, runtime_sonames))
    (destination / "SHA256SUMS").write_text("".join(f"{digest(path)}  {path.name}\n" for path in artifacts))
    print("\n".join(str(path) for path in artifacts))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
