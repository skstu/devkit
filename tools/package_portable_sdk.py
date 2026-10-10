#!/usr/bin/env python3
"""Package standalone libcrypt/libnet/libble on Android, Linux, or Windows.

Requires an existing Release build. Runs its tests (on the specified Android
device for cross builds), checks the public exports, and records every source
and installed byte. Packages are local development artifacts, never published.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def capture(*args):
    return subprocess.check_output([str(x) for x in args], text=True).strip()


def run(*args):
    subprocess.run([str(x) for x in args], check=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    result = {}
    for p in sorted(root.rglob('*')):
        if '__pycache__' in p.parts or p.suffix == '.pyc':
            continue
        name = p.relative_to(root).as_posix()
        if p.is_symlink():
            target = os.readlink(p)
            if os.path.isabs(target) or not p.resolve().is_relative_to(root.resolve()):
                raise ValueError('Escaping package symlink: ' + name)
            result[name] = {'symlink': target}
        elif p.is_file():
            result[name] = {'sha256': sha(p), 'size': p.stat().st_size}
    return result


def sources(root, component):
    files = inventory(root / component)
    result = {component + '/' + k: v for k, v in files.items()}
    for name in ['VERSION', 'vcpkg.json', 'vcpkg-configuration.json',
                 'cmake/NativeLibsBuild.cmake', 'tools/package_portable_sdk.py']:
        path = root / name
        if path.is_file():
            result[name] = {'sha256': sha(path), 'size': path.stat().st_size}
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--component', choices=['libcrypt', 'libnet', 'libble'], required=True)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--dependencies', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--platform', choices=['android', 'linux', 'windows'], required=True)
    p.add_argument('--android-serial')
    p.add_argument('--adb', default='adb')
    p.add_argument('--allow-dirty', action='store_true')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = a.build.resolve()
    cache = (build / 'CMakeCache.txt').read_text()
    setting = lambda key: next((line.split('=', 1)[1] for line in cache.splitlines()
                                if re.match(re.escape(key) + r':[^=]+=', line)), '')
    source = root / (a.component + ('/sdk' if a.component == 'libnet' else ''))
    if Path(setting('CMAKE_HOME_DIRECTORY')).resolve() != source:
        p.error('Build must use the standalone component from this source tree')
    if setting('BUILD_TESTING') != 'ON':
        p.error('BUILD_TESTING=ON is required')
    if a.platform != 'windows' and setting('CMAKE_BUILD_TYPE') != 'Release':
        p.error('Require a Release build')
    system = next((build / 'CMakeFiles').glob('*/CMakeSystem.cmake')).read_text()
    expected_system = {'android': 'Android', 'linux': 'Linux', 'windows': 'Windows'}[a.platform]
    if f'set(CMAKE_SYSTEM_NAME "{expected_system}")' not in system:
        p.error('Build platform mismatch')
    architecture = 'arm64' if a.platform == 'android' else 'x86_64'
    if a.platform == 'android' and setting('ANDROID_ABI') != 'arm64-v8a':
        p.error('The initial Android package supports arm64-v8a')
    if a.platform == 'linux' and 'set(CMAKE_SYSTEM_PROCESSOR "x86_64")' not in system:
        p.error('The initial Linux package supports x86_64')
    if a.component != 'libble':
        if not a.dependencies:
            p.error('--dependencies is required')
        dep = a.dependencies.resolve()
        if Path(setting('VCPKG_INSTALLED_DIR')).resolve() / setting('VCPKG_TARGET_TRIPLET') != dep:
            p.error('Dependencies must match the configured provider tree')
    before = sources(root, a.component)
    dirty = bool(capture('git', '-C', root, 'status', '--porcelain', '--',
                         a.component, 'VERSION', 'vcpkg.json', 'vcpkg-configuration.json',
                         'cmake/NativeLibsBuild.cmake', 'tools/package_portable_sdk.py'))
    if dirty and not a.allow_dirty:
        p.error('Pass --allow-dirty to package uncommitted development inputs')
    commit = capture('git', '-C', root, 'rev-parse', 'HEAD')
    run('cmake', '--build', build, '--config', 'Release', '--parallel', '4')
    a.output.mkdir(parents=True, exist_ok=True)
    package = Path(tempfile.mkdtemp(prefix=f'{a.component}-{a.platform}-{architecture}-', dir=a.output.resolve()))
    run('cmake', '--install', build, '--config', 'Release', '--prefix', package)
    suffix = a.component.removeprefix('lib')
    binaries = list((package / ('bin' if a.platform == 'windows' else 'lib')).glob(
        f'devkit_{suffix}.dll' if a.platform == 'windows' else f'libdevkit_{suffix}.so*'))
    binary = next(x for x in binaries if not x.is_symlink())
    prefix = {'libcrypt': 'DKCRYPT', 'libnet': 'DKNET', 'libble': 'DKBLE'}[a.component]
    header = package / f'include/{a.component}/{suffix}.h'
    expected = set(re.findall(prefix + r'_CALL\s+(dk\w+)\(', header.read_text()))
    if a.platform == 'windows':
        # dumpbin is available in the same MSVC environment used by CMake.
        dump = capture('dumpbin', '/nologo', '/exports', binary)
        exports = set(re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\w+)\s*$', dump, re.M))
        machine = capture('dumpbin', '/nologo', '/headers', binary)
        if '8664 machine (x64)' not in machine:
            raise ValueError('Runtime architecture mismatch')
        dependencies = re.findall(r'^\s+(\S+\.dll)\s*$', capture('dumpbin', '/nologo', '/dependents', binary), re.M | re.I)
        soname = binary.name
        minimum = '10.0'
    else:
        compiler_value = setting('CMAKE_C_COMPILER')
        if not compiler_value:
            compiler_file = next((build / 'CMakeFiles').glob('*/CMakeCCompiler.cmake')).read_text()
            compiler_value = re.search(r'set\(CMAKE_C_COMPILER "([^"]+)"\)', compiler_file).group(1)
        compiler = Path(compiler_value)
        readelf = compiler.parent / 'llvm-readelf' if a.platform == 'android' else Path(shutil.which('readelf'))
        dynamic = capture(readelf, '--dynamic', binary)
        dependencies = re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic)
        soname = re.search(r'\(SONAME\).*?\[(.*?)\]', dynamic).group(1)
        symbols = capture(readelf, '--dyn-syms', '--wide', binary)
        exports = set()
        for line in symbols.splitlines():
            fields = line.split()
            if len(fields) >= 8 and fields[4] in ('GLOBAL', 'WEAK') and fields[6] not in ('UND', 'ABS'):
                exports.add(fields[7].split('@')[0])
        machine = capture(readelf, '--file-header', binary)
        if ('AArch64' if a.platform == 'android' else 'X86-64') not in machine:
            raise ValueError('Runtime architecture mismatch')
        if a.platform == 'android':
            minimum = setting('ANDROID_PLATFORM').removeprefix('android-')
            if not minimum.isdecimal():
                p.error('Explicit ANDROID_PLATFORM is required')
            if a.component == 'libble':
                expected.update({'Java_com_skstu_devkit_ble_BleRuntime_nativeInitialize',
                                 'Java_com_skstu_devkit_ble_BleEngine_nativeEvent'})
            allowed = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so'}
            if not set(dependencies) <= allowed:
                raise ValueError('Unexpected Android runtime dependencies: ' + repr(dependencies))
        else:
            versions = re.findall(r'GLIBC_(\d+\.\d+)', capture(readelf, '--version-info', binary))
            minimum = 'glibc ' + max(versions, key=lambda v: tuple(map(int, v.split('.'))))
    if exports != expected or not expected:
        raise ValueError(f'ABI mismatch: missing={expected-exports}, extra={exports-expected}')
    if a.platform == 'android':
        if not a.android_serial:
            p.error('Cross-built packages require tests on --android-serial')
        adb = [a.adb, '-s', a.android_serial]
        remote = '/data/local/tmp/devkit-sdk-' + package.name
        run(*adb, 'shell', 'mkdir', '-p', remote)
        run(*adb, 'push', binary, remote + '/' + soname)
        tests = json.loads(capture('ctest', '--test-dir', build, '-C', 'Release', '--show-only=json-v1'))['tests']
        if not tests:
            raise ValueError('No tests found')
        for test in tests:
            command = test['command']
            executable = Path(command[0])
            run(*adb, 'push', executable, remote + '/' + executable.name)
            run(*adb, 'shell', 'chmod', '700', remote + '/' + executable.name)
            run(*adb, 'shell', 'cd ' + shlex.quote(remote) + ' && LD_LIBRARY_PATH=. ' +
                shlex.join(['./' + executable.name, *command[1:]]))
        test_record = {'executor': 'adb', 'device': a.android_serial, 'tests': [t['name'] for t in tests]}
    else:
        run('ctest', '--test-dir', build, '-C', 'Release', '--output-on-failure')
        test_record = {'executor': 'ctest', 'result': 'passed'}
    archives = {}
    providers = {'libcrypt': [('libsodium', 'libsodium'), ('openssl', 'libcrypto')],
                 'libnet': [('libuv', 'libuv'), ('ngtcp2', 'libngtcp2'),
                            ('ngtcp2', 'libngtcp2_crypto_ossl'), ('openssl', 'libcrypto'), ('openssl', 'libssl')],
                 'libble': []}[a.component]
    for port, name in providers:
        library = dep / 'lib' / (name + ('.lib' if a.platform == 'windows' else '.a'))
        if not library.exists() and a.platform == 'windows':
            library = dep / 'lib' / (name.removeprefix('lib') + '.lib')
        archives[library.name] = sha(library)
        notices = package / 'share' / a.component / 'licenses'
        notices.mkdir(exist_ok=True)
        shutil.copy2(dep / 'share' / port / 'copyright', notices / (port + '.txt'))
    if sources(root, a.component) != before:
        raise ValueError('Source inputs changed during packaging')
    manifest = {'schemaVersion': 1, 'component': a.component, 'version': (root / 'VERSION').read_text().strip(),
                'abiVersion': 1, 'kind': 'development-preview' if dirty else 'source-clean-build',
                'platform': a.platform, 'architecture': architecture, 'minimumOS': minimum,
                'configuration': 'Release', 'runtime': binary.relative_to(package).as_posix(), 'soname': soname,
                'sourceCommit': commit, 'sourceDirty': dirty, 'sourceFiles': before,
                'staticArchives': archives, 'dynamicDependencies': dependencies, 'exports': sorted(exports),
                'tests': test_record, 'files': inventory(package)}
    (package / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    archive = Path(shutil.make_archive(str(package), 'gztar', root_dir=package.parent, base_dir=package.name))
    lock = {k: manifest[k] for k in ['schemaVersion', 'component', 'version', 'abiVersion', 'kind', 'platform',
                                    'architecture', 'minimumOS', 'sourceCommit', 'sourceDirty']}
    lock.update(repository='git@github.com:skstu/devkit.git', archive=archive.name,
                archiveSha256=sha(archive), manifestSha256=sha(package / 'manifest.json'))
    lock_path = a.output / f'{a.component}.{a.platform}.{architecture}.lock.json'
    lock_path.write_text(json.dumps(lock, indent=2) + '\n')
    print(json.dumps({'sdk': str(package), 'lock': str(lock_path), 'archive': str(archive)}))


if __name__ == '__main__':
    main()
