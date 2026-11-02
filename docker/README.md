# CEF binary builds on Ubuntu

The builder uses `ubuntu:26.10`, the latest stable Ubuntu release as of
October 29, 2026. The version is explicit because `ubuntu:latest` tracks the
latest LTS release rather than the newest interim release.
It builds Linux x64 binaries, not Windows binaries. No database, Docker-in-Docker,
or additional service containers are needed.
The custom entrypoint is Bash (`docker/build.sh`). Python is installed only
because CEF and Chromium's upstream build tools require it.

## Requirements

- Docker Engine/Desktop with BuildKit, Linux containers and Docker Compose v2.
- An x64 host/VM. Emulating x64 on ARM is not recommended for Chromium builds.
- At least 32 GB RAM and 200 GB free SSD space; 64 GB RAM and 400 GB+ free
  space are safer for larger configurations. Allocate these resources to the
  Docker VM, not just the host. Initial checkout and compilation take hours.
- Internet access to Docker Hub, Ubuntu package archives, GitHub, Chromium's
  Git servers and Google storage/CIPD.

Chromium's downloaded Clang and Debian sysroot avoid depending on
Ubuntu's target development libraries; the container still needs compatible
host runtime libraries for build tools. A complete build on your Docker host is
necessary to establish compatibility for your chosen CEF revision.

## First build

Docker tooling is owned by this CEF fork, alongside the native source. On the
existing Azure Linux build VM, check out the exact full CEF commit first and
enter its Docker directory. Reach builds must use
`Deployment\build-vm\Invoke-ReleaseBuild.ps1`; do not build CEF locally.

```sh
mkdir -p artifacts
cd docker
python3 compose.py build builder
python3 compose.py run --rm builder all
```

All following commands run from `docker`. `compose.py` forwards Compose arguments
and resolves read-only Git metadata for standalone clones, linked CEF worktrees
and initialized Reach `external/cef` submodules (including linked superprojects).
Use it instead of direct Compose for gitfile-based checkouts. Paths are Linux
Docker-host paths, not Windows paths or remote-client paths.
Before building the
image, set `BUILDER_UID` and `BUILDER_GID` in `.env` to your numeric user/group
IDs if they are not 1000, so the non-root builder can write the artifacts bind
mount. The volume is initialized with that ownership on first use; changing
these IDs later requires migrating the existing volume's ownership.
The stock Ubuntu account is removed so the builder can retain UID/GID 1000.

`artifacts` receives a versioned `cef_binary_*_linux64` SDK directory containing
`Release/libcef.so`, resources, locales, headers, the wrapper source and CMake
files. Distributions are uncompressed and API documentation is omitted to avoid
extra work. This is a development Release build, not an official CEF release.
No GUI application is launched.

## Migrating an existing Azure Linux workspace

Keep the `cef` Compose project and `cef_cef-work` volume. The old separate
cef-docker checkout/history must be retained, not deleted; use a CEF fork root
with its own committed `docker/` tree instead. Do not copy a stale Docker tar
over a new checkout. Native sources and tooling share one immutable revision.
Build the new image once, then reuse the existing Chromium checkout, downloaded
toolchain and object files:

```sh
python3 compose.py build builder
python3 compose.py run --rm builder build
```

The new image is tagged `cef-builder:ubuntu-26.10`; the old Azure Linux image
is not overwritten. Do not run `sync`, change the project name or remove the
volume just to switch the host distribution. Changed source/version metadata
may still require incremental compilation.

## Fast edit/build loop

Edit files in the CEF fork root normally, including uncommitted changes and new
files:

```sh
python3 compose.py run --rm builder build
```

This only compiles. When you need refreshed distributable binaries:

```sh
python3 compose.py run --rm builder package
```

Or use `all` to compile and package in one invocation. Once initialized, `all`
does not sync dependencies again unless `CHROMIUM_BUILD_COMPATIBILITY.txt`
changes. `build` refuses to compile against a mismatched Chromium revision.

To build both configurations:

```sh
python3 compose.py run --rm builder all --config both
```

`--config debug` builds Debug only. Packaging includes any existing Debug and
Release outputs in the volume, so it may include an older, previously built
configuration; use `--config both` to refresh both before distributing them.
Without `--jobs`, the builder uses `nproc` to run one parallel job per available
container CPU. Use `--jobs N` to lower concurrency if Docker runs out of memory.

## What is retained

`cef-work` is a Compose-managed named volume containing Chromium, depot_tools,
downloaded SDKs/sysroots, Python/CIPD caches and `out/*` object files. Keeping
Chromium on the Linux volume avoids Windows bind-mount metadata overhead.
The host CEF root and resolved Git/common directories are read-only in the container;
a local Git mirror and
checksum-based rsync copy edits into the Linux volume. Unchanged files retain
their timestamps, and deletions propagate without deleting binary distribution
directories or files listed in CEF's translator-generated inventory. Explicit
host edits still replace generated files. The CEF source tree is small compared
with Chromium.
Git trust exceptions are scoped to this source checkout and its resolved Git/common directories
for clone/fetch, including when the bind mount is owned by a different user.
No host Git configuration or ownership changes are required.

New CEF mirrors and depot_tools checkouts use depth-one Git clones. CEF mirror
fetches also stay depth one. Chromium and its Git dependencies use upstream
`--no-chromium-history` / `gclient sync --no-history`, checking out the exact
compatible Chromium version without downloading repository history. Cached
depot_tools is reused with auto-update disabled; its Python/CIPD tools are
initialized explicitly with upstream `ensure_bootstrap`. Existing checkouts are not deleted
or recloned; previously downloaded Git objects may remain on disk.

Upstream no-history mode cannot switch an existing Chromium checkout to a different
version in place. Use a new Compose project (for example
`python3 compose.py -p cef-next run --rm builder all`) when changing to a CEF revision
that requires a different Chromium version. This leaves the previous build cache
intact. Shallow CEF history also limits history-derived version metadata; these
remain development builds, not official CEF releases.

Only the dependency installation and entrypoint are baked into the image.
APT installs exclude recommended packages, with locked BuildKit caches for
package downloads and indexes rather than storing them in the image.
Host GCC, C++ development headers and Make are omitted because Chromium
supplies the compiler and target sysroot.
The Docker build context excludes the source checkout. CEF edits therefore
require neither rebuilding the image nor downloading Chromium again. Persistent
object files supply the primary incremental-build cache; no additional compiler
cache service or duplicate object cache is required.

After pulling/checking out CEF, `all` uses the Chromium revision specified by
that local checkout. To refresh dependencies or recover an interrupted sync:

```sh
python3 compose.py run --rm builder sync
python3 compose.py run --rm builder all
```

`sync` can revert the **container-managed Chromium** checkout as part of upstream
automation. Keep edits in the host CEF clone, not in managed Chromium or CEF
mirrors. For changes to CEF's Chromium patch files, follow the upstream patch
update/reapply workflow before rebuilding; incremental compilation alone cannot
replace a patch that was already applied.

Keep the CEF checkout initialized. The wrapper mounts only the required external
Git directories read-only at their original absolute locations, preserving
relative gitfile and `commondir` resolution without rewriting the host checkout.
Missing Git metadata fails before Docker, and bind mounts never create missing
host metadata directories. Generated mount configuration lives briefly under
`.artifacts` and is removed after Compose exits. Only one invocation may use the
workspace volume at a time. Separate Compose project names (`docker compose -p`)
create independent volumes for parallel branches. `docker compose down` keeps
the volume; **`docker compose down -v` deletes the expensive build state**.

## Build settings

Set `GN_DEFINES` in `.env` to replace the default GN arguments:

```dotenv
GN_DEFINES=use_sysroot=true is_component_build=false symbol_level=1 is_cfi=false use_thin_lto=false use_remoteexec=false enable_remoting=false proprietary_codecs=true ffmpeg_branding="Chrome"
```

Keep `use_sysroot=true` for this image and `is_component_build=false` for binary
distributions. The defaults disable LTO/CFI and reduce symbols for lower local
build overhead; they are not production hardening recommendations. Changing GN
arguments may require substantial recompilation. `cefsimple` and `chrome_sandbox`
are built; `cefclient` is deliberately avoided because of its GTK dependency.
`enable_remoting=false` excludes Chromium's separate remote-desktop product and
its additional desktop packaging dependency probes.

H.264 playback (including video encoded with x264) and AAC support are enabled
by default with `proprietary_codecs=true ffmpeg_branding="Chrome"`. Keep both
arguments when overriding `GN_DEFINES`. This enables Chromium's codec support;
it does **not** link the GPL/commercial libx264 encoder into CEF or guarantee
hardware acceleration. Review applicable codec/patent licensing obligations
before distributing these binaries; build flags do not grant licenses.
These settings do not promise every proprietary codec: HEVC/H.265, Dolby formats,
DRM/CDM integration and licensed encoding require separate platform-specific
support and licensing.

To pin the image to an immutable digest, set `BASE_IMAGE` in `.env` to
`ubuntu:26.10@sha256:<digest>` and rebuild. Overrides must provide an
Ubuntu-compatible base with APT.
Dependency upgrades also require rebuilding the image; source-only edits do not.

## Official (optimized) profile

The default `release` profile above is unchanged. An explicitly selected
`official` profile builds a maximally optimized Release libcef with
`is_official_build=true`, ThinLTO and PGO, then publishes stripped binaries with
separate debug symbols. It runs only in the `builder-official` Compose service:

```sh
python3 compose.py build -q builder-official
python3 compose.py run --rm builder-official all --profile official
```

`build`, `sync` and `package` accept `--profile official` the same way. Only
`--config release` is supported. `builder-official` refuses the release profile,
`builder` refuses the official profile, and the official profile refuses to run
unless `out/` is a separate mount.

### Isolation and disk

`builder-official` shares the `cef-work` volume (depot_tools, the Chromium
checkout, toolchains, sysroot and the `/work/.cef-build.lock` serialization)
with `builder`, but mounts the separate `cef-official-out` volume over
`/work/chromium/src/out`. Every per-configuration file that CEF's
`gclient_hook.py`, GN, Ninja, `reset_patched_files.py` and `make_distrib.py`
write lives under `out/`, so official configuration and object files never
touch the release `out/Release_GN_x64` tree or its incremental state. The
official volume also uses the fixed CEF name `out/Release_GN_x64` internally.
Budget a second full Release output tree (typically 100 GB or more; ThinLTO
caches add more) plus the PGO profile (about 1 GB).

Run the first `sync` with the `builder` service: the official service requires
an existing shared checkout. Do not start `builder-official` against a new,
empty `cef-work` volume; Docker would create the `out` mount point before
Chromium is checked out. If that happens, remove `/work/chromium` with the
`builder` service before syncing.

Shared state outside `out/` that an official run can update is identical to
what a release run writes: the CEF source mirror and translator output in
`src/cef`, re-applied Chromium patches, the sysroot, and (only if it already
exists) `src/compile_commands.json`. The official run additionally downloads
PGO profiles into `src/chrome/build/pgo_profiles/`, which release builds ignore.
Official builds are serialized with release builds by the shared lock; they
cannot run concurrently.

### Build settings and PGO

The service sets `GN_DEFINES` from `OFFICIAL_GN_DEFINES`, never from the release
`GN_DEFINES` value:

```dotenv
OFFICIAL_GN_DEFINES=use_sysroot=true is_component_build=false is_official_build=true symbol_level=1 use_debug_fission=false is_cfi=false use_remoteexec=false enable_remoting=false proprietary_codecs=true ffmpeg_branding="Chrome"
```

`use_thin_lto` is left at Chromium's official default (enabled). CEF itself adds
`disable_fieldtrial_testing_config=true` and disables PartitionAlloc-Everywhere
and BackupRefPtr for official Linux builds. `use_debug_fission=false` keeps the
complete DWARF in `libcef.so` so packaging can move it into one `.debug` file;
with Chromium's default split DWARF most debug information would remain as
`.dwo` files inside the official out volume. `is_official_build=true` is
required.

Chromium defaults official Linux builds to `chrome_pgo_phase=2`, which compiles
with a profile named by `chrome/build/linux.pgo.txt`. `sync` and `all` download
it with `tools/update_pgo_profiles.py --target=linux update
--gs-url-base=chromium-optimization-profiles/pgo_profiles`, the same command as
Chromium's `checkout_pgo_profiles` DEPS hook, without changing the shared
`.gclient`. `build` does not download; it fails before GN generation if the
profile is missing. To opt out of PGO explicitly, append `chrome_pgo_phase=0` to
`OFFICIAL_GN_DEFINES` (or supply your own `pgo_data_path`).

### Artifacts and symbols

Official artifacts are written to `artifacts/official`:

```text
artifacts/official/
  cef_binary_<version>_linux64/                    # runtime distribution
    Release/libcef.so                              # stripped, .gnu_debuglink
  cef_binary_<version>_linux64_release_symbols/
    libcef.so.debug                                # DWARF and .symtab
    .build-id/<xx>/<rest>.debug -> ../../libcef.so.debug
    manifest.json
```

`libcef.so` is stripped with the Chromium checkout's
`third_party/llvm-build/Release+Asserts/bin/llvm-objcopy --strip-unneeded`,
which removes debug sections and non-dynamic symbols but keeps `.dynsym`. Other
distribution files, including `chrome-sandbox` and the other shared libraries,
are not modified. Packaging happens in a hidden staging directory; the outputs
are renamed into place only after verifying that the library and debug file
share one GNU build-id, the library has no `.debug_*` sections and has
`.gnu_debuglink`, and the debug file contains DWARF. A failure leaves previously
published outputs unchanged.

`manifest.json` (`"format": "cef-official-symbols/v1"`) records the SHA-256 of
the stripped `Release/libcef.so` and of `libcef.so.debug`, the build-id, the PGO
phase and profile name, selected effective GN values (`gn args --list`) and the
verbatim `args.gn`.

To debug, either copy `libcef.so.debug` next to the deployed `libcef.so` (GDB
follows the debuglink), or point GDB at the symbols directory, which it searches
by build-id:

```sh
gdb -iex 'set debug-file-directory /path/to/cef_binary_<version>_linux64_release_symbols' ./app
```

`llvm-symbolizer --debug-file-directory=<symbols dir>` and `eu-unstrip` work the
same way.

## Entrypoint checks

The orchestration tests do not download or compile Chromium:

```sh
python3 -m unittest discover -s docker -p '*_test.py'
python3 compose.py config --quiet
python3 compose.py build builder
python3 compose.py run --rm --entrypoint bash builder /builder/build_test.sh
python3 compose.py run --rm --user root --entrypoint bash builder /builder/ownership_test.sh
```

Only the Python fixtures run without Docker. Container ownership, rsync mirror,
and native checks require the exclusively leased Azure build VM; source-only
fixture success is not build or runtime qualification.

## Source provenance

The retained tracked `docker/` tree was imported with `git archive` from
`msabansal/cef-docker` commit `037f7893ab12395ad7bbf7f64eb00382c5de7343`,
local checkout `Q:\az\cef`. Original tooling authorship is Sandeep Bansal
(`sabansal@microsoft.com`); introduction and incremental-build commits include
`866ee4c`, `2eac671`, `5e6ceb6`, and `56fe480`. The source tree's Git tree ID is
recorded in the consolidation commit message. No nested CEF gitlink, uncommitted
helper changes, caches or artifacts were imported. The separate repository is
retained; only its active dependency is retired. No native patches were changed
by this layout migration, and no new qualified native hash is claimed.

Upstream references:
[CEF quick start](https://chromiumembedded.github.io/cef/master_build_quick_start),
[CEF building and packaging](https://chromiumembedded.github.io/cef/branches_and_building),
[Chromium Linux builds](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md).
