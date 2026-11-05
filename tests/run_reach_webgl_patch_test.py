"""Exercise production reset/patcher with independently pinned Chromium files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(
            f"{command!r} exited {result.returncode}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_baseline(src, files):
    for name, expected in files.items():
        path = src / name
        if expected is None:
            assert not path.exists(), f"Upstream file must be absent: {name}"
        else:
            assert digest(path) == expected, f"Pinned source differs: {name}"


def check_patch_blobs(src, patch):
    for section in re.split(r"(?=^diff --git )", patch, flags=re.MULTILINE):
        if not section.startswith("diff --git "):
            continue
        name = section.splitlines()[0].split()[2]
        expected = re.search(r"^index \w+\.\.(\w+)", section, re.MULTILINE)
        assert expected, f"Missing final blob identity: {name}"
        data = (src / name).read_bytes()
        actual = hashlib.sha1(
            b"blob " + str(len(data)).encode() + b"\0" + data
        ).hexdigest()
        assert actual.startswith(expected[1]), f"Applied blob differs: {name}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True,
                        help="Unmodified Chromium files at the manifest pin.")
    parser.add_argument("--baseline",
                        help="Test patch bytes from this CEF Git revision.")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads(
        (root / "tests" / "reach_webgl_patch_baseline.json").read_text()
    )
    files = manifest["files"]
    fixture = Path(args.fixture).resolve()
    check_baseline(fixture, files)
    config = {}
    exec((root / "patch" / "patch.cfg").read_text(), config)
    selected = [
        entry["name"] for entry in config["patches"]
        if entry["name"] in manifest["patches"]
    ]
    assert selected == manifest["patches"], "Production patch order changed"
    for entry in config["patches"]:
        if entry["name"] == selected[-1]:
            break
        if entry["name"] not in selected:
            text = (root / "patch" / "patches"
                    / (entry["name"] + ".patch")).read_text()
            paths = set(re.findall(r"^diff --git (\S+) \S+$", text, re.MULTILINE))
            assert paths.isdisjoint(files), "New preceding patch dependency"
    src = root / ".artifacts" / "reach-webgl-patch-test" / uuid.uuid4().hex
    src.mkdir(parents=True)
    for name, expected in files.items():
        if expected is not None:
            output = src / name
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(fixture / name, output)
    run(["git", "init", "-q"], src)
    run(["git", "config", "core.autocrlf", "false"], src)
    run(["git", "add", "--", "cc", "third_party"], src)
    run([
        "git", "-c", "user.name=Copilot",
        "-c", "user.email=223556219+Copilot@users.noreply.github.com",
        "commit", "-q", "-m",
        "Pinned Chromium " + manifest["chromium_commit"] + "\n\n"
        "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>\n"
        "Copilot-Session: 302f2553-f149-432f-aca2-e1dcc8d57e61",
    ], src)
    cef = src / "cef"
    tools = cef / "tools"
    patches = cef / "patch" / "patches"
    tools.mkdir(parents=True)
    patches.mkdir(parents=True)
    for name in ("patcher.py", "git_util.py", "exec_util.py", "file_util.py"):
        shutil.copyfile(root / "tools" / name, tools / name)
    (cef / "patch" / "patch.cfg").write_text(
        "patches = " + repr([{"name": name} for name in selected]) + "\n"
    )
    patch_texts = {}
    for name in selected:
        relative = "patch/patches/" + name.replace("reach/", "reach_", 1) + ".patch"
        data = subprocess.check_output(
            ["git", "show", args.baseline + ":" + relative], cwd=root
        ) if args.baseline else (
            root / "patch" / "patches" / (name + ".patch")
        ).read_bytes()
        output = patches / (name + ".patch")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
        patch_texts[name] = data.decode()
    paths = {
        name for text in patch_texts.values()
        for name in re.findall(r"^diff --git (\S+) \S+$", text, re.MULTILINE)
    }
    assert paths == set(files), "Pinned baseline coverage must be exact"
    reset = root / "docker" / "reset_patched_files.py"
    patcher = tools / "patcher.py"
    run([sys.executable, str(reset), "reset", str(src)], root)
    check_baseline(src, files)
    applied = run([sys.executable, str(patcher)], src)
    assert "3 patches total (3 applied, 0 skipped, 0 failed)" in applied, applied
    check_patch_blobs(src, patch_texts["reach/webgl_commands"])
    run([sys.executable, str(reset), "restore", str(src)], root)
    final = {
        name: (digest(src / name), (src / name).stat().st_mtime_ns)
        for name in files
    }
    skipped = run([
        sys.executable, str(patcher), "--patch-file", "reach/webgl_commands"
    ], src)
    assert "... already applied (skipping)." in skipped, skipped
    webgl = patches / "reach" / "webgl_commands.patch"
    run(["git", "apply", "-p0", "--reverse", "--check",
         "--whitespace=error", str(webgl)], src)
    run(["git", "apply", "-p0", "--reverse", str(webgl)], src)
    for name, expected in files.items():
        if expected is None:
            assert not (src / name).exists(), name
    run(["git", "apply", "-p0", "--check", "--whitespace=error",
         str(webgl)], src)
    run(["git", "apply", "-p0", str(webgl)], src)
    assert all(digest(src / name) == value[0] for name, value in final.items())
    # Record real applied-file timestamps before the incremental reset cycle.
    final = {
        name: (digest(src / name), (src / name).stat().st_mtime_ns)
        for name in files
    }
    run([sys.executable, str(reset), "reset", str(src)], root)
    check_baseline(src, files)
    applied = run([sys.executable, str(patcher)], src)
    assert "3 patches total (3 applied, 0 skipped, 0 failed)" in applied, applied
    run([sys.executable, str(reset), "restore", str(src)], root)
    for name, expected in final.items():
        path = src / name
        assert (digest(path), path.stat().st_mtime_ns) == expected, name
    print(
        f"PASS: {len(files)} pinned paths; clean and incremental production "
        "reset/patcher, absent-file creation, exact WebGL blobs, strict "
        f"forward/reverse checks, skip and timestamp preservation\n{src}"
    )


if __name__ == "__main__":
    main()
