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

default: BetterVrHud.asi

BetterVrHud.asi: better_vr_hud.o $(MINHOOK_OBJECTS)
	$(CXX) -shared -O2 -o $@ $^

better_vr_hud.o: better_vr_hud.cpp better_vr_hud_math.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

buffer.o: $(MINHOOK_SRC)/buffer.c
	$(CC) $(CFLAGS) -c -o $@ $<

hook.o: $(MINHOOK_SRC)/hook.c
	$(CC) $(CFLAGS) -c -o $@ $<

trampoline.o: $(MINHOOK_SRC)/trampoline.c
	$(CC) $(CFLAGS) -c -o $@ $<

hde64.o: $(MINHOOK_SRC)/hde/hde64.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: better_vr_hud_math_test
	./better_vr_hud_math_test

better_vr_hud_math_test: better_vr_hud_math_test.cpp better_vr_hud_math.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ $<

clean:
	rm -f BetterVrHud.asi better_vr_hud.o better_vr_hud_math_test $(MINHOOK_OBJECTS)
