#!/bin/bash
# Runs inside the bbndk Docker container (see docker-build.sh).
# Builds Term49 into Device-Debug/Term49 and packages an unsigned
# Term49CX.bar. With the "sdl" argument, first rebuilds the patched
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

echo "=== Packaging unsigned Term49CX.bar (case-sensitive staging) ==="
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
# prefer the tar pre-staged by docker-build.sh on the host; git may not
# work in here (subdirectory checkout, or a git too old for the repo)
if [ -f /work/terminfo-stage.tar ]; then
	(cd "$STAGE" && tar -xf /work/terminfo-stage.tar)
else
	git archive HEAD share/terminfo | (cd "$STAGE" && tar -xf -)
fi
# The source tree was authored on a case-insensitive (macOS) filesystem, so
# git only ever stored ONE case per collided single-letter directory - e.g.
# xterm-256color lives under X/ and there is no x/ at all. The device is
# case-sensitive and ncurses looks up terminfo by the literal first byte of
# $TERM, so x/xterm-256color is never found -> $TERM fails to load and the
# shell can no longer move the cursor (backspace leaves stray spaces). Mirror
# every single-letter directory to its opposite-case sibling so both resolve.
TIDIR="$STAGE/share/terminfo"
for d in "$TIDIR"/?; do
	[ -d "$d" ] || continue
	b="$(basename "$d")"
	case "$b" in
		[A-Za-z]) ;;
		*) continue ;;
	esac
	alt="$(printf '%s' "$b" | tr 'A-Za-z' 'a-zA-Z')"
	[ "$alt" = "$b" ] && continue
	mkdir -p "$TIDIR/$alt"
	cp -n "$d"/* "$TIDIR/$alt"/ 2>/dev/null || true
done
# the packager exits non-zero over the placeholder authorId but still
# writes the bar; tolerate that and check for the file instead.
# Note: no -devMode here - it stamps Development-Mode into the manifest,
# which makes installs fail unless the bar author matches the device's
# debug token.
(cd "$STAGE" && blackberry-nativepackager -package Term49CX.bar bar-descriptor.xml -configuration Device-Debug) || true
[ -f "$STAGE/Term49CX.bar" ] || { echo "packaging failed"; exit 1; }
cp "$STAGE/Term49CX.bar" .

echo "=== Done: Device-Debug/Term49 and Term49CX.bar ==="
