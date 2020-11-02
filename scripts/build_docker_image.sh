#/bin/bash

set -e

BUILD_TYPE=Release

docker build -t priority_strand --build-arg BUILD_TYPE=${BUILD_TYPE} .
