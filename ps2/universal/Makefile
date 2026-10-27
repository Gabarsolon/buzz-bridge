# Build in a shell with an existing ps2dev toolchain, see TOOLCHAIN.md.
# ps2sdk build conventions: Copyright ps2dev - http://www.ps2dev.org
# Licensed under the Academic Free License version 2.0 (licenses/AFL-2.0.txt).
# Integration changes Copyright (c) 2026 Gabarsolon, MIT (../../LICENSE).
ifndef PS2SDK
$(error Set PS2SDK to your installed SDK; see TOOLCHAIN.md)
endif

IOP_BIN = bbusbd.irx
IOP_OBJS = exports.o imports.o main.o gpcall.o vusb.o bb_core.o bbpad.o
IOP_SRC_DIR = src/
IOP_INC_DIR = src/
IOP_OBJS_DIR = obj/
IOP_WARNFLAGS = -Wall -Werror
IOP_CFLAGS += -I$(IOP_SRC_DIR)

all: $(IOP_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal

.PHONY: all clean

clean:
	rm -rf obj $(IOP_BIN) $(IOP_BIN_ELF) $(IOP_BIN_STRIPPED_ELF)
