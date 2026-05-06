================================================================
  CRUCIBLE  --  doomlike hybride (DOOM x VAMPIRE x ISAAC x DIABLO)
  Mode d'emploi pas-a-pas pour Windows
================================================================

Tu es dev novice ? Suis ce guide a la lettre. 5 minutes, on est parti.


----------------------------------------------------------------
1) CE QUE TU VAS INSTALLER
----------------------------------------------------------------
   - MSYS2 : un environnement gratuit qui fournit le compilateur
     C (gcc) et la librairie graphique SDL2 dont le jeu a besoin.
   - C'est tout. Pas de Visual Studio, pas de compte, rien d'autre.


----------------------------------------------------------------
2) INSTALLER MSYS2  (3 minutes)
----------------------------------------------------------------
   a) Va sur :  https://www.msys2.org
   b) Telecharge l'installeur en bas de page
      (ex.  msys2-x86_64-XXXXXX.exe ).
   c) Lance-le, "Suivant" "Suivant" "Installer". Garde le chemin
      par defaut :  C:\msys64
   d) A la fin laisse coche  "Run MSYS2 now"  --> une fenetre
      noire de terminal s'ouvre. Si tu l'as fermee, ouvre-la via
      le menu Demarrer :   "MSYS2 MINGW64".

      ATTENTION : tu as plusieurs raccourcis MSYS2.
      ---> Utilise UNIQUEMENT  "MSYS2 MINGW64"  (icone bleue).


----------------------------------------------------------------
3) INSTALLER LE COMPILATEUR ET SDL2  (2 minutes)
----------------------------------------------------------------
   Dans la fenetre  "MSYS2 MINGW64"  copie-colle CETTE LIGNE
   (clic droit dans le terminal pour coller) :

      pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkg-config make

   Appuie sur ENTREE. Ca telecharge environ 200 Mo. Attends que
   le prompt revienne (c'est rapide).


----------------------------------------------------------------
4) ALLER DANS LE DOSSIER DU JEU
----------------------------------------------------------------
   Imaginons que ton dossier soit :   C:\Users\Toi\Bureau\Jeu1

   Dans MSYS2 MINGW64, tape EXACTEMENT :

      cd /c/Users/Toi/Bureau/Jeu1

   --> Remplace  "Toi"  par ton nom Windows. Si tu n'es pas sur,
       tape :   ls /c/Users/   pour voir la liste.

   Verifie qu'on est au bon endroit :

      ls

   Tu dois voir :   src   Makefile   build.bat   Readme.txt   ...


----------------------------------------------------------------
5) COMPILER
----------------------------------------------------------------
   Tape :

      make

   Ca devrait afficher des lignes "cc -O2 -Wall ..." puis se
   terminer sans erreur. Si ca rale, lis l'erreur affichee et
   fais defiler vers le haut, c'est toujours utile.


----------------------------------------------------------------
6) LANCER LE JEU
----------------------------------------------------------------
   Toujours dans MSYS2 :

      ./crucible.exe

   La fenetre du jeu s'ouvre. ENTREE pour demarrer.


----------------------------------------------------------------
7) RACCOURCI : LANCER LE JEU PLUS TARD
----------------------------------------------------------------
   Refais juste les etapes 4 et 6 :

      cd /c/Users/Toi/Bureau/Jeu1
      ./crucible.exe

   Pas besoin de recompiler tant que tu ne touches pas au code.


----------------------------------------------------------------
PROBLEMES COURANTS
----------------------------------------------------------------
   "make: command not found"
       --> tu n'es pas dans MSYS2 MINGW64. Ouvre-le via le menu
           Demarrer (raccourci bleu).

   "sdl2-config: command not found"  ou  "SDL.h: No such file"
       --> le paquet SDL2 n'est pas installe. Refais l'etape 3.

   "Failed to load SDL2.dll" en lancant
       --> tu lances depuis Windows et pas depuis MSYS2.
           Soit lance via MSYS2 ( ./crucible.exe ), soit copie
           SDL2.dll depuis  C:\msys64\mingw64\bin\SDL2.dll  dans
           le dossier du jeu, puis double-clique  crucible.exe
           depuis l'explorateur.

   Aucune fenetre, ecran noir
       --> ton GPU est tres ancien. Edite  src/main.c  ligne
           "SDL_RENDERER_ACCELERATED"  --> remplace par
           "SDL_RENDERER_SOFTWARE"  puis refais  make.


================================================================
                           LE JEU
================================================================

CONCEPT
   Top-down pixel art. Donjon procedural en 10 etages, un BOSS par
   etage, puis portail-boutique. ARPG diablolike avec inventaire,
   raretes, fusion, sensibilites elementaires.

CONTROLES
   ZQSD / WASD / FLECHES   deplacement
   SOURIS                  visee
   ESPACE                  dash (i-frames)
   1 / 2 / TAB             arme active (recoit les elements)
   I                       inventaire (pause)
   ECHAP                   abandonner / fermer / quitter

INVENTAIRE
   FLECHES                 navigation
   E                       equiper / desequiper
   M                       marquer 1 item (pour fusion)
   F                       FUSIONNER 3 items identiques (-> rarete +1, +20% stats)
   X                       effacer marques

LES 5 HEROS
   Guerrier   +25 PV, +15% degats melee
   Voleur     +20 vitesse, dash plus long
   Mage       +30% degats, PV bas
   Berserker  +20% degats, vol de vie 8%, fragile
   Paladin    +2 armure, regen 1 PV/s

LES 6 ARMES (max 2 equipees)
   Poings (par defaut) -- Epee -- Bouclier -- Arc -- Baton -- Hache
   La combinaison de 2 armes definit ta SOUS-CLASSE :
     Epee + Baton  = Sorcelame
     Bouclier+Hache= Croise
     Arc + Baton   = Archimage
     etc.

LES 7 ELEMENTS
   Feu  Eau  Terre  Foudre  Air  Vide  Fee
   Greffe jusqu'a 3 par arme. La combinaison change l'effet :
     Feu+Eau         = Vapeur (AOE)
     Feu+Foudre      = Plasma (chaine)
     Eau+Foudre      = Choc (paralyse)
     Vide+Fee+Foudre = Dechirure (perce + chaine + vol vie)
     ...une trentaine de combos uniques + fallback hybride.

SENSIBILITES (Pokemon-like)
   EAU > FEU         FEU > FEE
   FOUDRE > EAU      EAU > TERRE
   TERRE > FOUDRE    AIR > LIGHTNING
   AIR > TERRE       VIDE <-> FEE (mutuels)
   Frapper avec la bonne couleur = x2 degats. La mauvaise = /2.

ELITES
   Chance d'apparition = 5% par etage atteint (plafond 50%).
   Chaque elite a un element. Aura visible. Plus de PV, plus de
   degats, drop garanti d'un equipement.

EQUIPEMENT
   6 slots : Casque (PV) / Torse (Armure) / Jambes (Vitesse) /
   Bottes / Ceinture (Regen) / Gants (Degats %).
   5 raretes :
     Commun (x1.0)  Magique (x1.25)  Rare (x1.5)
     Epique (x1.75) Legendaire (x2.0)
   Fusionne 3 items IDENTIQUES (meme slot, meme rarete, meme
   variant) pour obtenir 1 item de rarete superieure +20% stats.

DEROULE D'UNE COURSE
   Sanctuaire -> choisir heros -> course (10 etages):
     - tue tout dans chaque salle
     - boss dans la derniere salle
     - portail apparait, declenche la BOUTIQUE
     - achete (pieces) puis etage suivant
     - Etage 10 = boss final = VICTOIRE

PROGRESSION META
   - Eclats d'ame gagnes a la mort, depenses au sanctuaire
     (debloquer armes, elements, et plus tard heros).
   - Achats permanents au shop (armure, PV max, +%dmg) sont
     conserves entre courses.

================================================================
                          FICHIERS
================================================================
   src/game.h       structures et API
   src/main.c       boucle, etats, machine de jeu
   src/world.c      donjon, ennemis, joueur, salles
   src/combat.c     armes, elements, combos, projectiles, fees
   src/inventory.c  inventaire, equipement, raretes, fusion
   src/render.c     rendu pixel art, font, UI
   src/audio.c      sons procedural via SDL_QueueAudio
   src/meta.c       sauvegarde permanente (crucible_save.dat)
   Makefile         build Linux/MinGW
   build.bat        build Windows (MinGW)
   build_msvc.bat   build Windows (MSVC)

Aucun asset externe. Sprites, font 5x7, sons : tout est genere
par code.

Bon vibe-coding -- et bonne course.
