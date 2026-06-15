#!/usr/bin/env python3
"""Build one quasar framework test case with one OPC-UA backend, then judge it
by its declared kind.

A case is declared once in .CI/test_cases/manifest.json and selected with
--case <name>; the same file drives the CI matrix via .CI/expand_matrix.py. The
legacy explicit flags (--design/--config/--compare_with_nodeset/
--generate_all_devices) still work for ad-hoc runs and override the manifest.

Pipeline: select backend -> [copy design] -> [generate device --all] ->
[overlay device sources] -> build -> [install config] -> run + dump address
space -> evaluate. Evaluation is by kind: 'oracle' compares the NodeSet2 dump
against a reference; 'smoke' is satisfied by a clean build + run.
"""
import argparse
import os
import shutil
import sys
import json
from colorama import Fore, Style

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(SCRIPT_DIR, 'test_cases')
MANIFEST = os.path.join(CASES_DIR, 'manifest.json')


def invoke_and_check(cmd):
    print(f'{Fore.BLUE}{Style.BRIGHT}Will invoke{Style.RESET_ALL} {cmd}')
    ret_val = os.system(cmd)
    if ret_val != 0:
        raise Exception(f'Stopping because the command {cmd} returned wrong return value of {ret_val}')


def clone_quasar(test_branch):
    invoke_and_check(f'git clone --recursive -b {test_branch} --depth=1 https://github.com/quasar-team/quasar.git')
    os.chdir('quasar')


def prepare_opcua_backend(opcua_backend, open62541_compat_branch):
    if opcua_backend == 'o6':
        invoke_and_check(f'./quasar.py enable_module open62541-compat {open62541_compat_branch}')
        invoke_and_check('./quasar.py set_build_config open62541_config.cmake')
    elif opcua_backend == 'uasdk':
        invoke_and_check('./quasar.py set_build_config .CI/travis/build_configs/uasdk-eval.cmake')
    else:
        raise Exception(f"OPCUA backend {opcua_backend} was not recognized")


def generate_all_devices():
    invoke_and_check('./quasar.py generate device --all')


def overlay_device_sources(device_sources_dir):
    """Drop hand-written D<Class> sources into Device/, stripping the '.test'
    marker (DTestClass.test.h -> Device/include/DTestClass.h). Lets a single
    invocation cover both backends -- no per-backend overlay-and-rebuild dance."""
    src_dir = os.path.join(CASES_DIR, device_sources_dir)
    os.makedirs(os.path.join('Device', 'include'), exist_ok=True)
    os.makedirs(os.path.join('Device', 'src'), exist_ok=True)
    overlaid = []
    for fname in sorted(os.listdir(src_dir)):
        if fname.endswith('.test.h'):
            dest = os.path.join('Device', 'include', fname.replace('.test.', '.'))
        elif fname.endswith('.test.cpp'):
            dest = os.path.join('Device', 'src', fname.replace('.test.', '.'))
        else:
            continue
        shutil.copyfile(os.path.join(src_dir, fname), dest)
        overlaid.append(dest)
    if not overlaid:
        raise Exception(f'device_sources {device_sources_dir} contained no *.test.{{h,cpp}} files')
    print(f'{Fore.CYAN}Overlaid device sources{Style.RESET_ALL} {overlaid}')


def build():
    invoke_and_check('./quasar.py build Release')


def run_and_dump_address_space():
    invoke_and_check('./.CI/travis/server_fixture.py --command_to_run uasak_dump')


def compare_with_nodeset(reference_ns):
    invoke_and_check(f'/opt/NodeSetTools/nodeset_compare.py {reference_ns} build/bin/dump.xml --ignore_nodeids StandardMetaData')


def resolve_case(case_name, manifest_backends):
    with open(MANIFEST) as handle:
        manifest = json.load(handle)
    for case in manifest['cases']:
        if case['name'] == case_name:
            return case
    raise Exception(f'case {case_name} not found in {MANIFEST}')


def _case_path(rel):
    return os.path.join(CASES_DIR, rel)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--clone', action='store_true')
    parser.add_argument('--quasar_branch', default='master')
    parser.add_argument('--opcua_backend', '--backend', dest='opcua_backend')
    parser.add_argument('--open62541_compat_branch')
    parser.add_argument('--case', help='case name in .CI/test_cases/manifest.json')
    # Legacy explicit flags (still honoured; override the manifest for ad-hoc runs)
    parser.add_argument('--design', default=None)
    parser.add_argument('--config', default=None)
    parser.add_argument('--device_sources', default=None)
    parser.add_argument('--compare_with_nodeset', default=None)
    parser.add_argument('--generate_all_devices', action='store_true')
    args = parser.parse_args()

    if args.clone:
        clone_quasar(args.quasar_branch)

    design = args.design
    config = args.config
    device_sources = args.device_sources
    oracle = args.compare_with_nodeset
    generate_devices = args.generate_all_devices
    compat_branch = args.open62541_compat_branch

    if args.case:
        case = resolve_case(args.case, None)
        design = _case_path(case['design']) if case.get('design') else None
        config = _case_path(case['config']) if case.get('config') else None
        device_sources = case.get('device_sources')
        oracle = _case_path(case['oracle']) if case.get('oracle') else None
        generate_devices = bool(case.get('generate_devices'))
        if not compat_branch and args.opcua_backend == 'o6':
            with open(MANIFEST) as handle:
                compat_branch = json.load(handle)['backends']['o6'].get('compat_branch')

    prepare_opcua_backend(args.opcua_backend, compat_branch)

    if design:
        shutil.copyfile(design, 'Design/Design.xml')

    if generate_devices:
        generate_all_devices()

    if device_sources:
        overlay_device_sources(device_sources)

    build()

    if config:
        shutil.copyfile(config, 'build/bin/config.xml')

    run_and_dump_address_space()

    if oracle:
        compare_with_nodeset(oracle)
    else:
        print(f'{Fore.CYAN}Smoke case: clean build + run was the assertion (no NodeSet reference).{Style.RESET_ALL}')


if __name__ == "__main__":
    try:
        main()
    except Exception as ex:
        print(f'{Fore.RED}Caught exception{Style.RESET_ALL} {str(ex)}, returning as failed')
        sys.exit(1)
