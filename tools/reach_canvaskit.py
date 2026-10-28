#!/usr/bin/env python3
"""Build the CanvasKit client matching this fork's pinned Chromium/Skia."""

import argparse
import concurrent.futures
import importlib.machinery
import importlib.util
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-sync", action="store_true",
                        help="Use already synchronized standalone Skia dependencies")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Run inside the Linux Chromium/CEF source checkout")
    source = Path(__file__).resolve().parents[2]
    skia = source / "third_party" / "skia"
    expected = "9c7b2dffb2433f5a0cc2b77f06025a09126807ed"
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
    subprocess.run([
        "bash", "-c",
        "set -euo pipefail; source third_party/externals/emsdk/emsdk_env.sh; "
        "exec modules/canvaskit/compile.sh release no_skottie no_embedded_font "
        "no_paragraph primitive_shaper",
    ], check=True, env=environment)
    print("Matched client: " + str(skia / "out" / "canvaskit_wasm"))


if __name__ == "__main__":
    main()
