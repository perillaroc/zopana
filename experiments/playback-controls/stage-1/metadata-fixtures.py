"""Prepare isolated metadata fixtures and record independent FFprobe values."""
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
    base, output = args.base.resolve(), args.output.resolve()
    output.mkdir(parents=True)
    commands = []

    def ffmpeg(name, *arguments):
        command = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', '-n',
                   *map(str, arguments), str(output / name)]
        commands.append(command)
        subprocess.run(command, check=True)

    tagged = '未播放 第02集.mp4'
    title = '测试媒体标题'
    grid = base / 'grid.mp4'
    ffmpeg(tagged, '-i', grid, '-map', '0', '-c', 'copy', '-metadata', f'title={title}')
    ffmpeg('multi.mkv', '-i', base / '网格 multi.mkv', '-map', '0', '-c', 'copy',
           '-metadata', 'TITLE=Matroska multi-track')
    ffmpeg('sar.mp4', '-i', grid, '-vf', 'scale=720:540,setsar=4/3',
           '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast')
    ffmpeg('rotated-sar.mp4', '-display_rotation:v:0', '-90', '-i', output / 'sar.mp4', '-c', 'copy')
    ffmpeg('clip.ts', '-i', grid, '-map', '0:v:0', '-c', 'copy')
    ffmpeg('unknown-duration.webm', '-f', 'lavfi', '-i', 'color=c=navy:s=320x180:r=15',
           '-t', '2', '-an', '-c:v', 'libvpx-vp9', '-threads', '2', '-live', '1')
    ffmpeg('cover.m4a', '-i', base / 'audio.m4a', '-i', base / 'grid.png', '-map', '0:a:0', '-map', '1:v:0',
           '-c:a', 'copy', '-c:v', 'mjpeg', '-threads:v', '2', '-disposition:v:0', 'attached_pic')
    shutil.copyfile(base / 'audio.m4a', output / 'audio.m4a')
    shutil.copyfile(base / 'broken.mkv', output / 'broken.mkv')
    ffmpeg('playing.mp4', '-stream_loop', '1', '-i', base / 'motion-1080.mp4', '-map', '0', '-c', 'copy')

    sizes = {tagged: (960, 540), 'multi.mkv': (960, 540), 'rotated-sar.mp4': (720, 540),
             'clip.ts': (960, 540), 'unknown-duration.webm': (320, 180)}
    cases = []
    for name in (*sizes, 'audio.m4a', 'cover.m4a', 'broken.mkv'):
        pos = subprocess.run([
            'ffprobe', '-v', 'error', '-show_entries',
            'format=format_name,duration:format_tags=title:stream=codec_type,width,height,sample_aspect_ratio:'
            'stream_disposition=attached_pic,default:stream_side_data=rotation', '-of', 'json', str(output / name),
        ], capture_output=True, encoding='utf-8', check=False)
        if name == 'broken.mkv':
            if not pos.returncode:
                raise RuntimeError('Broken sample unexpectedly passed FFprobe')
            cases.append({'file': name, 'status': 'unreadable', 'ffprobeError': pos.stderr.strip()})
            continue
        pos.check_returncode()
        oracle = json.loads(pos.stdout)
        container = oracle['format']
        tags = {key.lower(): value for key, value in container.get('tags', {}).items()}
        videos = [stream for stream in oracle['streams'] if stream['codec_type'] == 'video'
                  and not stream['disposition'].get('attached_pic')]
        width, height = (videos[0]['width'], videos[0]['height']) if videos else (None, None)
        if (width, height) != sizes.get(name, (None, None)):
            raise RuntimeError(f'Unexpected dimensions in {name}: {width}×{height}')
        duration = float(container['duration']) if 'duration' in container else None
        # FFprobe may estimate MPEG-TS duration by reading the end. The probe records both values.
        cases.append({'file': name, 'status': 'ok', 'title': tags.get('title'), 'duration': duration,
                      'width': width, 'height': height, 'ffprobe': oracle})
    files = {path.name: {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
             for path in sorted(output.iterdir())}
    (output / 'manifest.json').write_text(json.dumps({'commands': commands, 'files': files, 'cases': cases},
                                                     indent=2, ensure_ascii=False), encoding='utf-8')


if __name__ == '__main__':
    main()
