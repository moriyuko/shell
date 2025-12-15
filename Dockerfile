FROM python:3.13-slim

WORKDIR /opt

RUN apt update && apt install -y --no-install-recommends \
    g++ \
    libreadline-dev \
    libfuse3-dev \
    make \
    devscripts \
    debhelper \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install -r requirements.txt

COPY . .

RUN mkdir -p /opt/users
ENV VFS_DIR=/opt/users

CMD ["/bin/bash"]