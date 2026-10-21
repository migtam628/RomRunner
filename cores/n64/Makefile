# RomRunner N64 core.
#   make            -> build/romrunner_n64_libretro.(so|dylib|dll)
#   make test       -> builds and runs the CPU and system tests
CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fPIC -Isrc -I../../packages/romrunner_core/src/third_party
LDLIBS = -lm

SRC = src/cpu.c src/fpu.c src/bus.c src/pif.c src/vi.c src/n64.c src/libretro.c
OBJ = $(SRC:src/%.c=build/%.o)

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  LIB = build/romrunner_n64_libretro.dylib
  SHARED = -dynamiclib
else ifneq (,$(findstring MINGW,$(UNAME)))
  LIB = build/romrunner_n64_libretro.dll
  SHARED = -shared
else
  LIB = build/romrunner_n64_libretro.so
  SHARED = -shared
endif

all: $(LIB)

build/%.o: src/%.c src/n64.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	$(CC) $(SHARED) -o $@ $(OBJ) $(LDLIBS)

build/cpu_test: test/cpu_test.c test/asm.h $(OBJ)
	$(CC) $(CFLAGS) -o $@ test/cpu_test.c $(OBJ) $(LDLIBS)

build/system_test: test/system_test.c test/asm.h $(OBJ)
	$(CC) $(CFLAGS) -o $@ test/system_test.c $(OBJ) $(LDLIBS)

test: build/cpu_test build/system_test
	./build/cpu_test
	./build/system_test

clean:
	rm -rf build

.PHONY: all test clean
