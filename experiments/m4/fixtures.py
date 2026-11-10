"""Create immutable subtitle matching fixtures; never modify a shared media file."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--base', required=True, type=Path)
args = parser.parse_args()
if args.output.exists():
    raise SystemExit('Retain the existing fixtures or choose a new version directory.')
args.output.mkdir(parents=True)
sources = [('match.mp4', 'grid.mp4'), ('match.srt', 'external.srt'),
           ('match.en.ass', 'external.ass'), ('match.zh-Hans.vtt', 'external.vtt'),
           ('match.notes.srt', 'external.srt'), ('match-other.vtt', 'external.vtt'),
           ('external.ssa', 'external.ass')]
for name, source in sources:
    target = args.output / name
    try:
        os.link(args.base / source, target)
    except OSError:
        shutil.copyfile(args.base / source, target)
files = [dict(name=name, source=source, sha256=hashlib.sha256((args.output / name).read_bytes()).hexdigest())
         for name, source in sources]
(args.output / 'manifest.json').write_text(json.dumps(dict(version=1, base=str(args.base.resolve()), files=files), indent=2), encoding='utf-8')
(args.output / 'README.md').write_text(
    '# M4 subtitle fixtures v1\n\nKeep this library immutable beside zopana-ui-v1. Files may share NTFS hard links. '
    'match.srt and the en/zh-Hans language variants must load automatically; notes and match-other must not. '
    'external.ssa tests the manual SSA extension. SHA-256 values are in manifest.json.\n', encoding='utf-8')
