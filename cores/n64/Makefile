# RomRunner N64 core.
#   make            -> build/romrunner_n64_libretro.(so|dylib|dll)
#   make test       -> builds and runs the CPU and system tests
CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fPIC -Isrc -I../../packages/romrunner_core/src/third_party
LDLIBS = -lm -lpthread

SRC = src/cpu.c src/fpu.c src/rsp.c src/rdp.c src/rdp_thread.c src/bus.c src/pif.c src/flash.c src/vi.c src/n64.c src/libretro.c
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

build/rsp_test: test/rsp_test.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ test/rsp_test.c $(OBJ) $(LDLIBS)

build/rdp_test: test/rdp_test.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ test/rdp_test.c $(OBJ) $(LDLIBS)

build/romtest: test/romtest.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ test/romtest.c $(OBJ) $(LDLIBS)

build/chksum64: test/chksum64.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ test/chksum64.c $(OBJ) $(LDLIBS)

build/play: test/play.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ test/play.c $(OBJ) $(LDLIBS)

tools: build/romtest build/chksum64 build/play

test: build/cpu_test build/rsp_test build/rdp_test build/system_test build/romtest
	./build/cpu_test
	./build/rsp_test
	./build/rdp_test
	./build/system_test

clean:
	rm -rf build

.PHONY: all test tools clean
