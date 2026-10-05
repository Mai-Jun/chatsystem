#!/usr/bin/env bash
# 验证基础设施五件套（在 WSL 内运行；容器由 docker/docker-compose.dev.yml 启动）
set -u

ok=0; fail=0
check() {
  local name="$1"; shift
  if "$@" >/dev/null 2>&1; then
    printf '[OK]   %s\n' "$name"; ok=$((ok+1))
  else
    printf '[FAIL] %s\n' "$name"; fail=$((fail+1))
  fi
}

check "MySQL     :3306  mysqladmin ping"  docker exec im-mysql mysqladmin ping -h127.0.0.1 -uroot -pim123456
check "Redis     :6379  ping"             docker exec im-redis redis-cli ping
check "etcd      :2379  /health"          curl -sf http://127.0.0.1:2379/health
check "RabbitMQ  :5672  diagnostics"      docker exec im-rabbitmq rabbitmq-diagnostics -q ping
check "RabbitMQ  :15672 管理台 API"        curl -sf -uguest:guest http://127.0.0.1:15672/api/overview
check "ES        :9200  集群健康"          curl -sf http://127.0.0.1:9200/_cluster/health
IK=$(curl -sf http://127.0.0.1:9200/_cat/plugins 2>/dev/null | grep -c analysis-ik || true)
if [ "${IK:-0}" -ge 1 ]; then
  printf '[OK]   ES        IK 中文分词插件\n'; ok=$((ok+1))
else
  printf '[FAIL] ES        IK 中文分词插件\n'; fail=$((fail+1))
fi

echo "--------------------"
echo "通过 $ok 项，失败 $fail 项"
[ "$fail" -eq 0 ]
