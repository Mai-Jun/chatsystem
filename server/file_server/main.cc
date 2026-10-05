#include <brpc/server.h>
#include <gflags/gflags.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "file.pb.h"
#include "flags.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "etcd_client.hpp"
#include "odb/database.hpp"
#include "odb/entities.hpp"
#include "entities-odb.hxx"

// ============================================================
// 文件子服务：单/批量上传下载（本地磁盘 + MySQL 元数据）
// 文件实体路径: storage_path/<file_id>；元数据表 file
// ============================================================
namespace im {

namespace fs = std::filesystem;

class FileServiceImpl : public FileService {
 public:
  explicit FileServiceImpl(odb::mysql::database* db) : db_(db) {}

  void PutSingle(google::protobuf::RpcController* cntl_base, const PutSingleReq* req,
                 PutSingleResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl_base;

    if (req->data().file_content().empty() || req->data().file_name().empty()) {
      resp->set_success(false);
      resp->set_errmsg("文件名或内容为空");
      return;
    }
    std::string file_id = uuid();
    std::string path = FLAGS_storage_path + "/" + file_id;
    if (!write_file(path, req->data().file_content())) {
      LOG_ERROR("文件写入磁盘失败: {}", path);
      resp->set_success(false);
      resp->set_errmsg("文件写入磁盘失败");
      return;
    }
    try {
      odb::transaction t(db_->begin());
      db_->persist(FileMeta(file_id, req->data().file_name(),
                            static_cast<int64_t>(req->data().file_content().size()), file_id));
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("文件元数据入库失败: {}", e.what());
      std::filesystem::remove(path);
      resp->set_success(false);
      resp->set_errmsg("文件元数据入库失败");
      return;
    }
    LOG_INFO("上传成功: id={} name={} size={}", file_id, req->data().file_name(),
             req->data().file_content().size());
    resp->set_success(true);
    resp->set_file_id(file_id);
  }

  void PutBatch(google::protobuf::RpcController* cntl_base, const PutBatchReq* req,
                PutBatchResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl_base;

    for (const auto& data : req->data()) {
      PutSingleReq single;
      *single.mutable_data() = data;
      PutSingleResp one;
      PutSingle(cntl_base, &single, &one, nullptr /* 由本方法管理 done */);
      if (!one.success()) {
        resp->set_success(false);
        resp->set_errmsg("批量上传中止: " + one.errmsg() + " (" + data.file_name() + ")");
        return;
      }
      resp->add_file_ids(one.file_id());
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  void GetSingle(google::protobuf::RpcController* cntl_base, const GetSingleReq* req,
                 GetSingleResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl_base;

    FileMeta meta;
    {
      try {
        odb::transaction t(db_->begin());
        auto p = db_->query_one<FileMeta>(odb::query<FileMeta>::id == req->file_id());
        if (!p) {
          resp->set_success(false);
          resp->set_errmsg("文件不存在: " + req->file_id());
          return;
        }
        meta = *p;
        t.commit();
      } catch (const std::exception& e) {
        LOG_ERROR("文件元数据查询失败: {}", e.what());
        resp->set_success(false);
        resp->set_errmsg("文件元数据查询失败");
        return;
      }
    }
    std::string content;
    if (!read_file(FLAGS_storage_path + "/" + meta.file_path(), &content)) {
      LOG_ERROR("文件读取失败: {}", meta.file_path());
      resp->set_success(false);
      resp->set_errmsg("文件读取失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
    resp->mutable_data()->set_file_id(meta.id());
    resp->mutable_data()->set_file_name(meta.file_name());
    resp->mutable_data()->set_file_size(meta.file_size());
    resp->mutable_data()->set_file_content(content);
  }

  void GetBatch(google::protobuf::RpcController* cntl_base, const GetBatchReq* req,
                GetBatchResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl_base;

    for (const auto& fid : req->file_ids()) {
      GetSingleReq single;
      single.set_file_id(fid);
      GetSingleResp one;
      GetSingle(cntl_base, &single, &one, nullptr);
      if (!one.success()) {
        LOG_WARN("批量获取跳过缺失文件: {}", fid);
        continue;
      }
      *resp->add_data() = one.data();
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

 private:
  odb::mysql::database* db_;
};

}  // namespace im

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  im::init_logger(FLAGS_log_dir, FLAGS_service_name, FLAGS_log_level);
  LOG_INFO("{} 启动, 端口 {}", FLAGS_service_name, FLAGS_listen_port);

  // 存储目录
  std::filesystem::create_directories(FLAGS_storage_path);

  // 数据库
  auto db = im::create_db();

  // 注册中心
  im::ServiceRegistry registry(FLAGS_etcd_endpoints, FLAGS_service_name, FLAGS_instance_id,
                               FLAGS_register_host, FLAGS_listen_port, FLAGS_etcd_lease_ttl,
                               FLAGS_etcd_keepalive_interval);
  if (!registry.start()) {
    LOG_ERROR("注册失败，退出");
    return 1;
  }

  // RPC 服务
  brpc::Server server;
  im::FileServiceImpl impl(db.get());
  if (server.AddService(&impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
    LOG_ERROR("AddService 失败");
    return 1;
  }
  brpc::ServerOptions options;
  if (server.Start(FLAGS_listen_port, &options) != 0) {
    LOG_ERROR("brpc 启动失败: 端口 {}", FLAGS_listen_port);
    return 1;
  }
  LOG_INFO("{} 就绪: http://{}:{}", FLAGS_service_name, FLAGS_register_host, FLAGS_listen_port);
  server.RunUntilAskedToQuit();

  registry.stop();
  LOG_INFO("{} 退出", FLAGS_service_name);
  return 0;
}
