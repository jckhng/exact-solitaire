#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
CONTAINER="$("$ROOT/docker_start_builder.sh" | tail -n 1)"
exec docker exec -it "$CONTAINER" /bin/bash
