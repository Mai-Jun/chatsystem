#!/usr/bin/env bash
# Prepare the build context for the runtime image and build im-runtime:1.0.
# Run on the server from the repo root:  bash scripts/prepare_runtime_image.sh
# ASCII only (see docs/env.md pitfall 12).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CTX="$REPO_DIR/docker"
LIBDIR="$CTX/lib"

echo "[prepare] collect /usr/local dynamic libs into $LIBDIR"
mkdir -p "$LIBDIR"
# protobuf 3.20.2 (so.31) and redis++ (so.1): server-only build artifacts, not in apt.
for f in /usr/local/lib/libprotobuf.so.31 /usr/local/lib/libprotobuf.so.31.0.2 \
         /usr/local/lib/libredis++.so.1 /usr/local/lib/libredis++.so.1.3.15; do
  if [ -e "$f" ]; then
    cp -a "$f" "$LIBDIR/"
  else
    echo "[prepare] WARN missing: $f"
  fi
done
ls -l "$LIBDIR"

echo "[prepare] docker build -t im-runtime:1.0"
docker build -f "$CTX/runtime.Dockerfile" -t im-runtime:1.0 "$CTX"

echo "[prepare] done: im-runtime:1.0"
