"""Replay the canvas-video patch against independently pinned Chromium inputs."""
import argparse
import json
from pathlib import Path
import re
import shutil
import uuid

from run_reach_webgl_patch_test import (
    check_baseline, check_patch_blobs, digest, run,
)


def paths(text):
    return set(re.findall(r"^diff --git (\S+) \S+$", text, re.MULTILINE))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True,
                        help="Unmodified Chromium files at the immutable pin.")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    patch = root / "patch" / "patches" / "reach" / "canvas_video.patch"
    text = patch.read_text()
    files = json.loads(
        (root / "tests" / "reach_webgl_patch_baseline.json").read_text()
    )["files"]
    files.update({
        "third_party/blink/renderer/core/html/canvas/canvas_rendering_context.cc":
            "61b5037c3338e7357d3da59106287193dff69f8e4786c33ef3c62d136f710016",
        "third_party/blink/renderer/platform/graphics/reach_canvas_video.h": None,
    })
    files = {name: files[name] for name in paths(text)}
    check_baseline(args.fixture, files)
    output = root / ".artifacts" / "reach-canvas-video-patch-test" / uuid.uuid4().hex
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
        if entry["name"] == "reach/canvas_video":
            break
        previous = root / "patch" / "patches" / (entry["name"] + ".patch")
        overlap = paths(previous.read_text()).intersection(files)
        if not overlap:
            continue
        assert not entry.get("path"), "Unexpected relative predecessor"
        # Apply the real predecessor hunks for every canvas-video input path.
        run(["git", "apply", "-p0",
             *("--include=" + name for name in sorted(overlap)),
             str(previous)], output)
        predecessors.append(entry["name"])
    else:
        raise AssertionError("Canvas-video production patch is not configured")
    run(["git", "add", "--", "third_party"], output)
    run(["git", "-c", "user.name=Fixture",
         "-c", "user.email=fixture@example.invalid",
         "commit", "-q", "-m", "Pinned Chromium with configured predecessors"],
        output)
    before = {
        name: (output / name).read_bytes() if (output / name).exists() else None
        for name in files
    }
    run(["git", "apply", "-p0", "--check", "--whitespace=error", str(patch)],
        output)
    run(["git", "apply", "-p0", str(patch)], output)
    check_patch_blobs(output, text)
    final = {name: digest(output / name) for name in files}
    run(["git", "apply", "-p0", "--reverse", "--check",
         "--whitespace=error", str(patch)], output)
    run(["git", "apply", "-p0", "--reverse", str(patch)], output)
    for name, expected in before.items():
        value = (output / name).read_bytes() if (output / name).exists() else None
        assert value == expected, "Reverse changed predecessor: " + name
    run(["git", "apply", "-p0", "--check", "--whitespace=error", str(patch)],
        output)
    run(["git", "apply", "-p0", str(patch)], output)
    assert all(digest(output / name) == expected
               for name, expected in final.items())
    print(f"PASS: {len(files)} immutable inputs; configured predecessors "
          f"{predecessors}; exact blobs and strict forward/reverse/reapply\n"
          f"{output}")


if __name__ == "__main__":
    main()
