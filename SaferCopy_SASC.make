# Makefile pour SAS/C 6.x (sc)
# Usage: make -f SaferCopy_SASC.make

CC     = sc
LINK   = slink
RM     = delete

TARGET = SaferCopy
SRC    = SaferCopy.c

# Options SAS/C
# OPT       : optimisation
# NOSTKCHK  : pas de verif stack (appli CLI simple)
# STRMERGE  : merge des chaines litterales
# MATH=SOFT : pas de FPU obligatoire (680x0 sans 68881)
# IDIR      : chemins includes standard NDK 3.2
CFLAGS = OPT NOSTKCHK STRMERGE MATH=SOFT \
         IDIR=Include: IDIR=NDK3.2:Include

# Librairies SASC
LIBS   = LIB:sc.lib LIB:amiga.lib

OBJS   = SaferCopy.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINK) FROM $(OBJS) TO $(TARGET) LIB $(LIBS) SMALLCODE SMALLDATA

$(OBJS): $(SRC)
	$(CC) $(CFLAGS) $(SRC)

clean:
	-$(RM) $(OBJS) $(TARGET)

.PHONY: all clean
