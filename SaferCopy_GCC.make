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
CFLAGS = -O2 -fomit-frame-pointer -m68000 -noixemul \
         -Wall -Wextra \
         -Wno-pointer-sign

LDFLAGS = -noixemul

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

clean:
	$(RM) $(TARGET)

.PHONY: all clean
