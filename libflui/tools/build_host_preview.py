#!/usr/bin/env python3
"""Producer-only platform preview. Consumer source remains C++/XML and links an installed binary SDK.
This development harness is not yet a distributable multi-platform SDK release."""
import argparse, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--platform',choices=['android','ios','windows','linux'],required=True)
p.add_argument('--flutter-sdk',type=Path,required=True)
p.add_argument('--client',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
p.add_argument('--android-ndk',type=Path)
p.add_argument('--bundle-id',required=True)
p.add_argument('--development-host', action='store_true', help='Debuggable owned-device test host; never a normal distribution build')
p.add_argument('--build-number', type=int, default=1, help='Monotonically increasing consumer package build number')
p.add_argument('--display-name', default='Libflui Preview')
p.add_argument('--chinese-display-name', help='Optional consumer name for Chinese system languages')
p.add_argument('--logo-dir', type=Path, help='Consumer-generated platform icon directory')
p.add_argument('--client-cmake-arg', action='append', default=[])
p.add_argument('--ios-info-plist', type=Path, help='Consumer privacy/background declarations, merged into SDK-owned host')
p.add_argument('--embed-framework', action='append', type=Path, default=[])
p.add_argument('--runtime-dir', action='append', type=Path, default=[], help='Consumer SDK runtime directories; copied into the host bundle')
p.add_argument('--android-sources', action='append', type=Path, default=[], help='Consumer/platform Kotlin or Java source root')
p.add_argument('--android-manifest', type=Path, help='Consumer uses-permission and application privacy declarations')
p.add_argument('--android-initializer', help='Consumer platform initialization class with initialize(Context)')
a=p.parse_args()
component=Path(__file__).resolve().parents[1]
repo=component.parent
work=a.work.resolve(); work.mkdir(parents=True,exist_ok=True)
if a.platform=='linux':
    import platform
    release=dict(line.split('=',1) for line in Path('/etc/os-release').read_text().splitlines() if '=' in line)
    if release.get('ID','').strip('"')!='ubuntu' or release.get('VERSION_ID','').strip('"')!='22.04':
        p.error('Linux preview must be built in the Ubuntu 22.04 baseline environment')
    if platform.machine()!='x86_64':p.error('This Linux preview currently validates x86_64 only')
flutter=a.flutter_sdk.resolve()/('bin/flutter.bat' if a.platform=='windows' else 'bin/flutter')
lock=json.loads((component/'flutter.lock.json').read_text())
version=json.loads((a.flutter_sdk/'bin/cache/flutter.version.json').read_text())
for key in ['frameworkRevision','engineRevision','dartSdkVersion']:
    if version[key]!=lock[key]:p.error('Flutter does not match flutter.lock.json: '+key)
env=dict(os.environ,CI='true',FLUTTER_SUPPRESS_ANALYTICS='true')
def run(args,cwd=None):
    subprocess.run([str(x) for x in args],cwd=cwd,env=env,check=True)
def copy_runtimes(destination):
    destination.mkdir(parents=True, exist_ok=True)
    for directory in a.runtime_dir:
        if not directory.is_dir():p.error('Runtime directory does not exist: '+str(directory))
        for source in directory.iterdir():
            if source.is_file() and (source.name.endswith('.dll') or '.so' in source.name):
                shutil.copy2(source, destination/source.name)
host=work/'host'
if not (host/'pubspec.yaml').exists():
    run([flutter,'--suppress-analytics','--no-version-check','create','--no-pub','--platforms='+a.platform,
         '--project-name','libflui_host','--org','com.skstu.libflui',host])
for name in ['pubspec.yaml','pubspec.lock']:shutil.copy2(component/'renderer'/name,host/name)
shutil.copytree(component/'renderer/lib',host/'lib',dirs_exist_ok=True)
run([flutter,'--suppress-analytics','--no-version-check','pub','get','--offline','--enforce-lockfile'],host)
native=work/'native'; prefix=work/'sdk'
flags=['-DCMAKE_BUILD_TYPE=Release']
if a.platform=='android':
    if not a.android_ndk:p.error('--android-ndk required')
    flags+=['-DCMAKE_TOOLCHAIN_FILE='+str(a.android_ndk/'build/cmake/android.toolchain.cmake'),
            '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-24','-DANDROID_STL=c++_static']
elif a.platform=='linux':
    flags+=['-DCMAKE_SHARED_LINKER_FLAGS=-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL',
            '-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON','-DCMAKE_INSTALL_RPATH=$ORIGIN']
elif a.platform=='ios':
    flags+=['-DCMAKE_SYSTEM_NAME=iOS','-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0',
            '-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO']
run(['cmake','-S',component,'-B',native,'-DLIBFLUI_PORTABLE_HOST=ON','-DBUILD_TESTING=OFF',*flags])
run(['cmake','--build',native,'--config','Release','--parallel','4'])
run(['cmake','--install',native,'--config','Release','--prefix',prefix])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if (repo/'source-provenance.json').exists():
    provenance=json.loads((repo/'source-provenance.json').read_text())
    for name,digest in provenance['source_files'].items():
        if sha(repo/name)!=digest:raise SystemExit('Staged source changed: '+name)
    commit=provenance['source_commit']
else:
    commit=subprocess.check_output(['git','-C',repo,'rev-parse','HEAD'],text=True).strip()
manifest={'schema_version':1,'sdk':'libflui','channel':'unreleased-device-preview','source_commit':commit,
    'component_dirty':True,'platform':a.platform,'architecture':('x86_64' if a.platform in ('windows','linux') else 'arm64'),'runtime':lock,
    'source_files':{str(p.relative_to(repo)):sha(p) for p in component.rglob('*') if p.is_file() and not {'__pycache__','.dart_tool','build'}.intersection(p.parts)},
    'files':{str(p.relative_to(prefix)):sha(p) for p in prefix.rglob('*') if p.is_file() and p.name!='sdk-manifest.json'}}
(prefix/'sdk-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
sdklock=work/'native.lock.json'
sdklock.write_text(json.dumps({'source_commit':commit,'component_dirty':True,'manifest_sha256':sha(prefix/'sdk-manifest.json')},indent=2)+'\n')
clientbuild=work/'client'
run(['cmake','-S',a.client.resolve(),'-B',clientbuild,'-DZHIYU_PORTABLE_HOST=ON',
     '-DZHIYU_SDK_LOCK='+str(sdklock),'-Dlibflui_DIR='+str(prefix/'lib/cmake/libflui'),*flags,*a.client_cmake_arg])
run(['cmake','--build',clientbuild,'--config','Release','--parallel','4'])
if a.platform=='android':
    jni=host/'android/app/src/main/jniLibs/arm64-v8a';jni.mkdir(parents=True,exist_ok=True)
    shutil.copy2(prefix/'lib/libflui.so',jni/'libflui.so')
    shutil.copy2(clientbuild/'libflui_client.so',jni/'libflui_client.so')
    copy_runtimes(jni)
    for directory in a.android_sources:
        shutil.copytree(directory,host/'android/app/src/main/java',dirs_exist_ok=True)
    if a.android_initializer:
        import re
        if not re.fullmatch(r'[A-Za-z_][\w]*(?:\.[A-Za-z_][\w]*)+',a.android_initializer):p.error('Invalid Android initializer class')
        activities=list((host/'android/app/src/main/kotlin').rglob('MainActivity.kt'))
        if len(activities)!=1:p.error('Expected one SDK-owned Android activity')
        activity=activities[0]
        package=re.search(r'^package ([\w.]+)',activity.read_text(),re.M).group(1)
        activity.write_text('package '+package+'\nimport android.os.Bundle\nimport io.flutter.embedding.android.FlutterActivity\n'
            'class MainActivity: FlutterActivity() {\n override fun onCreate(savedInstanceState: Bundle?) {\n  '
            +a.android_initializer+'.initialize(this)\n  super.onCreate(savedInstanceState)\n }\n'
            +' override fun onRequestPermissionsResult(code: Int, permissions: Array<out String>, results: IntArray) {\n  super.onRequestPermissionsResult(code, permissions, results)\n  '
            +a.android_initializer+'.permissionResult(code, permissions, results)\n }\n}\n')
    gradle=host/'android/app/build.gradle.kts';s=gradle.read_text()
    import re
    s=re.sub(r'applicationId = "[^"]+"','applicationId = "'+a.bundle_id+'"',s)
    s=s.replace('minSdk = flutter.minSdkVersion','minSdk = 24')
    if a.development_host and not a.bundle_id.endswith('.test'):
        p.error('Debuggable host requires a test application identity')
    s=s.replace('            isDebuggable = true\n','')
    gradle.write_text(s)
    manifestxml=host/'android/app/src/main/AndroidManifest.xml'
    if a.android_manifest:
        import xml.etree.ElementTree as ET
        ET.register_namespace('android','http://schemas.android.com/apk/res/android')
        tree=ET.parse(manifestxml); consumer=ET.parse(a.android_manifest).getroot(); node=tree.getroot()
        android='{http://schemas.android.com/apk/res/android}'
        for permission in consumer:
            if permission.tag=='uses-permission':
                for old in list(node.findall('uses-permission')):
                    if old.get(android+'name')==permission.get(android+'name'):node.remove(old)
                node.insert(0,permission)
            elif permission.tag=='application':
                allowed={android+'allowBackup',android+'usesCleartextTraffic'}
                if not set(permission.attrib)<=allowed or len(permission):p.error('Unsupported Android application overlay')
                node.find('application').attrib.update(permission.attrib)
            else:p.error('Only permissions and application privacy flags are accepted')
        tree.write(manifestxml,encoding='unicode')
    from xml.sax.saxutils import escape
    manifestxml.write_text(manifestxml.read_text().replace('android:label="libflui_host"','android:label="@string/app_name"'),encoding='utf-8')
    names={'values':a.display_name}
    if a.chinese_display_name:names['values-zh']=a.chinese_display_name
    for folder,name in names.items():
        resources=host/'android/app/src/main/res'/folder;resources.mkdir(parents=True,exist_ok=True)
        (resources/'app_name.xml').write_text('<resources><string name="app_name">'+escape(name)+'</string></resources>\n',encoding='utf-8')
    if a.logo_dir:
        for folder in (a.logo_dir/'android').iterdir():
            if folder.is_dir():shutil.copytree(folder,host/'android/app/src/main/res'/folder.name,dirs_exist_ok=True)
    run([flutter,'--suppress-analytics','--no-version-check','build','apk','--debug' if a.development_host else '--release','--build-number',str(a.build_number),'--target-platform','android-arm64','--no-pub'],host)
    print('APK:',host/'build/app/outputs/flutter-apk'/('app-debug.apk' if a.development_host else 'app-release.apk'))
elif a.platform=='linux':
    runner=host/'linux/CMakeLists.txt'
    text=runner.read_text()
    if 'set(CMAKE_EXE_LINKER_FLAGS ' not in text:
        text=text.replace('project(', 'set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static-libgcc")\nproject(',1)
    import re
    text=re.sub(r'set\(APPLICATION_ID "[^"]+"\)', 'set(APPLICATION_ID "'+a.bundle_id+'")',text)
    runner.write_text(text)
    main=host/'linux/runner/my_application.cc'
    name=json.dumps(a.display_name,ensure_ascii=False)
    if a.chinese_display_name:
        name='(g_str_has_prefix(g_get_language_names()[0], "zh") ? '+json.dumps(a.chinese_display_name,ensure_ascii=False)+' : '+name+')'
    text=main.read_text().replace('"libflui_host"',name)
    main.write_text(text,encoding='utf-8')
    run([flutter,'--suppress-analytics','--no-version-check','build','linux','--release','--no-pub'],host)
    output=host/'build/linux/x64/release/bundle'
    shutil.copy2(prefix/'lib/libflui.so',output/'lib/libflui.so')
    shutil.copy2(clientbuild/'libflui_client.so',output/'lib/libflui_client.so')
    copy_runtimes(output/'lib')
    print('Linux bundle:',output)
elif a.platform=='windows':
    runner=host/'windows/CMakeLists.txt'
    text=runner.read_text()
    text=text.replace('cmake_minimum_required(VERSION 3.14)', 'cmake_minimum_required(VERSION 3.24)')
    if 'set(CMAKE_MSVC_RUNTIME_LIBRARY ' not in text:
        text=text.replace('project(', 'set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")\nproject(',1)
    runner.write_text(text)
    main=host/'windows/runner/main.cpp'
    name='L'+json.dumps(a.display_name,ensure_ascii=False)
    if a.chinese_display_name:
        name='(PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? L'+json.dumps(a.chinese_display_name,ensure_ascii=False)+' : '+name+')'
    main.write_text(main.read_text().replace('L"libflui_host"',name),encoding='utf-8')
    if a.logo_dir:shutil.copy2(a.logo_dir/'windows/zhiyu.ico',host/'windows/runner/resources/app_icon.ico')
    run([flutter,'--suppress-analytics','--no-version-check','build','windows','--release','--no-pub'],host)
    output=host/'build/windows/x64/runner/Release'
    shutil.copy2(prefix/'bin/flui.dll',output/'flui.dll')
    shutil.copy2(clientbuild/'Release/flui_client.dll',output/'flui_client.dll')
    copy_runtimes(output)
    print('Windows app:',output/'libflui_host.exe')
else:
    import re,plistlib
    project=host/'ios/Runner.xcodeproj/project.pbxproj'
    s=project.read_text()
    s=re.sub(r'PRODUCT_BUNDLE_IDENTIFIER = [^;]+;', 'PRODUCT_BUNDLE_IDENTIFIER = '+a.bundle_id+';',s)
    s=re.sub(r'IPHONEOS_DEPLOYMENT_TARGET = [^;]+;', 'IPHONEOS_DEPLOYMENT_TARGET = 15.0;',s)
    project.write_text(s)
    plist=host/'ios/Runner/Info.plist';data=plistlib.loads(plist.read_bytes());data['CFBundleDisplayName']=a.display_name;data['CFBundleName']=a.display_name;data['CFBundleLocalizations']=['en','zh-Hans','zh-Hant'];
    if a.ios_info_plist:
        overlay=plistlib.loads(a.ios_info_plist.read_bytes())
        allowed={'NSBluetoothAlwaysUsageDescription','NSBluetoothPeripheralUsageDescription','UIBackgroundModes','NSLocalNetworkUsageDescription','NSBonjourServices'}
        if not set(overlay).issubset(allowed):p.error('Only declared network/Bluetooth privacy and background keys are accepted')
        data.update(overlay)
    plist.write_bytes(plistlib.dumps(data))
    if a.logo_dir:shutil.copytree(a.logo_dir/'apple/ios/AppIcon.appiconset',host/'ios/Runner/Assets.xcassets/AppIcon.appiconset',dirs_exist_ok=True)
    run([flutter,'--suppress-analytics','--no-version-check','build','ios','--release','--build-number',str(a.build_number),'--no-codesign','--no-pub'],host)
    app=host/'build/ios/iphoneos/Runner.app'
    if a.chinese_display_name:
        for locale,name in [('en',a.display_name),('zh-Hans',a.chinese_display_name),('zh-Hant',a.chinese_display_name)]:
            folder=app/(locale+'.lproj');folder.mkdir(exist_ok=True)
            (folder/'InfoPlist.strings').write_text('"CFBundleDisplayName" = '+json.dumps(name,ensure_ascii=False)+';\n"CFBundleName" = '+json.dumps(name,ensure_ascii=False)+';\n',encoding='utf-8')
    for source in [prefix/'lib/flui.framework',clientbuild/'FluiClient.framework',*a.embed_framework]:
        target=app/'Frameworks'/source.name
        if target.exists():shutil.rmtree(target)
        shutil.copytree(source,target,symlinks=True)
    print('Unsigned app:',app)
