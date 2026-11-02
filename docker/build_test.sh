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
