#!/usr/bin/env python3
"""Mocked host/ADB, summary, and source ZIP integration checks for benchmark.sh."""
from pathlib import Path
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

SOURCE = Path(__file__).resolve().parents[1]
SAMPLE = b'''# AOWIS Mobile GPU benchmark, 15 s, steady_clock; CPU/GPU timing as reported by bgfx
# Device: Test Linux
# Preset: 2; logical nodes=2; logical pipes=3; total=5
# Process RSS beginning KB=0; end KB=0
elapsed_us,interval_us,submit_us,gpu_us,draws,visible_tiles,uploads,lod_stride,nodes_drawn,pipes_drawn,vertex_buffer_bytes
1,10000,120,50,3,2,1,1,2,3,100
2,20000,250,-1,4,3,0,1,2,3,200
3,30000,350,200,5,4,3,2,4,6,300
'''
NAME = 'network-2-20261105-172111-123.csv'


def create_tar(files):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode='w') as archive:
        for name, content in files.items():
            member = tarfile.TarInfo(name='./' + name)
            member.size = len(content)
            archive.addfile(member, io.BytesIO(content))
    return output.getvalue()


class BenchmarkCollectorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.root = self.work / 'some arbitrary checkout'
        self.root.mkdir()
        for filename in ('benchmark.sh', '.gitignore', 'tools/collect_benchmarks.py',
                         'tools/zip_mobile_project.sh'):
            target = self.root / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(SOURCE / filename, target)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        (self.root / 'README.md').write_text('Test project\n')
        self.home_data = self.work / 'data'
        self.desktop = self.home_data / 'AOWIS Mobile' / 'benchmarks'
        self.desktop.mkdir(parents=True)
        (self.desktop / NAME).write_bytes(SAMPLE)
        self.bin = self.work / 'bin'
        self.bin.mkdir()
        self.android_tar = self.work / 'android.tar'
        self.android_tar.write_bytes(create_tar({NAME: SAMPLE}))
        self.lifecycle_tar = self.work / 'lifecycle.tar'
        self.lifecycle_tar.write_bytes(create_tar({'events.csv': b'utc,event\n1,recovered\n'}))
        script = self.bin / 'adb'
        script.write_text('''#!/usr/bin/env python3
import os, sys
from pathlib import Path
args = sys.argv[1:]
if args[:2] == ['devices', '-l']:
    print('List of devices attached')
    print('phone-a device product:fake model:Pixel')
    print('phone-b device product:fake model:Pixel')
    print('phone-c unauthorized')
    sys.exit(0)
if len(args) >= 5 and args[0] == '-s':
    serial = args[1]
    rest = args[2:]
    if rest[:3] == ['shell', 'pm', 'path']:
        print('package:/data/app/base.apk')
    elif rest[:3] == ['shell', 'getprop', 'ro.product.model']:
        print('TestPhone')
    elif rest[:3] == ['shell', 'getprop', 'ro.build.version.release']:
        print('14')
    elif rest[:3] == ['exec-out', 'run-as', 'org.aowis.mobile']:
        src = os.environ['MOCK_ANDROID_TAR'] if 'files/benchmarks' in rest else os.environ['MOCK_LIFECYCLE_TAR']
        sys.stdout.buffer.write(Path(src).read_bytes())
    else:
        sys.exit(22)
else:
    sys.exit(23)
''')
        script.chmod(0o755)
        self.env = dict(os.environ)
        self.env.update(XDG_DATA_HOME=str(self.home_data),
                        PATH=f'{self.bin}:{os.environ["PATH"]}',
                        MOCK_ANDROID_TAR=str(self.android_tar),
                        MOCK_LIFECYCLE_TAR=str(self.lifecycle_tar))

    def invoke(self, *args):
        return subprocess.run([str(self.root / 'benchmark.sh'), *args],
                              cwd=self.root, env=self.env, text=True, capture_output=True)

    def test_collect_two_devices_and_summary(self):
        result = self.invoke('--no-zip')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.root / 'benchmarks/desktop' / NAME).exists())
        devices = list((self.root / 'benchmarks/android').glob('*/' + NAME))
        self.assertEqual(len(devices), 2)
        events = list((self.root / 'benchmarks/lifecycle').glob('*/events.csv'))
        self.assertEqual(len(events), 2)
        summary = (self.root / 'benchmarks/summary.csv').read_text()
        self.assertIn('20.000', summary)
        self.assertIn('30.000', summary)
        self.assertIn('TestPhone', (self.root / 'benchmarks/summary.md').read_text())
        self.assertEqual(summary.count(NAME), 3)
        rerun = self.invoke('--no-zip')
        self.assertEqual(rerun.returncode, 0, rerun.stderr)
        self.assertIn('0 new', rerun.stdout)
        self.assertEqual(len(list((self.root / 'benchmarks/android').rglob('network-*.csv'))), 2)

    def test_snapshot_contains_reports_even_though_ignored(self):
        result = self.invoke('--desktop-only')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.work / 'mobile.zip') as archive:
            names = archive.namelist()
            self.assertIn('AOWIS-GUI-Mobile/benchmarks/summary.csv', names)
            self.assertIn('AOWIS-GUI-Mobile/benchmarks/desktop/' + NAME, names)
            self.assertNotIn('AOWIS-GUI-Mobile/.git/config', names)
        git_check = subprocess.run(['git', 'ls-files', '-o', '--exclude-standard'],
                                   cwd=self.root, capture_output=True, text=True, check=True)
        self.assertNotIn('benchmarks/', git_check.stdout)

    def test_no_device_is_nonfatal_unless_strict(self):
        (self.bin / 'adb').unlink()
        # Guarantee adb is absent: the collector can still produce a Linux report.
        env = dict(self.env)
        env['PATH'] = '/usr/bin:/bin'
        result = subprocess.run([str(self.root / 'benchmark.sh'), '--no-zip'],
                                cwd=self.root, env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('NOTICE:', result.stderr)
        result = subprocess.run([str(self.root / 'benchmark.sh'), '--strict', '--no-zip'],
                                cwd=self.root, env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 1)

    def test_strict_selection_and_android_only(self):
        result = self.invoke('--android-only', '--serial', 'phone-a', '--strict', '--no-zip')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(list((self.root / 'benchmarks/android').rglob('network-*.csv'))), 1)
        result = self.invoke('--android-only', '--serial', 'not-connected', '--strict', '--no-zip')
        self.assertEqual(result.returncode, 1)

    def test_no_path_traversal_from_device_archive(self):
        self.android_tar.write_bytes(create_tar({'../malicious.csv': b'x', 'ignored.exe': b'y', NAME: SAMPLE}))
        result = self.invoke('--android-only', '--no-zip')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / 'malicious.csv').exists())
        self.assertFalse((self.root / 'benchmarks/android/ignored.exe').exists())


if __name__ == '__main__':
    unittest.main()
