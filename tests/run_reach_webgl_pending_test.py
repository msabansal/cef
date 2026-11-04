"""Compile only the portable pending queue extracted from the native patch."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--baseline",
                        help="Read the native patch from this Git revision "
                             "(the oversized-command regression must fail).")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / ".artifacts" / "reach-webgl-test"
    output.mkdir(parents=True, exist_ok=True)
    patch = subprocess.check_output(
        ["git", "show", args.baseline + ":patch/patches/reach_webgl_commands.patch"],
        cwd=root, text=True,
    ) if args.baseline else (root / "patch" / "patches" / "reach_webgl_commands.patch").read_text(
        encoding="utf-8"
    )
    name = "third_party/blink/renderer/modules/webgl/reach_webgl_pending.h"
    section = next(
        item for item in re.split(r"(?=^diff --git )", patch, flags=re.MULTILINE)
        if item.startswith(f"diff --git {name} {name}\n")
    )
    header = "\n".join(
        line[1:] for line in section.splitlines()
        if line.startswith("+") and not line.startswith("+++")
    ) + "\n"
    (output / "reach_webgl_pending.h").write_text(header, encoding="utf-8")
    source = root / "tests" / "reach_webgl_pending_test.cc"
    executable = output / "pending-test.exe"
    if Path(args.compiler).stem.lower() == "cl":
        command = [
            args.compiler, "/nologo", "/EHsc", "/std:c++20", "/W4", "/WX",
            "/I" + str(output), str(source),
            "/Fe:" + str(executable), "/Fo:" + str(output / "pending-test.obj"),
        ]
    else:
        command = [
            args.compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
            "-I", str(output), str(source), "-o", str(executable),
        ]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(executable)], cwd=root, check=True)


if __name__ == "__main__":
    main()
