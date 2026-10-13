#!/usr/bin/env bash
# Safe build wrapper for the 2-core/1.6G server (see docs/deviation-log D1/D5/D7).
#
# Why: compiling brpc/protobuf TUs on this box peaks near 1GB per cc1plus. With
# Elasticsearch running (JVM + Lucene, ~530MB RSS) the box tips into swap
# thrash and sshd hangs for minutes-to-hours (happened 4 times: D1, D5, D7, and
# again on 2026-10-13). This wrapper makes the mitigation automatic: it stops
# ES, builds with -j1 + a 1100M memory cap, then starts ES again.
#
# Usage (run from the repo root on the server):
#   bash scripts/safe_build.sh                        # default target: all
#   bash scripts/safe_build.sh message_storage_server # single target
#   KEEP_ES=1 bash scripts/safe_build.sh foo          # skip ES stop/start (rarely correct)
# ASCII only (docs/env.md pitfall 12).
set -uo pipefail

TARGET="${1:-}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_DIR"

ES_WAS_RUNNING=0
if [ "${KEEP_ES:-0}" != "1" ]; then
  if docker inspect -f '{{.State.Running}}' im-es 2>/dev/null | grep -q true; then
    ES_WAS_RUNNING=1
    echo "[safe_build] stopping im-es to free memory"
    docker stop im-es >/dev/null
    sleep 2
  fi
  free -m | head -2
else
  echo "[safe_build] KEEP_ES=1: Elasticsearch left running (only safe for tiny incremental builds)"
fi

BUILD_ARGS=(--build build -j1)
if [ -n "$TARGET" ]; then
  BUILD_ARGS+=(--target "$TARGET")
fi

echo "[safe_build] cmake ${BUILD_ARGS[*]}"
systemd-run --scope --quiet -p MemoryMax=1100M nice -n 19 cmake "${BUILD_ARGS[@]}"
RC=$?
echo "[safe_build] build exit=$RC"

if [ "$ES_WAS_RUNNING" = "1" ]; then
  echo "[safe_build] restarting im-es"
  docker start im-es >/dev/null
  # wait for green briefly (do not fail the build if ES is slow)
  for i in $(seq 1 20); do
    s=$(curl -s -m 5 http://127.0.0.1:9200/_cluster/health 2>/dev/null | grep -o '"status":"[a-z]*"' | head -1)
    if [ "$s" = '"status":"green"' ] || [ "$s" = '"status":"yellow"' ]; then
      echo "[safe_build] ES back: $s"
      break
    fi
    sleep 5
  done
fi

exit $RC
