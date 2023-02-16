NAME = gameboy_c

HEADERDIR = ../headers
OBJDIR = ../objs

CC = gcc
CFLAGS = -Wall -O2 -MMD -MP `pkg-config --cflags sdl2` -I $(HEADERDIR)
LDFLAGS = `pkg-config --libs sdl2` -lpthread

DEPS = cart.h cpu.h dma.h ui.h emulator.h ppu.h hdma.h gamepad.h interrupts.h bus.h rtc.h sdl.h spu.h sync.h timer.h
OBJS = main.o cpu.o bus.o cart.o ppu.o sync.o sdl.o gamepad.o interrupts.o dma.o timer.o spu.o hdma.o rtc.o

DEP = $(patsubst %,$(HEADERDIR)/%,$(DEPS))
OBJ = $(patsubst %,$(OBJDIR)/%,$(OBJS))

$(OBJDIR)/%.o: %.c $(DEP)
	@mkdir -p $(OBJDIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(NAME): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

.PHONY : clean

clean:
	rm -f $(OBJDIR)/*.o $(OBJDIR)/*.d *~ core gameboy_c
