"""Create deterministic, redistributable M0 media; requires Pillow and ffmpeg."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    out = args.directory.resolve()
    out.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 24)
    image = Image.new('RGB', (960, 540), '#233447')
    draw = ImageDraw.Draw(image)
    for x in range(0, 960, 80):
        draw.line((x, 0, x, 539), fill='#718293', width=2)
    for y in range(0, 540, 60):
        draw.line((0, y, 959, y), fill='#718293', width=2)
    for x, y, color, label in [(10, 10, '#e82c35', 'TOP LEFT'),
                                (750, 10, '#28d967', 'TOP RIGHT'),
                                (10, 450, '#306ceb', 'BOTTOM LEFT'),
                                (750, 450, '#efca32', 'BOTTOM RIGHT')]:
        draw.rectangle((x, y, x + 195, y + 75), fill=color)
        draw.text((x + 5, y + 22), label, font=font, fill='white')
    draw.rectangle((385, 210, 575, 330), outline='#ff7833', width=5)
    draw.text((399, 251), 'M0 CENTER', font=font, fill='#ff7833')
    image.save(out / 'grid.png')
    for name, label in [('inline.srt', 'M0 EMBEDDED SRT'), ('external.srt', 'M0 EXTERNAL SRT')]:
        (out / name).write_text(f'1\n00:00:00,000 --> 00:00:16,000\n{label}\n', encoding='utf-8')
    ass = '''[Script Info]
ScriptType: v4.00+
PlayResX: 960
PlayResY: 540
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,36,&H0000FF00,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,30,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,0:00:16.00,Default,,0,0,0,,LABEL
'''
    (out / 'inline.ass').write_text(ass.replace('LABEL', 'M0 EMBEDDED ASS'), encoding='utf-8')
    (out / 'external.ass').write_text(ass.replace('LABEL', 'M0 EXTERNAL ASS'), encoding='utf-8')
    (out / 'external.vtt').write_text('WEBVTT\n\n00:00.000 --> 00:16.000\nM0 EXTERNAL VTT\n', encoding='utf-8')
    commands = []

    def run(arguments):
        cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', *map(str, arguments)]
        commands.append(cmd)
        subprocess.run(cmd, check=True)

    multi = out / '网格 multi.mkv'
    run(['-loop', '1', '-framerate', '25', '-i', out / 'grid.png',
         '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
         '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000',
         '-i', out / 'inline.srt', '-i', out / 'inline.ass',
         '-map', '0:v', '-map', '1:a', '-map', '2:a', '-map', '3:s', '-map', '4:s',
         '-t', '16', '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast', '-crf', '18',
         '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '96k', '-c:s', 'copy',
         '-metadata:s:a:0', 'language=eng', '-metadata:s:a:1', 'language=jpn',
         '-metadata:s:s:0', 'language=eng', '-metadata:s:s:1', 'language=jpn', multi])
    run(['-i', multi, '-map', '0:v:0', '-c', 'copy', out / 'grid.mp4'])
    run(['-display_rotation:v:0', '90', '-i', out / 'grid.mp4', '-c', 'copy', out / 'portrait.mp4'])
    run(['-i', multi, '-map', '0:a:0', '-c', 'copy', out / 'audio.m4a'])
    (out / 'broken.mkv').write_bytes(b'M0 deliberately invalid Matroska fixture\n')
    for size, name in [('1920x1080', 'motion-1080.mp4'), ('3840x2160', 'motion-4k.mp4')]:
        run(['-f', 'lavfi', '-i', f'testsrc2=size={size}:rate=30',
             '-f', 'lavfi', '-i', 'sine=frequency=220:sample_rate=48000',
             '-t', '18', '-c:v', 'libx264', '-threads', '4', '-preset', 'ultrafast', '-crf', '28',
             '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '96k', out / name])
    # A separate media path permits A/B/C load isolation checks with identical pixels.
    import shutil
    shutil.copyfile(multi, out / '换片 C.mkv')
    shutil.copyfile(multi, out / ('long-' + 'fixture-' * 12 + '中文 空格.mkv'))
    files = []
    for path in sorted(out.iterdir()):
        if path.is_file() and path.name not in {'manifest.json', 'media-info.json', 'README.md'}:
            files.append({'name': path.name, 'bytes': path.stat().st_size,
                          'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (out / 'manifest.json').write_text(json.dumps({'commands': commands, 'files': files},
                                                  ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'created': len(files), 'directory': str(out)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
