#!/usr/bin/env python3
"""Enforce .github/fork-ci-inventory.yml.

supernova is a fork of quasar-team/quasar. Upstream workflows arrive here on
every upstream merge, including ones that only make sense in quasar -- they
watch quasar's CERN GitLab mirror or its self-hosted runner. Those fail on
secrets this repo does not have and file false alarms into this repo (issue #26).

This catches such a workflow at merge time. Three rules:

  1. Every workflow file on disk is accounted for in the inventory.
  2. A workflow listed under `absent` must not exist -- if an upstream merge
     brought a deleted one back, say so by name.
  3. A workflow may only reference secrets this repo actually has.
"""

import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
INVENTORY = ROOT / ".github" / "fork-ci-inventory.yml"
WORKFLOW_DIR = ROOT / ".github" / "workflows"

SECRET_RE = re.compile(r"secrets\.([A-Za-z_][A-Za-z0-9_]*)")
VALID = {"ours", "shared"}


def main():
    inventory = yaml.safe_load(INVENTORY.read_text())
    available = set(inventory.get("available_secrets") or [])
    declared = inventory.get("workflows") or {}
    absent = inventory.get("absent") or {}

    for path, disposition in declared.items():
        if disposition not in VALID:
            sys.exit(f"fork-ci-inventory.yml: {path} has unknown disposition "
                     f"'{disposition}' (expected one of {sorted(VALID)})")
    for path in sorted(set(declared) & set(absent)):
        sys.exit(f"fork-ci-inventory.yml: {path} is listed both as a workflow "
                 f"and as absent; it can only be one.")

    on_disk = {
        str(p.relative_to(ROOT))
        for p in sorted(WORKFLOW_DIR.glob("*.yml")) + sorted(WORKFLOW_DIR.glob("*.yaml"))
    }
    failures = []

    # Rule 2 -- deliberately deleted workflows must stay deleted.
    for path in sorted(set(absent) & on_disk):
        why = " ".join(str(absent[path]).split())
        failures.append(
            f"{path}\n"
            f"    Back on disk, but this repo deliberately does not run it:\n"
            f"      {why}\n"
            f"    An upstream merge reports `CONFLICT (modify/delete)` for this file;\n"
            f"    it looks like that was resolved by keeping upstream's copy. Delete it\n"
            f"    again:  git rm {path}"
        )

    # Rule 1 -- inventory and reality agree.
    for path in sorted(on_disk - set(declared) - set(absent)):
        failures.append(
            f"{path}\n"
            f"    Not listed in .github/fork-ci-inventory.yml. If it arrived with an\n"
            f"    upstream merge, decide whether supernova wants it, then add it under\n"
            f"    `workflows:` as `shared`, or delete it and list it under `absent:`."
        )
    for path in sorted(set(declared) - on_disk):
        failures.append(
            f"{path}\n"
            f"    Listed under `workflows:` but not on disk. Drop the line, or move it\n"
            f"    to `absent:` if it was deleted on purpose."
        )

    # Rule 3 -- secrets must resolve.
    for path in sorted(on_disk & set(declared)):
        text = (ROOT / path).read_text()
        try:
            yaml.safe_load(text)
        except yaml.YAMLError as exc:
            failures.append(f"{path}\n    Not parseable as YAML: {exc}")
            continue
        missing = sorted({m.group(1) for m in SECRET_RE.finditer(text)} - available)
        if missing:
            failures.append(
                f"{path}\n"
                f"    References secrets this repo does not have: {', '.join(missing)}\n"
                f"    Declared '{declared[path]}', so it runs here and can only fail.\n"
                f"    This is the signature of a quasar-only workflow inherited from\n"
                f"    upstream. Either add the secret to quasarnova-team/supernova, or\n"
                f"    delete the workflow and list it under `absent:` with the reason."
            )

    if failures:
        print("Fork CI hygiene: FAIL\n")
        for f in failures:
            print(f"  {f}\n")
        print("See .github/fork-ci-inventory.yml for what this repo runs and why.")
        return 1

    print(f"Fork CI hygiene: OK -- {len(on_disk)} workflows, all accounted for.")
    for path in sorted(declared):
        print(f"  {declared[path]:<8} {path}")
    for path in sorted(absent):
        print(f"  {'absent':<8} {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
