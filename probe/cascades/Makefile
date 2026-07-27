# Cascades TouchKeyboardHandler probe
QCC_TARGET  ?= gcc_ntoarmv7le
CXX         := qcc
CXXFLAGS    := -V$(QCC_TARGET) -lang-c++ -Wc,-std=gnu++98 -O0 -g -Wall
DEFINES     := -D_FORTIFY_SOURCE=2
INCLUDES    := -I$(QNX_TARGET)/usr/include -I$(QNX_TARGET)/usr/include/qt4 \
               -I$(QNX_TARGET)/usr/include/qt4/QtCore \
               -I$(QNX_TARGET)/usr/include/bb -Isrc
QT4LIB      := $(QNX_TARGET)/armle-v7/usr/lib/qt4/lib
LIBPATHS    := -L$(QNX_TARGET)/armle-v7/lib -L$(QNX_TARGET)/armle-v7/usr/lib \
               -L$(QT4LIB) -Wl,-rpath-link,$(QT4LIB)
LIBS        := -lbbcascades -lbb -lbps -lscreen -lbbdevice \
               -lQtDeclarative -lQtScript -lQtSvg -lQtXmlPatterns \
               -lQtGui -lQtNetwork -lQtXml -lQtSql -lQtCore \
               -lQtSensors -lm -lpng -lz

ASSET       := Device-Debug
BINARY      := TouchProbeCascades
BAR         := TouchProbeCascades.bar
BINARY_PATH := $(ASSET)/$(BINARY)

SRCS := src/main.cpp src/applicationui.cpp src/moc_applicationui.cpp
OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean package

all: package

src/moc_applicationui.cpp: src/applicationui.hpp
	moc -I$(QNX_TARGET)/usr/include -I$(QNX_TARGET)/usr/include/qt4 \
	    -I$(QNX_TARGET)/usr/include/qt4/QtCore \
	    -I$(QNX_TARGET)/usr/include/bb -o $@ $<

$(BINARY_PATH): $(OBJS)
	mkdir -p $(ASSET)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LIBPATHS) $(LIBS) -o $(BINARY_PATH)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(ASSET) $(OBJS) $(BAR) src/moc_applicationui.cpp

package: $(BINARY_PATH)
	blackberry-nativepackager -package $(BAR) bar-descriptor.xml -configuration Device-Debug || true
	test -f $(BAR)
	@echo "Built $(BAR)"
