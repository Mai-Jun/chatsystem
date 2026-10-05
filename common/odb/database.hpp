#pragma once
#include <memory>

#include <odb/database.hxx>
#include <odb/connection.hxx>
#include <odb/transaction.hxx>
#include <odb/mysql/database.hxx>

#include "flags.hpp"
#include "logger.hpp"

// ============================================================
// MySQL(ODB) 数据库实例工厂
// 用法: auto db = im::create_db();
//       odb::transaction t(db->begin()); ... t.commit();
// ============================================================
namespace im {

inline std::unique_ptr<odb::mysql::database> create_db() {
  auto db = std::make_unique<odb::mysql::database>(
      FLAGS_mysql_user, FLAGS_mysql_password, FLAGS_mysql_db, FLAGS_mysql_host,
      static_cast<unsigned short>(FLAGS_mysql_port), nullptr, "utf8mb4");
  return db;
}

}  // namespace im
