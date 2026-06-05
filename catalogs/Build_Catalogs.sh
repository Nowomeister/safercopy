#!/bin/sh
# Build_Catalogs.sh - Compile all .ct files into binary .catalog files
# Run this on the Amiga or via a cross-tool that has catcomp.
#
# Usage: sh Build_Catalogs.sh
# Requires: catcomp (from ADE or NDK)
#
# Output: LOCALE:Catalogs/<lang>/SaferCopy.catalog

CD=../SaferCopy.cd

for lang in english français deutsch español nederlands dansk norsk \
            polski czech slovensko português català euskara türkçe \
            srpski russian greek roman; do
    CT="$lang/SaferCopy.ct"
    CAT="$lang/SaferCopy.catalog"
    if [ -f "$CT" ]; then
        echo "Building $CAT ..."
        catcomp "$CD" CATALOG "$CAT" TRANSLATION "$CT"
    else
        echo "SKIP: $CT not found"
    fi
done

echo "Done. Copy each SaferCopy.catalog to LOCALE:Catalogs/<lang>/"
