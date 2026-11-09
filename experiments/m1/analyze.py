"""Verify UI evidence, color contrast and exact vendored Lucide source bytes."""
import hashlib
import json
from pathlib import Path
import sys


def luminance(color):
    values = [int(color[i:i+2], 16) / 255 for i in (1, 3, 5)]
    values = [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in values]
    return sum(v * weight for v, weight in zip(values, (.2126, .7152, .0722)))


def main():
    build = Path(sys.argv[1]).resolve()
    repo = Path(__file__).resolve().parents[2]
    checks = []

    def check(name, passed, **fields):
        checks.append(dict(name=name, passed=bool(passed), **fields))
        print(('PASS ' if passed else 'FAIL ') + name)

    for percent in (100, 150, 200):
        result = json.loads((build / f'qa-{percent}/results.json').read_text(encoding='utf-8'))
        check(f'{percent}% UI checks', result['passed'])
        check(f'{percent}% effective DPR', result['details']['devicePixelRatio'] == percent / 100)
        palette = result['details']['palette']
        for a, b, minimum in [('text', 'surface', 4.5), ('secondary', 'window', 4.5), ('accentText', 'accent', 4.5), ('accent', 'surface', 3)]:
            x, y = sorted((luminance(palette[a]), luminance(palette[b])))
            ratio = (y + .05) / (x + .05)
            check(f'{percent}% contrast {a}/{b}', ratio >= minimum, ratio=ratio, minimum=minimum)
    sources = json.loads((repo / 'resources/icons/lucide/SOURCES.json').read_text(encoding='utf-8-sig'))
    for asset in sources['files']:
        content = (repo / asset['local']).read_bytes()
        blob = hashlib.sha1(b'blob ' + str(len(content)).encode() + b'\0' + content).hexdigest()
        check('pinned asset ' + asset['upstream'], hashlib.sha256(content).hexdigest() == asset['sha256'] and blob == asset['gitBlob'])
    license_text = (repo / 'packaging/notices/lucide-LICENSE.txt').read_text(encoding='utf-8')
    check('complete ISC and Feather MIT retained', 'ISC License' in license_text and 'The MIT License' in license_text and 'Cole Bemis' in license_text)
    report = {'passed': all(c['passed'] for c in checks), 'checks': checks, 'lucideCommit': sources['commit']}
    (build / 'asset-contrast-analysis.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
