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
                     $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_readout_demo.o
readout_demo: $(READOUT_DEMO_OBJS)
	$(CC) $(READOUT_DEMO_OBJS) -o $@ -lm

# ---- Roster de races & système de traits (autonome) ----------------------
SPECIES_DEMO_OBJS := $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_species_demo.o
species_demo: $(SPECIES_DEMO_OBJS)
	$(CC) $(SPECIES_DEMO_OBJS) -o $@

# ---- Visualiseur de carte + UI diégétique (SDL2 + SDL_ttf) ---------------
# Le viewer lie toute la chaîne sim (la membrane scps_readout traduit en mots),
# mais N'inclut PAS scps_core.h (cloison vérifiée par grep).
SCPS_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
             $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_econ.o \
             $(OBJDIR)/scps_scps_trade.o $(OBJDIR)/scps_scps_tech.o \
             $(OBJDIR)/scps_scps_core.o $(OBJDIR)/scps_scps_legitimacy.o \
             $(OBJDIR)/scps_scps_prosperity.o $(OBJDIR)/scps_scps_readout.o \
             $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_scps_diplo.o \
             $(OBJDIR)/scps_scps_routes.o $(OBJDIR)/scps_scps_statecraft.o \
             $(OBJDIR)/scps_scps_agency.o $(OBJDIR)/scps_scps_events.o \
             $(OBJDIR)/scps_scps_demography.o $(OBJDIR)/scps_scps_labor.o \
             $(OBJDIR)/scps_scps_modifier.o $(OBJDIR)/scps_scps_ai.o \
             $(OBJDIR)/scps_viewer.o
SCPS_TARGET := scps_viewer$(EXE)

scps: $(SCPS_TARGET)
$(SCPS_TARGET): $(SCPS_OBJS)
	$(CC) $(SCPS_OBJS) -o $@ $(SDL_LIBS) -lSDL2_ttf -lm $(WINLIBS)
run_scps: scps
	./$(SCPS_TARGET)

# ---- Générateur d'images headless (sans SDL) -----------------------------
SCPS_DUMP_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                  $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_econ.o \
                  $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_dump.o
scps_dump: $(SCPS_DUMP_OBJS)
	$(CC) $(SCPS_DUMP_OBJS) -o $@ -lm

# ---- Planche-contact de 5 mondes (montage.bmp) ---------------------------
SCPS_BATCH_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                   $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_batch.o
scps_batch: $(SCPS_BATCH_OBJS)
	$(CC) $(SCPS_BATCH_OBJS) -o $@ -lm

# ---- Banc d'essai économie + commerce ------------------------------------
ECON_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                  $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                  $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_econ_demo.o
econ_demo: $(ECON_DEMO_OBJS)
	$(CC) $(ECON_DEMO_OBJS) -o $@ -lm

ECON_TAX_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                  $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                  $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_econ_tax_demo.o
econ_tax_demo: $(ECON_TAX_DEMO_OBJS)
	$(CC) $(ECON_TAX_DEMO_OBJS) -o $@ -lm

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
                        $(OBJDIR)/scps_scps_prosperity.o $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_prosperity_demo.o
prosperity_demo: $(PROSPERITY_DEMO_OBJS)
	$(CC) $(PROSPERITY_DEMO_OBJS) -o $@ -lm

# ---- Couche d'agency : actions, temps, bâtiments-leviers (§1-§2) ----------
AGENCY_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                    $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                    $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_tech.o \
                    $(OBJDIR)/scps_scps_core.o $(OBJDIR)/scps_scps_legitimacy.o \
                    $(OBJDIR)/scps_scps_prosperity.o $(OBJDIR)/scps_scps_species.o \
                    $(OBJDIR)/scps_scps_readout.o $(OBJDIR)/scps_scps_agency.o \
                    $(OBJDIR)/scps_agency_demo.o
agency_demo: $(AGENCY_DEMO_OBJS)
	$(CC) $(AGENCY_DEMO_OBJS) -o $@ -lm

# ---- Diplomatie & guerre (§5-§6) -----------------------------------------
DIPLO_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                   $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                   $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_tech.o \
                   $(OBJDIR)/scps_scps_core.o $(OBJDIR)/scps_scps_legitimacy.o \
                   $(OBJDIR)/scps_scps_prosperity.o $(OBJDIR)/scps_scps_species.o \
                   $(OBJDIR)/scps_scps_readout.o $(OBJDIR)/scps_scps_diplo.o \
                   $(OBJDIR)/scps_diplo_demo.o
diplo_demo: $(DIPLO_DEMO_OBJS)
	$(CC) $(DIPLO_DEMO_OBJS) -o $@ -lm

# ---- Routes commerciales : la cloche f(D̄) faite action (§7) --------------
ROUTES_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_render.o \
                    $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_trade.o \
                    $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_tech.o \
                    $(OBJDIR)/scps_scps_core.o $(OBJDIR)/scps_scps_legitimacy.o \
                    $(OBJDIR)/scps_scps_prosperity.o $(OBJDIR)/scps_scps_species.o \
                    $(OBJDIR)/scps_scps_readout.o $(OBJDIR)/scps_scps_routes.o \
                    $(OBJDIR)/scps_routes_demo.o
routes_demo: $(ROUTES_DEMO_OBJS)
	$(CC) $(ROUTES_DEMO_OBJS) -o $@ -lm

# ---- Boucle de décision IA : un lecteur de coordonnées qui choisit des leviers (§13.1)
# Aucune dépendance membrane (l'IA lit les coordonnées du moteur, pas les mots).
AI_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_econ.o \
                $(OBJDIR)/scps_scps_trade.o $(OBJDIR)/scps_scps_culture.o \
                $(OBJDIR)/scps_scps_tech.o $(OBJDIR)/scps_scps_core.o \
                $(OBJDIR)/scps_scps_legitimacy.o $(OBJDIR)/scps_scps_prosperity.o \
                $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_scps_agency.o \
                $(OBJDIR)/scps_scps_routes.o $(OBJDIR)/scps_scps_diplo.o \
                $(OBJDIR)/scps_scps_ai.o $(OBJDIR)/scps_ai_demo.o
ai_demo: $(AI_DEMO_OBJS)
	$(CC) $(AI_DEMO_OBJS) -o $@ -lm

CHRONICLE_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_econ.o \
                  $(OBJDIR)/scps_scps_trade.o $(OBJDIR)/scps_scps_culture.o \
                  $(OBJDIR)/scps_scps_tech.o $(OBJDIR)/scps_scps_core.o \
                  $(OBJDIR)/scps_scps_legitimacy.o $(OBJDIR)/scps_scps_prosperity.o \
                  $(OBJDIR)/scps_scps_readout.o $(OBJDIR)/scps_scps_species.o \
                  $(OBJDIR)/scps_scps_diplo.o $(OBJDIR)/scps_scps_routes.o \
                  $(OBJDIR)/scps_scps_statecraft.o $(OBJDIR)/scps_scps_agency.o \
                  $(OBJDIR)/scps_scps_events.o $(OBJDIR)/scps_scps_demography.o \
                  $(OBJDIR)/scps_scps_labor.o $(OBJDIR)/scps_scps_modifier.o \
                  $(OBJDIR)/scps_scps_ai.o $(OBJDIR)/scps_chronicle.o
chronicle: $(CHRONICLE_OBJS)
	$(CC) $(CHRONICLE_OBJS) -o $@ -lm

# ---- Métriques de jeu (0-100), Influence, Diplomates & Révolte -----------
# La membrane projette les coordonnées en nombres+mots ; le statecraft est SIM
# (il lit des flottants), son API ne rend que des entiers de jeu.
STATECRAFT_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_econ.o \
                        $(OBJDIR)/scps_scps_trade.o $(OBJDIR)/scps_scps_culture.o \
                        $(OBJDIR)/scps_scps_tech.o $(OBJDIR)/scps_scps_core.o \
                        $(OBJDIR)/scps_scps_legitimacy.o $(OBJDIR)/scps_scps_prosperity.o \
                        $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_scps_readout.o \
                        $(OBJDIR)/scps_scps_diplo.o $(OBJDIR)/scps_scps_routes.o \
                        $(OBJDIR)/scps_scps_statecraft.o $(OBJDIR)/scps_statecraft_demo.o
statecraft_demo: $(STATECRAFT_DEMO_OBJS)
	$(CC) $(STATECRAFT_DEMO_OBJS) -o $@ -lm

# ---- Évènements, chocs géo & âges : la dynamique du monde -----------------
# Chocs ancrés dans la géo (failles/rivières/pluie/routes), évènements par la
# fiche, âges déclenchés par l'état du monde. Effets = coordonnées/métriques.
EVENTS_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_econ.o \
                    $(OBJDIR)/scps_scps_trade.o $(OBJDIR)/scps_scps_culture.o \
                    $(OBJDIR)/scps_scps_tech.o $(OBJDIR)/scps_scps_core.o \
                    $(OBJDIR)/scps_scps_legitimacy.o $(OBJDIR)/scps_scps_prosperity.o \
                    $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_scps_readout.o \
                    $(OBJDIR)/scps_scps_diplo.o $(OBJDIR)/scps_scps_routes.o \
                    $(OBJDIR)/scps_scps_statecraft.o $(OBJDIR)/scps_scps_events.o \
                    $(OBJDIR)/scps_events_demo.o
events_demo: $(EVENTS_DEMO_OBJS)
	$(CC) $(EVENTS_DEMO_OBJS) -o $@ -lm

# ---- Âges structurels : Lumières, Soulèvements, l'Ordre de Fer ------------
# Ils poussent les ENTRÉES du moteur d'ordre ; le verdict §2.4 fait le reste.
STRUCTURAL_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_econ.o \
                    $(OBJDIR)/scps_scps_trade.o $(OBJDIR)/scps_scps_culture.o \
                    $(OBJDIR)/scps_scps_tech.o $(OBJDIR)/scps_scps_core.o \
                    $(OBJDIR)/scps_scps_legitimacy.o $(OBJDIR)/scps_scps_prosperity.o \
                    $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_scps_readout.o \
                    $(OBJDIR)/scps_scps_diplo.o $(OBJDIR)/scps_scps_routes.o \
                    $(OBJDIR)/scps_scps_statecraft.o $(OBJDIR)/scps_scps_agency.o \
                    $(OBJDIR)/scps_scps_ai.o $(OBJDIR)/scps_scps_events.o \
                    $(OBJDIR)/scps_structural_demo.o
structural_demo: $(STRUCTURAL_DEMO_OBJS)
	$(CC) $(STRUCTURAL_DEMO_OBJS) -o $@ -lm

# ---- L'économie des populations : main-d'œuvre, jobs, matériaux, marché ---
# La prod scale sur les JOBS REMPLIS ; les sorties LISENT la géo du worldgen.
LABOR_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_culture.o \
                   $(OBJDIR)/scps_scps_econ.o $(OBJDIR)/scps_scps_species.o \
                   $(OBJDIR)/scps_scps_labor.o $(OBJDIR)/scps_labor_demo.o
labor_demo: $(LABOR_DEMO_OBJS)
	$(CC) $(LABOR_DEMO_OBJS) -o $@ -lm

# ---- Les armées : recrutement, armes, contres, combat au dé ---------------
# Bâti sur l'économie (pop par classe + armes fabriquées). Autonome (pas de SDL).
ARMY_DEMO_OBJS := $(OBJDIR)/scps_scps_labor.o $(OBJDIR)/scps_scps_army.o \
                  $(OBJDIR)/scps_army_demo.o
army_demo: $(ARMY_DEMO_OBJS)
	$(CC) $(ARMY_DEMO_OBJS) -o $@ -lm

# ---- Le refactor démographique : la province contient des GROUPES (clé de voûte)
# Branche scps_modifier (pile de dérive). Alimente scps_order (inchangé).
DEMOGRAPHY_DEMO_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_econ.o \
                    $(OBJDIR)/scps_scps_culture.o $(OBJDIR)/scps_scps_species.o \
                    $(OBJDIR)/scps_scps_tech.o $(OBJDIR)/scps_scps_core.o \
                    $(OBJDIR)/scps_scps_legitimacy.o $(OBJDIR)/scps_scps_prosperity.o \
                    $(OBJDIR)/scps_scps_readout.o $(OBJDIR)/scps_scps_modifier.o \
                    $(OBJDIR)/scps_scps_demography.o $(OBJDIR)/scps_demography_demo.o
demography_demo: $(DEMOGRAPHY_DEMO_OBJS)
	$(CC) $(DEMOGRAPHY_DEMO_OBJS) -o $@ -lm

# ---- L'intégration au moteur vivant (la province réelle porte des groupes) -
DEMOGRAPHY_INTEG_OBJS := $(OBJDIR)/scps_scps_world.o $(OBJDIR)/scps_scps_econ.o \
                    $(OBJDIR)/scps_scps_trade.o $(OBJDIR)/scps_scps_culture.o \
                    $(OBJDIR)/scps_scps_species.o $(OBJDIR)/scps_scps_tech.o \
                    $(OBJDIR)/scps_scps_core.o $(OBJDIR)/scps_scps_legitimacy.o \
                    $(OBJDIR)/scps_scps_prosperity.o $(OBJDIR)/scps_scps_readout.o \
                    $(OBJDIR)/scps_scps_modifier.o $(OBJDIR)/scps_scps_demography.o \
                    $(OBJDIR)/scps_demography_integ_demo.o
demography_integ_demo: $(DEMOGRAPHY_INTEG_OBJS)
	$(CC) $(DEMOGRAPHY_INTEG_OBJS) -o $@ -lm

clean:
	rm -rf $(OBJDIR) scps_viewer scps_viewer.exe scps_dump scps_batch econ_demo \
	       tech_demo culture_demo prosperity_demo agency_demo diplo_demo routes_demo ai_demo statecraft_demo events_demo core_demo readout_demo species_demo \
	       out_*.ppm montage.bmp

.PHONY: all scps run_scps clean core_demo readout_demo species_demo scps_dump scps_batch \
        econ_demo tech_demo culture_demo prosperity_demo agency_demo diplo_demo routes_demo ai_demo statecraft_demo events_demo

# Inclusion des fichiers de dépendances générés (-MMD). Le tiret ignore leur
# absence au premier build.
-include $(wildcard $(OBJDIR)/*.d)
