# ES 7.17.23 + IK 中文分词（插件版本必须与 ES 版本完全一致）
# 由 docker-compose.dev.yml 的 elasticsearch 服务 build 引用
FROM docker.elastic.co/elasticsearch/elasticsearch:7.17.23
RUN elasticsearch-plugin install --batch https://get.infini.cloud/elasticsearch/analysis-ik/7.17.23
