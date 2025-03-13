FROM ubuntu:20.04

# 安装基础依赖
RUN apt update && apt install -y \
    gcc g++ make cmake autogen autoconf automake \
    yasm nasm libtool libboost-all-dev libevent-dev \
    memcached wget curl \
    && rm -rf /var/lib/apt/lists/*

# 设置安装路径
ENV PREFIX=/usr/local

# 安装 libmemcached
WORKDIR /opt
RUN git clone https://github.com/memcached/libmemcached.git \
    && cd libmemcached \
    && ./configure --prefix=$PREFIX \
    && make -j$(nproc) \
    && make install

# 安装 ISA-L
RUN git clone https://github.com/intel/isa-l.git \
    && cd isa-l \
    && ./autogen.sh \
    && ./configure --prefix=$PREFIX \
    && make -j$(nproc) \
    && make install

# 复制 HAF-WEC 源码
COPY . /haf-wec
WORKDIR /haf-wec

# 编译 HAF-WEC
RUN make -j$(nproc)

# 运行 HAF-WEC
CMD ["bash"]