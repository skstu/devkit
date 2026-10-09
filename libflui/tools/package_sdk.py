#!/usr/bin/env python3
"""Build and package the standalone macOS arm64 SDK, without consumer sources."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def sha256(path):
    with path.open('rb') as source:
        digest = hashlib.sha256()
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--allow-dirty', action='store_true',
                        help='Mark local uncommitted builds explicitly in the manifest')
    args = parser.parse_args()
    component = Path(__file__).resolve().parents[1]
    repository = component.parent
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()

    def git(*command):
        return subprocess.check_output(['git', '-C', str(repository), *command], text=True).strip()

    # These are the complete source inputs of the standalone producer. The optional
    # wxui comparison is not part of the runtime and is not installed as a binary.
    scope = ['libflui', 'VERSION', 'cmake/NativeLibsBuild.cmake']
    dirty = bool(git('status', '--porcelain', '--untracked-files=all', '--', *scope))
    if dirty and not args.allow_dirty:
        parser.error('Commit libflui and its build support first, or use --allow-dirty for a local preview')
    revision = git('rev-parse', 'HEAD')
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if '=' in line and ':' in line and not line.startswith(('#', '//')):
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if Path(cache.get('CMAKE_HOME_DIRECTORY', '')).resolve() != component:
        parser.error('--build-dir must be configured from this checkout\'s standalone libflui directory')
    if cache.get('CMAKE_BUILD_TYPE') != 'Release' or cache.get('LIBFLUI_ARCHS') != 'arm64':
        parser.error('Only the validated Release / arm64 package is supported')
    generated = {'__pycache__', '.dart_tool', 'build'}
    sources = [p for p in component.rglob('*')
               if p.is_file() and not generated.intersection(p.relative_to(component).parts)]
    sources += [repository / name for name in scope[1:]]
    source_hashes = {str(p.relative_to(repository)): sha256(p) for p in sorted(sources)}
    version = (repository / 'VERSION').read_text().strip()
    lock = json.loads((component / 'flutter.lock.json').read_text())
    run('cmake', '--build', str(build), '--config', 'Release', '--target', 'libflui')
    output.mkdir(parents=True, exist_ok=True)
    name = f'libflui-{version}-preview-macos-arm64-{revision[:12]}' + ('-dirty' if dirty else '')
    archive = output / (name + '.tar.gz')
    with tempfile.TemporaryDirectory(prefix='libflui-package-') as temp:
        prefix = Path(temp) / 'libflui-sdk'
        run('cmake', '--install', str(build), '--config', 'Release', '--prefix', str(prefix))
        runtime = prefix / 'lib/libflui_runtime.bundle'
        actual_lock = json.loads((runtime / 'Contents/Resources/runtime.json').read_text())
        if any(actual_lock.get(key) != value for key, value in lock.items()):
            raise RuntimeError('Installed runtime does not match flutter.lock.json')
        required = [prefix / 'share/libflui/LICENSE',
                    runtime / 'Contents/Resources/licenses/Flutter-LICENSE',
                    runtime / 'Contents/Resources/licenses/Flutter-engine-artifacts.md',
                    runtime / 'Contents/Resources/licenses/Flutter-engine-LICENSE']
        if any(not path.is_file() for path in required) or not any(runtime.rglob('NOTICES.Z')):
            raise RuntimeError('Required upstream license artifacts are missing')
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(runtime))
        # A source change during the build must not be attributed to the first digest.
        if source_hashes != {str(p.relative_to(repository)): sha256(p) for p in sorted(sources)}:
            raise RuntimeError('Producer source changed during packaging; retry after it is stable')
        if git('rev-parse', 'HEAD') != revision:
            raise RuntimeError('Source commit changed during packaging')
        files = {}
        for path in sorted(prefix.rglob('*')):
            key = str(path.relative_to(prefix))
            if path.is_symlink():
                path.resolve().relative_to(prefix.resolve())  # reject escaping links
                files[key] = {'symlink': str(path.readlink())}
            elif path.is_file():
                files[key] = {'sha256': sha256(path), 'bytes': path.stat().st_size}
        manifest = {'schema_version': 1, 'sdk': 'libflui', 'version': version,
                    'channel': 'preview', 'abi_version': 1, 'platform': 'macos',
                    'architecture': 'arm64', 'source_commit': revision,
                    'component_dirty': dirty,
                    'repository_dirty': bool(git('status', '--porcelain')),
                    'runtime': actual_lock, 'source_files': source_hashes, 'files': files}
        (prefix / 'sdk-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        temporary_archive = Path(temp) / archive.name
        with tarfile.open(temporary_archive, 'w:gz', dereference=False) as bundle:
            bundle.add(prefix, arcname='libflui-sdk')
        # Copy through a temporary file on the destination volume for atomic replace.
        with tempfile.NamedTemporaryFile(dir=output, prefix='.libflui-', delete=False) as pending:
            pending_path = Path(pending.name)
            try:
                with temporary_archive.open('rb') as source:
                    for block in iter(lambda: source.read(1024 * 1024), b''):
                        pending.write(block)
                pending.flush()
                pending_path.replace(archive)
            finally:
                pending_path.unlink(missing_ok=True)
    checksum = archive.with_name(archive.name + '.sha256')
    checksum.write_text(f'{sha256(archive)}  {archive.name}\n')
    print(f'SDK: {archive}\nChecksum: {checksum}')


if __name__ == '__main__':
    main()
