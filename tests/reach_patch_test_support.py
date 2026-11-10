"""Shared immutable-input and exact-blob checks for native patch regressions."""
import hashlib
import re
import subprocess


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
