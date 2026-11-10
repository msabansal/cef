"""Compile the production media journal with substituted Blink boundaries."""

import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    parser.add_argument("--baseline")
    parser.add_argument("--case", choices=("global", "init", "compact"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    patch_path = "patch/patches/reach/media_journal.patch"
    if args.baseline:
        patch = subprocess.check_output(
            ["git", "show", f"{args.baseline}:{patch_path}"],
            cwd=root,
            text=True)
    else:
        patch = (root / patch_path).read_text(encoding="utf-8")
    sections = re.split(r"(?=^diff --git )", patch, flags=re.MULTILINE)
    sources = []
    for extension in ("h", "cc"):
        name = ("third_party/blink/renderer/core/html/media/"
                f"reach_media_journal.{extension}")
        section = next(item for item in sections
                       if item.startswith(f"diff --git {name} {name}\n"))
        sources.append(
            "\n".join(line[1:]
                      for line in section.splitlines()
                      if line.startswith("+") and not line.startswith("+++") and
                      not line.startswith("+#include")))
    output = root / ".artifacts" / "reach-media-journal-test"
    output.mkdir(parents=True, exist_ok=True)
    unit = output / "journal-test.cc"
    harness = (root / "tests" /
               "reach_media_journal_test.cc").read_text(encoding="utf-8")
    unit.write_text(harness.replace("// NATIVE_MEDIA_JOURNAL",
                                    "\n".join(sources)),
                    encoding="utf-8")
    executable = output / "journal-test.exe"
    if Path(args.compiler).stem.lower() == "cl":
        command = [
            args.compiler,
            "/nologo",
            "/EHsc",
            "/std:c++20",
            "/W4",
            "/WX",
            "/O2",
            str(unit),
            "/Fe:" + str(executable),
            "/Fo:" + str(output / "journal-test.obj"),
        ]
    else:
        command = [
            args.compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O2",
            str(unit), "-o",
            str(executable)
        ]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(executable)] + ([args.case] if args.case else []),
                   cwd=root,
                   check=True)


if __name__ == "__main__":
    main()
