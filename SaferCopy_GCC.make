# Makefile pour m68k-amigaos-gcc (cross-compiler ou VBCC natif)
# Usage: make -f SaferCopy_GCC.make

CC     = m68k-amigaos-gcc
RM     = rm -f

TARGET  = SaferCopy
TARGETR = SaferCopy_pure
SRC     = SaferCopy.c
OBJ     = SaferCopy.o
OBJR    = SaferCopy_pure.o

# -O2          : optimisation raisonnable
# -fomit-frame-pointer : code plus compact
# -m68000      : compatible avec tout 680x0
# -noixemul    : utilise libnix (pas ixemul) = binaire autonome
#
# Le binaire par defaut (SaferCopy) est compile SANS -resident :
# le support -resident de bebbo gcc (data base-relative via A4,
# relocation au lancement) est la partie la plus fragile de la
# chaine ; un rate de relocation = ecritures par pointeurs faux
# (corruption filesystem, crash systeme). Tant que la variante
# pure n est pas validee sur du vrai materiel, ne distribuer que
# le binaire par defaut.
CFLAGS = -O2 -fomit-frame-pointer -m68000 -noixemul \
         -Wall -Wextra \
         -Wno-pointer-sign

LDFLAGS = -noixemul

# Variante PURE (residentable) : make -f SaferCopy_GCC.make pure
# Produit SaferCopy_pure, utilisable avec RESIDENT dans le Shell.
RESFLAGS = -resident

all: $(TARGET)

pure: $(TARGETR)

both: $(TARGET) $(TARGETR)

# Compilation en deux etapes (compile .o puis link) plutot qu en une
# seule commande. Si la compilation echoue (ex: cc1 ne peut pas ecrire
# son .s temporaire faute de TEMP valide), make s arrete sur l erreur
# reelle au lieu de continuer vers le link et de cracher un trompeur
# "undefined reference to main".
$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJ)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

# Variante pure : objet distinct compile avec -resident.
$(TARGETR): $(OBJR)
	$(CC) $(LDFLAGS) $(RESFLAGS) -o $(TARGETR) $(OBJR)

$(OBJR): $(SRC)
	$(CC) $(CFLAGS) $(RESFLAGS) -c $(SRC) -o $(OBJR)

clean:
	$(RM) $(TARGET) $(TARGETR) $(OBJ) $(OBJR)

.PHONY: all pure both clean
