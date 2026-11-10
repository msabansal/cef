"""Compile the production chunk cache with substituted paint-record types."""

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
        cwd=root,
        text=True,
    ) if args.baseline else (root / "libcef" / "renderer" /
                             "blink_glue.cc").read_text(encoding="utf-8")
    start = source.index("class ChunkSerializationCache {")
    cache = source[start:source.index("// Repeats a cached chunk", start)]
    harness = (root / "tests" /
               "reach_chunk_cache_test.cc").read_text(encoding="utf-8")
    output = root / ".artifacts" / "reach-chunk-cache-test"
    output.mkdir(parents=True, exist_ok=True)
    unit = output / "chunk-cache-test.cc"
    unit.write_text(harness.replace("// NATIVE_CHUNK_CACHE", cache),
                    encoding="utf-8")
    executable = output / "chunk-cache-test.exe"
    subprocess.run([
        args.compiler,
        "/nologo",
        "/EHsc",
        "/std:c++20",
        "/W4",
        "/WX",
        "/D_CRT_SECURE_NO_WARNINGS",
        str(unit),
        "/Fe:" + str(executable),
        "/Fo:" + str(output / "chunk-cache-test.obj"),
    ],
                   cwd=root,
                   check=True)
    subprocess.run([str(executable)], cwd=root, check=True)
    print("Chunk cache: aborted-capture byte/entry budgets and retirement PASS")


if __name__ == "__main__":
    main()
