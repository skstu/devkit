#!/usr/bin/env python3
"""Package an already-built libsys device-info component; local previews only."""
import argparse,hashlib,json,subprocess,tarfile,tempfile
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--platform',choices=['macos','ios','android','windows','linux'],required=True)
 p.add_argument('--provenance',type=Path);p.add_argument('--minimum-os',required=True);a=p.parse_args()
 root=Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True)
 cache={}
 for line in (a.build/'CMakeCache.txt').read_text().splitlines():
  if '=' in line and ':' in line and not line.startswith(('#','//')):
   key,value=line.split('=',1);cache[key.split(':',1)[0]]=value
 if Path(cache['devkitDevice_SOURCE_DIR']).resolve()!=root/'libsys/device-info':raise SystemExit('Unexpected source build')
 if a.platform in ('macos','ios') and cache.get('CMAKE_OSX_ARCHITECTURES')!='arm64':raise SystemExit('Expected arm64 Apple package')
 if a.platform in ('macos','ios') and cache.get('CMAKE_OSX_DEPLOYMENT_TARGET')!=a.minimum_os:raise SystemExit('Minimum OS mismatch')
 if a.platform=='android' and (cache.get('CMAKE_ANDROID_ARCH_ABI',cache.get('ANDROID_ABI'))!='arm64-v8a' or cache.get('ANDROID_PLATFORM')!='android-'+a.minimum_os):raise SystemExit('Android target mismatch')
 if a.platform=='windows' and cache.get('CMAKE_GENERATOR_PLATFORM')!='x64':raise SystemExit('Expected x64 Windows package')
 stage=Path(tempfile.mkdtemp(prefix='libsys-device-info-',dir=a.output.resolve()))
 subprocess.run(['cmake','--install',str(a.build),'--config','Release','--prefix',str(stage)],check=True)
 # Preserve the producer license and exact implementation provenance.
 libraries=list((stage/'lib').glob('*device_info*'))
 if not libraries:raise SystemExit('Missing built device-info library')
 if min(x.stat().st_mtime for x in libraries)<max((root/n).stat().st_mtime for n in ('libsys/src/device_info.cc','libsys/src/device_form.hpp','libsys/src/device_text.hpp')):raise SystemExit('Stale producer build')
 license=root/'LICENSE';(stage/'LICENSE').write_bytes(license.read_bytes())
 names=['libsys/include/libsys/device_info.h','libsys/src/device_info.cc','libsys/src/device_form.hpp','libsys/src/device_text.hpp','libsys/device-info/CMakeLists.txt','libsys/tests/device_info_test.cpp','tools/package_device_info_sdk.py','VERSION']
 sources={n:{'sha256':sha(root/n),'size':(root/n).stat().st_size} for n in names}
 if a.provenance:
  provenance=json.loads(a.provenance.read_text())
  if sources!=provenance['sourceFiles']:raise SystemExit('Source provenance mismatch')
  commit=provenance['sourceCommit'];dirty=provenance['sourceDirty']
 else:
  commit=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
  dirty=bool(subprocess.check_output(['git','-C',str(root),'status','--porcelain','--',*names],text=True).strip())
 files={x.relative_to(stage).as_posix():{'sha256':sha(x),'size':x.stat().st_size} for x in sorted(stage.rglob('*')) if x.is_file()}
 m={'schemaVersion':1,'component':'libsys-device-info','version':(root/'VERSION').read_text().strip(),'abiVersion':1,'kind':'development-preview','platform':a.platform,'architecture':'arm64' if a.platform in ('macos','ios','android') else 'x86_64','minimumOS':a.minimum_os,'sourceCommit':commit,'sourceDirty':dirty,'exports':['dksys_device_detect'],'sourceFiles':sources,'files':files}
 (stage/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
 archive=a.output/(stage.name+'-'+a.platform+'.tar.gz')
 with tarfile.open(archive,'w:gz') as t:t.add(stage,arcname=stage.name)
 lock={k:m[k] for k in ('schemaVersion','component','version','abiVersion','kind','platform','architecture','minimumOS','sourceCommit','sourceDirty')}
 lock.update(repository='git@github.com:skstu/devkit.git',archive=archive.name,archiveSha256=sha(archive),manifestSha256=sha(stage/'manifest.json'))
 (a.output/(a.platform+'.lock.json')).write_text(json.dumps(lock,indent=2)+'\n')
 (a.output/(a.platform+'.package.json')).write_text(json.dumps({'sdk':str(stage),'archive':str(archive)},indent=2)+'\n')
 print('SDK:',stage)
if __name__=='__main__':main()
