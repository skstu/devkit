#!/usr/bin/env python3
"""Package a tested libnet ICE/QUIC static slice for an exact consumer build."""
import argparse, hashlib, json, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--platform',required=True,choices=['macos','android'])
    a=p.parse_args(); build=a.build.resolve(); out=a.output.resolve()
    if out.exists(): raise ValueError('Use a fresh package directory')
    cache=(build/'CMakeCache.txt').read_text()
    if f'CMAKE_HOME_DIRECTORY:INTERNAL={ROOT / "libnet"}' not in cache or 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:
        raise ValueError('Require the standalone Release libnet build')
    inputs={str(f.relative_to(ROOT)):digest(f) for f in sorted((ROOT/'libnet').rglob('*')) if f.is_file()}
    inputs['tools/package_wan_net_sdk.py']=digest(Path(__file__))
    subprocess.run(['cmake','--build',str(build),'-j','4'],check=True)
    if a.platform=='macos': subprocess.run(['ctest','--test-dir',str(build),'--output-on-failure'],check=True)
    out.mkdir(parents=True)
    (out/'lib').mkdir(); (out/'include').mkdir()
    for name in ['libnet_transport.a','liblibnet_uv.a']:
        shutil.copy2(build/name,out/'lib'/name)
    for f in (ROOT/'libnet/include').glob('*.h'): shutil.copy2(f,out/'include'/f.name)
    (out/'share/libnet-wan').mkdir(parents=True)
    shutil.copy2(ROOT/'libnet/LICENSE',out/'share/libnet-wan/LICENSE')
    files={str(f.relative_to(out)):digest(f) for f in sorted(out.rglob('*')) if f.is_file()}
    for relative,sha in inputs.items():
        if digest(ROOT/relative)!=sha: raise ValueError('Source changed during packaging: '+relative)
    manifest={'version':1,'component':'libnet-wan','stunPoolVersion':2,'platform':a.platform,
        'architecture':'arm64','sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'developmentPreview':bool(subprocess.check_output(['git','status','--porcelain','--','libnet','tools/package_wan_net_sdk.py'],cwd=ROOT)),
        'sourceSha256':inputs,'files':files}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'sdk':str(out),'manifestSha256':digest(out/'manifest.json')}))
if __name__=='__main__': main()
