#!/usr/bin/env python3
"""Produce a verified, self-contained macOS arm64 libnet SDK; no publication."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def run(*args):
    subprocess.run([str(x) for x in args], check=True)

def capture(*args):
    return subprocess.check_output([str(x) for x in args], text=True).strip()

def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def source_files():
    paths = list((ROOT / "libnet").rglob("*"))
    paths += [ROOT / x for x in ("VERSION", "cmake/NativeLibsBuild.cmake", "vcpkg.json", "vcpkg-configuration.json")]
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(paths)
            if p.is_file() and "__pycache__" not in p.parts and not p.name.endswith(".pyc")}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--dependencies", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / ".build/net-packages")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("This packager currently validates native macOS arm64 only.")
    build = args.build.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    def setting(name):
        match = re.search(r"^" + re.escape(name) + r":[^=]+=(.*)$", cache, re.M)
        return match.group(1) if match else ""
    if (setting("CMAKE_BUILD_TYPE") != "Release" or
        setting("CMAKE_OSX_ARCHITECTURES") != "arm64" or
        setting("CMAKE_OSX_DEPLOYMENT_TARGET") != "13.0" or
        setting("CMAKE_HOME_DIRECTORY") != str(ROOT / "libnet/sdk") or
        setting("BUILD_TESTING") != "ON"):
        parser.error("Require a tested standalone Release / arm64 / macOS 13.0 build.")
    # Do not relabel a package when a different installed provider tree was used.
    dep = args.dependencies.resolve()
    if Path(setting("VCPKG_INSTALLED_DIR")).resolve() / setting("VCPKG_TARGET_TRIPLET") != dep:
        parser.error("--dependencies must be the exact provider tree used by CMake.")
    dirty = bool(capture("git", "-C", ROOT, "status", "--porcelain", "--", "libnet", "VERSION", "cmake/NativeLibsBuild.cmake", "vcpkg.json", "vcpkg-configuration.json"))
    if dirty and not args.allow_dirty:
        parser.error("Component has uncommitted work; explicitly use --allow-dirty for a development preview.")
    before = source_files()
    run("cmake", "--build", build, "--parallel", "4")
    run("ctest", "--test-dir", build, "--output-on-failure")
    args.output.mkdir(parents=True, exist_ok=True)
    package = Path(tempfile.mkdtemp(prefix="libnet-macos-arm64-", dir=args.output.resolve()))
    run("cmake", "--install", build, "--prefix", package)
    binary = next(p for p in (package / "lib").glob("libdevkit_net.*.dylib") if not p.is_symlink())
    expected = set(re.findall(r"DKNET_CALL\s+(dknet_\w+)\(", (package / "include/libnet/net.h").read_text()))
    exports = {line.split()[-1].removeprefix("_") for line in capture("nm", "-gU", binary).splitlines()}
    if exports != expected:
        raise SystemExit(f"Unexpected ABI exports: extra={exports-expected} missing={expected-exports}")
    dependencies = [line.strip().split(" (")[0] for line in capture("otool", "-L", binary).splitlines()[1:]]
    allowed = {"@rpath/libdevkit_net.1.dylib", "/usr/lib/libSystem.B.dylib", "/usr/lib/libc++.1.dylib"}
    if set(dependencies) != allowed or capture("lipo", "-archs", binary) != "arm64":
        raise SystemExit("Unexpected dynamic dependency or architecture: " + repr(dependencies))
    library = ctypes.CDLL(str(binary))
    library.dknet_backend_versions.restype = ctypes.c_char_p
    providers = library.dknet_backend_versions().decode()
    archives = {}
    licenses = package / "share/libnet/licenses"
    licenses.mkdir()
    for port, archive in (("libuv", "libuv.a"), ("ngtcp2", "libngtcp2.a"), ("ngtcp2", "libngtcp2_crypto_ossl.a"), ("openssl", "libcrypto.a"), ("openssl", "libssl.a")):
        archives[archive] = digest(dep / "lib" / archive)
        shutil.copy2(dep / "share" / port / "copyright", licenses / (port + ".txt"))
    after = source_files()
    if before != after:
        raise SystemExit("Source inputs changed during packaging; retry after edits stop.")
    payload = {}
    for path in sorted(package.rglob("*")):
        key = path.relative_to(package).as_posix()
        if path.is_symlink():
            payload[key] = {"symlink": str(path.readlink())}
        elif path.is_file():
            payload[key] = {"sha256": digest(path), "size": path.stat().st_size}
    manifest = {
        "schemaVersion": 1, "component": "libnet", "version": (ROOT / "VERSION").read_text().strip(),
        "abiVersion": 1, "kind": "development-preview" if dirty else "source-clean-build",
        "platform": "macos", "architecture": "arm64", "minimumOS": "13.0",
        "configuration": "Release", "runtime": binary.relative_to(package).as_posix(),
        "soname": "libdevkit_net.1.dylib", "backendVersions": providers,
        "sourceCommit": capture("git", "-C", ROOT, "rev-parse", "HEAD"),
        "sourceDirty": dirty, "sourceFiles": before,
        "staticArchives": archives, "dynamicDependencies": dependencies,
        "exports": sorted(exports), "files": payload,
    }
    (package / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    archive = Path(shutil.make_archive(str(package), "gztar", root_dir=package))
    archive.with_name(archive.name + ".sha256").write_text(digest(archive) + "  " + archive.name + "\n")
    print(json.dumps({"package": str(package), "archive": str(archive),
                      "sha256": digest(archive), "manifestSha256": digest(package / "manifest.json")}, indent=2))

if __name__ == "__main__":
    main()
