#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
CONTAINER="$("$ROOT/docker_start_builder.sh" | tail -n 1)"
UID_HOST="$(id -u)"
GID_HOST="$(id -g)"
MAKE_TARGETS="${EXACT_SOLITAIRE_MAKE_TARGETS:-exact-solitaire smoke-test}"
DO_PACKAGE="${EXACT_SOLITAIRE_PACKAGE:-1}"

docker exec "$CONTAINER" chown -R "$UID_HOST:$GID_HOST" /src/exact-solitaire
docker exec --user "$UID_HOST:$GID_HOST" "$CONTAINER" /bin/sh -lc "make $MAKE_TARGETS && ./smoke-test"

if [ "$DO_PACKAGE" = "1" ]; then
    EXACT_SOLITAIRE_DOCKER_CONTAINER="$CONTAINER" "$ROOT/package_extension.sh"
fi

echo "Builder container: $CONTAINER"
echo "Binary: $ROOT/exact-solitaire"
if [ "$DO_PACKAGE" = "1" ]; then
    echo "Package: $ROOT/dist/exact-solitaire-extension.zip"
fi
