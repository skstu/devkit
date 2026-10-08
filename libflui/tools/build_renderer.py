#!/usr/bin/env python3
"""Producer-only build: consumers receive binaries, never a Flutter toolchain."""
import argparse
import json
import os
import plistlib
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--sdk', required=True, type=Path)
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--work', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--arch', default='arm64')
parser.add_argument('--test', action='store_true')
a = parser.parse_args()
lock = json.loads((a.source.parent / 'flutter.lock.json').read_text())
version = json.loads((a.sdk / 'bin/cache/flutter.version.json').read_text())
for key in ('frameworkRevision', 'engineRevision', 'dartSdkVersion'):
    if version[key] != lock[key]:
        raise SystemExit(f'Flutter {key} differs from flutter.lock.json; upgrade and validate the complete SDK together')
a.work.mkdir(parents=True, exist_ok=True)
for name in ('pubspec.yaml', 'pubspec.lock'):
    if (a.source / name).exists(): shutil.copy2(a.source / name, a.work / name)
for name in ('lib', 'test'):
    shutil.copytree(a.source / name, a.work / name, dirs_exist_ok=True)
shutil.copy2(a.source.parent / 'examples/quotes/flui.xml', a.work / 'test/quotes.xml')
env = dict(os.environ, FLUTTER_SUPPRESS_ANALYTICS='true', CI='true')
flutter = str(a.sdk / 'bin/flutter')
def run(*cmd):
    subprocess.run(cmd, cwd=a.work, env=env, check=True)
run(flutter, '--suppress-analytics', '--no-version-check', 'pub', 'get', '--offline', '--enforce-lockfile')
if a.test:
    run(flutter, '--suppress-analytics', '--no-version-check', 'analyze', '--no-pub')
    run(flutter, '--suppress-analytics', '--no-version-check', 'test', '--no-pub')
else:
    runtime_bundle = a.output
    assembly = a.work / 'assembly'
    assembly.mkdir(exist_ok=True)
    sdk_root = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
    run(flutter, '--suppress-analytics', '--no-version-check', 'assemble',
        '--output=' + str(assembly), '-dTargetPlatform=darwin', '-dTargetFile=lib/main.dart',
        '-dBuildMode=release', '-dDarwinArchs=' + a.arch.replace(';', ' '),
        '-dSdkRoot=' + sdk_root, 'release_macos_bundle_flutter_assets')
    # Only executable frameworks belong in this signed-code directory. Flutter's
    # build stamps and debug symbols stay in assembly, outside the consumer SDK.
    frameworks = runtime_bundle / 'Contents' / 'Frameworks'
    if frameworks.exists(): shutil.rmtree(frameworks)
    frameworks.mkdir(parents=True)
    for name in ('FlutterMacOS.framework', 'App.framework'):
        shutil.copytree(assembly / name, frameworks / name, symlinks=True)
        run('/usr/bin/codesign', '--force', '--sign', '-', str(frameworks / name))
    resources = runtime_bundle / 'Contents' / 'Resources'
    resources.mkdir(exist_ok=True)
    licenses = resources / 'licenses'
    licenses.mkdir(exist_ok=True)
    shutil.copy2(a.sdk / 'LICENSE', licenses / 'Flutter-LICENSE')
    engine_licenses = a.sdk / 'bin/cache/artifacts/engine/darwin-x64-release/LICENSE.artifacts.md'
    shutil.copy2(engine_licenses, licenses / 'Flutter-engine-artifacts.md')
    shutil.copy2(a.sdk / 'bin/cache/pkg/sky_engine/LICENSE', licenses / 'Flutter-engine-LICENSE')
    lock['macos_renderer'] = 'skia-metal'
    (resources / 'runtime.json').write_text(json.dumps(lock, indent=2) + '\n')
    (runtime_bundle / 'Contents' / 'Info.plist').write_bytes(plistlib.dumps({
        'CFBundleIdentifier': 'org.skstu.libflui.runtime',
        'CFBundleName': 'libflui Runtime',
        'CFBundlePackageType': 'BNDL',
        'CFBundleVersion': (a.source.parent.parent / 'VERSION').read_text().strip(),
    }))
    run('/usr/bin/codesign', '--force', '--sign', '-', str(runtime_bundle))
    (runtime_bundle.parent / 'renderer.complete').write_text('release renderer\n')
