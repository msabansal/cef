"""Verify video-only canvas patches, normal semantics and retired-file cleanup."""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys
import uuid

from reach_patch_test_support import check_baseline, check_patch_blobs, digest, run


def paths(text):
    return set(re.findall(r"^diff --git (\S+) \S+$", text, re.MULTILINE))


def method(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def check_normal_semantics(output, baseline):
    prefix = "third_party/blink/renderer/"
    methods = {
        "modules/canvas/canvas2d/canvas_rendering_context_2d.cc": [
            "bool CanvasRenderingContext2D::WritePixels(",
            "void CanvasRenderingContext2D::RecordingCleared(",
            "std::optional<cc::PaintRecord> CanvasRenderingContext2D::FlushCanvas(",
            "scoped_refptr<StaticBitmapImage> blink::CanvasRenderingContext2D::GetImage(",
            "ImageData* CanvasRenderingContext2D::getImageDataInternal(",
            "void CanvasRenderingContext2D::CreateProvider(",
        ],
        "platform/graphics/canvas_2d_bitmap_provider.cc": [
            "scoped_refptr<StaticBitmapImage> Canvas2DBitmapProvider::Snapshot(",
            "void Canvas2DBitmapProvider::ClearAtCreation(",
            "void Canvas2DBitmapProvider::RasterRecord(",
            "bool Canvas2DBitmapProvider::WritePixels(",
        ],
        "modules/webgl/webgl_rendering_context_base.cc": [
            "void WebGLRenderingContextBase::TexImageStaticBitmapImage(",
            "ScriptValue WebGLRenderingContextBase::getParameter(",
        ],
    }
    checked = 0
    for name, signatures in methods.items():
        original = (baseline / (prefix + name)).read_text(encoding="utf-8")
        current = (output / (prefix + name)).read_text(encoding="utf-8")
        for signature in signatures:
            assert method(current, signature) == method(original, signature), signature
            checked += 1
    print(f"PASS: {checked} complete native pixel/readback/provider/WebGL methods "
          "are byte-identical to pinned upstream")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True,
                        help="Unmodified Chromium files at the immutable pin.")
    parser.add_argument("--migration-baseline",
                        help="Optional previous CEF revision to exercise removal.")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads(
        (root / "tests" / "reach_canvas_patch_baseline.json").read_text())
    files = manifest["files"]
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
    run(["git", "add", "--", "cc", "third_party"], output)
    run(["git", "-c", "user.name=Fixture",
         "-c", "user.email=fixture@example.invalid",
         "commit", "-q", "-m", "Pinned Chromium " + manifest["chromium_commit"]],
        output)
    config = {}
    exec((root / "patch" / "patch.cfg").read_text(), config)
    selected = [entry for entry in config["patches"]
                if entry["name"] in manifest["patches"]]
    assert [entry["name"] for entry in selected] == manifest["patches"]
    selected_paths = set()
    for entry in config["patches"]:
        text = (root / "patch" / "patches" /
                (entry["name"] + ".patch")).read_text(encoding="utf-8")
        if entry in selected:
            selected_paths.update(paths(text))
        else:
            assert paths(text).isdisjoint(files), "New overlapping patch dependency"
    retired = json.loads((root / "patch" / "retired_canvas_files.json").read_text())
    assert selected_paths.isdisjoint(retired)
    assert selected_paths.union(retired) == set(files)
    cef = output / "cef"
    (cef / "tools").mkdir(parents=True)
    (cef / "patch" / "patches" / "reach").mkdir(parents=True)
    for name in ("patcher.py", "git_util.py", "exec_util.py", "file_util.py"):
        shutil.copyfile(root / "tools" / name, cef / "tools" / name)
    (cef / "patch" / "patch.cfg").write_text("patches = " + repr(selected))
    shutil.copyfile(root / "patch" / "retired_canvas_files.json",
                    cef / "patch" / "retired_canvas_files.json")
    for entry in selected:
        name = entry["name"] + ".patch"
        shutil.copyfile(root / "patch" / "patches" / name,
                        cef / "patch" / "patches" / name)
    reset = root / "docker" / "reset_patched_files.py"
    patcher = cef / "tools" / "patcher.py"
    if args.migration_baseline:
        for name in ("canvas_frames", "video_commands", "webgl_commands",
                     "canvas_video", "canvas_video_all"):
            text = run(["git", "show", args.migration_baseline +
                        ":patch/patches/reach/" + name + ".patch"], root)
            previous = output / "previous.patch"
            previous.write_text(text, newline="\n")
            run(["git", "apply", "-p0", str(previous)], output)
        previous.unlink()
    run([sys.executable, str(reset), "reset", str(output)], root)
    check_baseline(output, files)
    for entry in selected:
        patch = cef / "patch" / "patches" / (entry["name"] + ".patch")
        run(["git", "apply", "-p0", "--check", "--whitespace=error", str(patch)], output)
        run(["git", "apply", "-p0", str(patch)], output)
        check_patch_blobs(output, patch.read_text())
    check_normal_semantics(output, args.fixture)
    for name in retired:
        check_baseline(output, {name: files[name]})
    for entry in reversed(selected):
        patch = cef / "patch" / "patches" / (entry["name"] + ".patch")
        run(["git", "apply", "-p0", "--reverse", "--check",
             "--whitespace=error", str(patch)], output)
        run(["git", "apply", "-p0", "--reverse", str(patch)], output)
    check_baseline(output, files)
    applied = run([sys.executable, str(patcher)], output)
    assert "3 patches total (3 applied, 0 skipped, 0 failed)" in applied, applied
    final = {name: (digest(output / name), (output / name).stat().st_mtime_ns)
             for name in files if (output / name).exists()}
    skipped = run([sys.executable, str(patcher), "--patch-file",
                   "reach/canvas_video"], output)
    assert "... already applied (skipping)." in skipped, skipped
    run([sys.executable, str(reset), "reset", str(output)], root)
    check_baseline(output, files)
    applied = run([sys.executable, str(patcher)], output)
    assert "3 patches total (3 applied, 0 skipped, 0 failed)" in applied, applied
    run([sys.executable, str(reset), "restore", str(output)], root)
    for name, expected in final.items():
        assert (digest(output / name), (output / name).stat().st_mtime_ns) == expected, name
    print(f"PASS: {len(files)} immutable inputs; exact blobs; strict "
          "forward/reverse/reapply; production reset/patcher; retired replay "
          f"cleanup; incremental timestamps\n{output}")


if __name__ == "__main__":
    main()
