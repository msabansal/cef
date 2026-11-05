"""Compile actual demand notification and animation/discovery code with stubs."""
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
    callback_start = source.index(
        "std::function<void(blink::WebLocalFrame*)>& WebGlChangeCallback() {")
    callback = source[callback_start:source.index(
        "void OnWebGlChanged", callback_start)]
    notifications = callback + source[source.index("// Local roots"):source.index(
        "// Media playback state")]
    start = source.index("std::string WebGlRequest(")
    end = source.index("  uint64_t id = 0, context_generation", start)
    request = source[start:end] + """
  static_cast<void>(std::tie(stream, generation, position, offset, max_bytes,
                           output, originals));
  return {};
}
"""
    harness = (root / "tests" / "reach_webgl_tick_test.cc").read_text()
    harness = harness.replace("// NATIVE_DEMAND_NOTIFICATIONS", notifications)
    harness = harness.replace("// NATIVE_WEBGL_REQUEST_PREFIX", request)
    output = root / ".artifacts" / "reach-animation-test"
    output.mkdir(parents=True, exist_ok=True)
    unit = output / "animation-test.cc"
    unit.write_text(harness)
    executable = output / "animation-test.exe"
    subprocess.run([
        args.compiler, "/nologo", "/EHsc", "/std:c++20", "/W4", "/WX",
        "/DREACH_TEST_DEMAND", str(unit), "/Fe:" + str(executable),
        "/Fo:" + str(output / "animation-test.obj"),
    ], cwd=root, check=True)
    subprocess.run([str(executable)], cwd=root, check=True)
    print("Actual notification/tick/discovery: idle, first RAF, rearm, "
          "coalescing, cancellation, navigation isolation PASS")


if __name__ == "__main__":
    main()
