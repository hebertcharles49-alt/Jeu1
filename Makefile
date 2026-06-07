# Makefile — moteur SCPS (simulation de civilisations, headless + visualiseur)
# Linux/Mac : make            → construit core_demo (banc d'essai vérifié, sans SDL)
# MinGW Win : make            (autodétection OS=Windows_NT) ou make WIN=1
#
# stb_perlin.h est vendorisé dans scps/ : le moteur est entièrement autonome.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
# Génération automatique des dépendances d'en-têtes (.d) : un .o est recompilé
# quand un .h qu'il inclut change.
CFLAGS  += -MMD -MP
OBJDIR  := build

# Détection automatique : MSYS2/MinGW expose OS=Windows_NT.
ifeq ($(OS),Windows_NT)
  WIN := 1
endif

# SDL n'est requis QUE par le visualiseur (scps_viewer). Les bancs d'essai
# headless (core_demo, scps_dump, …) se construisent sans SDL. La détection
# est silencieuse : en l'absence de sdl2-config, SDL_* reste vide.
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs   2>/dev/null)

ifdef WIN
  EXE     := .exe
  WINLIBS := -lopengl32 -mwindows -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
else
  EXE     :=
  WINLIBS := -lGL
endif

# Cible par défaut : le moteur vérifié §2, autonome (aucune dépendance SDL).
all: core_demo

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Compilation générique des sources scps/. SDL_CFLAGS n'ajoute que des chemins
# d'inclusion (inoffensif pour les fichiers headless).
$(OBJDIR)/scps_%.o: scps/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

# ---- Moteur SCPS headless (§2 + annexe) — colonne vertébrale VÉRIFIÉE -----
# Banc d'essai auto-vérifiant (35 contrôles, sortie ≠ 0 si échec).
CORE_DEMO_OBJS := $(OBJDIR)/scps_scps_core.o $(OBJDIR)/scps_core_demo.o
core_demo: $(CORE_DEMO_OBJS)
	$(CC) $(CORE_DEMO_OBJS) -o $@ -lm

# ---- Membrane diégétique (flottants SCPS → mots) — banc d'essai headless --
# Prouve le test décisif « Tenue · Contrainte » et la couverture du lexique.
READOUT_DEMO_OBJS := $(OBJDIR)/scps_scps_core.o $(OBJDIR)/scps_scps_readout.o \
                     $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_culture.o \
                     $(OBJDIR)/scps_readout_demo.o
readout_demo: $(READOUT_DEMO_OBJS)
	$(CC) $(READOUT_DEMO_OBJS) -o $@ -lm

# ---- Visualiseur de carte procédurale (SDL2/OpenGL) ----------------------
SCPS_SRCS := scps/scps_world.c scps/scps_render.c scps/scps_culture.c scps/viewer.c
SCPS_OBJS := $(SCPS_SRCS:scps/%.c=$(OBJDIR)/scps_%.o)
SCPS_TARGET := scps_viewer$(EXE)

scps: $(SCPS_TARGET)
$(SCPS_TARGET): $(SCPS_OBJS)
	$(CC) $(SCPS_OBJS) -o $@ $(SDL_LIBS) -lm $(WINLIBS)
run_scps: scps
	./$(SCPS_TARGET)

# ---- Générateur d'images headless (sans SDL) -----------------------------
SCPS_DUMP_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                  $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_econ.o \
                  $(OBJDIR)/scps_dump.o
scps_dump: $(SCPS_DUMP_OBJS)
	$(CC) $(SCPS_DUMP_OBJS) -o $@ -lm

# ---- Planche-contact de 5 mondes (montage.bmp) ---------------------------
SCPS_BATCH_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                   $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_batch.o
scps_batch: $(SCPS_BATCH_OBJS)
	$(CC) $(SCPS_BATCH_OBJS) -o $@ -lm

# ---- Banc d'essai économie + commerce ------------------------------------
ECON_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                  $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                  $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_econ_demo.o
econ_demo: $(ECON_DEMO_OBJS)
	$(CC) $(ECON_DEMO_OBJS) -o $@ -lm

# ---- Banc d'essai de l'arbre de technologies -----------------------------
TECH_DEMO_OBJS := $(OBJDIR)/scps_scps_tech.o $(OBJDIR)/scps_tech_demo.o
tech_demo: $(TECH_DEMO_OBJS)
	$(CC) $(TECH_DEMO_OBJS) -o $@ -lm

# ---- Banc d'essai des pools culturels ------------------------------------
CULTURE_DEMO_OBJS := $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_culture_demo.o
culture_demo: $(CULTURE_DEMO_OBJS)
	$(CC) $(CULTURE_DEMO_OBJS) -o $@ -lm

# ---- Banc d'essai du générateur de prospérité PE/SI ----------------------
PROSPERITY_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                        $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                        $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_tech.o \
                        $(OBJDIR)/scps_scps_core.o $(OBJDIR)/scps_scps_legitimacy.o \
                        $(OBJDIR)/scps_scps_prosperity.o $(OBJDIR)/scps_prosperity_demo.o
prosperity_demo: $(PROSPERITY_DEMO_OBJS)
	$(CC) $(PROSPERITY_DEMO_OBJS) -o $@ -lm

clean:
	rm -rf $(OBJDIR) scps_viewer scps_viewer.exe scps_dump scps_batch econ_demo \
	       tech_demo culture_demo prosperity_demo core_demo readout_demo out_*.ppm montage.bmp

.PHONY: all scps run_scps clean core_demo readout_demo scps_dump scps_batch econ_demo \
        tech_demo culture_demo prosperity_demo

# Inclusion des fichiers de dépendances générés (-MMD). Le tiret ignore leur
# absence au premier build.
-include $(wildcard $(OBJDIR)/*.d)
