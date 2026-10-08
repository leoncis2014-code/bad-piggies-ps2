# Compilacion para PlayStation 2 (ps2sdk). Se usa dentro de la imagen ps2dev/ps2dev.
EE_BIN = bpiggies.elf
GSKIT ?= $(PS2DEV)/gsKit
BOX2D = third_party/box2d
B2_SRC = $(wildcard $(BOX2D)/src/collision/*.cpp) $(wildcard $(BOX2D)/src/common/*.cpp) $(wildcard $(BOX2D)/src/dynamics/*.cpp) $(wildcard $(BOX2D)/src/rope/*.cpp)
B2_OBJS = $(B2_SRC:.cpp=.o)
EE_OBJS = ps2_main.o game.o sprites.o font.o sounds.o audsrv_irx.o $(B2_OBJS)
EE_INCS += -I$(GSKIT)/include -I$(BOX2D)/include -I$(BOX2D)/src -I.
EE_LDFLAGS += -L$(GSKIT)/lib
EE_CFLAGS += -O2
EE_CXXFLAGS += -O2 -fno-exceptions -fno-rtti -std=gnu++11 -Wno-psabi
# Si el sonido diera problemas, quitar el simbolo # de la linea siguiente para compilar sin sonido:
# EE_CXXFLAGS += -DNO_AUDIO
EE_LIBS += -lgskit -ldmakit -lpad -laudsrv -lm -lstdc++

all: $(EE_BIN)

# el modulo de audio del IOP se incrusta en el programa
audsrv_irx.c: $(PS2SDK)/iop/irx/audsrv.irx
	$(PS2SDK)/bin/bin2c $< $@ audsrv_irx

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
