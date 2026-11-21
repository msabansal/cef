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
    architecture = os.environ.get('CEF_TARGET_ARCH', 'x64')
    if architecture not in ('x64', 'arm64'):
        raise ValueError('CEF_TARGET_ARCH must be x64 or arm64')
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
    artifacts = source / 'artifacts'
    if architecture == 'arm64':
        artifacts = artifacts / 'arm64'
    artifacts.mkdir(parents=True, exist_ok=True)
    args = sys.argv[1:] if argv is None else argv
    if OFFICIAL_SERVICE in args:
        (artifacts / 'official').mkdir(exist_ok=True)
    override = directory / ('compose-metadata-' + uuid.uuid4().hex + '.json')
    env = os.environ.copy()
    env['CEF_SOURCE_DIRECTORY'] = str(source)
    try:
        with override.open('x', encoding='utf-8') as output:
            services = {
                service: {
                    'volumes': list(mounts)
                } for service in SERVICES
            }
            if architecture == 'arm64':
                for service in SERVICES:
                    destination = artifacts
                    if service == OFFICIAL_SERVICE:
                        destination = artifacts / 'official'
                    services[service]['volumes'].append({
                        'type': 'bind',
                        'source': str(destination),
                        'target': '/artifacts',
                        'bind': {
                            'create_host_path': False
                        }
                    })
            json.dump({'services': services}, output)
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
