# Compilacion para PlayStation 2 (ps2sdk). Se usa dentro de la imagen ps2dev/ps2dev.
EE_BIN = bpiggies.elf
GSKIT ?= $(PS2DEV)/gsKit
BOX2D = third_party/box2d
B2_SRC = $(wildcard $(BOX2D)/src/collision/*.cpp) $(wildcard $(BOX2D)/src/common/*.cpp) $(wildcard $(BOX2D)/src/dynamics/*.cpp) $(wildcard $(BOX2D)/src/rope/*.cpp)
B2_OBJS = $(B2_SRC:.cpp=.o)
EE_OBJS = ps2_main.o game.o sprites.o font.o sounds.o audsrv_irx.o decor_raw.o terrain1_raw.o terrain2_raw.o terrain3_raw.o terrain4_raw.o terrain5_raw.o icon_sys.o icon_ico.o screen_title.o screen_worlds.o screen_levels.o screen_levels2.o music_main.o music_hai.o music_final.o $(B2_OBJS)
EE_INCS += -I$(GSKIT)/include -I$(BOX2D)/include -I$(BOX2D)/src -I.
EE_LDFLAGS += -L$(GSKIT)/lib
EE_CFLAGS += -O2
EE_CXXFLAGS += -O2 -fno-exceptions -fno-rtti -std=gnu++11 -Wno-psabi
# Si el sonido diera problemas, quitar el simbolo # de la linea siguiente para compilar sin sonido:
# EE_CXXFLAGS += -DNO_AUDIO
EE_LIBS += -lgskit -ldmakit -lpad -laudsrv -lmc -lm -lstdc++

all: $(EE_BIN)

# el modulo de audio del IOP se incrusta en el programa
audsrv_irx.c: $(PS2SDK)/iop/irx/audsrv.irx
	$(PS2SDK)/bin/bin2c $< $@ audsrv_irx

# decoraciones y musica: archivos binarios que se incrustan en el programa
decor_raw.c: decor.raw
	$(PS2SDK)/bin/bin2c $< $@ decor_raw
terrain1_raw.c: terrain1.raw
	$(PS2SDK)/bin/bin2c $< $@ terrain1_raw
screen_title.c: screen_title.raw
	$(PS2SDK)/bin/bin2c $< $@ screen_title
screen_worlds.c: screen_worlds.raw
	$(PS2SDK)/bin/bin2c $< $@ screen_worlds
screen_levels2.c: screen_levels2.raw
	$(PS2SDK)/bin/bin2c $< $@ screen_levels2
screen_levels.c: screen_levels.raw
	$(PS2SDK)/bin/bin2c $< $@ screen_levels
icon_sys.c: icon.sys
	$(PS2SDK)/bin/bin2c $< $@ icon_sys
icon_ico.c: bpiggies.ico
	$(PS2SDK)/bin/bin2c $< $@ icon_ico
terrain2_raw.c: terrain2.raw
	$(PS2SDK)/bin/bin2c $< $@ terrain2_raw
terrain3_raw.c: terrain3.raw
	$(PS2SDK)/bin/bin2c $< $@ terrain3_raw
terrain4_raw.c: terrain4.raw
	$(PS2SDK)/bin/bin2c $< $@ terrain4_raw
terrain5_raw.c: terrain5.raw
	$(PS2SDK)/bin/bin2c $< $@ terrain5_raw
music_main.c: music_main.raw
	$(PS2SDK)/bin/bin2c $< $@ music_main
music_hai.c: music_hai.raw
	$(PS2SDK)/bin/bin2c $< $@ music_hai
music_final.c: music_final.raw
	$(PS2SDK)/bin/bin2c $< $@ music_final

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
