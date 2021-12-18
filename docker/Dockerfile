FROM debian:bullseye
RUN sed -i 's/deb.debian.org/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list
RUN sed -i 's/security.debian.org/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list
RUN apt update
RUN apt install -y python3-pip ninja-build gcc g++ sudo socat iproute2
RUN pip3 install -i https://pypi.tuna.tsinghua.edu.cn/simple meson
ENV PATH="${HOME}/.local/bin:${PATH}"
