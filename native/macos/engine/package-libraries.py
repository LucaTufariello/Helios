"""Merge native static libraries and preserve their component license notices."""
import pathlib
import hashlib
import json
import shlex
import subprocess
import sys

build, source = map(pathlib.Path, sys.argv[1:])
commands = json.loads((build / "compile_commands.json").read_text())
command = next(item for item in commands if item["file"].endswith("GodEyeScene.mm"))
arguments = shlex.split(command["command"])
prefixes = [pathlib.Path(arguments[i + 1]).parent for i, arg in enumerate(arguments[:-1]) if arg == "-isystem"]
installed = next(prefix for prefix in prefixes if prefix.name == "arm64-godeye" and (prefix / "lib/libfmt.a").is_file())
libraries = [build / "libGodEyeScene.a"]
libraries += sorted((build / "cesium").rglob("*.a"))
libraries += sorted((installed / "lib").glob("*.a"))
subprocess.run(["/usr/bin/libtool", "-static", "-o", str(build / "libGodEyeSceneAll.a"), *map(str, libraries)], check=True)
subprocess.run(["/usr/bin/lipo", "-verify_arch", "arm64", str(build / "libGodEyeSceneAll.a")], check=True)
notices = ["GodEye native scene components\n", "Cesium Native v0.65.0, commit 1bca8021711294cc83a5a032599ac72d9b3166c9\n", (source / "LICENSE").read_text()]
for notice in sorted((installed / "share").glob("*/copyright")):
    notices += [f"\n--- {notice.parent.name} ---\n", notice.read_text(errors="replace")]
(build / "SCENE-NOTICES.txt").write_text("\n".join(notices))

# SwiftPM does not track unsafe external linker inputs. Change a compiled C source
# only when the native archive actually changes, forcing a safe relink.
digest = hashlib.sha256((build / "libGodEyeSceneAll.a").read_bytes()).hexdigest()
header = build.parent.parent / "Sources/CScene/include/GodEyeSceneBuild.h"
header_text = '#pragma once\n#define GODEYE_SCENE_BUILD_ID "' + digest + '"\n'
if not header.exists() or header.read_text() != header_text:
    header.write_text(header_text)
