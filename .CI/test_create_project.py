#!/usr/bin/env python3
"""An installed project must contain every module its own build system names.

Runs create_project into a temp directory and asserts each NATIVE_SERVER_MODULES
entry of the installed CMakeLists arrived with its module CMakeLists. Guards the
files.txt registration gap: a module added to the build but never registered in
FrameworkInternals/original_files.txt installs a project that dies at configure.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

QUASAR_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    scratch = tempfile.mkdtemp(prefix='quasar_create_project_')
    project = os.path.join(scratch, 'project')
    try:
        subprocess.run([sys.executable, 'quasar.py', 'create_project', project],
                       cwd=QUASAR_ROOT, check=True, timeout=600,
                       env=dict(os.environ, PYTHONUTF8='1'))
        cmake = open(os.path.join(project, 'CMakeLists.txt'), encoding='utf-8').read()
        match = re.search(r'set\(\s*NATIVE_SERVER_MODULES\s+([^)]*)\)', cmake)
        if not match:
            print('FAIL: installed CMakeLists.txt has no NATIVE_SERVER_MODULES list')
            return 1
        modules = match.group(1).split()
        missing = [m for m in modules
                   if not os.path.isfile(os.path.join(project, m, 'CMakeLists.txt'))]
        if missing:
            print('FAIL: the installed CMakeLists names %s in NATIVE_SERVER_MODULES '
                  'but the installer never delivered them - the project dies at CMake '
                  'configure. Register the missing files in '
                  'FrameworkInternals/original_files.txt and run '
                  '"./quasar.py create_release".' % ', '.join(missing))
            return 1
        print('OK: create_project delivered all %d native modules: %s'
              % (len(modules), ' '.join(modules)))
        return 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


sys.exit(main())
