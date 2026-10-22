# RomRunner PS1 core.
#   make            -> build/romrunner_ps1_libretro.(so|dylib|dll)
#   make test       -> builds and runs the tests
CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fPIC -Isrc -I../../packages/romrunner_core/src/third_party
LDLIBS = -lm

SRC = src/cpu.c src/bus.c src/dma.c src/timers.c src/gpu.c src/raster.c src/gte.c src/sio.c src/stubs.c src/hle.c src/ps1.c src/libretro.c
OBJ = $(SRC:src/%.c=build/%.o)
CORE_OBJ = $(filter-out build/libretro.o,$(OBJ))

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  LIB = build/romrunner_ps1_libretro.dylib
  SHARED = -dynamiclib
else ifneq (,$(findstring MINGW,$(UNAME)))
  LIB = build/romrunner_ps1_libretro.dll
  SHARED = -shared
else
  LIB = build/romrunner_ps1_libretro.so
  SHARED = -shared
endif

all: $(LIB)

build/%.o: src/%.c src/ps1.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	$(CC) $(SHARED) -o $@ $(OBJ) $(LDLIBS)

build/cpu_test: test/cpu_test.c test/asm.h $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ test/cpu_test.c $(CORE_OBJ) $(LDLIBS)

build/exerun: test/exerun.c $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ test/exerun.c $(CORE_OBJ) $(LDLIBS)

tools: build/exerun

test: build/cpu_test
	./build/cpu_test

clean:
	rm -rf build

.PHONY: all test tools clean
