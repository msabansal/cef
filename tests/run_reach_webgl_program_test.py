"""Compile production program capture against the pinned GLES output guard."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--clang-tidy")
    parser.add_argument("--fixture", type=Path, required=True,
                        help="Unmodified pinned Chromium GLES client sources.")
    parser.add_argument("--baseline")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    client = args.fixture / "gpu" / "command_buffer" / "client"
    sources = {}
    for name, digest in (
        ("gles2_implementation.cc",
         "40f84935012ccafe0eb85ecd3fe25e0310c13f413cace0bbeceb2a3e050dec44"),
        ("gles2_implementation_impl_autogen.h",
         "4053a6ff872bfc51ce039e395c4d86ab1903cee0dc0a5d774d2c763f6f1c9f1a"),
    ):
        data = (client / name).read_bytes()
        assert hashlib.sha256(data).hexdigest() == digest, name
        sources[name] = data.decode()
    implementation = sources["gles2_implementation.cc"]
    start = implementation.index(
        "#define GPU_CLIENT_VALIDATE_DESTINATION_INITALIZATION(type, ptr)")
    end = implementation.index("\n\n", start)
    guard = implementation[start:end]
    generated = sources["gles2_implementation_impl_autogen.h"]
    start = generated.index("void GLES2Implementation::GetProgramiv(")
    end = generated.index("\n}", start)
    validation = re.search(
        r"  GPU_CLIENT_VALIDATE_DESTINATION_INITALIZATION\([^;]+;",
        generated[start:end],
    )[0]
    relative = "patch/patches/reach_webgl_commands.patch"
    patch = subprocess.check_output(
        ["git", "show", args.baseline + ":" + relative], cwd=root, text=True,
    ) if args.baseline else (root / relative).read_text()
    name = "third_party/blink/renderer/modules/webgl/reach_webgl_journal.cc"
    section = next(item for item in re.split(
        r"(?=^diff --git )", patch, flags=re.MULTILINE)
        if item.startswith(f"diff --git {name} {name}\n"))
    journal = "\n".join(line[1:] for line in section.splitlines()
                        if line.startswith("+") and not line.startswith("+++"))
    start = journal.index("void ReachWebGLJournal::Program(")
    method = journal[start:journal.index("\n}", start) + 2]
    harness = (root / "tests" / "reach_webgl_program_test.cc").read_text()
    output = root / ".artifacts" / "reach-webgl-program-test"
    output.mkdir(parents=True, exist_ok=True)
    unit = output / "program-test.cc"
    unit.write_text(harness.replace("// PINNED_DESTINATION_GUARD", guard)
                    .replace("// PINNED_PROGRAM_QUERY_VALIDATION", validation)
                    .replace("// NATIVE_PROGRAM_METHOD", method))
    if args.clang_tidy:
        subprocess.run([
            args.clang_tidy, str(unit),
            "-checks=-*,clang-diagnostic-*,clang-analyzer-core.CallAndMessage",
            "-warnings-as-errors=*", "--", "-std=c++20", "-Werror",
            "-Wshadow", "-Wshorten-64-to-32",
        ], cwd=root, check=True)
    executable = output / "program-test.exe"
    if Path(args.compiler).stem.lower() == "cl":
        command = [args.compiler, "/nologo", "/EHsc", "/std:c++20", "/W4", "/WX",
                   str(unit), f"/Fe:{executable}",
                   f"/Fo:{output / 'program-test.obj'}"]
    else:
        command = [args.compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-Wshadow", str(unit), "-o", str(executable)]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(executable)], cwd=root, check=True)


if __name__ == "__main__":
    main()
