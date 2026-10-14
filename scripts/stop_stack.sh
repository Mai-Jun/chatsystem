#!/usr/bin/env bash
# Staged (sequential) stop of the whole prod stack.
#
# Why not `docker compose stop`: it stops all containers in parallel, which makes
# Elasticsearch(JVM) and MySQL shut down (flush) simultaneously — on the 2-core/
# 1.6G box that IO+memory spike hangs sshd (deviation-log D8, fifth face of the
# same resource thrash). This script stops one container at a time with generous
# timeouts, keeping the machine responsive throughout.
#
# Usage (server, repo root):  bash scripts/stop_stack.sh
# Restart afterwards:         docker compose -f docker/docker-compose.prod.yml up -d
# ASCII only (docs/env.md pitfall 12).
set -uo pipefail

echo "=== state before stop ==="
docker ps --format "{{.Names}} {{.Status}}" | head -14 || true

# Stage 1: app services (fast shutdown, low IO)
for c in im-gateway-server im-file-server im-user-server im-friend-server \
         im-message-storage-server im-message-server im-speech-server; do
  docker stop -t 15 "$c" 2>&1 | tail -1
  sleep 2
done

# Stage 2: Elasticsearch (JVM - generous time to flush)
docker stop -t 45 im-es 2>&1 | tail -1
sleep 3

# Stage 3: message queue / cache / registry
for c in im-rabbitmq im-redis im-etcd; do
  docker stop -t 15 "$c" 2>&1 | tail -1
  sleep 2
done

# Stage 4: MySQL last (flush + clean shutdown)
docker stop -t 45 im-mysql 2>&1 | tail -1

echo "=== final state ==="
docker ps -q | wc -l | xargs echo "running containers:"
docker ps -a --format "{{.Names}} {{.Status}}" | head -14
free -m | head -2
