#!/usr/bin/env python3
"""Expand .CI/test_cases/manifest.json into the GitHub Actions job matrix.

One declarative manifest -> one strategy.matrix, instead of hand-written jobs.
The SAME manifest is read by run_test_case.py --case (the driver), so each test
case is declared exactly once. Emits {"include": [ ...cells... ]} on stdout, for

    matrix: ${{ fromJSON(needs.matrix.outputs.include) }}

Modes:
  (default)         emit the matrix JSON on one line (for $GITHUB_OUTPUT)
  --print           human-readable cell list grouped by case + total
  --assert-count N  exit non-zero unless exactly N cells expand (migration guard)
  --check-paths     verify every design/config/oracle/device_sources path exists

Stdlib only (json) so it runs in any CI image without extra packages.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(HERE, 'test_cases')
MANIFEST = os.path.join(CASES_DIR, 'manifest.json')

_CC = {'gcc': '', 'clang': 'clang'}
_CXX = {'gcc': '', 'clang': 'clang++'}


def load():
    with open(MANIFEST) as handle:
        return json.load(handle)


def _cell(manifest, case, backend, image_key, compiler):
    name = case['name']
    label = '-'.join([name, backend, image_key] + ([compiler] if compiler != 'gcc' else []))
    return {
        'label': label,
        'case': name,
        'backend': backend,
        'image_tag': manifest['images'][image_key],
        'compiler': compiler,
        'cc': _CC[compiler],
        'cxx': _CXX[compiler],
        'compat_branch': manifest['backends'].get(backend, {}).get('compat_branch', ''),
    }


def expand(manifest):
    """Flatten cases x (backends on the default image + any extra_cells)."""
    cells = []
    for case in manifest['cases']:
        for backend in case.get('backends', []):
            cells.append(_cell(manifest, case, backend, 'default', 'gcc'))
        for extra in case.get('extra_cells', []):
            cells.append(_cell(manifest, case, extra['backend'],
                               extra.get('image', 'default'),
                               extra.get('compiler', 'gcc')))
    return cells


def check_paths(manifest):
    missing = []
    for case in manifest['cases']:
        for key in ('design', 'config', 'oracle'):
            rel = case.get(key)
            if rel and not os.path.exists(os.path.join(CASES_DIR, rel)):
                missing.append('{}.{} -> {}'.format(case['name'], key, rel))
        device_sources = case.get('device_sources')
        if device_sources and not os.path.isdir(os.path.join(CASES_DIR, device_sources)):
            missing.append('{}.device_sources -> {}/'.format(case['name'], device_sources))
    return missing


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--print', action='store_true', dest='do_print')
    parser.add_argument('--assert-count', type=int)
    parser.add_argument('--check-paths', action='store_true')
    args = parser.parse_args()

    manifest = load()
    cells = expand(manifest)

    if args.check_paths:
        missing = check_paths(manifest)
        if missing:
            print('manifest references missing files:', file=sys.stderr)
            for entry in missing:
                print('  ' + entry, file=sys.stderr)
            sys.exit(1)
        print('paths OK ({} cases)'.format(len(manifest['cases'])))
        return

    if args.assert_count is not None and len(cells) != args.assert_count:
        print('cell count {} != expected {}'.format(len(cells), args.assert_count), file=sys.stderr)
        sys.exit(1)

    if args.do_print:
        by_case = {}
        for cell in cells:
            by_case.setdefault(cell['case'], []).append(cell['label'])
        for case, labels in by_case.items():
            print('{} ({}):'.format(case, len(labels)))
            for label in labels:
                print('    ' + label)
        print('\ntotal cells: {}'.format(len(cells)))
    else:
        print(json.dumps({'include': cells}))


if __name__ == '__main__':
    main()
