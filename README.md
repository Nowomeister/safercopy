Short:        Reliable file copy with VERIFY, DATES, MAXERR fix for Copy 47.7
Author:       Nowee (& Claude)
Uploader:     Nowee
Type:         util/shell
Version:      1.0
Requires:     AmigaOS 3.x, 68000+, ~1MB free RAM per BUF size
Architecture: m68k-amigaos

---

SaferCopy 1.0 - A reliable replacement for AmigaDOS Copy 47.7

BACKGROUND

  Copy 47.7 shipped with AmigaOS 3.2.3 silently ignores the VERIFY,
  DATES and BUF/BUFFER keywords documented in the AmigaDOS 3.2.3 manual.
  On USB stacks (Poseidon) and FAT95, this causes random incomplete file
  copies with no error reported - particularly destructive during large
  backups involving millions of files.

FIXES vs COPY 47.7

  - BUF/BUFFER  : actually uses the specified buffer size (default 512KB).
                  Critical for USB performance: reduces DOS calls from
                  700/sec to ~1/sec for a 500MB file.

  - DATES       : actually calls SetFileDate() when specified alone,
                  without requiring CLONE.

  - VERIFY      : actually re-reads the destination after every write and
                  compares byte-for-byte with the source. Corrupted files
                  are deleted immediately.

  - Partial write detection: checks Write() return value on every call.
                  Silently truncated files (common on FAT95/Poseidon) are
                  caught, logged, and the destination is deleted.

EXTRA FEATURES

  UPDATE        : skips files where destination exists with identical
                  size and datestamp. Re-copies if destination is smaller
                  (previous interrupted copy) or older than source.
                  Reports "[dest incomplete: X/Y bytes]" for visibility.

  MAXERR/K/N    : abort after N errors. Useful for unattended large copies
                  over unreliable media. Default 0 = no limit.

  Error buffering: errors print immediately AND are summarised at the end.
                  No need to watch the terminal for hours.

  FORCE         : strips write-protection from destination before copying.

TEMPLATE

  FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N,CLONE/S,DATES/S,
  NOPRO/S,VERIFY/S,NOREQ/S,UPDATE/S,FORCE/S,MAXERR/K/N

EXAMPLES

  Full backup with verification, preserve dates, 1MB buffer
    SaferCopy FROM Work: TO DH1:Backup/ ALL CLONE VERIFY BUF=1048576 NOREQ

  Incremental update, abort after 50 errors
    SaferCopy FROM Work: TO DH1:Backup/ ALL UPDATE VERIFY BUF=1048576 MAXERR=50

  USB to HD, big buffer for Poseidon performance
    SaferCopy FROM USB0:data/ TO DH1:data/ ALL CLONE VERIFY BUF=2097152 NOREQ

COMPILATION

  GCC : m68k-amigaos-gcc -O2 -m68000 -noixemul -Wall -o SaferCopy SaferCopy.c
  SASC: sc SaferCopy.c LINK MATH=SOFT NOSTKCHK OPT IDIR=Include: IDIR=NDK3.2:Include
