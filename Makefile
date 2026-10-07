# Compilacion para PlayStation 2 (ps2sdk). Se usa dentro de la imagen ps2dev/ps2dev.
EE_BIN = bpiggies.elf
BOX2D = third_party/box2d
B2_SRC = $(wildcard $(BOX2D)/src/collision/*.cpp) $(wildcard $(BOX2D)/src/common/*.cpp) \
         $(wildcard $(BOX2D)/src/dynamics/*.cpp) $(wildcard $(BOX2D)/src/rope/*.cpp)
B2_OBJS = $(B2_SRC:.cpp=.o)
EE_OBJS = ps2_main.o game.o sprites.o $(B2_OBJS)
EE_INCS += -I$(BOX2D)/include -I$(BOX2D)/src -I.
EE_CFLAGS += -O2
EE_CXXFLAGS += -O2 -fno-exceptions -fno-rtti -std=gnu++11 -Wno-psabi
EE_LIBS += -lgskit -ldmakit -lpad -lm -lstdc++

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
