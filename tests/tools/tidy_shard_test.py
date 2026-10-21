"""Sharding must keep every translation unit in exactly one shard."""

import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

TOOLS = Path(__file__).resolve().parents[2] / 'tools'
COMPILER = shutil.which('clang++-23') or shutil.which('clang++')


@unittest.skipUnless(COMPILER, 'Clang is needed for dependency discovery')
class TidyShardTest(unittest.TestCase):
    def test_disjoint_complete_partitions(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'include').mkdir()
            header = root / 'include/common.h'
            header.write_text('#pragma once\n')
            sources = []
            entries = []
            for index in range(17):
                path = root / ('src' if index % 2 else 'tests') / f'File {index}.cpp'
                path.parent.mkdir(exist_ok=True)
                path.write_text('#include "common.h"\n')
                sources.append(str(path))
                entries.append({'directory': str(root), 'file': str(path),
                                'arguments': [COMPILER, '-Werror', '-c', str(path),
                                              '-I', str(header.parent)]})
            database = root / 'compile_commands.json'
            database.write_text(json.dumps(entries))
            seen = set()
            for index in range(4):
                command = [sys.executable, str(TOOLS / 'clang_tidy_shard.py'),
                           '--root', str(root), '--compile-commands', str(database),
                           '--pattern', '.*', '--index', str(index), '--count', '4']
                pattern = subprocess.check_output(command, text=True).strip()
                selected = {path for path in sources if pattern != 'SKIP' and re.search(pattern, path)}
                self.assertFalse(seen & selected)
                seen.update(selected)
                # Database ordering and hash randomization must not reshuffle work.
                database.write_text(json.dumps(entries[::-1]))
                self.assertEqual(subprocess.check_output(command, text=True).strip(), pattern)
            self.assertEqual(seen, set(sources))
            invalid = subprocess.run([*command[:-1], '0'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(invalid.returncode, 0)


if __name__ == '__main__':
    unittest.main()
