"""Create immutable playlist fixtures beside the original media library."""
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
sources = [('E1.mp4','grid.mp4'),('E2.mp4','grid.mp4'),('E10.mp4','grid.mp4'),('E1.srt','external.srt'),
           ('bad1.mkv','broken.mkv'),('bad2.mkv','broken.mkv'),('bad3.mkv','broken.mkv'),('subfolder/ignored.mp4','grid.mp4')]
sources += [(f'large/episode-{i+1:04d}.mp4','grid.mp4') for i in range(1000)]
for name, source in sources:
    target = args.output / name
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.link(args.base / source, target)
    except OSError:
        shutil.copyfile(args.base / source, target)
(args.output / 'ignore.txt').write_text('not media', encoding='utf-8')
files = [dict(name=path.relative_to(args.output).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
         for path in args.output.rglob('*') if path.is_file()]
(args.output / 'manifest.json').write_text(json.dumps(dict(version=1, base=str(args.base.resolve()), files=files), indent=2), encoding='utf-8')
(args.output / 'README.md').write_text(
    '# M3 playlist fixtures v1\n\nRetain this directory alongside zopana-ui-v1. '
    'These files may share NTFS hard links with immutable base media: never edit their contents. '
    'E1/E2/E10 test natural ordering; bad1/2/3 test bounded failures; subfolder must not be scanned; '
    'large contains 1000 individually named media paths. SHA-256 values are in manifest.json.\n', encoding='utf-8')
