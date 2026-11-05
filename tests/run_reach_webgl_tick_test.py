"""Compile the actual WebGlRequest animation prefix with Blink boundary stubs."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--baseline")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = subprocess.check_output(
        ["git", "show", args.baseline + ":libcef/renderer/blink_glue.cc"],
        cwd=root, text=True,
    ) if args.baseline else (
        root / "libcef" / "renderer" / "blink_glue.cc"
    ).read_text()
    start = source.index("std::string WebGlRequest(")
    end = source.index('  if (operation == "Reach.WebGlStreams.v2" ||', start)
    # The unmodified production prefix ends before discovery/read delivery.
    # Only parameters/closure used by the omitted suffix need this test tail.
    prefix = source[start:end] + """
  static_cast<void>(owned);
  static_cast<void>(std::tie(stream, generation, position, offset, max_bytes,
                           output, originals));
  return {};
}
"""
    output = root / ".artifacts" / "reach-webgl-tick-test"
    output.mkdir(parents=True, exist_ok=True)
    unit = output / "tick-test.cc"
    harness = (root / "tests" / "reach_webgl_tick_test.cc").read_text()
    callback_start = source.index(
        "std::function<void(blink::WebLocalFrame*)>& WebGlChangeCallback() {")
    callback = source[callback_start:source.index(
        "void OnWebGlChanged", callback_start)]
    notifications = callback + source[source.index("// Local roots"):source.index(
        "// Media playback state")]
    harness = harness.replace("// NATIVE_DEMAND_NOTIFICATIONS", notifications)
    unit.write_text(harness.replace("// NATIVE_WEBGL_REQUEST_PREFIX", prefix))
    subprocess.run([
        args.clang_tidy, str(unit),
        "-checks=-*,clang-diagnostic-*,clang-analyzer-core.CallAndMessage",
        "-warnings-as-errors=*", "--", "-std=c++20", "-Werror", "-Wshadow",
    ], cwd=root, check=True)
    executable = output / "tick-test.exe"
    if Path(args.compiler).stem.lower() == "cl":
        command = [
            args.compiler, "/nologo", "/EHsc", "/std:c++20", "/W4", "/WX",
            str(unit), "/Fe:" + str(executable),
            "/Fo:" + str(output / "tick-test.obj"),
        ]
    else:
        command = [args.compiler, "-std=c++20", "-Werror", "-Wshadow",
                   str(unit), "-o", str(executable)]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(executable)], cwd=root, check=True)
    print("WebGlRequest: shadow diagnostics, pinned lifetime, both document "
          "identity checks and stale wire identity PASS")


if __name__ == "__main__":
    main()
