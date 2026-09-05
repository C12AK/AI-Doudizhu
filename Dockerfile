# 构建阶段：拷网页、编译服务端
# 拉不了 Docker Hub 时把 BASE_IMAGE 换成镜像站上的 ubuntu:22.04
ARG BASE_IMAGE=docker.m.daocloud.io/library/ubuntu:22.04
FROM ${BASE_IMAGE} AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    g++ \
    make \
    libmysqlclient-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN bash web/build/build.sh && bash server/build/build.sh

# 运行阶段：只留下可执行文件、网页和配置
ARG BASE_IMAGE=docker.m.daocloud.io/library/ubuntu:22.04
FROM ${BASE_IMAGE}
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libmysqlclient21 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/server/deploy/bin/ddz-server /app/bin/ddz-server
COPY --from=builder /src/web/deploy/www /app/www
COPY config.ini /app/config/sysparam.ini
RUN mkdir -p /app/log

EXPOSE 8080
CMD ["/app/bin/ddz-server", "/app/config/sysparam.ini"]
