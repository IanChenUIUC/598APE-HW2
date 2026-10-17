#!/bin/bash

docker run --rm --cap-add PERFMON --security-opt seccomp=unconfined -v "$(pwd):/host" -w /host "$USER/598ape" "$@"
