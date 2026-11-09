"""Validate actual rendered pixels, including orientation, bars and subtitle clipping."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image


def rgb(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)


def difference(on, off):
    mask = np.max(np.abs(on - off), axis=2) > 20
    ys, xs = np.where(mask)
    return {'pixels': int(mask.sum()),
            'bbox': [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1] if len(xs) else None}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    build = parser.parse_args().build.resolve()
    checks = []
    metrics = []

    def check(name, passed, **data):
        checks.append(dict(name=name, passed=bool(passed), **data))
        print(('PASS ' if passed else 'FAIL ') + name)

    for percent in (100, 150, 200):
        folder = build / f'probe-{percent}'
        result = json.loads((folder / 'results.json').read_text(encoding='utf-8'))
        check(f'{percent}% probe cases', result['passed'])
        check(f'{percent}% effective DPR', abs(result['details']['devicePixelRatio'] - percent / 100) < .001)
        source = rgb(folder / 'source.png')
        source_zoom = rgb(folder / 'source-zoom.png')
        check(f'{percent}% source invariant under zoom', np.array_equal(source, source_zoom))
        check(f'{percent}% worker PNG pixels match viewport', np.array_equal(rgb(folder / 'viewport-worker.png'), rgb(folder / 'viewport-overlay-excluded.png')))
        viewport = rgb(folder / 'viewport-fit-off.png')
        h, w = viewport.shape[:2]
        image_h = round(w * 540 / 960)
        top = round((h - image_h) / 2)
        expected = np.zeros_like(viewport)
        expected[top:top + image_h] = np.asarray(Image.fromarray(source.astype('uint8')).resize((w, image_h), Image.Resampling.BILINEAR))
        error = float(np.abs(viewport - expected).mean())
        check(f'{percent}% fit orientation and letterbox', error < 1.0, meanAbsoluteRgbError=error)
        # Source corner patches establish orientation and color channel order.
        colors = [source[40, 30], source[40, 780], source[480, 30], source[480, 780]]
        check(f'{percent}% RGB orientation', colors[0][0] > 200 and colors[1][1] > 150 and colors[2][2] > 150 and colors[3][0] > 180 and colors[3][1] > 150)
        for kind in ('srt', 'ass'):
            fit = difference(rgb(folder / f'{kind}-fit.png'), rgb(folder / f'{kind}-fit-off.png'))
            zoom = difference(rgb(folder / f'{kind}-zoom.png'), rgb(folder / f'{kind}-zoom-off.png'))
            pan = difference(rgb(folder / f'{kind}-zoom-pan.png'), rgb(folder / f'{kind}-zoom-pan-off.png'))
            ratio = (pan['bbox'][3] - pan['bbox'][1]) / (fit['bbox'][3] - fit['bbox'][1]) if pan['bbox'] and fit['bbox'] else 0
            check(f'{percent}% {kind} visible at fit, clipped at zoom', fit['pixels'] > 100 and zoom['pixels'] < fit['pixels'] / 10, fit=fit, zoom=zoom)
            check(f'{percent}% {kind} scales with panned frame', pan['pixels'] > 100 and 1.7 < ratio < 2.3, pan=pan, glyphHeightRatio=ratio)
        metrics.append({'percent': percent, 'fitError': error, 'probeChecks': len(result['checks'])})
    report = {'passed': all(c['passed'] for c in checks), 'checks': checks, 'metrics': metrics}
    (build / 'pixel-analysis.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
