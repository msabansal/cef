"""Compile journal lifetime and deferred retirement code with Blink stubs."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    parser.add_argument("--clang-tidy")
    parser.add_argument("--baseline")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]

    def read(path):
        if args.baseline:
            return subprocess.check_output(
                ["git", "show", args.baseline + ":" + path],
                cwd=root, text=True,
            )
        return root.joinpath(*path.split("/")).read_text()

    patch = read("patch/patches/reach/webgl_commands.patch")
    journal = patch.split(
        "+++ third_party/blink/renderer/modules/webgl/reach_webgl_journal.cc\n",
        1,
    )[1].split("\ndiff --git ", 1)[0]
    journal = "\n".join(line[1:] for line in journal.splitlines()
                        if line.startswith("+"))
    lifetime = journal[journal.index(
        "ReachWebGLJournal::ReachWebGLJournal("):journal.index(
        "void ReachWebGLJournal::SetOriginalHasher(")]
    source = read("libcef/renderer/blink_glue.cc")
    callbacks = source[source.index(
        "void OnWebGlChanged("):source.index("bool WebGlDecimal(")]
    registration = source[source.index(
        "  blink::ReachWebGLJournal::SetChangeCallback("):source.index(
        "  blink::ReachWebGLJournal::SetOriginalHasher(")]
    harness = root.joinpath("tests", "reach_webgl_retirement_test.cc").read_text()
    harness = harness.replace("// NATIVE_JOURNAL_LIFETIME", lifetime)
    harness = harness.replace("// NATIVE_RETIREMENT_CALLBACKS", callbacks)
    harness = harness.replace("// NATIVE_CALLBACK_REGISTRATION", registration)
    output = root / ".artifacts" / "reach-webgl-retirement-test"
    output.mkdir(parents=True, exist_ok=True)
    unit = output / "retirement-test.cc"
    unit.write_text(harness)
    executable = output / "retirement-test.exe"
    if args.clang_tidy:
        subprocess.run([
            args.clang_tidy, str(unit),
            "-checks=-*,clang-diagnostic-*,clang-analyzer-core.CallAndMessage",
            "-warnings-as-errors=*", "--", "-std=c++20", "-Werror", "-Wshadow",
        ], cwd=root, check=True)
    subprocess.run([
        args.compiler, "/nologo", "/EHsc", "/std:c++20", "/W4", "/WX",
        *([] if args.baseline else ["/DREACH_TEST_RETIREMENT_REBIND"]),
        str(unit), "/Fe:" + str(executable),
        "/Fo:" + str(output / "retirement-test.obj"),
    ], cwd=root, check=True)
    subprocess.run([str(executable)], cwd=root, check=True)
    print("Actual journal lifetime/retirement: post-removal idle GC wake, "
          "deferred GC safety, coalescing/rearm, weak document, navigation "
          "and sibling isolation, pinned adoption rebinding PASS")


if __name__ == "__main__":
    main()
