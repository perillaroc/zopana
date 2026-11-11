"""Summarize M6 paired B04 samples and retain the host verification limits."""

import argparse
import json
from pathlib import Path
from statistics import median


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def performance(root):
    names = ('motion-1080.mp4', 'motion-4k.mp4')
    results = {name: {'baseline': [], 'production': []} for name in names}
    for variant in ('baseline', 'production'):
        for pair in (1, 2, 3):
            path = root / f'performance-{variant}-{pair}/results.json'
            report = load(path)
            details = report['details']
            samples = details['performance']
            if not report['passed'] or details['devicePixelRatio'] != 1 or tuple(x['media'] for x in samples) != names:
                raise ValueError(f'Unexpected performance report {path}')
            for sample in samples:
                if sample['hwdec'] != 'no' or len(sample['memorySamples']) != 24:
                    raise ValueError(f'Inconsistent playback or sampling in {path}')
                results[sample['media']][variant].append(dict(
                    pair=pair, firstFrameMs=sample['firstFrameMs'], cpuPercentMachine=sample['cpuPercentMachine'],
                    workingSetMiB=median(m['workingSetMiB'] for m in sample['memorySamples']),
                    privateMiB=median(m['privateMiB'] for m in sample['memorySamples']),
                    frameDrop=sample['frameDropAfter'] - sample['frameDropBefore'],
                    decoderDrop=sample['decoderDropAfter'] - sample['decoderDropBefore']))
    keys = ('firstFrameMs', 'cpuPercentMachine', 'workingSetMiB', 'privateMiB', 'frameDrop', 'decoderDrop')
    return {name: {variant: dict(samples=samples, medians={key: median(s[key] for s in samples) for key in keys})
                   for variant, samples in variants.items()} for name, variants in results.items()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('reports', type=Path)
    args = parser.parse_args()
    root = args.reports.resolve()
    performance_results = performance(root)
    no_playback_drops = all(s['frameDrop'] == 0 and s['decoderDrop'] == 0
                            for variants in performance_results.values()
                            for variant in variants.values() for s in variant['samples'])
    shortcuts = load(root / 'shortcuts/results.json')
    stress = load(root / 'stress/results.json')
    details = stress['details']
    cycles = details['cycleResources']
    summary = dict(shortcuts=dict(passed=shortcuts['passed'], checks=len(shortcuts['checks']),
                                  nativeIme=shortcuts['details']['nativeIme']),
                   stress=dict(passed=stress['passed'], checks=len(stress['checks']), cycles=len(cycles) - 1,
                               resourcesFirst=cycles[0], resourcesLast=cycles[-1]),
                   performance=performance_results, noPlaybackDrops=no_playback_drops)
    (root / 'm6-evidence.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    if not (shortcuts['passed'] and stress['passed'] and summary['stress']['cycles'] == 20 and no_playback_drops):
        raise SystemExit('M6 probe evidence includes an incomplete or failing check')


if __name__ == '__main__':
    main()
