CC := qcc

INCLUDE := -I$(QNX_TARGET)/usr/include
INCLUDE += -I$(QNX_TARGET)/usr/include/freetype2
INCLUDE += -I./external/include
QT4LIB := $(QNX_TARGET)/armle-v7/usr/lib/qt4/lib
QTINCLUDE := -I$(QNX_TARGET)/usr/include/qt4 -I$(QNX_TARGET)/usr/include/qt4/QtCore

# BB10 libraries
LIBPATHS	:= -L$(QNX_TARGET)/armle-v7/lib
LIBS    	:= -lbps -licui18n -licuuc -lscreen -lm -lfreetype -lclipboard
LIBS    	+= -lconfig

# Defines
DEFINES := -D_FORTIFY_SOURCE=2 -D__PLAYBOOK__ -fstack-protector-strong

# OpenGL libraries
LIBPATHS += -L$(QNX_TARGET)/armle-v7/usr/lib

# Include bundles libs
LIBPATHS += -L./external/lib
LIBS     += -lconfig -lSDL12 -lTouchControlOverlay
LIBPATHS += -L$(QT4LIB) -Wl,-rpath-link,$(QT4LIB)
LIBS     += -lbbsystem -lQtDeclarative -lQtGui -lQtCore

# Optimised by default, and without DEBUGMSGS: with it defined, PRINT()
# expands to fprintf, and ecma48_filter_text() logs *every character* of
# child output to stderr. On device that alone costs more than parsing and
# rendering combined, and it is done while holding the input lock, so all
# input stalls behind it whenever a tmux pane is producing output.
# Override from the environment for a debug build, e.g.
#   make DEBUGFLAGS='-O0 -g -DDEBUGMSGS'
DEBUGFLAGS	?= -O2 -g
# qcc target: works with both NDK 10.2 (gcc 4.6.3) and 10.3.1 (gcc 4.8.3)
QCC_TARGET	?= gcc_ntoarmv7le
CFLAGS    	:= $(INCLUDE) -V$(QCC_TARGET) -Wc,-std=gnu99 $(DEBUGFLAGS)
CXXFLAGS  	:= $(INCLUDE) $(QTINCLUDE) -V$(QCC_TARGET) -lang-c++ -Wc,-std=gnu++98 $(DEBUGFLAGS)
LDFLAGS   	:= $(LIBPATHS) $(LIBS)
LDOPTS    	:= -Wl,-z,relro -Wl,-z,now

ASSET      	:= Device-Debug
BINARY     	:= Term49
BAR        	:= Term49CX.bar
BINARY_PATH	:= $(ASSET)/$(BINARY)

SRCS := $(wildcard src/*.c)
CPPSRCS := $(wildcard src/*.cpp)
OBJS := $(SRCS:.c=.o) $(CPPSRCS:.cpp=.o)

include ./signing/bbpass

.PHONY: all clean package-debug deploy launch-debug

all: package-debug

$(BINARY): $(OBJS)
	mkdir -p $(ASSET)
	$(CC) $(CXXFLAGS) $(LDFLAGS) $(LDOPTS) $(OBJS) -o $(BINARY_PATH)

%.o: %.c
	$(CC) $(CFLAGS) -c $(DEFINES) $< -o $@

%.o: %.cpp
	$(CC) $(CXXFLAGS) -c $(DEFINES) $< -o $@

clean:
	@rm -fv src/*.o
	@rm -fv $(BINARY_PATH)
	@rmdir -v $(ASSET)
	@rm -fv $(BAR)

signing/debugtoken.bar:
	$(error Debug token error: place debug token in signing/debugtoken.bar or see signing/Makefile))

package-debug: $(BINARY) signing/debugtoken.bar
	blackberry-nativepackager -package $(BAR) bar-descriptor.xml -devMode -debugToken signing/debugtoken.bar

signing/ssh-key:
	$(error SSH key error: signing/ssh-key not found. `cd signing` and `make ssh-key`))
connect: signing/ssh-key
	blackberry-connect $(BBIP) -password $(BBPASS) -sshPublicKey signing/ssh-key.pub

BBIP ?= 169.254.0.1

deploy: package-debug
	blackberry-deploy -installApp $(BBIP) -password $(BBPASS) $(BAR)

launch-debug: deploy
	blackberry-deploy -debugNative -device $(BBIP) -password $(BBPASS) -launchApp $(BAR)
	trap '' SIGINT; BINARY_PATH=$(BINARY_PATH) BBIP=$(BBIP) ntoarm-gdb -x scripts/gdb-debug-setup.py

package-release: $(BINARY)
	blackberry-nativepackager -package $(BAR) bar-descriptor.xml

sign: package-release
	blackberry-signer -bbidtoken ./signing/$(BBIDTOKEN) -storepass $(KEYSTOREPASS) -keystore ./signing/$(KEYSTORE) $(BAR)

