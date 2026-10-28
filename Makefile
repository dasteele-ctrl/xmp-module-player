CC ?= cc
CXX ?= c++
CFLAGS += -O2 -Wall -Wextra $(shell pkg-config --cflags gtk+-3.0 sdl2 libxmp libavformat libavcodec libavutil libswresample)
CXXFLAGS += -O2 -Wall -Wextra -std=c++11 $(shell pkg-config --cflags libsidplayfp)
LDLIBS += $(shell pkg-config --libs gtk+-3.0 sdl2 libxmp libavformat libavcodec libavutil libswresample) $(shell pkg-config --libs libsidplayfp) -lm

all: xmp-linux

xmp-linux: main.o xmp-resources.o ffmpeg_audio.o sid_audio.o hvsc_download.o
	$(CXX) -o $@ $^ $(LDLIBS)

main.o: main.c ffmpeg_audio.h sid_audio.h
	$(CC) $(CFLAGS) -c -o $@ main.c

ffmpeg_audio.o: ffmpeg_audio.c ffmpeg_audio.h
	$(CC) $(CFLAGS) -c -o $@ ffmpeg_audio.c

sid_audio.o: sid_audio.cpp sid_audio.h
	$(CXX) $(CXXFLAGS) -c -o $@ sid_audio.cpp

xmp-resources.o: xmp-resources.c
	$(CC) $(CFLAGS) -c -o $@ xmp-resources.c

xmp-resources.c: xmp.gresource.xml icons/xmp.png
	glib-compile-resources --generate-source --target=$@ $<

clean:
	rm -f xmp-linux hvsc_download.o main.o xmp-resources.o ffmpeg_audio.o sid_audio.o xmp-resources.c

.PHONY: all clean

hvsc_download.o: hvsc_download.c hvsc_download.h
	$(CC) $(CFLAGS) -c -o $@ hvsc_download.c
