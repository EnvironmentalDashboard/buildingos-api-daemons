#!/bin/bash

if [ -f "buildingos.config" ]
then
    . buildingos.config
else
    echo "Missing buildingos.config"
    exit 1
fi

if [ -f "db.config" ]
then
    . db.config
else
    echo "Missing db.config"
    exit 1
fi

docker run -dit \
    -e CLIENT_ID=$CLIENT_ID -e CLIENT_SECRET=$CLIENT_SECRET -e USERNAME=$USERNAME -e PASSWORD=$PASSWORD \
    -e DB_SERVER=$DB_SERVER -e DB_USER=$DB_USER -e DB_PASS=$DB_PASS -e DB_NAME=$DB_NAME -e DB_PORT=$DB_PORT \
    -v $(pwd):/root --restart always --name buildingosd buildingosd
# if using gdb, need to add `--cap-add=SYS_PTRACE --security-opt seccomp=unconfined` to docker run, see https://stackoverflow.com/a/46676907/2624391
