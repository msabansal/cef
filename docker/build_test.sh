#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ ! -f "$script_dir/build.sh" ]]; then
    echo "FAIL: Bash build entrypoint is missing" >&2
    exit 1
fi
source "$script_dir/build.sh"

test_root=${CEF_TEST_DIRECTORY:-/work/.artifacts}/build-test-$BASHPID-$RANDOM
mkdir -p "$test_root"
trap 'rm -rf -- "$test_root"' EXIT
source_dir=$test_root/source
work=$test_root/work
artifacts=$test_root/artifacts
cef=$work/cef
src=$work/chromium/src
stamp=$work/.cef-synced-compatibility
mkdir -p "$source_dir" "$cef" "$src/.git" "$src/cef/tools" "$work/depot_tools"
printf '#!/usr/bin/env bash\nexit "${BOOTSTRAP_TEST_STATUS:-0}"\n' \
    >"$work/depot_tools/ensure_bootstrap"
chmod +x "$work/depot_tools/ensure_bootstrap"
printf "%s\n" "{'chromium_checkout': 'refs/tags/156.0.8078.0'}" \
    > "$source_dir/CHROMIUM_BUILD_COMPATIBILITY.txt"
cp "$source_dir/CHROMIUM_BUILD_COMPATIBILITY.txt" "$cef/"
cp "$cef/CHROMIUM_BUILD_COMPATIBILITY.txt" "$stamp"
log=$test_root/commands
gn_log=$test_root/gn-defines

fail() { echo "FAIL: $*" >&2; exit 1; }

# Only the external Chromium tools are replaced; Git and rsync remain real.
python3() {
    printf 'python3 %s %s\n' "$*" "${GN_OUT_CONFIGS:-}" >> "$log"
    if [[ "$1" == tools/gclient_hook.py ]]; then
        printf '%s\n' "${GN_DEFINES:-}" > "$gn_log"
    fi
    return "${python_status:-0}"
}
autoninja() { printf 'autoninja %s\n' "$*" >> "$log"; }
nproc() { printf '3\n'; }

config=release
jobs=4
unset GN_DEFINES
compile
grep -q 'gclient_hook.py Release_GN_x64' "$log" ||
    fail "Release configuration was not generated"
grep -q 'autoninja -C out/Release_GN_x64 -j 4 cefsimple chrome_sandbox' "$log" ||
    fail "Incremental build did not honor the job limit"
[[ $(wc -l < "$log") -eq 4 ]] || fail "Incremental build synced dependencies"
grep -q 'reset_patched_files.py reset ' "$log" ||
    fail "Incremental build did not reset the registered native patches"
grep -q 'reset_patched_files.py restore ' "$log" ||
    fail "Incremental build did not restore unchanged native patch timestamps"
grep -q -F 'proprietary_codecs=true' "$gn_log" ||
    fail "Build did not enable proprietary codecs"
grep -q -F 'ffmpeg_branding="Chrome"' "$gn_log" ||
    fail "Build did not select Chrome FFmpeg codecs"

printf 'changed\n' > "$cef/CHROMIUM_BUILD_COMPATIBILITY.txt"
if (compile) 2>"$test_root/error"; then
    fail "Changed Chromium revision was accepted"
fi
grep -q sync "$test_root/error" || fail "Missing sync instructions"
cp "$source_dir/CHROMIUM_BUILD_COMPATIBILITY.txt" "$cef/"

if (package) 2>"$test_root/error"; then
    fail "Packaging accepted missing binaries"
fi
[[ ! -d "$artifacts" ]] || fail "Failed packaging created artifacts"
mkdir -p "$src/out/Release_GN_x64"
touch "$src/out/Release_GN_x64/libcef.so"
package
grep -q -- "--output-dir=$artifacts .*--no-archive" "$log" ||
    fail "Packaging did not disable compression"
[[ "$(< "$gn_log")" == "$release_gn_defines" &&
   "$release_gn_defines" == 'use_sysroot=true is_component_build=false symbol_level=1 is_cfi=false use_thin_lto=false use_remoteexec=false enable_remoting=false proprietary_codecs=true ffmpeg_branding="Chrome"' ]] ||
    fail "Default release GN arguments changed"
! grep -q 'update_pgo_profiles' "$log" || fail "Release build touched PGO profiles"
[[ -z $(find "$artifacts" -mindepth 1 -print -quit 2>/dev/null) ]] ||
    fail "Release packaging created extra artifacts or staging directories"
(profile=release; unset CEF_BUILD_SERVICE_PROFILE; check_service_profile) ||
    fail "Release profile was rejected by the release service"

# Official profile: separate service guard, GN defaults, PGO and symbols.
official_log=$test_root/official-commands
fake_mount=1
mountpoint() { [[ $fake_mount -eq 1 ]]; }
for args in 'build --profile official --config debug' \
            'build --profile official --config both' 'build --profile bogus'; do
    read -r -a words <<< "$args"
    if (parse_args "${words[@]}") 2>"$test_root/error"; then
        fail "Invalid official arguments accepted: $args"
    fi
done
if (profile=official; unset CEF_BUILD_SERVICE_PROFILE
    check_service_profile) 2>"$test_root/error"; then
    fail "Official profile ran in the release service"
fi
grep -q 'builder-official' "$test_root/error" ||
    fail "Missing official service instructions"
if (profile=release; CEF_BUILD_SERVICE_PROFILE=official
    check_service_profile) 2>"$test_root/error"; then
    fail "Release profile ran in the official service"
fi
if (profile=official; CEF_BUILD_SERVICE_PROFILE=official; fake_mount=0
    check_service_profile) 2>"$test_root/error"; then
    fail "Official profile ran without a separate out mount"
fi
(profile=official; CEF_BUILD_SERVICE_PROFILE=official; check_service_profile) ||
    fail "Official profile was rejected by the official service"

official_python() {
    printf 'python3 %s\n' "$*" >> "$official_log"
    case "$*" in
        *tools/gclient_hook.py*) printf '%s\n' "$GN_DEFINES" > "$gn_log" ;;
        *get_profile_path*) return "${pgo_status:-0}" ;;
    esac
}
(
    python3() { official_python "$@"; }
    profile=official config=release jobs=2
    unset GN_DEFINES
    compile
    [[ "$(< "$gn_log")" == "$official_gn_defines" ]] ||
        fail "Official GN defaults were not used"
    for define in is_official_build=true symbol_level=1 \
                  use_debug_fission=false proprietary_codecs=true; do
        grep -q -F -- "$define" "$gn_log" || fail "Official build lacks $define"
    done
    ! grep -q -e 'use_thin_lto=false' -e 'chrome_pgo_phase' "$gn_log" ||
        fail "Official build disabled ThinLTO or PGO"
    grep -q 'update_pgo_profiles.py --target=linux get_profile_path' \
        "$official_log" || fail "Official build did not check the PGO profile"
    grep -q 'autoninja -C out/Release_GN_x64 -j 2' "$log" ||
        fail "Official build did not target the shadowed Release output"
)
: > "$official_log"
if (python3() { official_python "$@"; }
    profile=official config=release pgo_status=1
    unset GN_DEFINES
    compile) 2>"$test_root/error"; then
    fail "Official build accepted a missing PGO profile"
fi
grep -q 'PGO profile is missing' "$test_root/error" ||
    fail "Missing PGO profile error was unclear"
! grep -q gclient_hook "$official_log" ||
    fail "Missing PGO profile still configured the build"
: > "$official_log"
(
    python3() { official_python "$@"; }
    profile=official config=release pgo_status=1
    GN_DEFINES="$official_gn_defines chrome_pgo_phase=0"
    compile
    fetch_pgo_profile
    ! grep -q update_pgo_profiles "$official_log" ||
        fail "PGO opt-out still required a profile"
)
: > "$official_log"
(
    python3() { official_python "$@"; }
    GN_DEFINES=$official_gn_defines
    fetch_pgo_profile
    grep -q -- '--target=linux update --gs-url-base=chromium-optimization-profiles/pgo_profiles' \
        "$official_log" || fail "Official sync did not download the PGO profile"
)
if (profile=official config=release
    GN_DEFINES='use_sysroot=true is_component_build=false'
    python3() { :; }
    compile) 2>"$test_root/error"; then
    fail "Official profile accepted non-official GN arguments"
fi

# Symbol split: stub LLVM tools model sections as '.name' lines in the files.
llvm_bin=$src/third_party/llvm-build/Release+Asserts/bin
mkdir -p "$llvm_bin" "$src/chrome/build"
cat > "$llvm_bin/llvm-objcopy" <<'OBJCOPY'
#!/usr/bin/env bash
set -euo pipefail
case "$1" in
    --only-keep-debug)
        [[ "${OBJCOPY_FAIL:-}" != debug ]] || exit 3
        grep -e '^build-id=' -e '^\.debug_' -e '^\.symtab' "$2" > "$3" ;;
    --strip-unneeded)
        [[ "${OBJCOPY_FAIL:-}" != strip ]] || exit 4
        link=${2#--add-gnu-debuglink=}
        [[ -f "$link" ]] || exit 5
        { grep -v -e '^\.debug_' -e '^\.symtab' "$3"; echo .gnu_debuglink; } > "$4" ;;
    *) exit 2 ;;
esac
OBJCOPY
cat > "$llvm_bin/llvm-readelf" <<'READELF'
#!/usr/bin/env bash
set -euo pipefail
file=${!#}
if [[ "$1" == -n ]]; then
    id=$(sed -n 's/^build-id=//p' "$file")
    [[ "${READELF_BAD_ID:-}" != 1 || "$file" != *.debug ]] || id=ffff
    echo "  GNU 0x14 NT_GNU_BUILD_ID"
    echo "    Build ID: $id"
else
    grep '^\.' "$file" | sed 's/^/  [ 1] /'
fi
READELF
chmod +x "$llvm_bin/llvm-objcopy" "$llvm_bin/llvm-readelf"
printf 'chrome-linux-main-1.profdata\n' > "$src/chrome/build/linux.pgo.txt"
printf 'is_official_build=true\n' > "$src/out/Release_GN_x64/args.gn"
dist_name='cef_binary_1.0.0+gabcdef0+chromium-156.0.8078.0_linux64'
official_artifacts=$test_root/official-artifacts
official_package() {
    (
        artifacts=$official_artifacts profile=official
        python3() {
            local output=${2#--output-dir=}
            [[ "$1" == make_distrib.py ]] || return 9
            mkdir -p "$output/$dist_name/Release"
            printf 'build-id=ABCDEF12\n.text\n.dynsym\n.symtab\n.debug_info\n.debug_line\n' \
                > "$output/$dist_name/Release/libcef.so"
            printf 'not elf\n' > "$output/$dist_name/Release/chrome-sandbox"
        }
        gn() {
            printf 'is_official_build = true\nchrome_pgo_phase = 2\n'
            printf 'pgo_data_path = ""\nuse_thin_lto = true\nsymbol_level = 1\n'
        }
        package
    )
}
official_package > /dev/null
published=$official_artifacts/$dist_name
symbols=${published}_release_symbols
[[ -f "$published/Release/libcef.so" && -f "$symbols/libcef.so.debug" ]] ||
    fail "Official package did not publish the distribution and symbols"
! grep -q '^\.debug_' "$published/Release/libcef.so" ||
    fail "Published libcef.so still has debug sections"
grep -q '^\.dynsym' "$published/Release/libcef.so" ||
    fail "Published libcef.so lost .dynsym"
grep -q '^\.gnu_debuglink' "$published/Release/libcef.so" ||
    fail "Published libcef.so has no debuglink"
grep -q '^\.debug_info' "$symbols/libcef.so.debug" ||
    fail "Debug file has no DWARF"
[[ "$(< "$published/Release/chrome-sandbox")" == 'not elf' ]] ||
    fail "Non-libcef files were modified"
[[ -e "$symbols/.build-id/ab/cdef12.debug" ]] || fail "Missing build-id link"
manifest=$symbols/manifest.json
lib_sha=$(sha256sum "$published/Release/libcef.so")
debug_sha=$(sha256sum "$symbols/libcef.so.debug")
grep -q "\"sha256\": \"${lib_sha%% *}\"" "$manifest" ||
    fail "Manifest has the wrong libcef.so hash"
grep -q "\"debug_sha256\": \"${debug_sha%% *}\"" "$manifest" ||
    fail "Manifest has the wrong debug file hash"
for expected in '"build_id": "abcdef12"' '"phase": "2"' \
                '"profile": "chrome-linux-main-1.profdata"' \
                '"is_official_build": "true"' '"use_thin_lto": "true"' \
                '"args_gn": "is_official_build=true"'; do
    grep -q -F -- "$expected" "$manifest" || fail "Manifest lacks $expected"
done
python3_real=$(type -P python3 || type -P python || true)
if [[ -n "$python3_real" ]]; then
    "$python3_real" -c 'import json, sys; json.load(open(sys.argv[1]))' \
        "$(cygpath -w "$manifest" 2>/dev/null || echo "$manifest")" ||
        fail "Manifest is not valid JSON"
fi
[[ -z $(find "$official_artifacts" -maxdepth 1 -name '.official-staging.*' \
    -print -quit) ]] || fail "Official package left a staging directory"

snapshot() { (cd "$official_artifacts" && find . -type f -exec sha256sum {} + | sort); }
before=$(snapshot)
for failure in OBJCOPY_FAIL=strip OBJCOPY_FAIL=debug READELF_BAD_ID=1; do
    if (export "$failure"; official_package) > /dev/null 2>"$test_root/error"; then
        fail "Official package ignored $failure"
    fi
    [[ "$(snapshot)" == "$before" ]] ||
        fail "$failure modified the published official outputs"
    [[ -z $(find "$official_artifacts" -maxdepth 1 -name '.official-staging.*' \
        -print -quit) ]] || fail "$failure left a staging directory"
done
grep -q 'Build-id mismatch' "$test_root/error" ||
    fail "Build-id mismatch was not reported"
rm "$llvm_bin/llvm-objcopy"
if official_package > /dev/null 2>"$test_root/error"; then
    fail "Official package ran without llvm-objcopy"
fi
grep -q 'llvm-objcopy' "$test_root/error" || fail "Missing LLVM tool error was unclear"
[[ "$(snapshot)" == "$before" ]] || fail "Missing tools modified outputs"
echo "PASS: official profile guard, GN defaults, PGO and symbol split"

config=debug
jobs=
compile
grep -q 'gclient_hook.py Debug_GN_x64' "$log" ||
    fail "Debug configuration was not generated"
grep -q 'autoninja -C out/Debug_GN_x64 -j 3 cefsimple chrome_sandbox' "$log" ||
    fail "Default build did not use all available CPUs"

python_status=7
if (sync_dependencies) 2>"$test_root/error"; then
    fail "Dependency sync ignored a tool failure"
fi
[[ ! -f "$stamp" ]] || fail "Failed sync retained the ready stamp"
python_status=0

if (export BOOTSTRAP_TEST_STATUS=9; sync_dependencies) 2>"$test_root/error"; then
    fail "Dependency sync ignored a bootstrap failure"
else
    [[ $? -eq 9 ]] || fail "Dependency sync changed the bootstrap failure status"
fi
[[ ! -f "$stamp" ]] || fail "Failed bootstrap retained the ready stamp"

for args in 'build --jobs 0' 'build --jobs nope' 'build --config wrong' \
            'build --jobs' 'wrong'; do
    # These are deliberately split CLI fixtures, not paths.
    read -r -a words <<< "$args"
    if (parse_args "${words[@]}") 2>"$test_root/error"; then
        fail "Invalid arguments accepted: $args"
    fi
done

git -C "$source_dir" init --quiet
git -C "$source_dir" config user.name "Container Test"
git -C "$source_dir" config user.email "container-test@example.invalid"
printf 'committed\n' > "$source_dir/tracked.txt"
git -C "$source_dir" add .
git -C "$source_dir" commit --quiet -m fixture
git -C "$source_dir" commit --quiet --allow-empty -m history
printf 'local edit\n' > "$source_dir/tracked.txt"
work=$test_root/mirror
mkdir -p "$work"
cef=$work/cef
mirror
[[ $(git -C "$cef" rev-parse --is-shallow-repository) == true &&
   $(git -C "$cef" rev-list --count HEAD) -eq 1 ]] ||
    fail "CEF clone downloaded unnecessary history"
cmp "$source_dir/tracked.txt" "$cef/tracked.txt" ||
    fail "Uncommitted edits were not copied"
before=$(stat -c %Y "$cef/tracked.txt")
sleep 1
mirror
[[ $(stat -c %Y "$cef/tracked.txt") == "$before" ]] ||
    fail "Unchanged source timestamps moved"
printf 'new\n' > "$source_dir/new.txt"
mirror
[[ -f "$cef/new.txt" ]] || fail "New source files were not copied"
rm "$source_dir/new.txt"
mirror
[[ ! -f "$cef/new.txt" ]] || fail "Deleted source files were retained"
[[ $(git -C "$source_dir" status --porcelain) == ' M tracked.txt' ]] ||
    fail "Host checkout was modified"
printf 'next commit\n' > "$source_dir/tracked.txt"
git -C "$source_dir" commit --quiet -am next
mirror
[[ $(git -C "$cef" rev-parse HEAD) == \
   "$(git -C "$source_dir" rev-parse HEAD)" ]] ||
    fail "Local mirror did not follow host commits"
[[ $(git -C "$cef" rev-list --count HEAD) -eq 1 ]] ||
    fail "CEF fetch deepened the shallow mirror"

src=$work/chromium/src
stamp=$work/.cef-synced-compatibility
depot_origin=$test_root/depot-origin
mkdir -p "$depot_origin" "$cef/tools/automate"
cp "${CEF_SOURCE_DIRECTORY:-/source/cef}/tools/automate/automate-git.py" "$cef/tools/automate/"
cat >"$depot_origin/gclient" <<'GCLIENT'
#!/usr/bin/env bash
set -euo pipefail
[[ -f "$(dirname -- "$0")/.test-bootstrapped" ]] || {
    echo "depot_tools was not bootstrapped before gclient" >&2
    exit 1
}
printf '%s\n' "$*" >>"$GCLIENT_TEST_LOG"
if [[ "$1" == sync ]]; then
    [[ " $* " == *" --no-history "* ]] || {
        echo "Chromium sync requested full history" >&2
        exit 1
    }
    git init --quiet src
    git -C src remote add origin https://chromium.googlesource.com/chromium/src.git
    mkdir -p src/chrome
    printf 'MAJOR=156\nMINOR=0\nBUILD=8078\nPATCH=0\n' >src/chrome/VERSION
    touch src/DEPS
fi
GCLIENT
cat >"$depot_origin/ensure_bootstrap" <<'BOOTSTRAP'
#!/usr/bin/env bash
set -euo pipefail
touch "$(dirname -- "$0")/.test-bootstrapped"
BOOTSTRAP
printf '#!/usr/bin/env bash\nexit 0\n' >"$depot_origin/update_depot_tools"
chmod +x "$depot_origin/gclient" "$depot_origin/update_depot_tools" \
    "$depot_origin/ensure_bootstrap"
git -C "$depot_origin" init --quiet
git -C "$depot_origin" add .
git -C "$depot_origin" -c user.name="Container Test" \
    -c user.email=container-test@example.invalid \
    commit --quiet -m fixture
git -C "$depot_origin" -c user.name="Container Test" \
    -c user.email=container-test@example.invalid \
    commit --quiet --allow-empty -m history
if ! (
    # Keep upstream automation and Git real; replace only costly gclient work.
    python3() { command python3 "$@"; }
    export GCLIENT_TEST_LOG=$test_root/gclient-commands
    export GIT_CONFIG_COUNT=1
    export GIT_CONFIG_KEY_0="url.file://$depot_origin.insteadOf"
    export GIT_CONFIG_VALUE_0=https://chromium.googlesource.com/chromium/tools/depot_tools.git
    sync_dependencies
    sync_dependencies
) >"$test_root/automate-output" 2>&1; then
    tail -n 15 "$test_root/automate-output" >&2
    fail "Upstream rejected the dependency sync invocation"
fi
[[ $(git -C "$work/depot_tools" rev-parse --is-shallow-repository) == true &&
   $(git -C "$work/depot_tools" rev-list --count HEAD) -eq 1 ]] ||
    fail "depot_tools downloaded unnecessary history"
[[ $(wc -l < "$test_root/gclient-commands") -eq 2 ]] ||
    fail "Repeated sync did not reuse the existing Chromium checkout"
cmp -s "$cef/CHROMIUM_BUILD_COMPATIBILITY.txt" "$stamp" ||
    fail "Successful dependency sync did not record the compatible revision"

generated=$test_root/generated
mkdir -p "$generated/include/capi"
printf '# This file was generated by the CEF translator tool\n/.gitignore\n/include/capi/generated.h\n' \
    > "$generated/.gitignore"
printf 'generated header\n' > "$generated/include/capi/generated.h"
printf 'deleted source\n' > "$generated/deleted.txt"
before=$(stat -c %Y "$generated/include/capi/generated.h")
sleep 1
copy_source "$cef" "$generated" --exclude=/.git
[[ -f "$generated/include/capi/generated.h" ]] ||
    fail "Source sync removed a translator-generated header"
[[ $(stat -c %Y "$generated/include/capi/generated.h") == "$before" ]] ||
    fail "Source sync changed a generated header timestamp"
[[ ! -f "$generated/deleted.txt" ]] ||
    fail "Generated-file protection retained a deleted host source"
mkdir -p "$cef/include/capi"
printf 'host customization\n' > "$cef/include/capi/generated.h"
copy_source "$cef" "$generated" --exclude=/.git
cmp "$cef/include/capi/generated.h" "$generated/include/capi/generated.h" ||
    fail "Generated-file protection blocked a host customization"

echo "PASS: incremental builds, configuration, shallow sync, failures, packaging, CLI, source edits"
