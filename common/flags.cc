#include "flags.hpp"
#include <gflags/gflags.h>

namespace im {

DEFINE_string(service_name, "user_server", "子服务名");
DEFINE_string(instance_id, "node1", "节点实例 id");
DEFINE_int32(listen_port, 10001, "brpc 监听端口");
DEFINE_int32(http_port, 9000, "网关 HTTP 端口");
DEFINE_int32(ws_port, 9001, "网关 WebSocket 端口");
DEFINE_string(register_host, "127.0.0.1", "注册到注册中心的可达地址");

DEFINE_string(etcd_endpoints, "http://127.0.0.1:2379", "etcd 地址，逗号分隔");
DEFINE_int32(etcd_lease_ttl, 10, "etcd 租约 TTL（秒）");
DEFINE_int32(etcd_keepalive_interval, 3, "etcd 租约续约间隔（秒）");

DEFINE_string(mysql_host, "127.0.0.1", "MySQL 地址");
DEFINE_int32(mysql_port, 3306, "MySQL 端口");
DEFINE_string(mysql_user, "root", "MySQL 用户");
DEFINE_string(mysql_password, "im123456", "MySQL 密码");
DEFINE_string(mysql_db, "im_system", "MySQL 库名");

DEFINE_string(redis_host, "127.0.0.1", "Redis 地址");
DEFINE_int32(redis_port, 6379, "Redis 端口");

DEFINE_string(rabbitmq_host, "127.0.0.1", "RabbitMQ 地址");
DEFINE_int32(rabbitmq_port, 5672, "RabbitMQ 端口");
DEFINE_string(rabbitmq_user, "guest", "RabbitMQ 用户");
DEFINE_string(rabbitmq_password, "guest", "RabbitMQ 密码");
DEFINE_string(rabbitmq_exchange, "im.events", "topic 交换机名");

DEFINE_string(es_host, "http://127.0.0.1:9200", "Elasticsearch 地址");

DEFINE_string(log_dir, "./logs", "日志目录");
DEFINE_string(log_level, "info", "日志级别 debug/info/warn/error");

DEFINE_string(storage_path, "./data/files", "文件存储根目录");

DEFINE_string(baidu_api_key, "", "百度云 ASR API Key");
DEFINE_string(baidu_secret_key, "", "百度云 ASR Secret Key");
DEFINE_int32(baidu_dev_pid, 1537, "百度云短语音识别 dev_pid（1537=普通话有标点16k）");

DEFINE_string(aliyun_sms_access_key_id, "", "阿里云短信 AccessKeyId");
DEFINE_string(aliyun_sms_access_key_secret, "", "阿里云短信 AccessKeySecret");
DEFINE_string(aliyun_sms_sign_name, "", "阿里云短信签名");
DEFINE_string(aliyun_sms_template_code, "", "阿里云短信验证码模板");

}  // namespace im
