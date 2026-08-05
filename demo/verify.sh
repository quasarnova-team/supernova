#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
trap 'docker compose down -v >/dev/null 2>&1 || true' EXIT

diagnose() {
  echo "--- containers ---"
  docker compose ps -a || true
  for svc in supernova kilonova registry seed subscriber; do
    echo "--- $svc ---"
    docker compose logs "$svc" 2>/dev/null | tail -20 || true
  done
}

start=$(date +%s)
docker compose up -d
deadline=$((start + 60))
live=""
while [ "$(date +%s)" -lt "$deadline" ]; do
  distinct=$(docker compose logs subscriber 2>/dev/null \
    | { grep -oE 'counter=[0-9]+' || true; } | sort -u | wc -l)
  if [ "$distinct" -ge 2 ]; then live=yes; break; fi
  sleep 2
done
elapsed=$(( $(date +%s) - start ))

docker compose exec -T supernova cat /demo/Design.xml | diff - Design.xml \
  || { echo "FAIL: supernova unreachable or not built from this Design.xml"; diagnose; exit 1; }
docker compose ps --status running --format '{{.Service}}' | grep -q '^kilonova$' \
  || { echo "FAIL: kilonova is not serving"; diagnose; exit 1; }

docker compose logs subscriber | tail -3
if [ -z "$live" ]; then
  echo "FAIL: no changing counter values within 60s"
  diagnose
  exit 1
fi
echo "OK: one Design, the whole family, live values in ${elapsed}s (budget 60s)"
