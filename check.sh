#!/bin/bash

cd /mnt

make deb

apt update && apt install -y ./kubsh.deb

cd /opt

pytest -v --log-cli-level=10 .
