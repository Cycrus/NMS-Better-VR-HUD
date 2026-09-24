CC := x86_64-w64-mingw32-gcc
CXX := x86_64-w64-mingw32-g++

MINHOOK := minhook
MINHOOK_INCLUDE := $(MINHOOK)/include
MINHOOK_SRC := $(MINHOOK)/src

CFLAGS := -O2 -Wall -Wextra -I$(MINHOOK_INCLUDE) -I$(MINHOOK_SRC) -I$(MINHOOK_SRC)/hde
CXXFLAGS := $(CFLAGS)

MINHOOK_OBJECTS := \
	buffer.o \
	hook.o \
	trampoline.o \
	hde64.o

default: HudTransformTest.asi

HudTransformTest.asi: hud_transform_test.o $(MINHOOK_OBJECTS)
	$(CXX) -shared -O2 -o $@ $^

hud_transform_test.o: hud_transform_test.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

buffer.o: $(MINHOOK_SRC)/buffer.c
	$(CC) $(CFLAGS) -c -o $@ $<

hook.o: $(MINHOOK_SRC)/hook.c
	$(CC) $(CFLAGS) -c -o $@ $<

trampoline.o: $(MINHOOK_SRC)/trampoline.c
	$(CC) $(CFLAGS) -c -o $@ $<

hde64.o: $(MINHOOK_SRC)/hde/hde64.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f HudTransformTest.asi hud_transform_test.o $(MINHOOK_OBJECTS)
