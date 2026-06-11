# Makefile pour m68k-amigaos-gcc (cross-compiler ou VBCC natif)
# Usage: make -f SaferCopy_GCC.make

CC     = m68k-amigaos-gcc
RM     = rm -f

TARGET = SaferCopy
SRC    = SaferCopy.c

# -O2          : optimisation raisonnable
# -fomit-frame-pointer : code plus compact
# -m68000      : compatible avec tout 680x0
# -noixemul    : utilise libnix (pas ixemul) = binaire autonome
# -resident    : binaire pur (residentable) : la section data est
#                copiee a chaque invocation et adressee via A4.
#                Necessaire pour la commande RESIDENT du Shell.
CFLAGS = -O2 -fomit-frame-pointer -m68000 -noixemul -resident \
         -Wall -Wextra \
         -Wno-pointer-sign

LDFLAGS = -noixemul -resident

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

clean:
	$(RM) $(TARGET)

.PHONY: all clean
