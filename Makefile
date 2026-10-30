CC ?= cc
CXX ?= c++
CFLAGS += -O2 -Wall -Wextra $(shell pkg-config --cflags gtk+-3.0 sdl2 libxmp libavformat libavcodec libavutil libswresample fluidsynth)
CXXFLAGS += -O2 -Wall -Wextra -std=c++11 $(shell pkg-config --cflags libsidplayfp)
LDLIBS += $(shell pkg-config --libs gtk+-3.0 sdl2 libxmp libavformat libavcodec libavutil libswresample fluidsynth) $(shell pkg-config --libs libsidplayfp) -lm

all: xmp-linux

# Native ZX Spectrum playback is the default for xmp-linux. The original
# zxtune123 -> temporary WAV implementation remains available as xmp-zx-wav.
# Native linking requires the static libraries built by ZXTune.
ZX_ROOT ?= $(HOME)/zxtune
ZX_INCLUDE = -I$(ZX_ROOT) -I$(ZX_ROOT)/include -I$(ZX_ROOT)/src
ZX_LIBRARIES = $(wildcard $(ZX_ROOT)/lib/linux/release/*.a)
ZX_NATIVE_OBJECTS = main_zx_native.o zx_audio.o xmp-resources.o ffmpeg_audio.o sid_audio.o midi_audio.o hvsc_download.o
ZX_ISOLATED_OBJECTS = $(filter-out zx_audio.o main_zx_native.o,$(ZX_NATIVE_OBJECTS)) zx_audio_proxy.o main_export.o export_wav.o
EXPORT_TEST_OBJECTS = $(ZX_ISOLATED_OBJECTS)
ZX_SID_TEST_OBJECTS = $(filter-out sid_audio.o,$(ZX_NATIVE_OBJECTS)) sid_audio_zx.o
ZX_WAV_OBJECTS = main.o xmp-resources.o ffmpeg_audio.o sid_audio.o midi_audio.o hvsc_download.o

# The tested default isolates libsidplayfp and ZXTune in different processes.
# Both render PCM directly, without a temporary WAV or conflicting SID symbols.
xmp-linux xmp-zx-native xmp-zx-isolated-test: $(ZX_ISOLATED_OBJECTS) xmp-zx-helper Makefile
	$(CXX) -o $@ $(ZX_ISOLATED_OBJECTS) $(LDLIBS) -ldl -lpthread -lz

# Keep the WAV-export regression-test executable available alongside the main build.
xmp-export-test: $(EXPORT_TEST_OBJECTS) xmp-zx-helper Makefile
	$(CXX) -o $@ $(EXPORT_TEST_OBJECTS) $(LDLIBS) -ldl -lpthread -lz

main_export.o: main.c ffmpeg_audio.h sid_audio.h midi_audio.h zx_audio.h export_wav.h
	$(CC) $(CFLAGS) -DENABLE_ZXTUNE_NATIVE -DENABLE_WAV_EXPORT -c -o $@ main.c

export_wav.o: export_wav.c export_wav.h ffmpeg_audio.h sid_audio.h midi_audio.h zx_audio.h
	$(CC) $(CFLAGS) -c -o $@ export_wav.c

xmp-zx-helper: zx_helper.o zx_audio.o Makefile
	@test -n "$(ZX_LIBRARIES)" || { echo "Build ~/zxtune/apps/zxtune123 first"; exit 1; }
	$(CXX) -o $@ zx_helper.o zx_audio.o -Wl,--start-group $(ZX_LIBRARIES) -Wl,--end-group \
	    $(LDLIBS) -lboost_program_options -ldl -lpthread -lz -fopenmp

zx_helper.o: zx_helper.cpp zx_helper_protocol.h zx_audio.h
	$(CXX) $(CXXFLAGS) -std=c++20 -c -o $@ zx_helper.cpp

zx_audio_proxy.o: zx_audio_proxy.c zx_helper_protocol.h zx_audio.h
	$(CC) $(CFLAGS) -c -o $@ zx_audio_proxy.c

# Side-by-side SID/ZX test using ZXTune's matching SID headers and static ABI.
xmp-zx-sid-test: $(ZX_SID_TEST_OBJECTS) Makefile
	@test -n "$(ZX_LIBRARIES)" || { echo "Missing ZXTune static libraries"; exit 1; }
	$(CXX) -o $@ $(ZX_SID_TEST_OBJECTS) \
	    -Wl,--start-group $(ZX_LIBRARIES) -Wl,--end-group \
	    $(LDLIBS) -lboost_program_options -ldl -lpthread -lz

sid_audio_zx.o: sid_audio.cpp sid_audio.h
	$(CXX) $(CXXFLAGS) -std=c++20 -DXMP_USE_ZXTUNE_SID_HEADERS \
	    -I$(ZX_ROOT)/3rdparty/sidplayfp/src -c -o $@ sid_audio.cpp

main_zx_native.o: main.c ffmpeg_audio.h sid_audio.h midi_audio.h zx_audio.h
	$(CC) $(CFLAGS) -DENABLE_ZXTUNE_NATIVE -c -o $@ main.c

zx_audio.o: zx_audio.cpp zx_audio.h
	$(CXX) $(CXXFLAGS) -std=c++20 $(ZX_INCLUDE) -c -o $@ zx_audio.cpp

xmp-zx-wav: $(ZX_WAV_OBJECTS) Makefile
	$(CXX) -o $@ $(ZX_WAV_OBJECTS) $(LDLIBS)

main.o: main.c ffmpeg_audio.h sid_audio.h midi_audio.h
	$(CC) $(CFLAGS) -c -o $@ main.c

ffmpeg_audio.o: ffmpeg_audio.c ffmpeg_audio.h
	$(CC) $(CFLAGS) -c -o $@ ffmpeg_audio.c

sid_audio.o: sid_audio.cpp sid_audio.h
	$(CXX) $(CXXFLAGS) -c -o $@ sid_audio.cpp

midi_audio.o: midi_audio.c midi_audio.h
	$(CC) $(CFLAGS) -c -o $@ midi_audio.c

xmp-resources.o: xmp-resources.c
	$(CC) $(CFLAGS) -c -o $@ xmp-resources.c

xmp-resources.c: xmp.gresource.xml icons/xmp.png
	glib-compile-resources --generate-source --target=$@ $<

clean:
	rm -f xmp-linux xmp-zx-native xmp-zx-wav xmp-zx-sid-test xmp-zx-isolated-test xmp-zx-helper xmp-export-test main_export.o export_wav.o main_zx_native.o zx_audio.o zx_audio_proxy.o zx_helper.o hvsc_download.o main.o xmp-resources.o ffmpeg_audio.o midi_audio.o sid_audio.o sid_audio_zx.o xmp-resources.c

.PHONY: all clean

hvsc_download.o: hvsc_download.c hvsc_download.h
	$(CC) $(CFLAGS) -c -o $@ hvsc_download.c
