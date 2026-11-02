#!/usr/bin/env bash
set -euo pipefail

[[ $EUID -eq 0 ]] || {
    echo "Run this test in the container with --user root" >&2
    exit 1
}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_root=${CEF_TEST_DIRECTORY:-/work/.artifacts}/ownership-test-$BASHPID-$RANDOM
mkdir -p "$test_root"
trap 'rm -rf -- "$test_root"' EXIT
chmod 755 "$test_root"
mkdir "$test_root/source" "$test_root/work"
chown builder:builder "$test_root/work"
git -C "$test_root/source" init --quiet
git -C "$test_root/source" config user.name "Container Test"
git -C "$test_root/source" config user.email "container-test@example.invalid"
echo initial > "$test_root/source/tracked.txt"
git -C "$test_root/source" add .
git -C "$test_root/source" commit --quiet -m initial

mirror_as_builder() {
    setpriv --reuid=builder --regid=builder --clear-groups bash -c '
        source "$1"
        source_dir=$2
        work=$3
        cef=$work/cef
        mirror
    ' _ "$script_dir/build.sh" "$1" "$2"
}

mirror_as_builder "$test_root/source" "$test_root/work"
cmp "$test_root/source/tracked.txt" "$test_root/work/cef/tracked.txt"
echo next > "$test_root/source/tracked.txt"
git -C "$test_root/source" commit --quiet -am next
mirror_as_builder "$test_root/source" "$test_root/work"
expected=$(git -C "$test_root/source" rev-parse HEAD)
actual=$(setpriv --reuid=builder --regid=builder --clear-groups \
    git -C "$test_root/work/cef" rev-parse HEAD)
[[ "$actual" == "$expected" ]]
cmp "$test_root/source/tracked.txt" "$test_root/work/cef/tracked.txt"

git -C "$test_root" init --quiet
git -C "$test_root" -c protocol.file.allow=always \
    submodule add --quiet "$test_root/source" nested
[[ -f "$test_root/nested/.git" ]]
mkdir "$test_root/submodule-work"
chown builder:builder "$test_root/submodule-work"
mirror_as_builder "$test_root/nested" "$test_root/submodule-work"
echo submodule >"$test_root/nested/tracked.txt"
git -C "$test_root/nested" -c user.name="Container Test" \
    -c user.email=container-test@example.invalid commit --quiet -am submodule
mirror_as_builder "$test_root/nested" "$test_root/submodule-work"
expected=$(git -C "$test_root/nested" rev-parse HEAD)
actual=$(setpriv --reuid=builder --regid=builder --clear-groups \
    git -C "$test_root/submodule-work/cef" rev-parse HEAD)
[[ "$actual" == "$expected" ]]
cmp "$test_root/nested/tracked.txt" "$test_root/submodule-work/cef/tracked.txt"
[[ $(setpriv --reuid=builder --regid=builder --clear-groups \
    git -C "$test_root/submodule-work/cef" rev-list --count HEAD) -eq 1 ]]
git -C "$test_root/source" worktree add --quiet -b ownership-linked "$test_root/linked"
mkdir "$test_root/linked-work"
chown builder:builder "$test_root/linked-work"
mirror_as_builder "$test_root/linked" "$test_root/linked-work"
cmp "$test_root/linked/tracked.txt" "$test_root/linked-work/cef/tracked.txt"
[[ $(setpriv --reuid=builder --regid=builder --clear-groups \
    git -C "$test_root/linked-work/cef" rev-parse HEAD) == \
    "$(git -C "$test_root/linked" rev-parse HEAD)" ]]
echo "PASS: non-root shallow clone/fetch from differently owned clones, worktrees and submodules"
