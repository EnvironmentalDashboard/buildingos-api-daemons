#!/bin/bash

docker build -t buildingosd .
docker run -dit -p 52001:3306 -v /root:/root --restart always --name buildingosd1 buildingosd
docker run -dit -p 52002:3306 -v /root:/root --restart always --name buildingosd2 buildingosd
# if using gdb, need to add `--cap-add=SYS_PTRACE --security-opt seccomp=unconfined` to docker run, see https://stackoverflow.com/a/46676907/2624391
