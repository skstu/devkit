#!/usr/bin/env python3
"""Producer-only platform preview. Consumer source remains C++/XML and links an installed binary SDK.
This development harness is not yet a distributable multi-platform SDK release."""
import argparse, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--platform',choices=['android','ios','windows','linux'],required=True)
p.add_argument('--flutter-sdk',type=Path,required=True)
p.add_argument('--client',type=Path,required=True)
p.add_argument('--work',type=Path,help='Reusable build directory; defaults to a fixed consumer/platform/configuration slot')
p.add_argument('--configuration',choices=['Debug','Release'])
p.add_argument('--android-ndk',type=Path)
p.add_argument('--bundle-id',required=True)
p.add_argument('--development-host', action='store_true', help='Debuggable owned-device test host; never a normal distribution build')
p.add_argument('--build-number', type=int, default=1, help='Monotonically increasing consumer package build number')
p.add_argument('--display-name', default='Libflui Preview')
p.add_argument('--chinese-display-name', help='Optional consumer name for Chinese system languages')
p.add_argument('--logo-dir', type=Path, help='Consumer-generated platform icon directory')
p.add_argument('--client-cmake-arg', action='append', default=[])
p.add_argument('--ios-info-plist', type=Path, help='Consumer privacy/background declarations, merged into SDK-owned host')
p.add_argument('--ios-simulator', action='store_true', help='Isolated Debug iOS simulator host; never an App Store binary')
p.add_argument('--ios-simulator-team', default='', help='Simulator application-identifier prefix; no device provisioning or credentials')
p.add_argument('--ios-privacy-manifest', type=Path, help='Consumer PrivacyInfo.xcprivacy; defaults to client/resources/PrivacyInfo.xcprivacy')
p.add_argument('--embed-framework', action='append', type=Path, default=[])
p.add_argument('--runtime-dir', action='append', type=Path, default=[], help='Consumer SDK runtime directories; copied into the host bundle')
p.add_argument('--android-sources', action='append', type=Path, default=[], help='Consumer/platform Kotlin or Java source root')
p.add_argument('--android-proguard', action='append', type=Path, default=[], help='Dependency-owned JNI/R8 keep rules, included in optimized release builds')
p.add_argument('--android-manifest', type=Path, help='Consumer uses-permission and application privacy declarations')
p.add_argument('--android-apkanalyzer', type=Path, help='Android SDK apkanalyzer for mandatory final manifest verification')
p.add_argument('--android-initializer', help='Consumer platform initialization class with initialize(Context)')
a=p.parse_args()
if a.ios_simulator and (a.platform!='ios' or a.configuration!='Debug'):p.error('--ios-simulator requires --platform ios --configuration Debug')
if a.platform=='android':
    from android_manifest import resolve_overlay, merge as merge_android_manifest, find_analyzer, verify_apk
    try:a.android_manifest=resolve_overlay(a.client,a.android_manifest)
    except (ValueError,OSError) as error:p.error(str(error))
component=Path(__file__).resolve().parents[1]
repo=component.parent
configuration=a.configuration or ('Debug' if a.development_host else 'Release')
if a.development_host and configuration!='Debug':p.error('--development-host requires Debug')
a.development_host=configuration=='Debug'
consumer=hashlib.sha256(str(a.client.resolve()).encode()).hexdigest()[:12]
work=(a.work or repo/'.build/work/libflui'/consumer/a.platform/configuration.lower()).resolve()
env=dict(os.environ,CI='true',FLUTTER_SUPPRESS_ANALYTICS='true')
# Keep this Mac's Flutter dependencies and temporary files under Developer.
if __import__('sys').platform == 'darwin':
    developer = Path.home() / 'Developer'
    for directory in (work, a.flutter_sdk):
        if not directory.resolve().is_relative_to(developer.resolve()):
            raise SystemExit('Flutter SDK and build outputs must be inside ' + str(developer))
    cache = developer / '.cache/flutter'
    for key, name in [('PUB_CACHE','pub'), ('GRADLE_USER_HOME','gradle'), ('TMPDIR','tmp')]:
        location = cache / name
        location.mkdir(parents=True, exist_ok=True)
        env[key] = str(location)

work.mkdir(parents=True,exist_ok=True)
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
flags=['-DCMAKE_BUILD_TYPE='+configuration]
if a.platform=='android':
    if not a.android_ndk:p.error('--android-ndk required')
    flags+=['-DCMAKE_TOOLCHAIN_FILE='+str(a.android_ndk/'build/cmake/android.toolchain.cmake'),
            '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-24','-DANDROID_STL=c++_static']
elif a.platform=='linux':
    flags+=['-DCMAKE_SHARED_LINKER_FLAGS=-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL',
            '-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON','-DCMAKE_INSTALL_RPATH=$ORIGIN']
elif a.platform=='ios':
    flags+=['-DCMAKE_SYSTEM_NAME=iOS','-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0',
            '-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO','-DCMAKE_OSX_SYSROOT='+('iphonesimulator' if a.ios_simulator else 'iphoneos')]
run(['cmake','-S',component,'-B',native,'-DLIBFLUI_PORTABLE_HOST=ON','-DBUILD_TESTING=OFF',*flags])
run(['cmake','--build',native,'--config',configuration,'--parallel','4'])
run(['cmake','--install',native,'--config',configuration,'--prefix',prefix])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if (repo/'source-provenance.json').exists():
    provenance=json.loads((repo/'source-provenance.json').read_text())
    for name,digest in provenance['source_files'].items():
        if sha(repo/name)!=digest:raise SystemExit('Staged source changed: '+name)
    commit=provenance['source_commit']
else:
    commit=subprocess.check_output(['git','-C',repo,'rev-parse','HEAD'],text=True).strip()
manifest={'schema_version':1,'sdk':'libflui','channel':'unreleased-device-preview','source_commit':commit,
    'component_dirty':True,'platform':('ios-simulator' if a.ios_simulator else a.platform),'architecture':('x86_64' if a.platform in ('windows','linux') else 'arm64'),'runtime':lock,
    'source_files':{str(p.relative_to(repo)):sha(p) for p in component.rglob('*') if p.is_file() and not {'__pycache__','.dart_tool','build'}.intersection(p.parts)},
    'files':{str(p.relative_to(prefix)):sha(p) for p in prefix.rglob('*') if p.is_file() and p.name!='sdk-manifest.json'}}
(prefix/'sdk-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
sdklock=work/'native.lock.json'
sdklock.write_text(json.dumps({'source_commit':commit,'component_dirty':True,'manifest_sha256':sha(prefix/'sdk-manifest.json')},indent=2)+'\n')
# Flutter takes the package version from this exact copied pubspec.
app_version=next(line.split(':',1)[1].strip().split('+',1)[0] for line in (host/'pubspec.yaml').read_text().splitlines() if line.startswith('version:'))
clientbuild=work/'client'
run(['cmake','-S',a.client.resolve(),'-B',clientbuild,'-DZHIYU_PORTABLE_HOST=ON',
     '-DZHIYU_SDK_LOCK='+str(sdklock),'-Dlibflui_DIR='+str(prefix/'lib/cmake/libflui'),'-DZHIYU_BUILD_NUMBER='+str(a.build_number),'-DZHIYU_APP_VERSION='+app_version,*flags,*a.client_cmake_arg])
run(['cmake','--build',clientbuild,'--config',configuration,'--parallel','4'])
if a.platform=='android':
    jni=host/'android/app/src/main/jniLibs/arm64-v8a';jni.mkdir(parents=True,exist_ok=True)
    shutil.copy2(prefix/'lib/libflui.so',jni/'libflui.so')
    shutil.copy2(clientbuild/'libflui_client.so',jni/'libflui_client.so')
    copy_runtimes(jni)
    for directory in a.android_sources:
        shutil.copytree(directory,host/'android/app/src/main/java',dirs_exist_ok=True)
    import re
    if a.android_initializer and not re.fullmatch(r'[A-Za-z_][\w]*(?:\.[A-Za-z_][\w]*)+',a.android_initializer):p.error('Invalid Android initializer class')
    activities=list((host/'android/app/src/main/kotlin').rglob('MainActivity.kt'))
    if len(activities)!=1:p.error('Expected one SDK-owned Android activity')
    activity=activities[0]
    package=re.search(r'^package ([\w.]+)',activity.read_text(),re.M).group(1)
    init=(a.android_initializer+'.initialize(this)') if a.android_initializer else ''
    permissions=(a.android_initializer+'.permissionResult(code, permissions, results)') if a.android_initializer else ''
    activity.write_text('package '+package+"\n"+f"""
import android.os.Bundle
import android.content.Context
import android.content.Intent
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.embedding.engine.dart.DartExecutor
import io.flutter.plugin.common.MethodChannel
import org.skstu.libflui.FluiFiles
class MainActivity: FlutterActivity() {{
 companion object {{ private var retainedEngine: FlutterEngine? = null }}
 override fun provideFlutterEngine(context: Context): FlutterEngine {{
  return retainedEngine ?: FlutterEngine(context.applicationContext).also {{
   retainedEngine=it
   it.dartExecutor.executeDartEntrypoint(DartExecutor.DartEntrypoint.createDefault())
  }}
 }}
 override fun shouldDestroyEngineWithHost(): Boolean = false
 override fun configureFlutterEngine(engine: FlutterEngine) {{
  super.configureFlutterEngine(engine)
  FluiFiles.attach(this,MethodChannel(engine.dartExecutor.binaryMessenger,"devkit.libflui/files"))
 }}
 override fun onCreate(state: Bundle?) {{ {init};super.onCreate(state) }}
 override fun onRequestPermissionsResult(code:Int,permissions:Array<out String>,results:IntArray) {{
  super.onRequestPermissionsResult(code,permissions,results);{permissions}
 }}
 override fun onActivityResult(code:Int,result:Int,data:Intent?) {{
  if(!FluiFiles.result(code,result,data))super.onActivityResult(code,result,data)
 }}
}}
""")
    destination=host/'android/app/src/main/kotlin/org/skstu/libflui/FluiFiles.kt'
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(component/'platform/android/FluiFiles.kt',destination)
    gradle=host/'android/app/build.gradle.kts';s=gradle.read_text()
    import re
    s=re.sub(r'applicationId = "[^"]+"','applicationId = "'+a.bundle_id+'"',s)
    s=s.replace('minSdk = flutter.minSdkVersion','minSdk = 24')
    if a.development_host and not a.bundle_id.endswith('.test'):
        p.error('Debuggable host requires a test application identity')
    s=s.replace('            isDebuggable = true\n','')
    marker='            // libflui dependency JNI keep rules\n'
    rules_line='            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "libflui-consumer-rules.pro")\n'
    s=s.replace(marker+rules_line,'')
    if a.android_proguard:
        rules=[]
        for file in a.android_proguard:
            if not file.is_file():p.error('Android keep rules do not exist: '+str(file))
            rules.append(file.read_text())
        (host/'android/app/libflui-consumer-rules.pro').write_text('\n'.join(rules)+'\n')
        if len(re.findall(r'\brelease\s*\{',s))!=1:p.error('Expected one Android release build type')
        s=re.sub(r'(\brelease\s*\{\n)',lambda m:m.group(1)+marker+rules_line,s,count=1)
    gradle.write_text(s)
    manifestxml=host/'android/app/src/main/AndroidManifest.xml'
    if a.android_manifest:
        merge_android_manifest(manifestxml,a.android_manifest)
    import xml.etree.ElementTree as ET
    ET.register_namespace('android','http://schemas.android.com/apk/res/android')
    tree=ET.parse(manifestxml);android='{http://schemas.android.com/apk/res/android}';application=tree.getroot().find('application')
    authority=a.bundle_id+'.libflui.files'
    for old in list(application.findall('provider')):
        if old.get(android+'authorities')==authority:application.remove(old)
    provider=ET.SubElement(application,'provider',{android+'name':'androidx.core.content.FileProvider',android+'authorities':authority,android+'exported':'false',android+'grantUriPermissions':'true'})
    ET.SubElement(provider,'meta-data',{android+'name':'android.support.FILE_PROVIDER_PATHS',android+'resource':'@xml/libflui_files'})
    tree.write(manifestxml,encoding='unicode')
    resource=host/'android/app/src/main/res/xml/libflui_files.xml';resource.parent.mkdir(parents=True,exist_ok=True)
    resource.write_text('<paths><cache-path name="open-copy" path="libflui-open/"/></paths>')
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
    verification=work/'android-package-verification.json'
    if a.android_manifest:
        # Fail before Gradle if the binary-manifest inspector is unavailable.
        analyzer=a.android_apkanalyzer or find_analyzer(host)
        verification.write_text(json.dumps({'passed':False,'state':'building'})+'\n')
    run([flutter,'--suppress-analytics','--no-version-check','build','apk','--debug' if a.development_host else '--release','--build-number',str(a.build_number),'--target-platform','android-arm64','--no-pub'],host)
    apk=host/'build/app/outputs/flutter-apk'/('app-debug.apk' if a.development_host else 'app-release.apk')
    if a.android_manifest:
        report=verify_apk(apk,a.android_manifest,analyzer,a.bundle_id,a.build_number,a.development_host,env)
        verification.write_text(json.dumps(report,indent=2)+'\n')
    print('APK:',apk)
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
    from desktop_files import install as install_desktop_files
    install_desktop_files(host,'linux')
    run([flutter,'--suppress-analytics','--no-version-check','build','linux','--'+configuration.lower(),'--build-number',str(a.build_number),'--no-pub'],host)
    output=host/('build/linux/x64/'+configuration.lower()+'/bundle')
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
    from desktop_files import install as install_desktop_files
    install_desktop_files(host,'windows')
    run([flutter,'--suppress-analytics','--no-version-check','build','windows','--'+configuration.lower(),'--build-number',str(a.build_number),'--no-pub'],host)
    output=host/('build/windows/x64/runner/'+configuration)
    shutil.copy2(prefix/'bin/flui.dll',output/'flui.dll')
    shutil.copy2(clientbuild/configuration/'flui_client.dll',output/'flui_client.dll')
    copy_runtimes(output)
    print('Windows app:',output/'libflui_host.exe')
else:
    import re,plistlib
    shutil.copy2(component/'platform/ios/AppDelegate.swift',host/'ios/Runner/AppDelegate.swift')
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
    run([flutter,'--suppress-analytics','--no-version-check','build','ios','--'+configuration.lower(),'--build-number',str(a.build_number),'--no-codesign','--no-pub',*(['--simulator'] if a.ios_simulator else [])],host)
    app=host/('build/ios/'+('iphonesimulator' if a.ios_simulator else 'iphoneos')+'/Runner.app')
    if a.ios_simulator:
        derived=work/'xcode-simulator'
        run(['xcodebuild','-workspace','Runner.xcworkspace','-scheme','Runner','-configuration','Debug','-sdk','iphonesimulator','-destination','generic/platform=iOS Simulator','-derivedDataPath',derived,'-arch','arm64','ONLY_ACTIVE_ARCH=YES','CODE_SIGNING_ALLOWED=YES','CODE_SIGN_IDENTITY=-','DEVELOPMENT_TEAM='+a.ios_simulator_team,'build'],host/'ios')
        generated=derived/'Build/Products/Debug-iphonesimulator/Runner.app'
        for name in ['Runner','Runner.debug.dylib','__preview.dylib']:
            if (generated/name).exists():shutil.copy2(generated/name,app/name)
    privacy=a.ios_privacy_manifest or a.client/'resources/PrivacyInfo.xcprivacy'
    if privacy.is_file():
        value=plistlib.loads(privacy.read_bytes())
        if not isinstance(value.get('NSPrivacyTracking'),bool):p.error('Invalid consumer privacy manifest')
        shutil.copy2(privacy,app/'PrivacyInfo.xcprivacy')
    elif a.ios_privacy_manifest:p.error('Consumer privacy manifest does not exist')
    if a.chinese_display_name:
        for locale,name in [('en',a.display_name),('zh-Hans',a.chinese_display_name),('zh-Hant',a.chinese_display_name)]:
            folder=app/(locale+'.lproj');folder.mkdir(exist_ok=True)
            (folder/'InfoPlist.strings').write_text('"CFBundleDisplayName" = '+json.dumps(name,ensure_ascii=False)+';\n"CFBundleName" = '+json.dumps(name,ensure_ascii=False)+';\n',encoding='utf-8')
    for source in [prefix/'lib/flui.framework',clientbuild/'FluiClient.framework',*a.embed_framework]:
        target=app/'Frameworks'/source.name
        if target.exists():shutil.rmtree(target)
        shutil.copytree(source,target,symlinks=True)
        info_path=target/'Info.plist'
        info=plistlib.loads(info_path.read_bytes())
        binary=target/info['CFBundleExecutable']
        metadata=subprocess.check_output(['xcrun','vtool','-show-build',str(binary)],text=True)
        minimum=re.search(r'minos\s+(\d+\.\d+(?:\.\d+)?)',metadata)
        expected_platform='IOSSIMULATOR' if a.ios_simulator else 'IOS'
        if not re.search(r'platform\s+'+expected_platform+r'\b',metadata) or not minimum:p.error('Framework platform does not match the host: '+str(source))
        if tuple(map(int,minimum[1].split('.')))>(15,0,0):p.error('Framework requires a newer iOS version: '+str(source))
        info.update(MinimumOSVersion=minimum[1],CFBundleSupportedPlatforms=['iPhoneSimulator' if a.ios_simulator else 'iPhoneOS'])
        info_path.write_bytes(plistlib.dumps(info))
    if a.ios_simulator:
        for framework in (app/'Frameworks').glob('*.framework'):run(['codesign','--force','--sign','-',framework])
        run(['codesign','--force','--sign','-',app])
    print('Simulator app:' if a.ios_simulator else 'Unsigned app:',app)
