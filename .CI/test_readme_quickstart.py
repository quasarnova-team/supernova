#!/usr/bin/env python3
"""The README quick start must be executable verbatim - this test executes it.

Extracts the fenced block between the quickstart markers in README.md and runs
it under one wall-clock budget, so the block IS the test and cannot rot. Three
adaptations, all container-only: sudo is stripped (the container is root),
DEBIAN_FRONTEND=noninteractive is exported (a fresh container has no tzdata
answers; a workstation does), and the GitHub clone is replaced with a copy of
this checkout so a pull request tests its own README. The final line is treated
as the server boot: started, asserted alive, terminated.
"""

import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

QUASAR_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUDGET_SECONDS = 600
BOOT_GRACE_SECONDS = 8


def fail(message):
    print('FAIL: %s' % message)
    return 1


def main():
    readme = open(os.path.join(QUASAR_ROOT, 'README.md'), encoding='utf-8').read()
    match = re.search(
        r'<!-- quickstart:begin -->\s*```bash\n(.*?)```\s*<!-- quickstart:end -->',
        readme, re.S)
    if not match:
        return fail('README.md has no marked quickstart block '
                    '(<!-- quickstart:begin/end --> around one ```bash fence)')

    lines = [l.strip() for l in match.group(1).splitlines()
             if l.strip() and not l.strip().startswith('#')]
    if len(lines) < 2:
        return fail('quickstart block has fewer than two commands')

    project_dir = None
    steps = []
    for line in lines[:-1]:
        line = re.sub(r'\bsudo\s+', '', line)
        clone = re.match(r'git clone --recursive \S*/(\S+?)(?:\.git)?$', line)
        if clone:
            project_dir = clone.group(1)
            steps.append('cp -a --no-preserve=ownership %s %s && rm -rf %s/build'
                         % (shlex.quote(QUASAR_ROOT), project_dir, project_dir))
        else:
            steps.append(line)
    boot_line = re.sub(r'\bsudo\s+', '', lines[-1])
    if project_dir is None:
        return fail('quickstart block has no "git clone --recursive" line')

    pin = re.search(r'enable_module open62541-compat (\S+)', '\n'.join(steps))
    manifest = json.load(open(os.path.join(QUASAR_ROOT, '.CI', 'test_cases',
                                           'manifest.json'), encoding='utf-8'))
    compat_branch = manifest['backends']['o6']['compat_branch']
    if pin is None or pin.group(1) != compat_branch:
        return fail('the README pins open62541-compat %s but the manifest '
                    'compat_branch is %s - bump the README quickstart'
                    % (pin.group(1) if pin else '(missing)', compat_branch))

    env = dict(os.environ, DEBIAN_FRONTEND='noninteractive')
    scratch = tempfile.mkdtemp(prefix='quasar_quickstart_')
    started = time.monotonic()
    try:
        script = 'set -eu\n' + '\n'.join(steps)
        build = subprocess.run(['bash', '-c', script], cwd=scratch, env=env,
                               timeout=BUDGET_SECONDS)
        if build.returncode != 0:
            return fail('quickstart commands failed (exit %d) - the README is '
                        'ahead of or behind the tree' % build.returncode)

        server = subprocess.Popen(['bash', '-c', boot_line],
                                  cwd=os.path.join(scratch, project_dir), env=env,
                                  start_new_session=True)
        time.sleep(BOOT_GRACE_SECONDS)
        alive = server.poll() is None
        try:
            os.killpg(server.pid, signal.SIGTERM)
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(server.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        if not alive:
            return fail('the server from the final quickstart line ("%s") died '
                        'within %ds of starting (exit %s)'
                        % (boot_line, BOOT_GRACE_SECONDS, server.returncode))

        elapsed = time.monotonic() - started
        if elapsed > BUDGET_SECONDS:
            return fail('quickstart took %.0fs, over the %ds budget'
                        % (elapsed, BUDGET_SECONDS))
        print('OK: README quickstart delivered a live server in %.0fs '
              '(budget %ds)' % (elapsed, BUDGET_SECONDS))
        return 0
    except subprocess.TimeoutExpired:
        return fail('quickstart exceeded the %ds wall-clock budget' % BUDGET_SECONDS)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


sys.exit(main())
