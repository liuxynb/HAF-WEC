# 使用 Ubuntu 20.04 作为基础镜像
FROM ubuntu:20.04

# 关闭交互模式，避免 tzdata 相关问题
ENV DEBIAN_FRONTEND=noninteractive

# 安装基础依赖
RUN apt update && apt install -y \
    gcc g++ make cmake autogen autoconf automake \
    yasm nasm libtool libboost-all-dev libevent-dev \
    memcached wget curl \
    && rm -rf /var/lib/apt/lists/*

# 设置安装路径
ENV PREFIX=/usr/local
ENV LD_LIBRARY_PATH=$PREFIX/lib:$LD_LIBRARY_PATH

# 复制 libmemcached 和 ISA-L 源码压缩包到 /opt 目录
COPY libmemcached-1.0.18.tar.gz /opt/
COPY isa-l-2.14.0.tar.gz /opt/

# 解压并安装 libmemcached
WORKDIR /opt
RUN tar -zxvf libmemcached-1.0.18.tar.gz && \
    cd libmemcached-1.0.18 && \
    ./configure --prefix=$PREFIX && \
    make -j$(nproc) && \
    make install

# 解压并安装 ISA-L
WORKDIR /opt
RUN tar -zxvf isa-l-2.14.0.tar.gz && \
    cd isa-l-2.14.0 && \
    sh autogen.sh && \
    ./configure --prefix=$PREFIX && \
    make -j$(nproc) && \
    make install

# 确保动态库可用
RUN echo "$PREFIX/lib" > /etc/ld.so.conf.d/custom-libs.conf && ldconfig

# 复制 haf-wec 源码
COPY . /haf-wec
WORKDIR /haf-wec

# 设置memcached服务器
RUN if [ -f /haf-wec/proxy/cls.sh ]; then \
        cd /haf-wec/proxy && bash cls.sh; \
    fi

# 编译 haf-wec
RUN make -j$(nproc)

# 创建启动脚本
RUN echo '#!/bin/bash\n\
# 启动memcached服务\n\
service memcached start\n\
\n\
# 如果存在用户配置，应用它\n\
if [ -f /haf-wec/config/common.hpp ]; then\n\
    cp /haf-wec/config/common.hpp /haf-wec/requestor/common.hpp\n\
    cp /haf-wec/config/common.hpp /haf-wec/proxy/common.hpp\n\
    cd /haf-wec && make -j$(nproc)\n\
fi\n\
\n\
# 启动shell或执行其它命令\n\
if [ "$#" -eq 0 ]; then\n\
    bash\n\
else\n\
    exec "$@"\n\
fi' > /start.sh && chmod +x /start.sh

# 创建配置目录
RUN mkdir -p /haf-wec/config

# 设置默认命令
ENTRYPOINT ["/start.sh"]
CMD ["bash"]