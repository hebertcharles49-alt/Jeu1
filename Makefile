# Makefile pour Crucible
# Linux/Mac : make
# MinGW Win : make  (autodetection via OS=Windows_NT) ou make WIN=1

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
# Auto-detection : on prend tous les .c sous src/ . Pour ajouter un fichier,
# il suffit de le poser ici, pas besoin de toucher au Makefile.
SRCS    := $(wildcard src/*.c)
OBJDIR  := build
OBJS    := $(SRCS:src/%.c=$(OBJDIR)/%.o)

# Detection automatique : MSYS2/MinGW expose OS=Windows_NT.
ifeq ($(OS),Windows_NT)
  WIN := 1
endif

ifdef WIN
  TARGET  := element_dungeon.exe
  SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
  SDL_LIBS   := $(shell sdl2-config --libs   2>/dev/null)
  # Link statique de la runtime MinGW (libgcc + winpthread) pour que
  # l'exe ne depende plus de libgcc_s_seh-1.dll / libwinpthread-1.dll.
  # Seule SDL2.dll reste dynamique et est embarquee dans le setup.exe.
  LDFLAGS := $(SDL_LIBS) -lm -lopengl32 -mwindows \
             -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
else
  TARGET  := element_dungeon
  SDL_CFLAGS := $(shell sdl2-config --cflags)
  SDL_LIBS   := $(shell sdl2-config --libs)
  LDFLAGS := $(SDL_LIBS) -lm -lGL
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
	rm -rf $(OBJDIR) $(TARGET) crucible crucible.exe element_dungeon element_dungeon.exe

.PHONY: all run clean
