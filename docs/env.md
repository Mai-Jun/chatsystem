# 环境与版本记录（M0 维护）

> 部署服务器时按此对齐版本。执行完 install_deps.sh / verify_infra.sh 后更新本文件。

## 开发环境

- 宿主：Windows（wsl.exe 已存在，Ubuntu-22.04 发行版安装中/已装）
- 开发环境：WSL2 Ubuntu 22.04（安装命令：`wsl --install -d Ubuntu-22.04`）
- 基础设施：docker compose（`docker/docker-compose.dev.yml`）

## 工具链版本（待 install_deps.sh 执行后回填）

| 组件 | 版本 | 来源 |
|---|---|---|
| g++ | 待回填 | apt |
| cmake | 待回填 | apt |
| protoc | 待回填 | apt (libprotobuf-dev) |
| brpc | 1.9.0 | 源码编译 |
| redis-plus-plus | 待回填 | 源码编译 |
| ODB / libodb-mysql | 待回填 | apt（若缺包→源码编译） |
| gtest | 待回填 | apt (libgtest-dev) |
| spdlog / gflags / boost / websocketpp / librabbitmq / hiredis | 待回填 | apt |

## 基础设施镜像版本

| 镜像 | 版本 | 端口 |
|---|---|---|
| mysql | 8.0 | 3306（root 密码见 compose 文件，仅开发用） |
| redis | 7.2 | 6379 |
| bitnami/etcd | 3.5 | 2379 |
| rabbitmq | 3.12-management | 5672 / 15672（guest/guest） |
| elasticsearch | 7.17.23 + IK | 9200 |

## ODB 源码编译兜底步骤（仅当 apt 无包时）

```bash
# 1) 编译 ODB（GCC 插件）
sudo apt-get install -y build-essential g++ gcc libboost-dev
wget https://www.codesynthesis.com/download/odb/2.5.0/libodb-2.5.0.tar.gz
tar xf libodb-2.5.0.tar.gz && cd libodb-2.5.0 && ./configure && make -j$(nproc) && sudo make install
# 2) 安装 odb 编译器（pre-built binary 包）
wget https://www.codesynthesis.com/download/odb/2.5.0/odb-2.5.0-x86_64-linux-gnu.tar.gz
# 3) libodb-mysql（需 default-libmysqlclient-dev）
```

## 已知坑位（记录给部署阶段）

- （待补充：brpc 编译、ES IK 插件源、Docker 镜像加速等实际遇到的问题）
