#!/usr/bin/env python3
"""Exercise the production Android stream importer under a 32 MiB Java heap."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidFileImportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        java_home = os.environ.get('JAVA_HOME')
        cls.java = str(Path(java_home) / 'bin/java') if java_home else shutil.which('java')
        javac = str(Path(java_home) / 'bin/javac') if java_home else shutil.which('javac')
        if not cls.java or not javac:
            raise unittest.SkipTest('JDK required')
        work = ROOT.parent / '.build/work/tests/android-file-import'
        work.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(dir=work)
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.classes = Path(cls.temporary.name)
        subprocess.run([javac, '--release', '8', '-d', str(cls.classes),
                        str(ROOT / 'platform/android/FluiFileImport.java'),
                        str(ROOT / 'tests/android/FluiFileImportTest.java')],
                       check=True, capture_output=True, text=True, timeout=60)

    def run_case(self, name):
        result = subprocess.run([self.java, '-Xmx32m', '-Djava.io.tmpdir=' + str(self.classes),
                                 '-cp', str(self.classes), 'org.skstu.libflui.FluiFileImportTest', name],
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_large_unknown_length_file_exceeds_heap(self):
        self.run_case('large')

    def test_provider_read_and_close_failures_leave_no_partial_files(self):
        self.run_case('failure')

    def test_untrusted_names_empty_files_and_repeat_selections(self):
        self.run_case('names')


if __name__ == '__main__':
    unittest.main()
