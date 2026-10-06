#!/usr/bin/env python3
"""Explicit opt-in fetch; CMake never downloads dependencies implicitly."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--destination', type=Path, required=True)
args = parser.parse_args()
args.destination.mkdir(parents=True, exist_ok=True)
for name, version, digest in json.loads(Path(__file__).with_name('dependencies.json').read_text()):
    archive = args.destination / f'{name}-{version}.tar.gz'
    if not archive.is_file():
        request = urllib.request.Request(f'https://codeload.github.com/uNetworking/{name}/tar.gz/refs/tags/v{version}',
                                         headers={'User-Agent': 'Bridge-dependency-fetch'})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024 or hashlib.sha512(data).hexdigest() != digest:
            raise SystemExit('Dependency archive verification failed')
        archive.write_bytes(data)
    if hashlib.sha512(archive.read_bytes()).hexdigest() != digest:
        raise SystemExit('Dependency archive verification failed')
    target = args.destination / f'{name}-{version}'
    if target.exists():
        raise SystemExit('Source destination already exists; use a fresh destination to preserve local changes')
    with tarfile.open(archive) as source:
        # Works on the board's Python 3.11 without relying on newer tar filters.
        for member in source.getmembers():
            path = Path(member.name)
            if path.is_absolute() or '..' in path.parts or not (member.isfile() or member.isdir()):
                raise SystemExit('Unexpected archive member')
        source.extractall(args.destination)
    print(name, version, 'SHA512 verified')
