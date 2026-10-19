#!/bin/bash
# Builds the web version into build-web/site (needs the Emscripten SDK: EMSDK=/path/to/emsdk or
# emcc on the PATH). The gl4es source comes from platform/Android/fetch-deps.sh.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if ! command -v emcc >/dev/null; then
    # shellcheck disable=SC1091
    . "${EMSDK:-$HOME/.cache/emsdk}/emsdk_env.sh" >/dev/null 2>&1
fi
sh "$ROOT/platform/Android/fetch-deps.sh" >/dev/null
BUILD="${BUILD_DIR:-$ROOT/build-web}"
emcmake cmake -S "$ROOT/platform/Web" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DGL4ES_SOURCE_DIR="$ROOT/platform/Android/third_party/gl4es" >/dev/null
cmake --build "$BUILD" -j"$(nproc)" "$@"

# the page: the game, its data file, the shell (launcher, install, offline cache)
SITE="$BUILD/site"
rm -rf "$SITE"
mkdir -p "$SITE"
cp "$BUILD"/notblood.js "$BUILD"/notblood.wasm "$BUILD"/notblood.data "$SITE/"
cp -r "$ROOT"/platform/Web/shell/. "$SITE/"
# the offline cache is renewed whenever any file changes
VERSION=$(sed -n 's/#define CHOSEN_VERSION "\(.*\)"/\1/p' "$ROOT/source/blood/src/chosen.h")
HASH=$(cat "$SITE"/notblood.* "$SITE"/*.html "$SITE"/*.js "$SITE"/*.css 2>/dev/null | sha256sum | cut -c1-12)
sed -i "s/__VERSION__/$VERSION/g; s/__BUILD__/$HASH/g" "$SITE"/sw.js "$SITE"/index.html "$SITE"/app.js
# the server sends these compressed to browsers that take gzip (server/relay.py)
gzip -9 -k -n "$SITE"/notblood.wasm "$SITE"/notblood.data "$SITE"/notblood.js
# the download the relay installs (GitHub release asset)
rm -f "$BUILD"/NotBlood-Chosen-*-web.zip
(cd "$SITE" && zip -q -r -9 "$BUILD/NotBlood-Chosen-$VERSION-web.zip" .)
echo "web build: $SITE ($VERSION, $HASH)"
