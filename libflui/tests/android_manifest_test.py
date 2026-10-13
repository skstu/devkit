import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from android_manifest import merge, resolve_overlay, verify_xml


class AndroidManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.client = Path(self.temp.name).resolve()
        self.overlay = self.client / 'resources/android-permissions.xml'
        self.overlay.parent.mkdir()
        self.overlay.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android">
          <uses-permission android:name="android.permission.INTERNET"/>
          <uses-permission android:name="android.permission.BLUETOOTH" android:maxSdkVersion="30"/>
          <uses-permission android:name="android.permission.BLUETOOTH_SCAN" android:usesPermissionFlags="neverForLocation"/>
          <application android:allowBackup="false" android:usesCleartextTraffic="false"/>
        </manifest>''')
        self.host = self.client / 'AndroidManifest.xml'
        self.host.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android"
          package="org.example.client" android:versionCode="24">
          <application android:label="example"><provider android:name="existing.Provider"/></application>
        </manifest>''')

    def test_omitted_flag_uses_consumer_declarations(self):
        self.assertEqual(resolve_overlay(self.client), self.overlay)
        merge(self.host, resolve_overlay(self.client))
        before = self.host.read_text()
        merge(self.host, self.overlay)
        self.assertEqual(self.host.read_text(), before)
        self.assertIn('existing.Provider', before)
        report = verify_xml(before, self.overlay, 'org.example.client', 24)
        self.assertIn('android.permission.INTERNET', report['permissions'])
        self.assertEqual(report['privacy']['allowBackup'], 'false')

    def test_missing_permissions_and_privacy_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'android.permission.INTERNET'):
            verify_xml(self.host.read_text(), self.overlay)
        merge(self.host, self.overlay)
        with self.assertRaisesRegex(ValueError, 'allowBackup'):
            verify_xml(self.host.read_text().replace('android:allowBackup="false"', ''), self.overlay)

    def test_compiled_flags_and_permission_caps(self):
        merge(self.host, self.overlay)
        compiled = self.host.read_text().replace('neverForLocation', '0x00010000')
        verify_xml(compiled, self.overlay)
        with self.assertRaisesRegex(ValueError, 'maxSdkVersion'):
            verify_xml(compiled.replace('android:maxSdkVersion="30"', 'android:maxSdkVersion="31"'), self.overlay)
        with self.assertRaisesRegex(ValueError, 'INTERNET/maxSdkVersion'):
            verify_xml(compiled.replace('android.permission.INTERNET"', 'android.permission.INTERNET" android:maxSdkVersion="30"'), self.overlay)

    def test_wrong_identity_build_and_host_type(self):
        merge(self.host, self.overlay)
        for package, build, debug in [('org.other', 24, False), ('org.example.client', 25, False), ('org.example.client', 24, True)]:
            with self.assertRaises(ValueError):
                verify_xml(self.host.read_text(), self.overlay, package, build, debug)

    def test_missing_explicit_file_and_pure_ui_host(self):
        self.assertIsNone(resolve_overlay(self.client / 'ui-only'))
        with self.assertRaises(OSError):
            resolve_overlay(self.client, self.client / 'missing.xml')

    def test_duplicate_and_unsupported_declarations_rejected(self):
        for body in ['<activity/>', '<application android:allowBackup="@bool/unknown"/>',
                     '<uses-permission android:name="one"/><uses-permission android:name="one"/>']:
            self.overlay.write_text('<manifest xmlns:android="http://schemas.android.com/apk/res/android">' + body + '</manifest>')
            with self.assertRaises(ValueError):
                resolve_overlay(self.client)


if __name__ == '__main__':
    unittest.main()
