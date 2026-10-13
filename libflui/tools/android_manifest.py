#!/usr/bin/env python3
"""Consumer-owned Android permission overlay and final APK verification.

No device access. The producer merges declarations, never invents permissions.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

ANDROID = '{http://schemas.android.com/apk/res/android}'
ET.register_namespace('android', ANDROID[1:-1])
PRIVACY = {ANDROID + 'allowBackup', ANDROID + 'usesCleartextTraffic'}
PERMISSION_ATTRIBUTES = {ANDROID + name for name in ('name', 'maxSdkVersion', 'usesPermissionFlags')}


def resolve_overlay(client, explicit=None):
    """The conventional consumer file is automatic, even if a CLI flag is lost."""
    path = Path(explicit) if explicit else Path(client) / 'resources/android-permissions.xml'
    if explicit or path.exists():
        overlay(path)  # fail before building if missing or unsupported
        return path.resolve()
    return None  # a UI-only host need not request network or Bluetooth


def overlay(path):
    root = ET.parse(path).getroot()
    if root.tag != 'manifest' or root.attrib:
        raise ValueError('Android overlay must be a manifest containing declarations only')
    names = set()
    for child in root:
        if child.tag == 'uses-permission':
            name = child.get(ANDROID + 'name')
            if not name or name in names or len(child) or not set(child.attrib) <= PERMISSION_ATTRIBUTES:
                raise ValueError('Invalid or duplicate Android permission declaration')
            names.add(name)
        elif child.tag == 'application':
            if len(root.findall('application')) != 1 or len(child) or not set(child.attrib) <= PRIVACY:
                raise ValueError('Unsupported Android application overlay')
            if any(value not in ('true', 'false') for value in child.attrib.values()):
                raise ValueError('Android privacy flags must be explicit booleans')
        else:
            raise ValueError('Only permissions and application privacy flags are accepted')
    return root


def merge(manifest, consumer):
    tree = ET.parse(manifest)
    root = tree.getroot()
    for child in overlay(consumer):
        if child.tag == 'uses-permission':
            for old in list(root.findall('uses-permission')):
                if old.get(ANDROID + 'name') == child.get(ANDROID + 'name'):
                    root.remove(old)
            root.insert(0, copy.deepcopy(child))
        else:
            app = root.find('application')
            if app is None:
                raise ValueError('Generated Android host has no application')
            app.attrib.update(child.attrib)
    tree.write(manifest, encoding='unicode')


def normalized(key, value):
    if value is None:
        return None
    if key == ANDROID + 'usesPermissionFlags':
        if value == 'neverForLocation':
            return 0x10000
        return int(value, 0)
    if key == ANDROID + 'maxSdkVersion':
        return int(value, 0)
    return value


def verify_xml(xml, consumer, package=None, build_number=None, development_host=False):
    root = ET.fromstring(xml)
    expected = overlay(consumer)
    errors = []
    if package is not None and root.get('package') != package:
        errors.append('application ID differs from requested package')
    if build_number is not None and root.get(ANDROID + 'versionCode') != str(build_number):
        errors.append('versionCode differs from requested build')
    wanted = expected.findall('uses-permission')
    for permission in wanted:
        name = permission.get(ANDROID + 'name')
        found = [p for p in root.findall('uses-permission') if p.get(ANDROID + 'name') == name]
        if len(found) != 1:
            errors.append('missing or duplicate permission: ' + name)
            continue
        # Also reject an unintended maxSdkVersion cap on an uncapped permission.
        for key in PERMISSION_ATTRIBUTES:
            if normalized(key, found[0].get(key)) != normalized(key, permission.get(key)):
                errors.append('permission attribute differs: ' + name + '/' + key.split('}')[-1])
    app = root.find('application')
    privacy = expected.find('application')
    if app is None:
        errors.append('application element missing')
    else:
        if privacy is not None:
            for key, value in privacy.attrib.items():
                if app.get(key) != value:
                    errors.append('application flag missing or different: ' + key.split('}')[-1])
        if (app.get(ANDROID + 'debuggable', 'false') == 'true') != development_host:
            errors.append('debuggable state differs from requested host type')
    if errors:
        raise ValueError('APK manifest verification failed: ' + '; '.join(errors))
    return {'package': root.get('package'), 'buildNumber': root.get(ANDROID + 'versionCode'),
            'permissions': sorted(p.get(ANDROID + 'name') for p in wanted),
            'privacy': {key.split('}')[-1]: value for key, value in (privacy.attrib.items() if privacy is not None else [])}}


def find_analyzer(host):
    executable = 'apkanalyzer.bat' if os.name == 'nt' else 'apkanalyzer'
    found = shutil.which(executable)
    if found:
        return Path(found)
    sdk = os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME')
    properties = Path(host) / 'android/local.properties'
    if not sdk and properties.is_file():
        sdk = next((line.split('=', 1)[1].replace('\\:', ':').replace('\\\\', '\\')
                    for line in properties.read_text().splitlines() if line.startswith('sdk.dir=')), None)
    if sdk:
        candidates = sorted((Path(sdk) / 'cmdline-tools').glob('*/bin/' + executable))
        for path in [Path(sdk) / 'cmdline-tools/latest/bin' / executable, *reversed(candidates)]:
            if path.is_file():
                return path
    raise ValueError('Final APK verification requires Android SDK apkanalyzer; set ANDROID_SDK_ROOT or --android-apkanalyzer')


def verify_apk(apk, consumer, analyzer, package, build_number, development_host=False, env=None):
    xml = subprocess.check_output([str(analyzer), 'manifest', 'print', str(apk)], text=True, env=env)
    report = verify_xml(xml, consumer, package, build_number, development_host)
    report.update({'apkSha256': hashlib.sha256(Path(apk).read_bytes()).hexdigest(),
                   'overlaySha256': hashlib.sha256(Path(consumer).read_bytes()).hexdigest(), 'passed': True})
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', required=True, type=Path)
    parser.add_argument('--overlay', required=True, type=Path)
    parser.add_argument('--apkanalyzer', required=True, type=Path)
    parser.add_argument('--package', required=True)
    parser.add_argument('--build-number', required=True, type=int)
    parser.add_argument('--development-host', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(verify_apk(args.apk, args.overlay, args.apkanalyzer, args.package,
                                    args.build_number, args.development_host), indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError, ET.ParseError) as error:
        parser.exit(1, str(error) + '\n')
