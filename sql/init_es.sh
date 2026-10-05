#!/usr/bin/env bash
# 初始化 ES 历史消息索引（需 ES+IK 已启动：bash scripts/verify_infra.sh 先验证）
# 用法: bash sql/init_es.sh
set -euo pipefail

ES_HOST="${ES_HOST:-http://127.0.0.1:9200}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[init_es] 创建索引 im-message ..."
curl -fsSL -X PUT "$ES_HOST/im-message" \
  -H 'Content-Type: application/json' \
  --data-binary @"$SCRIPT_DIR/im-message.mapping.json"

echo "[init_es] 索引信息:"
curl -s "$ES_HOST/_cat/indices/im-message?v"
echo "[init_es] 完成"
