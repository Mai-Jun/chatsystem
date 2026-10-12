# IM 系统运行镜像（7 个应用服务通用；纯运行时，不含编译器/头文件）
#
# 构建前置（服务器上执行，负责补齐 /usr/local 动态库副本后构建）：
#   bash scripts/prepare_runtime_image.sh
#
# 设计说明：
#   - 代码更新不需要重建镜像：容器把 /root/chatsystem 挂到 /app，
#     重新 cmake --build 后 docker compose restart <svc> 即可生效。
#   - /usr/local/lib 的 libprotobuf.so.31 / libredis++.so.1 为服务器源码编译
#     遗留产物（3.20.2），apt 源没有对应 soname，由构建上下文 lib/ 目录带入。
#   - 基础镜像 ubuntu:22.04 与服务器发行版一致，apt 运行库版本零漂移。
FROM ubuntu:22.04

# 阿里云 apt 源（国内构建提速）
RUN sed -i 's@//.*archive.ubuntu.com@//mirrors.aliyun.com@g; s@//security.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libmysqlclient21 \
      libodb-2.4 \
      libodb-mysql-2.4 \
      libspdlog1 \
      libgflags2.2 \
      libfmt8 \
      librabbitmq4 \
      libhiredis0.14 \
      libssl3 \
      libuuid1 \
      libleveldb1d \
      libsnappy1v5 \
      libzstd1 \
      ca-certificates \
      tzdata \
 && rm -rf /var/lib/apt/lists/*

# /usr/local 动态库（protobuf 3.20.2 / redis++）：由 prepare_runtime_image.sh 拷入上下文 lib/
COPY lib/ /usr/local/lib/
RUN ldconfig

ENV TZ=Asia/Shanghai
WORKDIR /app
CMD ["/bin/bash"]
