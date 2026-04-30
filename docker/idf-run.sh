#!/bin/bash
# Non-interactive variant of idf-shell.sh, suitable for VS Code tasks / CI.
# Usage: ./docker/idf-run.sh "idf.py build"

set -e

rpath="$( dirname "$( readlink -f "$0" )" )"
cd "$rpath"

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <command...>"
    exit 1
fi

docker run --rm \
    -v /dev:/dev \
    --privileged \
    -e BOARD="${BOARD:-NERDQAXEPLUS2}" \
    -v "$rpath/..":/home/builder/project \
    esp-idf-builder \
    /bin/bash -lc "$*"
