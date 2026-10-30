CC ?= cc
CXX ?= c++
CFLAGS += -O2 -Wall -Wextra $(shell pkg-config --cflags gtk+-3.0 gio-2.0 sdl2 libxmp libavformat libavcodec libavutil libswresample fluidsynth)
CXXFLAGS += -O2 -Wall -Wextra -std=c++11 $(shell pkg-config --cflags libsidplayfp)
LDLIBS += $(shell pkg-config --libs gtk+-3.0 gio-2.0 sdl2 libxmp libavformat libavcodec libavutil libswresample fluidsynth) $(shell pkg-config --libs libsidplayfp) -lm

all: xmp-linux

# Native ZX Spectrum playback is the default for xmp-linux. The original
# zxtune123 -> temporary WAV implementation remains available as xmp-zx-wav.
# Native linking requires the static libraries built by ZXTune.
ZX_ROOT ?= $(HOME)/zxtune
ZX_INCLUDE = -I$(ZX_ROOT) -I$(ZX_ROOT)/include -I$(ZX_ROOT)/src
ZX_LIBRARIES = $(wildcard $(ZX_ROOT)/lib/linux/release/*.a)
IRC_OBJECTS = xmp_irc_widget.o irc_client.o irc_protocol.o
ZX_NATIVE_OBJECTS = main_zx_native.o zx_audio.o xmp-resources.o ffmpeg_audio.o sid_audio.o midi_audio.o equalizer.o hvsc_download.o $(IRC_OBJECTS)
ZX_ISOLATED_OBJECTS = $(filter-out zx_audio.o main_zx_native.o,$(ZX_NATIVE_OBJECTS)) zx_audio_proxy.o main_export.o export_wav.o
EXPORT_TEST_OBJECTS = $(ZX_ISOLATED_OBJECTS)
ZX_SID_TEST_OBJECTS = $(filter-out sid_audio.o,$(ZX_NATIVE_OBJECTS)) sid_audio_zx.o
ZX_WAV_OBJECTS = main.o xmp-resources.o ffmpeg_audio.o sid_audio.o midi_audio.o equalizer.o hvsc_download.o $(IRC_OBJECTS)

# The tested default isolates libsidplayfp and ZXTune in different processes.
# Both render PCM directly, without a temporary WAV or conflicting SID symbols.
xmp-linux xmp-zx-native xmp-zx-isolated-test: $(ZX_ISOLATED_OBJECTS) xmp-zx-helper Makefile
	$(CXX) -o $@ $(ZX_ISOLATED_OBJECTS) $(LDLIBS) -ldl -lpthread -lz

# Keep the WAV-export regression-test executable available alongside the main build.
xmp-export-test: $(EXPORT_TEST_OBJECTS) xmp-zx-helper Makefile
	$(CXX) -o $@ $(EXPORT_TEST_OBJECTS) $(LDLIBS) -ldl -lpthread -lz

main_export.o: main.c ffmpeg_audio.h sid_audio.h midi_audio.h equalizer.h zx_audio.h export_wav.h xmp_irc_widget.h
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

main_zx_native.o: main.c ffmpeg_audio.h sid_audio.h midi_audio.h equalizer.h zx_audio.h xmp_irc_widget.h
	$(CC) $(CFLAGS) -DENABLE_ZXTUNE_NATIVE -c -o $@ main.c

zx_audio.o: zx_audio.cpp zx_audio.h
	$(CXX) $(CXXFLAGS) -std=c++20 $(ZX_INCLUDE) -c -o $@ zx_audio.cpp

xmp-zx-wav: $(ZX_WAV_OBJECTS) Makefile
	$(CXX) -o $@ $(ZX_WAV_OBJECTS) $(LDLIBS)

main.o: main.c ffmpeg_audio.h sid_audio.h midi_audio.h equalizer.h xmp_irc_widget.h
	$(CC) $(CFLAGS) -c -o $@ main.c

xmp_irc_widget.o: xmp_irc_widget.c xmp_irc_widget.h irc_client.h irc_protocol.h
	$(CC) $(CFLAGS) -c -o $@ xmp_irc_widget.c

irc_client.o: irc_client.c irc_client.h irc_protocol.h
	$(CC) $(CFLAGS) -c -o $@ irc_client.c

irc_protocol.o: irc_protocol.c irc_protocol.h
	$(CC) $(CFLAGS) -c -o $@ irc_protocol.c

ffmpeg_audio.o: ffmpeg_audio.c ffmpeg_audio.h
	$(CC) $(CFLAGS) -c -o $@ ffmpeg_audio.c

sid_audio.o: sid_audio.cpp sid_audio.h
	$(CXX) $(CXXFLAGS) -c -o $@ sid_audio.cpp

midi_audio.o: midi_audio.c midi_audio.h
	$(CC) $(CFLAGS) -c -o $@ midi_audio.c

equalizer.o: equalizer.c equalizer.h
	$(CC) $(CFLAGS) -c -o $@ equalizer.c

xmp-resources.o: xmp-resources.c
	$(CC) $(CFLAGS) -c -o $@ xmp-resources.c

xmp-resources.c: xmp.gresource.xml icons/xmp.png
	glib-compile-resources --generate-source --target=$@ $<

clean:
	rm -f xmp-linux xmp-zx-native xmp-zx-wav xmp-zx-sid-test xmp-zx-isolated-test xmp-zx-helper xmp-export-test main_export.o export_wav.o main_zx_native.o zx_audio.o zx_audio_proxy.o zx_helper.o hvsc_download.o main.o xmp-resources.o ffmpeg_audio.o midi_audio.o equalizer.o sid_audio.o sid_audio_zx.o xmp-resources.c $(IRC_OBJECTS)

.PHONY: all clean

# Checks the pure-C DSP without GTK, SDL or other desktop libraries.
check-eq:
	$(CC) -O2 -Wall -Wextra -Werror -std=c11 -o /tmp/xmp-check-eq \
		test_equalizer.c equalizer.c -lm
	/tmp/xmp-check-eq

.PHONY: check-eq

hvsc_download.o: hvsc_download.c hvsc_download.h
	$(CC) $(CFLAGS) -c -o $@ hvsc_download.c

# Check the IRC parser and GTK3/GIO integration without linking ZXTune.
check-irc:
	$(CC) -std=c11 -Wall -Wextra -Wpedantic \
		$(shell pkg-config --cflags gtk+-3.0 gio-2.0) \
		-fsyntax-only xmp_irc_widget.c irc_client.c irc_protocol.c
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -o /tmp/xmp-check-irc \
		test_irc_protocol.c irc_protocol.c
	/tmp/xmp-check-irc

.PHONY: check-irc
