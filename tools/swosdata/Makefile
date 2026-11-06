# swosdata -- the data setup's conversion library and its command-line tool.
#
#   make -C tools/swosdata            # build/swosdata (the CLI) and the library
#   make -C tools/swosdata check      # convert, then compare with the reference trees
#
# The Slint setup app links the same sources through CMakeLists.txt. The
# converters are swosgfx's (third_party/swosgfx, a submodule: `git submodule
# update --init` first); lib/ here is what is OpenSWOS's own.

ROOT    := ../..
SG      := $(ROOT)/third_party/swosgfx
# miniz (the game's too): the setup reads .zip downloads of the Amiga version.
MZ      := $(ROOT)/third_party/miniz
OUT     := build
CC      ?= gcc
CFLAGS  ?= -O2 -g
# ARCHFLAGS: e.g. "-arch x86_64" for one slice of a macOS universal build.
ARCHFLAGS ?=
CFLAGS  += $(ARCHFLAGS) -std=c99 -Wall -Wextra -Wno-unused-parameter -Ilib -I$(SG)/lib -I$(MZ)
# OpenSWOS's include/, after the setup's own: the game's JSON code in lib/sd_json.c
CFLAGS  += -idirafter ../../include

LIB_SRCS := $(wildcard lib/*.c) $(wildcard $(SG)/lib/*.c) $(MZ)/miniz.c
LIB_OBJS := $(patsubst %.c,$(OUT)/obj/%.o,$(notdir $(LIB_SRCS)))

vpath %.c lib $(SG)/lib $(MZ) cli test

all: $(OUT)/swosdata

$(OUT)/obj/%.o: %.c | $(OUT)/obj
	$(CC) $(CFLAGS) -MMD -c $< -o $@

# Third-party: its own warnings are not ours to fix.
$(OUT)/obj/miniz.o: $(MZ)/miniz.c | $(OUT)/obj
	$(CC) $(CFLAGS) -w -MMD -c $< -o $@

$(OUT)/obj:
	mkdir -p $@

$(OUT)/libswosdata.a: $(LIB_OBJS)
	ar rcs $@ $^

$(OUT)/swosdata: $(OUT)/obj/swosdata_cli.o $(OUT)/libswosdata.a
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(OUT)/%_test: $(OUT)/obj/%_test.o $(OUT)/libswosdata.a
	$(CC) $(CFLAGS) $^ -o $@ -lm

clean:
	rm -rf $(OUT)

-include $(wildcard $(OUT)/obj/*.d)

.PHONY: all clean
