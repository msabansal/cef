"""Reuses immutable runtime files when packaging a fresh distribution."""

import errno
import os
import shutil
import stat


def _same_file_contents(source, previous):
    with open(source, 'rb') as current, open(previous, 'rb') as cached:
        while True:
            block = current.read(1024 * 1024)
            if block != cached.read(1024 * 1024):
                return False
            if not block:
                return True


def copy_runtime_file(source, destination, previous, quiet=True):
    """Copies changed files; hard-links an identical, immutable previous file."""
    source_stat = os.stat(source)
    if previous and os.path.isfile(previous) and not os.path.islink(previous):
        previous_stat = os.stat(previous)
        if (source_stat.st_size == previous_stat.st_size
                and source_stat.st_mtime_ns == previous_stat.st_mtime_ns
                and stat.S_IMODE(source_stat.st_mode)
                == stat.S_IMODE(previous_stat.st_mode)
                and _same_file_contents(source, previous)):
            try:
                if os.path.lexists(destination):
                    os.unlink(destination)
                os.link(previous, destination)
                if not quiet:
                    print('Reusing %s file (%d bytes).' %
                          (destination, source_stat.st_size))
                return destination
            except OSError as error:
                if error.errno not in (errno.EXDEV, errno.EOPNOTSUPP, errno.EMLINK):
                    raise
                if not quiet:
                    print('Cannot hard-link %s; copying instead: %s' %
                          (destination, error))
    if not quiet:
        print('Transferring %s file.' % source)
    # Never overwrite an inode shared with an older distribution.
    if os.path.lexists(destination):
        os.unlink(destination)
    return shutil.copy2(source, destination)


def copy_runtime_path(source, destination, previous, quiet=True):
    """Copies only source entries, never stale files from the previous SDK."""
    if not os.path.isdir(source):
        return copy_runtime_file(source, destination, previous, quiet)

    def copy_file(source_file, destination_file):
        previous_file = None
        if previous:
            previous_file = os.path.join(
                previous, os.path.relpath(source_file, source))
        return copy_runtime_file(
            source_file, destination_file, previous_file, quiet)

    return shutil.copytree(source, destination, copy_function=copy_file)
