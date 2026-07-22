#!/bin/bash
# Build Term49 (and optionally the patched libSDL12) with the BlackBerry 10
# NDK inside a Docker container.
#
# Usage (from the repo root):
#   ./scripts/docker-build.sh            # build Term49 binary + unsigned bar
#   ./scripts/docker-build.sh sdl        # also rebuild external/lib/libSDL12.so
#
# Uses the community image uvatbc/bbndk (BB10 NDK 10.3.1.995 preinstalled).
# On arm64 hosts (Apple silicon) the image runs under x86 emulation, which
# works but is slow. By using the image/NDK you accept the BlackBerry SDK
# license.

set -e

IMAGE="${BBNDK_IMAGE:-uvatbc/bbndk:latest}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Pre-stage the terminfo tree from git on the host: the container only
# mounts this directory, so when it is a subdirectory of a larger repo
# (or the container git is too old) `git archive` fails inside.
git -C "$REPO_ROOT" archive HEAD share/terminfo > "$REPO_ROOT/terminfo-stage.tar" || rm -f "$REPO_ROOT/terminfo-stage.tar"

STATUS=0
docker run --rm --platform linux/amd64 \
	-v "$REPO_ROOT":/work \
	-w /work \
	"$IMAGE" \
	/bin/bash /work/scripts/container-build.sh "$@" || STATUS=$?
rm -f "$REPO_ROOT/terminfo-stage.tar"
exit $STATUS
