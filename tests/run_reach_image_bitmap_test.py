"""Compile the pinned native upload branch, without building Chromium."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--baseline-upload", action="store_true",
                        help="Use the fixture index's pre-fix upload branch "
                             "(the regression must fail).")
    parser.add_argument("--baseline-limits",
                        help="Use journal Bytes/Append from a Git revision "
                             "(the large-upload regression must fail).")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (args.fixture / "third_party" / "blink" / "renderer" / "modules"
              / "webgl" / "webgl_rendering_context_base.cc").read_text()
    def method(text, signature):
        start = text.index(signature)
        end = text.index("\n}", start) + 2
        return text[start:end]

    if args.baseline_upload:
        source = subprocess.check_output(
            ["git", "-C", str(args.fixture), "show",
             ":third_party/blink/renderer/modules/webgl/"
             "webgl_rendering_context_base.cc"],
            text=True,
        )
    upload = method(
        source, "void WebGLRenderingContextBase::TexImageHelperImageBitmap(")
    output = root / ".artifacts" / "reach-image-bitmap-test"
    output.mkdir(parents=True, exist_ok=True)
    harness = (root / "tests" / "reach_image_bitmap_upload_test.cc").read_text()
    translation_unit = output / "image-bitmap-test.cc"
    for name in ("reach_image_bitmap_original.h", "reach_webgl_pending.h"):
        directory = "platform/graphics" if "image_bitmap" in name else "modules/webgl"
        header = args.fixture / "third_party/blink/renderer" / directory / name
        (output / name).write_bytes(header.read_bytes())
    journal = (args.fixture / "third_party/blink/renderer/modules/webgl"
               / "reach_webgl_journal.cc").read_text()
    baseline_journal = journal
    if args.baseline_limits:
        patch = subprocess.check_output(
            ["git", "show", args.baseline_limits +
             ":patch/patches/reach_webgl_commands.patch"], cwd=root, text=True)
        name = "third_party/blink/renderer/modules/webgl/reach_webgl_journal.cc"
        section = next(item for item in re.split(
            r"(?=^diff --git )", patch, flags=re.MULTILINE)
            if item.startswith(f"diff --git {name} {name}\n"))
        baseline_journal = "\n".join(line[1:] for line in section.splitlines()
                                    if line.startswith("+") and
                                    not line.startswith("+++"))
    methods = "\n\n".join(method(
        baseline_journal if args.baseline_limits and
        signature in ("base::Value ReachWebGLJournal::Bytes(",
                      "void ReachWebGLJournal::Append(") else journal,
        signature) for signature in (
        "base::Value ReachWebGLJournal::Bytes(",
        "bool ReachWebGLJournal::ReserveOriginalBytes(",
        "void ReachWebGLJournal::RejectTextureUpload(",
        "void ReachWebGLJournal::OriginalTexture(",
        "void ReachWebGLJournal::OriginalImageBitmapTexture(",
        "void ReachWebGLJournal::Append(",
        "std::string ReachWebGLJournal::ReadSlice(",
        "std::string ReachWebGLJournal::Applied(",
    ))
    harness = harness.replace("// NATIVE_UPLOAD_METHOD", upload)
    harness = harness.replace("// NATIVE_JOURNAL_METHODS", methods)
    translation_unit.write_text(harness)
    executable = output / "image-bitmap-test.exe"
    if Path(args.compiler).stem.lower() == "cl":
        command = [args.compiler, "/nologo", "/EHsc", "/std:c++20", "/W4", "/WX",
                   str(translation_unit), f"/Fe:{executable}",
                   f"/Fo:{output / 'image-bitmap-test.obj'}"]
    else:
        command = [args.compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   str(translation_unit), "-o", str(executable)]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(executable)], cwd=root, check=True)


if __name__ == "__main__":
    main()
