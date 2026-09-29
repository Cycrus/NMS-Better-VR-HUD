CC := x86_64-w64-mingw32-gcc
CXX := x86_64-w64-mingw32-g++
HOST_CXX := g++

MINHOOK := ../third-party/minhook
MINHOOK_INCLUDE := $(MINHOOK)/include
MINHOOK_SRC := $(MINHOOK)/src

CFLAGS := -O2 -Wall -Wextra -I$(MINHOOK_INCLUDE) -I$(MINHOOK_SRC) -I$(MINHOOK_SRC)/hde
CXXFLAGS := $(CFLAGS) -std=c++17
HOST_CXXFLAGS := -O2 -Wall -Wextra -std=c++17

MINHOOK_OBJECTS := \
	buffer.o \
	hook.o \
	trampoline.o \
	hde64.o

APP_OBJECTS := \
	math.o \
	nms_signatures.o \
	signature_scanner.o

default: BetterVrHudLevel.asi BetterVrHudFull.asi

BetterVrHudLevel.asi: better_vr_hud_level.o $(APP_OBJECTS) $(MINHOOK_OBJECTS)
	$(CXX) -shared -O2 -o $@ $^

BetterVrHudFull.asi: better_vr_hud_full.o $(APP_OBJECTS) $(MINHOOK_OBJECTS)
	$(CXX) -shared -O2 -o $@ $^

better_vr_hud_level.o: better_vr_hud.cpp math.h nms_signatures.h signature_scanner.h
	$(CXX) $(CXXFLAGS) -DLEVEL_HUD -c -o $@ $<

better_vr_hud_full.o: better_vr_hud.cpp math.h nms_signatures.h signature_scanner.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

math.o: math.cpp math.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

nms_signatures.o: nms_signatures.cpp nms_signatures.h signature_scanner.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

signature_scanner.o: signature_scanner.cpp signature_scanner.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

buffer.o: $(MINHOOK_SRC)/buffer.c
	$(CC) $(CFLAGS) -c -o $@ $<

hook.o: $(MINHOOK_SRC)/hook.c
	$(CC) $(CFLAGS) -c -o $@ $<

trampoline.o: $(MINHOOK_SRC)/trampoline.c
	$(CC) $(CFLAGS) -c -o $@ $<

hde64.o: $(MINHOOK_SRC)/hde/hde64.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: math_test signature_scanner_test
	./math_test
	./signature_scanner_test

math_test: math_test.cpp math.cpp math.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ math_test.cpp math.cpp

signature_scanner_test: signature_scanner_test.cpp signature_scanner.cpp signature_scanner.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ signature_scanner_test.cpp signature_scanner.cpp

clean:
	rm -f BetterVrHud.asi BetterVrHudLevel.asi BetterVrHudFull.asi better_vr_hud.o better_vr_hud_level.o better_vr_hud_full.o $(APP_OBJECTS) math_test signature_scanner_test $(MINHOOK_OBJECTS)
