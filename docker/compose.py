"""Run Compose with read-only Git metadata for clones, worktrees and submodules.

Run on the Linux Docker host. Paths retain their host locations so Git files,
commondir references and object stores remain resolvable without host edits.
"""

import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

OFFICIAL_SERVICE = 'builder-official'
SERVICES = ('builder', OFFICIAL_SERVICE)


def main(argv=None, source=None):
    source = Path(source or Path(__file__).resolve().parents[1]).resolve()
    metadata = subprocess.run(['git', '-C', str(source), 'rev-parse', '--path-format=absolute',
         '--git-dir', '--git-common-dir'],
                              check=True,
                              capture_output=True,
                              text=True).stdout.splitlines()
    if len(metadata) != 2:
        raise ValueError('Cannot resolve CEF Git directories')
    mounts = []
    for path in sorted({Path(value).resolve() for value in metadata}):
        if not path.is_dir():
            raise ValueError('Missing CEF Git metadata: ' + str(path))
        if not path.is_relative_to(source):
            mounts.append({
                'type': 'bind', 'source': str(path), 'target': str(path),
                'read_only': True, 'bind': {'create_host_path': False}
            })
    directory = source / '.artifacts'
    directory.mkdir(exist_ok=True)
    (source / 'artifacts').mkdir(exist_ok=True)
    args = sys.argv[1:] if argv is None else argv
    if OFFICIAL_SERVICE in args:
        (source / 'artifacts' / 'official').mkdir(exist_ok=True)
    override = directory / ('compose-metadata-' + uuid.uuid4().hex + '.json')
    env = os.environ.copy()
    env['CEF_SOURCE_DIRECTORY'] = str(source)
    try:
        with override.open('x', encoding='utf-8') as output:
            json.dump({'services': {service: {'volumes': mounts} for service in SERVICES}},
                      output)
        return subprocess.run(['docker', 'compose', '-f', str(source / 'docker' / 'compose.yaml'),
             '-f', str(override), *args],
                              env=env).returncode
    finally:
        override.unlink(missing_ok=True)


if __name__ == '__main__':
    if os.name != 'posix':
        sys.exit(
            'Run this entrypoint on the Linux Docker host (Azure build VM).')
    sys.exit(main())
