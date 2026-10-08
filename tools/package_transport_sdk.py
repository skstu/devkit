#!/usr/bin/env python3
"""Package the locally validated libble/libice slice; never publish or sign it."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_SHA512 = "770b7123949a644ab8df01020abb2c8744496e1e486c91292252bb309a8c38239ae2a6c369b4eb0c028f6ac0ea78e5da09922d0ddf8446e50b4eb299cf1dabca"

def digest(path, algorithm="sha256"):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()

def capture(*args, cwd=None):
    return subprocess.check_output([str(x) for x in args], text=True, cwd=cwd).strip()

def run(*args, cwd=None):
    subprocess.run([str(x) for x in args], check=True, cwd=cwd)

def inputs(component):
    paths = list((ROOT / component).rglob("*"))
    paths += [ROOT / x for x in ("VERSION", "cmake/NativeLibsBuild.cmake", "tools/package_transport_sdk.py")]
    if component == "libice":
        paths += list((ROOT / "cmake/vcpkg/ports/libjuice").rglob("*"))
        paths += [ROOT / "libnet" / x for x in (
            "src/libnet_ice.cc", "src/libnet_uv.cc", "src/libnet_network_path.cc",
            "include/libnet_ice.h", "include/libnet_uv.h", "include/libnet_network_path.h")]
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(set(paths))
            if p.is_file() and "__pycache__" not in p.parts and not p.name.endswith(".pyc")}

def juice_sources(package, archive):
    if digest(archive, "sha512") != UPSTREAM_SHA512:
        raise ValueError("The upstream libjuice archive does not match the locked overlay SHA-512")
    overlay = ROOT / "cmake/vcpkg/ports/libjuice"
    destination = package / "share/libice/sources"
    destination.mkdir(parents=True)
    with tarfile.open(archive) as tar:
        tar.extractall(destination, filter="data")
    source = destination / "libjuice-1.7.2"
    if not source.is_dir():
        raise ValueError("Unexpected upstream archive layout")
    port = (overlay / "portfile.cmake").read_text()
    patches = re.findall(r"^\s+([\w.-]+\.diff)\s*$", port, re.M)
    for patch in patches:
        # Match vcpkg z_vcpkg_apply_patches exactly; BSD patch handles these
        # deliberately short-context overlay hunks differently.
        capture("git", "-c", "core.longpaths=true", "-c", "core.autocrlf=false",
                "-c", "core.filemode=true", "--work-tree=.", "--git-dir=.git",
                "apply", overlay / patch, "--ignore-whitespace", "--whitespace=nowarn", cwd=source)
    for name in ("sovkit_mapping.inc", "sovkit_filtering.inc", "sovkit_pcp.inc"):
        shutil.copy2(overlay / name, source / "src" / name)
    # Applying patches must not leave rejection or alternate source files.
    if any(source.rglob("*.rej")):
        raise ValueError("Provider patch application failed")
    for backup in source.rglob("*.orig"):
        backup.unlink()
    shutil.copytree(overlay, destination / "libjuice-overlay")
    (destination / "SOURCE.txt").write_text(
        "libjuice v1.7.2, devkit overlay revision 13\n"
        "https://github.com/paullouisageneau/libjuice/tree/v1.7.2\n"
        "Upstream archive SHA-512: " + UPSTREAM_SHA512 + "\n"
        "libjuice-1.7.2 contains the corresponding source with every port patch applied.\n"
        "Private static providers retain their respective licenses.\n")

def check_consumer(package, component):
    """Use only installed public files; verify the relocated runtime was loaded."""
    short = component.removeprefix("lib")
    soname = f"libdevkit_{short}.1.dylib"
    with tempfile.TemporaryDirectory(prefix="consumer-", dir=package.parent) as temporary:
        work = Path(temporary)
        build, relocated = work / "build", work / "relocated"
        capture("cmake", "-S", package / f"share/{component}/examples/consumer", "-B", build,
                "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64",
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0", "-DCMAKE_PREFIX_PATH=" + str(package))
        capture("cmake", "--build", build)
        relocated.mkdir()
        executable = short + "_consumer"
        shutil.copy2(build / executable, relocated / executable)
        shutil.copy2(build / soname, relocated / soname)
        result = subprocess.run([str(relocated / executable)], cwd=relocated,
                                env={"PATH":"/usr/bin:/bin", "DYLD_PRINT_LIBRARIES":"1"},
                                capture_output=True, text=True, check=True)
        expected = str((relocated / soname).resolve())
        loaded = [line.split()[-1] for line in result.stderr.splitlines() if soname in line]
        if len(loaded) != 1 or str(Path(loaded[0]).resolve()) != expected:
            raise ValueError("Consumer did not load the relocated SDK runtime: " + repr(loaded))
        return {"language":"C", "installedFilesOnly":True, "relocated":True,
                "environmentCleared":True, "runtimeLoadedBesideExecutable":True,
                "output":result.stdout.strip()}

def main(component):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / ".build/transport-packages")
    parser.add_argument("--allow-dirty", action="store_true")
    if component == "libice":
        parser.add_argument("--juice-source", required=True, type=Path,
                            help="Original v1.7.2 tar archive matching the pinned port SHA-512")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("Current packaging validation covers native macOS arm64 only")
    build = args.build.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    def setting(name):
        match = re.search(r"^" + re.escape(name) + r":[^=]+=(.*)$", cache, re.M)
        return match.group(1) if match else ""
    expected = {"CMAKE_BUILD_TYPE":"Release", "CMAKE_OSX_ARCHITECTURES":"arm64",
                "CMAKE_OSX_DEPLOYMENT_TARGET":"13.0", "BUILD_TESTING":"ON",
                "CMAKE_HOME_DIRECTORY":str(ROOT / component)}
    if any(setting(k) != v for k,v in expected.items()):
        parser.error("Require a tested standalone Release / arm64 / macOS 13.0 build")
    before = inputs(component)
    changed = capture("git", "-C", ROOT, "status", "--porcelain", "--", *before.keys())
    dirty = bool(changed)
    if dirty and not args.allow_dirty:
        parser.error("Uncommitted component inputs require --allow-dirty for a development preview")
    providers = {}
    archive_hashes = {}
    if component == "libice":
        providers = {k:Path(v) for k,v in json.loads((build / "provider-inputs.json").read_text()).items()}
        for name,path in providers.items():
            if path.suffix != ".a":
                raise ValueError("Expected static provider: " + name)
            archive_hashes[name] = digest(path)
        spdx = json.loads((providers["libjuice"].parent.parent / "share/libjuice/vcpkg.spdx.json").read_text())
        if "@1.7.2#13 " not in spdx["name"]:
            raise ValueError("Wrong libjuice port revision")
        for record in spdx["files"]:
            if record["SPDXID"].startswith("SPDXRef-port-file-"):
                path = ROOT / "cmake/vcpkg/ports/libjuice" / record["fileName"].removeprefix("./")
                sha = next(x["checksumValue"] for x in record["checksums"] if x["algorithm"] == "SHA256")
                if digest(path) != sha:
                    raise ValueError("Installed libjuice was built from a different overlay: " + path.name)
    run("cmake", "--build", build, "--parallel", "4")
    run("ctest", "--test-dir", build, "--output-on-failure")
    args.output.mkdir(parents=True, exist_ok=True)
    package = Path(tempfile.mkdtemp(prefix=component + "-macos-arm64-", dir=args.output.resolve()))
    run("cmake", "--install", build, "--prefix", package)
    short = component.removeprefix("lib")
    binary = next(p for p in (package / "lib").glob(f"libdevkit_{short}.*.dylib") if not p.is_symlink())
    header = package / f"include/{component}/{short}.h"
    exports = {line.split()[-1].removeprefix("_") for line in capture("nm", "-gU", binary).splitlines()}
    wanted = set(re.findall(r"DK" + short.upper() + r"_CALL\s+(dk" + short + r"_\w+)\(", header.read_text()))
    if exports != wanted:
        raise ValueError(f"ABI exports differ: extra={exports-wanted}, missing={wanted-exports}")
    dependencies = [line.strip().split(" (")[0] for line in capture("otool", "-L", binary).splitlines()[1:]]
    soname = f"libdevkit_{short}.1.dylib"
    if any(not (s == "@rpath/" + soname or s.startswith(("/usr/lib/", "/System/Library/Frameworks/"))) for s in dependencies):
        raise ValueError("Unexpected non-system runtime dependency: " + repr(dependencies))
    if capture("lipo", "-archs", binary) != "arm64" or not re.search(r"minos\s+13\.0(?:\s|$)", capture("xcrun", "vtool", "-show-build", binary)):
        raise ValueError("Wrong binary architecture or minimum OS")
    notices = package / "share" / component / "licenses"
    notices.mkdir()
    for port,path in providers.items():
        share = path.parent.parent / "share" / port
        shutil.copy2(share / "copyright", notices / (port + ".txt"))
        shutil.copy2(share / "vcpkg.spdx.json", notices / (port + ".spdx.json"))
    if component == "libice":
        juice_sources(package, args.juice_source)
    consumer = check_consumer(package, component)
    if before != inputs(component) or any(digest(p) != archive_hashes[k] for k,p in providers.items()):
        raise ValueError("Build inputs changed while packaging")
    files = {}
    for path in sorted(package.rglob("*")):
        name = path.relative_to(package).as_posix()
        if path.is_symlink(): files[name] = {"symlink":str(path.readlink())}
        elif path.is_file(): files[name] = {"sha256":digest(path), "size":path.stat().st_size}
    manifest = {"schemaVersion":1, "component":component, "version":(ROOT/"VERSION").read_text().strip(),
                "abiVersion":1, "kind":"development-preview" if dirty else "source-clean-build",
                "platform":"macos", "architecture":"arm64", "minimumOS":"13.0", "configuration":"Release",
                "runtime":binary.relative_to(package).as_posix(), "soname":soname,
                "sourceCommit":capture("git","-C",ROOT,"rev-parse","HEAD"), "sourceDirty":dirty,
                "sourceFiles":before, "staticArchives":archive_hashes, "dynamicDependencies":dependencies,
                "exports":sorted(exports), "consumerCheck":consumer, "files":files}
    (package/"manifest.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")
    archive = Path(shutil.make_archive(str(package),"gztar",root_dir=package))
    archive.with_name(archive.name+".sha256").write_text(digest(archive)+"  "+archive.name+"\n")
    print(json.dumps({"package":str(package), "archive":str(archive), "sha256":digest(archive),
                      "manifestSha256":digest(package/"manifest.json")},indent=2))
