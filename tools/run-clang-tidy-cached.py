#!/usr/bin/env python3
"""Cache clean clang-tidy runs using compiler-discovered include contents.

Dependency discovery always runs against the current tree, including system
headers. Failed scans and failed tidy runs never create cache entries.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from tempfile import TemporaryDirectory
import threading
import time

from clang_tidy_support import dependencies, is_first_party_source, source_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--tidy', required=True)
    parser.add_argument('--driver', required=True)
    parser.add_argument('--pattern', required=True)
    parser.add_argument('--header-filter', required=True)
    parser.add_argument('--jobs', type=int, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('extra', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    extra = args.extra[1:] if args.extra[:1] == ['--'] else args.extra
    def run_driver(pattern):
        return subprocess.run([args.driver, '-clang-tidy-binary', args.tidy,
                               '-config-file', str(args.config), '-header-filter', args.header_filter,
                               '-p', str(args.build), '-j', str(args.jobs), pattern, *extra], check=False)

    # Extra compiler arguments can change the include graph; -fix mutates it.
    # Replay arbitrary driver options without caching rather than guessing.
    if extra:
        print('Extra tidy arguments: running uncached.', flush=True)
        return run_driver(args.pattern).returncode

    started = time.monotonic()
    root = args.root.resolve()
    entries = json.loads((args.build / 'compile_commands.json').read_text())
    entries = [entry for entry in entries
               if is_first_party_source(root, source_path(entry))
               and re.search(args.pattern, str(source_path(entry)))]
    if not entries:
        print('No selected first-party translation units.')
        return 0

    # Include the runner itself so cache-key and dependency-discovery changes
    # invalidate entries. Include ancestor configs that can supply inherited settings.
    version = subprocess.check_output([args.tidy, '--version'])
    identity = hashlib.sha256(version)
    configuration_hashes = {}
    configuration_paths = {Path(__file__).resolve(),
                           Path(__file__).with_name('clang_tidy_support.py').resolve(),
                           root / '.clang-tidy', args.config.resolve()}
    for parent in args.config.resolve().parents:
        inherited = parent / '.clang-tidy'
        if inherited.is_file():
            configuration_paths.add(inherited)
    for path in sorted(configuration_paths):
        identity.update(str(path.resolve()).encode())
        contents = path.read_bytes()
        identity.update(contents)
        configuration_hashes[path] = hashlib.sha256(contents).digest()
    identity.update(json.dumps([args.header_filter, extra]).encode())
    prefix = identity.digest()
    hashes = configuration_hashes.copy()
    compiler_versions = {}
    lock = threading.Lock()

    def file_hash(path):
        # Most includes are shared; read and hash each once per invocation.
        with lock:
            if path not in hashes:
                with path.open('rb') as stream:
                    hashes[path] = hashlib.file_digest(stream, 'sha256').digest()
            return hashes[path]

    def fingerprint(item, directory):
        index, entry = item
        try:
            source, includes = dependencies(entry, index, directory)
            arguments = entry.get('arguments') or shlex.split(entry['command'])
            compiler = arguments[1] if Path(arguments[0]).name in ('ccache', 'sccache') else arguments[0]
            with lock:
                if compiler not in compiler_versions:
                    compiler_versions[compiler] = subprocess.check_output([compiler, '--version'], timeout=10)
                compiler_version = compiler_versions[compiler]
            digest = hashlib.sha256(prefix)
            digest.update(compiler_version)
            digest.update(json.dumps(entry, sort_keys=True).encode())
            for path in sorted(includes | {source}):
                digest.update(os.fsencode(path))
                digest.update(file_hash(path))
            marker = args.cache / digest.hexdigest()
            return source, marker, marker.is_file()
        except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired, subprocess.CalledProcessError) as exc:
            print(f'Cache bypass for {entry["file"]}: {exc}', file=sys.stderr)
            return source_path(entry), None, False

    with TemporaryDirectory() as directory, ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda item: fingerprint(item, Path(directory)), enumerate(entries)))
    grouped = {}
    for source, marker, hit in results:
        grouped.setdefault(source, []).append((marker, hit))
    misses = {source: [marker for marker, _ in variants]
              for source, variants in grouped.items() if not all(hit for _, hit in variants)}
    print(f'clang-tidy cache: {len(grouped) - len(misses)} hits, {len(misses)} misses '
          f'({time.monotonic() - started:.1f}s dependency scan)', flush=True)
    if not misses:
        return 0

    def analyze(source):
        began = time.monotonic()
        result = subprocess.run([args.tidy, '-p', str(args.build),
                                 '--config-file=' + str(args.config),
                                 '--header-filter=' + args.header_filter, str(source)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                check=False)
        return result, time.monotonic() - began

    clean = []
    failures = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = {pool.submit(analyze, source): source for source in misses}
        for index, future in enumerate(as_completed(pending), 1):
            source = pending[future]
            result, elapsed = future.result()
            print(f'[{index}/{len(misses)}][{elapsed:.1f}s] {source}', flush=True)
            if result.stdout:
                print(result.stdout, end='', flush=True)
            if result.returncode == 0:
                clean.extend(misses[source])
            else:
                failures += 1
    # A developer may edit files during an analysis. Never label the earlier
    # fingerprint clean when tidy might have read different contents.
    stable = True
    if clean:
        for path, before in hashes.items():
            try:
                with path.open('rb') as stream:
                    stable = hashlib.file_digest(stream, 'sha256').digest() == before
            except OSError:
                stable = False
            if not stable:
                print('Inputs changed during analysis; not saving clean results.', flush=True)
                break
    if clean and stable:
        args.cache.mkdir(parents=True, exist_ok=True)
        for marker in clean:
            if marker is not None:
                marker.touch()
    print(f'clang-tidy completed in {time.monotonic() - started:.1f}s', flush=True)
    return int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
