#!/bin/bash
# Build the patched libSDL12.so from the SDL submodule with the BB10 NDK.
# Run inside the NDK environment (bbndk-env sourced), e.g. via
# scripts/docker-build.sh sdl. The SDL submodule must be on the term48
# branch with patches/sdl-term48-trackpad.patch applied.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT/SDL"

if ! grep -q handleTrackpadEvent src/video/playbook/SDL_playbookevents.c; then
	echo "Applying trackpad patch to SDL submodule"
	patch -p1 < "$REPO_ROOT/patches/sdl-term48-trackpad.patch"
fi

# Figure out the cross triplet shipped with this NDK (BB10 is nto-qnx8.0.0)
TRIPLET="$(ls "$QNX_HOST/usr/bin/" | grep -o '^arm-unknown-nto-qnx[0-9.]*eabi' | head -1)"
if [ -z "$TRIPLET" ]; then
	echo "Could not find an arm-unknown-nto-qnx*eabi toolchain in $QNX_HOST/usr/bin" >&2
	exit 1
fi
echo "Using toolchain triplet: $TRIPLET"

CPPFLAGS="-D__PLAYBOOK__ -D__QNXNTO__ -DRAW_KEYBOARD_EVENTS -I$REPO_ROOT/TouchControlOverlay/public" \
CFLAGS="-g -O2" \
LDFLAGS="-L$REPO_ROOT/external/lib -lscreen -lbps -lasound -lm -lEGL -lGLESv2 -lTouchControlOverlay" \
./configure --host="$TRIPLET" \
            --without-x \
            --enable-pthreads \
            --enable-video-playbook

make -j"$(nproc)"

# Do the final shared link ourselves so the library gets the exact name and
# soname (libSDL12.so) that Term49 links against and bar-descriptor.xml ships.
OBJS=$(ls build/.libs/*.o 2>/dev/null || ls build/*.o)
qcc -Vgcc_ntoarmv7le -shared -Wl,-soname,libSDL12.so \
    -o "$REPO_ROOT/external/lib/libSDL12.so" \
    $OBJS \
    -L"$REPO_ROOT/external/lib" \
    -lscreen -lbps -lasound -lm -lEGL -lGLESv1_CM -lGLESv2 -lTouchControlOverlay

echo "Built $REPO_ROOT/external/lib/libSDL12.so"
