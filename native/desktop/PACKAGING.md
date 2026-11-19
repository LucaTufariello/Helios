# Linux desktop package

The Linux package targets x86_64 Omarchy/Arch and the system-library generation
used to build it. It bundles the exact Qt shared libraries and selected Wayland,
X11, JPEG, TLS and network plugins used by the application. The launcher selects
these libraries and plugins together because Qt's private RHI ABI must match.
For native clip playback it also carries the distribution FFmpeg executable
and libav/libsw shared libraries, without a Qt Quick/QML media runtime.
The host supplies its graphics driver, Vulkan/OpenGL loader and ICDs, graphical
session, fonts, CA certificates and other libraries listed in
`package-manifest.json`. This is not an all-distribution Linux binary.

Build and package from the repository root:

```sh
make test-desktop
python3 native/desktop/package-linux.py --native
```

The script snapshots an existing completed build. It never installs packages,
changes the graphics driver or replaces an existing package output. Artifacts
appear in a new `output/packages/linux-UTC/` directory: a relocatable `.tar.zst`,
an optional native `.pkg.tar.zst`, matching Helios and FFmpeg source snapshots,
the FFmpeg distribution recipe/patch, and checksums.
The native package declares its external Arch dependencies. Retain all artifacts
together. This is a migration build; platform acceptance is recorded separately.
The distribution's `ffmpeg` package must be installed explicitly before
packaging. The script fetches matching FFmpeg source and the Arch recipe into
`output/vendor` for the source artifact; it never installs runtime packages.

After extracting the relocatable archive, change into `helios-desktop/`:

```sh
./helios-desktop --check-runtime
./helios-desktop
```

Optional user installation adds a desktop entry and copies the app to
`$XDG_DATA_HOME/helios-desktop` (normally `~/.local/share/helios-desktop`):

```sh
python3 install-user.py
```

The installer refuses an existing application or desktop entry. To remove that
installation, run `python3 install-user.py --uninstall` from the extracted
package or installed directory; custom `--prefix`/`--desktop-dir` options must
match the original installation. Working profiles, imagery, missions, jobs and
settings are stored separately and are preserved. Installing the native package
with the distribution package manager instead places the app under `/opt` and
adds `helios-desktop` to the executable search path.
Both desktop entries register support for `helios://` view links and pass one
URI directly to the app. They do not force the system's default association.

For isolated checks, use a new profile:

```sh
./helios-desktop --profile /absolute/path/to/disposable-profile --offline
./helios-desktop --validate --qa-output /absolute/path/to/new-qa-output
```

`--check-runtime` checks ELF dependencies and Qt library resolution. It does not
prove hardware rendering, HTTPS trust or scientific processing. Run GPU checks
inside the actual graphical session and inspect their report and screenshots.
`--validate-live` additionally exercises HTTPS map/terrain requests. A clean
profile does not alter an existing mission or satellite library.

The repeatable package check copies the app to a disposable directory outside
the checkout, checks its hashes and Qt resolution, and exercises user
installation/removal with paths containing spaces. To add actual GPU and HTTPS
checks in a graphical session:

```sh
python3 native/desktop/packaging/check-package.py \
  --package output/packages/linux-UTC/helios-desktop \
  --output output/qa/package-UTC --gpu --live
```

Use `--raster-python /absolute/path/to/python3` for the generated scientific
worker checks and `--inference-python /absolute/path/to/python` for generated
CPU model and proposal checks. Neither option downloads model weights.
Logs, screenshots, runtime hashes and reports remain in the new
evidence directory. Omitting `--gpu` and `--live` permits a headless package check;
that does not establish rendering or network acceptance.

The package includes shared scientific worker sources and their native process
host. Scientific Python/Rasterio/GDAL/NumPy/Pillow/h5py and SNAP GPT remain
explicit external processing dependencies. Configure their executable paths in
Processing Center. Ordinary map startup does not install or invoke them. No
CUDA runtime or detector model is downloaded by packaging or startup.

Project, Qt and static scene component licenses are retained under
`share/helios/licenses/`; `SOURCES.md` records source locations and replacement
instructions. Before public binary redistribution, retain corresponding third
party sources/distributor patches and review dataset terms. Local packaging and
an ad-hoc Mac signature are not a notarized or audited public release.

# macOS bundle

`native/desktop/build.sh` creates `Helios.app` on Apple Silicon, deploys
its matching Homebrew Qt frameworks/plugins, copies the worker host, workers and
licenses, and verifies an ad-hoc signature. The CI Qt job builds and checks that
bundle on an arm64 runner. Its notice collector retains installed Homebrew Qt
and dependency license files, source recipes, and deployed library linkage;
missing notices are explicitly reported. A hosted compile/test job does not establish Metal,
physical input, accessibility or Apple M2 runtime acceptance. The Qt client is
the only application; hosted results do not replace hardware sessions.
Mac clip playback requires an external `ffmpeg`; the app checks next to its
executable, standard Homebrew locations, then the executable search path. Missing
FFmpeg does not prevent ordinary map startup.

# Sanitizer checks

`native/desktop/sanitize-core.sh` creates a separate build that instruments
owned geometry, storage, network, job code and its tests with AddressSanitizer
and UndefinedBehaviorSanitizer. It uses the same explicit source lists as the
normal build and excludes the scene test target to avoid recompiling upstream
scene dependencies. `python3 native/desktop/sanitize-scene.py` separately
instruments the two owned scene adapter sources and their tests while linking
the already built upstream archives. The latter requires a completed Unix
Makefiles build. Neither command instruments Qt, GPU code or upstream archives;
leak detection is disabled by default and remains a separate acceptance gate.
