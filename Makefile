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
# Génération automatique des dépendances d'en-têtes (.d) : un .o est
# recompilé quand un .h qu'il inclut change. Évite les objets périmés
# (ex. struct modifiée dans un header → ABI désynchronisée → crash).
CFLAGS += -MMD -MP

all: $(TARGET)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

# ---- Moteur SCPS (visualiseur de carte procédurale) ---------------------
SCPS_SRCS := scps/scps_world.c scps/scps_render.c scps/viewer.c
SCPS_OBJS := $(SCPS_SRCS:scps/%.c=$(OBJDIR)/scps_%.o)
SCPS_LDFLAGS := $(SDL_LIBS) -lm
ifdef WIN
  SCPS_TARGET := scps_viewer.exe
  SCPS_LDFLAGS += -lopengl32 -mwindows -static-libgcc \
                  -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
else
  SCPS_TARGET := scps_viewer
endif

scps: $(SCPS_TARGET)

$(OBJDIR)/scps_%.o: scps/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(SCPS_TARGET): $(SCPS_OBJS)
	$(CC) $(SCPS_OBJS) -o $@ $(SCPS_LDFLAGS)

run_scps: scps
	./$(SCPS_TARGET)

# Générateur d'images headless (sans SDL) — vérification de la génération
SCPS_DUMP_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o $(OBJDIR)/scps_dump.o
scps_dump: $(SCPS_DUMP_OBJS)
	$(CC) $(SCPS_DUMP_OBJS) -o $@ -lm

# Planche-contact de 5 mondes (montage.bmp) — revue rapide après chaque
# modification du générateur.
SCPS_BATCH_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o $(OBJDIR)/scps_batch.o
scps_batch: $(SCPS_BATCH_OBJS)
	$(CC) $(SCPS_BATCH_OBJS) -o $@ -lm

# Banc d'essai du moteur économique (console, sans SDL)
ECON_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                  $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                  $(OBJDIR)/scps_econ_demo.o
econ_demo: $(ECON_DEMO_OBJS)
	$(CC) $(ECON_DEMO_OBJS) -o $@ -lm

# Banc d'essai de l'arbre de technologies (console, autonome)
TECH_DEMO_OBJS := $(OBJDIR)/scps_scps_tech.o $(OBJDIR)/scps_tech_demo.o
tech_demo: $(TECH_DEMO_OBJS)
	$(CC) $(TECH_DEMO_OBJS) -o $@ -lm

# Banc d'essai des pools culturels (console, autonome)
CULTURE_DEMO_OBJS := $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_culture_demo.o
culture_demo: $(CULTURE_DEMO_OBJS)
	$(CC) $(CULTURE_DEMO_OBJS) -o $@ -lm

PROSPERITY_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                        $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                        $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_tech.o \
                        $(OBJDIR)/scps_scps_prosperity.o $(OBJDIR)/scps_prosperity_demo.o
prosperity_demo: $(PROSPERITY_DEMO_OBJS)
	$(CC) $(PROSPERITY_DEMO_OBJS) -o $@ -lm

clean:
	rm -rf $(OBJDIR) $(TARGET) crucible crucible.exe element_dungeon element_dungeon.exe \
	       scps_viewer scps_viewer.exe scps_dump scps_batch econ_demo tech_demo culture_demo \
	       prosperity_demo out_*.ppm montage.bmp

.PHONY: all run scps run_scps clean prosperity_demo

# Inclusion des fichiers de dépendances générés (-MMD). Le tiret ignore
# leur absence au premier build.
-include $(OBJS:.o=.d)
-include $(SCPS_OBJS:.o=.d)
-include $(OBJDIR)/scps_dump.d
-include $(OBJDIR)/scps_batch.d
-include $(OBJDIR)/scps_scps_econ.d
-include $(OBJDIR)/scps_scps_trade.d
-include $(OBJDIR)/scps_econ_demo.d
-include $(OBJDIR)/scps_scps_tech.d
-include $(OBJDIR)/scps_tech_demo.d
-include $(OBJDIR)/scps_scps_culture.d
-include $(OBJDIR)/scps_culture_demo.d
-include $(OBJDIR)/scps_scps_prosperity.d
-include $(OBJDIR)/scps_prosperity_demo.d
