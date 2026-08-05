#!/usr/bin/env python3
"""The Pub/Sub documentation cannot drift from the artifacts it documents.

Two invariants: every literalinclude in Documentation/source resolves to a
real file (the tutorial owns no copies — only pointers into the CI-gated
demo), and every attribute the generated configuration schema declares for
the PubSub section is mentioned in PubSub.rst's reference, so a schema
change without documentation fails the build.
"""

import os
import re
import sys

QUASAR_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(QUASAR_ROOT, 'Documentation', 'source')
TEMPLATE = os.path.join(QUASAR_ROOT, 'Configuration', 'templates',
                        'designToConfigurationXSD.jinja')


def literalincludes_resolve():
    ok = True
    count = 0
    for name in sorted(os.listdir(DOCS)):
        if not name.endswith('.rst'):
            continue
        text = open(os.path.join(DOCS, name), encoding='utf-8').read()
        for target in re.findall(r'^\.\. literalinclude:: (\S+)', text, re.M):
            count += 1
            path = os.path.normpath(os.path.join(DOCS, target))
            if not os.path.isfile(path):
                print('FAIL: %s includes %s, which does not exist - the doc '
                      'points at artifacts, it never copies them; fix the '
                      'path or restore the artifact' % (name, target))
                ok = False
    if ok:
        print('OK: all %d literalinclude targets resolve' % count)
    return ok


def pubsub_reference_complete():
    jinja = open(TEMPLATE, encoding='utf-8').read()
    attrs = set()
    for _, block in re.findall(
            r'<xs:complexType name="(PubSub\w*)">(.*?)</xs:complexType>',
            jinja, re.S):
        attrs.update(re.findall(r'<xs:attribute name="(\w+)"', block))
    doc = open(os.path.join(DOCS, 'PubSub.rst'), encoding='utf-8').read()
    missing = sorted(a for a in attrs if ('``%s``' % a) not in doc)
    if missing:
        print('FAIL: the generated schema declares PubSub attribute(s) %s '
              'that PubSub.rst never mentions - document them in the '
              'configuration reference' % ', '.join(missing))
        return False
    print('OK: all %d PubSub schema attributes documented' % len(attrs))
    return True


results = [literalincludes_resolve(), pubsub_reference_complete()]
sys.exit(0 if all(results) else 1)
