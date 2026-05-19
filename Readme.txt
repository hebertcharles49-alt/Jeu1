================================================================
  ELEMENT DUNGEON  --  roguelite elementaire voxel 3D
  Mode d'emploi pas-a-pas pour Windows
================================================================

Tu es dev novice ? Suis ce guide a la lettre. 5-10 minutes, on
est parti. Pour Linux/macOS : juste `make` puis `./element_dungeon`.


----------------------------------------------------------------
0) RECUPERER LE CODE  (si tu n'as pas deja le dossier)
----------------------------------------------------------------
   --- OPTION RAPIDE : zip ---
   Sur la page Github du projet, clique le bouton vert "Code"
   puis "Download ZIP". Decompresse-le ou tu veux, par exemple :
       C:\Users\Toi\Bureau\Jeu1
   Saute a l'etape 1.

   --- OPTION GIT (recommandee si tu veux les MAJ futures) ---
   a) Installe Git :  https://git-scm.com/download/win
   b) Ouvre "Git Bash" depuis le menu Demarrer.
   c) cd ou tu veux poser le projet, par ex. :
         cd /c/Users/Toi/Bureau
   d) Clone (URL de ton depot, finit en .git) :
         git clone https://github.com/UTILISATEUR/REPO.git Jeu1
   e) Pour mettre a jour plus tard :
         cd /c/Users/Toi/Bureau/Jeu1
         git pull


----------------------------------------------------------------
1) PRE-REQUIS GRAPHIQUES
----------------------------------------------------------------
   Le jeu fait du voxel 3D via OpenGL 3.3 core + shaders.
   Pratiquement tout GPU sorti depuis 2010 supporte ca, mais
   ASSURE-TOI d'avoir tes pilotes graphiques a jour (Intel,
   AMD ou NVIDIA). Sans ca, le contexte GL3.3 echoue.


----------------------------------------------------------------
2) INSTALLER MSYS2 + TOOLCHAIN  (5 minutes)
----------------------------------------------------------------
   a) https://www.msys2.org -> telecharge l'installeur,
      "Suivant" "Suivant" "Installer". Garde C:\msys64.
   b) Ouvre "MSYS2 MINGW64" depuis le menu Demarrer (icone bleue).
      Pas un autre raccourci -- BIEN MINGW64.
   c) Copie-colle dans le terminal :

      pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkg-config make

      ENTREE. Ca telecharge ~200 Mo.


----------------------------------------------------------------
3) ALLER DANS LE DOSSIER + COMPILER
----------------------------------------------------------------
   Dans MSYS2 MINGW64 :

      cd /c/Users/Toi/Bureau/Jeu1
      make

   Le Makefile detecte automatiquement Windows (OS=Windows_NT)
   et passe sur -lopengl32 + -mwindows. Plus besoin de WIN=1.

   Tu dois voir des lignes "cc -O2 ..." puis "ld ..." sans
   erreur, et un fichier element_dungeon.exe est genere.


----------------------------------------------------------------
4) LANCER LE JEU
----------------------------------------------------------------
      ./element_dungeon.exe

   La fenetre 1280x720 s'ouvre sur l'ecran titre. Curseur
   clavier (HAUT/BAS) + ENTREE, ou survol souris + clic.


----------------------------------------------------------------
5) RACCOURCI : LANCER PLUS TARD
----------------------------------------------------------------
      cd /c/Users/Toi/Bureau/Jeu1
      ./element_dungeon.exe

   Pas besoin de recompiler tant que tu ne touches pas au code.


----------------------------------------------------------------
PROBLEMES COURANTS
----------------------------------------------------------------
   "make: command not found"
       Tu n'es pas dans MSYS2 MINGW64. Ouvre via le menu
       Demarrer (raccourci bleu) -- JAMAIS un autre raccourci.

   "cannot find -lGL: No such file or directory"
       Tu es bien sous MSYS2 mais le Makefile ne detecte pas
       Windows. Verifie que tu es a jour (git pull). Sinon
       force avec :   make WIN=1

   "sdl2-config: command not found" ou "SDL.h: No such file"
       Le paquet SDL2 manque. Refais l'etape 2 commande pacman.

   "echec init OpenGL" / fenetre noire / crash a l'init
       Ton GPU/pilotes ne supportent pas OpenGL 3.3 core.
       Mets a jour tes pilotes (Intel, AMD ou NVIDIA via Windows
       Update ou les sites constructeurs).

   "Failed to load SDL2.dll" en double-cliquant l'exe
       Lance via MSYS2 ( ./element_dungeon.exe ), OU copie
       SDL2.dll depuis C:\msys64\mingw64\bin\SDL2.dll dans
       le dossier du jeu, puis double-clique l'exe.

   "git: command not found"
       Git pas installe -- option zip a l'etape 0.


================================================================
                          LE JEU
================================================================

CONCEPT
   Roguelite elementaire. Voxel 3D, vue 3e personne fixe, donjon
   procedural 10 etages + 1 boss par etage. Combat a la souris.
   Greffe jusqu'a 3 elements par arme pour declencher des combos
   (Vapeur, Plasma, Lave, Tempete, Phenix...). Loot type ARPG :
   raretes, affixes, fusion 3-en-1, uniques build-defining.

LORE  (distille en jeu via parchemins ramasses, cf option LORE)
   Il y a sept mille ans, le Cristal Originel se brisa. Sept
   eclats tomberent dans l'abime -- les sept elements. Le Donjon
   des Elements vient de s'ouvrir. 10 etages, 10 Gardiens.

CONTROLES (defauts, modifiables dans Options)
   WASD / FLECHES         deplacement
   SOURIS                 viser
   CLIC GAUCHE            attaquer (maintenir pour enchainer)
   ESPACE                 dash (i-frames)
   1 / 2 / TAB            arme active (recoit les elements)
   E                      interagir (HUB)
   I                      inventaire (pause)
   K                      codex
   O                      Options
   H                      aide
   ECHAP                  fermer / abandonner / quitter

INVENTAIRE
   Souris ou fleches      navigation
   Clic gauche            equiper (sac) / desequiper (panneau)
   E                      equiper / desequiper sur curseur
   M                      marquer 1 item (pour fusion)
   F                      FUSIONNER 3 items identiques
                          -> rarete +1, +20% stats bonus
   X                      effacer marques

OPTIONS  (touche O)
   CONTROLES : remappe les touches (WASD/fleches reserves)
   AUDIO     : mute on/off, volume 0..4
   VIDEO     : DLSS Generatif, Debug room bac-a-sable,
               Barres de vie flottantes (toggle)
   Reglages persistants dans crucible_settings.dat.

LE HUB  (Cimetiere walkable, scene 3D, pas un menu)
   Tu apparais dans une grande salle voxel avec 5 batiments :
     TEMPLE   stat permanente (eclats : PV / armure / vitesse / dmg)
     FORGE    amelioration d'arme (lvl 0..5, +5 dmg / niveau)
     DONJON   lance la run (porte centrale)
     TAVERNE  recrute / change de heros
     LICHE    modificateurs de run (a venir)
   Tu marches avec WASD, t'approches d'un batiment, tu vois
   l'indicateur [E] et le nom -- appuie sur E pour interagir.

LES 10 HEROS  (debloques 1 par boss tue)
   GUERRIER   +25 PV, +15% degats melee
   VOLEUR     +20 vitesse, dash plus long
   MAGE       +30% degats elementaires, PV bas
   BERSERKER  +8% vol de vie, +20% degats, fragile
   PALADIN    +2 armure, +15 PV, regen 1 PV/s
   DRUIDE     +50% degats elem / -30% degats melee
   ASSASSIN   +25% crit, x2 crit dmg / -25 PV max
   RANGER     +40% degats distance / -25% degats melee
   TEMPLIER   +3 armure, +15 PV / -15% atk speed
   NECROMANT  +15% vol de vie, +20% Vide/Tenebres / -1 regen/s

LES 6 ARMES  (max 2 equipees, seule l'active attaque)
   Poings (defaut) -- Epee -- Bouclier -- Arc -- Baguette -- Hache
   Combinaisons nommees (~16 sous-classes) :
     Epee + Baguette     = Sorcelame
     Bouclier + Hache    = Croise
     Arc + Baguette      = Archimage
     Epee + Bouclier     = Garde
     Hache + Hache       = Boucher
   L'arme active est visible dans la main du heros et a son
   geste / son / timing propre :
     Epee     slash transversal rapide, motion-blur
     Hache    chop overhead, landing burst, lourd
     Arc      bow visible avec corde en V, fleche sur la corde,
              vibration de corde apres release
     Baguette pointe trace une ellipse, trail des 8 positions,
              sparkles magiques
     Bouclier bash + ring de lumiere, reflete les projectiles
     Poings   jab rapide

LES 10 ELEMENTS
   Originels : Feu, Eau, Terre, Foudre, Air, Vide, Fee
   Neutres   : Acier, Tenebres, Sacre
   Greffe jusqu'a 3 par arme. Les combos donnent ~30 noms uniques
   (Vapeur, Plasma, Choc, Lave, Volcan, Tempete, Phenix...) +
   fallback hybride.

SENSIBILITES  (table 11x11)
   EAU > FEU         FEU > FEE
   FOUDRE > EAU      EAU > TERRE
   TERRE > FOUDRE    AIR > FOUDRE
   AIR > TERRE       VIDE <-> FEE
   ACIER > TERRE     SACRE <-> TENEBRES
   x2 si on tape avec la bonne couleur, /2 sinon.

TRIPLES COMBOS  (3 elements greffes)
   Declenchent une boucle de feedback persistante (aura + buff
   de stats). Killstreak elevee -> passage en OVERLOAD : +30%
   dmg, +50% atk speed, aura rouge.

ELITES
   Chance 5% par etage atteint (plafond 50%).
   Affinite elementaire visible (aura pulsante au sol).
   x2 PV, drop garanti d'un equipement legendaire.
   Nom procedural genre "Vorgar le Brulant".

EQUIPEMENT
   6 slots : Casque (+PV) / Torse (+Armure) / Jambes (+Vitesse) /
   Bottes (+Esquive) / Ceinture (+Regen) / Gants (+%Degats).
   5 raretes :
     Commun (x1.0)  Magique (x1.25)  Rare (x1.5)
     Epique (x1.75) Legendaire (x2.0)
   Affixes en archetypes (Offensif / Defensif / Mobilite /
   Vampirique / Frenetique) avec compromis positifs+negatifs.
   Fusion 3-en-1 : items identiques (slot/rarete/variant) ->
   rarete superieure +20% stats.
   Les pieces equipees sont visibles sur le heros.

UNIQUES BUILD-DEFINING
   20 items uniques avec flags de gameplay (dash gratuit,
   phoenix revive, kill wave, immune frontal, void trail,
   crowd regen, etc). 3 dispatches par run sur des milestones
   de kills (invisible cote joueur, lie a la seed).

TALISMANS
   7 dispatches par run sur les kills selon la seed. Statistiques
   pures (+attaque, +PV, +%dmg elemental...).

ATTAQUES
   Maintenir CLIC GAUCHE pour attaquer en continu (au rythme du
   cooldown). Seule l'arme ACTIVE attaque (1/2/TAB pour switch).
   Tu peux frapper dans le vide (whiff distinct).
   Visee depuis le ray-cast souris (unproject GL).

STATS JOUEUR
   PV, Armure, Vitesse, Regen, Vol de vie, Degats flat, %Degats
   (global / melee / distance / elementaire), Atk speed,
   %Crit, x Crit dmg, %Portee, Esquive, +affinite par element.
   Ajustables via shop, equipement, talismans, uniques.

FEEDBACK DE COMBAT
   - Knockback dans la direction de l'impact
   - Flash rouge plein-ecran + vignette + RGB-split a la touche
   - Sparks rouges / dust kick orientes derriere le joueur
   - Halo low-HP pulse "battement de coeur" sous 25% PV
   - SFX_HEARTBEAT sync sur la pulsation visuelle
   - Anneau jaune au pied du joueur pendant l'invuln (i-frames
     lisibles, telegraphes l'esquive)
   - Shake / hitstop / flash scalent a la severite du coup
   - Barres de vie flottantes (option) au-dessus des mobs

DEROULE D'UNE COURSE
   Cimetiere -> choisir heros / forge / temple -> course :
     - tue tout dans chaque salle
     - boss multi-phases dans la derniere salle de l'etage
     - portail apparait, declenche la BOUTIQUE
     - achete (pieces) puis etage suivant
     - Etage 10 = boss final = VICTOIRE

SHOP  (entre etages)
   4 cartes par visite + bouton REROLL paye.
   ~40 recettes : Bandage, Pierre tranchante, Lame lourde,
   Loupe ardente, Sang de dragon, Pacte sombre, Anneau de
   verre, Lentille folle, Couronne de fer, Talisman du Vide,
   Pacte du Necromancien, Cle des dieux, Larme du Cristal...
   Beaucoup ont un cout (degats - PV, dmg - vitesse, etc).
   Probabilite de rarete monte avec l'etage.

PROGRESSION META
   Eclats d'ame gagnes a la mort, depenses au Temple :
     +PV max / +armure / +vitesse / +%degats
   FORGE : +5 dmg / niveau permanent par arme (lvl 0..5)
   Decouvertes : combos, elements, armes, uniques, equipement
   visibles dans le CODEX (touche K)
   Tout est sauvegarde dans crucible_save.dat.


================================================================
              ENGINE 3D / DETAILS TECHNIQUES
================================================================

RENDU
   - SDL2 + OpenGL 3.3 core
   - Loader minimal de fonctions GL via SDL_GL_GetProcAddress
   - 3 shaders embarques :
       terrain  : Lambert + fog distance depuis joueur
       cube     : Lambert + tint pour les entites
       UI       : ortho colore (batched quads)
   - Mesh voxel construit depuis le donjon (heightmap),
     re-emis quand le donjon change (gen_id counter)
   - Camera 3eme personne, lerp doux vers le joueur
   - FBO offscreen 640x360 -> blit NEAREST sur backbuffer
     1280x720 = pixel-art voxel coherent
   - Math vec3/mat4 inline, unproject 4x4 pour ray-cast souris

VOXELS
   - Murs en colonnes 2 blocs de hauteur, sols a y=0
   - Tuiles : sol, sang, os, runes (pulse violet), torches,
     exit (bleu emissif), lave/eau (hazard)
   - Construction du mesh : top quad par sol + 4 cotes + top
     pour chaque colonne mur

ENTITES
   - Joueur compose de 7-9 cubes : ombre, jambes, corps, bras,
     tete, casque/cheveux, equipement, arme tenue
   - Bobbing en mouvement, swing des bras et jambes
   - Ennemis : 5-7 cubes selon type (zombie, bandit, demon,
     slime, boss). Cornes pour demon, masque pour bandit,
     blessure pour zombie, couronne doree pour boss
   - Aura pulsante au sol pour les elites (couleur element)

AUDIO
   - Mixer software callback : 24 voix simultanees (vraie
     superposition, pas une queue sequentielle)
   - Resampling lineaire pour le pitch
   - Steal de voix par priorite
   - Duck 80ms quand un son critique (hurt/death/boss) demarre
   - SFX percussifs : +/-8% pitch jitter
   - Tous procedural (sinus + bruit + low-pass + enveloppe)
   - SFX par arme :
       SFX_SWORD_SLASH  sweep 1.6kHz -> 600Hz tranchant
       SFX_AXE_SWING    whoosh 110Hz + noise bas
       SFX_BOW_FIRE     creak + twang 350Hz + whoosh fleche
       SFX_WAND_CAST    glissando 300 -> 1400 Hz, quinte
       SFX_HEAVY_HIT    thump bas (bouclier, hache)
       SFX_PUNCH        coup sec (poings)
       SFX_HEARTBEAT    thump grave (low-HP)

UI 2D
   - Passe ortho post-3D, batched quads (jusqu'a 16384/frame)
   - Font 5x7 bitmap rendue pixel-par-pixel en fillRect
   - HP bars / noms / damage numbers projetes via
     world_to_screen (matrice view * proj manuelle)


================================================================
                          FICHIERS
================================================================
   src/game.h        types + API
   src/main.c        boucle, etats, hub walkable
   src/world.c       donjon procedural, salles, biomes
   src/dungeon.c     generation
   src/player.c      mouvement, dash, take_damage, pickups
   src/combat.c      armes, elements, combos, fire_*
   src/enemies.c     AI par type, 5 boss multi-phases
   src/projectiles.c projectiles + homing
   src/surfaces.c    flaques (eau/feu/glace/electrifiee/etc)
   src/entities.c    particles, pickups, dmgnums, fairies
   src/inventory.c   items, affixes, fusion, archetypes
   src/uniques.c     20 uniques build-defining
   src/shop.c        ~40 recettes data-driven
   src/biomes.c      5 biomes / 10 etages
   src/assets.c      props decoratifs (piliers, statues, etc)
   src/heroes.c      definition des 10 heros
   src/audio.c       mixer software + sons procedural
   src/render_world.c    pipeline 3D + overlay HP/dmgnum
   src/render_menus.c    titre, hub overlay, options, help
   src/render_hud.c      HUD principal (PV, XP, stats panel)
   src/render_run.c      ecran de mort, victoire, levelup
   src/render_codex.c    codex
   src/render_inventory.c inventaire
   src/gfx.h/.c      couche OpenGL 3.3 (shaders, batcher, math)
   src/ui_common.h/.c    text_draw, fill_rect, toast, etc
   src/options.c     menu Options + persistance settings
   src/meta.c        sauvegarde permanente
   src/elements.c    table sensibilites, noms de combos
   src/names.c       generation procedural de noms d'items
   src/mods.c        chargeur de mods/*.cfg

   Makefile          build Linux/MinGW (autodetection Windows)
   build.bat         build Windows (MinGW)
   build_msvc.bat    build Windows (MSVC)

   crucible_save.dat       progression permanente
   crucible_settings.dat   reglages (touches, son, options video)
   mods/*.cfg              overrides charges au demarrage

Aucun asset externe : sprites, fonts, sons, voxels -- tout
genere par code.

Build flags : -O2 -Wall -Wextra -std=c99. Zero warning.

Bonne descente.
