#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

start=$(date +%s)
docker compose up -d
deadline=$((start + 60))
live=""
while [ "$(date +%s)" -lt "$deadline" ]; do
  distinct=$(docker compose logs subscriber 2>/dev/null | grep -oE 'counter=[0-9]+' | sort -u | wc -l)
  if [ "$distinct" -ge 2 ]; then live=yes; break; fi
  sleep 2
done
elapsed=$(( $(date +%s) - start ))

docker compose exec -T supernova cat /demo/Design.xml | diff - Design.xml \
  || { echo "FAIL: the supernova image was not built from this Design.xml"; docker compose down -v; exit 1; }
docker compose ps --status running --format '{{.Service}}' | grep -q '^kilonova$' \
  || { echo "FAIL: kilonova is not serving"; docker compose logs kilonova | tail -20; docker compose down -v; exit 1; }

docker compose logs subscriber | tail -3
docker compose down -v
if [ -z "$live" ]; then
  echo "FAIL: no changing counter values within 60s"
  exit 1
fi
echo "OK: one Design, the whole family, live values in ${elapsed}s (budget 60s)"
