#!/bin/bash

docker build -t buildingosd .
docker run -dit --rm -p 52001:3306 -v /root:/root --restart unless-stopped --name buildingosd1 buildingosd
# if using gdb, need to add `--cap-add=SYS_PTRACE --security-opt seccomp=unconfined` to docker run, see https://stackoverflow.com/a/46676907/2624391
