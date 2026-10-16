#!/usr/bin/env python3
"""Instrument the owned scene adapter, reusing a completed build's upstream archives."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "output/desktop/build")
    parser.add_argument("--output", type=Path, default=ROOT / "output/desktop/scene-sanitizer")
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    entries = json.loads((build / "compile_commands.json").read_text()) if (build / "compile_commands.json").is_file() else []
    link_file = build / "native/desktop/CMakeFiles/GodEyeSceneTests.dir/link.txt"
    if not link_file.is_file():
        raise ValueError("Use a completed Unix Makefiles build.")
    if not any(Path(entry["file"]).resolve() == ROOT / "native/scene/Scene.cpp" for entry in entries):
        # Some third-party CMake projects enable compile_commands only in their
        # directory. Read the owned Makefiles' exact flags in that case.
        compiler = re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.+)$", (build / "CMakeCache.txt").read_text(), re.MULTILINE)
        if not compiler:
            raise ValueError("Cannot identify the build's C++ compiler.")
        entries = []
        for target, directory, sources in (
            ("GodEyeScene", build / "native/scene", [(ROOT / "native/scene/Scene.cpp", "Scene.cpp"),
             (ROOT / "native/scene/SceneTransportQt.cpp", "SceneTransportQt.cpp"),
             (build / "native/scene/GodEyeScene_autogen/mocs_compilation.cpp", "GodEyeScene_autogen/mocs_compilation.cpp")]),
            ("GodEyeSceneTests", build / "native/desktop", [(ROOT / "native/desktop/Tests/SceneTests.cpp", "Tests/SceneTests.cpp"),
             (build / "native/desktop/GodEyeSceneTests_autogen/mocs_compilation.cpp", "GodEyeSceneTests_autogen/mocs_compilation.cpp")]),
        ):
            flags_text = (directory / "CMakeFiles" / (target + ".dir") / "flags.make").read_text()
            flags = []
            for kind in ("DEFINES", "INCLUDES", "FLAGS"):
                match = re.search(r"^CXX_" + kind + r" = (.*)$", flags_text, re.MULTILINE)
                if match:
                    flags += shlex.split(match.group(1))
            for source, object_name in sources:
                entries.append({"file": str(source), "directory": str(directory),
                                "arguments": [compiler.group(1), *flags, "-o", "CMakeFiles/" + target + ".dir/" + object_name + ".o", "-c", str(source)]})
    output.mkdir(parents=True, exist_ok=True)
    objects: dict[Path, Path] = {}
    scene_objects: list[Path] = []
    compiled: list[dict] = []
    for entry in entries:
        file = Path(entry["file"]).resolve()
        scene = file.parent == ROOT / "native/scene" or "GodEyeScene_autogen" in file.parts
        test = file == ROOT / "native/desktop/Tests/SceneTests.cpp" or "GodEyeSceneTests_autogen" in file.parts
        if not scene and not test:
            continue
        command = entry.get("arguments") or shlex.split(entry["command"])
        directory = Path(entry["directory"])
        object_position = command.index("-o")
        original_object = (directory / command[object_position + 1]).resolve()
        destination = output / ("scene-" if scene else "test-") / (file.name + ".o")
        destination.parent.mkdir(parents=True, exist_ok=True)
        command[object_position + 1] = str(destination)
        clean: list[str] = []
        index = 0
        while index < len(command):
            if command[index] in ("-MT", "-MF", "-MQ"):
                index += 2
                continue
            if command[index] not in ("-MD", "-MMD", "-MP"):
                clean.append(command[index])
            index += 1
        clean += ["-O0", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(clean, cwd=directory, check=True)
        objects[original_object] = destination
        if scene:
            scene_objects.append(destination)
        compiled.append({"source": str(file.relative_to(ROOT)), "command": clean})
    if len(scene_objects) < 2 or len(objects) < 3:
        raise ValueError("The build did not contain both owned scene sources and scene tests.")
    archive = output / "libGodEyeSceneSanitized.a"
    if archive.exists():
        archive.unlink()
    subprocess.run(["ar", "rcs", str(archive), *map(str, scene_objects)], check=True)
    executable = output / "GodEyeSceneTestsSanitized"
    original = shlex.split(link_file.read_text())
    link: list[str] = []
    index = 0
    while index < len(original):
        argument = original[index]
        if argument == "-o":
            link += [argument, str(executable)]
            index += 2
            continue
        if argument.startswith("-Wl,--dependency-file="):
            index += 1
            continue
        resolved = (link_file.parents[2] / argument).resolve()
        if resolved in objects:
            argument = str(objects[resolved])
        elif argument.endswith("/libGodEyeScene.a"):
            argument = str(archive)
        link.append(argument)
        index += 1
    link += ["-fsanitize=address,undefined"]
    subprocess.run(link, cwd=link_file.parents[2], check=True)
    environment = {**os.environ, "ASAN_OPTIONS": os.environ.get("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=1"),
                   "UBSAN_OPTIONS": os.environ.get("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")}
    started = time.monotonic()
    with (output / "test.log").open("w") as log:
        result = subprocess.run([str(executable)], env=environment, cwd=output, stdout=log, stderr=subprocess.STDOUT)
    (output / "report.json").write_text(json.dumps({"exitCode": result.returncode, "elapsedSeconds": time.monotonic() - started,
                                                  "compiled": compiled, "link": link, "asan": environment["ASAN_OPTIONS"],
                                                  "ubsan": environment["UBSAN_OPTIONS"],
                                                  "scope": "Owned scene adapter and its tests; upstream archives, Qt and GPU are uninstrumented."}, indent=2) + "\n")
    print((output / "test.log").read_text())
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
