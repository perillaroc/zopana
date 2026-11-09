"""Audit the ZIP and exercise the deployed runtime without development PATHs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('build', type=Path)
parser.add_argument('fixture', type=Path)
args = parser.parse_args()
build = args.build.resolve()
stage = build / 'dist-final/zopana-windows-x64'
zip_path = build / 'dist-final/zopana-windows-x64.zip'
repo = Path(__file__).resolve().parents[2]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
with zipfile.ZipFile(zip_path) as archive:
    entries = [name for name in archive.namelist() if not name.endswith('/')]
    matches = all(archive.read(name) == (build / 'dist-final' / name).read_bytes() for name in entries)
audit = dict(entries=len(entries), zipSHA256=sha(zip_path), exeSHA256=sha(build / 'zopana.exe'),
             allZipEntriesMatchStaging=matches, exeMatches=sha(stage / 'zopana.exe') == sha(build / 'zopana.exe'),
             qtSvgIncluded=(stage / 'Qt6Svg.dll').is_file(),
             lucideLicenseMatches=sha(stage / 'notices/lucide-LICENSE.txt') == sha(repo / 'packaging/notices/lucide-LICENSE.txt'))
audit['passed'] = all(audit[key] for key in ('allZipEntriesMatchStaging', 'exeMatches', 'qtSvgIncluded', 'lucideLicenseMatches'))
(build / 'package-audit.json').write_text(json.dumps(audit, indent=2), encoding='utf-8')
if not audit['passed']:
    raise SystemExit('Package audit failed')

test = stage / '_m2-validation.exe'
if test.exists():
    raise SystemExit('Temporary validation executable already exists; investigate before rerunning.')
shutil.copy2(build / 'zopana_m2_smoke.exe', test)
environment = {key: value for key, value in os.environ.items()
               if not key.upper().startswith('QT_') and key.upper() != 'CMAKE_PREFIX_PATH'}
environment['PATH'] = os.environ['SystemRoot'] + '\\System32;' + os.environ['SystemRoot']
try:
    with (build / 'portable-smoke.log').open('w', encoding='utf-8') as log:
        result = subprocess.run([str(test), str(args.fixture.resolve()), str(build / 'portable-smoke.json')],
                                env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=20,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    report = json.loads((build / 'portable-smoke.json').read_text(encoding='utf-8'))
    report.update(exitCode=result.returncode, restrictedPath=True, qtEnvironmentRemoved=True)
    report['modulePathsVerified'] = all(Path(path).parent == stage and Path(path).name == name
                                        for name, path in report['modules'].items())
    report['passed'] = report['passed'] and result.returncode == 0 and report['modulePathsVerified']
    (build / 'portable-smoke.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(dict(zipSHA256=audit['zipSHA256'], runtimePassed=report['passed'], exitCode=result.returncode)))
    if not report['passed']:
        raise SystemExit('Deployed runtime smoke test failed')
finally:
    test.unlink(missing_ok=True)
