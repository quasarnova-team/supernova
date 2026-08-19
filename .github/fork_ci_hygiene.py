#!/usr/bin/env python3
"""Enforce .github/fork-ci-inventory.yml.

supernova is a fork of quasar-team/quasar. Upstream workflows arrive here on
every upstream merge, including ones that only make sense in quasar (they watch
quasar's CERN GitLab mirror or its self-hosted runner). Those fail on secrets
this repo does not have and file false alarms into this repo.

This catches such a workflow at merge time. Two rules:

  1. Every workflow file is accounted for in the inventory.
  2. A workflow may only reference secrets this repo actually has -- unless it
     is declared `upstream-only-guarded`, in which case every one of its jobs
     must carry an `if: github.repository == ...` guard so it self-skips here.
"""

import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
INVENTORY = ROOT / ".github" / "fork-ci-inventory.yml"
WORKFLOW_DIR = ROOT / ".github" / "workflows"

GUARD_RE = re.compile(r"github\.repository\s*==")
SECRET_RE = re.compile(r"secrets\.([A-Za-z_][A-Za-z0-9_]*)")
VALID = {"ours", "shared", "upstream-only-guarded"}


def guarded_jobs(doc):
    """Return (guarded, unguarded) job names for a parsed workflow."""
    jobs = (doc or {}).get("jobs") or {}
    guarded, unguarded = [], []
    for name, body in jobs.items():
        cond = str((body or {}).get("if", ""))
        (guarded if GUARD_RE.search(cond) else unguarded).append(name)
    return guarded, unguarded


def main():
    inventory = yaml.safe_load(INVENTORY.read_text())
    available = set(inventory.get("available_secrets") or [])
    declared = inventory.get("workflows") or {}

    for path, disposition in declared.items():
        if disposition not in VALID:
            sys.exit(f"{INVENTORY.name}: {path} has unknown disposition "
                     f"'{disposition}' (expected one of {sorted(VALID)})")

    on_disk = {
        str(p.relative_to(ROOT))
        for p in sorted(WORKFLOW_DIR.glob("*.yml")) + sorted(WORKFLOW_DIR.glob("*.yaml"))
    }
    failures = []

    # Rule 1 -- inventory and reality agree.
    for path in sorted(on_disk - set(declared)):
        failures.append(
            f"{path}\n"
            f"    Not listed in .github/fork-ci-inventory.yml. If it arrived with an\n"
            f"    upstream merge, decide whether supernova wants it, then add a line:\n"
            f"      {path}: shared            # we want it, it runs here\n"
            f"      {path}: upstream-only-guarded   # quasar-only, must self-skip"
        )
    for path in sorted(set(declared) - on_disk):
        failures.append(
            f"{path}\n"
            f"    Listed in the inventory but no longer on disk. Drop the line."
        )

    # Rule 2 -- secrets must resolve, or the workflow must self-skip.
    for path in sorted(on_disk & set(declared)):
        text = (ROOT / path).read_text()
        try:
            doc = yaml.safe_load(text)
        except yaml.YAMLError as exc:
            failures.append(f"{path}\n    Not parseable as YAML: {exc}")
            continue

        guarded, unguarded = guarded_jobs(doc)
        disposition = declared[path]
        missing = sorted({m.group(1) for m in SECRET_RE.finditer(text)} - available)

        if disposition == "upstream-only-guarded":
            if unguarded:
                failures.append(
                    f"{path}\n"
                    f"    Declared upstream-only-guarded, but these jobs have no\n"
                    f"    `if: github.repository == ...` guard and would run here:\n"
                    f"      {', '.join(sorted(unguarded))}\n"
                    f"    The guard belongs upstream in quasar-team/quasar so this file\n"
                    f"    stays identical to upstream's. It may have been lost in a merge."
                )
        elif missing:
            failures.append(
                f"{path}\n"
                f"    References secrets this repo does not have: {', '.join(missing)}\n"
                f"    Declared '{disposition}', so every job runs here and will fail.\n"
                f"    This is the signature of a quasar-only workflow inherited from\n"
                f"    upstream. Either add the secret to quasarnova-team/supernova, or\n"
                f"    guard the workflow upstream and mark it upstream-only-guarded here."
            )

    if failures:
        print("Fork CI hygiene: FAIL\n")
        for f in failures:
            print(f"  {f}\n")
        print("See .github/fork-ci-inventory.yml for what each disposition means.")
        return 1

    print(f"Fork CI hygiene: OK -- {len(on_disk)} workflows, all accounted for.")
    for path in sorted(declared):
        print(f"  {declared[path]:<22} {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
