#!/usr/bin/env python3
"""Cross-backend parity check -- the .parity-night mechanic harvested into the PR
matrix. For each framework test case that ran on BOTH backends, diff the o6 dump
against the uasdk dump with NodeSetTools' nodeset_compare.

Advisory by default: it prints a per-case report and does NOT fail the build,
because a few cross-backend differences are known-benign (method-argument
access-level bits; config-property browse-name namespace). Pass --strict to exit
non-zero on any diff once a benign-classifier is wired in.

For oracle cases the shared reference NodeSet already pins both backends, so this
job's unique value is the SMOKE cases (which have no reference): it is the only
check that the two backends agree there.

Usage: cross_backend_parity.py <artifacts_dir> [--strict]
  artifacts_dir holds one subdir per uploaded artifact:
  dump-<case>-<backend>-<os>[-<compiler>]/dump.xml
"""
import argparse
import os
import re
import subprocess
import sys

NODESET_COMPARE = '/opt/NodeSetTools/nodeset_compare.py'
_LABEL = re.compile(r'^dump-(?P<case>.+?)-(?P<backend>o6|uasdk)-(?P<os>[a-z0-9]+)(?:-(?P<compiler>\w+))?$')


def collect(artifacts_dir):
    """(case, os, compiler) -> {backend: dump_path}."""
    groups = {}
    if not os.path.isdir(artifacts_dir):
        return groups
    for name in os.listdir(artifacts_dir):
        match = _LABEL.match(name)
        if not match:
            continue
        dump = os.path.join(artifacts_dir, name, 'dump.xml')
        if os.path.exists(dump):
            key = (match['case'], match['os'], match['compiler'] or 'gcc')
            groups.setdefault(key, {})[match['backend']] = dump
    return groups


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('artifacts_dir')
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()

    pairs = [(k, v) for k, v in collect(args.artifacts_dir).items()
             if 'o6' in v and 'uasdk' in v]
    if not pairs:
        print('cross-backend parity: no o6+uasdk dump pairs to compare '
              '(both backends must run -- uasdk images are token-gated on this fork).')
        return 0

    any_diff = False
    for (case, os_name, compiler), backends in sorted(pairs):
        print('\n=== parity: {} ({}/{}) o6 vs uasdk ==='.format(case, os_name, compiler))
        result = subprocess.run(
            [sys.executable, NODESET_COMPARE, backends['o6'], backends['uasdk'],
             '--ignore_nodeids', 'StandardMetaData'])
        if result.returncode != 0:
            any_diff = True
            print('  -> diffs for {} ({}); review -- some are known-benign '
                  '((b) method-arg accesslevel, (c) browse-name ns).'.format(case, os_name))

    if any_diff and args.strict:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
