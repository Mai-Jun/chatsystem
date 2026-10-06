#pragma once
#include <gflags/gflags_declare.h>

// ============================================================
// 全项目 gflags 声明（定义在 flags.cc，默认值与 conf/*.flags 一致）
// 每个节点启动: ./xxx_server --flagfile=conf/common.flags --flagfile=conf/xxx.flags
// ============================================================
namespace im {

DECLARE_string(service_name);   // 子服务名（注册中心 key 与日志文件名）
DECLARE_string(instance_id);    // 节点实例 id（同服务多节点时唯一）
DECLARE_int32(listen_port);     // brpc 监听端口（子服务）
DECLARE_int32(http_port);       // 网关 HTTP
DECLARE_int32(ws_port);         // 网关 WebSocket
DECLARE_string(register_host);  // 注册到 etcd 的可达地址（默认 127.0.0.1，跨机部署改本机内网 IP）

// etcd
DECLARE_string(etcd_endpoints);        // 逗号分隔，目前取第一个
DECLARE_int32(etcd_lease_ttl);         // 租约秒数
DECLARE_int32(etcd_keepalive_interval);// 续约间隔秒数

// MySQL
DECLARE_string(mysql_host);
DECLARE_int32(mysql_port);
DECLARE_string(mysql_user);
DECLARE_string(mysql_password);
DECLARE_string(mysql_db);

// Redis
DECLARE_string(redis_host);
DECLARE_int32(redis_port);

// RabbitMQ
DECLARE_string(rabbitmq_host);
DECLARE_int32(rabbitmq_port);
DECLARE_string(rabbitmq_user);
DECLARE_string(rabbitmq_password);
DECLARE_string(rabbitmq_exchange);     // topic 交换机

// Elasticsearch
DECLARE_string(es_host);

// 日志
DECLARE_string(log_dir);
DECLARE_string(log_level);             // debug/info/warn/error

// 文件子服务
DECLARE_string(storage_path);          // 文件存储根目录

// 百度云 ASR（语音子服务）
DECLARE_string(baidu_api_key);
DECLARE_string(baidu_secret_key);
DECLARE_int32(baidu_dev_pid);  // 1537=普通话有标点16k

// 阿里云短信（用户子服务）
DECLARE_string(aliyun_sms_access_key_id);
DECLARE_string(aliyun_sms_access_key_secret);
DECLARE_string(aliyun_sms_sign_name);
DECLARE_string(aliyun_sms_template_code);

}  // namespace im
