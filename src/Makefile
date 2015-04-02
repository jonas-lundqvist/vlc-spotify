CC ?= clang

CFLAGS_VLC=$(shell pkg-config --cflags vlc-plugin)
CFLAGS_LIBSPOTIFY=$(shell pkg-config --cflags libspotify)
LDFLAGS_VLC=$(shell pkg-config --libs vlc-plugin)
LDFLAGS_LIBSPOTIFY=$(shell pkg-config --libs libspotify)

CFLAGS=-std=gnu99 $(CFLAGS_VLC) $(CFLAGS_LIBSPOTIFY)
LDFLAGS=$(LDFLAGS_VLC) $(LDFLAGS_LIBSPOTIFY) -shared

override CPPFLAGS += -DPIC -I. -Isrc
override CFLAGS += -Wall -g -O2 -fPIC
override LDFLAGS += -Wl,-no-undefined,-z,defs -lpthread
override CPPFLAGS += -DMODULE_STRING=\"spotify\"

TARGET = libspotify_plugin.so
SOURCES= spotify.c appkey.c uriparser.c
OBJECTS=$(SOURCES:.c=.o)

all: $(SOURCES) $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

spotify.o : spotify.c uriparser.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

appkey.o: appkey.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

uriparser.o: uriparser.c uriparser.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	$(RM) $(TARGET) $(OBJECTS)

install:
	cp libspotify_plugin.so /usr/lib/vlc/plugins/access
