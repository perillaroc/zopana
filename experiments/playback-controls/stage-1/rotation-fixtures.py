"""Build isolated rotation and sizing fixtures from the existing synthetic grid."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    base = args.base.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True)

    for name in ('grid.mp4', 'audio.m4a', 'external.srt', 'external.ass'):
        shutil.copyfile(base / name, output / name)

    commands = []

    def ffmpeg(*arguments):
        command = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', *map(str, arguments)]
        commands.append(command)
        subprocess.run(command, check=True)

    for name, filter_, audio in (
        ('sar.mp4', 'scale=720:540,setsar=4/3', True),
        ('square.mp4', 'scale=722:722,setsar=1', False),
        ('wide-1080.mp4', 'scale=1920:1080,setsar=1', False),
        ('wide-2160.mp4', 'scale=3840:2160,setsar=1', False),
        ('small.mp4', 'scale=320:180,setsar=1', False),
    ):
        ffmpeg('-loop', '1', '-framerate', '25', '-i', base / 'grid.png',
               *(['-i', base / 'audio.m4a', '-map', '0:v:0', '-map', '1:a:0', '-c:a', 'copy'] if audio else ['-an']),
               '-vf', filter_, '-t', '8', '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast',
               '-crf', '16', '-pix_fmt', 'yuv420p', output / name)
    ffmpeg('-display_rotation:v:0', '-90', '-i', output / 'sar.mp4', '-map', '0', '-c', 'copy', output / 'sar-90.mp4')

    # Legacy V4 syntax, distinct from the ASS script copied above.
    (output / 'legacy.ssa').write_text('''[Script Info]
ScriptType: v4.00
PlayResX: 960
PlayResY: 540
[V4 Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, TertiaryColour, BackColour, Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding
Style: Default,Arial,36,65535,65535,0,0,0,0,1,2,0,2,15,15,30,0,1
[Events]
Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: Marked=0,0:00:00.00,0:00:08.00,Default,,0,0,0,,SSA ROTATION SAMPLE
''', encoding='utf-8')

    files = {}
    for path in sorted(output.iterdir()):
        entry = {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        if path.suffix == '.mp4':
            entry['ffprobe'] = json.loads(subprocess.check_output([
                'ffprobe', '-v', 'error', '-show_entries',
                'stream=codec_type,codec_name,width,height,sample_aspect_ratio,duration:stream_side_data=rotation',
                '-of', 'json', str(path),
            ]))['streams']
        files[path.name] = entry
    (output / 'manifest.json').write_text(json.dumps({'commands': commands, 'files': files}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
