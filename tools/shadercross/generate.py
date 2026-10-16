#!/usr/bin/env python3
"""Regenerate the two offline shader formats; --check rejects stale artifacts."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--shadercross', required=True, type=Path)
parser.add_argument('--glslang', default='glslangValidator')
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
source = Path(__file__).resolve().parents[2] / 'src/surrender/devices/sdl_gpu'
output = source / 'compiled'
output.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='wiz8-shaders-') as directory:
    temporary = Path(directory)
    for stage, extension in [('vertex', 'vert'), ('fragment', 'frag')]:
        glsl = source / f'sr_{stage}.glsl'
        spirv = temporary / f'{stage}.spv'
        subprocess.run([args.glslang, '-V', '-S', extension, '-o', spirv, glsl], check=True)
        for format_ in ['msl', 'dxil']:
            artifact = temporary / f'sr_{stage}.{format_}'
            subprocess.run([args.shadercross, spirv, '-s', 'SPIRV', '-d', format_.upper(),
                            '-t', stage, '-o', artifact], check=True)
            if format_ == "msl":
                artifact.write_bytes(artifact.read_bytes().rstrip() + b"\n")
            destination = output / artifact.name
            if args.check:
                if not destination.exists() or artifact.read_bytes() != destination.read_bytes():
                    raise SystemExit(f'Stale shader artifact: {destination}')
            else:
                shutil.copyfile(artifact, destination)
        digest = hashlib.sha256(glsl.read_bytes()).hexdigest() + '\n'
        manifest = output / f'sr_{stage}.sha256'
        if args.check:
            if not manifest.exists() or manifest.read_text() != digest:
                raise SystemExit(f'Stale shader source digest: {manifest}')
        else:
            manifest.write_text(digest)
