#!/usr/bin/env bash
# M10: switch the server from the dev form (bare processes + dev compose infra)
# to the prod form (im-system-prod: all services in containers, restart policy).
# Run on the server from the repo root:  bash scripts/switch_to_prod.sh
# ASCII only (docs/env.md pitfall 12).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_DIR"
PROD="docker/docker-compose.prod.yml"

echo "[switch] 1/5 stop bare app services (dev form)"
for s in file_server user_server friend_server message_storage_server message_server gateway_server speech_server; do
  pkill -f "build/server/$s/" 2>/dev/null || true
done
sleep 2

echo "[switch] 2/5 down dev infra stack (volumes are kept)"
docker compose -f docker/docker-compose.dev.yml down

echo "[switch] 3/5 build images if missing"
docker image inspect im-runtime:1.0 >/dev/null 2>&1 || {
  echo "[switch] im-runtime:1.0 missing - run scripts/prepare_runtime_image.sh first" >&2
  exit 1
}
docker image inspect im-es-7.17.23-ik >/dev/null 2>&1 || \
  docker compose -f "$PROD" build elasticsearch

echo "[switch] 4/5 up prod stack"
docker compose -f "$PROD" up -d

echo "[switch] 5/5 wait and report"
sleep 15
docker compose -f "$PROD" ps
echo "--- gateway health ---"
curl -s -m 10 http://127.0.0.1:9000/health && echo " OK" || echo "NOT READY YET"
