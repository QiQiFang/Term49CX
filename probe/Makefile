# Standalone TouchProbe for BB10 (Passport keyboard research).
# Build inside NDK env or via: make docker

CC          := qcc
QCC_TARGET  ?= gcc_ntoarmv7le
DEBUGFLAGS  := -O0 -g
CFLAGS      := -V$(QCC_TARGET) -Wc,-std=gnu99 $(DEBUGFLAGS) -Wall
DEFINES     := -D_FORTIFY_SOURCE=2 -fstack-protector-strong
LIBPATHS    := -L$(QNX_TARGET)/armle-v7/lib -L$(QNX_TARGET)/armle-v7/usr/lib
LIBS        := -lbps -lscreen -lm
LDFLAGS     := $(LIBPATHS) $(LIBS)
LDOPTS      := -Wl,-z,relro -Wl,-z,now

ASSET       := Device-Debug
BINARY      := TouchProbe
BAR         := TouchProbe.bar
BINARY_PATH := $(ASSET)/$(BINARY)

# Reuse Term49 signing credentials when present
-include ../signing/bbpass

BBIP        ?= 169.254.0.1
BBNDK_IMAGE ?= delaya73/bbndk:latest

.PHONY: all clean package-debug deploy docker pull-log

all: package-debug

$(BINARY_PATH): main.c
	mkdir -p $(ASSET)
	$(CC) $(CFLAGS) $(DEFINES) main.c $(LDFLAGS) $(LDOPTS) -o $(BINARY_PATH)

clean:
	rm -rf $(ASSET) $(BAR)

package-debug: $(BINARY_PATH)
	# Same packaging approach as Term49: no -devMode (that stamps Development-Mode
	# into the manifest and breaks install unless author matches the debug token).
	# Packager may exit non-zero on placeholder authorId but still write the bar.
	@if [ -f ../signing/debugtoken.bar ]; then \
		blackberry-nativepackager -package $(BAR) bar-descriptor.xml \
			-configuration Device-Debug -devMode \
			-debugToken ../signing/debugtoken.bar || true; \
	else \
		blackberry-nativepackager -package $(BAR) bar-descriptor.xml \
			-configuration Device-Debug || true; \
	fi
	@test -f $(BAR) || { echo "packaging failed: $(BAR) not created"; exit 1; }
	@echo "Packaged $(BAR)"

deploy: package-debug
	blackberry-deploy -installApp $(BBIP) -password $(BBPASS) $(BAR)

# Build binary + bar inside Docker (no local NDK required)
docker:
	docker run --rm --platform linux/amd64 \
		-v "$(CURDIR)":/work \
		-v "$(CURDIR)/../signing":/signing:ro \
		-w /work \
		$(BBNDK_IMAGE) \
		/bin/bash -lc 'set -e; \
			. /home/admin/bin/bbndk/bbndk-env_10_3_1_995.sh; \
			echo "QNX_TARGET=$$QNX_TARGET"; \
			# Expose debug token where Makefile expects ../signing/ \
			if [ -f /signing/debugtoken.bar ]; then \
			  mkdir -p /tmp/signing-link && \
			  ln -sfn /signing /work/../signing 2>/dev/null || \
			  cp -a /signing/debugtoken.bar /tmp/ 2>/dev/null || true; \
			fi; \
			make -C /work clean all; \
			ls -la /work/Device-Debug /work/TouchProbe.bar'

# After a session: pull log from device over USB/ssh (blackberry-connect running)
pull-log:
	@echo "Trying scp of shared Documents log..."
	@scp -o StrictHostKeyChecking=no -i ../signing/ssh-key \
		devuser@$(BBIP):/accounts/1000/shared/documents/TouchProbe.log \
		./TouchProbe.log 2>/dev/null \
	|| scp -o StrictHostKeyChecking=no -i ../signing/ssh-key \
		devuser@$(BBIP):/accounts/1000/shared/misc/TouchProbe.log \
		./TouchProbe.log 2>/dev/null \
	|| (echo "scp failed — open File Manager → Device → Documents → TouchProbe.log"; exit 1)
	@echo "Saved ./TouchProbe.log"
	@wc -l ./TouchProbe.log
