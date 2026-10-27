CC ?= cc
CFLAGS += -O2 -Wall -Wextra $(shell pkg-config --cflags gtk+-3.0 sdl2 libxmp)
LDLIBS += $(shell pkg-config --libs gtk+-3.0 sdl2 libxmp) -lm

all: xmp-linux

xmp-linux: main.c xmp-resources.c
	$(CC) $(CFLAGS) -o $@ main.c xmp-resources.c $(LDLIBS)

xmp-resources.c: xmp.gresource.xml icons/xmp.png
	glib-compile-resources --generate-source --target=$@ $<

clean:
	rm -f xmp-linux xmp-resources.c

.PHONY: all clean
