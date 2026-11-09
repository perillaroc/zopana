"""Create the immutable M2 long-seek fixture in a fresh directory."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--base', type=Path, required=True, help='Existing zopana-ui-v1 directory')
args = parser.parse_args()
target = args.output / 'long-grid.mp4'
if target.exists():
    raise SystemExit('Fixture already exists; retain it or choose a new version directory.')
args.output.mkdir(parents=True, exist_ok=True)
command = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-loop', '1', '-framerate', '25',
           '-i', str(args.base / 'grid.png'), '-f', 'lavfi', '-i', 'sine=frequency=220:sample_rate=48000',
           '-t', '137', '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '28', '-pix_fmt', 'yuv420p',
           '-c:a', 'aac', '-b:a', '64k', '-movflags', '+faststart', str(target)]
subprocess.run(command, check=True)
manifest = dict(version=1, purpose='M2 5/30/60-second seek validation', command=command,
                files=[dict(name=target.name, size=target.stat().st_size,
                            sha256=hashlib.sha256(target.read_bytes()).hexdigest())])
(args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
(args.output / 'README.md').write_text(
    '# M2 long-seek fixture v1\n\nRetain this directory alongside zopana-ui-v1. '
    'long-grid.mp4 is a 137-second 25-fps grid video with AAC audio. '
    'The source is grid.png from the base fixture set. Generation and SHA-256 are in manifest.json. '
    'Keep existing files immutable; use a new version directory for changes.\n', encoding='utf-8')
