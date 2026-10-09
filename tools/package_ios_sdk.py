#!/usr/bin/env python3
"""Package a built arm64 iOS libcrypt/libnet/libble SDK as an unsigned development framework.
The consumer signs and embeds it. This tool never labels cross-compilation as device testing.
"""
import argparse, hashlib, json, re, shutil, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def capture(*args):
    return subprocess.check_output([str(x) for x in args], text=True).strip()
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('component',choices=['libcrypt','libnet','libble'])
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--dependencies',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(); build=a.build.resolve(); dep=a.dependencies.resolve() if a.dependencies else None
    cache=(build/'CMakeCache.txt').read_text()
    def setting(key):
        m=re.search(r'^'+re.escape(key)+r':[^=]+=(.*)$',cache,re.M)
        return m[1] if m else ''
    source=ROOT/a.component/('sdk' if a.component=='libnet' else '')
    if any(setting(k)!=v for k,v in {'CMAKE_HOME_DIRECTORY':str(source),'CMAKE_BUILD_TYPE':'Release','CMAKE_OSX_ARCHITECTURES':'arm64','CMAKE_OSX_DEPLOYMENT_TARGET':'15.0'}.items()):
        p.error('Require standalone Release / arm64 / iOS 15.0 build')
    if a.component!='libble' and (not dep or Path(setting('VCPKG_INSTALLED_DIR'))/setting('VCPKG_TARGET_TRIPLET')!=dep):
        p.error('Provider directory must match the configured build')
    inputs=list((ROOT/a.component).rglob('*'))+[ROOT/'VERSION',ROOT/'cmake/NativeLibsBuild.cmake',Path(__file__).resolve()]
    files={str(f.relative_to(ROOT)):digest(f) for f in inputs if f.is_file() and '__pycache__' not in f.parts}
    subprocess.run(['cmake','--build',str(build),'--parallel','4'],check=True)
    a.output.mkdir(parents=True,exist_ok=True)
    dest=Path(tempfile.mkdtemp(prefix=a.component+'-ios-arm64-',dir=a.output.resolve()))
    subprocess.run(['cmake','--install',str(build),'--prefix',str(dest)],check=True)
    name={'libcrypt':'DevkitCrypt','libnet':'DevkitNet','libble':'DevkitBle'}[a.component]
    binary=dest/'lib'/(name+'.framework')/name
    metadata=capture('xcrun','vtool','-show-build',binary)
    if not re.search(r'platform\s+IOS\b',metadata) or not re.search(r'minos\s+15\.0\b',metadata) or capture('lipo','-archs',binary)!='arm64':
        raise SystemExit('Wrong Mach-O platform, minimum OS or architecture')
    short=a.component.removeprefix('lib')
    expected=set(re.findall(r'DK'+short.upper()+r'_CALL\s+(dk'+short+r'_\w+)\(', (dest/'include'/a.component/(short+'.h')).read_text()))
    exports={s.split()[-1].removeprefix('_') for s in capture('nm','-gU',binary).splitlines()}
    if exports!=expected:raise SystemExit('SDK public export mismatch')
    dependencies=[x.strip().split(' (')[0] for x in capture('otool','-L',binary).splitlines()[1:]]
    soname='@rpath/'+name+'.framework/'+name
    if dependencies[0]!=soname or any(not x.startswith(('/usr/lib/','/System/Library/')) for x in dependencies[1:]):
        raise SystemExit('Unexpected runtime dependencies: '+repr(dependencies))
    ports={'libsodium':'libsodium.a','openssl':'libcrypto.a'} if short=='crypt' else {'openssl':'libssl.a','libuv':'libuv.a','ngtcp2':'libngtcp2.a'}
    if short=='ble': ports={}
    archives={}; licenses=dest/'share'/a.component/'licenses';licenses.mkdir(exist_ok=True,parents=True)
    for port,archive in ports.items():
        archives[archive]=digest(dep/'lib'/archive)
        shutil.copy2(dep/'share'/port/'copyright',licenses/(port+'.txt'))
    if short=='net':
        for archive in ['libcrypto.a','libngtcp2_crypto_ossl.a']:archives[archive]=digest(dep/'lib'/archive)
    if any(digest(ROOT/f)!=h for f,h in files.items()):raise SystemExit('Source changed during packaging')
    payload={}
    for f in sorted(dest.rglob('*')):
        if f.is_symlink():payload[str(f.relative_to(dest))]={'symlink':str(f.readlink())}
        elif f.is_file():payload[str(f.relative_to(dest))]={'sha256':digest(f),'size':f.stat().st_size}
    dirty=bool(capture('git','-C',ROOT,'status','--porcelain','--',a.component,'cmake/NativeLibsBuild.cmake','VERSION','tools/package_ios_sdk.py'))
    manifest={'schemaVersion':1,'component':a.component,'version':(ROOT/'VERSION').read_text().strip(),'abiVersion':1,
      'kind':'development-preview','platform':'ios','architecture':'arm64','minimumOS':'15.0','configuration':'Release',
      'runtime':str(binary.relative_to(dest)),'soname':soname,'sourceCommit':capture('git','-C',ROOT,'rev-parse','HEAD'),
      'sourceDirty':dirty,'sourceFiles':files,'staticArchives':archives,'dynamicDependencies':dependencies,
      'exports':sorted(exports),'files':payload,'validation':'Cross-compiled and inspected; device execution must be verified by the consumer.'}
    (dest/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    archive=Path(shutil.make_archive(str(dest),'gztar',root_dir=dest))
    receipt={k:manifest[k] for k in ['schemaVersion','component','version','abiVersion','kind','platform','architecture','minimumOS','sourceCommit','sourceDirty']}
    receipt.update(repository='git@github.com:skstu/devkit.git',archive=archive.name,archiveSha256=digest(archive),manifestSha256=digest(dest/'manifest.json'))
    (dest.parent/(dest.name+'.lock.json')).write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'sdk':str(dest),'lock':str(dest.parent/(dest.name+'.lock.json'))}))
if __name__=='__main__':main()
