#!/bin/bash
# Runs inside the bbndk Docker container (see docker-build.sh).
# Builds Term49 into Device-Debug/Term49 and packages an unsigned
# Term49C.bar. With the "sdl" argument, first rebuilds the patched
# libSDL12.so from the SDL submodule (term48 branch +
# patches/sdl-term48-trackpad.patch) into external/lib/.

set -e

# Locate and load the NDK environment
BBNDK_ENV="$(ls /home/*/bin/bbndk/bbndk-env*.sh /opt/bbndk/bbndk-env*.sh /root/bbndk/bbndk-env*.sh 2>/dev/null | head -1)"
if [ -z "$BBNDK_ENV" ]; then
	echo "Could not find bbndk-env script in the container" >&2
	exit 1
fi
echo "Using NDK env: $BBNDK_ENV"
source "$BBNDK_ENV"

cd /work

if [ "$1" = "sdl" ]; then
	echo "=== Building patched libSDL12.so ==="
	scripts/build-sdl.sh
fi

echo "=== Building Term49 ==="
make clean >/dev/null 2>&1 || true
make Term49

echo "=== Packaging unsigned Term49C.bar (case-sensitive staging) ==="
# The terminfo tree contains case-colliding directories (x/ and X/, e/ and
# E/, ...). On a macOS (case-insensitive) checkout these merge, silently
# shipping a bar whose terminfo is missing half its directories - lookups
# like x/xterm-256color then fail on the device. Stage the packaging root
# on the container's case-sensitive filesystem and restore share/terminfo
# from git, which preserves both cases.
STAGE=/tmp/bar-stage
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -r bar-descriptor.xml icons external Device-Debug share "$STAGE"/
rm -rf "$STAGE/share/terminfo"
git archive HEAD share/terminfo | (cd "$STAGE" && tar -xf -)
# the packager exits non-zero over the placeholder authorId but still
# writes the bar; tolerate that and check for the file instead.
# Note: no -devMode here - it stamps Development-Mode into the manifest,
# which makes installs fail unless the bar author matches the device's
# debug token.
(cd "$STAGE" && blackberry-nativepackager -package Term49C.bar bar-descriptor.xml -configuration Device-Debug) || true
[ -f "$STAGE/Term49C.bar" ] || { echo "packaging failed"; exit 1; }
cp "$STAGE/Term49C.bar" .

echo "=== Done: Device-Debug/Term49 and Term49C.bar ==="
