#!/usr/bin/env python3
"""Build the CanvasKit client matching this fork's pinned Chromium/Skia."""

import argparse
import concurrent.futures
import importlib.machinery
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-sync", action="store_true",
                        help="Use already synchronized standalone Skia dependencies")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                        help="Parallel compile jobs (default: all available CPUs)")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if os.name != "posix":
        parser.error("Run inside the Linux Chromium/CEF source checkout")
    source = Path(__file__).resolve().parents[2]
    skia = source / "third_party" / "skia"
    expected = "a02d4982d7658aa18fbbdc8f136776a76e6d771f"
    actual = subprocess.check_output(
        ["git", "-C", str(skia), "rev-parse", "HEAD"], text=True).strip()
    if actual != expected:
        parser.error("Skia revision does not match the native command protocol")
    os.chdir(skia)
    if not args.skip_sync:
        loader = importlib.machinery.SourceFileLoader(
            "reach_skia_sync", str(skia / "tools" / "git-sync-deps"))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        sync = importlib.util.module_from_spec(spec)
        loader.exec_module(sync)
        deps = sync.parse_file_to_dict("DEPS")["deps"]
        required = [
            "brotli", "emsdk", "freetype", "highway", "libjpeg-turbo", "libpng",
            "libwebp", "wuffs", "zlib", "opengl-registry", "egl-registry",
            "partition_alloc",
        ]

        def fetch(name):
            directory = "third_party/externals/" + name
            repository, revision = deps[directory].rsplit("@", 1)
            sync.git_checkout_to_directory(
                "git", repository, revision, directory, True, False)

        with concurrent.futures.ThreadPoolExecutor(
                max_workers=min(8, os.cpu_count() or 1)) as pool:
            list(pool.map(fetch, required))
        subprocess.run(["python3", "bin/activate-emsdk"], check=True)
    if "MakePictureWithMedia" not in (
            skia / "modules" / "canvaskit" / "skp.js").read_text():
        parser.error("Apply the registered reach_canvaskit_picture_images patch first")
    environment = dict(os.environ, EMSDK_QUIET="1")
    # Upstream's driver deletes archives unconditionally. Keep the same GN and
    # compiler commands, but let Ninja's dependency graph reuse valid outputs.
    driver = (skia / "modules" / "canvaskit" / "compile.sh").read_text()
    substitutions = {
        "BASE_DIR=`cd $(dirname ${BASH_SOURCE[0]}) && pwd`":
            "BASE_DIR=" + shlex.quote(str(skia / "modules" / "canvaskit")),
        "rm -f $BUILD_DIR/*.a": ": # Retain Ninja-managed archives",
        "${NINJA} -C ${BUILD_DIR} canvaskit.js -k 10":
            "${NINJA} -C ${BUILD_DIR} canvaskit.js -k 10 -j" + str(args.jobs),
    }
    for original, replacement in substitutions.items():
        if driver.count(original) != 1:
            parser.error("Pinned CanvasKit build driver changed: " + original)
        driver = driver.replace(original, replacement)
    subprocess.run([
        "bash", "-c",
        "set -euo pipefail; source third_party/externals/emsdk/emsdk_env.sh; "
        "exec bash -s -- release no_skottie no_embedded_font "
        "no_paragraph primitive_shaper",
    ], check=True, env=environment, input=driver, text=True)
    print("Matched client: " + str(skia / "out" / "canvaskit_wasm"))


if __name__ == "__main__":
    main()
