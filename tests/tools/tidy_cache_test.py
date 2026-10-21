"""Protect against stale clean results hiding newly introduced diagnostics."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

RUNNER = Path(__file__).resolve().parents[2] / 'tools/run-clang-tidy-cached.py'
COMPILER = shutil.which('clang++-23') or shutil.which('clang++')


@unittest.skipUnless(COMPILER, 'Clang is needed for real dependency discovery')
class TidyCacheTest(unittest.TestCase):
    def test_transitive_inputs_and_failures(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'src').mkdir()
            source = root / 'src/test.cpp'
            source.write_text('#include "outer.h"\nint n = VALUE;\n')
            (source.parent / 'outer.h').write_text('#include "leaf.h"\n')
            leaf = source.parent / 'leaf.h'
            leaf.write_text('#define VALUE 1\n')
            config = root / '.clang-tidy'
            config.write_text('Checks: "-*"\n')
            build = root / 'build'
            build.mkdir()
            entry = {'directory': str(root), 'file': str(source),
                     'arguments': [COMPILER, '-Werror', '-c', str(source), '-o', str(build / 'test.o')]}
            database = build / 'compile_commands.json'
            database.write_text(json.dumps([entry]))
            tidy = root / 'tidy'
            analysis = ('#!/usr/bin/env python3\nimport os, sys\nfrom pathlib import Path\n'
                        'if "--version" in sys.argv:\n'
                        '    print("test tidy version 1"); raise SystemExit(0)\n'
                        'if os.environ.get("TIDY_TEST_EDIT"):\n'
                        '    p = Path(os.environ["TIDY_TEST_EDIT"]); p.write_text(p.read_text() + "// edited\\n")\n'
                        'if os.environ.get("TIDY_TEST_FAIL_FILE") == Path(sys.argv[-1]).name:\n'
                        '    raise SystemExit(1)\n'
                        'raise SystemExit(int(os.environ.get("TIDY_TEST_FAIL", "0")))\n')
            tidy.write_text(analysis)
            tidy.chmod(0o755)
            driver = root / 'driver'
            driver.write_text(analysis)
            driver.chmod(0o755)
            command = [sys.executable, str(RUNNER), '--root', str(root), '--build', str(build),
                       '--config', str(config), '--tidy', str(tidy), '--driver', str(driver),
                       '--pattern', '.*', '--header-filter', str(root), '--jobs', '2',
                       '--cache', str(root / 'cache')]

            def run(hit=False, fail=False, bypass=False):
                result = subprocess.run(command, env={**os.environ, 'TIDY_TEST_FAIL': str(int(fail))},
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                self.assertEqual(result.returncode, int(fail), result.stdout)
                self.assertIn('1 hits, 0 misses' if hit else '0 hits, 1 misses', result.stdout)
                if bypass:
                    self.assertIn('Cache bypass', result.stdout)

            run()
            run(hit=True)
            # Comments matter to NOLINT; hash raw include contents as well as code.
            leaf.write_text('#define VALUE 1\n// NOLINT\n')
            run()
            run(hit=True)
            source.write_text(source.read_text() + 'int another;\n')
            run()
            config.write_text('Checks: "bugprone-*"\n')
            run()
            entry['arguments'].insert(1, '-DSOME_FLAG=1')
            database.write_text(json.dumps([entry]))
            run()
            tidy.write_text(tidy.read_text().replace('version 1', 'version 2'))
            run()
            leaf.write_text('#define VALUE 2\n')
            run(fail=True)
            run()  # A failed analysis must be retried with exactly the same inputs.
            run(hit=True)
            leaf.write_text('#define VALUE 3\n')
            editing = subprocess.run(command, env={**os.environ, 'TIDY_TEST_EDIT': str(leaf)},
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertEqual(editing.returncode, 0, editing.stdout)
            self.assertIn('Inputs changed during analysis', editing.stdout)
            leaf.write_text('#define VALUE 3\n')
            run()  # The original fingerprint was not actually analyzed cleanly.

            # A broken include graph must never hit or populate the cache, even
            # when a driver returns success (e.g. no matching database entry).
            leaf.unlink()
            run(bypass=True)
            run(bypass=True)
            extra = subprocess.run([*command, '--', '-extra-arg=-DSOME_FLAG=2'],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertEqual(extra.returncode, 0, extra.stdout)
            self.assertIn('running uncached', extra.stdout)

            # Keep clean results from other files when one file has a finding.
            leaf.write_text('#define VALUE 4\n')
            other = source.with_name('other.cpp')
            other.write_text('int other;\n')
            second = {**entry, 'file': str(other),
                      'arguments': [COMPILER, '-Werror', '-c', str(other)]}
            database.write_text(json.dumps([entry, second]))
            for expected in ('0 hits, 2 misses', '1 hits, 1 misses'):
                mixed = subprocess.run(command, env={**os.environ, 'TIDY_TEST_FAIL_FILE': other.name},
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                self.assertEqual(mixed.returncode, 1, mixed.stdout)
                self.assertIn(expected, mixed.stdout)
            recovered = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertEqual(recovered.returncode, 0, recovered.stdout)
            self.assertIn('1 hits, 1 misses', recovered.stdout)




if __name__ == '__main__':
    unittest.main()
