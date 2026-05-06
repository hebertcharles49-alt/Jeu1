# Makefile pour Crucible
# Usage:
#   Linux/Mac : make
#   MinGW Win : make WIN=1   (ou utilise build.bat)

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
SRCS    := src/main.c src/world.c src/combat.c src/render.c src/meta.c
OBJDIR  := build
OBJS    := $(SRCS:src/%.c=$(OBJDIR)/%.o)

ifdef WIN
  # MinGW / MSYS2: paquets requis: mingw-w64-x86_64-SDL2 mingw-w64-x86_64-gcc
  TARGET  := crucible.exe
  SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
  SDL_LIBS   := $(shell sdl2-config --libs   2>/dev/null)
  LDFLAGS := $(SDL_LIBS) -lm -mwindows
else
  TARGET  := crucible
  SDL_CFLAGS := $(shell sdl2-config --cflags)
  SDL_LIBS   := $(shell sdl2-config --libs)
  LDFLAGS := $(SDL_LIBS) -lm
endif

CFLAGS += $(SDL_CFLAGS)

all: $(TARGET)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET) crucible crucible.exe

.PHONY: all run clean
