#!/usr/bin/env python3
"""Expand .CI/test_cases/manifest.json into the GitHub Actions job matrix.

One declarative manifest -> one strategy.matrix, instead of hand-written jobs.
The SAME manifest is read by run_test_case.py --case (the driver), so each test
case is declared exactly once. Emits {"include": [ ...cells... ]} on stdout, for

    matrix: ${{ fromJSON(needs.matrix.outputs.include) }}

A cell is (case x backend x os x compiler). Its CI image is
    ghcr.io/<owner>/quasar-ci:<os>-<backend>[-<toolkit_version>]
computed here as the `image` field (the workflow prepends ghcr.io/<owner>/...).

Filters (compose; also read from env so the workflow can pass them as vars):
    --backends o6,uasdk     QUASAR_CI_BACKENDS   restrict to these backends
    --os alma9,alma10       QUASAR_CI_OS         restrict to these operating systems
    --tier pr,nightly       QUASAR_CI_TIERS      restrict to these tiers (default tier: pr)

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


def _image(os_name, backend, toolkit_version):
    """ghcr.io/<owner>/quasar-ci:<image>. o6 needs no toolkit; uasdk pins a version."""
    if backend == 'uasdk' and toolkit_version:
        return '{}-{}-{}'.format(os_name, backend, toolkit_version)
    return '{}-{}'.format(os_name, backend)


def _cell(manifest, case, backend, os_name, compiler, toolkit_version, tier):
    name = case['name']
    parts = [name, backend, os_name] + ([compiler] if compiler != 'gcc' else [])
    if toolkit_version:
        parts.append(toolkit_version)
    return {
        'label': '-'.join(parts),
        'case': name,
        'backend': backend,
        'os': os_name,
        'compiler': compiler,
        'toolkit_version': toolkit_version or '',
        'tier': tier,
        'image': _image(os_name, backend, toolkit_version),
        'cc': _CC[compiler],
        'cxx': _CXX[compiler],
        'compat_branch': manifest['backends'].get(backend, {}).get('compat_branch', ''),
    }


def expand(manifest):
    """Flatten cases x backends x (uasdk toolkit versions) x os/compiler, with tiers.

    A backend listed in manifest['toolkits'] (uasdk) fans out into one cell per
    toolkit version, each carrying that version's tier; a backend without toolkit
    entries (o6) yields a single cell at the cell's base tier."""
    default_os = manifest.get('default_os', 'alma9')
    toolkits = manifest.get('toolkits', {})

    def emit(case, backend, os_name, compiler, base_tier):
        versions = toolkits.get(backend)
        if versions:
            return [_cell(manifest, case, backend, os_name, compiler,
                          tk['version'], tk.get('tier', base_tier)) for tk in versions]
        return [_cell(manifest, case, backend, os_name, compiler, None, base_tier)]

    cells = []
    for case in manifest['cases']:
        case_tier = case.get('tier', 'pr')
        for backend in case.get('backends', []):
            cells += emit(case, backend, default_os, 'gcc', case_tier)
        for extra in case.get('extra_cells', []):
            cells += emit(case, extra['backend'], extra.get('os', default_os),
                          extra.get('compiler', 'gcc'), extra.get('tier', case_tier))
    return cells


def _csv_env(cli_value, env_name):
    raw = cli_value if cli_value is not None else os.environ.get(env_name)
    if not raw:
        return None
    return {item.strip() for item in raw.split(',') if item.strip()}


def apply_filters(cells, backends, oses, tiers, versions=None):
    if backends:
        cells = [c for c in cells if c['backend'] in backends]
    if oses:
        cells = [c for c in cells if c['os'] in oses]
    if tiers:
        cells = [c for c in cells if c['tier'] in tiers]
    if versions:
        # Only narrows version-bearing (uasdk) cells; version-less cells (o6) pass through,
        # so this composes with --backends (e.g. uasdk + 1.8.9,2.0.2 -> just those cells).
        cells = [c for c in cells if (not c['toolkit_version']) or c['toolkit_version'] in versions]
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
    parser.add_argument('--backends')
    parser.add_argument('--os', dest='oses')
    parser.add_argument('--tier', dest='tiers')
    parser.add_argument('--versions', help='restrict uasdk toolkit versions (e.g. 1.8.9,2.0.2); o6 cells unaffected')
    args = parser.parse_args()

    manifest = load()

    if args.check_paths:
        missing = check_paths(manifest)
        if missing:
            print('manifest references missing files:', file=sys.stderr)
            for entry in missing:
                print('  ' + entry, file=sys.stderr)
            sys.exit(1)
        print('paths OK ({} cases)'.format(len(manifest['cases'])))
        return

    cells = apply_filters(
        expand(manifest),
        _csv_env(args.backends, 'QUASAR_CI_BACKENDS'),
        _csv_env(args.oses, 'QUASAR_CI_OS'),
        _csv_env(args.tiers, 'QUASAR_CI_TIERS'),
        _csv_env(args.versions, 'QUASAR_CI_VERSIONS'),
    )

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
