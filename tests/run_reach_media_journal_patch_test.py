"""Replay the media journal patch against independently pinned Chromium inputs."""

import argparse
import json
from pathlib import Path
import shutil
import uuid

from run_reach_canvas_video_patch_test import paths
from run_reach_webgl_patch_test import (
    check_baseline,
    check_patch_blobs,
    digest,
    run,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    patch = root / "patch" / "patches" / "reach" / "media_journal.patch"
    text = patch.read_text(encoding="utf-8")
    manifest = json.loads(
        (root / "tests" /
         "reach_media_journal_patch_baseline.json").read_text(encoding="utf-8"))
    files = manifest["files"]
    assert paths(text) == set(files), "Journal patch input paths changed"
    check_baseline(args.fixture, files)
    output = root / ".artifacts" / "reach-media-journal-patch-test" / uuid.uuid4(
    ).hex
    output.mkdir(parents=True)
    for name, expected in files.items():
        if expected is not None:
            dest = output / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.fixture / name, dest)
    run(["git", "init", "-q"], output)
    run(["git", "config", "core.autocrlf", "false"], output)
    config = {}
    exec((root / "patch" / "patch.cfg").read_text(), config)
    predecessors = []
    for entry in config["patches"]:
        if entry["name"] == "reach/media_journal":
            break
        previous = root / "patch" / "patches" / (entry["name"] + ".patch")
        overlap = paths(previous.read_text()).intersection(files)
        if not overlap:
            continue
        assert not entry.get("path"), "Unexpected relative predecessor"
        run([
            "git", "apply", "-p0",
            *("--include=" + name for name in sorted(overlap)),
            str(previous)
        ], output)
        predecessors.append(entry["name"])
    else:
        raise AssertionError("Media journal production patch is not configured")
    before = {
        name: (output / name).read_bytes() if (output / name).exists() else None
        for name in files
    }
    run(["git", "apply", "-p0", "--check", "--whitespace=error",
         str(patch)], output)
    run(["git", "apply", "-p0", str(patch)], output)
    check_patch_blobs(output, text)
    final = {name: digest(output / name) for name in files}
    run([
        "git", "apply", "-p0", "--reverse", "--check", "--whitespace=error",
        str(patch)
    ], output)
    run(["git", "apply", "-p0", "--reverse", str(patch)], output)
    for name, expected in before.items():
        value = (output / name).read_bytes() if (output /
                                                 name).exists() else None
        assert value == expected, "Reverse changed predecessor: " + name
    run(["git", "apply", "-p0", "--check", "--whitespace=error",
         str(patch)], output)
    run(["git", "apply", "-p0", str(patch)], output)
    assert all(
        digest(output / name) == expected for name, expected in final.items())
    print(f"PASS: Chromium {manifest['chromium_commit']}; {len(files)} paths; "
          f"configured predecessors {predecessors}; strict replay\n{output}")


if __name__ == "__main__":
    main()
