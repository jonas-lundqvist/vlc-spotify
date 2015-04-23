CC ?= clang
ifeq ($(OS),win32)
	CC = i686-w64-mingw32-gcc
endif

CFLAGS_VLC=$(shell pkg-config --cflags vlc-plugin)
CFLAGS_LIBSPOTIFY=$(shell pkg-config --cflags libspotify)
LDFLAGS_VLC=$(shell pkg-config --libs vlc-plugin)
LDFLAGS_LIBSPOTIFY=$(shell pkg-config --libs libspotify)
ifeq ($(OS),win32)
	# No pkg-config files for libspotify win32
	LDFLAGS_LIBSPOTIFY=-L$(LIBSPOTIFY_WIN32_DIR)/lib -lspotify
	CFLAGS_LIBSPOTIFY=-I$(LIBSPOTIFY_WIN32_DIR)/include
endif

CFLAGS=-std=gnu99 $(CFLAGS_VLC) $(CFLAGS_LIBSPOTIFY) -Wall -g -O2 -fPIC
LDFLAGS=$(LDFLAGS_VLC) $(LDFLAGS_LIBSPOTIFY) -shared -Wl,-no-undefined -lpthread
CPPFLAGS = -DPIC -I. -Isrc -DMODULE_STRING=\"spotify\"

ifneq ($(OS),win32)
	override LDFLAGS += -Wl,-z,defs
else
	override LDFLAGS += -static-libgcc
endif

TARGET = libspotify_plugin.so
ifeq ($(OS),win32)
	TARGET = libspotify_plugin.dll
endif
TARGETS_ALL = libspotify_plugin.*

SOURCES= spotify.c appkey.c uriparser.c
OBJECTS=$(SOURCES:.c=.o)

all: $(SOURCES) $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

spotify.o : spotify.c uriparser.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

appkey.o: appkey.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

uriparser.o: uriparser.c uriparser.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	$(RM) $(TARGETS_ALL) $(OBJECTS)

install:
	cp libspotify_plugin.so /usr/lib/vlc/plugins/access
