// M1 公共库测试
// - UtilTest/LoggerTest: 纯单元测试
// - RedisTest/EtcdRegistryTest/MqRoundtripTest/OdbTest: 集成测试，依赖基础设施
//   （基础设施未启动时自动跳过，不算失败）
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "flags.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "etcd_client.hpp"
#include "channel_manager.hpp"
#include "redis_client.hpp"
#include "mq.hpp"
#include "es_client.hpp"
#include "odb/database.hpp"
#include "odb/entities.hpp"
#include "entities-odb.hxx"

namespace {

using namespace im;  // NOLINT

// 原始 TCP 探测（Redis/RabbitMQ 不是 HTTP，httplib 探测不可靠）
bool tcp_open(int port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  timeval tv{};
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
  ::close(fd);
  return ok;
}

std::mutex g_mutex;
std::condition_variable g_cv;
std::string g_received;

// ---------------- Util ----------------

TEST(UtilTest, UuidUniqueAndShaped) {
  std::set<std::string> seen;
  for (int i = 0; i < 1000; ++i) {
    auto u = uuid();
    EXPECT_EQ(u.size(), 36);
    EXPECT_EQ(u[8], '-');
    seen.insert(u);
  }
  EXPECT_EQ(seen.size(), 1000);
}

TEST(UtilTest, Base64Roundtrip) {
  for (int len : {0, 1, 2, 3, 10, 57, 100, 1000}) {
    std::string raw;
    for (int i = 0; i < len; ++i) raw.push_back(static_cast<char>((i * 37 + len) % 256));
    auto enc = base64_encode(raw);
    EXPECT_EQ(base64_decode(enc), raw);
  }
}

TEST(UtilTest, SplitString) {
  auto v = split_string("a,,b,c", ",");
  ASSERT_EQ(v.size(), 3);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
  EXPECT_TRUE(split_string("", ",").empty());
  EXPECT_TRUE(has_prefix("/im/registry/user", "/im/"));
}

// ---------------- Redis ----------------

TEST(RedisTest, SetGetDel) {
  if (!tcp_open(FLAGS_redis_port)) GTEST_SKIP() << "Redis 未启动";
  RedisClient r;
  ASSERT_TRUE(r.set("test:key1", "hello"));
  auto v = r.get("test:key1");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, "hello");
  ASSERT_TRUE(r.setex("test:key2", "temp", 2));
  EXPECT_TRUE(r.exists("test:key2"));
  ASSERT_TRUE(r.del("test:key1"));
  EXPECT_FALSE(r.get("test:key1").has_value());
  ASSERT_TRUE(r.expire("test:key2", 10));
  ASSERT_TRUE(r.del("test:key2"));
}

// ---------------- etcd 注册与发现 ----------------

TEST(EtcdTest, RegistryAndDiscovery) {
  if (!tcp_open(2379)) GTEST_SKIP() << "etcd 未启动";

  ServiceRegistry reg(FLAGS_etcd_endpoints, "test_service", "node_ut", "127.0.0.1", 19999, 10, 2);
  ASSERT_TRUE(reg.start());

  std::vector<std::string> found;
  for (int i = 0; i < 20 && found.empty(); ++i) {
    std::map<std::string, std::string> kv;
    EtcdClient cli(FLAGS_etcd_endpoints);
    ASSERT_TRUE(cli.range_prefix("/im/registry/test_service/", &kv));
    for (auto& [k, v] : kv) found.push_back(v);
    if (found.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  ASSERT_FALSE(found.empty());
  EXPECT_EQ(found[0], "127.0.0.1:19999");

  reg.stop();
  std::map<std::string, std::string> kv;
  EtcdClient cli(FLAGS_etcd_endpoints);
  ASSERT_TRUE(cli.range_prefix("/im/registry/test_service/", &kv));
  EXPECT_TRUE(kv.empty());
}

TEST(EtcdTest, ChannelManagerBuilds) {
  if (!tcp_open(2379)) GTEST_SKIP() << "etcd 未启动";
  EtcdClient cli(FLAGS_etcd_endpoints);
  ASSERT_TRUE(cli.put("/im/registry/test_channel/n1", "127.0.0.1:19998"));
  ChannelManager mgr(FLAGS_etcd_endpoints);
  mgr.discover("test_channel");
  brpc::Channel* ch = nullptr;
  for (int i = 0; i < 20 && ch == nullptr; ++i) {
    ch = mgr.channel("test_channel");
    if (ch == nullptr) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  EXPECT_NE(ch, nullptr);
  cli.del_prefix("/im/registry/test_channel/");
}

// ---------------- RabbitMQ ----------------

TEST(MqTest, PublishSubscribeRoundtrip) {
  if (!tcp_open(FLAGS_rabbitmq_port)) GTEST_SKIP() << "RabbitMQ 未启动";

  MqSubscriber sub("127.0.0.1", FLAGS_rabbitmq_port, FLAGS_rabbitmq_user,
                   FLAGS_rabbitmq_password, FLAGS_rabbitmq_exchange, "test_q_ut", "test.route",
                   [](const std::string& rk, const std::string& body) {
                     std::lock_guard<std::mutex> lk(g_mutex);
                     g_received = rk + "|" + body;
                     g_cv.notify_all();
                   });
  ASSERT_TRUE(sub.start());

  MqPublisher pub(FLAGS_rabbitmq_exchange);
  ASSERT_TRUE(pub.connect());
  ASSERT_TRUE(pub.publish("test.route", "ping-ut"));

  {
    std::unique_lock<std::mutex> lk(g_mutex);
    EXPECT_TRUE(g_cv.wait_for(lk, std::chrono::seconds(5), [] { return !g_received.empty(); }));
  }
  EXPECT_EQ(g_received, "test.route|ping-ut");
  sub.stop();
}

// ---------------- ODB / MySQL ----------------

TEST(OdbTest, UserCrud) {
  std::unique_ptr<odb::mysql::database> db;
  try {
    db = create_db();
    odb::transaction probe(db->begin());
    probe.commit();
  } catch (const std::exception& e) {
    GTEST_SKIP() << "MySQL 未启动: " << e.what();
  }

  std::string uid = uuid();
  {
    odb::transaction t(db->begin());
    db->persist(User(uid, "测试用户", "单测", "19900000000", "hash", ""));
    t.commit();
  }
  {
    odb::transaction t(db->begin());
    auto u = db->query_one<User>(odb::query<User>::phone == "19900000000");
    ASSERT_TRUE(u);
    EXPECT_EQ(u->id(), uid);
    EXPECT_EQ(u->nickname(), "测试用户");
    t.commit();
  }
  {
    odb::transaction t(db->begin());
    auto u = db->query_one<User>(odb::query<User>::phone == "19900000000");
    ASSERT_TRUE(u);
    u->nickname("测试用户改");
    u->touch();
    db->update(*u);
    t.commit();
  }
  {
    odb::transaction t(db->begin());
    auto u = db->query_one<User>(odb::query<User>::id == uid);
    ASSERT_TRUE(u);
    EXPECT_EQ(u->nickname(), "测试用户改");
    t.commit();
  }
  {
    odb::transaction t(db->begin());
    db->erase<User>(uid);
    t.commit();
  }
  {
    odb::transaction t(db->begin());
    EXPECT_FALSE(db->query_one<User>(odb::query<User>::id == uid));
    t.commit();
  }
}

}  // namespace
