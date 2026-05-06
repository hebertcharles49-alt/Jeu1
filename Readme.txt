================================================================
  ELEMENT DUNGEON  --  doomlike hybride 3D voxel
  (DOOM x HADES x ISAAC x DIABLO 2 x MINECRAFT)
  Mode d'emploi pas-a-pas pour Windows
================================================================

Tu es dev novice ? Suis ce guide a la lettre. 5-10 minutes, on
est parti.


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

   La fenetre 1280x720 s'ouvre, vue 3eme personne sur un donjon
   voxel. ENTREE ou clic sur "JOUER" pour commencer.


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

LORE
   Il y a sept mille ans, le Cristal Originel se brisa. Sept
   eclats tomberent dans l'abime, devenant les sept elements :
   FEU, EAU, TERRE, FOUDRE, AIR, VIDE, FEE -- plus tard rejoints
   par ACIER, TENEBRES et SACRE. Le Donjon des Elements vient
   de s'ouvrir : 10 etages, 10 Gardiens, 10 eclats. Descend.

CONCEPT
   Donjon voxel 3D a la Minecraft (rendu OpenGL 3.3 + shaders),
   ambiance Hades / Isaac / Diablo 2. Vue 3eme personne fixe
   axonometrique. Donjon procedural en 10 etages, un BOSS par
   etage, puis portail-boutique. ARPG diablolike : inventaire,
   raretes, fusion, sensibilites elementaires.

CONTROLES (defauts, modifiables dans Options)
   ZQSD / WASD / FLECHES   deplacement (toujours actif)
   SOURIS                  visee + naviguer/cliquer dans menus
                           (ray-cast monde via unproject GL)
   ESPACE                  dash (i-frames)
   1 / 2 / TAB             arme active (recoit les elements)
   I                       inventaire (pause)
   O                       Options (titre / sanctuaire)
   L                       Lore depuis le titre
   H                       aide
   ECHAP                   abandonner / fermer / quitter

INVENTAIRE
   Souris ou fleches       navigation
   Clic gauche             equiper (sac) / desequiper (panneau)
   E                       equiper / desequiper sur curseur
   M                       marquer 1 item (pour fusion)
   F                       FUSIONNER 3 items identiques
                           -> rarete +1, +20% stats bonus
   X                       effacer marques

OPTIONS  (touche O)
   Section CONTROLES : remappe Dash, Inventaire, Changer arme,
       Arme 1, Arme 2, Interagir. WASD/fleches reserves.
   Section AUDIO : mute on/off, volume 0..4 (sons procedural
       generes via SDL_QueueAudio, pas de fichiers wav).
   Section VIDEO : DLSS Generatif on/off
       (toggle settings stocke ; le filtrage de blit FBO peut
        s'activer plus tard depuis ce switch).
   Reglages persistants dans crucible_settings.dat.

LES 10 HEROS
   GUERRIER   +25 PV, +15% degats melee
   VOLEUR     +20 vitesse, dash plus long
   MAGE       +30% degats elementaires, PV bas
   BERSERKER  +8% vol de vie, +20% degats, fragile
   PALADIN    +2 armure, +15 PV, regen 1 PV/s

   DRUIDE     +50% degats elem  / -30% degats melee
   ASSASSIN   +25% crit, x2 crit dmg / -25 PV max
   RANGER     +40% degats distance / -25% degats melee
   TEMPLIER   +3 armure, +15 PV / -15% atk speed
   NECROMANT  +15% vol de vie, +20% Vide+Tenebres / -1 regen/s

   Decouverte : tu commences avec GUERRIER. Chaque boss tue
   revele le heros suivant dans le sanctuaire (puis tu paies
   en eclats pour le debloquer).

LES 6 ARMES (max 2 equipees)
   Poings (par defaut) -- Epee -- Bouclier -- Arc -- Baton -- Hache
   Sous-classe nommee selon la combinaison :
     Epee + Baton    = Sorcelame
     Bouclier+Hache  = Croise
     Arc + Baton     = Archimage
     Epee + Bouclier = Garde
     Hache + Hache   = Boucher
     ...environ 16 combos.
   L'arme equipee est visible dans la main du heros en jeu.

LES 10 ELEMENTS
   Originels : Feu, Eau, Terre, Foudre, Air, Vide, Fee
   Neutres   : Acier, Tenebres, Sacre
   Greffe jusqu'a 3 par arme. Les combinaisons donnent ~30
   noms uniques (Vapeur, Plasma, Choc, Lave, Volcan, Tempete,
   Dechirure, Phenix...) plus un fallback hybride.

SENSIBILITES (Pokemon-like, table 11x11)
   EAU > FEU         FEU > FEE
   FOUDRE > EAU      EAU > TERRE
   TERRE > FOUDRE    AIR > LIGHTNING
   AIR > TERRE       VIDE <-> FEE
   ACIER > TERRE     SACRE > TENEBRES
   FOUDRE > ACIER    DARK > HOLY (et reciproque)
   x2 si on tape avec la bonne couleur, /2 sinon.

ELITES
   Chance 5% par etage atteint (plafond 50%).
   Affinite elementaire visible (aura pulsante au sol).
   2x PV, drop garanti d'un equipement de la table par etage.
   Nom procedural genre "Vorgar le Brulant".

EQUIPEMENT
   6 slots : Casque (+PV) / Torse (+Armure) / Jambes (+Vitesse) /
   Bottes (+Esquive) / Ceinture (+Regen) / Gants (+%Degats).
   5 raretes :
     Commun (x1.0)  Magique (x1.25)  Rare (x1.5)
     Epique (x1.75) Legendaire (x2.0)
   Fusionne 3 items IDENTIQUES (slot/rarete/variant identiques)
   pour 1 item rarete superieure +20% stats.
   Les pieces equipees sont visibles sur le heros, teintees
   selon leur rarete.

ATTAQUES
   Toutes les attaques visent la SOURIS (ray-cast monde via
   unproject OpenGL). Auto-fire respecte le cooldown de chaque
   arme. Les melee ne s'animent que si un ennemi est en portee.

STATS JOUEUR (Brotato-like)
   PV, Armure, Vitesse, Regen, Vol de vie, Degats flat, %Degats
   (global / melee / distance / elementaire), Atk speed,
   %Crit, x Crit dmg, %Portee, Esquive, +affinite par element.
   Toutes ajustables via shop, equipement, archetype.

DEROULE D'UNE COURSE
   Sanctuaire -> choisir heros -> course (10 etages):
     - tue tout dans chaque salle (mesh voxel chunk regenere
       quand le donjon change)
     - boss dans la derniere salle de l'etage
     - portail apparait, declenche la BOUTIQUE
       (uniquement entre les etages, style Brotato)
     - achete (pieces) puis etage suivant
     - Etage 10 = boss final = VICTOIRE

SHOP BROTATO-LIKE
   4 cartes par visite + bouton REROLL paye
   ~40 recettes : Bandage, Pierre tranchante, Lame lourde,
   Loupe ardente, Sang de dragon, Pacte sombre, Anneau de
   verre, Lentille folle, Couronne de fer, Talisman du Vide,
   Pacte du Necromancien, Cle des dieux, Larme du Cristal...
   Beaucoup ont un cout (degats - PV, dmg - vitesse, etc).
   Probabilite de rarete monte avec l'etage.

PROGRESSION META
   Eclats d'ame gagnes a la mort, depenses au sanctuaire :
     +10 PV max / +1 armure / +5 vitesse / +5% degats
     deblocage des heros decouverts (boss kill)
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
     1280x720 = chunky pixel-art voxel coherent
   - Math vec3/mat4 inline, unproject 4x4 pour ray-cast souris

VOXELS
   - Murs en colonnes 2 blocs de hauteur, sols a y=0
   - Tuiles : sol, sang, os, runes (pulse violet), torches
     (poste lumineux), exit (bleu emissif), lave/eau (hazard)
   - Construction du mesh : top quad par sol + 4 cotes + top
     pour chaque colonne mur (5 faces visibles par mur)

ENTITES
   - Joueur compose de 7-9 cubes : ombre, jambes, corps, bras,
     tete, casque/cheveux, equipement (cape/torse), arme tenue
   - Bobbing en mouvement, swing des bras et jambes
   - Orientation legere vers le sens du deplacement
   - Ennemis : 5-7 cubes selon type (zombie, bandit, demon,
     slime, boss). Cornes pour demon, masque pour bandit,
     blessure pour zombie, couronne doree pour boss
   - Aura pulsante au sol pour les elites (couleur element)

UI 2D
   - Passe ortho post-3D, batched quads (jusqu'a 16384/frame)
   - Font 5x7 bitmap rendue pixel-par-pixel en fillRect
   - HP bars / noms procedural / damage numbers projetes via
     world_to_screen (matrice view * proj manuelle)


================================================================
                          FICHIERS
================================================================
   src/game.h       structures + API
   src/main.c       boucle, etats (titre/hub/run/options/...)
   src/world.c      donjon procedural, ennemis, joueur, salles
   src/combat.c     armes, elements, combos, projectiles, fees
   src/inventory.c  inventaire, equipement, raretes, fusion
   src/options.c    menu Options + persistance settings
   src/render.c     UI + rendu 3D du monde voxel
   src/audio.c      sons procedural via SDL_QueueAudio
   src/meta.c       sauvegarde permanente
   src/shop.c       boutique Brotato-like, ~40 recettes
   src/names.c      generateur de noms procedural Diablo-like
   src/mods.c       chargeur de mods/*.cfg
   src/gfx.h/.c     couche OpenGL 3.3 (shaders, batcher, math)

   Makefile         build Linux/MinGW (autodetection Windows)
   build.bat        build Windows (MinGW)
   build_msvc.bat   build Windows (MSVC)

   crucible_save.dat       progression permanente
   crucible_settings.dat   reglages (touches, son, DLSS)
   mods/*.cfg             overrides charges au demarrage

Aucun asset externe : sprites, fonts, sons, voxels -- tout
genere par code.

Bonne descente.
