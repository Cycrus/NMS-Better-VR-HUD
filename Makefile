# Builds the ASI plugins with mingw-w64.
#   Windows: mingw32-make           (WinLibs / MSYS2 toolchain on PATH)
#   Linux:   make                   (x86_64-w64-mingw32 cross compiler)
#
#   build/BetterVrHud.asi  the mod
#   build/FrameProbe.asi   diagnostic logger (install one or the other, not both)

ifeq ($(OS),Windows_NT)
  CC := gcc
  CXX := g++
  MKDIR = if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
  RMDIR = if exist "$(subst /,\,$(1))" rmdir /s /q "$(subst /,\,$(1))"
else
  CC := x86_64-w64-mingw32-gcc
  CXX := x86_64-w64-mingw32-g++
  MKDIR = mkdir -p $(1)
  RMDIR = rm -rf $(1)
endif

BUILD := build
MINHOOK := third_party/minhook

CPPFLAGS := -Isrc -I$(MINHOOK)/include
CFLAGS := -O2 -Wall
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra
LDFLAGS := -shared -static -static-libgcc -static-libstdc++ -s
LDLIBS := -luser32

MINHOOK_SRC := $(MINHOOK)/src/buffer.c $(MINHOOK)/src/hook.c $(MINHOOK)/src/trampoline.c $(MINHOOK)/src/hde/hde64.c
COMMON_SRC := $(wildcard src/common/*.cpp)

MINHOOK_OBJ := $(patsubst %.c,$(BUILD)/obj/%.o,$(MINHOOK_SRC))
COMMON_OBJ := $(patsubst %.cpp,$(BUILD)/obj/%.o,$(COMMON_SRC))
MOD_OBJ := $(BUILD)/obj/src/mod/better_vr_hud.o
PROBE_OBJ := $(BUILD)/obj/tools/FrameProbe/frame_probe.o
TEST_OBJ := $(BUILD)/obj/tools/tests/resolve_test.o

.PHONY: all tests clean
all: $(BUILD)/BetterVrHud.asi $(BUILD)/FrameProbe.asi

$(BUILD)/BetterVrHud.asi: $(MOD_OBJ) $(COMMON_OBJ) $(MINHOOK_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Run: build\mat4_test.exe and build\resolve_test.exe "<path to NMS.exe>"
tests: $(BUILD)/resolve_test.exe $(BUILD)/mat4_test.exe

$(BUILD)/mat4_test.exe: $(BUILD)/obj/tools/tests/mat4_test.o
	$(CXX) -static -o $@ $^

$(BUILD)/FrameProbe.asi: $(PROBE_OBJ) $(COMMON_OBJ) $(MINHOOK_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/resolve_test.exe: $(TEST_OBJ) $(COMMON_OBJ) $(MINHOOK_OBJ)
	$(CXX) -municode -static -static-libgcc -static-libstdc++ -o $@ $^ $(LDLIBS)

$(BUILD)/obj/%.o: %.cpp $(wildcard src/common/*.h)
	@$(call MKDIR,$(@D))
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/obj/%.o: %.c
	@$(call MKDIR,$(@D))
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	@$(call RMDIR,$(BUILD))
