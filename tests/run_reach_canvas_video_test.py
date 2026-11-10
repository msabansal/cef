"""Compile the production snapshot ownership state without building CEF."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--fixture", type=Path,
                        help="Pinned Chromium with Reach patches applied.")
    parser.add_argument("--baseline-hooks", action="store_true",
                        help="Use fixture HEAD's pre-video hook bodies (RED).")
    parser.add_argument("--baseline-hook",
                        choices=["dirty", "composited", "finalize"],
                        help="Substitute one pre-video hook body (RED).")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / ".artifacts" / "reach-canvas-video-test"
    output.mkdir(parents=True, exist_ok=True)
    patch = root / "patch" / "patches" / "reach" / "canvas_video.patch"
    assert patch.exists(), "native canvas video producer is not implemented"
    name = "third_party/blink/renderer/platform/graphics/reach_canvas_video.h"
    sections = re.split(r"(?=^diff --git )", patch.read_text(),
                        flags=re.MULTILINE)
    section = next(item for item in sections if f"+++ {name}\n" in item)
    header = "\n".join(line[1:] for line in section.splitlines()
                       if line.startswith("+") and not line.startswith("+++"))
    if args.fixture:
        header = (args.fixture / name).read_text()
    (output / "reach_canvas_video.h").write_text(header + "\n")
    def method_body(text, signature, result_type="void"):
        start = text.index(result_type + " " + signature + "(")
        body = text.index("{", start)
        end, depth = body + 1, 1
        while depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        return text[start:end]

    packing = method_body(
        (root / "libcef" / "renderer" / "frame_impl.cc").read_text(),
        "AttachCanvasVideo")
    (output / "reach_canvas_video_ipc.inc").write_text(packing)
    raster = method_body(
        (root / "libcef" / "renderer" / "blink_glue.cc").read_text(),
        "CheckRasterCanvasVisibility", "bool")
    (output / "reach_canvas_video_raster.inc").write_text(raster)
    sources = ["reach_canvas_video_test", "reach_canvas_video_ipc_test",
               "reach_canvas_video_raster_test"]
    if "ReleaseAdmission()" in header:
        sources.append("reach_canvas_video_painted_test")
    if args.fixture:
        hooks = []
        methods = [
            ("modules/webgl/webgl_rendering_context_base.cc",
             "WebGLRenderingContextBase::MarkContextChanged"),
            ("modules/webgl/webgl_rendering_context_base.cc",
             "WebGLRenderingContextBase::MarkLayerComposited"),
            ("core/html/canvas/html_canvas_element.cc",
             "HTMLCanvasElement::PostFinalizeFrame"),
        ]
        for label, (path, method) in zip(
                ["dirty", "composited", "finalize"], methods):
            path = "third_party/blink/renderer/" + path
            text = subprocess.check_output(
                ["git", "show", "HEAD:" + path], cwd=args.fixture, text=True
            ) if (args.baseline_hooks or args.baseline_hook == label) else (
                args.fixture / path).read_text()
            hooks.append(method_body(text, method))
        webgl = (args.fixture / "third_party" / "blink" / "renderer"
                 / "modules" / "webgl" / "webgl_rendering_context_base.cc")
        hooks.append(method_body(
            webgl.read_text(),
            "WebGLRenderingContextBase::ReachCanvasVideoIsSDR", "bool"))
        (output / "reach_canvas_video_hooks.inc").write_text("\n".join(hooks))
        sources.append("reach_canvas_video_hooks_test")
        lifecycle = []
        for path, method in [
                ("core/html/canvas/canvas_rendering_context.cc",
                 "CanvasRenderingContext::ReachCanvasVideoChanged"),
                ("core/html/canvas/canvas_rendering_context.cc",
                 "CanvasRenderingContext::ReleaseReachCanvasVideo"),
                ("modules/webgl/webgl_rendering_context_base.cc",
                 "WebGLRenderingContextBase::ForceLostContext")]:
            text = (args.fixture / "third_party" / "blink" / "renderer"
                    / path).read_text()
            lifecycle.append(method_body(text, method))
        (output / "reach_canvas_video_lifecycle.inc").write_text(
            "\n".join(lifecycle))
        sources.append("reach_canvas_video_lifecycle_test")
        mode = (args.fixture / "third_party" / "blink" / "renderer"
                / "platform" / "graphics" / "canvas_2d_bitmap_provider.cc")
        mode = mode.read_text()
        names = ["IsReachCommandMode", "IsReachCanvasVideoMode"]
        (output / "reach_canvas_video_mode.inc").write_text("\n".join(
            method_body(mode, "Canvas2DBitmapProvider::" + name, "bool")
            for name in names))
        sources.append("reach_canvas_video_mode_test")
    for name in sources:
        source = root / "tests" / (name + ".cc")
        executable = output / (name + ".exe")
        if Path(args.compiler).stem.lower() == "cl":
            command = [args.compiler, "/nologo", "/EHsc", "/std:c++20", "/W4",
                       "/WX", "/I" + str(output), str(source),
                       "/Fe:" + str(executable),
                       "/Fo:" + str(output / (name + ".obj"))]
        else:
            command = [args.compiler, "-std=c++20", "-Wall", "-Wextra",
                       "-Werror", "-I", str(output), str(source),
                       "-o", str(executable)]
        subprocess.run(command, cwd=root, check=True)
        if name == "reach_canvas_video_mode_test":
            cases = [
                ["1", "0", "0", "1", "1"],
                ["1", "1", "1", "1", "1"],
                ["1", "1", "0", "1", "1"],
                ["1", "1", "true", "1", "1"],
                ["1", "0", "1", "1", "1"],
                ["0", "1", "1", "1", "0"],
                ["1", "1", "1", "0", "1"],
                ["0", "1", "0", "0", "0"],
            ]
            for case in cases:
                subprocess.run([str(executable), *case], cwd=root, check=True)
            print("actual mode selection: canvas VIDEO is mandatory in "
                  "COMMAND mode; legacy flags ignored; full-VIDEO isolated")
        else:
            subprocess.run([str(executable)], cwd=root, check=True)


if __name__ == "__main__":
    main()
