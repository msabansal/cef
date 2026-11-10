"""Compile actual demand notifications and animation-only IPC glue with stubs."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    parser.add_argument("--baseline")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = subprocess.check_output(
        ["git", "show", args.baseline + ":libcef/renderer/blink_glue.cc"],
        cwd=root, text=True,
    ) if args.baseline else (
        root / "libcef" / "renderer" / "blink_glue.cc"
    ).read_text()
    notifications = source[source.index("// Local roots"):source.index(
        "// Media playback state")]
    start = source.index("std::string DrawingAnimationRequest(")
    end = source.index("std::string ReadDrawingMediaJournal(", start)
    request = source[start:end]
    harness = (root / "tests" / "reach_animation_test.cc").read_text()
    harness = harness.replace("// NATIVE_DEMAND_NOTIFICATIONS", notifications)
    harness = harness.replace("// NATIVE_ANIMATION_REQUEST", request)
    output = root / ".artifacts" / "reach-animation-test"
    output.mkdir(parents=True, exist_ok=True)
    unit = output / "animation-test.cc"
    unit.write_text(harness)
    executable = output / "animation-test.exe"
    if Path(args.compiler).stem.lower() == "cl":
        command = [
            args.compiler, "/nologo", "/EHsc", "/std:c++20", "/W4", "/WX",
            str(unit), "/Fe:" + str(executable),
            "/Fo:" + str(output / "animation-test.obj"),
        ]
    else:
        command = [args.compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-Wshadow", str(unit), "-o", str(executable)]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(executable)], cwd=root, check=True)
    print("Animation-only notifications/state/tick: idle, first RAF, rearm, "
          "coalescing, cancellation, pinned ownership, navigation isolation PASS")


if __name__ == "__main__":
    main()
