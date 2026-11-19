# NetViz 3D - build file.   `make` builds the app, `make test`, `make bench`, `make gen`, `make asan`.
#
# raylib location (only needed if the compiler cannot find it on its own):
#   make RAYLIB_INC=/path/to/raylib/include RAYLIB_LIB=/path/to/raylib/lib
#
# On MSYS2 UCRT64:  pacman -S mingw-w64-ucrt-x86_64-raylib mingw-w64-ucrt-x86_64-make   (then: mingw32-make)

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
BUILD    := build

CORE_SRC := $(wildcard src/core/*.cpp)
APP_SRC  := $(wildcard src/app/*.cpp)
CORE_OBJ := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CORE_SRC))
APP_OBJ  := $(patsubst src/%.cpp,$(BUILD)/%.o,$(APP_SRC))

INC := -Isrc/core
ifdef RAYLIB_INC
  APP_INC := -isystem $(RAYLIB_INC)
endif
ifdef RAYLIB_LIB
  LIBDIR := -L$(RAYLIB_LIB)
endif

ifeq ($(OS),Windows_NT)
  EXE := .exe
  GFX := -lraylib -lopengl32 -lgdi32 -lwinmm
else ifeq ($(shell uname -s),Darwin)
  GFX := -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
else
  GFX := -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
endif

.PHONY: all test bench gen asan clean
all: netviz$(EXE)

netviz$(EXE): $(CORE_OBJ) $(APP_OBJ)
	$(CXX) $^ -o $@ $(LIBDIR) $(GFX)

# --- tools that do NOT need raylib -------------------------------------------------------
netviz_tests$(EXE): $(CORE_OBJ) $(BUILD)/tools/tests.o
	$(CXX) $^ -o $@
netviz_bench$(EXE): $(CORE_OBJ) $(BUILD)/tools/benchmark.o
	$(CXX) $^ -o $@
netviz_gen$(EXE): $(CORE_OBJ) $(BUILD)/tools/gen_dataset.o
	$(CXX) $^ -o $@

test: netviz_tests$(EXE)
	./netviz_tests$(EXE)
bench: netviz_bench$(EXE)
	./netviz_bench$(EXE)
gen: netviz_gen$(EXE)

# Tests under AddressSanitizer + UBSan (not available in MinGW GCC; use Linux/WSL or clang64).
asan:
	$(CXX) -std=c++17 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(CORE_SRC) src/tools/tests.cpp -o netviz_tests_asan$(EXE)
	./netviz_tests_asan$(EXE)

# --- compile rules (header dependencies tracked with -MMD) ---------------------------------
$(BUILD)/app/%.o: src/app/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INC) $(APP_INC) -MMD -MP -c $< -o $@
$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INC) -MMD -MP -c $< -o $@

-include $(CORE_OBJ:.o=.d) $(APP_OBJ:.o=.d)

clean:
	rm -rf $(BUILD) netviz netviz.exe netviz_tests netviz_tests.exe netviz_bench netviz_bench.exe \
	       netviz_gen netviz_gen.exe netviz_tests_asan netviz_tests_asan.exe netviz_autoshot.png
