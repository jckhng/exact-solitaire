#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
CONTAINER="$("$ROOT/docker_start_builder.sh" | tail -n 1)"
UID_HOST="$(id -u)"
GID_HOST="$(id -g)"
MAKE_TARGETS="${KINDLE_AISLERIOT_MAKE_TARGETS:-kindle-aisleriot smoke-test}"
DO_PACKAGE="${KINDLE_AISLERIOT_PACKAGE:-1}"

docker exec "$CONTAINER" chown -R "$UID_HOST:$GID_HOST" /src/kindle-aisleriot
docker exec --user "$UID_HOST:$GID_HOST" "$CONTAINER" /bin/sh -lc "make $MAKE_TARGETS && ./smoke-test"

if [ "$DO_PACKAGE" = "1" ]; then
    KINDLE_AISLERIOT_DOCKER_CONTAINER="$CONTAINER" "$ROOT/package_extension.sh"
fi

echo "Builder container: $CONTAINER"
echo "Binary: $ROOT/kindle-aisleriot"
if [ "$DO_PACKAGE" = "1" ]; then
    echo "Package: $ROOT/dist/kindle-aisleriot-extension.zip"
fi
