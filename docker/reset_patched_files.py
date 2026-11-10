#!/usr/bin/env python3
"""Makes re-running CEF's patcher on an existing Chromium tree incremental.

CEF's patcher cannot re-apply patches that are already applied once a patch
changed or another patch is layered on top of it (e.g. reach_video_sink on
viz_osr_2575). `reset` restores every file named by any configured patch to its
repository HEAD and records its content hash and mtime; after the patcher has
re-applied all patches, `restore` puts back the old mtime of every file whose
content is unchanged, so ninja only rebuilds what really changed.

  reset_patched_files.py reset|restore <chromium-src>
"""
import hashlib
import json
import os
import re
import subprocess
import sys

mode, src = sys.argv[1], os.path.abspath(sys.argv[2])
state_path = os.path.join(src, 'out', 'reach-patched-files.json')


def digest(path):
    with open(path, 'rb') as file:
        return hashlib.sha256(file.read()).hexdigest()


def tracked_names(repo, names):
    """Names present in HEAD, using one git process for the whole repository."""
    query = ''.join('HEAD:%s\n' % name for name in names)
    result = subprocess.run(['git', '-C', repo, 'cat-file', '--batch-check'],
                            input=query, capture_output=True, text=True, check=True)
    lines = result.stdout.splitlines()
    if len(lines) != len(names):
        sys.exit('unexpected git cat-file output for %s' % repo)
    return [name for name, line in zip(names, lines) if not line.endswith(' missing')]


if mode == 'reset':
    scope = {}
    config = os.path.join(src, 'cef', 'patch', 'patch.cfg')
    exec(compile(open(config, 'rb').read(), config, 'exec'), scope)
    state = {}
    repos = {}
    for entry in scope['patches']:
        repo = os.path.join(src, entry.get('path', ''))
        patch = os.path.join(src, 'cef', 'patch', 'patches', entry['name'] + '.patch')
        if not os.path.isfile(patch) or not os.path.isdir(repo):
            continue
        text = open(patch, encoding='utf-8', errors='replace').read()
        for name in sorted(set(re.findall(r'^diff --git \S+ (\S+)$', text, re.M))):
            path = os.path.join(repo, name)
            if path in state:
                continue
            state[path] = (digest(path), os.stat(path).st_mtime_ns) if os.path.isfile(path) else None
            repos.setdefault(repo, []).append(name)
    # Removed patches must also be undone on an existing build checkout.
    # Otherwise stale binding hooks, retained records and journal sources
    # survive indefinitely because no current patch names their paths.
    retired = os.path.join(src, 'cef', 'patch', 'retired_canvas_files.json')
    if os.path.isfile(retired):
        with open(retired, encoding='utf-8') as file:
            for name in json.load(file):
                path = os.path.join(src, name)
                if path in state:
                    continue
                state[path] = ((digest(path), os.stat(path).st_mtime_ns)
                               if os.path.isfile(path) else None)
                repos.setdefault(src, []).append(name)
    # Per-file git calls reload Chromium's large index each time; batch them
    # so resetting hundreds of patched files costs one checkout per repository.
    for repo, names in repos.items():
        tracked = tracked_names(repo, names)
        if tracked:
            subprocess.run(['git', '-C', repo, 'checkout', '-q', 'HEAD',
                            '--pathspec-from-file=-', '--pathspec-file-nul'],
                           input='\0'.join(tracked), text=True, check=True)
        for name in set(names) - set(tracked):
            path = os.path.join(repo, name)
            if os.path.isfile(path):
                os.remove(path)
    os.makedirs(os.path.dirname(state_path), exist_ok=True)
    with open(state_path, 'w') as file:
        json.dump(state, file)
    print('reset %d patched files to upstream' % len(state))
elif mode == 'restore':
    if not os.path.isfile(state_path):
        sys.exit(0)
    state = json.load(open(state_path))
    kept = 0
    for path, old in state.items():
        if old and os.path.isfile(path) and digest(path) == old[0]:
            os.utime(path, ns=(old[1], old[1]))
            kept += 1
    os.remove(state_path)
    print('kept timestamps of %d unchanged patched files' % kept)
else:
    sys.exit('usage: reset_patched_files.py reset|restore <chromium-src>')
