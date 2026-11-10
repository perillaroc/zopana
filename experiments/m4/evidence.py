"""Summarize the completed M4 run and record working-tree/media identities."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('build', type=Path)
parser.add_argument('media', type=Path)
args = parser.parse_args()
build = args.build.resolve()
repo = Path(__file__).resolve().parents[2]
read = lambda path: json.loads(path.read_text(encoding='utf-8-sig'))
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
reports = []
for percent in (100, 150, 200):
    for name in ('m4', 'm4-recovery', 'm3', 'm2', 'qa', 'm0-regression/probe'):
        path = build / f'{name}-{percent}/results.json'
        report = read(path)
        dpr = report.get('devicePixelRatio', report.get('details', {}).get('devicePixelRatio'))
        reports.append(dict(path=path.relative_to(build).as_posix(), passed=report['passed'] and dpr == percent / 100,
                            checks=len(report['checks']), devicePixelRatio=dpr))
for name in ('lifecycle/results.json', 'm0-regression/pixel-analysis.json', 'asset-contrast-analysis.json',
             'package-audit.json', 'portable-smoke.json'):
    report = read(build / name)
    reports.append(dict(path=name, passed=report['passed'], checks=len(report.get('checks', []))))

media = []
for name in ('zopana-ui-v1', 'zopana-ui-m2-v1', 'zopana-ui-m3-v1', 'zopana-ui-m4-v1'):
    bank = args.media / name
    files = read(bank / 'manifest.json')['files']
    checks = [dict(name=item['name'], matches=sha(bank / item['name']) == item['sha256']) for item in files]
    media.append(dict(library=name, files=len(files), passed=all(item['matches'] for item in checks), checks=checks))
(build / 'fixture-integrity.json').write_text(json.dumps(media, indent=2, ensure_ascii=False), encoding='utf-8')

paths = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=repo).decode('utf-8').split('\0')
# Renamed/deleted index entries may still exist while the user reviews staging.
# Record the actual working files used by the build, without changing the index.
files = [dict(path=name, sha256=sha(repo / name)) for name in sorted(set(paths)) if name and (repo / name).is_file()]
identity = dict(baseCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo).decode().strip(),
                scope='existing working-tree files, including staged and untracked additions; no index mutation', files=files)
(build / 'source-identity.json').write_text(json.dumps(identity, indent=2, ensure_ascii=False), encoding='utf-8')
summary = dict(passed=all(item['passed'] for item in reports + media), generatedAt=datetime.now(timezone.utc).isoformat(),
               reports=reports, libraries=[{key: item[key] for key in ('library', 'files', 'passed')} for item in media],
               sourceFiles=len(files), binaries={name: sha(build / name) for name in ('zopana.exe', 'zopana_m4.exe', 'libmpv-2.dll')})
(build / 'm4-evidence.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps({key: summary[key] for key in ('passed', 'sourceFiles', 'libraries')}, ensure_ascii=False))
raise SystemExit(0 if summary['passed'] else 1)
